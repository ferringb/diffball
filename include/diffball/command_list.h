// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_COMMAND_LIST
#define _HEADER_COMMAND_LIST 1

#include <diffball/defs.h>

typedef struct
{
	off_u64 offset;
	off_u64 len;
} DCLoc;

typedef struct
{
	off_u64 src_pos;
	off_u64 ver_pos;
	off_u64 len;
} DCLoc_match;

typedef struct
{
	DCLoc *command;
	DCLoc_match *full_command;
	unsigned char *src_id;
	unsigned long com_count;
	unsigned long com_size;
} command_list;

int CL_init(command_list *cl, unsigned char full, unsigned long size, unsigned char store_src_ids);

void CL_free(command_list *cl);

int CL_add_command(command_list *cl, off_u64 src_pos, off_u64 len, unsigned char src_id);

int CL_add_full_command(command_list *cl, off_u64 src_pos, off_u64 len, off_u64 ver_pos, unsigned char src_id);

int CL_resize(command_list *cl, unsigned long increment);

#endif
