// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2015 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_ADLER32
#define _HEADER_ADLER32 1

typedef struct _ADLER32_SEED_CTX
{
        unsigned int seed_len;
        unsigned int multi;
        unsigned long last_multi;
        unsigned char *seed_chars;
        unsigned int tail;
        unsigned long s1;
        unsigned long s2;
} ADLER32_SEED_CTX;

int init_adler32_seed(ADLER32_SEED_CTX *ads, unsigned int seed_len);
#define get_checksum(ptr) \
        (((ADLER32_SEED_CTX *)ptr)->s2)
//unsigned long get_checksum(ADLER32_SEED_CTX *ads);

unsigned int free_adler32_seed(ADLER32_SEED_CTX *ads);

#ifdef DIFFBALL_ENABLE_INLINE
inline void
update_adler32_seed(ADLER32_SEED_CTX *ads, unsigned char *buff,
                    unsigned int len)
{
        int x;
        if (len == ads->seed_len)
        {
                //printf("computing seed fully\n");
                ads->s1 = ads->s2 = ads->tail = 0;
                for (x = 0; x < ads->seed_len; x++)
                {
                        ads->s1 += buff[x];
                        ads->s2 *= ads->multi;
                        ads->s2 += ads->s1;
                        ads->seed_chars[x] = buff[x];
                }
                ads->tail = 0;
        }
        else
        {
                for (x = 0; x < len; x++)
                {
                        ads->s1 = ads->s1 - ads->seed_chars[ads->tail] + buff[x];

                        ads->s2 -= (ads->last_multi * ads->seed_chars[ads->tail]);
                        ads->s2 *= ads->multi;
                        ads->s2 += ads->s1;

                        ads->seed_chars[ads->tail] = buff[x];
                        ads->tail = (ads->tail + 1) % ads->seed_len;
                }
        }
};

#else
void update_adler32_seed(ADLER32_SEED_CTX *ads, unsigned char *buff,
                         unsigned int len);
#endif

#endif
