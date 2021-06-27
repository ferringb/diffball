// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2013 Brian Harring <ferringb@gmail.com>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "internal.h"
#include <bzlib.h>
#include <string.h>
#include <fcntl.h>

unsigned int
cclose_bz2(cfile *cfh, void *data)
{
	bz_stream *bzs = (bz_stream *)data;
	if (bzs)
	{
		if (cfh->access_flags & CFILE_WONLY)
		{
			BZ2_bzCompressEnd(bzs);
		}
		else
		{
			BZ2_bzDecompressEnd(bzs);
		}
		free(bzs);
	}
	return 0;
}

ssize_t
cseek_bz2(cfile *cfh, void *data, ssize_t offset, ssize_t data_offset, int offset_type)
{
	bz_stream *bzs = (bz_stream *)data;
	cfile_lprintf(1, "cseek: %u: bz2: data_off(%li), data.offset(%lu)\n", cfh->cfh_id, data_offset, cfh->data.offset);
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
		   bzlib to support seeking... */
		cfile_lprintf(1, "cseek: bz2: data_offset < cfh->data.offset, resetting\n");
		flag_lseek_needed(cfh);
		BZ2_bzDecompressEnd(bzs);
		bzs->bzalloc = NULL;
		bzs->bzfree = NULL;
		bzs->opaque = NULL;
		cfh->state_flags &= ~CFILE_EOF;
		BZ2_bzDecompressInit(bzs, BZIP2_VERBOSITY_LEVEL, 0);
		bzs->next_in = (char *)cfh->raw.buff;
		bzs->next_out = (char *)cfh->data.buff;
		bzs->avail_in = bzs->avail_out = 0;
		cfh->data.end = cfh->raw.end = cfh->data.pos =
			cfh->data.offset = cfh->raw.offset = cfh->raw.pos = 0;
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

	/* note bzip2 doens't use the normal return */
	return (CSEEK_ABS == offset_type ? data_offset + cfh->data.window_offset : data_offset);
}

int crefill_bz2(cfile *cfh, void *data)
{
	size_t x;
	int err;
	bz_stream *bzs = (bz_stream *)data;
	assert(bzs->total_out_lo32 >= cfh->data.offset + cfh->data.end);
	if (cfh->state_flags & CFILE_EOF)
	{
		cfile_lprintf(1, "crefill: %u: bz2: CFILE_EOF flagged, returning 0\n", cfh->cfh_id);
		cfh->data.offset += cfh->data.end;
		cfh->data.end = cfh->data.pos = 0;
	}
	else
	{
		cfh->data.offset += cfh->data.end;
		cfile_lprintf(1, "crefill: %u: bz2, refilling data\n", cfh->cfh_id);
		bzs->avail_out = cfh->data.size;
		bzs->next_out = (char *)cfh->data.buff;
		do
		{
			if (0 == bzs->avail_in && (cfh->raw.offset +
										   (cfh->raw.end - bzs->avail_in) <
									   cfh->raw.window_len))
			{
				cfile_lprintf(1, "crefill: %u: bz2, refilling raw: ", cfh->cfh_id);
				if (ensure_lseek_position(cfh))
				{
					return (cfh->err = IO_ERROR);
				}
				cfh->raw.offset += cfh->raw.end;
				x = read(cfh->raw_fh, cfh->raw.buff, MIN(cfh->raw.size, cfh->raw.window_len - cfh->raw.offset));
				cfile_lprintf(1, "read %lu of possible %lu\n", x, cfh->raw.size);
				bzs->avail_in = cfh->raw.end = x;
				cfh->raw.pos = 0;
				bzs->next_in = (char *)cfh->raw.buff;
			}
			err = BZ2_bzDecompress(bzs);

			/* note, this doesn't handle BZ_DATA_ERROR/BZ_DATA_ERROR_MAGIC ,
			which should be handled (rather then aborting) */
			if (err != BZ_OK && err != BZ_STREAM_END)
			{
				eprintf("hmm, bzip2 didn't return BZ_OK, borking cause of %i.\n", err);
				return IO_ERROR;
			}
			if (err == BZ_STREAM_END)
			{
				cfile_lprintf(1, "encountered stream_end\n");
				/* this doesn't handle u64 yet, so make it do so at some point*/
				cfh->data.window_len = MAX(bzs->total_out_lo32,
										   cfh->data.window_len);
				cfh->state_flags |= CFILE_EOF;
			}
		} while ((!(cfh->state_flags & CFILE_EOF)) && bzs->avail_out > 0);
		cfh->data.end = cfh->data.size - bzs->avail_out;
		cfh->data.pos = 0;
	}
	return 0;
}

int internal_copen_bzip2(cfile *cfh)
{
	cfh->data.size = CFILE_DEFAULT_BUFFER_SIZE;
	cfh->raw.size = CFILE_DEFAULT_BUFFER_SIZE;
	bz_stream *bzs = (bz_stream *)malloc(sizeof(bz_stream));
	cfh->io.data = (void *)bzs;
	if (!bzs)
	{
		cfile_lprintf(1, "mem error for bz2 stream\n");
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
	bzs->bzalloc = NULL;
	bzs->bzfree = NULL;
	bzs->opaque = NULL;

	BZ2_bzDecompressInit(bzs, BZIP2_VERBOSITY_LEVEL, 0);
	bzs->next_in = (char *)cfh->raw.buff;
	bzs->next_out = (char *)cfh->data.buff;
	bzs->avail_in = bzs->avail_out = 0;

	cfh->access_flags |= CFILE_SEEK_IS_COSTLY;
	cfh->raw.pos = cfh->raw.offset = cfh->raw.end = cfh->data.pos =
		cfh->data.offset = cfh->data.end = cfh->raw.write_end = cfh->raw.write_start = 0;
	cfh->data.offset = 10;
	cfh->io.close = cclose_bz2;
	cfh->io.seek = cseek_bz2;
	cfh->io.refill = crefill_bz2;

	return 0;
}
