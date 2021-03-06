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

/*
static unsigned int PRIMES[] = {1583,113,919,719,1373,659,503,373,653,1759,1289,911,1163,1109,1123,467,619,1051,109,1583,971,563,283,1619,241,1693,1543,643,181,1471,929,271,433,809,1433,257,179,1409,271,109,827,1399,1699,719,1301,1747,577,607,631,173,229,563,787,1129,463,577,1801,1559,1283,617,733,379,127,491,131,293,461,1459,239,1543,1451,821,613,1237,1493,593,1697,677,373,1427,383,1619,449,997,1571,1049,107,1031,991,1307,1181,641,107,701,863,1361,101,811,1823,1709,787,701,359,883,281,659,1523,179,719,127,1571,331,163,199,293,613,1789,317,647,421,827,1483,1289,421,241,1193,859,743,1453,199,1637,1301,1559,269,1123,433,839,1163,1021,1439,1427,157,1657,1721,1039,1453,1217,1301,1723,593,463,521,1013,1063,1069,1747,499,1753,1523,1373,1493,1213,449,1151,307,607,167,193,1193,1123,1511,449,1597,733,1553,1231,593,277,1723,113,1373,643,1721,229,613,1187,1697,281,1423,1427,1429,701,1103,353,1559,1097,241,439,1031,677,1117,619,863,499,811,1553,463,1163,1129,1093,463,857,919,1801,1493,1301,563,967,947,211,367,227,787,1637,787,643,1553,223,997,281,1021,193,1459,859,1021,127,1451,1637,347,461,1481,197,677,1051,1033,373,839,659,941,1493,101,1093,233,977,163,181};
static unsigned long PRIMES[] = {
  0xbcd1, 0xbb65, 0x42c2, 0xdffe, 0x9666, 0x431b, 0x8504, 0xeb46,
  0x6379, 0xd460, 0xcf14, 0x53cf, 0xdb51, 0xdb08, 0x12c8, 0xf602,
  0xe766, 0x2394, 0x250d, 0xdcbb, 0xa678, 0x02af, 0xa5c6, 0x7ea6,
  0xb645, 0xcb4d, 0xc44b, 0xe5dc, 0x9fe6, 0x5b5c, 0x35f5, 0x701a,
  0x220f, 0x6c38, 0x1a56, 0x4ca3, 0xffc6, 0xb152, 0x8d61, 0x7a58,
  0x9025, 0x8b3d, 0xbf0f, 0x95a3, 0xe5f4, 0xc127, 0x3bed, 0x320b,
  0xb7f3, 0x6054, 0x333c, 0xd383, 0x8154, 0x5242, 0x4e0d, 0x0a94,
  0x7028, 0x8689, 0x3a22, 0x0980, 0x1847, 0xb0f1, 0x9b5c, 0x4176,
  0xb858, 0xd542, 0x1f6c, 0x2497, 0x6a5a, 0x9fa9, 0x8c5a, 0x7743,
  0xa8a9, 0x9a02, 0x4918, 0x438c, 0xc388, 0x9e2b, 0x4cad, 0x01b6,
  0xab19, 0xf777, 0x365f, 0x1eb2, 0x091e, 0x7bf8, 0x7a8e, 0x5227,
  0xeab1, 0x2074, 0x4523, 0xe781, 0x01a3, 0x163d, 0x3b2e, 0x287d,
  0x5e7f, 0xa063, 0xb134, 0x8fae, 0x5e8e, 0xb7b7, 0x4548, 0x1f5a,
  0xfa56, 0x7a24, 0x900f, 0x42dc, 0xcc69, 0x02a0, 0x0b22, 0xdb31,
  0x71fe, 0x0c7d, 0x1732, 0x1159, 0xcb09, 0xe1d2, 0x1351, 0x52e9,
  0xf536, 0x5a4f, 0xc316, 0x6bf9, 0x8994, 0xb774, 0x5f3e, 0xf6d6,
  0x3a61, 0xf82c, 0xcc22, 0x9d06, 0x299c, 0x09e5, 0x1eec, 0x514f,
  0x8d53, 0xa650, 0x5c6e, 0xc577, 0x7958, 0x71ac, 0x8916, 0x9b4f,
  0x2c09, 0x5211, 0xf6d8, 0xcaaa, 0xf7ef, 0x287f, 0x7a94, 0xab49,
  0xfa2c, 0x7222, 0xe457, 0xd71a, 0x00c3, 0x1a76, 0xe98c, 0xc037,
  0x8208, 0x5c2d, 0xdfda, 0xe5f5, 0x0b45, 0x15ce, 0x8a7e, 0xfcad,
  0xaa2d, 0x4b5c, 0xd42e, 0xb251, 0x907e, 0x9a47, 0xc9a6, 0xd93f,
  0x085e, 0x35ce, 0xa153, 0x7e7b, 0x9f0b, 0x25aa, 0x5d9f, 0xc04d,
  0x8a0e, 0x2875, 0x4a1c, 0x295f, 0x1393, 0xf760, 0x9178, 0x0f5b,
  0xfa7d, 0x83b4, 0x2082, 0x721d, 0x6462, 0x0368, 0x67e2, 0x8624,
  0x194d, 0x22f6, 0x78fb, 0x6791, 0xb238, 0xb332, 0x7276, 0xf272,
  0x47ec, 0x4504, 0xa961, 0x9fc8, 0x3fdc, 0xb413, 0x007a, 0x0806,
  0x7458, 0x95c6, 0xccaa, 0x18d6, 0xe2ae, 0x1b06, 0xf3f6, 0x5050,
  0xc8e8, 0xf4ac, 0xc04c, 0xf41c, 0x992f, 0xae44, 0x5f1b, 0x1113,
  0x1738, 0xd9a8, 0x19ea, 0x2d33, 0x9698, 0x2fe9, 0x323f, 0xcde2,
  0x6d71, 0xe37d, 0xb697, 0x2c4f, 0x4373, 0x9102, 0x075d, 0x8e25,
  0x1672, 0xec28, 0x6acb, 0x86cc, 0x186e, 0x9414, 0xd674, 0xd1a5
};
*/

int 
init_adler32_seed(ADLER32_SEED_CTX *ads, unsigned int seed_len)
{
		unsigned int x;
		ads->s1 = ads->s2 = ads->tail = 0;
		ads->seed_len = seed_len;
		ads->multi = 181;//multi;
		if((ads->seed_chars = (unsigned char *)calloc(seed_len, sizeof(unsigned char)))==NULL) {
				return MEM_ERROR;
		}

		ads->last_multi = 1;
		for(x=1; x < seed_len; x++) {
			ads->last_multi *= ads->multi;
			ads->last_multi++;
		}
	return 0;
}

inline void
update_adler32_seed(ADLER32_SEED_CTX *ads, unsigned char *buff, 
	unsigned int len) 
{		
	int x;
	if(len==ads->seed_len) {
		//printf("computing seed fully\n");
		ads->s1 = ads->s2 = ads->tail =0;
		for(x=0; x < ads->seed_len; x++) {
			ads->s1 += buff[x];
			ads->s2 *= ads->multi;
			ads->s2 += ads->s1;
			ads->seed_chars[x] = buff[x];
		}
		ads->tail = 0;
	} else {
		for(x=0; x < len; x++){
			ads->s1 = ads->s1 - ads->seed_chars[ads->tail] + buff[x];

			ads->s2 -= (ads->last_multi * ads->seed_chars[ads->tail]);
			ads->s2 *= ads->multi;
			ads->s2 += ads->s1;

			ads->seed_chars[ads->tail] = buff[x];
			ads->tail = (ads->tail + 1) % ads->seed_len;
		}
	}
}

/*
unsigned long
get_checksum(ADLER32_SEED_CTX *ads)
{
	return ads->s2;
}
*/

unsigned int 
free_adler32_seed(ADLER32_SEED_CTX *ads)
{
	//printf("free_adler32_seed\n");
	free(ads->seed_chars);
	ads->s1 = ads->s2 = ads->tail = 0;
	return 0;
}


