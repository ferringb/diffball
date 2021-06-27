// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2013 Brian Harring <ferringb@gmail.com>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "internal.h"
#include <lzma.h>
#include <string.h>
#include <fcntl.h>

unsigned int
cclose_xz(cfile *cfh, void *data)
{
	lzma_stream *xzs = (lzma_stream *)data;
	if (xzs)
	{
		lzma_end(xzs);
		free(xzs);
	}
	return 0;
}

ssize_t
cseek_xz(cfile *cfh, void *data, ssize_t offset, ssize_t data_offset, int offset_type)
{
	lzma_stream *xzs = (lzma_stream *)data;
	cfile_lprintf(1, "cseek: %u: xz: data_off(%li), data.offset(%lu)\n", cfh->cfh_id, data_offset, cfh->data.offset);
	if (data_offset < 0)
	{
		// this sucks.  quick kludge to find the eof, then set data_offset appropriately.
		// do something better.
		cfile_lprintf(1, "decompressed total_len isn't know, so having to decompress the whole shebang\n");
		while (!(cfh->state_flags & CFILE_EOF))
		{
			crefill(cfh);
		}
		cfh->data.window_len = cfh->data.offset + cfh->data.end;
		data_offset += cfh->data.window_len;
		cfile_lprintf(1, "setting total_len(%lu); data.offset(%li), seek_target(%li)\n", cfh->data.window_len, cfh->data.offset, data_offset);
	}
	if (data_offset < cfh->data.offset)
	{
		/* note this ain't optimal, but the alternative is modifying
		   lzma to support seeking... */
		cfile_lprintf(1, "cseek: xz: data_offset < cfh->data.offset, resetting\n");
		flag_lseek_needed(cfh);
		cfh->state_flags &= ~CFILE_EOF;
		if (lzma_stream_decoder(xzs, UINT64_MAX, LZMA_TELL_UNSUPPORTED_CHECK) != LZMA_OK)
		{
			return IO_ERROR;
		}
		cfh->raw.pos = cfh->raw.offset = cfh->raw.end = cfh->data.pos =
			cfh->data.offset = cfh->data.end = 0;
		if (ensure_lseek_position(cfh))
		{
			return (cfh->err = IO_ERROR);
		}
		if (cfh->data.window_offset)
		{
			while (cfh->data.offset + cfh->data.end < cfh->data.window_offset)
			{
				if (crefill(cfh) <= 0)
				{
					return EOF_ERROR;
				}
			}
			cfh->data.offset -= cfh->data.window_offset;
		}
	}
	else
	{
		if (ensure_lseek_position(cfh))
		{
			return (cfh->err = IO_ERROR);
		}
	}
	while (cfh->data.offset + cfh->data.end < data_offset)
	{
		if (crefill(cfh) <= 0)
		{
			return EOF_ERROR;
		}
	}
	cfh->data.pos = data_offset - cfh->data.offset;

	/* note xz doens't use the normal return */
	return (CSEEK_ABS == offset_type ? data_offset + cfh->data.window_offset : data_offset);
}

int crefill_xz(cfile *cfh, void *data)
{
	size_t x;
	lzma_ret xz_err;
	lzma_stream *xzs = (lzma_stream *)data;

	assert(xzs->total_out >= cfh->data.offset + cfh->data.end);
	if (cfh->state_flags & CFILE_EOF)
	{
		cfile_lprintf(1, "crefill: %u: xz: CFILE_EOF flagged, returning 0\n", cfh->cfh_id);
		cfh->data.offset += cfh->data.end;
		cfh->data.end = cfh->data.pos = 0;
	}
	else
	{
		cfh->data.offset += cfh->data.end;
		cfile_lprintf(1, "crefill: %u: xzs, refilling data\n", cfh->cfh_id);
		xzs->avail_out = cfh->data.size;
		xzs->next_out = cfh->data.buff;
		do
		{
			if (0 == xzs->avail_in && (cfh->raw.offset +
										   (cfh->raw.end - xzs->avail_in) <
									   cfh->raw.window_len))
			{
				cfile_lprintf(1, "crefill: %u: xzs, refilling raw: ", cfh->cfh_id);
				if (ensure_lseek_position(cfh))
				{
					cfile_lprintf(1, "encountered IO_ERROR in xz crefill: %u\n", __LINE__);
					return IO_ERROR;
				}
				cfh->raw.offset += cfh->raw.end;
				x = read(cfh->raw_fh, cfh->raw.buff, MIN(cfh->raw.size, cfh->raw.window_len - cfh->raw.offset));
				cfile_lprintf(1, "read %lu of possible %lu\n", x, cfh->raw.size);
				xzs->avail_in = cfh->raw.end = x;
				cfh->raw.pos = 0;
				xzs->next_in = cfh->raw.buff;
			}
			xz_err = lzma_code(xzs, LZMA_RUN);
			if (LZMA_OK != xz_err && LZMA_STREAM_END != xz_err)
			{
				cfile_lprintf(1, "encountered err(%i) in xz crefill:%u\n", xz_err, __LINE__);
				return IO_ERROR;
			}
			if (LZMA_STREAM_END == xz_err)
			{
				cfile_lprintf(1, "encountered stream_end\n");
				cfh->data.window_len = MAX(xzs->total_out,
										   cfh->data.window_len);
				cfh->state_flags |= CFILE_EOF;
			}
		} while ((!(cfh->state_flags & CFILE_EOF)) && xzs->avail_out > 0);
		cfh->data.end = cfh->data.size - xzs->avail_out;
		cfh->data.pos = 0;
	}
	return 0;
}

int internal_copen_xz(cfile *cfh)
{
	cfh->data.size = CFILE_DEFAULT_BUFFER_SIZE;
	cfh->raw.size = CFILE_DEFAULT_BUFFER_SIZE;
	lzma_stream *xzs = (lzma_stream *)malloc(sizeof(lzma_stream));
	cfh->io.data = (void *)xzs;
	if (!xzs)
	{
		return MEM_ERROR;
	}
	else if ((cfh->data.buff = (unsigned char *)malloc(cfh->data.size)) == NULL)
	{
		return MEM_ERROR;
	}
	else if ((cfh->raw.buff = (unsigned char *)malloc(cfh->raw.size)) == NULL)
	{
		return MEM_ERROR;
	}
	lzma_stream tmp = LZMA_STREAM_INIT;
	memcpy(xzs, &tmp, sizeof(lzma_stream));
	cfh->raw.write_end = cfh->raw.write_start = cfh->data.write_start =
		cfh->data.write_end = 0;
	/* compression unsupported for now */
	if (lzma_stream_decoder(xzs, UINT64_MAX, LZMA_TELL_UNSUPPORTED_CHECK) != LZMA_OK)
	{
		return IO_ERROR;
	}
	cfh->access_flags |= CFILE_SEEK_IS_COSTLY;
	cfh->raw.pos = cfh->raw.offset = cfh->raw.end = cfh->data.pos =
		cfh->data.offset = cfh->data.end = 0;
	cfh->io.close = cclose_xz;
	cfh->io.seek = cseek_xz;
	cfh->io.refill = crefill_xz;
	return 0;
}
