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

#include <linux/limits.h>


typedef struct {
	size_t	start;
	size_t	end;
} file_position;

typedef struct {
	// Directory root for all files that were given.  Guranteed to have a trailig /
	const char *root;
	int root_len;
	// The sorted list of files.
	const char **files;
	file_position *file_map;
	unsigned long file_count;
	int active_fd;
	unsigned long current_file_index;
} multifile_data;

void
get_filepath(multifile_data *data, unsigned long file_pos, char *buf)
{
	if (data->root_len) {
		memcpy(buf, data->root, data->root_len);
		buf += data->root_len + 1;
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
	} else if (offset > item->end) {
		return 1;
	}
	return 0;
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
		free(data);
	}
	return 0;
}

ssize_t
cseek_multifile(cfile *cfh, void *raw, ssize_t offset, ssize_t data_offset, int offset_type)
{
	multifile_data *data = (multifile_data *)raw;
	if (offset >= data->file_map[data->current_file_index].end || offset < data->file_map[data->current_file_index].start) {
		if (set_file_index(data, data_offset)) {
			return (cfh->err = IO_ERROR);
		}
		cfh->data.pos = 0;
		cfh->data.end = 0;
		cfh->data.offset = offset;
	} else {
		// We were asked to seek in a handle we already have- iow, greater seek then the crefill machinery
		// provided.
		cfh->data.pos = 0;
		cfh->data.end = 0;
		cfh->data.offset = offset;
		if (data->active_fd != -1) {
			// Only lseek if the handle is actually open.
			size_t desired = cfh->data.offset - data->file_map[data->current_file_index].start;
			if (desired != lseek(data->active_fd, desired, SEEK_SET)) {
				eprintf("Somehow failed to lseek to %lu for %s\n", desired, data->files[data->current_file_index]);
				return (cfh->err = IO_ERROR);
			}
		}
	}
	// Check this; CSEEK_ABS behaviour may be retarded.
	return offset;
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
	if (cfh->data.offset >= data->file_map[data->current_file_index].end) {
		multifile_close_active_fd(data);
		if (data->current_file_index + 1 == data->file_count) {
			cfh->state_flags |= CFILE_EOF;
			cfh->data.offset += cfh->data.end;
			cfh->data.pos = cfh->data.end = 0;
			return 0;
		}
	}
	if (multifile_ensure_open_active(data)) {
		return (cfh->err = IO_ERROR);
	}
	cfh->data.end = read(data->active_fd, cfh->data.buff,
		MIN(cfh->data.size, data->file_map[data->current_file_index].end - cfh->data.offset));
	dcprintf("crefill_multifile: %u: no_compress, got %lu\n", cfh->cfh_id, cfh->data.end);
	return 0;
}

int
copen_multifile(cfile *cfh, const char *files[], unsigned long file_count)
{
	int result = 0;
	memset(cfh, 0, sizeof(cfile));
	multifile_data *data = (multifile_data *)calloc(sizeof(multifile_data), 1);
	if (!data) {
		return MEM_ERROR;
	}
	data->file_map = calloc(sizeof(file_position), file_count);
	if (!data->file_map) {
		free(data);
		return MEM_ERROR;
	}
	data->files = files;
	data->file_count = file_count;
	data->active_fd = -1;

	char buf[PATH_MAX];
	struct stat st;
	size_t position = 0;
	unsigned long x = 0;
	const char *file = files[0];
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
	
	return 0;

cleanup:
	cclose_multifile(cfh, data);
	return result;
}	
