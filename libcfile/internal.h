// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2013 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_CFILE_DEFS
#define _HEADER_CFILE_DEFS 1

#include "config.h"
#include <stdio.h>
#include <errno.h>

#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define MIN(x,y) ((x) < (y) ? (x) : (y))

#define v0printf(expr...) fprintf(stderr, expr);

#ifdef DEV_VERSION
#include <assert.h>
#else
#define assert(expr) ((void)0)
#endif
#define eprintf(expr...)   fprintf(stderr, expr);

#define v1printf(expr...)  if(cfile_verbosity>0){fprintf(stderr,expr);}
#define v2printf(expr...)  if(cfile_verbosity>1){fprintf(stderr,expr);}
#define v3printf(expr...)  if(cfile_verbosity>2){fprintf(stderr,expr);}
#define v4printf(expr...)  if(cfile_verbosity>3){fprintf(stderr,expr);}

#ifdef DEBUG_CFILE
#include <stdio.h>
#define dcprintf(fmt...) \
	fprintf(stderr, "%s: ",__FILE__);   \
	fprintf(stderr, fmt);
#else
#define dcprintf(expr...) ((void) 0);
#endif

#include "cfile.h"

int internal_copen_no_comp(cfile *cfh);
int internal_copen_gzip(cfile *cfh);
int internal_copen_bzip2(cfile *cfh);
int internal_copen_xz(cfile *cfh);

inline signed int ensure_lseek_position(cfile *cfh);
inline void flag_lseek_needed(cfile *cfh);
inline void set_last_lseeker(cfile *cfh);

#define LAST_LSEEKER(cfh) (CFH_IS_CHILD(cfh) ?                              \
	((cfh)->lseek_info.parent_ptr->lseek_info.parent.last) : (cfh)->lseek_info.parent.last)
    
#define IS_LAST_LSEEKER(cfh) ( (cfh)->cfh_id == LAST_LSEEKER((cfh)) || ((cfh)->state_flags & CFILE_MEM_ALIAS) )

signed int raw_ensure_position(cfile *cfh);
    
#endif
