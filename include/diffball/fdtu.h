// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_FDTU
#define _HEADER_FDTU 1
#include <cfile.h>

#define FDTU_MAGIC_LEN 3
#define FDTU_MAGIC "DTU"
#define FDTU_VERSION_LEN 2
#define FDTU_MAGIC_V4 0x04
#define FDTU_MAGIC_V3 0x03

unsigned int check_fdtu_magic(cfile *patchf);
signed int fdtuEncodeDCBuffer(CommandBuffer *buffer, cfile *out_cfh);
signed int fdtuReconstructDCBuff(DCB_SRC_ID src_id, cfile *patchf, CommandBuffer *dcbuff);

#endif
