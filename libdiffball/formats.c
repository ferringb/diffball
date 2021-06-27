// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#include <string.h>
#include <diffball/defs.h>
#include <diffball/formats.h>
#include <cfile.h>

unsigned long int
check_for_format(char *format_name, unsigned int len)
{
	if ((len == 6 && strncasecmp(format_name, "GDIFF4", 6) == 0) ||
		(5 == len && strncasecmp(format_name, "GDIFF", 5) == 0))
	{
		return GDIFF4_FORMAT;
	}
	else if (6 == len && strncasecmp(format_name, "GDIFF5", 6) == 0)
	{
		return GDIFF5_FORMAT;
	}
	else if (6 == len && strncasecmp(format_name, "BDELTA", 5) == 0)
	{
		return BDELTA_FORMAT;
	}
	else if (5 == len && strncasecmp(format_name, "XDELTA", 5) == 0)
	{
		return XDELTA1_FORMAT;
	}
	else if (9 == len && strncasecmp(format_name, "SWITCHING", 9) == 0)
	{
		return SWITCHING_FORMAT;
	}
	else if (5 == len && strncasecmp(format_name, "BDIFF", 5) == 0)
	{
		return BDIFF_FORMAT;
/* should someday bsdiff output be supported, uncomment this.
   } else if((6==len && strncasecmp(format_name, "BSDIFF", 6)==0) ||
		(7==len && strncasecmp(format_name, "BSDIFF4", 7)==0)) {
		return BSDIFF4_FORMAT;
   } else if((7==len && strncasecmp(format_name, "BSDIFF3", 7)==0) ||
		(8==len && strncasecmp(format_name, "QSUFDIFF", 8)==0)) {
		return BSDIFF3_FORMAT;
*/   }
return 0;
}

unsigned long int
identify_format(cfile *patchf)
{
	unsigned long int val = 0;
	unsigned long int format = 0;
	if ((val = check_gdiff4_magic(patchf)))
	{
		format = GDIFF4_FORMAT;
	}
	else if ((val = check_gdiff5_magic(patchf)))
	{
		format = GDIFF5_FORMAT;
	}
	else if ((val = check_switching_magic(patchf)))
	{
		format = SWITCHING_FORMAT;
	}
	else if ((val = check_bdelta_magic(patchf)))
	{
		format = BDELTA_FORMAT;
	}
	else if ((val = check_bdiff_magic(patchf)))
	{
		format = BDIFF_FORMAT;
	}
	else if ((val = check_xdelta1_magic(patchf)))
	{
		format = XDELTA1_FORMAT;
	}
	else if ((val = check_bsdiff_magic(patchf)))
	{
		format = BSDIFF_FORMAT;
	}
	else if ((val = check_fdtu_magic(patchf)))
	{
		format = FDTU_FORMAT;
	}
	v2printf("identify_format, val=%lu, format=%lu\n", val, format);
	if (format == 0)
	{
		return 0;
	}
	return ((format << 16) | val);
}
