// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_APPLY_PATCH
#define _HEADER_APPLY_PATCH 1
#include <diffball/dcbuffer.h>
#include <diffball/command_list.h>

int reconstructFile(CommandBuffer *dcbuff, cfile *out_cfh,
					int reorder_for_seq_access, unsigned long max_buff_size);
int read_seq_write_rand(command_list *cl, DCB_registered_src *u_src, unsigned char is_overlay, cfile *out_cfh,
						unsigned long buf_size);
#endif
