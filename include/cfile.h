// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2014 Brian Harring <ferringb@gmail.com>
#ifndef _HEADER_CFILE
#define _HEADER_CFILE

#include <sys/types.h>
extern unsigned int cfile_verbosity;

#define CFILE_DEFAULT_BUFFER_SIZE (4096)
//#define CFILE_DEFAULT_BUFFER_SIZE		(BUFSIZ)
#define NO_COMPRESSOR (0x0)
#define GZIP_COMPRESSOR (0x1)
#define BZIP2_COMPRESSOR (0x2)
#define XZ_COMPRESSOR (0x3)
#define AUTODETECT_COMPRESSOR (0x4)

// access flags
#define CFILE_RONLY (0x1)
#define CFILE_WONLY (0x2)
#define CFILE_NEW (0x10)
#define CFILE_READABLE (0x1)
#define CFILE_WRITEABLE (0x2)
#define CFILE_WR (CFILE_READABLE | CFILE_WRITEABLE)
#define CFILE_OPEN_FH (0x8)
#define CFILE_SEEKABLE (0x10)
#define CFILE_BUFFER_ALL (0x20)
#define CFILE_SEEK_IS_COSTLY (0x40)

// state flags
#define CFILE_MEM_ALIAS (0x40)
#define CFILE_CHILD_CFH (0x80)
#define CFILE_EOF (0x100)
#define CFILE_IS_OPEN (0x200)
#define CFILE_FREE_AT_CLOSING (0x400)
#define CFILE_CHILD_INHERITS_IO (0x800)
//#define CFILE_FLAG_BACKWARD_SEEKS		(0x800)

#define BZIP2_DEFAULT_COMPRESS_LEVEL 9
#ifdef DEBUG_CFILE
#define BZIP2_VERBOSITY_LEVEL 4
#else
#define BZIP2_VERBOSITY_LEVEL 0
#endif
#define BZIP2_DEFAULT_WORK_LEVEL 30

/*lseek type stuff
SEEK_SET
		The offset is set to offset bytes.
SEEK_CUR
		The offset is set to its current location plus off-
		set bytes.
SEEK_END
		The offset is set to the size of the file plus off-
		set bytes.*/
#define CSEEK_ABS 0
#define CSEEK_CUR 1
#define CSEEK_END 2
#define CSEEK_FSTART 3

/* errors */
#define IO_ERROR (-1)
#define EOF_ERROR (-2)
#define MEM_ERROR (-3)

#define CFILE_DEFAULT_MEM_ALIAS_W_REALLOC 0x40000

// this is thrown when the api allows something, but code doesn't yet.
// case in point, compressors bound to a copen_mem cfile.
// doing it this route, rather then changing the api down the line.
#define UNSUPPORTED_OPT (-6)

typedef struct _cfile *cfile_ptr;
typedef int (*copen_io_func)(cfile_ptr);
typedef unsigned int (*cclose_io_func)(cfile_ptr, void *);
typedef ssize_t (*cwrite_io_func)(cfile_ptr, void *src, size_t len);
typedef ssize_t (*cread_io_func)(cfile_ptr, void *out, size_t len);
typedef int (*crefill_io_func)(cfile_ptr, void *);
typedef ssize_t (*cflush_io_func)(cfile_ptr, void *);
typedef ssize_t (*cseek_io_func)(cfile_ptr, void *, ssize_t raw_offset, ssize_t data_offset, int offset_type);

typedef struct _cfile_io
{
	copen_io_func open;
	cclose_io_func close;
	cwrite_io_func write;
	cread_io_func read;
	crefill_io_func refill;
	cflush_io_func flush;
	cseek_io_func seek;
	void *data;
} cfile_io;

typedef struct
{
	size_t offset;
	size_t pos;
	size_t end;
	size_t size;
	size_t write_start;
	size_t write_end;
	unsigned char *buff;
	size_t window_offset;
	size_t window_len;
} cfile_window;

typedef unsigned short CFH_ID;
typedef signed int ECFH_ID;

typedef struct _cfile
{
	CFH_ID cfh_id;
	int raw_fh;
	unsigned int compressor_type;
	unsigned int access_flags;
	unsigned long state_flags;
	int err;

	/* Note: this pretty much attrocious.  Needs sanitizing... */
	union
	{
		struct
		{
			unsigned int last;
			unsigned int handle_count;
		} parent;
		cfile_ptr parent_ptr;
	} lseek_info;

	cfile_window data;

	cfile_window raw;

	/* io backing */
	cfile_io io;

} cfile;

#define CFH_IS_SEEKABLE(cfh) (((cfh)->access_flags & CFILE_SEEKABLE) > 1)
#define CFH_SEEK_IS_COSTLY(cfh) (((cfh)->access_flags & CFILE_SEEK_IS_COSTLY) > 1)
#define FREE_CFH_AT_CLOSE(cfh) ((cfh)->state_flags |= CFILE_FREE_AT_CLOSING)
#define CFH_IS_CHILD(cfh) ((cfh)->state_flags & CFILE_CHILD_CFH)

int internal_copen(cfile *cfh, int fh,
				   size_t raw_fh_start, size_t raw_fh_end,
				   size_t data_fh_start, size_t data_fh_end,
				   unsigned int compressor_type, unsigned int access_flags);

int copen_mem(cfile *cfh, unsigned char *buff, size_t len, unsigned int compressor_type, unsigned int access_flags);
int copen(cfile *cfh, const char *filename, unsigned int compressor_type, unsigned int access_flags);

int copen_child_cfh(cfile *cfh, cfile *parent, size_t fh_start,
					size_t fh_end, unsigned int compressor_type, unsigned int access_flags);

cfile *copen_dup_cfh(cfile *cfh);
int copen_dup_fd(cfile *cfh, int fh, size_t fh_start, size_t fh_end,
				 unsigned int compressor_type, unsigned int access_flags);

unsigned int cclose(cfile *cfh);
ssize_t cread(cfile *cfh, void *out_buff, size_t len);
ssize_t cwrite(cfile *cfh, void *in_buff, size_t len);
ssize_t crefill(cfile *cfh);
ssize_t cflush(cfile *cfh);
size_t ctell(cfile *cfh, unsigned int tell_type);
ssize_t cseek(cfile *cfh, ssize_t offset, int offset_type);
ssize_t copy_cfile_block(cfile *out_cfh, cfile *in_cfh, size_t in_offset, size_t len);
size_t cfile_len(cfile *cfh);
size_t cfile_start_offset(cfile *cfh);
cfile_window *expose_page(cfile *cfh);
cfile_window *next_page(cfile *cfh);
cfile_window *prev_page(cfile *cfh);
int cfile_is_open(cfile *cfh);

typedef struct
{
	char *filename;
	struct stat *st;
	size_t start;
	size_t end;
	char *link_target;
} multifile_file_data;

int multifile_expose_content(cfile *cfh, multifile_file_data ***results, unsigned long *file_count);
multifile_file_data *multifile_find_file(const char *filename, multifile_file_data **array, unsigned long fs_count);

typedef int (*copen_io_func)(cfile_ptr);

typedef int (*multifile_directory_filter)(void *data, const char *filepath, struct stat *st);
int copen_multifile_directory(cfile *cfh, const char *src_directory, multifile_directory_filter filter_func, void *filter_data);
int copen_multifile(cfile *cfh, const char *root, multifile_file_data **files, unsigned long file_count, unsigned int access_flags);

// Given an array, free the data in each item.  Note this doesn't free the array itself- just the contents.
// freeing the array is left to the caller to sort.
void multifile_free_file_data_array(multifile_file_data **array, unsigned long count);

// Ensure the files for the array exist- creating them if necessary and allowed, else erroring.
// If report_all_failures is nonzero, then this code will continue through the full list, exiting
// with 1 if any failed (rather than stopping at the first).  report_all_failures should not be
// nonzero if allow_creation is nonzero; kind of pointless in that case.
int multifile_ensure_files(cfile *cfh, int allow_creation, int report_all_failures);

unsigned char *cfile_read_null_string(cfile *cfh);
unsigned char *cfile_read_string_delim(cfile *cfh, char delim, int eof_is_delim);
#endif
