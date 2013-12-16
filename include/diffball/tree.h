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
#ifndef _HEADER_BDELTA
#define _HEADER_BDELTA 1
#include <diffball/dcbuffer.h>
#include <cfile.h>

#define TREE_MAGIC				"TREE"
#define TREE_MAGIC_LEN		4
#define TREE_VERSION				0x1
#define TREE_VERSION_LEN		2

#define TREE_INTERFILE_MAGIC	"--TREE--"
#define TREE_INTERFILE_MAGIC_LEN			8

unsigned int check_tree_magic(cfile *patchf);
signed int treeEncodeDCBuffer(CommandBuffer *dcbuff, 
			cfile *out_cfh);
signed int treeReconstructDCBuff(DCB_SRC_ID src_id, cfile *patchf, 
		CommandBuffer *dcbuff);


// These are defined so that OS specific values- systems limited to unsigned short uid's for example-
// don't wind up having OS sizes encoded into the cross OS patches.
#define TREE_COMMAND_UID_LEN		4
#define TREE_COMMAND_GID_LEN		4
#define TREE_COMMAND_MODE_LEN		2
#define TREE_COMMAND_DEV_LEN		4
// Note; this doesn't play nice with high precission FS's; the second granualarity is
// right, but the NS precision won't be.
#define TREE_COMMAND_TIME_LEN		4

#define TREE_COMMAND_LEN		1
#define TREE_COMMAND_REG 		0x00
#define TREE_COMMAND_HARDLINK 		0x01
#define TREE_COMMAND_DIR		0x02
#define TREE_COMMAND_SYM		0x03
#define TREE_COMMAND_FIFO		0x04
#define TREE_COMMAND_CHR		0x05
#define TREE_COMMAND_BLK		0x06
#define TREE_COMMAND_UNLINK		0x07

/* TREE NOTES

This could benefit from some optimization; in examining some cases,
~44%  was the data *prior* to the delta.
~47.8% was the delta
~8.9% was the command stream

FORMAT:
  magic
  version
  4 bytes: # of src files.
    filename\0
    8 bytes size
  4 bytes: # of generated files.
    filename\0
    8 bytes size
  8 bytes: # length of the delta
  <delta>
  --TREE--
  4 bytes: # of UID/GID/MODE tuple entries in the table; position is the index.
    4 bytes: uid
    4 bytes: gid
    3 bytes: mode
  4 bytes: # command count
  command-stream


single byte command:
 0x01 == delta
   UID/GID/MODE index
   ctime
   mtime
   xattrs null delimited
 0x02 == hardlink
   filename null delimited
   null delimited hardlink source
 0x03 == directory
   filename null delimited
   UID/GID/MODE index
   ctime
   mtime
   xattrs null delimited
 0x04 == symlink
   filename null delimited
   symlink target null delimited
   UID/GID/MODE index
   ctime
   mtime
 0x05 == fifo
   filename null delimited
   UID/GID/MODE index
   ctime
   mtime
 0x06 == chr device
   filename null delimited
   UID/GID/MODE index
   ctime
   mtime
   4 bytes major
   4 bytes minor
 0x07 == blk device
   filename null delimited
   UID/GID/MODE index
   ctime
   mtime
   4 bytes major
   4 bytes minor
 0x08 == delete
   filename null delimited

*/


signed int treeReconstruct(const char *src_directory, cfile *patchf, const char *raw_directory, const char *tmp_directory);

#endif
