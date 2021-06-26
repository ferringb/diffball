// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_UDIFF
#define _HEADER_UDIFF 1
#include <diffball/line-util.h>
#include <cfile.h>
#include <diffball/tar.h>
#include <diffball/dcbuffer.h>

signed int UdiffReconstructDCBuff(cfile *ref_cfh, cfile *patchf, tar_entry **tarball,
								  CommandBuffer *dcbuff);
#endif
