# Distributed under the Berekeley 4-part license

DISTCLEANFILES = \
	Makefile.in aclocal.m4 affconfig.h.in config.guess config.sub configure depcomp \
	install-sh ltmain.sh missing test-driver \
	m4/libtool.m4 m4/ltoptions.m4 m4/ltsugar.m4 m4/ltversion.m4 m4/lt~obsolete.m4

SUBDIRS = lib tools lzma443 tests doc pyaff man

pkgconfigdir = $(libdir)/pkgconfig

pkgconfig_DATA = afflib.pc

include_HEADERS = 
pkginclude_HEADERS = include/afflib/afflib.h include/afflib/afflib_i.h include/afflib/aftimer.h include/afflib/utils.h  

ACLOCAL_AMFLAGS = -I m4

AM_CFLAGS = -Wall
AM_CPPFLAGS = -Wall

DOCS = BUGLIST.txt README_Linux.txt README_Win32.txt \
	doc/crypto_doc.txt \
	doc/crypto_design.txt \
	doc/announce_1.0.txt \
	doc/announce_1.7.txt \
	doc/announce_1.8.txt \
	doc/announce_2.2.txt \
	doc/announce_3.0.txt \
	doc/announce_3.3.5.txt \
	doc/announce_3.3.txt \
	doc/announce_3.7.txt \
	doc/announce_3.7.5.txt

EXTRA_DIST = $(DOCS) \
	bootstrap.sh \
	make_mingw.sh \
	m4/acx_pthread.m4 \
	afflib.spec.in \
	afflib.spec \
	afflib.pc.in \
	afflib.pubkey.asc \
	tests/encrypted.raw \
	tests/encrypted.aff \
	win32/Changes.txt \
	win32/README_MSVC++.txt \
	win32/affconfig.h \
	win32/afflib.mak \
	win32/make.bat

#
# Note: don't forget to run autoreconf when significant changes are made
#
