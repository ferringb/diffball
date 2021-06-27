// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2013 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_DEFS
#define _HEADER_DEFS 1

#include "config.h"
#include <stdio.h>
#include <errno.h>

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

typedef unsigned int off_u32;
typedef signed int off_s32;
#ifdef LARGEFILE_SUPPORT
typedef unsigned long long off_u64;
typedef signed long long off_s64;
#else
typedef off_u32 off_u64;
typedef off_s32 off_s64;
#endif
typedef unsigned long long act_off_u64;
typedef signed long long act_off_s64;

#define MAX_SEED_LEN 65535
#define MAX_SAMPLE_RATE 32767
#define MAX_HASH_SIZE 2147483647 //if you have 2gb for a hash size... yeah, feel free to donate hardware/memory to me :)

#define MEM_ERROR (-3)
#define FORMAT_ERROR (-4)
#define DATA_ERROR (-5)
#define PATCH_TRUNCATED (-17)
#define PATCH_CORRUPT_ERROR (-18)
#define UNKNOWN_FORMAT (-19)

extern unsigned int _diffball_logging_level;
void diffball_set_logging_level(unsigned int level);
unsigned int diffball_get_logging_level();
#define diffball_increase_logging_level() \
	diffball_set_logging_level(MAX(100, diffball_get_logging_level() + 1))

#ifdef DEV_VERSION
#include <assert.h>
#define eprintf(expr...)   \
	fprintf(stderr, expr); \
	fflush(stderr);        \
	abort();
#else
#define assert(expr) ((void)0)
#define eprintf(expr...) fprintf(stderr, expr)
#endif

#define errno_printf(context) err(2, "%s", context);
#define dcb_lprintf(level, expr...)           \
	{                                         \
		if (level <= _diffball_logging_level) \
		{                                     \
			fprintf(stderr, expr);            \
		}                                     \
	}

#define SLEEP_DEBUG 1

#endif
