// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_SWITCHING
#define _HEADER_SWITCHING 1
#include <cfile.h>

#define SWITCHING_MAGIC "%SWITCHD%"
#define SWITCHING_MAGIC_LEN 9
#define SWITCHING_VERSION 0x00
#define SWITCHING_VERSION_LEN 1

unsigned int check_switching_magic(cfile *patchf);
signed int switchingEncodeDCBuffer(CommandBuffer *buffer, 
	cfile *out_cfh);
signed int switchingReconstructDCBuff(DCB_SRC_ID src_id, cfile *patchf, CommandBuffer *dcbuff);
#endif
