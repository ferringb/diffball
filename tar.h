// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2011 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_TAR
#define _HEADER_TAR 1

#include <diffball/defs.h>
#include <cfile.h>

/* the data structs were largely taken/modified/copied from gnutar's header file.
   I had to go there for the stupid gnu extension... */
#define TAR_NAME_LOC 0
#define TAR_MODE_LOC 100
#define TAR_UID_LOC 108
#define TAR_GID_LOC 116
#define TAR_SIZE_LOC 124
#define TAR_MTIME_LOC 136
#define TAR_CHKSUM_LOC 148
#define TAR_TYPEFLAG_LOC 156
#define TAR_LINKNAME_LOC 157
#define TAR_MAGIC_LOC 257
#define TAR_VERSION_LOC 263
#define TAR_UNAME_LOC 265
#define TAR_GNAME_LOC 297
#define TAR_DEVMAJOR_LOC 329
#define TAR_DEVMINOR_LOC 337
#define TAR_PREFIX_LOC 345

#define TAR_NAME_LEN 100
#define TAR_MODE_LEN 8
#define TAR_UID_LEN 8
#define TAR_GID_LEN 8
#define TAR_SIZE_LEN 12
#define TAR_MTIME_LEN 12
#define TAR_CHKSUM_LEN 8
#define TAR_TYPEFLAG_LEN 1
#define TAR_LINKNAME_LEN 100
#define TAR_MAGIC_LEN 6
#define TAR_VERSION_LEN 2
#define TAR_UNAME_LEN 32
#define TAR_GNAME_LEN 32
#define TAR_DEVMAJOR_LEN 8
#define TAR_DEVMINOR_LEN 8
#define TAR_PREFIX_LEN 155

#define TAR_EMPTY_ENTRY 0x1

typedef struct
{
	//	unsigned long		file_loc;
	off_u64 start;
	off_u64 end;
	unsigned int entry_num;
	unsigned char *fullname;
} tar_entry;

int check_str_chksum(const unsigned char *entry);
signed int read_fh_to_tar_entry(cfile *src_fh, tar_entry **tar_entries, unsigned long *total_count);
int read_entry(cfile *src_cfh, off_u64 start, tar_entry *te);
int readh_cfh_to_tar_entries(cfile *src_cfh, tar_entry ***file,
							 unsigned long *total_count);

/* possibly this could be done different, what of endptr of strtol?
   Frankly I worry about strtol trying to go too far and causing a segfault, due to tar fields not always having trailing \0 */
inline unsigned long octal_str2long(const unsigned char *string, unsigned int length)
{
	char *ptr = (char *)malloc(length + 1);
	unsigned long x;
	strncpy((char *)ptr, (const char *)string, length);
	ptr[length] = '\0'; /* overkill? */
	x = strtol((char *)ptr, NULL, 8);
	free(ptr);
	return (x);
}
#endif
