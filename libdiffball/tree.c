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
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <diffball/dcbuffer.h>
#include <diffball/defs.h>
#include <cfile.h>
#include <diffball/bit-functions.h>
#include <diffball/tree.h>
#include <diffball/switching.h>
#include <diffball/formats.h>
#include <diffball/api.h>

// Used only for the temp file machinery; get rid of this at that time.
#include <unistd.h>


typedef struct {
	uid_t uid;
	gid_t gid;
	mode_t mode;
	// The index to write/read for this particular pairing.  This is tracked w/ the intent
	// to allow the most common uid/gid/mode to be recorded as a \0 in the delta- which
	// will likely be able to be RLE'd alongside matching size attributes.
	unsigned long index;
} ugm_tuple;

typedef struct {
	ugm_tuple *array;
	unsigned long count;
	unsigned int byte_size;
} ugm_table;


void enforce_no_trailing_slash(char *ptr);
#define ERETURN(value) { if ((value) != 0) { eprintf("%s:%i Exiting due to nonzero return: %i\n", __FILE__, __LINE__, (int)(value)); }; return (value) ; };

static int flush_file_content_delta(CommandBuffer *dcbuff, cfile *patchf);
static int encode_fs_entry(cfile *patchf, multifile_file_data *entry, ugm_table *table);
static int encode_unlink(cfile *patchf, multifile_file_data *entry);
static signed long
generate_unlinks(cfile *patchf, multifile_file_data **src, unsigned long *src_pos, unsigned long src_count, multifile_file_data *ref_entry,
				 int dry_run);
static int enforce_standard_attributes(const char *path, const struct stat *st, mode_t extra_flags);
static int enforce_standard_attributes_via_path(const char *path, const struct stat *st, int skip_mode);
static int enforce_directory(const char *path, const struct stat *st);
static int enforce_symlink(const char *path, const char *link_target, const struct stat *st);

static int consume_command_chain(const char *target_directory, const char *tmpspace, cfile *patchf,
	multifile_file_data **ref_files, char **final_paths, unsigned long ref_count, unsigned long *ref_pos,
	ugm_table *table,
	unsigned long command_count);

// used with fstatat if available
#ifndef AT_NO_AUTOMOUNT
#define AT_NO_AUTOMOUNT 0
#endif


static char *
concat_path(const char *directory, const char *frag)
{
	size_t dir_len = strlen(directory);
	size_t frag_len = strlen(frag);
	char *p = malloc(dir_len + frag_len + 1);
	if (p) {
		memcpy(p, directory, dir_len);
		memcpy(p + dir_len, frag, frag_len);
		p[dir_len + frag_len] = 0;
	}
	return p;
}

static int
cWriteUBytesLE(cfile *cfh, unsigned long value, unsigned int len)
{
	unsigned char buff[16];
	writeUBytesLE(buff, value, len);
	if (len != cwrite(cfh, buff, len)) {
		v0printf("Failed writing %i bytes\n", len);
		ERETURN(IO_ERROR);
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
cmp_ugm_tuple(const void *item1, const void *item2)
{
	ugm_tuple *desired = (ugm_tuple *)item1;
	ugm_tuple *item = (ugm_tuple *)item2;

	int result = desired->uid - item->uid;
	if (result)
		return result;
	result = desired->gid - item->gid;
	if (result)
		return result;
	return desired->mode - item->mode;
}

static int
cmp_ugm_index(const void *item1, const void *item2)
{
	return ((ugm_tuple *)item1)->index - ((ugm_tuple *)item2)->index;
}

static inline ugm_tuple *
search_ugm_table(ugm_table *table, uid_t uid, gid_t gid, mode_t mode)
{
	ugm_tuple key;
	key.uid = uid;
	key.gid = gid;
	key.mode = (07777 & mode);
	return bsearch(&key, table->array, table->count, sizeof(ugm_tuple), cmp_ugm_tuple);
}

static inline void
free_ugm_table(ugm_table *table)
{
	if (table->array) {
		free(table->array);
	}
	free(table);
}

static int
compute_and_flush_ugm_table(cfile *patchf, multifile_file_data **fs, unsigned long fs_count, ugm_table **resultant_table)
{
	unsigned long table_size = 32;

	ugm_table *table = calloc(1, sizeof(ugm_table));
	if (table) {
		table->array = calloc(table_size, sizeof(ugm_tuple));
	}
	if (!table || !table->array) {
		if (table) {
			free_ugm_table(table);
		}
		eprintf("Failed allocating UGM table\n");
		ERETURN(MEM_ERROR);
	}

	// Note; we use the index field as a temporary histo count as we're building the table.
	// After we've finished the walk, we convert that down into a proper index, making the
	// resultant table searchable by uid/gid/mode pairing.

	unsigned long x;
	for (x = 0; x < fs_count; x++) {
		ugm_tuple *match = search_ugm_table(table, fs[x]->st->st_uid, fs[x]->st->st_gid, fs[x]->st->st_mode);
		if (match) {
			match->index++;
			continue;
		}

		// Insert the new item, then sort.
		if (table->count == table_size) {
			ugm_tuple *tmp_array = realloc(table->array, sizeof(ugm_tuple) * table_size * 2);
			if (!tmp_array) {
				free_ugm_table(table);
				eprintf("Allocation failed\n");
				ERETURN(MEM_ERROR);
			}

			table->array = tmp_array;
			memset(table->array + table_size, 0, sizeof(ugm_tuple) * table_size);
			table_size *= 2;
		}
		table->array[table->count].uid = fs[x]->st->st_uid;
		table->array[table->count].gid = fs[x]->st->st_gid;
		table->array[table->count].mode = (07777 & fs[x]->st->st_mode);
		table->count++;
		qsort(table->array, table->count, sizeof(ugm_tuple), cmp_ugm_tuple);
	}

	table->byte_size = unsignedBytesNeeded(table->count ? table->count -1 : 0);

	if (cWriteUBytesLE(patchf, table->count, 4)) {
		eprintf("Failed writing the uid/gid/mode table; out of space?\n");
		free_ugm_table(table);
		ERETURN(IO_ERROR);
	}

	// Sort by greatest count first, resetting the index, and flushing while we're at it.
	// Mark the index, then return.
	qsort(table->array, table->count, sizeof(ugm_tuple), cmp_ugm_index);
	for (x = 0; x < table->count; x++) {
		table->array[x].index = x;
		if (cWriteUBytesLE(patchf, table->array[x].uid, TREE_COMMAND_UID_LEN) ||
			cWriteUBytesLE(patchf, table->array[x].uid, TREE_COMMAND_GID_LEN) ||
			cWriteUBytesLE(patchf, table->array[x].mode, TREE_COMMAND_MODE_LEN)) {
			eprintf("Failed writing the uid/gid/mode table; out of space?\n");
			free_ugm_table(table);
			ERETURN(IO_ERROR);
		}
		v3printf("UGM table %lu: uid(%i), gid(%i), mode(%i)\n", x, table->array[x].uid, table->array[x].gid, table->array[x].mode);
	}

	qsort(table->array, table->count, sizeof(ugm_tuple), cmp_ugm_tuple);

	*resultant_table = table;

	return 0;
}

static int
consume_ugm_table(cfile *patchf, ugm_table **resultant_table)
{
	unsigned char buff[TREE_COMMAND_UID_LEN + TREE_COMMAND_GID_LEN + TREE_COMMAND_MODE_LEN];

	ugm_table *table = calloc(1, sizeof(ugm_table));
	if (!table) {
		eprintf("Failed allocating ugm table\n");
		ERETURN(MEM_ERROR);
	}

	if (4 != cread(patchf, buff, 4)) {
		eprintf("Failed reading ugm table header\n");
		free(table);
		ERETURN(PATCH_TRUNCATED);
	}
	table->count = readUBytesLE(buff, 4);

	v3printf("Reading %lu entries in the UID/GID/Mode table\n", table->count);

	table->array = calloc(table->count, sizeof(ugm_tuple));
	if (!table->array) {
		free(table);
		eprintf("Failed allocating ugm table\n");
		ERETURN(MEM_ERROR);
	}

	unsigned long x;
	for (x = 0; x < table->count; x++) {
		if (sizeof(buff) != cread(patchf, buff, sizeof(buff))) {
			eprintf("Failed reading ugm table at index %lu\n", x);
			free_ugm_table(table);
			ERETURN(PATCH_TRUNCATED);
		}
		table->array[x].uid = readUBytesLE(buff, TREE_COMMAND_UID_LEN);
		table->array[x].gid = readUBytesLE(buff + TREE_COMMAND_UID_LEN, TREE_COMMAND_GID_LEN);
		// Limit the mask to protect against any old screwups.
		table->array[x].mode = (07777 & readUBytesLE(buff + TREE_COMMAND_UID_LEN + TREE_COMMAND_GID_LEN, TREE_COMMAND_MODE_LEN));
		table->array[x].index = 0;
	}

	table->byte_size = unsignedBytesNeeded(table->count ? table->count -1 : 0);
	*resultant_table = table;
	return 0;
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
				ERETURN(IO_ERROR);
			}
			err = cWriteUBytesLE(patchf, fs[x]->st->st_size, 8);
			if (err) {
				ERETURN(err);
			}
			file_count--;
		}
	}
	return 0;
}

static int
read_file_manifest(cfile *patchf, multifile_file_data ***fs, unsigned long *fs_count, const char *manifest_name)
{
    v3printf("Reading %s file manifest\n", manifest_name);
	unsigned char buff[16];
	if (4 != cread(patchf, buff, 4)) {
		eprintf("Failed reading %s manifest count\n", manifest_name);
		ERETURN(PATCH_TRUNCATED);
	}
	unsigned long file_count = readUBytesLE(buff, 4);
	*fs_count = file_count;
	int err = 0;
	multifile_file_data **results = calloc(sizeof(multifile_file_data *), file_count);
	if (!results) {
		eprintf("Failed allocating internal array for %s file manifest: %lu entries.\n", manifest_name, file_count);
		ERETURN(MEM_ERROR);
	}
	*fs = results;
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
		results[x]->st = calloc(sizeof(struct stat), 1);
		if (!results[x]->st) {
			eprintf("Failed allocating memory\n");
			err = MEM_ERROR;
			file_count = x + 1;
			goto cleanup;
		}
		results[x]->st->st_size = readUBytesLE(buff, 8);
		results[x]->st->st_mode = S_IFREG | 0600;
		position = results[x]->end = position + results[x]->st->st_size;
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
	ERETURN(err);
}

signed int 
treeEncodeDCBuffer(CommandBuffer *dcbuff, cfile *patchf)
{
	int err;
	ugm_table *ugm_table = NULL;
	cwrite(patchf, TREE_MAGIC, TREE_MAGIC_LEN);
	cWriteUBytesLE(patchf, TREE_VERSION, TREE_VERSION_LEN);

	multifile_file_data **src_files = NULL, **ref_files = NULL;
	unsigned long src_count = 0, ref_count = 0;
	cfile *src_cfh = DCB_EXPOSE_COPY_CFH(dcbuff);
	cfile *ref_cfh = DCB_EXPOSE_ADD_CFH(dcbuff);
	if (multifile_expose_content(src_cfh, &src_files, &src_count)) {
		v0printf("Failed accessing multifile content for src\n");
		ERETURN(DATA_ERROR);
	}
	if (multifile_expose_content(ref_cfh, &ref_files, &ref_count)) {
		v0printf("Failed accessing multifile content for ref\n");
		ERETURN(DATA_ERROR);
	}

	// Dump the list of source files needed, then dump the list of files generated by this patch.
	err = flush_file_manifest(patchf, src_files, src_count, "source");
	if (err) {
		eprintf("Failed flushing ref files content\n");
		ERETURN(err);
	}

	err = flush_file_manifest(patchf, ref_files, ref_count, "target");
	if (err) {
		eprintf("Failed flushing ref files content\n");
		ERETURN(err);
	}

	v3printf("Flushed delta manifest; writing the delta now\n");
	// TODO: Move to a size estimate implementation for each encoding- use that to get the size here.

	err = flush_file_content_delta(dcbuff, patchf);
	if (err) {
		ERETURN(err);
	}

	v3printf("Flushed the file content delta.  Writing magic, magic then command stream\n");
	if (TREE_INTERFILE_MAGIC_LEN != cwrite(patchf, TREE_INTERFILE_MAGIC, TREE_INTERFILE_MAGIC_LEN)) {
		v0printf("Failed flushing interfile magic\n");
		ERETURN(IO_ERROR);
	}

	// Compute and flush the UGM table.
	err = compute_and_flush_ugm_table(patchf, ref_files, ref_count, &ugm_table);
	if (err) {
		ERETURN(err);
	}

	unsigned long command_count = ref_count;
	unsigned long ref_pos, src_pos;
	signed long unlink_result;
	for(ref_pos = 0, src_pos = 0; ref_pos < ref_count; ref_pos++) {
		unlink_result = generate_unlinks(patchf, src_files, &src_pos, src_count, ref_files[ref_pos], 1);
		if (unlink_result < 0) {
			eprintf("Failed identification of unlinks\n");
			free_ugm_table(ugm_table);
			ERETURN(unlink_result);
		}
		command_count += unlink_result;
	}
	unlink_result = generate_unlinks(patchf, src_files, &src_pos, src_count, NULL, 1);
	if (unlink_result < 0) {
		eprintf("Failed tail end of identification of unlinks\n");
		free_ugm_table(ugm_table);
		ERETURN(unlink_result);
	} else {
		command_count += unlink_result;
	}

	// Flush the command count.  It's number of ref_count entries + # of unlinks commands.
	v3printf("Flushing command count %lu\n", command_count);
	err = cWriteUBytesLE(patchf, command_count, 4);
	if (err) {
		free_ugm_table(ugm_table);
		ERETURN(err);
	}

	for(ref_pos = 0, src_pos = 0; ref_pos < ref_count; ref_pos++) {
		unlink_result = generate_unlinks(patchf, src_files, &src_pos, src_count, ref_files[ref_pos], 0);
		if (unlink_result >= 0) {
			err = encode_fs_entry(patchf, ref_files[ref_pos], ugm_table);
		} else {
			err = unlink_result;
		}
		if (err) {
			free_ugm_table(ugm_table);
			ERETURN(err);
		}
	}

	// Flush all remaining unlinks; anything remaining in the src must be wiped since it's
	// not in the ref.
	unlink_result = generate_unlinks(patchf, src_files, &src_pos, src_count, NULL, 0);
	if (unlink_result < 0) {
		err = unlink_result;
	}
	free_ugm_table(ugm_table);
	ERETURN(err);
}

static signed long
generate_unlinks(cfile *patchf, multifile_file_data **src, unsigned long *src_pos, unsigned long src_count, multifile_file_data *ref_entry, int dry_run)
{
	unsigned long unlink_count = 0;
	while (*src_pos < src_count) {
		if (ref_entry) {
			int result = strcmp(src[*src_pos]->filename, ref_entry->filename);
			if (result > 0) {
				// Our src file is still greater than ref; no deletes doable until the ref is further along.
				return unlink_count;
			} else if (result == 0) {
				// Our src file is the ref file.  advance, but ignore it.
				(*src_pos)++;
				return unlink_count;
			}
		}
		if (!dry_run) {
			int err = encode_unlink(patchf, src[*src_pos]);
			if (err) {
				ERETURN(IO_ERROR);
			}
		}
		(*src_pos)++;
		unlink_count++;
	}
	return unlink_count;
}

static int
encode_unlink(cfile *patchf, multifile_file_data *entry)
{
	v3printf("Writing unlink command for %s\n", entry->filename);
	int err = cWriteUBytesLE(patchf, TREE_COMMAND_UNLINK, TREE_COMMAND_LEN);
	if (!err) {
		int len = strlen(entry->filename) + 1;
		if (len != cwrite(patchf, entry->filename, len)) {
			err = IO_ERROR;
		}
	}
	if (err) {
		eprintf("Failed writing unlink command for %s to the patch\n", entry->filename);
	}
	ERETURN(err);
}

static int
encode_fs_entry(cfile *patchf, multifile_file_data *entry, ugm_table *table)
{
	#define write_or_return(value, len) {int err=cWriteUBytesLE(patchf, (value), (len)); if (err) { ERETURN(err); }; }

	#define write_common_block(st) \
		{ ugm_tuple *match = search_ugm_table(table, (st)->st_uid, (st)->st_gid, (st)->st_mode); \
		  assert(match); \
		  write_or_return(match->index, table->byte_size); \
		} \
		write_or_return((st)->st_ctime, TREE_COMMAND_TIME_LEN); \
		write_or_return((st)->st_mtime, TREE_COMMAND_TIME_LEN);

#define write_null_string(value) \
{ int len=strlen((value)) + 1; if (len != cwrite(patchf, (value), len)) { v0printf("Failed writing string len %i\n", len); ERETURN(IO_ERROR); }; }

	if (S_ISREG(entry->st->st_mode)) {
		if (!entry->link_target) {
			v3printf("writing manifest command for regular %s\n", entry->filename);
			write_or_return(TREE_COMMAND_REG, TREE_COMMAND_LEN);
			write_common_block(entry->st);
			// xattrs
			return 0;
		}
		v3printf("writing manifest command for hardlink %s -> %s\n", entry->filename, entry->link_target);
		write_or_return(TREE_COMMAND_HARDLINK, TREE_COMMAND_LEN);
		write_null_string(entry->filename);
		write_null_string(entry->link_target);

	} else if (S_ISDIR(entry->st->st_mode)) {
		v3printf("writing manifest command for directory %s\n", entry->filename);
		write_or_return(TREE_COMMAND_DIR, TREE_COMMAND_LEN);
		write_null_string(entry->filename);
		write_common_block(entry->st);

	} else if (S_ISLNK(entry->st->st_mode)) {
		v3printf("writing manifest command for symlink %s -> %s\n", entry->filename, entry->link_target);
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
		write_or_return(
			(S_ISCHR(entry->st->st_mode) ? TREE_COMMAND_CHR : TREE_COMMAND_BLK),
			TREE_COMMAND_LEN);
		write_null_string(entry->filename);
		write_common_block(entry->st);
		write_or_return(major(entry->st->st_dev), TREE_COMMAND_DEV_LEN);
		write_or_return(minor(entry->st->st_dev), TREE_COMMAND_DEV_LEN);

	} else if (S_ISSOCK(entry->st->st_mode)) {
		v3printf("writing manifest command for socket %s\n", entry->filename);
		write_or_return(TREE_COMMAND_SOCKET, TREE_COMMAND_LEN);
		write_null_string(entry->filename);
		write_common_block(entry->st);

	} else {
		v0printf("Somehow encountered an unknown fs entry: %s: %i\n", entry->filename, entry->st->st_mode);
		ERETURN(DATA_ERROR);
	}
	return 0;

#undef write_null_string
#undef write_common_block
}

static int
flush_file_content_delta(CommandBuffer *dcbuff, cfile *patchf)
{
	cfile deltaf;
	memset(&deltaf, 0, sizeof(cfile));
	char tmpname[] = "/tmp/differ.XXXXXX";
	unsigned char buff[16];
	int tmp_fd = mkstemp(tmpname);
	if (tmp_fd < 0) {
		v0printf("Failed getting a temp file\n");
		ERETURN(IO_ERROR);
	}

	int err = copen_dup_fd(&deltaf, tmp_fd, 0, 0, NO_COMPRESSOR, CFILE_WONLY);
	if (err) {
		v0printf("Failed opening cfile handle to the tmpfile\n");
		close(tmp_fd);
		ERETURN(IO_ERROR);
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
	ERETURN(err);
}

static int
enforce_recursive_unlink(DIR *directory)
{
	struct dirent *entry;
	int err = 0;
	while ((entry = readdir(directory))) {
		if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, "..")) {
			continue;
		}
		int is_dir = 0;
		if (entry->d_type == DT_DIR) {
			is_dir = 1;
		} else if (entry->d_type == DT_UNKNOWN) {
			struct stat st;
			if (fstatat(dirfd(directory), entry->d_name, &st, AT_NO_AUTOMOUNT|AT_SYMLINK_NOFOLLOW)) {
				closedir(directory);
				ERETURN(IO_ERROR);
			}
			is_dir = S_ISDIR(st.st_mode);
		}
		if (is_dir) {
			int fd = openat(dirfd(directory), entry->d_name, O_DIRECTORY);
			if (fd >= 0) {
				DIR *subdir = fdopendir(fd);
				if (subdir) {
					err = enforce_recursive_unlink(subdir);
					closedir(subdir);
					if (!err) {
						err = unlinkat(dirfd(directory), entry->d_name, AT_REMOVEDIR);
					}
				} else {
					close(fd);
					err = IO_ERROR;
				}
			} else {
				err = IO_ERROR;
			}
		} else {
			err = unlinkat(dirfd(directory), entry->d_name, 0);
		}

		if (err) {
			break;
		}
	}
	ERETURN(err);
}

static int
enforce_unlink(const char *path)
{
	v3printf("Removing content at %s\n", path);
	int err = unlink(path);
	if (-1 == err) {
		if (ENOENT == errno) {
			err = 0;
			errno = 0;
		} else if (EISDIR == errno) {
			DIR *directory = opendir(path);
			if (directory) {
				err = enforce_recursive_unlink(directory);
				closedir(directory);
				if (!err) {
					err = rmdir(path);
				}
			}
		}
	}
	ERETURN(err);
}

static int
enforce_standard_attributes(const char *path, const struct stat *st, mode_t extra_flags)
{
	int fd = open(path, O_RDONLY | O_NOFOLLOW | extra_flags);
	if (-1 == fd) {
		eprintf("Failed opening expected pathway %s\n", path);
		ERETURN(IO_ERROR);
	}
	int err = fchmod(fd, st->st_mode);

	if (!err) {
		err = fchown(fd, st->st_uid, st->st_gid);
		if (!err) {
			struct timeval times[2] = {{st->st_ctime, 0}, {st->st_mtime, 0}};
			err = futimes(fd, times);
			if (err) {
				eprintf("Failed futimes for %s: errno %i\n", path, errno);
			}
		} else {
			eprintf("Failed fchown'ing for %s: errno %i\n", path, errno);
		}
	} else {
		eprintf("Failed fchmod'ing permissions for %s: errno %i\n", path, errno);
	}
	close(fd);
	ERETURN(err);
}

static int
enforce_standard_attributes_via_path(const char *path, const struct stat *st, int skip_mode)
{
	v3printf("Enforcing permissions on %s via path\n", path);
	int err = 0;
	if (!skip_mode) {
		err = chmod(path, st->st_mode);
		if (err) {
			eprintf("Chmod of %s failed: errno=%i\n", path, errno);
		}
	}
	if (!err) {
		err = lchown(path, st->st_uid, st->st_gid);
		if (!err) {
			struct timeval times[2] = {{st->st_ctime, 0}, {st->st_mtime, 0}};
			err = lutimes(path, times);
			if (err) {
				eprintf("lutimes of %s failed: errno=%i\n", path, errno);
			}
		} else {
			eprintf("lchown of %s failed: errno=%i\n", path, errno);
		}
	}
	ERETURN(err);
}

static int
enforce_directory(const char *path, const struct stat *st)
{
	v3printf("Creating directory at %s\n", path);
	int err = mkdir(path, st->st_mode);
	if (-1 == err) {
		if (EEXIST == errno) {
			struct stat ondisk_st;
			if (0 != lstat(path, &ondisk_st)) {
				eprintf("Race occurred checking %s: errno %i\n", path, errno);
				ERETURN(IO_ERROR);
			} else if (!S_ISDIR(ondisk_st.st_mode)) {
				v3printf("Removing blocking content at %s\n", path);
				err = unlink(path);
				if (!err) {
					err = mkdir(path, st->st_mode);
				}
			} else {
				err = 0;
			}
		} else if (ENOTDIR == errno) {
			err = unlink(path);
			if (!err) {
				err = mkdir(path, st->st_mode);
			}
		}
	}

	if (!err) {
		// Note, this doesn't guarantee mtime if we go screwing around w/in a directory after the command.
		// Need to track/sort that somehow.
		err = enforce_standard_attributes(path, st, O_DIRECTORY);
	}
	ERETURN(err);
}

static int
enforce_symlink(const char *path, const char *link_target, const struct stat *st)
{
	v3printf("Creating symlink at %s\n", path);
	int err = symlink(link_target, path);
	if (-1 == err && EEXIST == errno) {
		v3printf("Removing blocking content at %s\n", path);
		err = enforce_unlink(path);
		if (!err) {
			err = symlink(link_target, path);
		}
	}

	if (!err) {
		err = enforce_standard_attributes_via_path(path, st, 1);
	}
	ERETURN(err);
}

static int
enforce_file_move(const char *trg, const char *src, const struct stat *st)
{
	v3printf("Transferring reconstructed file %s to %s\n", src, trg);
	int err = rename(src, trg);
	if (!err) {
		err = enforce_standard_attributes(trg, st, 0);
	}
	ERETURN(err);
}

static int
enforce_hardlink(const char *path, const char *link_target)
{
	v3printf("Enforcing hardlink of %s -> %s\n", path, link_target);
	int err = link(link_target, path);
	if (-1 == err && EEXIST == errno) {
		errno = 0;
		err = enforce_unlink(path);
		if (!err) {
			err = link(path, link_target);
		}
	}
	ERETURN(err);
}

static int
enforce_mknod(const char *path, mode_t type, unsigned long major, unsigned long minor, const struct stat *st)
{
	v3printf("Mknod type %i for %s\n", type, path);
	dev_t dev = makedev(major, minor);
	mode_t mode = st->st_mode | type;
	int err = mknod(path, mode, dev);
	if (-1 == err && EEXIST == errno) {
		errno = 0;
		err = enforce_unlink(path);
		if (!err) {
			err = mknod(path, mode, dev);
		}
	}
	if (!err) {
		err = enforce_standard_attributes_via_path(path, st, 0);
	}
	ERETURN(err);
}

static int
enforce_trailing_slash(char **ptr)
{
	size_t len = strlen(*ptr);
	if (len == 0 || (*ptr)[len -1] != '/') {
		char *p = realloc(*ptr, len + 2);
		if (!p) {
			eprintf("Somehow encountered realloc failure for string of desired size %zi\n", len + 2);
			ERETURN(MEM_ERROR);
		}
		(*ptr)[len] = '/';
		(*ptr)[len + 1] = 0;
		*ptr = p;
		return 0;
	}
	return 0;
}

void
enforce_no_trailing_slash(char *ptr)
{
	size_t len = strlen(ptr);
	while (len && ptr[len -1] == '/') {
		ptr[len -1] = 0;
		len--;
	}
}

static int
consume_command_chain(const char *target_directory, const char *tmpspace, cfile *patchf,
    multifile_file_data **ref_files, char **final_paths, unsigned long ref_count, unsigned long *ref_pos,
    ugm_table *table,
    unsigned long command_count)
{
	int err = 0;
	struct stat st;
	char *filename = NULL;
	char *abs_filepath = NULL;
	char *link_target = NULL;
	unsigned long ugm_index;
	unsigned char buff[8];

	// This code assumes tree commands are a single byte; assert to catch if that ever changes.
	assert (TREE_COMMAND_LEN == 1);
	if(patchf->data.pos == patchf->data.end) {
		if(crefill(patchf) <= 0) {
			eprintf("Failed reading command %lu\n", command_count);
			ERETURN(PATCH_TRUNCATED);
		}
	}
	#define read_string_or_return(value) \
		{ (value) = (char *)cfile_read_null_string(patchf); if (!(value)) { eprintf("Failed reading null string\n"); ERETURN(PATCH_TRUNCATED); }; };

	#define read_or_return(value, len) \
		{ \
			if ((len) != cread(patchf, buff, (len))) { eprintf("Failed reading %i bytes\n", (len)); ERETURN(PATCH_TRUNCATED); }; \
			(value) = readUBytesLE(buff, (len)); \
		}

	#define read_common_block(st) \
		read_or_return(ugm_index, table->byte_size); \
		(st).st_uid = table->array[ugm_index].uid; \
		(st).st_gid = table->array[ugm_index].gid; \
		(st).st_mode = table->array[ugm_index].mode; \
		read_or_return((st).st_ctime, TREE_COMMAND_TIME_LEN); \
		read_or_return((st).st_mtime, TREE_COMMAND_TIME_LEN);

	#define enforce_or_fail(command, args...) \
		{ \
			abs_filepath = concat_path(target_directory, filename); \
			if (abs_filepath) { \
				err = command(abs_filepath, args); \
			} else { \
				eprintf("Failed allocating filepath.\n"); \
				err = MEM_ERROR; \
			} \
		}


	unsigned char command_type = patchf->data.buff[patchf->data.pos];
	patchf->data.pos++;

	int is_chr = 0;

	switch (command_type) {
		case TREE_COMMAND_REG:
			v3printf("command %lu: regular file\n", command_count);
			if ((*ref_pos) == ref_count) {
				eprintf("Encountered a file command, but no more recontruction targets were defined by this patch.  Likely corruption or internal bug\n");
				ERETURN(PATCH_CORRUPT_ERROR);
			}
			read_common_block(st);
			char *src = concat_path(tmpspace, ref_files[*ref_pos]->filename);
			char *abs_filepath = concat_path(target_directory, final_paths[*ref_pos]);
			if (src && abs_filepath) {
				err = enforce_file_move(abs_filepath, src, &st);
			} else {
				eprintf("Failed allocating memory for link target and filepath\n");
				err = MEM_ERROR;
			}
			if (src) {
				free(src);
			}

			(*ref_pos)++;
			break;

		case TREE_COMMAND_HARDLINK:
			v3printf("command %lu: hardlink\n", command_count);
			read_string_or_return(filename);
			read_string_or_return(link_target);
			char *abs_link_target = concat_path(target_directory, link_target);
			if (abs_link_target) {
				enforce_or_fail(enforce_hardlink, abs_link_target);
				free(abs_link_target);
			} else {
				eprintf("Failed allocating memory for link target\n");
				err = MEM_ERROR;
			}
			break;

		case TREE_COMMAND_DIR:
			v3printf("command %lu: create directory\n", command_count);
			read_string_or_return(filename);
			// Strip the trailing slash; if our enforce_directory needs to
			// resort to lstating, a trailing '/' results in the call incorrectly
			// failing w/ ENOTDIR .
			enforce_no_trailing_slash(filename);
			read_common_block(st);
			enforce_or_fail(enforce_directory, &st);
			break;

		case TREE_COMMAND_SYM:
			v3printf("command %lu: create symlink\n", command_count);
			read_string_or_return(filename);
			read_string_or_return(link_target);
			read_common_block(st);
			enforce_or_fail(enforce_symlink, link_target, &st);
			break;

		case TREE_COMMAND_FIFO:
			v3printf("command %lu: create fifo\n", command_count);
			read_string_or_return(filename);
			read_common_block(st);
			enforce_or_fail(enforce_mknod, S_IFIFO, 0, 0, &st);
			break;

		case TREE_COMMAND_CHR:
			is_chr = 1;
			// intentional fall through.

		case TREE_COMMAND_BLK:
			v3printf("command %lu: mknod dev, is_chr? == %i\n", command_count, is_chr);
			read_string_or_return(filename);
			read_common_block(st);
			unsigned long major = 0, minor = 0;
			read_or_return(major, TREE_COMMAND_DEV_LEN);
			read_or_return(minor, TREE_COMMAND_DEV_LEN);
			enforce_or_fail(enforce_mknod, is_chr ? S_IFCHR : S_IFBLK, major, minor, &st);
			break;

		case TREE_COMMAND_SOCKET:
			v3printf("command %lu: socket\n", command_count);
			read_string_or_return(filename);
			read_common_block(st);
			enforce_or_fail(enforce_mknod, S_IFSOCK, 0, 0, &st);
			break;

		case TREE_COMMAND_UNLINK:
			v3printf("command %lu: unlink\n", command_count);
			read_string_or_return(filename);

			abs_filepath = concat_path(target_directory, filename);
			if (abs_filepath) {
				err = enforce_unlink(abs_filepath);
			} else {
				eprintf("Failed allocating filepath.\n");
				err = MEM_ERROR;
			}
			break;

		default:
			eprintf("command %lu: unknown command: %i\n", command_count, patchf->data.buff[patchf->data.pos]);
			ERETURN(PATCH_CORRUPT_ERROR);
	}

	if (abs_filepath) {
		free(abs_filepath);
	}

	if (filename) {
		free(filename);
	}

	if (link_target) {
		free(link_target);
	}

	ERETURN(err);

	#undef enforce_or_fail
	#undef read_or_return
	#undef read_string_or_return
	#undef read_common_block
}

static char *
make_tempdir(const char *tmp_directory)
{
	if (NULL == tmp_directory) {
		tmp_directory = getenv("TMPDIR");
		if (!tmp_directory) {
			tmp_directory = "/tmp";
		}
	}
	const size_t tmp_len = strlen(tmp_directory);

	const char template_frag[] = "delta-XXXXXX";
	const size_t template_frag_len = strlen(template_frag);

	char *template = malloc(tmp_len + 3 + template_frag_len);
	if (!template) {
		eprintf("Failed allocating memory for a temp dir\n");
		return NULL;
	}
	memcpy(template, tmp_directory, tmp_len);
	template[strlen(tmp_directory)] = '/';
	memcpy(template + tmp_len + 1, template_frag, template_frag_len);
	template[tmp_len + template_frag_len + 1] = 0;
	char *result = mkdtemp(template);
	if (!result) {
		free(template);
	}
	// Tweak the results, enforcing a trailing '/';
	template[tmp_len + template_frag_len + 1] = '/';
	template[tmp_len + template_frag_len + 2] = 0;
	return template;
}

static int
build_and_swap_tmpspace_array(char ***final_paths_ptr, multifile_file_data **ref_files, unsigned long ref_count)
{
	char **final_paths = (char **)calloc(sizeof(char *), ref_count);
	if (!final_paths) {
		eprintf("Failed allocating memory for tmp paths\n");
		ERETURN(1);
	}
	*final_paths_ptr = final_paths;

	// Swap in the tmp pathways, building an array we use for moving files
	// as we encounter the command in the stream.
	size_t chars_needed = 1;
	unsigned long x;
	for (x = ref_count; x > 0; x /= 10) {
		chars_needed++;
	}

	char buf[chars_needed];

	for (x = 0; x < ref_count; x++) {
		int len = snprintf(buf, chars_needed, "%lu", x);
		assert (len <= chars_needed);
		char *p = strdup(buf);
		if (!p) {
			eprintf("Failed allocating memory\n");
			ERETURN(1);
		}

		final_paths[x] = ref_files[x]->filename;
		ref_files[x]->filename = p;
	}
	return 0;
}

static int
rebuild_files_from_delta(cfile *src_cfh, cfile *containing_patchf, cfile *out_cfh, size_t delta_start, size_t delta_length)
{
	cfile deltaf;
	unsigned char *buff = NULL;
	memset(&deltaf, 0, sizeof(cfile));
	int err;
	if (containing_patchf->compressor_type != NO_COMPRESSOR) {
		v1printf("libcfile doesn't support windowing through compression; pulling the delta into memory: %zi bytes\n", delta_length);
		buff = malloc(delta_length);
		if (!buff) {
			eprintf("Couldn't allocate necessary memory to pull the patch into memory\n");
			ERETURN(MEM_ERROR);
		}
		if (delta_length != cread(containing_patchf, buff, delta_length)) {
			eprintf("Failed reading the patch into memory; truncated or corrupted?\n");
			ERETURN(PATCH_TRUNCATED);
		}
		err = copen_mem(&deltaf, buff, delta_length, NO_COMPRESSOR, CFILE_RONLY);
	} else {
		err = copen_child_cfh(&deltaf, containing_patchf, delta_start, delta_start + delta_length, NO_COMPRESSOR, CFILE_RONLY);
	}
	if (err) {
		eprintf("Failed opening cfile for the embedded delta: window was %zu to %zu\n", delta_start, delta_start + delta_length);
		if (buff) {
			free(buff);
		}
		ERETURN(err);
	}

	cfile *delta_array[1] = {&deltaf};
	err = simple_reconstruct(src_cfh, delta_array, 1, out_cfh, SWITCHING_FORMAT, 0xffff);
	cclose(&deltaf);
	if (!err) {
		cseek(containing_patchf, delta_start + delta_length, CSEEK_FSTART);
	}
	if (buff) {
		free(buff);
	}
	ERETURN(err);
}

signed int 
treeReconstruct(const char *src_directory, cfile *patchf, const char *raw_directory, const char *tmp_directory)
{
	cfile src_cfh, trg_cfh;
	memset(&src_cfh, 0, sizeof(cfile));
	memset(&trg_cfh, 0, sizeof(cfile));
	int err = 0;

	unsigned char buff[16];
	char *target_directory = NULL;
	multifile_file_data **src_files = NULL, **ref_files = NULL;
	unsigned long src_count = 0, ref_count = 0;
	unsigned long x;
	char **final_paths = NULL;
	char *tmpspace = NULL;
	ugm_table *table = NULL;
	mode_t original_umask = umask(0000);

	if(TREE_MAGIC_LEN != cseek(patchf, TREE_MAGIC_LEN, CSEEK_FSTART)) {
		eprintf("Failed seeking beyond the format magic\n");
		umask(original_umask);
		ERETURN(PATCH_TRUNCATED);
	}
	if(TREE_VERSION_LEN != cread(patchf, buff, TREE_VERSION_LEN)) {
		eprintf("Failed reading version identifier\n");
		umask(original_umask);
		ERETURN(PATCH_TRUNCATED);
	}
	unsigned int ver = readUBytesLE(buff, TREE_VERSION_LEN);
	v2printf("patch format ver=%u\n", ver);

//	add_id = DCB_REGISTER_VOLATILE_ADD_SRC(dcbuff, patchf, NULL, 0);
//	ref_id = src_id;
	v3printf("Reading src file manifest\n");

	err = read_file_manifest(patchf, &src_files, &src_count, "source");
	if (err) {
		umask(original_umask);
		ERETURN(err);
	}

	err = copen_multifile(&src_cfh, src_directory, src_files, src_count, CFILE_RONLY);
	if (err) {
		eprintf("Failed open source directory %s: err %i\n", src_directory, err);
		goto cleanup;
	}

	v3printf("Starting verification of src manifest\n");
	err = multifile_ensure_files(&src_cfh, 0);
	if (err) {

		goto cleanup;
	}

	err = read_file_manifest(patchf, &ref_files, &ref_count, "target");
	if (err) {
		goto cleanup;
	}

	target_directory = strdup(raw_directory);
	if (!target_directory || enforce_trailing_slash(&target_directory)) {
		eprintf("allocation errors encountered\n");
		err = MEM_ERROR;
		goto cleanup;
	}

	tmpspace = make_tempdir(tmp_directory);
	if (!tmpspace) {
		eprintf("Failed creating tmpdir for reconstructed files\n");
		goto cleanup;
	}

	v3printf("Creating temp file array, and on disk files\n");
	if (build_and_swap_tmpspace_array(&final_paths, ref_files, ref_count)) {
		eprintf("Failed allocating tmpspace array\n");
		goto cleanup;
	}

	err = copen_multifile(&trg_cfh, tmpspace, ref_files, ref_count, CFILE_WONLY);
	if (err) {
		eprintf("Failed opening temp space directory %s: err %i\n", tmpspace, err);
		goto cleanup;
	}

	// Create our files now.
	err = multifile_ensure_files(&trg_cfh, 1);
	if (err) {
		eprintf("Failed creating temporary files for reconstruction\n");
		goto cleanup;
	}

	if (8 != cread(patchf, buff, 8)) {
		eprintf("Failed reading delta length\n");
		err = PATCH_TRUNCATED;
		goto cleanup;
	}

	unsigned long delta_size = readUBytesLE(buff, 8);
	size_t delta_start = ctell(patchf, CSEEK_FSTART);
	err = rebuild_files_from_delta(&src_cfh, patchf, &trg_cfh, delta_start, delta_size);
	if (err) {
		eprintf("Failed regenerating new files from the delta: err %i\n", err);
		goto cleanup;
	}

	// Flush the output handle; ensure all content is on disk.
	err = cflush(&trg_cfh);

	assert(TREE_INTERFILE_MAGIC_LEN < sizeof(buff));
	if (TREE_INTERFILE_MAGIC_LEN != cread(patchf, buff, TREE_INTERFILE_MAGIC_LEN)) {
		eprintf("Failed reading intrafile magic in patch file at position %zu\n", delta_size + delta_start);
		err = PATCH_TRUNCATED;
		goto cleanup;
	}
	if (memcmp(buff, TREE_INTERFILE_MAGIC, TREE_INTERFILE_MAGIC_LEN) != 0) {
		eprintf("Failed to verify intrafile magic in patch file at position %zu; likely corrupted\n", delta_size + delta_start);
		err = PATCH_CORRUPT_ERROR;
		goto cleanup;
	}
	v3printf("Loading UID/GID/Mode table at %zu\n", ctell(patchf, CSEEK_FSTART));

	err = consume_ugm_table(patchf, &table);
	if (err) {
		goto cleanup;
	}

	v3printf("Starting tree command stream at %zu\n", ctell(patchf, CSEEK_FSTART));

	if (4 != cread(patchf, buff, 4)) {
		eprintf("Failed reading command count\n");
		err = PATCH_TRUNCATED;
		goto cleanup;
	}
	unsigned long command_count = readUBytesLE(buff, 4);
	v3printf("command stream is %lu commands\n", command_count);

	unsigned long file_pos = 0;
	for (x = 0; x < command_count; x++) {
		err = consume_command_chain(target_directory, tmpspace, patchf, ref_files, final_paths, ref_count, &file_pos, table, x);
		if (err) {
			goto cleanup;
		}
	}

	v1printf("Reconstruction completed successfully\n");
	err = 0;

	cleanup:
	if (target_directory) {
		free(target_directory);
	}

	if (final_paths) {
		for (x=0; x < ref_count; x++) {
			if (final_paths[x]) {
				free(final_paths[x]);
			}
		}
		free(final_paths);
	}

	if (tmpspace) {
		err = enforce_unlink(tmpspace);
		if (err) {
			eprintf("Failed cleaning up temp directory %s\n", tmpspace);
		}
		free(tmpspace);
	}

	if (cfile_is_open(&src_cfh)) {
		cclose(&src_cfh);
	} else if (src_files) {
		multifile_free_file_data_array(src_files, src_count);
		free(src_files);
	}

	if (cfile_is_open(&trg_cfh)) {
		cclose(&trg_cfh);
	} else if (ref_files) {
		multifile_free_file_data_array(ref_files, ref_count);
		free(ref_files);
	}
	umask(original_umask);
	ERETURN(err);
}
