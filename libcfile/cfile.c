// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2014 Brian Harring <ferringb@gmail.com>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "internal.h"
#include <string.h>
#include <fcntl.h>

#define MIN(x, y) ((x) < (y) ? (x) : (y))
unsigned int cfile_verbosity;
unsigned int largefile_support = 0;
#ifdef _LARGEFILE_SOURCE
largefile_support = 1;
#endif

// C standards for inline are stupid.
extern void flag_lseek_needed(cfile *cfh);
extern void set_last_lseeker(cfile *cfh);
extern signed int ensure_lseek_position(cfile *cfh);

static inline int
error_if_closed(cfile *cfh, const char *operation)
{
	if (!cfile_is_open(cfh))
	{
		eprintf("%s: cfile is not open, internal bug\n", operation);
		return 1;
	}
	return 0;
}

/* quick kludge */
signed long
cfile_identify_compressor(int fh)
{
	unsigned char buff[5]; // XZ magic number length is 5
	if (read(fh, buff, 5) != 5)
	{
		return EOF_ERROR;
	}
	else
	{
		lseek(fh, -5, SEEK_CUR);
	}
	if (memcmp(buff, "BZ", 2) == 0)
	{
		return BZIP2_COMPRESSOR;
	}
	else if (0x1f == buff[0] && 0x8b == buff[1])
	{
		return GZIP_COMPRESSOR;
	}
	else if (0xfd == buff[0] && '7' == buff[1] && 'z' == buff[2] && 'X' == buff[3] && 'Z' == buff[4])
	{
		return XZ_COMPRESSOR;
	}
	return NO_COMPRESSOR;
}

cfile *
copen_dup_cfh(cfile *cfh)
{
	if (error_if_closed(cfh, "copen_dup_cfh"))
		return NULL;

	cfile *dup;
	dup = (cfile *)calloc(1, sizeof(cfile));
	if (dup == NULL)
	{
		return NULL;
	}
	if (copen_child_cfh(dup, cfh, cfh->data.window_offset,
						cfh->data.window_len == 0 ? 0 : cfh->data.window_offset + cfh->data.window_len,
						cfh->compressor_type, cfh->access_flags))
	{
		free(dup);
		return NULL;
	}
	return dup;
}

int copen_child_cfh(cfile *cfh, cfile *parent, size_t fh_start,
					size_t fh_end, unsigned int compressor_type, unsigned int access_flags)
{
	if (error_if_closed(parent, "copen_child_cfh"))
	{
		return UNSUPPORTED_OPT;
	}
	else if (cfile_is_open(cfh))
	{
		eprintf("passed in cfh is already open\n");
		return UNSUPPORTED_OPT;
	}

	if (CFH_IS_CHILD(parent))
	{
		cfh->lseek_info.parent_ptr = parent->lseek_info.parent_ptr;
		// Update the request range to fit into the parents range.
		fh_start += cfh->lseek_info.parent_ptr->data.window_offset;
		fh_end += cfh->lseek_info.parent_ptr->data.window_offset;
	}
	else
	{
		cfh->lseek_info.parent_ptr = parent;
	}

	int err = 0;
	dcprintf("copen_child_cfh: %u: calling internal_copen\n", parent->cfh_id);
	cfh->state_flags = CFILE_CHILD_CFH | parent->state_flags;
	cfh->lseek_info.parent_ptr->lseek_info.parent.handle_count++;
	dcprintf("setting child id=%u\n", cfh->lseek_info.parent_ptr->lseek_info.parent.handle_count);
	cfh->cfh_id = cfh->lseek_info.parent_ptr->lseek_info.parent.handle_count;
	if (parent->compressor_type != NO_COMPRESSOR)
	{
		if (compressor_type != NO_COMPRESSOR)
		{
			/* cfile doesn't handle this. deal. */
			v0printf("unable to open a compressor w/in a compressor, crapping out.\n");
			return UNSUPPORTED_OPT;
		}
		err = internal_copen(cfh, parent->raw_fh, parent->raw.window_offset, parent->raw.window_len,
							 fh_start, fh_end, parent->compressor_type, access_flags);
	}
	else
	{
		err = internal_copen(cfh, parent->raw_fh, fh_start, fh_end, 0, 0,
							 compressor_type, access_flags);
	}
	if (parent->state_flags & CFILE_MEM_ALIAS)
	{
		assert((cfh->access_flags & CFILE_WONLY) == 0);
		free(cfh->data.buff);
		cfh->data.buff = parent->data.buff + fh_start;
		cfh->data.size = cfh->data.end = fh_end - fh_start;
		cfh->state_flags |= CFILE_MEM_ALIAS;
	}
	else if (parent->state_flags & CFILE_CHILD_INHERITS_IO)
	{
		memcpy(&(cfh->io), &(parent->io), sizeof(parent->io));
	}
	return err;
}

int copen_mem(cfile *cfh, unsigned char *buff, size_t len, unsigned int compressor_type, unsigned int access_flags)
{
	if (cfile_is_open(cfh))
	{
		eprintf("passed in cfh is already open\n");
		return UNSUPPORTED_OPT;
	}

	int ret;
	size_t len2;
	if (buff == NULL)
	{
		if (access_flags & CFILE_RONLY)
			return UNSUPPORTED_OPT;
	}
	else
	{
		if ((access_flags & CFILE_WONLY) && buff != NULL)
		{
			v0printf("supplying an initial buffer for a write mem alias isn't totally supported yet, sorry") return UNSUPPORTED_OPT;
		}
	}
	if (access_flags & CFILE_WONLY)
		len2 = 0;
	else
		len2 = len;

	if (compressor_type != NO_COMPRESSOR)
	{
		return UNSUPPORTED_OPT;
	}
	cfh->state_flags = CFILE_MEM_ALIAS;
	cfh->lseek_info.parent.handle_count = 0;
	cfh->cfh_id = 1;
	// pass in a daft handle
	ret = internal_copen(cfh, -1, 0, len2, 0, 0, compressor_type, access_flags);
	if (ret != 0)
		return ret;
	// mangle things a bit.
	if (buff != NULL)
	{
		free(cfh->data.buff);
		cfh->data.buff = buff;
		cfh->data.window_len = cfh->data.end = cfh->data.size = len;
	}
	cfh->lseek_info.parent.last = cfh->cfh_id;

	return 0;
}

int copen(cfile *cfh, const char *filename, unsigned int compressor_type, unsigned int access_flags)
{
	if (cfile_is_open(cfh))
	{
		eprintf("passed in cfh is already open\n");
		return UNSUPPORTED_OPT;
	}

	dcprintf("copen: calling internal_copen\n");
	struct stat st;
	int fd, flags;
	size_t size = 0;
	flags = 0;
	if (access_flags & CFILE_RONLY)
	{
		flags = O_RDONLY;
		if (stat(filename, &st))
			return IO_ERROR;
		size = st.st_size;
	}
	else if (access_flags & CFILE_WONLY)
	{
		if (access_flags & CFILE_NEW)
		{
			flags = O_RDWR | O_CREAT | O_TRUNC;
			size = 0;
		}
		else
		{
			flags = O_WRONLY;
			if (stat(filename, &st))
				return IO_ERROR;
			size = st.st_size;
		}
	}
	fd = open(filename, flags, 0644);
	if (fd < 0)
	{
		return IO_ERROR;
	}
	cfh->state_flags = CFILE_OPEN_FH;
	cfh->lseek_info.parent.last = 0;
	cfh->lseek_info.parent.handle_count = 1;
	cfh->cfh_id = 1;
	return internal_copen(cfh, fd, 0, size, 0, 0,
						  compressor_type, access_flags);
}

int copen_dup_fd(cfile *cfh, int fh, size_t fh_start, size_t fh_end,
				 unsigned int compressor_type, unsigned int access_flags)
{
	if (cfile_is_open(cfh))
	{
		eprintf("passed in cfh is already open\n");
		return UNSUPPORTED_OPT;
	}

	dcprintf("copen: calling internal_copen\n");
	cfh->state_flags = 0;
	cfh->lseek_info.parent.last = 0;
	cfh->lseek_info.parent.handle_count = 1;
	cfh->cfh_id = 1;
	return internal_copen(cfh, fh, fh_start, fh_end, 0, 0,
						  compressor_type, access_flags);
}

int internal_copen(cfile *cfh, int fh, size_t raw_fh_start, size_t raw_fh_end,
				   size_t data_fh_start, size_t data_fh_end, unsigned int compressor_type, unsigned int access_flags)
{
	signed long ret_val;
	/* this will need adjustment for compressed files */
	cfh->raw_fh = fh;

	assert(raw_fh_start <= raw_fh_end);
	cfh->access_flags = access_flags;

	cfh->state_flags |= CFILE_IS_OPEN;

	if (AUTODETECT_COMPRESSOR == compressor_type)
	{
		dcprintf("copen: autodetecting comp_type: ");
		ret_val = cfile_identify_compressor(fh);
		if (ret_val < 0)
		{
			return IO_ERROR;
		}
		dcprintf("got %ld\n", ret_val);
		cfh->compressor_type = ret_val;
		flag_lseek_needed(cfh);
	}
	else
	{
		cfh->compressor_type = compressor_type;
	}
	if (cfh->compressor_type != NO_COMPRESSOR)
	{
		if ((cfh->access_flags & CFILE_WR) == CFILE_WR)
		{
			// one or the other, not both.
			return IO_ERROR;
		}
		else if (cfh->access_flags & CFILE_RONLY)
		{
			cfh->access_flags |= CFILE_SEEKABLE;
		}
	}
	else
	{
		cfh->access_flags |= CFILE_SEEKABLE;
	}

	int result = 0;
	cfh->io.open = NULL;
	cfh->io.close = NULL;
	cfh->io.write = NULL;
	cfh->io.read = NULL;
	cfh->io.refill = NULL;
	cfh->io.flush = NULL;
	cfh->io.seek = NULL;
	cfh->io.data = NULL;
	switch (cfh->compressor_type)
	{
	case NO_COMPRESSOR:
		cfh->data.window_offset = raw_fh_start;
		cfh->data.window_len = raw_fh_end - raw_fh_start;
		result = internal_copen_no_comp(cfh);
		break;

	case BZIP2_COMPRESSOR:
		cfh->raw.window_offset = raw_fh_start;
		cfh->raw.window_len = raw_fh_end - raw_fh_start;
		cfh->data.window_offset = data_fh_start;
		cfh->data.window_len = (data_fh_end == 0 ? 0 : data_fh_end - data_fh_start);
		result = internal_copen_bzip2(cfh);
		break;

	case GZIP_COMPRESSOR:
		cfh->raw.window_offset = raw_fh_start;
		cfh->raw.window_len = raw_fh_end - raw_fh_start;
		cfh->data.window_offset = data_fh_start;
		cfh->data.window_len = (data_fh_end == 0 ? 0 : data_fh_end - data_fh_start);
		result = internal_copen_gzip(cfh);
		break;

	case XZ_COMPRESSOR:
		cfh->raw.window_offset = raw_fh_start;
		cfh->raw.window_len = raw_fh_end - raw_fh_start;
		cfh->data.window_offset = data_fh_start;
		cfh->data.window_len = (data_fh_end == 0 ? 0 : data_fh_end - data_fh_start);
		result = internal_copen_xz(cfh);
		break;
	}

	return result;
}

unsigned int
cclose(cfile *cfh)
{
	if (!cfile_is_open(cfh))
	{
		return 0;
	}

	if (cfh->access_flags & CFILE_WONLY)
	{
		cflush(cfh);
	}
	dcprintf("id(%u), data_size=%lu, raw_size=%lu\n", cfh->cfh_id, cfh->data.size, cfh->raw.size);
	if (!(cfh->state_flags & CFILE_MEM_ALIAS))
	{
		if (cfh->data.buff)
			free(cfh->data.buff);
	}
	if (cfh->raw.buff)
		free(cfh->raw.buff);
	unsigned int result = 0;
	if (!(cfh->state_flags & CFILE_CHILD_CFH) && cfh->io.close)
	{
		result = cfh->io.close(cfh, cfh->io.data);
	}
	memset(&(cfh->io), 0, sizeof(cfh->io));

	/* XXX questionable */
	if (cfh->state_flags & CFILE_OPEN_FH && !(cfh->state_flags & CFILE_CHILD_CFH))
	{
		close(cfh->raw_fh);
	}
	if (cfh->state_flags & CFILE_FREE_AT_CLOSING)
	{
		free(cfh);
	}
	else
	{
		cfh->raw.pos = cfh->raw.end = cfh->raw.size = cfh->raw.offset =
			cfh->data.pos = cfh->data.end = cfh->data.size = cfh->data.offset =
				cfh->raw.window_len = cfh->data.window_len = 0;
	}
	cfh->state_flags &= ~CFILE_IS_OPEN;
	return result;
}

ssize_t
cread(cfile *cfh, void *buff, size_t len)
{
	if (error_if_closed(cfh, "cread"))
	{
		return UNSUPPORTED_OPT;
	}

	size_t bytes_wrote = 0;
	size_t x;
	ssize_t val;
	while (bytes_wrote != len)
	{
		if (cfh->data.end == cfh->data.pos)
		{
			val = crefill(cfh);
			if (val <= 0)
			{
				dcprintf("%u: got an error/0 bytes, returning from cread\n", cfh->cfh_id);
				if (val == 0)
					return (bytes_wrote);
				else
					return val;
			}
		}
		x = MIN(cfh->data.end - cfh->data.pos, len - bytes_wrote);

		/* possible to get stuck in a loop here, fix this */

		memcpy(buff + bytes_wrote, cfh->data.buff + cfh->data.pos, x);
		bytes_wrote += x;
		cfh->data.pos += x;
	}
	return bytes_wrote;
}

ssize_t
cwrite(cfile *cfh, void *buff, size_t len)
{
	if (error_if_closed(cfh, "cread"))
	{
		return UNSUPPORTED_OPT;
	}

	size_t bytes_wrote = 0, x;
	if (cfh->access_flags & CFILE_RONLY && cfh->data.write_end == 0)
	{

		//basically, RW mode, and the first write to this buffer.
		cfh->data.write_start = cfh->data.pos;
	}
	while (bytes_wrote < len)
	{
		if (cfh->data.size == cfh->data.write_end)
		{
			cflush(cfh);
		}
		x = MIN(cfh->data.size - cfh->data.pos, len - bytes_wrote);
		memcpy(cfh->data.buff + cfh->data.pos, buff + bytes_wrote, x);
		bytes_wrote += x;
		cfh->data.pos += x;
		cfh->data.write_end = cfh->data.pos;
	}
	cfh->data.write_end = cfh->data.pos;
	return bytes_wrote;
}

ssize_t
cseek(cfile *cfh, ssize_t offset, int offset_type)
{
	if (error_if_closed(cfh, "cread"))
	{
		return UNSUPPORTED_OPT;
	}

	ssize_t data_offset;
	if (CSEEK_ABS == offset_type)
		data_offset = abs(offset) - cfh->data.window_offset;
	else if (CSEEK_CUR == offset_type)
		data_offset = cfh->data.offset + cfh->data.pos + offset;
	else if (CSEEK_END == offset_type)
		data_offset = cfh->data.window_len + offset;
	else if (CSEEK_FSTART == offset_type)
		data_offset = offset;
	else
		return IO_ERROR;

	assert(data_offset >= 0 || NO_COMPRESSOR != cfh->compressor_type);

	if (cfh->access_flags & CFILE_WRITEABLE)
	{
		dcprintf("%u: flushing cfile prior to cseek\n", cfh->cfh_id);
		if (cflush(cfh))
		{
			return IO_ERROR;
		}
	}
	/* see if the desired location is w/in the data buff */
	if (data_offset >= cfh->data.offset &&
		data_offset < cfh->data.offset + cfh->data.size &&
		cfh->data.end > data_offset - cfh->data.offset)
	{

		dcprintf("cseek: %u: buffered data, repositioning pos\n", cfh->cfh_id);
		cfh->data.pos = data_offset - cfh->data.offset;
		return (CSEEK_ABS == offset_type ? data_offset + cfh->data.window_offset : data_offset);
	}
	else if ((cfh->access_flags & !CFILE_WRITEABLE) && data_offset >= cfh->data.end + cfh->data.offset &&
			 data_offset < cfh->data.end + cfh->data.size + cfh->data.offset && IS_LAST_LSEEKER(cfh))
	{

		// see if the desired location is the next page (avoid lseek + read, get read instead).
		crefill(cfh);
		if (cfh->data.end + cfh->data.offset > data_offset)
			cfh->data.pos = data_offset - cfh->data.offset;
		return (CSEEK_ABS == offset_type ? cfh->data.pos + cfh->data.offset + cfh->data.window_offset : cfh->data.pos + cfh->data.offset);
	}

	assert((cfh->state_flags & CFILE_MEM_ALIAS) == 0);
	assert(cfh->io.seek != NULL);

	return cfh->io.seek(cfh, cfh->io.data, offset, data_offset, offset_type);
}

signed int
raw_ensure_position(cfile *cfh)
{
	set_last_lseeker(cfh);
	if (NO_COMPRESSOR == cfh->compressor_type)
	{
		return (lseek(cfh->raw_fh, cfh->data.offset + cfh->data.window_offset + cfh->data.end, SEEK_SET) !=
				(cfh->data.offset + cfh->data.window_offset + cfh->data.end));
	}
	else if (BZIP2_COMPRESSOR == cfh->compressor_type ||
			 GZIP_COMPRESSOR == cfh->compressor_type ||
			 XZ_COMPRESSOR == cfh->compressor_type)
	{
		return (lseek(cfh->raw_fh, cfh->raw.offset + cfh->raw.window_offset + cfh->raw.end, SEEK_SET) != (cfh->raw.offset +
																										  cfh->raw.window_offset + cfh->raw.end));
	}
	return IO_ERROR;
}

size_t
ctell(cfile *cfh, unsigned int tell_type)
{
	if (error_if_closed(cfh, "cread"))
	{
		return UNSUPPORTED_OPT;
	}

	if (CSEEK_ABS == tell_type)
		return cfh->data.window_offset + cfh->data.offset + cfh->data.pos;
	else if (CSEEK_FSTART == tell_type)
		return cfh->data.offset + cfh->data.pos;
	else if (CSEEK_END == tell_type)
		return cfh->data.window_len - (cfh->data.offset + cfh->data.pos);
	return 0;
}

ssize_t
cflush(cfile *cfh)
{
	if (error_if_closed(cfh, "cread"))
	{
		return UNSUPPORTED_OPT;
	}

	/* kind of a hack, I'm afraid. */
	ssize_t result = 0;
	if (cfh->data.write_end != 0)
	{
		if (cfh->state_flags & CFILE_MEM_ALIAS)
		{
			if (cfh->data.write_end < cfh->data.size)
				return 0;
			unsigned char *p;
			size_t realloc_size;
			if (cfh->data.size >= CFILE_DEFAULT_MEM_ALIAS_W_REALLOC)
				realloc_size = cfh->data.size + CFILE_DEFAULT_MEM_ALIAS_W_REALLOC;
			else
				realloc_size = cfh->data.size * 2;

			p = realloc(cfh->data.buff, cfh->data.size + CFILE_DEFAULT_MEM_ALIAS_W_REALLOC);
			if (p == NULL)
				return MEM_ERROR;
			cfh->data.size = realloc_size;
			cfh->data.buff = p;
			return 0;
		}
		assert(cfh->io.flush != NULL);
		result = cfh->io.flush(cfh, cfh->io.data);
	}
	return result;
}

ssize_t
crefill(cfile *cfh)
{
	if (error_if_closed(cfh, "cread"))
	{
		return UNSUPPORTED_OPT;
	}

	if (cfh->state_flags & CFILE_MEM_ALIAS)
	{
		return 0;
	}
	assert(cfh->io.refill != NULL);

#ifdef DEBUG_CFILE
	memset(cfh->data.buff, 0, cfh->data.size);
#endif
	int result = cfh->io.refill(cfh, cfh->io.data);

	return result == 0 ? cfh->data.end : result;
}

size_t
cfile_len(cfile *cfh)
{
	if (error_if_closed(cfh, "cread"))
	{
		return 0;
	}

	return cfh->data.window_len;
}

size_t
cfile_start_offset(cfile *cfh)
{
	if (error_if_closed(cfh, "cread"))
	{
		return 0;
	}

	return cfh->data.window_offset;
}

/* while I realize this may not *necessarily* belong in cfile,
   eh, it's going here.

   deal with it.  :-) */

ssize_t
copy_cfile_block(cfile *out_cfh, cfile *in_cfh, size_t in_offset, size_t len)
{
	unsigned char buff[CFILE_DEFAULT_BUFFER_SIZE];
	unsigned int lb;
	size_t bytes_wrote = 0;
	;
	if (in_offset != cseek(in_cfh, in_offset, CSEEK_FSTART))
	{
		return EOF_ERROR;
	}
	while (len)
	{
		lb = MIN(CFILE_DEFAULT_BUFFER_SIZE, len);
		if ((lb != cread(in_cfh, buff, lb)) ||
			(lb != cwrite(out_cfh, buff, lb)))
		{
			v2printf("twas copy_cfile_block2, in_offset is %zi, lb was %i, remaining len was %zi, bytes_wrote %zi, pos %zi, end %zi!\n", in_offset, lb, len, bytes_wrote, in_cfh->data.pos, in_cfh->data.end);
			return EOF_ERROR;
		}
		len -= lb;
		bytes_wrote += lb;
	}
	return bytes_wrote;
}

cfile_window *
expose_page(cfile *cfh)
{
	if (error_if_closed(cfh, "cread"))
	{
		return NULL;
	}

	if (cfh->access_flags & CFILE_RONLY)
	{
		if (cfh->data.end == 0)
		{
			assert((cfh->state_flags & CFILE_MEM_ALIAS) == 0);
			crefill(cfh);
		}
	}
	return &cfh->data;
}

cfile_window *
next_page(cfile *cfh)
{
	if (error_if_closed(cfh, "cread"))
	{
		return NULL;
	}

	if (cfh->access_flags & CFILE_WRITEABLE)
	{
		cflush(cfh);
	}
	if (cfh->access_flags & CFILE_RONLY)
	{
		if ((cfh->state_flags & CFILE_MEM_ALIAS) == 0)
			crefill(cfh);
		else
			return NULL;
	}
	return &cfh->data;
}

cfile_window *
prev_page(cfile *cfh)
{
	if (error_if_closed(cfh, "cread"))
	{
		return NULL;
	}

	ssize_t page_start = cfh->data.offset;
	/* possibly do an error check or something here */
	if (cfh->access_flags & CFILE_WRITEABLE)
	{
		cflush(cfh);
	}
	if (cfh->data.offset == 0)
	{
		cfh->data.end = 0;
		cfh->data.pos = 0;
	}
	else
	{
		if (cfh->data.size > cfh->data.offset)
		{
			cseek(cfh, 0, CSEEK_FSTART);
		}
		else
		{
			cseek(cfh, -cfh->data.size, CSEEK_CUR);
		}
		crefill(cfh);
		// Hack: multifile can rewind too far if it crosses a file boundary-
		// Remove this once the api is expanded to allow the cfile implementation
		// to do page exposure.
		while (cfh->data.offset + cfh->data.end < page_start)
		{
			crefill(cfh);
		}
	}
	return &cfh->data;
}

unsigned char *
cfile_read_null_string(cfile *cfh)
{
	return cfile_read_string_delim(cfh, 0, 0);
}

unsigned char *
cfile_read_string_delim(cfile *cfh, char delim, int eof_is_delim)
{
	if (error_if_closed(cfh, "cread"))
	{
		return NULL;
	}

	unsigned char *result = NULL, *tmp = NULL;
	size_t len = 0;
	do
	{
		if (cfh->data.end != cfh->data.pos)
		{
			unsigned char *match = memchr(cfh->data.buff + cfh->data.pos, delim, cfh->data.end - cfh->data.pos);
			if (match)
			{
				unsigned char *tmp = realloc(result, len + 1 + (match - cfh->data.buff));
				if (tmp)
				{
					memcpy(tmp + len, cfh->data.buff + cfh->data.pos, (match - cfh->data.buff) - cfh->data.pos + 1);
					tmp[len + (match - cfh->data.buff) - cfh->data.pos] = 0;
					cfh->data.pos = match - cfh->data.buff + 1;
					return tmp;
				}
				if (result)
				{
					free(result);
				}
				eprintf("Failed allocating memory\n");
				return NULL;
			}
			tmp = realloc(result, len + cfh->data.end - cfh->data.pos);
			if (!tmp)
			{
				if (result)
				{
					free(result);
				}
				eprintf("failed allocating memory\n");
				return NULL;
			}
			result = tmp;
			memcpy(result + len, cfh->data.buff + cfh->data.pos, cfh->data.end - cfh->data.pos);
			len += cfh->data.end - cfh->data.pos;
		}
	} while (crefill(cfh) > 0);

	if (!eof_is_delim || !result)
	{
		if (result)
		{
			free(result);
		}
		return NULL;
	}
	tmp = realloc(result, len + 1);
	if (!tmp)
	{
		eprintf("allocation error\n");
		free(result);
		return NULL;
	}
	tmp[len] = 0;
	return tmp;
}

int cfile_is_open(cfile *cfh)
{
	return (cfh->state_flags & CFILE_IS_OPEN) ? 1 : 0;
}
