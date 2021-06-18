// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2014 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_BIT_FUNCTIONS
#define _HEADER_BIT_FUNCTIONS 1

#define BYTE_BIT_COUNT 8
#define BYTE_BYTE_COUNT 1
#define SHORT_BIT_COUNT 16
#define SHORT_BYTE_COUNT 2
#define INT_BIT_COUNT 24
#define INT_BYTE_COUNT 4
#define LONG_BIT_COUNT 64
#define LONG_BYTE_COUNT 8

#include <cfile.h>

inline unsigned int unsignedBitsNeeded(unsigned long int y);
inline unsigned int signedBitsNeeded(signed long int y);
inline unsigned int unsignedBytesNeeded(unsigned long int y);
inline unsigned int signedBytesNeeded(signed long int y);

unsigned long readUBytesBE(const unsigned char *buff, unsigned int l);
unsigned long readUBytesLE(const unsigned char *buff, unsigned int l);
signed long long creadUBytesLE(cfile *cfh, unsigned int l);
signed long readSBytesBE(const unsigned char *buff, unsigned int l);
//signed long readSBytesLE(const unsigned char *buff, unsigned int l);

unsigned int writeUBytesBE(unsigned char *buff, unsigned long value, 
	unsigned int l);
unsigned int writeUBytesLE(unsigned char *buff, unsigned long value,
	unsigned int l);
int cwriteUBytesLE(cfile *cfh, unsigned long value, unsigned int len);
unsigned int writeSBytesBE(unsigned char *buff, signed long value,
	unsigned int l);
unsigned int writeSBytesLE(unsigned char *buff, signed long value,
	unsigned int l);

unsigned int writeSBitsBE(unsigned char *out_buff, signed long value,
	unsigned int bit_count);
unsigned int writeUBitsBE(unsigned char *out_buff, unsigned long value, 
	unsigned int bit_count);

int
cwriteHighBitVariableIntBE(cfile *cfh, unsigned long long value);
signed long long
creadHighBitVariableIntBE(cfile *cfh);
int
cwriteHighBitVariableIntLE(cfile *cfh, unsigned long long value);
signed long long
creadHighBitVariableIntLE(cfile *cfh);

#endif

