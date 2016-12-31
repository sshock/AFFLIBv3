bin_PROGRAMS = affcat affcompare affconvert affcopy affcrypto affix affinfo affsegment \
	affstats affverify affxml affuse affrecover affsign affdiskprint

DISTCLEANFILES = Makefile.in

EXTRA_DIST = test_make_random_iso.sh test_crypto.sh test_signing.sh test_recovery.sh \
	test_passphrase.sh test_afsegment.sh 

TESTS = test_signing.sh test_recovery.sh test_passphrase.sh test_afsegment.sh test_crypto.sh

# See http://www.gnu.org/software/libtool/manual.html		# Using-Automake

affcat_SOURCES = affcat.cpp 
affcrypto_SOURCES = affcrypto.cpp
affcompare_SOURCES = affcompare.cpp  unix4win32.h
affconvert_SOURCES = affconvert.cpp  unix4win32.h
affcopy_SOURCES = affcopy.cpp unix4win32.h aff_bom.h aff_bom.cpp
affdiskprint_SOURCES = affdiskprint.cpp unix4win32.h hashextent.h
affix_SOURCES = affix.cpp unix4win32.h
affuse_SOURCES = affuse.c
affinfo_SOURCES = affinfo.cpp unix4win32.h
affrecover_SOURCES = affrecover.cpp unix4win32.h
affsegment_SOURCES = affsegment.cpp
affsign_SOURCES = affsign.cpp aff_bom.h aff_bom.cpp
affstats_SOURCES = affstats.cpp
affverify_SOURCES = affverify.cpp aff_bom.h aff_bom.cpp
affxml_SOURCES = affxml.cpp unix4win32.h

AM_CPPFLAGS = -I@top_srcdir@/lib -I@top_srcdir@/include/afflib
LDADD = @top_builddir@/lib/libafflib.la 

affuse_CFLAGS = @FUSE_CFLAGS@
affuse_LDADD = @top_builddir@/lib/libafflib.la @FUSE_LIBS@ 
affuse_LINK = $(CXXLINK)

CLEANFILES = deskclerk.pem test_password.sh blank.iso archives.pem password_signed.aff signed.aff \
	random.iso agent.pem r3.iso random*.aff blank.aff blanke.aff words r3.iso evidence1.aff
