// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2014 Brian Harring <ferringb@gmail.com>
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
#define TREE_COMMAND_MODE_LEN		2

#define TREE_COMMAND_LEN		1
#define TREE_COMMAND_REG 		0x00
#define TREE_COMMAND_HARDLINK   0x01
#define TREE_COMMAND_DIR		0x02
#define TREE_COMMAND_SYM		0x03
#define TREE_COMMAND_FIFO		0x04
#define TREE_COMMAND_CHR		0x05
#define TREE_COMMAND_BLK		0x06
#define TREE_COMMAND_SOCKET		0x07
#define TREE_COMMAND_UNLINK		0x08

// Flags for commands.
// If set, then mtime is not encoded- ctime is the same value.
#define TREE_COMMAND_REUSE_CTIME 0x40


/* TREE NOTES

This could benefit from some optimization; in examining some cases,
~44%  was the data *prior* to the delta.
~47.8% was the delta
~8.9% was the command stream

FORMAT:
  magic
  version
  4 bytes: # of src files.
    filename\0 # PE encoded;
    8 bytes size
  4 bytes: # of generated files.
    filename\0 # PE encoded;
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

# PE encoded == string, relative path encoded against the last path using this scheme.


single byte command:
 0x00 == regenerated file
   UID/GID/MODE index
   ctime
   mtime # Only if TREE_COMMAND_REUSE_CTIME isn't set.
   xattrs null delimited
 0x01 == hardlink
   filename null delimited; PE encoded
   null delimited hardlink source; PE encoded
 0x02 == directory
   filename null delimited; PE encoded
   UID/GID/MODE index
   ctime
   mtime # Only if TREE_COMMAND_REUSE_CTIME isn't set.
   xattrs null delimited
 0x03 == symlink
   filename null delimited; PE encoded
   symlink target null delimited
   UID/GID/MODE index
   ctime
   mtime # Only if TREE_COMMAND_REUSE_CTIME isn't set.
 0x04 == fifo
   filename null delimited; PE encoded
   UID/GID/MODE index
   ctime
   mtime # Only if TREE_COMMAND_REUSE_CTIME isn't set.
 0x05 == chr device
   filename null delimited; PE encoded
   UID/GID/MODE index
   ctime
   mtime # Only if TREE_COMMAND_REUSE_CTIME isn't set.
   4 bytes major
   4 bytes minor
 0x06 == blk device
   filename null delimited; PE encoded
   UID/GID/MODE index
   ctime
   mtime # Only if TREE_COMMAND_REUSE_CTIME isn't set.
   4 bytes major
   4 bytes minor
 0x07 == socket
   filename null delimited; PE encoded
   UID/GID/MODE index
   ctime
   mtime # Only if TREE_COMMAND_REUSE_CTIME isn't set.
 0x08 == delete
   filename null delimited; PE encoded

*/


signed int treeReconstruct(const char *src_directory, cfile *patchf, const char *raw_directory, const char *tmp_directory);

#endif
