#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.61])
AC_INIT([FbTerm], [1.5])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/fbterm.cpp])
AC_CONFIG_HEADERS([config.h])

AC_SUBST([RELEASE_DATE], "April 2009")

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_RANLIB
PKG_PROG_PKG_CONFIG

AC_ARG_ENABLE(epoll, 
              AC_HELP_STRING([--disable-epoll], [Disable use epoll system call [[default=auto]]]), 
              [EPOLL="$enalbeval"],
              [EPOLL=auto])

AC_ARG_ENABLE(signalfd,
              AC_HELP_STRING([--disable-signalfd], [Disable use signalfd system call [[default=auto]]]),
              [SIGNALFD="$enalbeval"],
              [SIGNALFD=auto])

                                          
# Checks for libraries.
AC_CHECK_LIB([util], [forkpty])
PKG_CHECK_MODULES([freetype2], [freetype2])
PKG_CHECK_MODULES([fontconfig], [fontconfig])

AC_SUBST([FT2_INC], ["$freetype2_CFLAGS"])
AC_SUBST([FT2_LIB], ["$freetype2"])

AC_SUBST([FC_INC], ["$fontconfig_CFLAGS"])
AC_SUBST([FC_LIB], ["$fontconfig_LIBS"])

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([fcntl.h langinfo.h locale.h stddef.h stdlib.h string.h termios.h unistd.h \
				  sys/ioctl.h sys/socket.h sys/time.h linux/fb.h linux/kd.h linux/keyboard.h linux/input.h],
				  [], [AC_MSG_RESULT(required header file is missing!); exit 1])

AC_CHECK_HEADERS([gpm.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_STRUCT_TM

# Checks for library functions.
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MMAP
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_CHECK_FUNCS([memset memmove munmap nl_langinfo setenv setlocale socket strcasecmp strchr strstr strtol],
				[], [AC_MSG_RESULT(required lib function is missing!); exit 1])

if test x"$EPOLL" = xauto -a x"$cross_compiling" = xno; then
  AC_RUN_IFELSE(
    AC_LANG_PROGRAM([[#include <sys/epoll.h>]],
      [[if (epoll_create(10) >= 0) return 0; return 1;]]),
    [EPOLL=yes]
  )
fi

if test x"$SIGNALFD" = xauto -a x"$cross_compiling" = xno; then
  AC_RUN_IFELSE(
    AC_LANG_PROGRAM([[#include <sys/signalfd.h>]],
      [[sigset_t mask; if (signalfd(-1, &mask, 0) >= 0) return 0; return 1;]]),
    [SIGNALFD=yes]
  )
fi

AH_TEMPLATE([HAVE_EPOLL])
AH_TEMPLATE([HAVE_SIGNALFD])

[[ "$EPOLL" = "yes" ]] && AC_DEFINE([HAVE_EPOLL])
[[ "$SIGNALFD" = "yes" ]] && AC_DEFINE([HAVE_SIGNALFD])

AC_CONFIG_FILES([Makefile src/Makefile src/lib/Makefile im/Makefile terminfo/Makefile doc/Makefile doc/fbterm.1])
AC_CONFIG_LINKS([im/instance.h:src/lib/instance.h im/input_key.h:src/input_key.h im/immessage.h:src/immessage.h])
AC_OUTPUT

if test "$ac_cv_header_gpm_h" != yes; then
	AC_MSG_WARN([gpm.h dosn't exist! gpm mouse support will be disabled!])
fi
