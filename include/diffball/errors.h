// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_ERRORS
#define _HEADER_ERRORS 1
#include "defs.h"

#define check_return(err, msg, extra)                                     \
	if (err)                                                              \
	{                                                                     \
		fprintf(stderr, "error detected in %s:%d\n", __FILE__, __LINE__); \
		if (msg)                                                          \
			fprintf(stderr, "%s: ", (char *)(msg));                       \
		print_error(err);                                                 \
		if (extra)                                                        \
			fprintf(stderr, "%s\n", (char *)(extra));                     \
		exit(err);                                                        \
	}

#define check_return2(err, msg)                                           \
	if (err)                                                              \
	{                                                                     \
		fprintf(stderr, "error detected in %s:%d\n", __FILE__, __LINE__); \
		if (msg)                                                          \
			fprintf(stderr, "%s: ", (char *)(msg));                       \
		print_error(err);                                                 \
		exit(err);                                                        \
	}

#define check_return_ret(err, level, msg)    \
	if (err)                                 \
	{                                        \
		if (_diffball_logging_level > level) \
		{                                    \
			if (msg)                         \
				fprintf(stderr, msg);        \
			print_error(err);                \
		}                                    \
		return err;                          \
	}

void print_error(int err);

#endif
