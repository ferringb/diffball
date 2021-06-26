// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>

#ifndef _HEADER_STRING_MISC
#define _HEADER_STRING_MISC 1
#include "config.h"
#include <string.h>

#ifndef HAVE_STRNLEN
size_t strnlen(const char *s, size_t maxlen);
#endif

#endif
