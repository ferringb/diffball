// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_BSDIFF
#define _HEADER_BSDIFF 1

#define BSDIFF4_MAGIC "BSDIFF40"
#define BSDIFF3_MAGIC "BSDIFF30"
#define BSDIFF_QS_MAGIC "QSUFDIFF"
#define BSDIFF_MAGIC_LEN 8
#include <diffball/diff-algs.h>
#include <cfile.h>

unsigned int check_bsdiff_magic(cfile *patchf);
signed int bsdiffEncodeDCBuffer(CommandBuffer *buffer, cfile *ver_cfh,
								cfile *out_cfh);
signed int bsdiffReconstructDCBuff(DCB_SRC_ID src_id, cfile *patchf, CommandBuffer *dcbuff);

#endif
