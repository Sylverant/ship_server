dnl
dnl This file is part of Sylverant PSO Server.
dnl
dnl Copyright (C) 2009, 2011, 2013, 2015, 2016, 2018 Lawrence Sebald
dnl
dnl This program is free software: you can redistribute it and/or modify
dnl it under the terms of the GNU Affero General Public License version 3
dnl as published by the Free Software Foundation.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU Affero General Public License for more details.
dnl
dnl You should have received a copy of the GNU Affero General Public License
dnl along with this program.  If not, see <http://www.gnu.org/licenses/>.

AC_PREREQ([2.61])

AC_INIT([ship_server], [0.1.0])
AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([subdir-objects])
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_GREP

echo $host_os | $GREP darwin > /dev/null && IS_OSX=yes

AC_LANG(C)

AC_CONFIG_SRCDIR([src/block.c])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
if test $IS_OSX; then
    test $libxml2_CFLAGS || libxml2_CFLAGS="-I/usr/include/libxml2"
    test $libxml2_LIBS || libxml2_LIBS="-lxml2"
else
    PKG_CHECK_MODULES([libxml2], [libxml-2.0 >= 2.6])
fi

CFLAGS="$CFLAGS $libxml2_CFLAGS"
LIBS="$LIBS $libxml2_LIBS"

PKG_CHECK_MODULES([gnutls], [gnutls >= 2.0])
CFLAGS="$CFLAGS $gnutls_CFLAGS"
LIBS="$LIBS $gnutls_LIBS"

AC_CHECK_LIB([sylverant], [sylverant_read_config], , AC_MSG_ERROR([libsylverant is required!]))
AC_CHECK_LIB([mini18n], [mini18n_get], , AC_MSG_WARN([Internationalization support requires mini18n]))
AC_CHECK_LIB([psoarchive], [pso_gsl_read_open], , AC_MSG_ERROR([libpsoarchive is required!]))

AC_SEARCH_LIBS([pidfile_open], [util bsd], [NEED_PIDFILE=0], [NEED_PIDFILE=1])

dnl AC_ARG_ENABLE([scripting], [AS_HELP_STRING([--enable-scripting],
dnl               [enable scripting support in the binary (requires Python 2.x (x >= 5))])],
dnl               [enable_scripting=$enableval],
dnl               [enable_scripting=no])

dnl AS_IF([test "x$enable_scripting" != xno], [
dnl     AM_PATH_PYTHON([2.5], , AC_MSG_WARN([Scripting support requires Python >= 2.5]))
dnl
dnl     # Add Python's flags into the mix if its enabled.
dnl     if test "$PYTHON" != ":" ; then
dnl         AC_ARG_VAR([PYTHON_INCLUDES], [Include directories for python])
dnl         AC_ARG_VAR([PYTHON_LIBS], [Link libraries for python])
dnl         AC_ARG_VAR([PYTHON_CONFIG], [Path to python-config])
dnl         AS_IF([test -z "$PYTHON_CONFIG"], [
dnl             AC_PATH_PROGS([PYTHON_CONFIG],
dnl                           [python$PYTHON_VERSION-config python-config],
dnl                           [no],
dnl                           [`dirname $PYTHON`])
dnl             AS_IF([test "$PYTHON_CONFIG" = no],
dnl                   [AC_MSG_ERROR([cannot find python-config for $PYTHON.])])
dnl         ])
dnl
dnl         AS_IF([test -z "$PYTHON_INCLUDES"], [
dnl             AC_MSG_CHECKING([python include directories])
dnl             PYTHON_INCLUDES=`$PYTHON_CONFIG --includes`
dnl             AC_MSG_RESULT([$PYTHON_INCLUDES])
dnl         ])
dnl
dnl         AS_IF([test -z "$PYTHON_LIBS"], [
dnl             AC_MSG_CHECKING([python libraries])
dnl             PYTHON_LIBS=`$PYTHON_CONFIG --libs`
dnl             AC_MSG_RESULT([$PYTHON_LIBS])
dnl         ])
dnl
dnl         CFLAGS="$CFLAGS $PYTHON_INCLUDES -DHAVE_PYTHON=1"
dnl         LIBS="$LIBS $PYTHON_LIBS"
dnl     fi
dnl ])

AM_ICONV
AM_ICONV_LINK
AM_CONDITIONAL([NEED_PIDFILE], [test "$NEED_PIDFILE" -eq 1])

AX_PTHREAD

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h inttypes.h netdb.h netinet/in.h stdlib.h string.h sys/socket.h sys/time.h unistd.h pwd.h grp.h])
AC_CHECK_HEADERS([libutil.h bsd/libutil.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T
AC_C_BIGENDIAN
AC_CHECK_SIZEOF([void *])
AC_CHECK_SIZEOF([int])
AC_CHECK_SIZEOF([long int])

# Checks for library functions.
AC_CHECK_FUNCS([malloc realloc gethostname gettimeofday inet_ntoa memmove memset select socket strtoul getgrouplist])

CFLAGS="$CFLAGS -Wall -DGIT_BUILD=\"\\\"\`git log --oneline | wc -l | tr -d \'[[:space:]]\'\`\\\"\""
CFLAGS="$CFLAGS -DGIT_CHANGESET=\"\\\"\`git show -s --pretty=format:%h\`\\\"\""

AC_ARG_ENABLE([debug], [AS_HELP_STRING([--enable-debug],
              [enable debugging symbols in the binary])],
              [enable_debug=$enableval],
              [enable_debug=no])
AC_ARG_ENABLE([ipv6], [AS_HELP_STRING([--disable-ipv6],
               [disable IPv6 support (enabled by default)])],
               [enable_ipv6=$enableval],
               [enable_ipv6=yes])

AS_IF([test "x$enable_ipv6" != xno],
      [AC_DEFINE([SYLVERANT_ENABLE_IPV6], [1],
                 [Define if you want IPv6 support])])

AS_IF([test "x$enable_debug" != xno],
      [AC_DEFINE([DEBUG], [1], [Define if you want debugging turned on])
       CFLAGS="$CFLAGS -g -DSYLVERANT_DEBUG"])

AC_CONFIG_FILES([Makefile]
                [l10n/Makefile])

AC_OUTPUT
