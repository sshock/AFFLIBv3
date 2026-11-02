# --------------------------------------------------------------------------
# AX_OPENSSL_VERSION
#
# Detect the OpenSSL version in use, whether the deprecated low-level
# APIs (MD5_*, SHA1_*, AES_*) are still visible in the installed headers,
# and expose the results to the C code.
#
# Works with OpenSSL 1.0.2, 1.1.1, 3.x and 4.x. The probe compiles and
# runs a program that reads <openssl/opensslv.h>; when execution is not
# possible (e.g. cross-compiling), the version is reported as unknown.
#
# Defined macros:
#   OPENSSL_VERSION_MAJOR / OPENSSL_VERSION_MINOR / OPENSSL_VERSION_PATCH
#   HAVE_OPENSSL_1_1_1    (version >= 1.1.1 and < 3.0)
#   HAVE_OPENSSL3         (version >= 3.0.0)
#   HAVE_OPENSSL4         (version >= 4.0.0)
#   HAVE_OPENSSL_DEPRECATED_MD5_SHA1 (MD5_Init/SHA1_Init declarations visible)
#   HAVE_OPENSSL_DEPRECATED_AES      (AES_set_encrypt_key/AES_cbc_encrypt visible)
#
# Substituted variable:
#   OPENSSL_VERSION  (e.g. "3.0.13"; "unknown" when not determinable)
#
# Copyright (c) 2026 AFFLIB. Public domain / BSD-style, like the rest of AFFLIB.
# --------------------------------------------------------------------------
AC_DEFUN([AX_OPENSSL_VERSION],[

dnl -- Determine major/minor/patch by compiling and running a small probe --
AC_MSG_CHECKING([OpenSSL version])
AC_LANG_SAVE
AC_LANG([C])
probe_file=conftest.openssl_version
rm -f $probe_file
AC_RUN_IFELSE([AC_LANG_PROGRAM([[
#include <openssl/opensslv.h>
#include <stdio.h>
]], [[
  FILE *f = fopen("conftest.openssl_version","w");
  int maj, min, pat;
#ifdef OPENSSL_VERSION_MAJOR
  maj = OPENSSL_VERSION_MAJOR;
  min = OPENSSL_VERSION_MINOR;
  pat = OPENSSL_VERSION_PATCH;
#else
  /* older OpenSSL: decode the 0xMNN00PPSL layout of OPENSSL_VERSION_NUMBER */
  maj = (int)(OPENSSL_VERSION_NUMBER >> 28);
  min = (int)((OPENSSL_VERSION_NUMBER >> 20) & 0xFF);
  pat = (int)((OPENSSL_VERSION_NUMBER >> 12) & 0xF);
#endif
  if(!f) return 1;
  fprintf(f,"%d %d %d %s\n",maj,min,pat,OPENSSL_VERSION_TEXT);
  fclose(f);
  return 0;
]])],
[ax_openssl_version=`cat $probe_file 2>/dev/null`],
[ax_openssl_version=""])
rm -f $probe_file
AC_LANG_RESTORE

if test -n "$ax_openssl_version" ; then
    ax_openssl_major=`echo $ax_openssl_version | cut -d' ' -f1`
    ax_openssl_minor=`echo $ax_openssl_version | cut -d' ' -f2`
    ax_openssl_patch=`echo $ax_openssl_version | cut -d' ' -f3`
    ax_openssl_text=`echo $ax_openssl_version | sed -e 's/^[[0-9]][[0-9]]* [[0-9]][[0-9]]* [[0-9]][[0-9]]* //'`
    OPENSSL_VERSION=$ax_openssl_major.$ax_openssl_minor.$ax_openssl_patch
    AC_MSG_RESULT([$OPENSSL_VERSION (${ax_openssl_text})])
    AC_DEFINE_UNQUOTED([OPENSSL_VERSION_MAJOR],[$ax_openssl_major],[OpenSSL major version])
    AC_DEFINE_UNQUOTED([OPENSSL_VERSION_MINOR],[$ax_openssl_minor],[OpenSSL minor version])
    AC_DEFINE_UNQUOTED([OPENSSL_VERSION_PATCH],[$ax_openssl_patch],[OpenSSL patch version])
    if test x"$ax_openssl_major.$ax_openssl_minor" = x"1.1" && test $ax_openssl_patch -ge 1 ; then
	AC_DEFINE([HAVE_OPENSSL_1_1_1],1,[Defined if OpenSSL 1.1.1 or newer (but < 3.0)])
    fi
    if test $ax_openssl_major -ge 3 ; then
	AC_DEFINE([HAVE_OPENSSL3],1,[Defined if OpenSSL 3.0 or newer])
	if test $ax_openssl_major -ge 4 ; then
	    AC_DEFINE([HAVE_OPENSSL4],1,[Defined if OpenSSL 4.0 or newer])
	fi
    fi
else
    OPENSSL_VERSION=unknown
    AC_MSG_RESULT([unknown (could not run the version probe)])
fi
AC_SUBST([OPENSSL_VERSION])

dnl -- Check whether the deprecated low-level APIs are still visible ----
dnl (i.e. the headers were not built/compiled with OPENSSL_NO_DEPRECATED_3_0)
dnl Take the address of the functions -- never call them.

AC_LANG_SAVE
AC_LANG([C])

AC_MSG_CHECKING([whether the deprecated OpenSSL MD5/SHA1 APIs are visible])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <openssl/md5.h>
#include <openssl/sha.h>
]], [[
void (*ax_probe_md5)(MD5_CTX *) = MD5_Init;
void (*ax_probe_sha1)(SHA_CTX *) = SHA1_Init;
ax_probe_md5 = 0;
ax_probe_sha1 = 0;
]])],[
    AC_DEFINE([HAVE_OPENSSL_DEPRECATED_MD5_SHA1],1,[Deprecated MD5_Init/SHA1_Init declarations are visible])
    AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no (hidden by OPENSSL_NO_DEPRECATED_3_0)])])

AC_MSG_CHECKING([whether the deprecated OpenSSL AES APIs are visible])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <openssl/aes.h>
]], [[
int (*ax_probe_aes_set)(const unsigned char *, int, AES_KEY *) = AES_set_encrypt_key;
void (*ax_probe_aes_cbc)(const unsigned char *, unsigned char *, size_t,
                         const AES_KEY *, unsigned char *, int) = AES_cbc_encrypt;
ax_probe_aes_set = 0;
ax_probe_aes_cbc = 0;
]])],[
    AC_DEFINE([HAVE_OPENSSL_DEPRECATED_AES],1,[Deprecated AES_set_encrypt_key/AES_cbc_encrypt declarations are visible])
    AC_MSG_RESULT([yes])],
[AC_MSG_RESULT([no (hidden by OPENSSL_NO_DEPRECATED_3_0)])])

AC_LANG_RESTORE
])dnl AX_OPENSSL_VERSION
