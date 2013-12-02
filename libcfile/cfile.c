/*
  Copyright (C) 2003-2005 Brian Harring

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
#include "defs.h"
#include <string.h>
#include <fcntl.h>
#include "cfile.h"

#define MIN(x,y) ((x) < (y) ? (x) : (y))
unsigned int cfile_verbosity;
unsigned int largefile_support = 0;
#ifdef _LARGEFILE_SOURCE
largefile_support = 1;
#endif

unsigned int internal_gzopen(cfile *cfh);

/* quick kludge */
signed long
cfile_identify_compressor(int fh)
{
	unsigned char buff[5]; // XZ magic number length is 5
	if(read(fh, buff, 5)!=5) {
		return EOF_ERROR;
	} else {
		lseek(fh, -5, SEEK_CUR);
	}
	if(memcmp(buff, "BZ", 2)==0) {
		return BZIP2_COMPRESSOR;
	} else if(0x1f==buff[0] && 0x8b==buff[1]) {
		return GZIP_COMPRESSOR;
	} else if(0xfd==buff[0] && '7'==buff[1] && 'z'==buff[2] && 'X'==buff[3] && 'Z'==buff[4]) {
		return XZ_COMPRESSOR;
	}
	return NO_COMPRESSOR;
}


cfile *
copen_dup_cfh(cfile *cfh)
{
	cfile *dup;
	dup = (cfile *)malloc(sizeof(cfile));
	if(dup == NULL) {
		return NULL;
	}
	if(copen_child_cfh(dup, cfh, cfh->data_fh_offset, 
		cfh->data_total_len == 0 ? 0 : cfh->data_fh_offset + cfh->data_total_len,
		cfh->compressor_type, cfh->access_flags)) {
		free(dup);
		return NULL;
	}
	return dup;
}

int
copen_child_cfh(cfile *cfh, cfile *parent, size_t fh_start, 
	size_t fh_end, unsigned int compressor_type, unsigned int 
	access_flags)
{
	int err = 0;
	dcprintf("copen_child_cfh: %u: calling internal_copen\n", parent->cfh_id);
	cfh->state_flags = CFILE_CHILD_CFH | (~CFILE_SEEK_IS_COSTLY & parent->access_flags);
	cfh->lseek_info.last_ptr = &parent->lseek_info.parent.last;
	parent->lseek_info.parent.handle_count++;
	dcprintf("setting child id=%u\n", parent->lseek_info.parent.handle_count);
	cfh->cfh_id = parent->lseek_info.parent.handle_count;
	if(parent->compressor_type != NO_COMPRESSOR) {
		if(compressor_type != NO_COMPRESSOR) {
			/* cfile doesn't handle this. deal. */
			v0printf("unable to open a compressor w/in a compressor, crapping out.\n");
			return UNSUPPORTED_OPT;
		}
		err = internal_copen(cfh, parent->raw_fh, parent->raw_fh_offset, parent->raw_total_len, 
			fh_start, fh_end, parent->compressor_type, access_flags);
	} else {
		err = internal_copen(cfh, parent->raw_fh, fh_start, fh_end, 0, 0,
			compressor_type, access_flags);
	}
	if(parent->state_flags & CFILE_MEM_ALIAS) {
		assert((cfh->access_flags & CFILE_WONLY) == 0);
		free(cfh->data.buff);
		cfh->data.buff = parent->data.buff + fh_start;
		cfh->data.size = cfh->data.end = fh_end - fh_start;
		cfh->state_flags |= CFILE_MEM_ALIAS;
	}
	return err;
}

int
copen_mem(cfile *cfh, unsigned char *buff, size_t len, unsigned int compressor_type, unsigned int access_flags)
{
	int ret;
	size_t len2;
	if(buff == NULL) {
		if(access_flags & CFILE_RONLY)
			return UNSUPPORTED_OPT;
	} else {
		if( (access_flags & CFILE_WONLY) && buff != NULL) {
			v0printf("supplying an initial buffer for a write mem alias isn't totally supported yet, sorry")
			return UNSUPPORTED_OPT;
		}
	}
	if(access_flags & CFILE_WONLY)
		len2 = 0;
	else
		len2 = len;
	
	if(compressor_type != NO_COMPRESSOR) {
		return UNSUPPORTED_OPT;
	}
	cfh->state_flags = CFILE_MEM_ALIAS;
	cfh->lseek_info.parent.handle_count = 0;
	cfh->cfh_id = 1;
	// pass in a daft handle
	ret = internal_copen(cfh, -1, 0, len2, 0,0, compressor_type, access_flags);
	if(ret != 0)
		return ret;
	// mangle things a bit.
	if(buff != NULL) {
		free(cfh->data.buff);
		cfh->data.buff = buff;
		cfh->data_total_len = cfh->data.end = cfh->data.size = len;
	}
	cfh->lseek_info.parent.last = cfh->cfh_id;

	return 0;
}

int
copen(cfile *cfh, const char *filename, unsigned int compressor_type, unsigned int access_flags)
{
	dcprintf("copen: calling internal_copen\n");
	struct stat st;
	int fd, flags;
	size_t size = 0;
	flags =0;
	if(access_flags & CFILE_RONLY) {
		flags = O_RDONLY;
		if(stat(filename, &st))
			return IO_ERROR;
		size = st.st_size;
	} else if(access_flags & CFILE_WONLY) {
		if(access_flags & CFILE_NEW) {
			flags = O_RDWR | O_CREAT | O_TRUNC;
			size = 0;
		} else {
			flags = O_WRONLY;
			if(stat(filename, &st))
				return IO_ERROR;
			size = st.st_size;
		}
	}
	fd=open(filename, flags, 0644);
	if(fd < 0) {
		return IO_ERROR;
	}
	cfh->state_flags = CFILE_OPEN_FH;
	cfh->lseek_info.parent.last = 0;
	cfh->lseek_info.parent.handle_count =1;
	cfh->cfh_id = 1;
	return internal_copen(cfh, fd, 0, size, 0,0,
		compressor_type, access_flags);
}

int
copen_dup_fd(cfile *cfh, int fh, size_t fh_start, size_t fh_end,
	unsigned int compressor_type, unsigned int access_flags)
{
	dcprintf("copen: calling internal_copen\n");
	cfh->state_flags = 0;
	cfh->lseek_info.parent.last = 0;
	cfh->lseek_info.parent.handle_count =1;
	cfh->cfh_id = 1;
	return internal_copen(cfh, fh, fh_start, fh_end, 0,0,
	compressor_type, access_flags);
}

int
internal_copen_bzip2(cfile *cfh)
{
	cfh->data.size = CFILE_DEFAULT_BUFFER_SIZE;
	cfh->raw.size = CFILE_DEFAULT_BUFFER_SIZE;
	bz_stream *bzs = (bz_stream *)malloc(sizeof(bz_stream));
	cfh->io.data = (void *)bzs;
	if (!bzs) {
		v1printf("mem error for bz2 stream\n");
		return MEM_ERROR;
	} else if((cfh->data.buff = (unsigned char *)malloc(cfh->data.size))==NULL) {
		return MEM_ERROR;
	} else if((cfh->raw.buff = (unsigned char *)malloc(cfh->raw.size))==NULL) {
		return MEM_ERROR;
	}
	bzs->bzalloc = NULL;
	bzs->bzfree =  NULL;
	bzs->opaque = NULL;
/*	
	if(cfh->access_flags & CFILE_WONLY)
		BZ2_bzCompressInit(bzs, BZIP2_DEFAULT_COMPRESS_LEVEL, 
			BZIP2_VERBOSITY_LEVEL, BZIP2_DEFAULT_WORK_LEVEL);
		bzs->next_in = cfh->data.buff;
		bzs->next_out = cfh->raw.buff;
	else {
*/
		BZ2_bzDecompressInit(bzs, BZIP2_VERBOSITY_LEVEL, 0);
		bzs->next_in = (char *)cfh->raw.buff;
		bzs->next_out = (char *)cfh->data.buff;
		bzs->avail_in = bzs->avail_out = 0;
//	}
	cfh->access_flags |= CFILE_SEEK_IS_COSTLY;
	cfh->raw.pos = cfh->raw.offset  = cfh->raw.end = cfh->data.pos = 
		cfh->data.offset = cfh->data.end = cfh->raw.write_end = cfh->raw.write_start = 0;
	cfh->data.offset = 10;
	if(0 != cseek(cfh, 0, CSEEK_FSTART)) {
		return (cfh->err = IO_ERROR);
	}
	return 0;
}

int
internal_copen_gz(cfile *cfh)
{
	cfh->data.size = CFILE_DEFAULT_BUFFER_SIZE;
	cfh->raw.size = CFILE_DEFAULT_BUFFER_SIZE;
	z_stream *zs = (z_stream *)malloc(sizeof(z_stream));
	cfh->io.data = zs;
	if (!zs) {
		return MEM_ERROR;
	} else if((cfh->data.buff = (unsigned char *)malloc(cfh->data.size))==NULL) {
		return MEM_ERROR;
	} else if((cfh->raw.buff = (unsigned char *)malloc(cfh->raw.size))==NULL) {
		return MEM_ERROR;
	}
	cfh->access_flags |= CFILE_SEEK_IS_COSTLY;
	cfh->raw.write_end = cfh->raw.write_start = cfh->data.write_start = cfh->data.write_end = 0;
	internal_gzopen(cfh);
	return 0;
}

int
internal_copen_xz(cfile *cfh)
{
	cfh->data.size = CFILE_DEFAULT_BUFFER_SIZE;
	cfh->raw.size = CFILE_DEFAULT_BUFFER_SIZE;
	lzma_stream *xzs = (lzma_stream *)malloc(sizeof(lzma_stream));
	cfh->io.data = (void *)xzs;
	if (!xzs) {
		return MEM_ERROR;
	} else if((cfh->data.buff = (unsigned char *)malloc(cfh->data.size))==NULL) {
		return MEM_ERROR;
	} else if((cfh->raw.buff = (unsigned char *)malloc(cfh->raw.size))==NULL) {
		return MEM_ERROR;
	}
	lzma_stream tmp = LZMA_STREAM_INIT;
	memcpy(xzs, &tmp, sizeof(lzma_stream));
	cfh->raw.write_end = cfh->raw.write_start = cfh->data.write_start =
		cfh->data.write_end = 0;
	/* compression unsupported for now */
	if(lzma_stream_decoder(xzs, UINT64_MAX, LZMA_TELL_UNSUPPORTED_CHECK)!=LZMA_OK){
		return IO_ERROR;
	}
	cfh->access_flags |= CFILE_SEEK_IS_COSTLY;
	cfh->raw.pos = cfh->raw.offset = cfh->raw.end = cfh->data.pos =
		cfh->data.offset = cfh->data.end = 0;
	return 0;
}

int
internal_copen_no_comp(cfile *cfh)
{
	dcprintf("copen: opening w/ no_compressor\n");
	if(cfh->access_flags & CFILE_BUFFER_ALL) {
		cfh->data.size = cfh->data_total_len;
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
	return 0;
}

int
internal_copen(cfile *cfh, int fh, size_t raw_fh_start, size_t raw_fh_end, 
	size_t data_fh_start, size_t data_fh_end, unsigned int compressor_type, unsigned int access_flags)
{
	signed long ret_val;
	/* this will need adjustment for compressed files */
	cfh->raw_fh = fh;

	assert(raw_fh_start <= raw_fh_end);
	cfh->access_flags = access_flags;

	if(AUTODETECT_COMPRESSOR == compressor_type) {
		dcprintf("copen: autodetecting comp_type: ");
		ret_val = cfile_identify_compressor(fh);
		if(ret_val < 0) {
			return IO_ERROR;
		}
		dcprintf("got %ld\n", ret_val);
		cfh->compressor_type = ret_val;
		flag_lseek_needed(cfh);
	} else {
		cfh->compressor_type = compressor_type;
	}
	if(cfh->compressor_type != NO_COMPRESSOR) {
		if((cfh->access_flags & CFILE_WR) == CFILE_WR) {
			// one or the other, not both.
			return IO_ERROR;
		} else if(cfh->access_flags & CFILE_RONLY) {
			cfh->access_flags |= CFILE_SEEKABLE;
		}
	} else {
		cfh->access_flags |= CFILE_SEEKABLE;
	}
/*	if(! ((cfh->access_flags & CFILE_WONLY) && 
		(compressor_type != NO_COMPRESSOR)) ){
		cfh->access_flags |= CFILE_SEEKABLE;
	}
*/	
	int result = 0;
	switch(cfh->compressor_type) {
	case NO_COMPRESSOR:
		cfh->data_fh_offset = raw_fh_start;
		cfh->data_total_len = raw_fh_end - raw_fh_start;
		result = internal_copen_no_comp(cfh);
 		break;
 		
	case BZIP2_COMPRESSOR:
		cfh->raw_fh_offset = raw_fh_start;
		cfh->raw_total_len = raw_fh_end - raw_fh_start;
		cfh->data_fh_offset = data_fh_start;
		cfh->data_total_len = (data_fh_end == 0 ? 0 : data_fh_end - data_fh_start);
		result = internal_copen_bzip2(cfh);
		break;

	case GZIP_COMPRESSOR:
		cfh->raw_fh_offset = raw_fh_start;
		cfh->raw_total_len = raw_fh_end - raw_fh_start;
		cfh->data_fh_offset = data_fh_start;
		cfh->data_total_len = (data_fh_end == 0 ? 0 : data_fh_end - data_fh_start);
		result = internal_copen_gz(cfh);
		break;

	case XZ_COMPRESSOR:
		cfh->raw_fh_offset = raw_fh_start;
		cfh->raw_total_len = raw_fh_end - raw_fh_start;
		cfh->data_fh_offset = data_fh_start;
		cfh->data_total_len = (data_fh_end == 0 ? 0 : data_fh_end - data_fh_start);
		result = internal_copen_xz(cfh);
		break;
	}
	/* no longer in use.  leaving it as a reminder for updating when 
		switching over to the full/correct sub-window opening */
//	cfh->state_flags |= CFILE_SEEK_NEEDED;
	return result;
}

unsigned int
internal_gzopen(cfile *cfh)
{
	unsigned int x, y, skip;
	z_stream *zs = cfh->io.data;
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
		
	if(ensure_lseek_position(cfh)) {
		dcprintf("internal_gzopen:%u ensure_lseek_position failed.n", __LINE__);
		return IO_ERROR;
	}
	x = read(cfh->raw_fh, cfh->raw.buff, MIN(cfh->raw.size, 
		cfh->raw_total_len -2));
	cfh->raw.end = x;

	if(inflateInit2(zs, -MAX_WBITS) != Z_OK) {
		dcprintf("internal_gzopen:%u inflateInit2 failed\n", __LINE__);
		return IO_ERROR;
	}

/* pulled straight out of zlib's gzio.c. */
#define GZ_RESERVED		0xE0
#define GZ_HEAD_CRC		0x02
#define GZ_EXTRA_FIELD		0x04
#define GZ_ORIG_NAME		0x08
#define GZ_COMMENT		0x10

	if(cfh->raw.buff[0]!= Z_DEFLATED || (cfh->raw.buff[1] & GZ_RESERVED)) {
		dcprintf("internal_gzopen:%u either !Z_DEFLATED || GZ_RESERVED\n", __LINE__);
		return IO_ERROR;
	}
	/* save flags, since it's possible the gzip header > cfh->raw.size */
	x = cfh->raw.buff[1];
	skip = 0;
	/* set position to after method,flags,time,xflags,os code */
	cfh->raw.pos = 8;
	if(x & GZ_EXTRA_FIELD) {
		cfh->raw.pos += ((cfh->raw.buff[7] << 8) | cfh->raw.buff[6]) + 4;
		if(cfh->raw.pos > cfh->raw.end) {
			cfh->raw.offset += cfh->raw.pos;
			cfh->raw.pos = cfh->raw.end;
			if(raw_ensure_position(cfh)) {
				return IO_ERROR;
			}
		}
	}
	if(x & GZ_ORIG_NAME)
		skip++;
	if(x & GZ_COMMENT)
		skip++;
	dcprintf("internal_gzopen:%u skip=%u\n", __LINE__, skip);
	dcprintf("initial off(%lu), pos(%lu)\n", cfh->raw.offset, cfh->raw.pos);
	while(skip) {
		while(cfh->raw.buff[cfh->raw.pos]!=0) {
			if(cfh->raw.end == cfh->raw.pos) {
				cfh->raw.offset += cfh->raw.end;
				y = read(cfh->raw_fh, cfh->raw.buff, MIN(cfh->raw.size, 
					cfh->raw_total_len - cfh->raw.offset));
				cfh->raw.end = y;
				cfh->raw.pos = 0;
			} else {
				cfh->raw.pos++;
			}
		}
		cfh->raw.pos++;
		skip--;
	}
	dcprintf("after skip off(%lu), pos(%lu)\n", cfh->raw.offset, cfh->raw.pos);
	if(x & GZ_HEAD_CRC) {
		cfh->raw.pos +=2;
		if(cfh->raw.pos >= cfh->raw.end) {
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
cclose_bz2(cfile *cfh)
{
	bz_stream *bzs = cfh->io.data;
	if (bzs) {
		if(cfh->access_flags & CFILE_WONLY) {
			BZ2_bzCompressEnd(bzs);
		} else {
			BZ2_bzDecompressEnd(bzs);
		}
		free(bzs);
		cfh->io.data = NULL;
	}
	return 0;
}

unsigned int
cclose_gz(cfile *cfh)
{
	z_stream *zs = cfh->io.data;
	if (zs) {
		if(cfh->access_flags & CFILE_WONLY) {
			deflateEnd(zs);
		} else {
			inflateEnd(zs);
		}
		free(zs);
		cfh->io.data = NULL;
	}
	return 0;
}

unsigned int
cclose_xz(cfile *cfh)
{
	lzma_stream *xzs = cfh->io.data;
	if(xzs) {
		lzma_end(xzs);
		free(xzs);
		cfh->io.data = NULL;
	}
	return 0;
}

unsigned int
cclose(cfile *cfh)
{
	if(cfh->access_flags & CFILE_WONLY) {
		cflush(cfh);
	}
	dcprintf("id(%u), data_size=%lu, raw_size=%lu\n", cfh->cfh_id, cfh->data.size, cfh->raw.size);
	if(! ( cfh->state_flags & CFILE_MEM_ALIAS)) {
		if(cfh->data.buff)
			free(cfh->data.buff);
	}
	if(cfh->raw.buff)
		free(cfh->raw.buff);
	unsigned int result = 0;
	if(cfh->compressor_type == BZIP2_COMPRESSOR) {
		result = cclose_bz2(cfh);
	}
	if(cfh->compressor_type == GZIP_COMPRESSOR) {
		result = cclose_gz(cfh);
	}
	if(cfh->compressor_type == XZ_COMPRESSOR) {
		result = cclose_xz(cfh);
	}
	/* XXX questionable */
	if(cfh->state_flags & CFILE_OPEN_FH) {
		close(cfh->raw_fh);
	}
	if(cfh->state_flags & CFILE_FREE_AT_CLOSING) {
		free(cfh);
	} else {
		cfh->raw.pos = cfh->raw.end = cfh->raw.size = cfh->raw.offset = 
			cfh->data.pos = cfh->data.end = cfh->data.size = cfh->data.offset = 
			cfh->raw_total_len = cfh->data_total_len = 0;
	}
	return result;
}

ssize_t
cread(cfile *cfh, void *buff, size_t len)
{
	size_t bytes_wrote=0;
	size_t x;
	ssize_t val;
	while(bytes_wrote != len) {
		if(cfh->data.end==cfh->data.pos) {
			val = crefill(cfh);
			if(val <= 0) {
				dcprintf("%u: got an error/0 bytes, returning from cread\n", cfh->cfh_id);
				if(val==0)
					return(bytes_wrote);
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
	size_t bytes_wrote=0, x;
	if(cfh->access_flags & CFILE_RONLY && cfh->data.write_end == 0) {

		//basically, RW mode, and the first write to this buffer.
		cfh->data.write_start = cfh->data.pos;
	}
	while(bytes_wrote < len) {
		if(cfh->data.size==cfh->data.write_end) {
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
cseek_gzip(cfile *cfh, ssize_t offset, ssize_t data_offset, int offset_type)
{
	z_stream *zs = cfh->io.data;
	dcprintf("cseek: %u: gz: data_off(%li), data.offset(%lu)\n", cfh->cfh_id, data_offset, cfh->data.offset);
	if(offset < 0) {
		// this sucks.  quick kludge to find the eof, then set data_offset appropriately.
		// do something better.
		dcprintf("decompressed total_len isn't know, so having to decompress the whole shebang\n");
		while(!(cfh->state_flags & CFILE_EOF)) {
			crefill(cfh);
		}
		cfh->data_total_len = cfh->data.offset + cfh->data.end;
		data_offset += cfh->data_total_len;
		dcprintf("setting total_len(%lu); data.offset(%li), seek_target(%li)\n", cfh->data_total_len, cfh->data.offset, data_offset);
	}

	if(data_offset < cfh->data.offset ) {
		/* note this ain't optimal, but the alternative is modifying
		   zlib to support seeking... */
		dcprintf("cseek: gz: data_offset < cfh->data.offset, resetting\n");
		flag_lseek_needed(cfh);
		inflateEnd(zs);
		cfh->state_flags &= ~CFILE_EOF;
		internal_gzopen(cfh);
		if(ensure_lseek_position(cfh)) {
			return (cfh->err = IO_ERROR);
		}
		if(cfh->data_fh_offset) {
			while(cfh->data.offset + cfh->data.end < cfh->data_fh_offset) {
				if(crefill(cfh)<=0) {
					return EOF_ERROR;
				}
			}
			cfh->data.offset -= cfh->data_fh_offset;
		}
	} else {
		if(ensure_lseek_position(cfh)) {
			return (cfh->err = IO_ERROR);
		}
	}
	while(cfh->data.offset + cfh->data.end < data_offset) {
		if(crefill(cfh)<=0) {
			return EOF_ERROR;
		}
	}
	cfh->data.pos = data_offset - cfh->data.offset;

	/* note gzip doens't use the normal return */
	return (CSEEK_ABS==offset_type ? data_offset + cfh->data_fh_offset : data_offset);
}

ssize_t
cseek_bz2(cfile *cfh, ssize_t offset, ssize_t data_offset, int offset_type)
{
	bz_stream *bzs = cfh->io.data;
	dcprintf("cseek: %u: bz2: data_off(%li), data.offset(%lu)\n", cfh->cfh_id, data_offset, cfh->data.offset);
	if(data_offset < 0) {
		// this sucks.  quick kludge to find the eof, then set data_offset appropriately.
		// do something better.
		dcprintf("decompressed total_len isn't know, so having to decompress the whole shebang\n");
		while(!(cfh->state_flags & CFILE_EOF)) {
			crefill(cfh);
		}
		cfh->data_total_len = cfh->data.offset + cfh->data.end;
		data_offset += cfh->data_total_len;
		dcprintf("setting total_len(%lu); data.offset(%li), seek_target(%li)\n", cfh->data_total_len, cfh->data.offset, data_offset);
	}
	if(data_offset < cfh->data.offset ) {
		/* note this ain't optimal, but the alternative is modifying
		   bzlib to support seeking... */
		dcprintf("cseek: bz2: data_offset < cfh->data.offset, resetting\n");
		flag_lseek_needed(cfh);
		BZ2_bzDecompressEnd((bz_stream *)cfh->io.data);
		bzs->bzalloc = NULL;
		bzs->bzfree =  NULL;
		bzs->opaque = NULL;
		cfh->state_flags &= ~CFILE_EOF;
		BZ2_bzDecompressInit(bzs, BZIP2_VERBOSITY_LEVEL, 0);
		bzs->next_in = (char *)cfh->raw.buff;
		bzs->next_out = (char *)cfh->data.buff;
		bzs->avail_in = bzs->avail_out = 0;
		cfh->data.end = cfh->raw.end = cfh->data.pos =
			cfh->data.offset = cfh->raw.offset = cfh->raw.pos = 0;
		if(ensure_lseek_position(cfh)) {
			return (cfh->err = IO_ERROR);
		}
		if(cfh->data_fh_offset) {
			while(cfh->data.offset + cfh->data.end < cfh->data_fh_offset) {
				if(crefill(cfh)<=0) {
					return EOF_ERROR;
				}
			}
			cfh->data.offset -= cfh->data_fh_offset;
		}
	} else {
		if(ensure_lseek_position(cfh)) {
			return (cfh->err = IO_ERROR);
		}
	}
	while(cfh->data.offset + cfh->data.end < data_offset) {
		if(crefill(cfh)<=0) {
			return EOF_ERROR;
		}
	}
	cfh->data.pos = data_offset - cfh->data.offset;

	/* note bzip2 doens't use the normal return */
	return (CSEEK_ABS==offset_type ? data_offset + cfh->data_fh_offset : data_offset);
}

ssize_t
cseek_xz(cfile *cfh, ssize_t offset, ssize_t data_offset, int offset_type)
{
	lzma_stream *xzs = cfh->io.data;
	dcprintf("cseek: %u: xz: data_off(%li), data.offset(%lu)\n", cfh->cfh_id, data_offset, cfh->data.offset);
	if(data_offset < 0) {
		// this sucks.  quick kludge to find the eof, then set data_offset appropriately.
		// do something better.
		dcprintf("decompressed total_len isn't know, so having to decompress the whole shebang\n");
		while(!(cfh->state_flags & CFILE_EOF)) {
			crefill(cfh);
		}
		cfh->data_total_len = cfh->data.offset + cfh->data.end;
		data_offset += cfh->data_total_len;
		dcprintf("setting total_len(%lu); data.offset(%li), seek_target(%li)\n", cfh->data_total_len, cfh->data.offset, data_offset);
	}
	if(data_offset < cfh->data.offset ) {
		/* note this ain't optimal, but the alternative is modifying
		   lzma to support seeking... */
		dcprintf("cseek: xz: data_offset < cfh->data.offset, resetting\n");
		flag_lseek_needed(cfh);
		cfh->state_flags &= ~CFILE_EOF;
		if(lzma_stream_decoder(xzs, UINT64_MAX, LZMA_TELL_UNSUPPORTED_CHECK)!=LZMA_OK) {
			return IO_ERROR;
		}
		cfh->raw.pos = cfh->raw.offset = cfh->raw.end = cfh->data.pos =
			cfh->data.offset = cfh->data.end = 0;
		if(ensure_lseek_position(cfh)) {
			return (cfh->err = IO_ERROR);
		}
		if(cfh->data_fh_offset) {
			while(cfh->data.offset + cfh->data.end < cfh->data_fh_offset) {
				if(crefill(cfh)<=0) {
					return EOF_ERROR;
				}
			}
			cfh->data.offset -= cfh->data_fh_offset;
		}
	} else {
		if(ensure_lseek_position(cfh)) {
			return (cfh->err = IO_ERROR);
		}
	}
	while(cfh->data.offset + cfh->data.end < data_offset) {
		if(crefill(cfh)<=0) {
			return EOF_ERROR;
		}
	}
	cfh->data.pos = data_offset - cfh->data.offset;

	/* note xz doens't use the normal return */
	return (CSEEK_ABS==offset_type ? data_offset + cfh->data_fh_offset : data_offset);
}

ssize_t
cseek_no_comp(cfile *cfh, ssize_t offset, ssize_t data_offset, int offset_type)
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
	return (CSEEK_ABS==offset_type ? data_offset + cfh->data_fh_offset :
		data_offset);
}

ssize_t
cseek(cfile *cfh, ssize_t offset, int offset_type)
{
	ssize_t data_offset;
	if(CSEEK_ABS==offset_type) 
		data_offset = abs(offset) - cfh->data_fh_offset;
	else if (CSEEK_CUR==offset_type)
		data_offset = cfh->data.offset + cfh->data.pos + offset;
	else if (CSEEK_END==offset_type)
		data_offset = cfh->data_total_len + offset;
	else if (CSEEK_FSTART==offset_type)
		data_offset = offset;
	else
		return IO_ERROR;

	assert(data_offset >= 0 || NO_COMPRESSOR != cfh->compressor_type);

	if(cfh->access_flags & CFILE_WRITEABLE) {
		dcprintf("%u: flushing cfile prior to cseek\n", cfh->cfh_id);
		if(cflush(cfh)) {
			return IO_ERROR;
		}
	}
	/* see if the desired location is w/in the data buff */
	if(data_offset >= cfh->data.offset &&
		data_offset <  cfh->data.offset + cfh->data.size && 
		cfh->data.end > data_offset - cfh->data.offset) {

		dcprintf("cseek: %u: buffered data, repositioning pos\n", cfh->cfh_id);
		cfh->data.pos = data_offset - cfh->data.offset;
		return (CSEEK_ABS==offset_type ? data_offset + cfh->data_fh_offset: 
			data_offset);
	} else if((cfh->access_flags &! CFILE_WRITEABLE) && data_offset >= cfh->data.end + cfh->data.offset && 
		data_offset < cfh->data.end + cfh->data.size + cfh->data.offset && IS_LAST_LSEEKER(cfh) ) {

		// see if the desired location is the next page (avoid lseek + read, get read instead).
		crefill(cfh);
		if(cfh->data.end + cfh->data.offset > data_offset)
			cfh->data.pos = data_offset - cfh->data.offset;
		return (CSEEK_ABS==offset_type ? cfh->data.pos + cfh->data.offset + cfh->data_fh_offset:
			cfh->data.pos + cfh->data.offset);
	}

	assert((cfh->state_flags & CFILE_MEM_ALIAS) == 0);	

	ssize_t result = 0;
	switch(cfh->compressor_type) {
	case NO_COMPRESSOR:
		result = cseek_no_comp(cfh, offset, data_offset, offset_type);
		break;
	case GZIP_COMPRESSOR:
		result = cseek_gzip(cfh, offset, data_offset, offset_type);
		break;
	case BZIP2_COMPRESSOR:
		result = cseek_bz2(cfh, offset, data_offset, offset_type);
		break;
	case XZ_COMPRESSOR:
		result = cseek_xz(cfh, offset, data_offset, offset_type);
		break;
	}
	return result;
}

signed int
raw_ensure_position(cfile *cfh)
{
	set_last_lseeker(cfh);
	if(NO_COMPRESSOR == cfh->compressor_type) {
		return (lseek(cfh->raw_fh, cfh->data.offset + cfh->data_fh_offset +
			cfh->data.end, SEEK_SET) != 
			(cfh->data.offset + cfh->data_fh_offset + cfh->data.end));
	} else if(BZIP2_COMPRESSOR == cfh->compressor_type || 
		GZIP_COMPRESSOR == cfh->compressor_type ||
		XZ_COMPRESSOR == cfh->compressor_type) {
		return (lseek(cfh->raw_fh, cfh->raw.offset + cfh->raw_fh_offset + 
			cfh->raw.end, SEEK_SET) != (cfh->raw.offset + 
			cfh->raw_fh_offset + cfh->raw.end));
	}
	return IO_ERROR;
}

size_t
ctell(cfile *cfh, unsigned int tell_type)
{
	if(CSEEK_ABS==tell_type)
		return cfh->data_fh_offset + cfh->data.offset + cfh->data.pos;
	else if (CSEEK_FSTART==tell_type)
		return cfh->data.offset + cfh->data.pos;
	else if (CSEEK_END==tell_type)
		return cfh->data_total_len - (cfh->data.offset + cfh->data.pos);
	return 0;
}

ssize_t
cflush(cfile *cfh)
{
	/* kind of a hack, I'm afraid. */
	if(cfh->data.write_end != 0) {
		if(cfh->state_flags & CFILE_MEM_ALIAS) {
			if(cfh->data.write_end < cfh->data.size)
				return 0;
			unsigned char *p;
			size_t realloc_size;
			if(cfh->data.size >= CFILE_DEFAULT_MEM_ALIAS_W_REALLOC)
				realloc_size = cfh->data.size + CFILE_DEFAULT_MEM_ALIAS_W_REALLOC;
			else
				realloc_size = cfh->data.size * 2;
		
			p = realloc(cfh->data.buff, cfh->data.size + CFILE_DEFAULT_MEM_ALIAS_W_REALLOC);
			if(p == NULL)
				return MEM_ERROR;
			cfh->data.size = realloc_size;
			cfh->data.buff = p;
			return 0;
		}
		switch(cfh->compressor_type) {
		case NO_COMPRESSOR:
			// position the sucker, either at write_end, or at write_start (for CFILE_WR)
			if(cfh->access_flags & CFILE_READABLE) {
				if(lseek(cfh->raw_fh, cfh->data.offset + cfh->data_fh_offset + cfh->data.write_start, SEEK_SET) !=
					cfh->data.offset + cfh->data_fh_offset + cfh->data.write_start) {
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
			break;
/*
		case BZIP2_COMPRESSOR:
			// fairly raw, if working at all //
			if(cfh->raw.pos == cfh->raw.end) {
				if(cfh->raw.pos != write(cfh->raw_fh, cfh->raw.buff, 
					cfh->raw.size))
					return IO_ERROR;
				cfh->raw.offset += cfh->raw.end;
				cfh->raw.pos = 0;
			}
			cfh->bz_stream->next_in = cfh->data.buff;
			cfh->bz_stream->avail_in = cfh->data.end;
			if(cfh->bz_stream->avail_out==0) {
				cfh->bz_stream->next_out = cfh->raw.buff;
				cfh->bz_stream->avail_out = cfh->raw.size;
			}
			if(BZ_RUN_OK != BZ2_bzCompress(cfh->bz_stream, BZ_RUN)) {
				return IO_ERROR;
			}
			break;
		case GZIP_COMPRESSOR:
			if(cfh->data.pos != gzwrite(cfh->gz_handle, cfh->data.buff, 
				cfh->data.pos)) 
				return IO_ERROR;
			cfh->data.offset += cfh->data.pos;
			cfh->data.pos=0;
			break;
*/
		}
	}
	return 0;
}

int
crefill_bz2(cfile *cfh)
{
	size_t x;
	int err;
	bz_stream *bzs = cfh->io.data;
	assert(bzs->total_out_lo32 >= cfh->data.offset + cfh->data.end);
	if(cfh->state_flags & CFILE_EOF) {
		dcprintf("crefill: %u: bz2: CFILE_EOF flagged, returning 0\n", cfh->cfh_id);
		cfh->data.offset += cfh->data.end;
		cfh->data.end = cfh->data.pos = 0;
	} else {
		cfh->data.offset += cfh->data.end;
		dcprintf("crefill: %u: bz2, refilling data\n", cfh->cfh_id);
		bzs->avail_out = cfh->data.size;
		bzs->next_out = (char *)cfh->data.buff;
		do {
			if(0 == bzs->avail_in && (cfh->raw.offset +
				(cfh->raw.end - bzs->avail_in) < cfh->raw_total_len)) {
				dcprintf("crefill: %u: bz2, refilling raw: ", cfh->cfh_id);
				if(ensure_lseek_position(cfh)) {
					return (cfh->err = IO_ERROR);
				}
				cfh->raw.offset += cfh->raw.end;
				x = read(cfh->raw_fh, cfh->raw.buff, MIN(cfh->raw.size,
					cfh->raw_total_len - cfh->raw.offset));
				dcprintf("read %lu of possible %lu\n", x, cfh->raw.size);
				bzs->avail_in = cfh->raw.end = x;
				cfh->raw.pos = 0;
				bzs->next_in = (char *)cfh->raw.buff;
			}
			err = BZ2_bzDecompress(bzs);

			/* note, this doesn't handle BZ_DATA_ERROR/BZ_DATA_ERROR_MAGIC ,
			which should be handled (rather then aborting) */
			if(err != BZ_OK && err != BZ_STREAM_END) {
				eprintf("hmm, bzip2 didn't return BZ_OK, borking cause of %i.\n", err);
				return IO_ERROR;
			}
			if(err==BZ_STREAM_END) {
				dcprintf("encountered stream_end\n");
				/* this doesn't handle u64 yet, so make it do so at some point*/
				cfh->data_total_len = MAX(bzs->total_out_lo32,
					cfh->data_total_len);
				cfh->state_flags |= CFILE_EOF;
			}
		} while((!(cfh->state_flags & CFILE_EOF)) && bzs->avail_out > 0);
		cfh->data.end = cfh->data.size - bzs->avail_out;
		cfh->data.pos = 0;
	}
	return 0;
}

int
crefill_gz(cfile *cfh)
{
	size_t x;
	int err;
	z_stream *zs = cfh->io.data;
	assert(zs->total_out >= cfh->data.offset + cfh->data.end);
	if(cfh->state_flags & CFILE_EOF) {
		dcprintf("crefill: %u: gz: CFILE_EOF flagged, returning 0\n", cfh->cfh_id);
		cfh->data.offset += cfh->data.end;
		cfh->data.end = cfh->data.pos = 0;
	} else {
		cfh->data.offset += cfh->data.end;
		dcprintf("crefill: %u: zs, refilling data\n", cfh->cfh_id);
		zs->avail_out = cfh->data.size;
		zs->next_out = cfh->data.buff;
		do {
			if(0 == zs->avail_in && (cfh->raw.offset +
				(cfh->raw.end - zs->avail_in) < cfh->raw_total_len)) {
				dcprintf("crefill: %u: zs, refilling raw: ", cfh->cfh_id);
				if(ensure_lseek_position(cfh)) {
					v1printf("encountered IO_ERROR in gz crefill: %u\n", __LINE__);
					return IO_ERROR;
				}
				cfh->raw.offset += cfh->raw.end;
				x = read(cfh->raw_fh, cfh->raw.buff, MIN(cfh->raw.size,
					cfh->raw_total_len - cfh->raw.offset));
				dcprintf("read %lu of possible %lu\n", x, cfh->raw.size);
				zs->avail_in = cfh->raw.end = x;
				cfh->raw.pos = 0;
				zs->next_in = cfh->raw.buff;
			}
			err = inflate(zs, Z_NO_FLUSH);

			if(err != Z_OK && err != Z_STREAM_END) {
				v1printf("encountered err(%i) in gz crefill:%u\n", err, __LINE__);
				return IO_ERROR;
			}
			if(err==Z_STREAM_END) {
				dcprintf("encountered stream_end\n");
				/* this doesn't handle u64 yet, so make it do so at some point*/
				cfh->data_total_len = MAX(zs->total_out,
					cfh->data_total_len);
				cfh->state_flags |= CFILE_EOF;
			}
		} while((!(cfh->state_flags & CFILE_EOF)) && zs->avail_out > 0);
		cfh->data.end = cfh->data.size - zs->avail_out;
		cfh->data.pos = 0;
	}
	return 0;
}

int
crefill_xz(cfile *cfh)
{
	size_t x;
	lzma_ret xz_err;
	lzma_stream *xzs = cfh->io.data;

	assert(xzs->total_out >= cfh->data.offset + cfh->data.end);
	if(cfh->state_flags & CFILE_EOF) {
		dcprintf("crefill: %u: xz: CFILE_EOF flagged, returning 0\n", cfh->cfh_id);
		cfh->data.offset += cfh->data.end;
		cfh->data.end = cfh->data.pos = 0;
	} else {
		cfh->data.offset += cfh->data.end;
		dcprintf("crefill: %u: xzs, refilling data\n", cfh->cfh_id);
		xzs->avail_out = cfh->data.size;
		xzs->next_out = cfh->data.buff;
		do {
			if(0 == xzs->avail_in && (cfh->raw.offset +
				(cfh->raw.end - xzs->avail_in) < cfh->raw_total_len)) {
				dcprintf("crefill: %u: xzs, refilling raw: ", cfh->cfh_id);
				if(ensure_lseek_position(cfh)) {
					v1printf("encountered IO_ERROR in xz crefill: %u\n", __LINE__);
					return IO_ERROR;
				}
				cfh->raw.offset += cfh->raw.end;
				x = read(cfh->raw_fh, cfh->raw.buff, MIN(cfh->raw.size,
					cfh->raw_total_len - cfh->raw.offset));
				dcprintf("read %lu of possible %lu\n", x, cfh->raw.size);
				xzs->avail_in = cfh->raw.end = x;
				cfh->raw.pos = 0;
				xzs->next_in = cfh->raw.buff;
			}
			xz_err = lzma_code(xzs, LZMA_RUN);
			if(xz_err != LZMA_OK && xz_err != LZMA_STREAM_END) {
				v1printf("encountered err(%i) in xz crefill:%u\n", xz_err, __LINE__);
				return IO_ERROR;
			}
			if((xz_err == LZMA_STREAM_END)) {
				dcprintf("encountered stream_end\n");
				cfh->data_total_len = MAX(xzs->total_out,
					cfh->data_total_len);
				cfh->state_flags |= CFILE_EOF;
			}
		} while((!(cfh->state_flags & CFILE_EOF)) && xzs->avail_out > 0);
		cfh->data.end = cfh->data.size - xzs->avail_out;
		cfh->data.pos = 0;
	}
	return 0;
}

int
crefill_no_comp(cfile *cfh)
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
	if(cfh->data_total_len != 0) {
		x = read(cfh->raw_fh, cfh->data.buff, MIN(cfh->data.size,
			cfh->data_total_len - cfh->data.offset));
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
crefill(cfile *cfh)
{
	assert((cfh->state_flags & CFILE_MEM_ALIAS) == 0);
	int result = 0;
	switch(cfh->compressor_type) {
	case NO_COMPRESSOR:
		result = crefill_no_comp(cfh);
		break;

	case BZIP2_COMPRESSOR:
		result = crefill_bz2(cfh);
		break;

	case GZIP_COMPRESSOR:
		result = crefill_gz(cfh);
		break;

	case XZ_COMPRESSOR:
		result = crefill_xz(cfh);
		break;
	}
	return result == 0 ? cfh->data.end : result;
}

size_t
cfile_len(cfile *cfh)
{
	return cfh->data_total_len;
}

size_t
cfile_start_offset(cfile *cfh)
{
	return cfh->data_fh_offset;
}


/* while I realize this may not *necessarily* belong in cfile, 
   eh, it's going here.

   deal with it.  :-) */

ssize_t 
copy_cfile_block(cfile *out_cfh, cfile *in_cfh, size_t in_offset, size_t len) 
{
	unsigned char buff[CFILE_DEFAULT_BUFFER_SIZE];
	unsigned int lb;
	size_t bytes_wrote=0;;
	if(in_offset!=cseek(in_cfh, in_offset, CSEEK_FSTART)) {
		return EOF_ERROR;
	}
	while(len) {
		lb = MIN(CFILE_DEFAULT_BUFFER_SIZE, len);
		if( (lb!=cread(in_cfh, buff, lb)) ||
			(lb!=cwrite(out_cfh, buff, lb)) ) {
			v2printf("twas copy_cfile_block2, in_offset is %zi, lb was %i, remaining len was %zi, bytes_wrote %zi, pos %zi, end %zi!\n", in_offset, lb, len, bytes_wrote, in_cfh->data.pos, in_cfh->data.end);
			return EOF_ERROR;
		}
		len -= lb;
		bytes_wrote+=lb;
	}
	return bytes_wrote;
}

inline void
flag_lseek_needed(cfile *cfh)
{
	if(CFH_IS_CHILD(cfh)) {
		// if we last lseeked, reset it.
		if(*(cfh->lseek_info.last_ptr) == cfh->cfh_id)
			*(cfh->lseek_info.last_ptr) = 0;
	} else {
		// same deal here.
		if(cfh->lseek_info.parent.last == cfh->cfh_id)
			cfh->lseek_info.parent.last = 0;
	}
		
}

inline void
set_last_lseeker(cfile *cfh)
{
	if(CFH_IS_CHILD(cfh)) {
		*(cfh->lseek_info.last_ptr) = cfh->cfh_id;
	} else {
		cfh->lseek_info.parent.last = cfh->cfh_id;
	}
}

inline signed int
ensure_lseek_position(cfile *cfh)
{
	if(LAST_LSEEKER(cfh) != cfh->cfh_id)
		return raw_ensure_position(cfh);
	return 0;
}

ssize_t
copy_add_block(cfile *out_cfh, cfile *src_cfh, size_t src_offset, size_t len, void *extra)
{
	unsigned char buff[CFILE_DEFAULT_BUFFER_SIZE];
	unsigned int lb;
	size_t bytes_wrote=0;;
	if(src_offset!=cseek(src_cfh, src_offset, CSEEK_FSTART)) {
		v2printf("twas copy_add_block!\n");
		return EOF_ERROR;
	}
	while(len) {
		lb = MIN(CFILE_DEFAULT_BUFFER_SIZE, len);
		if( (lb!=cread(src_cfh, buff, lb)) ||
			(lb!=cwrite(out_cfh, buff, lb)) ) {
			v2printf("twas copy_add_block2\n");
			return EOF_ERROR;
		}
		len -= lb;
		bytes_wrote+=lb;
	}
	return bytes_wrote;
}


cfile_window *
expose_page(cfile *cfh)
{
	if(cfh->access_flags & CFILE_RONLY) {
		if(cfh->data.end==0) {
			assert((cfh->state_flags & CFILE_MEM_ALIAS) == 0);
			crefill(cfh);
		}
	}
	return &cfh->data;
}

cfile_window *
next_page(cfile *cfh)
{
	if(cfh->access_flags & CFILE_WRITEABLE) {
			cflush(cfh);
	}
	if(cfh->access_flags & CFILE_RONLY) {
		if((cfh->state_flags & CFILE_MEM_ALIAS) == 0)
			crefill(cfh);
		else
			return NULL;
			
	}
	return &cfh->data;
}

cfile_window *
prev_page(cfile *cfh)
{
	/* possibly do an error check or something here */
	if(cfh->access_flags & CFILE_WRITEABLE) {
		cflush(cfh);
	}
	if(cfh->data.offset ==0) {
		cfh->data.end=0;
		cfh->data.pos=0;
	} else {
		if(cfh->data.size > cfh->data.offset) {
			cseek(cfh, 0, CSEEK_FSTART);
		} else {
			cseek(cfh, -cfh->data.size, CSEEK_CUR);
		} 
		crefill(cfh);
	}
	return &cfh->data;
}
