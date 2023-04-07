AUTOMAKE_OPTIONS	= dist-bzip2 no-dist-gzip foreign

AM_CFLAGS = -Wall -D_GNU_SOURCE -I$(top_builddir)/include/
SUBDIRS = include man

if BUILD_DEBUG_CFILE
	cfile_bins = ctest
else
	cfile_bins =
endif

lib_LTLIBRARIES = libcfile.la
libcfile_la_SOURCES = \
	libcfile/logging.c \
	libcfile/cfile.c \
	libcfile/noop.c \
	libcfile/gzip.c \
	libcfile/bz2.c \
	libcfile/lzma.c \
	libcfile/multifile.c
libcfile_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(LIBLZMA_CFLAGS)
libcfile_la_LIBADD = \
	$(AM_LIBADD) \
	$(LIBLZMA_LIBS)
noinst_HEADERS = libcfile/internal.h

FORMAT_FILES = \
	libdiffball/bit-functions.c \
	libdiffball/gdiff.c \
	libdiffball/switching.c \
	libdiffball/bdiff.c \
	libdiffball/bdelta.c \
	libdiffball/xdelta1.c \
	libdiffball/bsdiff.c \
	libdiffball/fdtu.c \
	libdiffball/tree.c \
	libdiffball/formats.c

# internal represeentation of commands.
DCB_FILES  = libdiffball/dcbuffer.c \
    libdiffball/dcb-cfh-funcs.c \
    libdiffball/command_list.c

# algorithm implementations
ALGO_FILES = libdiffball/diff-algs.c \
    libdiffball/adler32.c \
    libdiffball/hash.c
API_FILES = libdiffball/api.c \
    libdiffball/apply-patch.c \
    libdiffball/errors.c
MISC_FILES = libdiffball/logging.c

libdiffball_la_SOURCES = \
	$(FORMAT_FILES) \
	$(DCB_FILES) \
	$(ALGO_FILES) \
	$(API_FILES) \
	$(MISC_FILES)
libdiffball_la_LDFLAGS = -lm
libdiffball_la_CFLAGS = $(AM_CFLAGS) -DDIFFBALL_ENABLE_INLINE
lib_LTLIBRARIES += libdiffball.la
libdiffball_la_LIBADD = libcfile.la


DIFF_LIBS = libdiffball.la
#CFILE_LIBS = libcfile.la
bin_PROGRAMS = differ patcher diffball convert_delta delta_tree delta_patcher ${cfile_bins}

patcher_LDADD	=	${DIFF_LIBS}
patcher_SOURCES = 	patcher.c	\
					options.c

delta_patcher_LDADD	=	${DIFF_LIBS}
delta_patcher_SOURCES = 	delta_patcher.c	\
						options.c

diffball_LDADD	= 	${DIFF_LIBS}
diffball_SOURCES = 	diffball.c	\
					tar.c 		\
					options.c

differ_LDADD	=	${DIFF_LIBS}
differ_SOURCES = 	differ.c 	\
					options.c 

convert_delta_LDADD   =	${DIFF_LIBS}
convert_delta_SOURCES = convert_delta.c	\
						options.c

delta_tree_LDADD	=	${DIFF_LIBS}
delta_tree_SOURCES = 	delta_tree.c 	\
						options.c

#man_MANS = differ.1 diffball.1 patcher.1 convert_delta.1
#EXTRA_DIST = $(man_MANS)

# line-util.h is left out, as is udiff.h
noinst_HEADERS += tar.h options.h

dist-hook:
	git log --format=short > $(distdir)/ChangeLog
