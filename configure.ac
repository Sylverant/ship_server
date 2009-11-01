dnl
dnl This file is part of Sylverant PSO Server.
dnl
dnl Copyright (C) 2009 Lawrence Sebald
dnl
dnl This program is free software: you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License version 3 as
dnl published by the Free Software Foundation.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this program.  If not, see <http://www.gnu.org/licenses/>.

AC_PREREQ([2.62])
AC_INIT([ship_server], [0.1.0])
AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([ship_server], [0.1.0])
AC_PROG_INSTALL
AC_PROG_LIBTOOL

AC_LANG(C)

AC_CONFIG_SRCDIR([src/block.c])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([expat], [XML_ParserCreate], , AC_MSG_ERROR([libexpat is required!]))
AC_CHECK_LIB([sylverant], [sylverant_read_config], , AC_MSG_ERROR([libsylverant is required!]))
AC_CHECK_LIB([crypto], [RC4], , AC_MSG_ERROR([libcrypto is required!]))

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h inttypes.h netdb.h netinet/in.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([gethostname gettimeofday inet_ntoa memmove memset select socket strtoul])

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
