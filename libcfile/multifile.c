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
#include <sys/stat.h>
#include "internal.h"
#include <string.h>
#include <fcntl.h>
#include <dirent.h>

#include <linux/limits.h>

typedef struct {
	// Directory root for all files that were given.  Guranteed to have a trailig /
	char *root;
	int root_len;

	// The individual files themselves.
	multifile_file_data **files;
	unsigned long file_count;

	int active_fd;
	unsigned long current_file_index;
	int flags;
} multifile_data;

void
get_filepath(multifile_data *data, unsigned long file_pos, char *buf)
{
	if (data->root_len) {
		memcpy(buf, data->root, data->root_len);
		buf += data->root_len;
	}
	strcpy(buf, data->files[file_pos]->file);
}

int
bsearch_compar(const void *key, const void *array_item)
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

void
multifile_close_active_fd(multifile_data *data)
{
	if (data->active_fd != -1) {
		close(data->active_fd);
		data->active_fd = -1;
	}
}

int
set_file_index(multifile_data *data, size_t offset)
{
	assert(offset <= data->files[data->file_count -1]->end);
	multifile_file_data **match = (multifile_file_data **)bsearch(&offset, data->files, data->file_count, sizeof(multifile_file_data *), bsearch_compar);
	if (!match) {
		eprintf("Somehow received NULL from bsearch for multifile: offset %li\n", offset);
		return 1;
	}
	multifile_close_active_fd(data);
	data->current_file_index = match - data->files;
	assert (data->current_file_index < data->file_count);
	return 0;
}

unsigned int
cclose_multifile(cfile *cfh, void *raw)
{
	multifile_data *data = (multifile_data *)raw;
	if (data) {
		multifile_close_active_fd(data);
		while (data->file_count > 0){
			data->file_count--;
			free(data->files[data->file_count]->file);
			free(data->files[data->file_count]->st);
			free(data->files[data->file_count]);
		}
		free(data->files);
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
	if (data_offset >= data->files[data->current_file_index]->end || data_offset < data->files[data->current_file_index]->start) {
		// This requires a new file; jump to that file.
		if (set_file_index(data, data_offset)) {
			return (cfh->err = IO_ERROR);
		}
	}
	if(multifile_ensure_open_active(data)) {
		return (cfh->err = IO_ERROR);
	}
	cfh->data.offset = data_offset;
	size_t desired = data_offset - data->files[data->current_file_index]->start;
	if (desired != lseek(data->active_fd, desired, SEEK_SET)) {
		eprintf("Somehow failed to lseek to %lu for %s\n", desired, data->files[data->current_file_index]->file);
		return (cfh->err = IO_ERROR);
	}
	// Check this; CSEEK_ABS behaviour may be retarded.
	return data_offset;
}

int
multifile_ensure_open_active(multifile_data *data)
{
	char buf[PATH_MAX];
	if (data->active_fd == -1) {
		get_filepath(data, data->current_file_index, buf);
		data->active_fd = open(buf, O_NOFOLLOW|O_RDONLY);
		if (data->active_fd == -1) {
			eprintf("Failed opening multifile %s; errno %i\n", buf, errno);
			return 1;
		}
	}
	return 0;
}

int
crefill_multifile(cfile *cfh, void *raw)
{
	multifile_data *data = (multifile_data *)raw;
	cfh->data.offset += cfh->data.end;
	cfh->data.end = cfh->data.pos = 0;

	unsigned long current = data->current_file_index;

	// Seek forward for the next file.
	// This should be a single step, but zero length files may be in
	// the list- as such we use a while loop here to skip them.
	while (cfh->data.offset >= data->files[current]->end) {
		if (current + 1 == data->file_count) {
			cfh->state_flags |= CFILE_EOF;
			cfh->data.offset += cfh->data.end;
			cfh->data.pos = cfh->data.end = 0;
			return 0;
		}
		current++;
	}
	if (current != data->current_file_index) {
		data->current_file_index = current;
		multifile_close_active_fd(data);
	}
	if (multifile_ensure_open_active(data)) {
		return (cfh->err = IO_ERROR);
	}
	size_t desired = cfh->data.offset - data->files[data->current_file_index]->start;
	if (desired != lseek(data->active_fd, desired, SEEK_SET)) {
		eprintf("Somehow lseek w/in refill failed\n");
		return (cfh->err = IO_ERROR);
	}
	ssize_t result = read(data->active_fd, cfh->data.buff,
		MIN(cfh->data.size, data->files[data->current_file_index]->end - cfh->data.offset));
	if (result >= 0) {
		cfh->data.end = result;
	} else {
		eprintf("got nonzero read: errno %i for %s\n", errno, data->files[data->current_file_index]->file);
		return (cfh->err = IO_ERROR);
	}
	dcprintf("crefill_multifile: %u: no_compress, got %lu\n", cfh->cfh_id, cfh->data.end);
	return 0;
}

int
copen_multifile(cfile *cfh, char *root, multifile_file_data **files, unsigned long file_count)
{
	int result = 0;
	memset(cfh, 0, sizeof(cfile));
	multifile_data *data = (multifile_data *)calloc(sizeof(multifile_data), 1);
	if (!data) {
		result = MEM_ERROR;
		goto cleanup;
	}
	data->root = root;
	data->root_len = strlen(root);
	data->files = files;
	data->file_count = file_count;
	data->active_fd = -1;

	char buf[PATH_MAX];
	size_t position = 0;
	unsigned long x = 0;
	strcpy(buf, root);
	for (; x < file_count; x++) {
		get_filepath(data, x, buf);
		data->files[x]->start = position;
		// ignore all secondary hardlinks.
		if (!data->files[x]->hardlink && S_ISREG(data->files[x]->st->st_mode)) {
			position += data->files[x]->st->st_size;
		}
		data->files[x]->end = position;
	}

	result = internal_copen(cfh, -1, 0, position, 0, 0, NO_COMPRESSOR, CFILE_RONLY);
	if (result) {
		goto cleanup;
	}

	cfh->io.seek = cseek_multifile;
	cfh->io.refill = crefill_multifile;
	cfh->io.close = cclose_multifile;
	cfh->io.data = (void *)data;
	
	return 0;

cleanup:
	if (data) {
		cclose_multifile(cfh, data);
	} else {
		free(root);
		free(files);
	}
	return result;
}	

int
multifile_recurse_directory(const char *root, const char *directory, multifile_file_data **files[], unsigned long *files_count, unsigned long *files_size)
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

		if (S_ISDIR(st->st_mode)){
			strcat(start, "/");;
			if(multifile_recurse_directory(root, directory_start, files, files_count, files_size)) {
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

		(*files)[*files_count] = entry;
		entry->file = strdup(directory_start);
		entry->st = st;
		if (!entry->file) {
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
	return strcmp(i1->file, i2->file);
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
	return strcmp(i1->file, i2->file);
}

int
copen_multifile_directory(cfile *cfh, const char *src_directory)
{
	multifile_file_data **files = NULL;
	unsigned long files_count = 0;
	unsigned long files_size = 0;
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
	} else {
		directory = strdup(src_directory);
	}
	if (!directory) {
		eprintf("multifile: directory dup mem allocaiton failed\n");
		return 1;
	}
	if (multifile_recurse_directory(directory, NULL, &files, &files_count, &files_size)) {
		free(directory);
		return 1;
	}
	qsort(files, files_count, sizeof(multifile_file_data *), cmp_hardlinks);
	unsigned long i;
	for (i=1; i < files_count; i++) {
		if (files[i -1]->st->st_dev == files[i]->st->st_dev &&
			files[i -1]->st->st_ino == files[i]->st->st_ino) {
			files[i]->hardlink = files[i -1]->file;
			dcprintf("hardlink found: %s to %s\n", files[i]->file, files[i]->hardlink);
		} else {
			files[i]->hardlink = NULL;
		}
	}
	qsort(files, files_count, sizeof(multifile_file_data *), cmpstrcmp);
/*
	unsigned long i=0;
	for (; i < files_count; i++) {
		printf("%s\n", files[i]);
	}
*/
	return copen_multifile(cfh, directory, files, files_count);
}