// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_PRIMES
#define _HEADER_PRIMES 1

typedef struct _PRIME_CTX {
		unsigned int *base_primes;
		unsigned long prime_count;
		unsigned long array_size;
} PRIME_CTX;

int init_primes(PRIME_CTX *ctx);
void free_primes(PRIME_CTX *ctx);
unsigned long get_nearest_prime(unsigned long near);
#endif
