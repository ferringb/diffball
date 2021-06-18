// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2015 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_ADLER32
#define _HEADER_ADLER32 1

typedef struct _ADLER32_SEED_CTX {
		unsigned int seed_len;
		unsigned int multi;
		unsigned long last_multi;
		unsigned char *seed_chars;
		unsigned int tail;
		unsigned long s1;
		unsigned long s2;
} ADLER32_SEED_CTX;

int init_adler32_seed(ADLER32_SEED_CTX *ads, unsigned int seed_len);
inline void update_adler32_seed(ADLER32_SEED_CTX *ads, unsigned char *buff,
	unsigned int len);
#define get_checksum(ptr)   \
    (((ADLER32_SEED_CTX *)ptr)->s2)
//unsigned long get_checksum(ADLER32_SEED_CTX *ads);
unsigned int free_adler32_seed(ADLER32_SEED_CTX *ads) ;
#endif

