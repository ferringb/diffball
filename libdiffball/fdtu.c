// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2014 Brian Harring <ferringb@gmail.com>

#include <stdlib.h>
#include <diffball/defs.h>
#include "string-misc.h"
#include <diffball/dcbuffer.h>
#include <cfile.h>
#include <diffball/bit-functions.h>
#include <diffball/xdelta1.h>
#include <diffball/fdtu.h>

unsigned int
check_fdtu_magic(cfile *patchf)
{
	unsigned char buff[FDTU_MAGIC_LEN];
	unsigned int ver;
	cseek(patchf, 0, CSEEK_FSTART);
	if (FDTU_MAGIC_LEN != cread(patchf, buff, FDTU_MAGIC_LEN))
		return 0;
	if (memcmp(buff, FDTU_MAGIC, FDTU_MAGIC_LEN) != 0)
		return 0;
	if (cread(patchf, buff, FDTU_VERSION_LEN) != FDTU_VERSION_LEN)
		return 0;
	ver = readUBytesLE(buff, FDTU_VERSION_LEN);
	if ((ver == FDTU_MAGIC_V4) || (ver == FDTU_MAGIC_V3))
	{
		return 2;
	}
	else
	{
		return 1;
	}
	return 0;
}

signed int
fdtuEncodeDCBuff(CommandBuffer *dcb, cfile *out_cfh)
{
	return -1L;
}

signed int
fdtuReconstructDCBuff(DCB_SRC_ID src_id, cfile *patchf, CommandBuffer *dcb)
{
	unsigned long int flags = 0;
	unsigned long p_len;
	unsigned long start = 0;
	unsigned char buff[4];
	unsigned int ver;
	cfile *cfh;
	if ((cfh = (cfile *)calloc(1, sizeof(cfile))) == NULL)
		return -2;
	if (cseek(patchf, 0, CSEEK_FSTART) != 0)
	{
		return IO_ERROR;
	}
	if (cread(patchf, buff, 3) != 3)
	{
		return EOF_ERROR;
	}
	else if (memcmp(buff, FDTU_MAGIC, FDTU_MAGIC_LEN) != 0)
	{
		return -1;
	}
	else if (cread(patchf, buff, 2) != 2)
	{
		return EOF_ERROR;
	}
	ver = readUBytesLE(buff, 2);

	start = FDTU_MAGIC_LEN + FDTU_VERSION_LEN;
	for (p_len = 0; p_len < 2; p_len++)
	{
		if (cread(patchf, buff, 2) != 2)
			return -1;
		start += readUBytesLE(buff, 2) + 2;

		/* only v4 has md5 */
		if (ver == 4)
		{
			start += 16;
		}

		/* skip the _redundant_ fdtu src filename, and md5.
	   I say redundant, because xdelta already encodes this info. */

		cseek(patchf, start, CSEEK_FSTART);
	}
	cseek(patchf, 8, CSEEK_CUR);
	if (cread(patchf, buff, 4) != 4)
		return -1;
	/* check for flags & 0x1, eg _redundant_ pristine information stored at 
		the end. */
	flags = readUBytesLE(buff, 4);
	if (cread(patchf, buff, 4) != 4)
		return -1;
	start += 16;
	p_len = readUBytesLE(buff, 4);
	//	start = ctell(patchf, CSEEK_FSTART);
	if (copen_child_cfh(cfh, patchf, start, p_len + start,
						NO_COMPRESSOR, CFILE_RONLY))
		return MEM_ERROR;
	v1printf("calling xdeltaReconstruct\n");
	return xdelta1ReconstructDCBuff(src_id, cfh, dcb, 1);
}
