/*
  Copyright (C) 2013 Brian Harring

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, US 
*/
#include <stdlib.h>
#include <diffball/defs.h>
#include <string.h>
#include <sys/stat.h>
#include <diffball/dcbuffer.h>
#include <diffball/defs.h>
#include <cfile.h>
#include <diffball/bit-functions.h>
#include <diffball/tree.h>
#include <diffball/switching.h>

// Used only for the temp file machinery; get rid of this at that time.
#include <unistd.h>

static int flush_file_content_delta(CommandBuffer *dcbuff, cfile *patchf);
static int encode_fs_entry(cfile *patchf, multifile_file_data *entry);

static int
cWriteUBytesLE(cfile *cfh, unsigned long value, unsigned int len)
{
	unsigned char buff[16];
	writeUBytesLE(buff, value, len);
	if (len != cwrite(cfh, buff, len)) {
		v0printf("Failed writing %i bytes\n", len);
		return IO_ERROR;
	}
	return 0;
}

unsigned int
check_tree_magic(cfile *patchf)
{
	unsigned char buff[TREE_MAGIC_LEN + 1];
	cseek(patchf, 0, CSEEK_FSTART);
	if(TREE_MAGIC_LEN != cread(patchf, buff, TREE_MAGIC_LEN)) {
		return 0;
	} else if (memcmp(buff, TREE_MAGIC, TREE_MAGIC_LEN)!=0) {
		return 0;
	}
	return 2;
}

static int
flush_file_manifest(cfile *patchf, multifile_file_data **fs, unsigned long fs_count, const char *manifest_name)
{
	int err;
	// Identify and output the # of files that will be created.
	unsigned long x, file_count;
	for(file_count = 0, x = 0; x < fs_count; x++) {
		if (S_ISREG(fs[x]->st->st_mode) && !fs[x]->link_target) {
			file_count++;
		}
	}
	// The format here is 4 bytes for the # of files in this manifest, then:
	//   null delimited string
	//   8 bytes for the file size for that file.
	//
	v3printf("Recording %lu files in the delta manifest\n", file_count);
	cWriteUBytesLE(patchf, file_count, 4);
	for(x=0; file_count > 0; x++) {
		if (S_ISREG(fs[x]->st->st_mode) && !fs[x]->link_target) {

			v3printf("Recording file %s length %zi in the %s manifest\n", fs[x]->filename, fs[x]->st->st_size, manifest_name);
			size_t len = strlen(fs[x]->filename) + 1;
			if (len != cwrite(patchf, fs[x]->filename, len)) {
				v0printf("Failed writing %s file manifest\n", manifest_name);
				return IO_ERROR;
			}
			err = cWriteUBytesLE(patchf, fs[x]->st->st_size, 8);
			if (err) {
				return err;
			}
			file_count--;
		}
	}
	return 0;
}

static int
read_file_manifest(cfile *patchf, multifile_file_data ***fs, unsigned long *fs_count, const char *manifest_name)
{
	unsigned char buff[16];
	if (4 != cread(patchf, buff, 4)) {
		eprintf("Failed reading %s manifest count\n", manifest_name);
		return PATCH_TRUNCATED;
	}
	unsigned long file_count = readUBytesLE(buff, 4);
	*fs_count = file_count;
	int err = 0;
	multifile_file_data **results = calloc(sizeof(multifile_file_data *), file_count);
	if (!results) {
		eprintf("Failed allocating internal array for %s file manifest: %lu entries.\n", manifest_name, file_count);
		return MEM_ERROR;
	}
	unsigned long x;
	size_t position = 0;
	for (x = 0; x < file_count; x++) {
		results[x] = calloc(sizeof(multifile_file_data), 1);
		if (!results[x]) {
			file_count = x;
			err = MEM_ERROR;
			goto cleanup;
		}
		results[x]->filename = (char *)cfile_read_null_string(patchf);
		if (!results[x]->filename) {
			file_count = x +1;
			err = MEM_ERROR;
			goto cleanup;
		}
		results[x]->start = position;
		if (8 != cread(patchf, buff, 8)) {
			eprintf("Failed reading %s manifest count\n", manifest_name);
			file_count = x +1;
			err = PATCH_TRUNCATED;
			goto cleanup;
		}
		results[x]->end = position + readUBytesLE(buff, 8);
		position = results[x]->end;
		results[x]->st = calloc(sizeof(struct stat), 1);
		if (!results[x]->st) {
			eprintf("Failed allocating memory\n");
			err = MEM_ERROR;
			file_count = x + 1;
			goto cleanup;
		}
		v3printf("adding to %s manifest: %s length %zu\n", manifest_name, results[x]->filename, position - results[x]->start);
	}
	return 0;

	cleanup:
	for (x=0; x < file_count; x++) {
		if (results[x]->filename) {
			free(results[x]->filename);
		}
		if (results[x]->st) {
			free(results[x]->st);
		}
		free(results[x]);
	}
	free(results);
	return err;
}

signed int 
treeEncodeDCBuffer(CommandBuffer *dcbuff, cfile *patchf)
{
	int err;
	cwrite(patchf, TREE_MAGIC, TREE_MAGIC_LEN);
	cWriteUBytesLE(patchf, TREE_VERSION, TREE_VERSION_LEN);

	multifile_file_data **src_files = NULL, **ref_files = NULL;
	unsigned long src_count = 0, ref_count = 0;
	cfile *src_cfh = DCB_EXPOSE_COPY_CFH(dcbuff);
	cfile *ref_cfh = DCB_EXPOSE_ADD_CFH(dcbuff);
	if (multifile_expose_content(src_cfh, &src_files, &src_count)) {
		v0printf("Failed accessing multifile content for src\n");
		return DATA_ERROR;
	}
	if (multifile_expose_content(ref_cfh, &ref_files, &ref_count)) {
		v0printf("Failed accessing multifile content for ref\n");
		return DATA_ERROR;
	}

	// Dump the list of source files needed, then dump the list of files generated by this patch.
	err = flush_file_manifest(patchf, src_files, src_count, "source");
	if (err) {
		eprintf("Failed flushing ref files content\n");
		return err;
	}

	err = flush_file_manifest(patchf, ref_files, ref_count, "target");
	if (err) {
		eprintf("Failed flushing ref files content\n");
		return err;
	}

	v3printf("Flushed delta manifest; writing the delta now\n");
	// TODO: Move to a size estimate implementation for each encoding- use that to get the size here.

	err = flush_file_content_delta(dcbuff, patchf);
	if (err) {
		return err;
	}

	v3printf("Flushed the file content delta.  Writing magic, magic then command stream\n");
	if (TREE_INTERFILE_MAGIC_LEN != cwrite(patchf, TREE_INTERFILE_MAGIC, TREE_INTERFILE_MAGIC_LEN)) {
		v0printf("Failed flushing interfile magic\n");
		return IO_ERROR;
	}

	// Flush the command count.  It's number of ref_count entries + # of unlinks commands.
	v3printf("Flushing command count %lu\n", ref_count);
	err = cWriteUBytesLE(patchf, ref_count, 4);
	if (err) {
		return err;
	}

	unsigned long x;
	for(x=0; x < ref_count; x++) {
		err = encode_fs_entry(patchf, ref_files[x]);
		if (err) {
			return err;
		}
	}

	return 0;
}

static int
encode_fs_entry(cfile *patchf, multifile_file_data *entry)
{
	#define write_or_return(value, len) {int err=cWriteUBytesLE(patchf, (value), (len)); if (err) { return err; }; }

	#define write_common_block(st) \
		write_or_return((st)->st_uid, TREE_COMMAND_UID_LEN); \
		write_or_return((st)->st_gid, TREE_COMMAND_GID_LEN); \
		write_or_return((st)->st_mode, TREE_COMMAND_MODE_LEN); \
		write_or_return((st)->st_ctime, TREE_COMMAND_TIME_LEN); \
		write_or_return((st)->st_mtime, TREE_COMMAND_TIME_LEN);

#define write_null_string(value) \
{ int len=strlen((value)) + 1; if (len != cwrite(patchf, (value), len)) { v0printf("Failed writing string len %i\n", len); return IO_ERROR; }; }

	if (S_ISREG(entry->st->st_mode)) {
		if (!entry->link_target) {
			v3printf("writing manifest command for regular %s\n", entry->filename);
			write_or_return(TREE_COMMAND_REG, TREE_COMMAND_LEN);
			write_common_block(entry->st);
			// xattrs
			return 0;
		}
		v3printf("writing manifest command for hardlink %s\n", entry->filename);
		write_or_return(TREE_COMMAND_HARDLINK, TREE_COMMAND_LEN);
		write_null_string(entry->filename);
		write_null_string(entry->link_target);

	} else if (S_ISDIR(entry->st->st_mode)) {
		v3printf("writing manifest command for directory %s\n", entry->filename);
		write_or_return(TREE_COMMAND_DIR, TREE_COMMAND_LEN);
		write_null_string(entry->filename);
		write_common_block(entry->st);

	} else if (S_ISLNK(entry->st->st_mode)) {
		v3printf("writing manifest command for symlink %s\n", entry->filename);
		write_or_return(TREE_COMMAND_SYM, TREE_COMMAND_LEN);
		write_null_string(entry->filename);
		assert(entry->link_target);
		write_null_string(entry->link_target);
		write_common_block(entry->st);

	} else if (S_ISFIFO(entry->st->st_mode)) {
		v3printf("writing manifest command for fifo %s\n", entry->filename);
		write_or_return(TREE_COMMAND_FIFO, TREE_COMMAND_LEN);
		write_null_string(entry->filename);
		write_common_block(entry->st);

	} else if (S_ISCHR(entry->st->st_mode) || S_ISBLK(entry->st->st_mode)) {
		v3printf("writing manifest command for dev %s\n", entry->filename);
		write_or_return(TREE_COMMAND_DEV, TREE_COMMAND_LEN);
		write_null_string(entry->filename);
		write_common_block(entry->st);

	} else {
		v0printf("Somehow encountered an unknown fs entry: %s: %i\n", entry->filename, entry->st->st_mode);
		return DATA_ERROR;
	}
	return 0;

#undef write_null_string
#undef write_common_block
}

static int
flush_file_content_delta(CommandBuffer *dcbuff, cfile *patchf)
{
	cfile deltaf;
	char tmpname[] = "/tmp/differ.XXXXXX";
	unsigned char buff[16];
	int tmp_fd = mkstemp(tmpname);
	if (tmp_fd < 0) {
		v0printf("Failed getting a temp file\n");
		return IO_ERROR;
	}

	int err = copen_dup_fd(&deltaf, tmp_fd, 0, 0, NO_COMPRESSOR, CFILE_WONLY);
	if (err) {
		v0printf("Failed opening cfile handle to the tmpfile\n");
		close(tmp_fd);
		return IO_ERROR;
	}

	v3printf("Invoking switching format to encode the delta\n");
	err = switchingEncodeDCBuffer(dcbuff, &deltaf);
	if (err) {
		goto ERR_CFILE;
	}

	err = cclose(&deltaf);
	if (err) {
		v0printf("Failed closing deltaf handle\n");
		goto ERR_FD;
	}
	struct stat st;
	fstat(tmp_fd, &st);

	v3printf("File content delta is %lu bytes\n", st.st_size);
	if (cWriteUBytesLE(patchf, st.st_size, 8)) {
		v0printf("Failed writing delta length\n");
		err = IO_ERROR;
		goto ERR_FD;
	}

	err = copen_dup_fd(&deltaf, tmp_fd, 0, st.st_size, NO_COMPRESSOR, CFILE_RONLY);
	if (err) {
		v0printf("Failed reopening the tmp handle for reading\n");
		goto ERR_FD;
	}

	v3printf("Flushing delta to the patch\n");
	if (st.st_size != copy_cfile_block(patchf, &deltaf, 0, st.st_size)) {
		v0printf("Failed flushing in memory delta to the patch file\n");
		err = IO_ERROR;
		goto ERR_CFILE;
	}
	err = 0;

	ERR_CFILE:
		cclose(&deltaf);
	ERR_FD:
		unlink(tmpname);
		close(tmp_fd);
	return err;
}

static int
consume_command_chain(cfile *patchf, unsigned long command_count)
{
	struct stat st;
	unsigned char *filename = NULL;
	unsigned char *link_target = NULL;
	unsigned char buff[8];
	// This code assumes tree commands are a single byte; assert to catch if that ever changes.
	assert (TREE_COMMAND_LEN == 1);
	if(patchf->data.pos == patchf->data.end) {
		if(crefill(patchf) <= 0) {
			eprintf("Failed reading command %lu\n", command_count);
			return PATCH_TRUNCATED;
		}
	}
	#define read_string_or_return(value) \
		{ (value) = cfile_read_null_string(patchf); if (!(value)) { eprintf("Failed reading null string\n"); return PATCH_TRUNCATED; }; };

	#define read_or_return(value, len) \
		{ \
			if ((len) != cread(patchf, buff, (len))) { eprintf("Failed reading %i bytes\n", (len)); return PATCH_TRUNCATED; }; \
			(value) = readUBytesLE(buff, (len)); \
		}

	#define read_common_block(st) \
		read_or_return((st)->st_uid, TREE_COMMAND_UID_LEN); \
		read_or_return((st)->st_gid, TREE_COMMAND_GID_LEN); \
		read_or_return((st)->st_mode, TREE_COMMAND_MODE_LEN); \
		read_or_return((st)->st_ctime, TREE_COMMAND_TIME_LEN); \
		read_or_return((st)->st_mtime, TREE_COMMAND_TIME_LEN);


	unsigned char command_type = patchf->data.buff[patchf->data.pos];
	patchf->data.pos++;
	switch (command_type) {
		case TREE_COMMAND_REG:
			v3printf("command %lu: regular file\n", command_count);
			read_common_block(&st);
			break;
		case TREE_COMMAND_HARDLINK:
			v3printf("command %lu: hardlink\n", command_count);
			read_string_or_return(filename);
			read_string_or_return(link_target);
			break;
		case TREE_COMMAND_DIR:
			v3printf("command %lu: create directory\n", command_count);
			read_string_or_return(filename);
			read_common_block(&st);
			break;
		case TREE_COMMAND_SYM:
			v3printf("command %lu: create symlink\n", command_count);
			read_string_or_return(filename);
			read_string_or_return(link_target);
			read_common_block(&st);
			break;
		case TREE_COMMAND_FIFO:
			v3printf("command %lu: create fifo\n", command_count);
			read_string_or_return(filename);
			read_common_block(&st);
			break;
		case TREE_COMMAND_DEV:
			v3printf("command %lu: mknod dev\n", command_count);
			read_string_or_return(filename);
			read_common_block(&st);
			break;
		case TREE_COMMAND_UNLINK:
			v3printf("command %lu: unlink\n", command_count);
			read_string_or_return(filename);
			break;
		default:
			eprintf("command %lu: unknown command: %i\n", command_count, patchf->data.buff[patchf->data.pos]);
			return PATCH_CORRUPT_ERROR;
	}

	if (filename) {
		free(filename);
	}

	if (link_target) {
		free(link_target);
	}

	return 0;

	#undef read_or_return
	#undef read_string_or_return
	#undef read_common_block
}

signed int 
treeReconstruct(cfile *patchf, cfile *target_directory)
{
	unsigned char buff[16];
	multifile_file_data **src_files = NULL, **ref_files = NULL;
	unsigned long src_count = 0, ref_count = 0;

	if(TREE_MAGIC_LEN != cseek(patchf, TREE_MAGIC_LEN, CSEEK_FSTART)) {
		eprintf("Failed seeking beyond the format magic\n");
		return PATCH_TRUNCATED;
	}
	if(TREE_VERSION_LEN != cread(patchf, buff, TREE_VERSION_LEN)) {
		eprintf("Failed reading version identifier\n");
		return PATCH_TRUNCATED;
	}
	unsigned int ver = readUBytesLE(buff, TREE_VERSION_LEN);
	v2printf("patch format ver=%u\n", ver);

//	add_id = DCB_REGISTER_VOLATILE_ADD_SRC(dcbuff, patchf, NULL, 0);
//	ref_id = src_id;
	v3printf("Reading src file manifest\n");

	int err = read_file_manifest(patchf, &src_files, &src_count, "source");
	if (err) {
		return err;
	}
	err = read_file_manifest(patchf, &ref_files, &ref_count, "target");
	if (err) {
		return err;
	}

	if (8 != cread(patchf, buff, 8)) {
		eprintf("Failed reading delta length\n");
		return PATCH_TRUNCATED;
	}
	unsigned long delta_size = readUBytesLE(buff, 8);
	size_t delta_start = ctell(patchf, CSEEK_FSTART);
	if (delta_start + delta_size != cseek(patchf, delta_size, CSEEK_CUR)) {
		eprintf("Failed seeking past the delta\n");
		return PATCH_TRUNCATED;
	}

	assert(TREE_INTERFILE_MAGIC_LEN < sizeof(buff));
	if (TREE_INTERFILE_MAGIC_LEN != cread(patchf, buff, TREE_INTERFILE_MAGIC_LEN)) {
		eprintf("Failed reading intrafile magic in patch file at position %zu\n", delta_size + delta_start);
		return PATCH_TRUNCATED;
	}
	if (memcmp(buff, TREE_INTERFILE_MAGIC, TREE_INTERFILE_MAGIC_LEN) != 0) {
		eprintf("Failed to verify intrafile magic in patch file at position %zu; likely corrupted\n", delta_size + delta_start);
		return PATCH_CORRUPT_ERROR;
	}
	v3printf("Starting tree command stream at %zu\n", ctell(patchf, CSEEK_FSTART));

	if (4 != cread(patchf, buff, 4)) {
		eprintf("Failed reading command count\n");
		return PATCH_TRUNCATED;
	}
	unsigned long x=0, command_count = readUBytesLE(buff, 4);
	v3printf("command stream is %lu commands\n", command_count);

	for (x = 0; x < command_count; x++) {
		err = consume_command_chain(patchf, x);
		if (err) {
			return err;
		}
	}

	return 0;

}
