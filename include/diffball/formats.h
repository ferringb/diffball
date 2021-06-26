// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2013 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_FORMATS
#define _HEADER_FORMATS 1
#include <cfile.h>
#include <diffball/bdelta.h>
#include <diffball/bdiff.h>
#include <diffball/gdiff.h>
#include <diffball/switching.h>
#include <diffball/xdelta1.h>
#include <diffball/fdtu.h>
#include <diffball/bsdiff.h>
#include <diffball/tree.h>

#define GDIFF4_FORMAT 0x1
#define GDIFF5_FORMAT 0x2
#define BDIFF_FORMAT 0x3
#define BSDIFF3_FORMAT 0x4
#define BSDIFF4_FORMAT 0x5
#define XDELTA1_FORMAT 0x6
#define SWITCHING_FORMAT 0x7
#define BDELTA_FORMAT 0x8
#define GNUDIFF_FORMAT 0x9
#define BSDIFF_FORMAT 0xa
#define FDTU_FORMAT 0xb
#define TREE_FORMAT 0xc
#define DEFAULT_PATCH_ID SWITCHING_FORMAT

#define UNDETECTED_COMPRESSOR NO_COMPRESSOR

unsigned long int check_for_format(char *format_name, unsigned int len);
unsigned long int identify_format(cfile *patchf);
//unsigned long int detect_compressor(cfile *cfh);
#endif
