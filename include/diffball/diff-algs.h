// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2015 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_DIFF_ALGS
#define _HEADER_DIFF_ALGS 1

#define DEFAULT_MULTIPASS_SEED_LEN (512)
#define DEFAULT_SEED_LEN		 (16)
#define COMPUTE_SAMPLE_RATE(hs, data, seed)				\
   ((data) > (hs) ? MAX(MAX(seed/16, 2), ((data)/(hs))-.5) : MAX(seed/16, 2))
#define MULTIPASS_GAP_KLUDGE	(1.25)

#include <cfile.h>
#include <diffball/dcbuffer.h>
#include <diffball/hash.h>

void print_RefHash_stats(RefHash *rhash);
signed int OneHalfPassCorrecting(CommandBuffer *buffer, RefHash *rhash, unsigned char src_id, 
		cfile *ver_cfh, unsigned char ver_id);
signed int MultiPassAlg(CommandBuffer *buffer, cfile *ref_cfh, unsigned char ref_id, 
	cfile *ver_cfh, unsigned char ver_id, 
	unsigned long max_hash_size, unsigned int seed_len);
#endif

