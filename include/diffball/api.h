// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2011 Brian Harring <ferringb@gmail.com>

#ifndef HEADER_API_
#define HEADER_API_ 1

int simple_difference(cfile *ref, cfile *ver, cfile *out, unsigned int patch_id, unsigned long seed_len,
					  unsigned long sample_rate, unsigned long hash_size);

int simple_reconstruct(cfile *src_cfh, cfile *patch_cfh[], unsigned char patch_count, cfile *out_cfh, unsigned int force_patch_id,
					   unsigned int max_buff_size);

#endif
