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
#ifndef _HEADER_BDIFF
#define _HEADER_BDIFF 1
#define BDIFF_MAGIC "BDIFF"
#define BDIFF_MAGIC_LEN 5
#define BDIFF_VERSION 'a'
#define BDIFF_DEFAULT_MAXBLOCKSIZE (1<<20)
#include <diffball/diff-algs.h>
#include <cfile.h>

unsigned int check_bdiff_magic(cfile *patchf);
signed int bdiffEncodeDCBuffer(CommandBuffer *buffer, cfile *out_cfh);
signed int bdiffReconstructDCBuff(DCB_SRC_ID src_id, cfile *patchf, CommandBuffer *dcbuff);
#endif
