// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_DCBUFFER_CFH_FUNCS
#define _HEADER_DCBUFFER_CFH_FUNCS 1

#include <cfile.h>
#include <diffball/dcbuffer.h>

unsigned long default_dcb_src_cfh_read_func(u_dcb_src usrc, unsigned long src_pos,
											unsigned char *buf, unsigned long len);

unsigned long default_dcb_src_cfh_copy_func(DCommand *dc, cfile *out_cfh);

#endif
