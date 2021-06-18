// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2013 Brian Harring <ferringb@gmail.com>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "internal.h"
#include <string.h>
#include <fcntl.h>

ssize_t
cseek_no_comp(cfile *cfh, void *data, ssize_t offset, ssize_t data_offset, int offset_type)
{
	dcprintf("cseek: %u: no_compressor, flagging it\n", cfh->cfh_id);
	flag_lseek_needed(cfh);
	cfh->data.offset = data_offset;
	cfh->data.pos = cfh->data.end = 0;
	if(cfh->access_flags & CFILE_WONLY) {
		if(raw_ensure_position(cfh)) {
			dcprintf("%u: raw_ensure_position on WONLY cfile failed\n", cfh->cfh_id);
			return IO_ERROR;
		}
	}
	return (CSEEK_ABS==offset_type ? data_offset + cfh->data.window_offset :
		data_offset);
}

int
crefill_no_comp(cfile *cfh, void *data)
{
	size_t x;
	if((cfh->access_flags & CFILE_WRITEABLE) && (cfh->data.write_end != 0)) {
		if(cflush(cfh)) {
//			error handling anyone?
				return 0L;
		}
	}
	if(ensure_lseek_position(cfh)) {
		return (cfh->err = IO_ERROR);
	}
	cfh->data.offset += cfh->data.end;
	if(cfh->data.window_len != 0) {
		x = read(cfh->raw_fh, cfh->data.buff, MIN(cfh->data.size,
			cfh->data.window_len - cfh->data.offset));
	} else {
		x = read(cfh->raw_fh, cfh->data.buff, cfh->data.size);
	}
	// is this valid for write & read mode?
	if(x==0)
		cfh->state_flags |= CFILE_EOF;
	cfh->data.end = x;
	cfh->data.pos = 0;
	dcprintf("crefill: %u: no_compress, got %lu\n", cfh->cfh_id, x);
	return 0;
}

ssize_t
cflush_no_comp(cfile *cfh, void *data)
{
	// position the sucker, either at write_end, or at write_start (for CFILE_WR)
	if(cfh->access_flags & CFILE_READABLE) {
		if(lseek(cfh->raw_fh, cfh->data.offset + cfh->data.window_offset + cfh->data.write_start, SEEK_SET) !=
			cfh->data.offset + cfh->data.window_offset + cfh->data.write_start) {
			return (cfh->err = IO_ERROR);
		}
	} else if(ensure_lseek_position(cfh)) {
		return (cfh->err = IO_ERROR);
	}
	if(cfh->data.write_end - cfh->data.write_start !=
		write(cfh->raw_fh, cfh->data.buff + cfh->data.write_start, cfh->data.write_end - cfh->data.write_start)) {
		return (cfh->err = IO_ERROR);
	}
	if((cfh->access_flags & CFILE_READABLE) && (cfh->data.end) && (cfh->data.end != cfh->data.write_end)) {
		flag_lseek_needed(cfh);
		cfh->data.offset += cfh->data.end;
	} else {
		cfh->data.offset += cfh->data.write_end;
	}
	cfh->data.write_end = cfh->data.write_start = cfh->data.pos = cfh->data.end = 0;
	return 0;
}

int
internal_copen_no_comp(cfile *cfh)
{
	dcprintf("copen: opening w/ no_compressor\n");
	if(cfh->access_flags & CFILE_BUFFER_ALL) {
		cfh->data.size = cfh->data.window_len;
	} else {
		cfh->data.size = CFILE_DEFAULT_BUFFER_SIZE;
	}
	if((cfh->data.buff = (unsigned char *)malloc(cfh->data.size))==NULL) {
		return MEM_ERROR;
	}
	dcprintf("copen: buffer size(%lu), buffer_all(%u)\n", cfh->data.size,
		cfh->access_flags & CFILE_BUFFER_ALL);
	cfh->raw.size = 0;
	cfh->raw.buff = NULL;
	cfh->raw.pos = cfh->raw.offset  = cfh->raw.end = cfh->raw.write_start = cfh->raw.write_end = cfh->data.pos = 
		cfh->data.offset = cfh->data.end = cfh->data.write_start = cfh->data.write_end = 0;
	cfh->io.seek = cseek_no_comp;
	cfh->io.refill = crefill_no_comp;
	cfh->io.flush = cflush_no_comp;
	return 0;
}
