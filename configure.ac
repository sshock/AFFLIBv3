                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
# Order is largely irrevellant, although it must start with AC_INIT and end with AC_OUTPUT
# See http://autotoolset.sourceforge.net/tutorial.html
# and http://www.openismus.com/documents/linux/automake/automake.shtml

AC_INIT([AFFLIB],[3.7.18],[https://github.com/sshock/AFFLIBv3])
AM_INIT_AUTOMAKE
AM_MAINTAINER_MODE

AC_CONFIG_FILES([Makefile lib/Makefile tools/Makefile lzma443/Makefile
			  tests/Makefile doc/Makefile pyaff/Makefile man/Makefile lib/version.h])
AC_CONFIG_FILES([afflib.spec])
AC_CONFIG_HEADERS([affconfig.h])

# Where we get installed
AC_PREFIX_PROGRAM

# Programs that we will be using
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL

# Turn off shared libraries during beta-testing, since they
# make the build process take too long.
AC_PROG_LIBTOOL
AC_CONFIG_MACRO_DIR([m4])

# We are now threading
m4_include([m4/acx_pthread.m4])
ACX_PTHREAD()

if test x"${cross_compiling}" = "xno" ; then
  # Bring additional directories where things might be found into our
  # search path. I don't know why autoconf doesn't do this by default
  for spfx in ${prefix} /sw /opt/local /usr/local ; do
    AC_MSG_NOTICE([checking ${spfx}/include])
    if test -d ${spfx}/include; then
        CPPFLAGS="$CPPFLAGS -I${spfx}/include"
        LDFLAGS="$LDFLAGS -L${spfx}/lib"
        AC_MSG_NOTICE([ *** ADDING ${spfx}/include to CPPFLAGS *** ])
        AC_MSG_NOTICE([ *** ADDING ${spfx}/lib to LDFLAGS *** ])
    fi
  done
  AC_MSG_NOTICE([ CPPFLAGS = ${CPPFLAGS} ])
  AC_MSG_NOTICE([ LDFLAGS = ${LDFLAGS} ])
else
  AC_MSG_NOTICE([Cross Compiling --- will not update CPPFALGS or LDFLAGS with /usr/local, /opt/local or /sw])
fi

if test -r /bin/uname.exe ; then
  if test `uname -o` == "Msys" ; then
    AC_MSG_NOTICE([Compiling with Msys. Setting flags appropriately.])
    enable_qemu="no"
    #CPPFLAGS="$CPPFLAGS -D__USE_MINGW_ANSI_STDIO"
    LIBS="$LIBS -lws2_32 -lgdi32"
    #LDFLAGS="$LDFLAGS -Wl,--enable-auto-import"
  fi
fi

# Special features that I use
AC_FUNC_ALLOCA
AC_FUNC_FSEEKO
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_SYS_LARGEFILE


# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_STRUCT_TM
AC_STRUCT_ST_BLOCKS
AC_STRUCT_ST_RDEV


# Specific headers that I plan to use
AC_CHECK_HEADERS([arpa/inet.h assert.h ctype.h dmalloc.h err.h errno.h fcntl.h getopt.h inttypes.h linux/fs.h malloc.h netinet/in.h regex.h signal.h stdint.h stdio.h stdlib.h string.h sys/cdefs.h sys/disk.h sys/file.h sys/ioctl.h sys/ioctl.h sys/param.h sys/param.h sys/socket.h sys/signal.h sys/stat.h sys/time.h sys/types.h sys/vfs.h sysexits.h time.h unistd.h zlib.h _mingw.h])

AC_CHECK_LIB([regex],[regcomp])        # see if we need -lregex

AC_CHECK_MEMBER([struct sockaddr_in.sin_len],
		[ AC_DEFINE(HAVE_SOCKADDR_SIN_LEN, 1, [Do we have sockaddr.sin_len?]) ],
		[],
		[
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
])


# Specific functions that we want to know about
AC_CHECK_FUNCS([getprogname strlcpy strlcat err_set_exit srandom srandomdev flock fstatfs valloc isdigit isalnum isalphanum isatty popen ftruncate memset mkdir putenv regcomp srandomdev strcasecmp strchr strdup strerror strrchr err errx warn warnx utimes unsetenv])

# Special features that can be enabled or disabled
AC_ARG_WITH([noopt], AC_HELP_STRING([--with-noopt],[Drop -O C flags]))

# C++ headers
AC_PROG_CXX
AC_LANG_PUSH([C++])
AC_CHECK_HEADERS([cstring])
AC_CHECK_HEADERS([string],
  AC_DEFINE([HAVE_STL],1,[STL Present])
  AC_MSG_NOTICE([STL Present]))
AC_LANG_POP([C++])

################################################################
# For AFF tools
AC_CHECK_HEADERS([readline/readline.h])
AC_CHECK_LIB([readline],[readline],, AC_MSG_RESULT([readline not installed]))
AC_CHECK_LIB([z],[uncompress],, AC_MSG_ERROR([zlib not installed; cannot continue. Try adding zlib-dev or zlib1g-dev.]))
AC_CHECK_LIB([rt],[aio_error64])

################################################################
## Expat
## Required for S3 and Digital Signatures
AC_ARG_WITH([expat],
  AS_HELP_STRING([--with-expat], [build with expat; required for S3 and Digital Signatures @<:@default=check@:>@]),
  [], [with_expat=check])

have_expat=no
AS_IF([test "x$with_expat" != xno],
  [AC_CHECK_HEADER([expat.h],
    [AC_CHECK_LIB([expat], [XML_ParserCreate])])
   have_expat=${ac_cv_lib_expat_XML_ParserCreate-no}
   AS_IF([test "x$with_expat" != xcheck && test "x$have_expat" = xno],
    [AC_MSG_FAILURE([--with-expat was given but test for expat failed])])])

################################################################
## LZMA (always enabled)
AC_DEFINE(USE_LZMA, 1, [Enable LZMA compression])

################################################################
## curl
## Required for S3
AC_ARG_WITH([curl],
  AS_HELP_STRING([--with-curl], [build with curl; required for S3 @<:@default=check@:>@]),
  [], [with_curl=check])

have_curl=no
AS_IF([test "x$with_curl" != xno],
  [AC_CHECK_HEADER([curl/curl.h],
    [AC_CHECK_LIB([curl], [curl_global_init],,)],)
   have_curl=${ac_cv_lib_curl_curl_global_init-no}
   AS_IF([test "x$with_curl" != xcheck && test "x$have_curl" = xno],
    [AC_MSG_FAILURE([--with-curl was given but test for curl failed])])])

################################################################
## Amazon S3
## S3 requires curl and expat; otherwise we don't need them
AC_ARG_ENABLE([s3],
  AC_HELP_STRING([--enable-s3], [Support for Amazon S3 service. Requires CURL and Expat. @<:@default=check@:>@]),
    [enable_s3=$enableval], [enable_s3=check])

AS_IF([test "x$enable_s3" != xno],
  [AS_IF([test "x$have_expat" = xyes && test "x$have_curl" = xyes],
    [enable_s3=yes],
    [AS_IF([test "x$enable_s3" = xcheck],
      [enable_s3=no],
      [AC_MSG_FAILURE([--enable-s3 was given but required libraries are not found])])])])

S3_BIN=
if test "x${enable_s3}" = "xyes" ; then
  AC_DEFINE([USE_S3],1,[Enable support for Amazon S3])
  S3_BIN='s3$(EXEEXT)'
fi
AC_SUBST(S3_BIN)
AM_CONDITIONAL([MAYBE_S3],[test "x${enable_s3}" = "xyes"])   # used by automake

################################################################
### QEMU
################################################################

AC_ARG_ENABLE([qemu])

echo "disable_qemu: " ${disable_qemu}
echo "enable_qemu:  " ${enable_qemu}

if test "x${enable_qemu}" = "xno" ; then
   enable_qemu="no"
   AC_DEFINE([DISABLE_QEMU],1,[User has disabled QEMU support])
else
   AC_DEFINE([USE_QEMU],1,[Use QEMU image drivers])
   enable_qemu="yes"
fi
AM_CONDITIONAL([MAYBE_QEMU],[test "x${enable_qemu}" = "xyes"])  # used by automake


################################################################
### PyAFF
################################################################
# python checks
AC_ARG_ENABLE([python],
	AC_HELP_STRING([--enable-python=no], [Build python bindings (pyaff)]),
        [enable_python=$enableval], [enable_python=no])
if test "${enable_python}" = "yes" ; then
  AM_PATH_PYTHON()
  AC_DEFINE([HAVE_PYTHON],1,[Build Python bindings])
fi

AM_CONDITIONAL(HAVE_PYTHON, test "$enable_python" = yes)



##
################################################################
################################################################
## Crypto (must follow S3)
## Note: -lcrypto is needed on Linux and MacOS
##       -lmd is needed on some older systems
##       TBD: is -lssl needed if using libressl?
## Always check for headers, then libs, then functions
###

AC_CHECK_HEADERS([openssl/aes.h openssl/bio.h openssl/evp.h openssl/hmac.h openssl/md5.h openssl/rand.h openssl/rsa.h openssl/sha.h openssl/pem.h openssl/x509.h])

AC_CHECK_LIB([crypto],[EVP_get_digestbyname],,
	AC_MSG_ERROR([OpenSSL developer library 'libssl-dev' or 'openssl-devel' not installed; cannot continue.]))

AC_CHECK_LIB([md],[MD5])		# if libmd is available, get it

AC_CHECK_FUNCS([MD5 SHA1 AES_encrypt RAND_bytes des_read_pw_string EVP_read_pw_string EVP_MD_size])
AC_CHECK_FUNCS([PEM_read_bio_RSA_PUBKEY])

################################################################
## FUSE: Filesystem in Userspace
AC_ARG_ENABLE(fuse,
  AC_HELP_STRING([--enable-fuse], [Support for FUSE, Filesystem in Userspace. @<:@default=check@:>@]),
    [enable_fuse=$enableval], [enable_fuse=check])

have_fuse=no
AS_IF([test "x$enable_fuse" != xno],
  [CPPFLAGS="-D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26 $CPPFLAGS"
   AC_CHECK_HEADER([fuse.h],
    [AC_CHECK_LIB([fuse], [fuse_version],
      [FUSE_LIBS=-lfuse
       have_fuse=yes])])
   AS_IF([test `uname -s` = Darwin && test "x$have_fuse" != "xyes"],
    [m4_ifdef([PKG_CHECK_MODULES],
      [PKG_CHECK_MODULES([FUSE], [fuse],
        [have_fuse=yes],
        [:])])])
   AS_IF([test "x$enable_fuse" != xcheck && test "x$have_fuse" = xno],
    [AC_MSG_FAILURE([--enable-fuse was given but test for fuse failed])])])
enable_fuse=$have_fuse

AFFUSE_BIN=
AS_IF([test "x${enable_fuse}" = "xyes"],
  [AC_SUBST(FUSE_LIBS)
   AC_SUBST(FUSE_CFLAGS)
   AC_DEFINE([USE_FUSE],1,[Use FUSE to mount AFF images])
   AFFUSE_BIN='affuse$(EXEEXT)'])
AC_SUBST(AFFUSE_BIN)
AM_PROG_CC_C_O			dnl for affuse
##
################################################################

############## drop optimization flags if requeted ################

if test x"${AFF_NOOPT}" != "x" ; then
   with_noopt="yes";
fi

if test "${with_noopt}" = "yes" ; then
  CFLAGS=`echo "$CFLAGS" | sed s/-O[[0-9]]//`             # note the double quoting!
  CXXFLAGS=`echo "$CXXFLAGS" | sed s/-O[[0-9]]//`
fi

# I am a glutten for punishment and this is security-critical software
CFLAGS="$CFLAGS -D_FORTIFY_SOURCE=2 -Wall -g"
CXXFLAGS="$CXXFLAGS -D_FORTIFY_SOURCE=2 -Wall -g"


################ drop threading if requested ################
AC_ARG_ENABLE(threading,
  [  --disable-threading    builds library without threading
   --enable-threading   use threading if available])
if test "x$disable_threading" != xno; then
  PTHREAD_CFLAGS=""
  PTHREAD_CXXFLAGS=""
  PTHREAD_LDFLAGS=""
  AC_DEFINE(DISABLE_PTHREAD,1,[User has disabled PTHREADING])
fi
AM_CONDITIONAL([THREADING], [test "x$enable_threading" != xno])

###### Tell user what flags we have #######
# see http://pre.plt-scheme.org/plt/src/mzscheme/configure.ac
#
AC_MSG_NOTICE([])
AC_MSG_NOTICE([*****************************************])
AC_MSG_NOTICE([AFFLIB ${PACKAGE_VERSION} configuration])
AC_MSG_NOTICE([Amazon S3 Support:  ${enable_s3}])
AC_MSG_NOTICE([LZMA Compression:   yes])
AC_MSG_NOTICE([PYTHON Bindings:    ${enable_python}])
AC_MSG_NOTICE([QEMU Image Drivers: ${enable_qemu}])
AC_MSG_NOTICE([FUSE:               ${enable_fuse}])
AC_MSG_NOTICE([LIBEXPAT:           ${have_expat} (needed for AFF signatures)])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([CFLAGS:     ${CFLAGS}])
AC_MSG_NOTICE([CPPFLAGS:   ${CPPFLAGS}])
AC_MSG_NOTICE([CXXFLAGS:   ${CXXFLAGS}])
AC_MSG_NOTICE([LIBS:       ${LIBS}])
AC_MSG_NOTICE([LDFLAGS:    ${LDFLAGS}])
AC_MSG_NOTICE([*****************************************])
AC_MSG_NOTICE([])

# AC_PROG_RANLIB not needed if you are using AC_PROG_LIBTOOL
# AC_PROG_RANLIB
AC_OUTPUT([afflib.pc])


