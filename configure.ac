# Process this file with autoconf to produce a configure script.
AC_INIT([diffball],[1.x],[ferringb@gmail.com])

AC_PROG_CC
AC_PROG_CC_STDC

AC_ARG_ENABLE(asserts,
    [AS_HELP_STRING([--enable-asserts],[enable all asserts (default no)])],
    [case "${enableval}" in
        yes) enable_asserts=true ;;
        no)  enable_asserts=false ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-asserts) ;;
    esac]
)

AC_ARG_ENABLE(debug-dcbuffer,
    [AS_HELP_STRING([--enable-debug-dcbuffer],[enable testing of internal command buffer (default no)])],
    [case "${enableval}" in
        yes) enable_debug_dcbuffer=true ;;
        no)  enable_debug_dcbuffer=false ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-debug-dcbuffer) ;;
    esac]
)

AC_ARG_ENABLE(debug-cfile,
    [AS_HELP_STRING([--enable-debug-cfile],[enable testing of internal cfile buffer (default no)])],
    [case "${enableval}" in
        yes) enable_debug_cfile=true ;;
        no)  enable_debug_cfile=false ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-debug-cfile) ;;
        esac]
)

if test x$enable_debug_cfile = xtrue
then
	AC_MSG_RESULT(DEBUGGING: enabling cfile debugging.)
	AC_DEFINE(DEBUG_CFILE, 1, cfile debug messages)
	enable_asserts=true
fi

if test x$enable_debug_hash = xtrue
then
	AC_MSG_RESULT(DEBUGGING: enabling hash debugging.)
	AC_DEFINE(DEBUG_HASH, 1, hash debug messages)
	enable_asserts=true
fi

if test x$enable_debug_dcbuffer = xtrue
then
	AC_MSG_RESULT(DEBUGGING: enabling dcbuffer debugging.)
	AC_DEFINE(DEBUG_DCBUFFER, 1, dcbuffer debug messages)
	enable_asserts=true
fi

if test x$enable_asserts = xtrue
then
	AC_MSG_RESULT(DEBUGGING: enabling asserts.)
	AC_DEFINE(DEV_VERSION, 1, asserts.)
	CFLAGS="$CFLAGS -g"
	CXXFLAGS="$CXXFLAGS -g"
fi

# keep these here.  they screw with cflags setting, meaning they're *evil*.
AM_INIT_AUTOMAKE([subdir-objects])
LT_INIT
AC_CONFIG_HEADERS([config.h])

AM_CONDITIONAL(BUILD_DEBUG_CFILE, test x$enable_debug_cfile = xtrue)
AM_CONDITIONAL(BUILD_DEBUG_HASH, test x$enable_debug_hash = xtrue)

# Checks for libraries.
AC_CHECK_LIB(bz2, BZ2_bzCompressInit, , AC_MSG_ERROR([libbz2 not found]))
AC_CHECK_LIB(z, gzdopen, , AC_MSG_ERROR([libz not found]))
AC_CHECK_LIB(m, ceil)

# Using pkgconfig for liblzma, as xz-utils provides liblzma.pc
PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([LIBLZMA], [liblzma])


AC_CHECK_HEADERS([errno.h fcntl.h stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_STAT
AC_CHECK_FUNCS([dup2 memmove strtol memset realloc])

CFLAGS="$CFLAGS -Wall"
CXXFLAGS="$CXXFLAGS -Wall"

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([include/Makefile])
AC_CONFIG_FILES([man/Makefile])
AC_OUTPUT
