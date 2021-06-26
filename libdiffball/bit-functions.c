// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2014 Brian Harring <ferringb@gmail.com>
#include <stdlib.h>
#include <diffball/bit-functions.h>
#include <cfile.h>
#include <assert.h>

unsigned long
readUBytesBE(const unsigned char *buff, unsigned int l)
{
	const unsigned char *p;
	unsigned long num = 0;
	for (p = (unsigned const char *)buff; p - buff < l; p++)
		num = (num << 8) | *p;
	return (unsigned long)num;
}

unsigned long
readUBytesLE(const unsigned char *buff, unsigned int l)
{
	unsigned long num = 0;
	for (; l > 0; l--)
		num = (num << 8) | buff[l - 1];
	return (unsigned long)num;
}

signed long long
creadUBytesLE(cfile *cfh, unsigned int l)
{
	assert(l < 8);
	unsigned char buff[8];
	int result = cread(cfh, buff, l);
	if (result < 0)
	{
		return result;
	}
	else if (result != l)
	{
		return EOF_ERROR;
	}
	return readUBytesLE(buff, l);
}

/*
signed long 
readSBytesLE(const unsigned char *buff, unsigned int l)
{
	unsigned long num = 0;
	num |= (buff[l-1] & 0x7f);
	for(; l > 1; l--)
		num |= (num << 8) + buff[l -1];

	num = *buff & 0x7f;  //strip the leading bit.
	for(p = buff -l -1; p != buff; p--) 
		num = (num << 8) + *p;
	return (signed long)(num * (*buff & 0x80 ? -1 : 1));
}
*/

signed long
readSBytesBE(const unsigned char *buff, unsigned int l)
{
	unsigned long num;
	unsigned const char *p;
	num = *buff & 0x7f; //strpi the leading bit.
	for (p = buff + 1; p - buff < l; p++)
	{
		num = (num << 8) + *p;
	}
	return (signed long)(num * (*buff & 0x80 ? -1 : 1));
}

unsigned int
writeUBytesBE(unsigned char *buff, unsigned long value, unsigned int l)
{
	unsigned int x;
	for (x = 0; x < l; x++)
		buff[x] = (value >> ((l - 1 - x) * 8)) & 0xff;
	if (l > 4 && (value >> (l * 8)) > 0)
		return 1;
	return 0;
}

unsigned int
writeUBytesLE(unsigned char *buff, unsigned long value, unsigned int l)
{
	unsigned int x;
	for (x = 0; l > 0; l--, x++)
		buff[x] = ((value >> (x * 8)) & 0xff);
	if (l != 4 && (value >> (l * 8)) > 0)
		return 1;
	return 0;
}

int cwriteUBytesLE(cfile *cfh, unsigned long value, unsigned int len)
{
	unsigned char buff[16];
	writeUBytesLE(buff, value, len);
	if (len != cwrite(cfh, buff, len))
	{
		return IO_ERROR;
	}
	return 0;
}

unsigned int
writeSBytesBE(unsigned char *buff, signed long value, unsigned int l)
{
	if (writeUBytesBE(buff, (unsigned long)abs(value), l) != 0)
	{
		return 1;
	}
	else if ((buff[0] & 0x80) != 0)
	{
		return 1;
	}
	if (value < 0)
		buff[0] |= 0x80;
	return 0;
}

unsigned int
writeSBytesLE(unsigned char *buff, signed long value, unsigned int l)
{
	if (writeUBytesLE(buff, (unsigned long)abs(value), l) != 0)
		return 1;
	else if ((buff[0] & 0x80) != 0)
		return 1;
	if (value < 0)
		buff[0] |= 0x80;
	return 0;
}

unsigned int
writeSBitsBE(unsigned char *out_buff, signed long value, unsigned int bit_count)
{
	unsigned int start = 0;
	start = bit_count % 8;
	writeUBitsBE(out_buff, abs(value), bit_count);
	if (value < 0)
	{
		if (out_buff[0] & (1 << start))
			return 1; //num was too large.
		out_buff[0] |= (1 << start);
	}
	else if (out_buff[0] & (1 << start))
	{ //num was too large.
		return 1;
	}
	return 0;
}

unsigned int
writeUBitsBE(unsigned char *out_buff, unsigned long value, unsigned int bit_count)
{
	unsigned int start_bit, byte;
	signed int x;
	start_bit = bit_count % 8;
	for (x = bit_count - start_bit, byte = 0; x >= 0; byte++, x -= 8)
		out_buff[byte] = (value >> x) & 0xff;
	return 0;
}

signed long long
creadHighBitVariableIntBE(cfile *cfh)
{
	unsigned long long result = 0;
	do
	{
		if (cfh->data.end == cfh->data.pos)
		{
			int err = crefill(cfh);
			if (err <= 0)
			{
				return err ? err : result;
			}
		}
		result <<= 7;
		result |= (cfh->data.buff[cfh->data.pos] & 0x7f);
		cfh->data.pos++;
	} while (cfh->data.buff[cfh->data.pos - 1] & 0x80);
	return result;
}

signed long long
creadHighBitVariableIntLE(cfile *cfh)
{
	unsigned long long result = 0;
	int position = 0;
	do
	{
		if (cfh->data.end == cfh->data.pos)
		{
			int err = crefill(cfh);
			if (err <= 0)
			{
				return err ? err : result;
			}
		}
		unsigned char val = (cfh->data.buff[cfh->data.pos] & 0x7f);
		result |= ((unsigned long long)val) << position;
		position += 7;
		cfh->data.pos++;
	} while (cfh->data.buff[cfh->data.pos - 1] & 0x80);
	return result;
}

int cwriteHighBitVariableIntLE(cfile *cfh, unsigned long long value)
{
	unsigned char buff;
	do
	{
		buff = value & 0x7f;
		value >>= 7;
		if (value)
		{
			// Flip the high bit to indicate more bytes will follow.
			buff |= 0x80;
		}
		int err = cwrite(cfh, &buff, 1);
		if (err != 1)
		{
			return err ? err : IO_ERROR;
		}
	} while (value);
	return 0;
}

int cwriteHighBitVariableIntBE(cfile *cfh, unsigned long long value)
{
	unsigned char buff;
	unsigned int bits_needed = unsignedBitsNeeded(value);
	do
	{
		bits_needed = bits_needed - 7;
		bits_needed = bits_needed >= 0 ? bits_needed : 0;
		buff = (value >> bits_needed) & 0x7f;
		if (bits_needed)
		{
			// Flip the high bit to indicate more bytes will follow.
			buff |= 0x80;
		}
		int err = cwrite(cfh, &buff, 1);
		if (err != 1)
		{
			return err ? err : IO_ERROR;
		}
	} while (bits_needed);
	return 0;
}
