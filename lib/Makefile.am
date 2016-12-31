EXTRA_PROGRAMS = aftest
TESTS = aftest 

CLEANFILES = aftest
DISTCLEANFILES = Makefile.in

AUTOMAKE_OPTIONS = subdir-objects

aftest_SOURCES = aftest.cpp
aftest_LDADD = libafflib.la  
aftest_LDFLAGS = $(PTHREAD_CFLAGS) -static # easier debugging

AFFLIB_SOURCES = aff_db.cpp aff_db.h aff_toc.cpp \
	afflib.cpp afflib.h afflib_i.h afflib_os.cpp \
	afflib_pages.cpp afflib_stream.cpp afflib_util.cpp \
	crypto.cpp \
	base64.cpp base64.h lzma_glue.cpp s3_glue.h \
	vnode_aff.cpp vnode_aff.h \
	vnode_afd.cpp vnode_afd.h \
	vnode_afm.cpp vnode_afm.h \
	vnode_raw.cpp vnode_raw.h \
	vnode_split_raw.cpp vnode_split_raw.h \
	aftimer.h \
	utils.cpp utils.h display.cpp

libafflib_la_SOURCES = $(AFFLIB_SOURCES) 

QEMU_SOURCES = \
	vnode_qemu.cpp \
	vnode_qemu.h \
	qemu/aes.h \
	qemu/aff-block-vmdk.h \
	qemu/block-bochs.c \
	qemu/block-cloop.c \
	qemu/block-cow.c \
	qemu/block-dmg.c \
	qemu/block-parallels.c \
	qemu/block-qcow.c \
	qemu/block-qcow2.c \
	qemu/block-raw-posix.c \
	qemu/block-vmdk.c \
	qemu/block-vpc.c \
	qemu/block-vvfat.c \
	qemu/block.c \
	qemu/block.h \
	qemu/block_int.h \
	qemu/bswap.h \
	qemu/config-host.h \
	qemu/console.h \
	qemu/exec-all.h \
	qemu/osdep.h \
	qemu/qemu-common.h \
	qemu/qemu-timer.h \
	qemu/qemu_glue.c 

if MAYBE_QEMU
   libafflib_la_SOURCES += $(QEMU_SOURCES)
   libafflib_la_CFLAGS = -DQEMU_IMG
   aftest_CFLAGS  = -DQEMU_IMG 
endif

if MAYBE_S3
   EXTRA_PROGRAMS += s3
   libafflib_la_SOURCES +=   vnode_s3.cpp vnode_s3.h s3_glue.cpp 
   s3_SOURCES = s3.cpp
   s3_LDADD = libafflib.la  
   s3_LDFLAGS = $(PTHREAD_CFLAGS)
endif


install-exec-hook:
	@if [ -r $(DESTDIR)/etc/ld.so.conf ] ; then \
	  echo "*************************************************" ;\
	  echo "*** Checking shared library config for $(libdir) " ;\
	  if grep ^$(libdir) $(DESTDIR)/etc/ld.so.conf >/dev/null ; \
             then echo $(libdir) already installed ; \
             else echo installing $(libdir) in $(DESTDIR)/etc/ld.so.conf ; \
                  echo $(libdir) >> $(DESTDIR)/etc/ld.so.conf ; \
		  PATH=$(PATH):/sbin; \
		  ldconfig; \
          fi ; \
	  echo "*************************************************" ;\
	fi

EXTRA_DIST = 

AM_CPPFLAGS =  \
	-I@top_srcdir@/lzma443/C  \
	-I@top_srcdir@/lzma443/C/7zip/Compress/LZMA_Alone  \
	-I@top_srcdir@/include/afflib

LZMA_SOURCES =  \
	@top_srcdir@/lzma443/C/7zip/Compress/LZMA_Alone/LzmaBench.cpp \
	@top_srcdir@/lzma443/C/7zip/Compress/LZMA_Alone/LzmaRam.cpp \
	@top_srcdir@/lzma443/C/7zip/Compress/LZMA_Alone/LzmaRamDecode.c \
	@top_srcdir@/lzma443/C/7zip/Compress/LZMA_C/LzmaDecode.c \
	@top_srcdir@/lzma443/C/7zip/Compress/Branch/BranchX86.c \
	@top_srcdir@/lzma443/C/7zip/Compress/LZMA/LZMADecoder.cpp \
	@top_srcdir@/lzma443/C/7zip/Compress/LZMA/LZMAEncoder.cpp \
	@top_srcdir@/lzma443/C/7zip/Compress/LZ/LZInWindow.cpp \
	@top_srcdir@/lzma443/C/7zip/Compress/LZ/LZOutWindow.cpp \
	@top_srcdir@/lzma443/C/7zip/Compress/RangeCoder/RangeCoderBit.cpp \
	@top_srcdir@/lzma443/C/7zip/Common/InBuffer.cpp \
	@top_srcdir@/lzma443/C/7zip/Common/OutBuffer.cpp \
	@top_srcdir@/lzma443/C/7zip/Common/StreamUtils.cpp \
	@top_srcdir@/lzma443/C/Common/Alloc.cpp \
	@top_srcdir@/lzma443/C/Common/CommandLineParser.cpp \
	@top_srcdir@/lzma443/C/Common/CRC.cpp \
	@top_srcdir@/lzma443/C/Common/String.cpp \
	@top_srcdir@/lzma443/C/Common/StringConvert.cpp \
	@top_srcdir@/lzma443/C/Common/StringToInt.cpp \
	@top_srcdir@/lzma443/C/Common/Vector.cpp 


# specify lib_ to force tools to be dynamically linked against the installed library. 
# specify noinst_LTLIBRARIES to have the tools be statically linked
# Unfortunately, when we specify lib_, things don't build. I can't figure it out.
lib_LTLIBRARIES = libafflib.la
libafflib_la_SOURCES += $(LZMA_SOURCES)
