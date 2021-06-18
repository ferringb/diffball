// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_GDIFF
#define _HEADER_GDIFF 1
#define GDIFF_MAGIC 0xd1ffd1ff
#define GDIFF_MAGIC_LEN 4
#define GDIFF_VER4_MAGIC 4
#define GDIFF_VER5_MAGIC 5
#define GDIFF_VER_LEN 1
#include <diffball/diff-algs.h>
#include <diffball/dcbuffer.h>
#include <cfile.h>
/* gdiff format
	EOF 0
	data 1 => 1 ubyte follows
	...
	data 246 => 246 bytes
	data 247 => followed by ushort specifying byte_len
	data 248 => followed by uint specifying byte_len
	copy 249 => ushort, ubyte
	copy 250 => ushort, ushort
	copy 251 => ushort, int
	copy 252 => int, ubyte
	copy 253 => int, ushort
	copy 254 => int, int
	copy 255 => long, int
	
	note, specification seemed a bit off by the whole switching between signed and unsigned.
	soo. unsigned for all position indications based off of file start (crappy scheme).
	soo. version 4=original rfc spec.
	v5= signed based off of last dc offset position.
	v6= signed based off of current (versioned) position during reconstruction.
	magic=0xd1ffd1ff
	version is one byte.
	*/

unsigned int check_gdiff4_magic(cfile *patchf);
unsigned int check_gdiff5_magic(cfile *patchf);

signed int gdiffEncodeDCBuffer(CommandBuffer *buffer, 
	unsigned int offset_type, cfile *out_cfh);
#define gdiff4EncodeDCBuffer(buff, ocfh)						\
	gdiffEncodeDCBuffer((buff), ENCODING_OFFSET_START, (ocfh))
#define gdiff5EncodeDCBuffer(buff, ocfh)						\
	gdiffEncodeDCBuffer((buff), ENCODING_OFFSET_DC_POS, (ocfh))

//signed int gdiffReconstructDCBuff(cfile *ref_cfh, cfile *patchf, CommandBuffer *dcbuff, 
signed int gdiffReconstructDCBuff(DCB_SRC_ID src_id, cfile *patchf, CommandBuffer *dcbuff, 
	unsigned int offset_type);
#define gdiff4ReconstructDCBuff(rcfh, pcfh, buff)						 \
	gdiffReconstructDCBuff((rcfh), (pcfh), (buff), ENCODING_OFFSET_START)
#define gdiff5ReconstructDCBuff(rcfh, pcfh, buff)						\
	gdiffReconstructDCBuff((rcfh), (pcfh), (buff), ENCODING_OFFSET_DC_POS)
#endif
