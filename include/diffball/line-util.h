// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_LINE_UTILITIES
#define _HEADER_LINE_UTILITIES 1
#include <cfile.h>

unsigned long skip_lines_forward(cfile *cfh, unsigned long n);
unsigned long skip_lines_backward(cfile *cfh, unsigned long n);

#endif
