AC_INIT([TorrentZip], [0.0], [StatMan, shindakun, Ultrasubmarine, r3nh03k <http://trrntzip.sf.net>], [trrntzip])

AM_INIT_AUTOMAKE
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

AC_PROG_CC

AC_PROG_INSTALL

AC_CHECK_LIB([m], [fmod], [], [AC_MSG_ERROR("Need libm to build TorrentZip")])
AC_CHECK_LIB([z], [zlibVersion], [], [AC_MSG_ERROR("Need libz to build TorrentZip")])
AC_SYS_LARGEFILE
AC_FUNC_FSEEKO

AC_CONFIG_FILES([Makefile src/Makefile torrentzip.spec])
AC_OUTPUT
