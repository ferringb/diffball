/*
  Copyright (C) 2003-2013 Brian Harring

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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "internal.h"
#include <string.h>
#include <fcntl.h>
#include <dirent.h>


typedef struct {
	// Directory root for all files that were given.  Guranteed to have a trailig /
	char *root;
	int root_len;

	multifile_file_data **fs;


	// The # of files (regardless of type).
	unsigned long fs_count;

	// An FD to whatever current_fs_index points at, or -1 if no file is open currently.
	int active_fd;

	// The current FS index- this always points at an actual file.
	unsigned long current_fs_index;
	int flags;
} multifile_data;

static int multifile_ensure_open_active(cfile *cfh, multifile_data *data, ssize_t data_offset);

static char *
strdup_ensure_trailing_slash(const char *src_directory)
{
	char *directory = NULL;
	int dir_len = strlen(src_directory);
	if (src_directory[dir_len] != '/') {
		directory = malloc(dir_len + 2);
		if(directory) {
			memcpy(directory, src_directory, dir_len);
			directory[dir_len] = '/';
			dir_len++;
			directory[dir_len] = 0;
		}
		return directory;
	}
	return strdup(src_directory);
}

static char *
readlink_dup(char *filepath, struct stat *st)
{
	char *linkname;
	ssize_t r;

	if (lstat(filepath, st) == -1) {
		eprintf("Failed readlink'ing %s; did the FS change?\n", filepath);
		return NULL;
	}

	linkname = malloc(st->st_size + 1);
	if (linkname == NULL) {
		eprintf("Failed allocating memory for symlink target of len %zu: %s\n", st->st_size +1, filepath);
		return NULL;
	}

	r = readlink(filepath, linkname, st->st_size + 1);

	if (r == -1 || r > st->st_size) {
		eprintf("Lstat failed: link changed under foot: %s\n", filepath);
		return NULL;
	}

    linkname[r] = '\0';
	return linkname;
}

void
get_filepath(multifile_data *data, unsigned long file_pos, char *buf)
{
	if (data->root_len) {
		memcpy(buf, data->root, data->root_len);
		buf += data->root_len;
	}
	strcpy(buf, data->fs[file_pos]->filename);
}

void
multifile_close_active_fd(multifile_data *data)
{
	if (data->active_fd != -1) {
		close(data->active_fd);
		data->active_fd = -1;
	}
}

static int
bsearch_cmp_offset(const void *key, const void *array_item)
{
	size_t offset = *((size_t *)key);
	multifile_file_data *item = *((multifile_file_data **)array_item);
	if (offset < item->start) {
		return -1;
	} else if (offset < item->end) {
		return 0;
	}
	return 1;
}

int
set_file_index(multifile_data *data, size_t offset)
{
	assert(offset <= data->fs[data->fs_count -1]->end);
	multifile_file_data **match = (multifile_file_data **)bsearch(&offset, data->fs, data->fs_count, sizeof(multifile_file_data *), bsearch_cmp_offset);
	if (!match) {
		if (offset != data->fs[data->fs_count -1]->end) {
			eprintf("Somehow received NULL from bsearch for multifile: offset %li\n", offset);
			return 1;
		}
		match = data->fs + (data->fs_count - 1);
	}
	multifile_close_active_fd(data);
	data->current_fs_index = match - data->fs;
	assert (data->current_fs_index < data->fs_count);
	return 0;
}

int
bsearch_cmp_filename(const void *key, const void *array_item)
{
	const char *filename = (const char *)key;
	multifile_file_data *item = *((multifile_file_data **)array_item);
	return strcmp(filename, item->filename);
}

multifile_file_data *
multifile_find_file(const char *filename, multifile_file_data **array, unsigned long fs_count)
{
	multifile_file_data **match = (multifile_file_data **)bsearch(filename, array, fs_count, sizeof(multifile_file_data *), bsearch_cmp_filename);
	if (match) {
		return match[0];
	}
	return NULL;
}

void
multifile_free_file_data_array(multifile_file_data **array, unsigned long count)
{
	while (count > 0) {
		count--;
		if (array[count]->filename) {
			free(array[count]->filename);
		}
		if (array[count]->st) {
			free(array[count]->st);
		}
		free(array[count]);
	}
}

unsigned int
cclose_multifile(cfile *cfh, void *raw)
{
	multifile_data *data = (multifile_data *)raw;
	if (data) {
		multifile_close_active_fd(data);
		multifile_free_file_data_array(data->fs, data->fs_count);
		data->fs_count = 0;
		free(data->fs);
		free(data->root);
		free(data);
	}
	return 0;
}

ssize_t
cseek_multifile(cfile *cfh, void *raw, ssize_t orig_offset, ssize_t data_offset, int offset_type)
{
	dcprintf("requested to cseek to %lu\n", data_offset);
	multifile_data *data = (multifile_data *)raw;
	cfh->data.pos = 0;
	cfh->data.end = 0;

	int err = multifile_ensure_open_active(cfh, data, data_offset);
	if (err) {
		return err;
	}
	cfh->data.offset = data_offset;
	return (CSEEK_ABS==offset_type ? data_offset + cfh->data.window_offset :
		data_offset);
}

static int
multifile_ensure_open_active(cfile *cfh, multifile_data *data, ssize_t data_offset)
{
	char buf[PATH_MAX];
	assert (data_offset >= 0);
	if (data_offset > cfh->data.window_len || data->fs_count == 0) {
		eprintf("Asked for an offset beyond the end of the window; returning EOF\n");
		return EOF_ERROR;
	}
	data_offset = data_offset + cfh->data.window_offset;
	if (data_offset >= data->fs[data->current_fs_index]->end || data_offset < data->fs[data->current_fs_index]->start) {
		// This requires a new file; jump to that file.
		if (set_file_index(data, data_offset)) {
			return (cfh->err = IO_ERROR);
		}
	}
	if (data->active_fd == -1) {
		get_filepath(data, data->current_fs_index, buf);
		if (cfh->access_flags & CFILE_WONLY) {
			data->active_fd = open(buf, O_NOFOLLOW|O_WRONLY);
		} else {
			data->active_fd = open(buf, O_NOFOLLOW|O_RDONLY);
		}
		if (data->active_fd == -1) {
			eprintf("Failed opening multifile %s; errno %i\n", buf, errno);
			return 1;
		}
	}
	size_t desired = data_offset - data->fs[data->current_fs_index]->start;
	if (desired != lseek(data->active_fd, desired, SEEK_SET)) {
		eprintf("Somehow lseek w/in refill failed\n");
		return (cfh->err = IO_ERROR);
	}
	return 0;
}

int
crefill_multifile(cfile *cfh, void *raw)
{
	multifile_data *data = (multifile_data *)raw;
	cfh->data.offset += cfh->data.end;
	cfh->data.end = cfh->data.pos = 0;

	int err = multifile_ensure_open_active(cfh, data, cfh->data.offset);
	if (err) {
		return err;
	}

	assert(cfh->data.offset <= cfh->data.window_len);

	// Read either the max data.size, all that is in the file, or the remaining allowed window.
	size_t desired = MIN(cfh->data.window_len - cfh->data.offset, data->fs[data->current_fs_index]->end - data->fs[data->current_fs_index]->start);
	desired = MIN(desired, cfh->data.size);

	assert(desired <= cfh->data.window_len);
	if (desired == 0) {
		return EOF_ERROR;
	}
	ssize_t result = read(data->active_fd, cfh->data.buff, desired);
	if (result >= 0) {
		cfh->data.end = result;
	} else {
		eprintf("got nonzero read: errno %i for %s\n", errno, data->fs[data->current_fs_index]->filename);
		return (cfh->err = IO_ERROR);
	}
	assert(cfh->data.offset + result <= cfh->data.window_len);
	dcprintf("crefill_multifile: %u: no_compress, got %lu\n", cfh->cfh_id, cfh->data.end);
	return 0;
}

ssize_t
cflush_multifile(cfile *cfh, void *raw)
{
	multifile_data *data = (multifile_data *)raw;

	// Note the seek to offset + write_start; the point there is so that we skip any segment that
	// the client didn't write to.  That way we're not writing nulls to the FS unless told to do so-
	// allows for sparse, basically.
	// Additionally, note that this may cross a file boundary- in which case we have to separate the
	// writes, working our way till there is no data left to flush.

	while (cfh->data.write_end > cfh->data.write_start) {
		int err = multifile_ensure_open_active(cfh, data, cfh->data.offset + cfh->data.write_start);
		if (err) {
			return err;
		}
		size_t desired = MIN(data->fs[data->current_fs_index]->end, cfh->data.write_end + cfh->data.offset + cfh->data.window_offset);
		desired -= cfh->data.offset + cfh->data.write_start;

		if (desired != write(data->active_fd, cfh->data.buff + cfh->data.write_start, desired)) {
			eprintf("Failed writing to %s\n", data->fs[data->current_fs_index]->filename);
			return IO_ERROR;
		}
		cfh->data.write_start += desired;
	}
	cfh->data.offset += cfh->data.write_end;
	cfh->data.write_end = cfh->data.write_start = cfh->data.pos = cfh->data.end = 0;
	return 0;
}

int
copen_multifile(cfile *cfh, const char *root, multifile_file_data **files, unsigned long fs_count, unsigned int access_flags)
{
	if (cfile_is_open(cfh)) {
		eprintf("copen_multifile: cfh is already open, failing\n");
		return UNSUPPORTED_OPT;
	}

	int result = 0;
	if ((access_flags & CFILE_WR) == CFILE_WR) {
		eprintf("multifile doesn't currently support read and write support; only one or other\n");
		return UNSUPPORTED_OPT;
	} else if (!(access_flags & CFILE_WR)) {
		eprintf("no access mode was given\n");
		return UNSUPPORTED_OPT;
	}
	memset(cfh, 0, sizeof(cfile));
	multifile_data *data = (multifile_data *)calloc(sizeof(multifile_data), 1);
	if (!data) {
		result = MEM_ERROR;
		goto cleanup;
	}
	data->root = strdup_ensure_trailing_slash(root);
	if (!data->root) {
		eprintf("Memory allocation failed\n");
		goto cleanup;
	}
	data->root_len = strlen(data->root);
	data->fs = files;
	data->fs_count = fs_count;
	data->active_fd = -1;

	size_t position = 0;
	unsigned long x = 0;
	for (; x < fs_count; x++) {
		data->fs[x]->start = position;
		// ignore all secondary hardlinks.
		if (data->fs[x]->link_target == NULL && S_ISREG(data->fs[x]->st->st_mode)) {
			position += data->fs[x]->st->st_size;
		}
		data->fs[x]->end = position;
	}

	result = internal_copen(cfh, -1, 0, position, 0, position, NO_COMPRESSOR, access_flags);
	if (result) {
		goto cleanup;
	}

	cfh->io.seek = cseek_multifile;
	cfh->io.refill = crefill_multifile;
	cfh->io.flush = cflush_multifile;
	cfh->io.close = cclose_multifile;
	cfh->io.data = (void *)data;
	cfh->state_flags |= CFILE_CHILD_INHERITS_IO;

	return 0;

cleanup:
	if (data) {
		cclose_multifile(cfh, data);
	}
	return result;
}	

int
multifile_recurse_directory(const char *root, const char *directory,
	multifile_file_data **files[], unsigned long *files_count, unsigned long *files_size,
    multifile_directory_filter filter_func, void *filter_data)
{
	DIR *the_dir;
	struct dirent *entry;
	char buf[PATH_MAX];
	strcpy(buf, root);
	char *directory_start = buf + strlen(root);
	char *start = directory_start;
	if (directory) {
		strcpy(directory_start, directory);
		start += strlen(directory);
	}

	if (!(the_dir = opendir(buf))) {
		eprintf("multifile: Failed opening directory %s, errno %i\n", directory, errno);
		return 1;
	}

	while ((entry = readdir(the_dir))) {
		const char *name = entry->d_name;
		if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) {
			// . or .. ; ignore.
			continue;
		}
		start[0] = 0;
		strcpy(start, name);
		struct stat *st = malloc(sizeof(struct stat));
		if (!st) {
			eprintf("multifile: Failed mallocing stat structure\n");
			return 1;
		}

		if (lstat(buf, st)) {
			eprintf("multifile: Failed lstating %s; errno %i\n", buf, errno);
			return 1;
		}

		if (filter_func) {
			int result = filter_func(filter_data, directory_start -1 , st);
			if (result == -1) {
				eprintf("multifile: error code from the filter function for %s: %i\n", directory_start, result);
				return 1;
			} else if (result == 1) {
				v1printf("multifile: filtering %s\n", directory_start);
				free(st);
				continue;
			}
		}
		if (S_ISDIR(st->st_mode)){
			strcat(start, "/");;
			if(multifile_recurse_directory(root, directory_start, files, files_count, files_size, filter_func, filter_data)) {
				return 1;
			}
		}

		if (*files_size == *files_count) {
			if (*files_size == 0) {
				*files_size = 16;
			}
			multifile_file_data **tmp = realloc(*files, sizeof(multifile_file_data *) * ((*files_size) * 2));
			if (!tmp) {
				eprintf("multifile: failed reallocing files array to size %lu\n", ((*files_size) * 2));
				return 1;
			}
			*files = tmp;
			*files_size *= 2;
		}
		multifile_file_data *entry = (multifile_file_data *)calloc(sizeof(multifile_file_data), 1);
		if (!entry) {
			eprintf("multifile: failed allocing file_data entry\n");
			return 1;
		}

		if (S_ISLNK(st->st_mode)) {
			entry->link_target = readlink_dup(buf, st);
			if (!entry->link_target) {
				return 1;
			}
		} else {
			entry->link_target = NULL;
		}

		(*files)[*files_count] = entry;
		entry->filename = strdup(directory_start);
		entry->st = st;
		if (!entry->filename) {
			eprintf("multifile: failed strdup for %s\n", buf);
			return 1;
		}
		*files_count += 1;
	}
	closedir(the_dir);
	return 0;
}		

static int
cmpstrcmp(const void *p1, const void *p2)
{
	multifile_file_data *i1 = *((multifile_file_data **)p1);
	multifile_file_data *i2 = *((multifile_file_data **)p2);
	return strcmp(i1->filename, i2->filename);
}

static int
cmp_hardlinks(const void *p1, const void *p2)
{
	multifile_file_data *i1 = *((multifile_file_data **)p1);
	multifile_file_data *i2 = *((multifile_file_data **)p2);
	#define icmp(x, y) if ((x) < (y)) { return -1; } else if ((x) > (y)) { return 1; }
	icmp(i1->st->st_dev, i2->st->st_dev);
	icmp(i1->st->st_ino, i2->st->st_ino);
	#undef icmp
	return strcmp(i1->filename, i2->filename);
}

int
copen_multifile_directory(cfile *cfh, const char *src_directory, multifile_directory_filter filter_func, void *filter_data)
{
	multifile_file_data **files = NULL;
	unsigned long files_count = 0;
	unsigned long files_size = 0;

	char *directory = strdup_ensure_trailing_slash(src_directory);

	if (!directory) {
		eprintf("multifile: directory dup mem allocaiton failed\n");
		return 1;
	}
	if (multifile_recurse_directory(directory, NULL, &files, &files_count, &files_size, filter_func, filter_data)) {
		free(directory);
		return 1;
	}
	qsort(files, files_count, sizeof(multifile_file_data *), cmp_hardlinks);
	unsigned long i;
	for (i=1; i < files_count; i++) {
		if (files[i -1]->st->st_dev == files[i]->st->st_dev &&
			files[i -1]->st->st_ino == files[i]->st->st_ino) {
			files[i]->link_target = files[i -1]->filename;
			dcprintf("hardlink found: %s to %s\n", files[i]->filename, files[i]->link_target);
		}
	}
	qsort(files, files_count, sizeof(multifile_file_data *), cmpstrcmp);

	int err = copen_multifile(cfh, directory, files, files_count, CFILE_RONLY);
	free(directory);
	if (err) {
		multifile_free_file_data_array(files, files_count);
		free(files);
	}
	return err;
}

int
multifile_expose_content(cfile *cfh, multifile_file_data ***results, unsigned long *fs_count)
{
	multifile_data *data = (multifile_data *)cfh->io.data;
	*results = data->fs;
	*fs_count = data->fs_count;
	return 0;
}

int
multifile_ensure_files(cfile *cfh, int allow_creation, int report_all_failures)
{
	char buff[PATH_MAX];
	if (!cfile_is_open(cfh)) {
		eprintf("cfile is not open\n");
		return UNSUPPORTED_OPT;
	}

	multifile_data *data = (multifile_data *)cfh->io.data;

	int flags = O_NOFOLLOW;
	if (cfh->access_flags & CFILE_WONLY) {
		flags |= O_CREAT|O_WRONLY;
	} else {
		flags |= O_RDONLY;
	}

	unsigned long x;
	int err = 0;
	for (x = 0; (report_all_failures || !err) && x < data->fs_count; x++) {
		get_filepath(data, x, buff);
		size_t desired = data->fs[x]->end - data->fs[x]->start;
		int fd = open(buff, flags, 0600);
		if (-1 == fd) {
			eprintf("Failed opening %s: errno %i\n", buff, errno);
			err = IO_ERROR;
			continue;
		}
		if (cfh->access_flags & CFILE_WONLY) {
			if (-1 == ftruncate(fd, desired)) {
				eprintf("Failed truncating file %s to %zu length\n", buff, desired);
				err = IO_ERROR;
			}
		} else {
			struct stat st;
			if (!fstat(fd, &st)) {
				if (st.st_size != desired) {
					eprintf("file %s is size %zu, expected %zu\n", buff, st.st_size, desired);
					err = IO_ERROR;
				}
			} else {
				eprintf("Failed fstating %s: errno %i\n", buff, errno);
				err = IO_ERROR;
			}
		}
		close(fd);
	}
	return err;
}
