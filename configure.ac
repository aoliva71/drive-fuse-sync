#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([drivefusesync], [0.1], [aoliva71@gmail.com])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile src/Makefile])

AM_INIT_AUTOMAKE

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
PKG_CHECK_MODULES(FUSE,[fuse])
PKG_CHECK_MODULES(CURL,[libcurl])
PKG_CHECK_MODULES(JSONC,[json-c])
PKG_CHECK_MODULES(SQLITE3,[sqlite3])

AC_CHECK_HEADER([log4c.h])
AC_CHECK_LIB([log4c],[log4c_init])

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_CHECK_FUNCS([memset])

AC_OUTPUT
