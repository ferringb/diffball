// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_BDIFF
#define _HEADER_BDIFF 1
#define BDIFF_MAGIC "BDIFF"
#define BDIFF_MAGIC_LEN 5
#define BDIFF_VERSION 'a'
#define BDIFF_DEFAULT_MAXBLOCKSIZE (1<<20)
#include <diffball/diff-algs.h>
#include <cfile.h>

unsigned int check_bdiff_magic(cfile *patchf);
signed int bdiffEncodeDCBuffer(CommandBuffer *buffer, cfile *out_cfh);
signed int bdiffReconstructDCBuff(DCB_SRC_ID src_id, cfile *patchf, CommandBuffer *dcbuff);
#endif
