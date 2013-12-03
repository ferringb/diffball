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
	size_t	start;
	size_t	end;
} file_position;

typedef struct {
	// Directory root for all files that were given.  Guranteed to have a trailig /
	char *root;
	int root_len;
	// The sorted list of files.
	char **files;
	file_position *file_map;
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
	strcpy(buf, data->files[file_pos]);
}

int
bsearch_compar(const void *key, const void *array_item)
{
	size_t offset = *((size_t *)key);
	file_position *item = (file_position *)array_item;
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
	assert(offset <= data->file_map[data->file_count - 1].end);
	file_position *match = (file_position *)bsearch(&offset, data->file_map, data->file_count, sizeof(file_position), bsearch_compar);
	if (!match) {
		eprintf("Somehow received NULL from bsearch for multitfile: offset %li\n", offset);
		return 1;
	}
	multifile_close_active_fd(data);
	data->current_file_index = match - data->file_map;
	assert (data->current_file_index < data->file_count);
	return 0;
}

unsigned int
cclose_multifile(cfile *cfh, void *raw)
{
	multifile_data *data = (multifile_data *)raw;
	if (data) {
		multifile_close_active_fd(data);
		free(data->file_map);
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
	if (data_offset >= data->file_map[data->current_file_index].end || data_offset < data->file_map[data->current_file_index].start) {
		// This requires a new file; jump to that file.
		if (set_file_index(data, data_offset)) {
			return (cfh->err = IO_ERROR);
		}
	}
	if(multifile_ensure_open_active(data)) {
		return (cfh->err = IO_ERROR);
	}
	cfh->data.offset = data_offset;
	size_t desired = data_offset - data->file_map[data->current_file_index].start;
	if (desired != lseek(data->active_fd, desired, SEEK_SET)) {
		eprintf("Somehow failed to lseek to %lu for %s\n", desired, data->files[data->current_file_index]);
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
	while (cfh->data.offset >= data->file_map[current].end) {
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
	size_t desired = cfh->data.offset - data->file_map[data->current_file_index].start;
	if (desired != lseek(data->active_fd, desired, SEEK_SET)) {
		eprintf("Somehow lseek w/in refill failed\n");
		return (cfh->err = IO_ERROR);
	}
	ssize_t result = read(data->active_fd, cfh->data.buff,
		MIN(cfh->data.size, data->file_map[data->current_file_index].end - cfh->data.offset));
	if (result >= 0) {
		cfh->data.end = result;
	} else {
		eprintf("got nonzero read: errno %i for %s\n", errno, data->files[data->current_file_index]);
		return (cfh->err = IO_ERROR);
	}
	dcprintf("crefill_multifile: %u: no_compress, got %lu\n", cfh->cfh_id, cfh->data.end);
	return 0;
}

int
copen_multifile(cfile *cfh, char *root, char *files[], unsigned long file_count)
{
	int result = 0;
	memset(cfh, 0, sizeof(cfile));
	multifile_data *data = (multifile_data *)calloc(sizeof(multifile_data), 1);
	if (!data) {
		result = MEM_ERROR;
		goto cleanup;
	}
	data->file_map = calloc(sizeof(file_position), file_count);
	if (!data->file_map) {
		result = MEM_ERROR;
		goto cleanup;
	}
	data->root = root;
	data->root_len = strlen(root);
	data->files = files;
	data->file_count = file_count;
	data->active_fd = -1;

	char buf[PATH_MAX];
	struct stat st;
	size_t position = 0;
	unsigned long x = 0;
	char *file = files[0];
	strcpy(buf, root);
	for (; x < file_count; x++, file++) {
		get_filepath(data, x, buf);
		result = lstat(buf, &st);
		if (result != 0) {
			result = IO_ERROR;
			eprintf("Failed initialized multifile due to errno %i for %s\n", errno, buf);
			goto cleanup;
		}
		data->file_map[x].start = position;
		position += st.st_size;
		data->file_map[x].end = position;
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
multifile_recurse_directory(const char *root, const char *directory, char **files[], unsigned long *files_count, unsigned long *files_size)
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
		if (entry->d_type == DT_UNKNOWN) {
			strcpy(start, name);
			struct stat st;
			if (lstat(buf, &st)) {
				eprintf("multifile: Failed lstating %s; errno %i\n", buf, errno);
				return 1;
			}

			if (!S_ISREG(st.st_mode)) {
				if (S_ISDIR(st.st_mode)){
					strcat(start, "/");;
					if(multifile_recurse_directory(root, directory_start, files, files_count, files_size)) {
						return 1;
					}
				}
				continue;
			}
		} else if (entry->d_type != DT_REG) {
			if (entry->d_type == DT_DIR) {
				strcat(start, name);
				strcat(start, "/");
				if (multifile_recurse_directory(root, directory_start, files, files_count, files_size)) {
					return 1;
				}
			}
			continue;
		} else {
			strcpy(start, name);
		}
			
		if (*files_size == *files_count) {
			if (*files_size == 0) {
				*files_size = 16;
			}
			char **tmp = realloc(*files, sizeof(char *) * ((*files_size) * 2));
			if (!tmp) {
				eprintf("multifile: failed reallocing files array to size %lu\n", ((*files_size) * 2));
				return 1;
			}
			*files = tmp;
			*files_size *= 2;
		}
		(*files)[*files_count] = strdup(directory_start);
		if (!((*files)[*files_count])) {
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
	return strcmp(* (char * const *) p1, * (char * const *) p2);
}

int
copen_multifile_directory(cfile *cfh, const char *src_directory)
{
	char **files = NULL;
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
	qsort(files, files_count, sizeof(char *), cmpstrcmp);
/*
	unsigned long i=0;
	for (; i < files_count; i++) {
		printf("%s\n", files[i]);
	}
*/
	return copen_multifile(cfh, directory, files, files_count);
}
