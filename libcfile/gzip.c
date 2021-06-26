// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2013 Brian Harring <ferringb@gmail.com>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "internal.h"
#include <zlib.h>
#include <string.h>
#include <fcntl.h>

unsigned int
internal_gzopen(cfile *cfh, z_stream *zs)
{
	unsigned int x, y, skip;
	dcprintf("internal gz_open called\n");
	assert(zs != NULL);
	zs->next_in = cfh->raw.buff;
	zs->next_out = cfh->data.buff;
	zs->avail_out = zs->avail_in = 0;
	zs->zalloc = NULL;
	zs->zfree = NULL;
	zs->opaque = NULL;
	cfh->raw.pos = cfh->raw.end = 0;
	/* skip the headers */
	cfh->raw.offset = 2;

	if (ensure_lseek_position(cfh))
	{
		dcprintf("internal_gzopen:%u ensure_lseek_position failed.n", __LINE__);
		return IO_ERROR;
	}
	x = read(cfh->raw_fh, cfh->raw.buff, MIN(cfh->raw.size, cfh->raw.window_len - 2));
	cfh->raw.end = x;

	if (inflateInit2(zs, -MAX_WBITS) != Z_OK)
	{
		dcprintf("internal_gzopen:%u inflateInit2 failed\n", __LINE__);
		return IO_ERROR;
	}

/* pulled straight out of zlib's gzio.c. */
#define GZ_RESERVED 0xE0
#define GZ_HEAD_CRC 0x02
#define GZ_EXTRA_FIELD 0x04
#define GZ_ORIG_NAME 0x08
#define GZ_COMMENT 0x10

	if (cfh->raw.buff[0] != Z_DEFLATED || (cfh->raw.buff[1] & GZ_RESERVED))
	{
		dcprintf("internal_gzopen:%u either !Z_DEFLATED || GZ_RESERVED\n", __LINE__);
		return IO_ERROR;
	}
	/* save flags, since it's possible the gzip header > cfh->raw.size */
	x = cfh->raw.buff[1];
	skip = 0;
	/* set position to after method,flags,time,xflags,os code */
	cfh->raw.pos = 8;
	if (x & GZ_EXTRA_FIELD)
	{
		cfh->raw.pos += ((cfh->raw.buff[7] << 8) | cfh->raw.buff[6]) + 4;
		if (cfh->raw.pos > cfh->raw.end)
		{
			cfh->raw.offset += cfh->raw.pos;
			cfh->raw.pos = cfh->raw.end;
			if (raw_ensure_position(cfh))
			{
				return IO_ERROR;
			}
		}
	}
	if (x & GZ_ORIG_NAME)
		skip++;
	if (x & GZ_COMMENT)
		skip++;
	dcprintf("internal_gzopen:%u skip=%u\n", __LINE__, skip);
	dcprintf("initial off(%lu), pos(%lu)\n", cfh->raw.offset, cfh->raw.pos);
	while (skip)
	{
		while (cfh->raw.buff[cfh->raw.pos] != 0)
		{
			if (cfh->raw.end == cfh->raw.pos)
			{
				cfh->raw.offset += cfh->raw.end;
				y = read(cfh->raw_fh, cfh->raw.buff, MIN(cfh->raw.size, cfh->raw.window_len - cfh->raw.offset));
				cfh->raw.end = y;
				cfh->raw.pos = 0;
			}
			else
			{
				cfh->raw.pos++;
			}
		}
		cfh->raw.pos++;
		skip--;
	}
	dcprintf("after skip off(%lu), pos(%lu)\n", cfh->raw.offset, cfh->raw.pos);
	if (x & GZ_HEAD_CRC)
	{
		cfh->raw.pos += 2;
		if (cfh->raw.pos >= cfh->raw.end)
		{
			cfh->raw.offset += cfh->raw.pos;
			cfh->raw.pos = cfh->raw.end = 0;
		}
	}

	zs->avail_in = cfh->raw.end - cfh->raw.pos;
	zs->next_in = cfh->raw.buff + cfh->raw.pos;
	cfh->data.pos = cfh->data.offset = cfh->data.end = 0;
	return 0L;
}

unsigned int
cclose_gzip(cfile *cfh, void *data)
{
	z_stream *zs = (z_stream *)data;
	if (zs)
	{
		if (cfh->access_flags & CFILE_WONLY)
		{
			deflateEnd(zs);
		}
		else
		{
			inflateEnd(zs);
		}
		free(zs);
	}
	return 0;
}

ssize_t
cseek_gzip(cfile *cfh, void *data, ssize_t offset, ssize_t data_offset, int offset_type)
{
	z_stream *zs = (z_stream *)data;
	dcprintf("cseek: %u: gz: data_off(%li), data.offset(%lu)\n", cfh->cfh_id, data_offset, cfh->data.offset);
	if (offset < 0)
	{
		// this sucks.  quick kludge to find the eof, then set data_offset appropriately.
		// do something better.
		dcprintf("decompressed total_len isn't know, so having to decompress the whole shebang\n");
		while (!(cfh->state_flags & CFILE_EOF))
		{
			crefill(cfh);
		}
		cfh->data.window_len = cfh->data.offset + cfh->data.end;
		data_offset += cfh->data.window_len;
		dcprintf("setting total_len(%lu); data.offset(%li), seek_target(%li)\n", cfh->data.window_len, cfh->data.offset, data_offset);
	}

	if (data_offset < cfh->data.offset)
	{
		/* note this ain't optimal, but the alternative is modifying
		   zlib to support seeking... */
		dcprintf("cseek: gz: data_offset < cfh->data.offset, resetting\n");
		flag_lseek_needed(cfh);
		inflateEnd(zs);
		cfh->state_flags &= ~CFILE_EOF;
		internal_gzopen(cfh, zs);
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

	/* note gzip doens't use the normal return */
	return (CSEEK_ABS == offset_type ? data_offset + cfh->data.window_offset : data_offset);
}

int crefill_gzip(cfile *cfh, void *data)
{
	size_t x;
	int err;
	z_stream *zs = (z_stream *)data;
	assert(zs->total_out >= cfh->data.offset + cfh->data.end);
	if (cfh->state_flags & CFILE_EOF)
	{
		dcprintf("crefill: %u: gz: CFILE_EOF flagged, returning 0\n", cfh->cfh_id);
		cfh->data.offset += cfh->data.end;
		cfh->data.end = cfh->data.pos = 0;
	}
	else
	{
		cfh->data.offset += cfh->data.end;
		dcprintf("crefill: %u: zs, refilling data\n", cfh->cfh_id);
		zs->avail_out = cfh->data.size;
		zs->next_out = cfh->data.buff;
		do
		{
			if (0 == zs->avail_in && (cfh->raw.offset +
										  (cfh->raw.end - zs->avail_in) <
									  cfh->raw.window_len))
			{
				dcprintf("crefill: %u: zs, refilling raw: ", cfh->cfh_id);
				if (ensure_lseek_position(cfh))
				{
					v1printf("encountered IO_ERROR in gz crefill: %u\n", __LINE__);
					return IO_ERROR;
				}
				cfh->raw.offset += cfh->raw.end;
				x = read(cfh->raw_fh, cfh->raw.buff, MIN(cfh->raw.size, cfh->raw.window_len - cfh->raw.offset));
				dcprintf("read %lu of possible %lu\n", x, cfh->raw.size);
				zs->avail_in = cfh->raw.end = x;
				cfh->raw.pos = 0;
				zs->next_in = cfh->raw.buff;
			}
			err = inflate(zs, Z_NO_FLUSH);

			if (err != Z_OK && err != Z_STREAM_END)
			{
				v1printf("encountered err(%i) in gz crefill:%u\n", err, __LINE__);
				return IO_ERROR;
			}
			if (err == Z_STREAM_END)
			{
				dcprintf("encountered stream_end\n");
				/* this doesn't handle u64 yet, so make it do so at some point*/
				cfh->data.window_len = MAX(zs->total_out,
										   cfh->data.window_len);
				cfh->state_flags |= CFILE_EOF;
			}
		} while ((!(cfh->state_flags & CFILE_EOF)) && zs->avail_out > 0);
		cfh->data.end = cfh->data.size - zs->avail_out;
		cfh->data.pos = 0;
	}
	return 0;
}

int internal_copen_gzip(cfile *cfh)
{
	cfh->data.size = CFILE_DEFAULT_BUFFER_SIZE;
	cfh->raw.size = CFILE_DEFAULT_BUFFER_SIZE;
	z_stream *zs = (z_stream *)malloc(sizeof(z_stream));
	cfh->io.data = zs;
	if (!zs)
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
	cfh->access_flags |= CFILE_SEEK_IS_COSTLY;
	cfh->raw.write_end = cfh->raw.write_start = cfh->data.write_start = cfh->data.write_end = 0;
	internal_gzopen(cfh, zs);
	cfh->io.close = cclose_gzip;
	cfh->io.refill = crefill_gzip;
	cfh->io.seek = cseek_gzip;
	return 0;
}
