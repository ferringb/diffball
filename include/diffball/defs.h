/*
  Copyright (C) 2003-2005 Brian Harring

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, US 
*/
#ifndef _HEADER_DEFS
#define _HEADER_DEFS 1

#include "config.h"
#include <stdio.h>
#include <errno.h>

#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define MIN(x,y) ((x) < (y) ? (x) : (y))

extern unsigned int global_verbosity;

typedef unsigned int off_u32;
typedef signed   int off_s32;
#ifdef LARGEFILE_SUPPORT
typedef unsigned long long off_u64;
typedef signed   long long off_s64;
#else
typedef off_u32 off_u64;
typedef off_s32 off_s64;
#endif
typedef unsigned long long act_off_u64;
typedef signed   long long act_off_s64;

#define MAX_SEED_LEN		65535
#define MAX_SAMPLE_RATE		32767
#define MAX_HASH_SIZE		2147483647		//if you have 2gb for a hash size... yeah, feel free to donate hardware/memory to me :)

#define MEM_ERROR					(-3)
#define FORMAT_ERROR            	(-4)
#define DATA_ERROR              	(-5)
#define PATCH_TRUNCATED				(-17)
#define PATCH_CORRUPT_ERROR		 	(-18)
#define UNKNOWN_FORMAT				(-19)

#define v0printf(expr...) fprintf(stderr, expr)

#ifdef DEV_VERSION
#include <assert.h>
#define eprintf(expr...)   abort(); fprintf(stderr, expr)
#define errno_printf(context) err(2, "%s", context);
#define v1printf(expr...)  fprintf(stderr,expr)
#define v2printf(expr...)  if(global_verbosity>0){fprintf(stderr,expr);}
#define v3printf(expr...)  if(global_verbosity>1){fprintf(stderr,expr);}
#define v4printf(expr...)  if(global_verbosity>2){fprintf(stderr,expr);}
#else
#define assert(expr) ((void)0)
#define eprintf(expr...)   fprintf(stderr, expr)
#define errno_printf(context) err(2, "%s", context);
#define v1printf(expr...)  if(global_verbosity>0){fprintf(stderr,expr);}
#define v2printf(expr...)  if(global_verbosity>1){fprintf(stderr,expr);}
#define v3printf(expr...)  if(global_verbosity>2){fprintf(stderr,expr);}
#define v4printf(expr...)  if(global_verbosity>3){fprintf(stderr,expr);}
#endif

#define SLEEP_DEBUG 1

#ifdef DEBUG_CFILE
#include <stdio.h>
#define dcprintf(fmt...) \
	fprintf(stderr, "%s: ",__FILE__);   \
	fprintf(stderr, fmt)
#else
#define dcprintf(expr...) ((void) 0)
#endif

#endif

