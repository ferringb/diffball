// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2015 Brian Harring <ferringb@gmail.com>

/* as per the norm, modified version of zlib's adler32. 
   eg the code I've wroted, but the original alg/rolling chksum is 
   Mark Adler's baby....*/

/* adler32.c -- compute the Adler-32 checksum of a data stream
 * Copyright (C) 1995-2002 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */
#include <stdlib.h>
#include <string.h>
#include <diffball/adler32.h>
#include <diffball/defs.h>

int init_adler32_seed(ADLER32_SEED_CTX *ads, unsigned int seed_len)
{
  unsigned int x;
  ads->s1 = ads->s2 = ads->tail = 0;
  ads->seed_len = seed_len;
  ads->multi = 181; // multiplier was choosen via past benching/comparison tests.  Should be a prime for collision reasons.
  if ((ads->seed_chars = (unsigned char *)calloc(seed_len, sizeof(unsigned char))) == NULL)
  {
    return MEM_ERROR;
  }

  ads->last_multi = 1;
  for (x = 1; x < seed_len; x++)
  {
    ads->last_multi *= ads->multi;
    ads->last_multi++;
  }
  return 0;
}

unsigned int
free_adler32_seed(ADLER32_SEED_CTX *ads)
{
  free(ads->seed_chars);
  ads->s1 = ads->s2 = ads->tail = 0;
  return 0;
}

// C standards for inline are stupid.
extern void update_adler32_seed(ADLER32_SEED_CTX *ads, unsigned char *buff,
                                unsigned int len);
