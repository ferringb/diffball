# format implementations
#udiff.c is intentionally left out, as is line-util.c
FORMAT_FILES = \
	bit-functions.c \
	gdiff.c \
	switching.c \
	bdiff.c \
	bdelta.c \
	xdelta1.c \
	bsdiff.c \
	fdtu.c \
	tree.c \
	formats.c

# internal represeentation of commands.
DCB_FILES  = dcbuffer.c dcb-cfh-funcs.c command_list.c

# algorithm implementations
ALGO_FILES = diff-algs.c adler32.c hash.c
API_FILES = api.c apply-patch.c errors.c
MISC_FILES = logging.c

libdiffball_la_SOURCES = \
	$(FORMAT_FILES) \
	$(DCB_FILES) \
	$(ALGO_FILES) \
	$(API_FILES) \
	$(MISC_FILES)

libdiffball_la_LDFLAGS = -lm
libdiffball_la_CFLAGS = $(AM_CFLAGS) -I$(top_builddir)/include -DDIFFBALL_ENABLE_INLINE
lib_LTLIBRARIES = libdiffball.la
libdiffball_la_LIBADD = $(top_builddir)/libcfile/libcfile.la
