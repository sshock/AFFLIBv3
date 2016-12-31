/*
 * afflib_i.h:
 * The "master include file" of the AFF Library.
 * Includes many fucntions that are not designed
 * to be used by application programmers.
 *
 *
 * Copyright (c) 2005-2006
 *	Simson L. Garfinkel and Basis Technology, Inc.
 *      All rights reserved.
 *
 * This code is derrived from software contributed by
 * Simson L. Garfinkel
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Simson L. Garfinkel
 *    and Basis Technology Corp.
 * 4. Neither the name of Simson Garfinkel, Basis Technology, or other
 *    contributors to this program may be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY SIMSON GARFINKEL, BASIS TECHNOLOGY,
 * AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL SIMSON GARFINKEL, BAIS TECHNOLOGy,
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef AFFLIB_I_H
#define AFFLIB_I_H

#ifdef KERNEL_LIBRARY
#ifdef __cplusplus
extern "C" {
#endif
void __cdecl AFDbgPrint (PCSTR Format,...);
#ifdef __cplusplus
}
#endif
#endif


/* Should we disable threading? */
#ifdef DISABLE_PTHREAD
#undef HAVE_PTHREAD
#endif

/* Standard includes */
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_ZLIB_H
#include <zlib.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif

#ifdef HAVE_ERR_H
#include <err.h>
#endif

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#ifdef HAVE_LIBCRYPTO
#include <openssl/aes.h>
#include <openssl/rsa.h>		// a standard part of OpenSSL
#include <openssl/rand.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#endif

#ifdef HAVE_PTHREAD
#include <pthread.h>
#define AF_READLOCK(af) pthread_rwlock_rdlock(&af->rwlock);
#define AF_WRLOCK(af) pthread_rwlock_wrlock(&af->rwlock);
#define AF_UNLOCK(af) pthread_rwlock_unlock(&af->rwlock);
#else
/* No threads */
#define AF_READLOCK(af) {}
#define AF_WRLOCK(af)   {}
#define AF_UNLOCK(af)   {}
#endif

#ifdef WIN32
#if !defined(HAVE__MINGW_H)
#pragma warning(disable: 4996)  /* Don't warn on Windows about using POSIX open() instead of _open() */
#endif
#include <malloc.h>
#include <windows.h>
#include <winsock.h>			// htonl()
#include <direct.h>
#define snprintf _snprintf
#define strcasecmp _stricmp
#define mkdir(path,mode) _mkdir(path)
#define random() rand()
#define access _access
#define strdup _strdup

#ifndef ENOTSUP
#define ENOTSUP 65536		/* made up number */
#endif

#ifndef _MODE_T_
#define _MODE_T_
typedef unsigned short mode_t;
typedef unsigned short _mode_t;
#endif

#ifndef S_ISDIR
#define S_ISDIR(m)(((m) & 0170000) == 0040000)
#endif

#if !defined(HAVE__MINGW_H)
#define ftruncate(fd,size) _chsize_s(fd,size)
#define MAXPATHLEN 1024
#endif

#if defined(HAVE__MINGW_H)
#ifndef ftello
#define ftello ftello64
#endif

#ifndef fseeko
#define fseeko fseeko64
#endif

#else
#define ftello _ftelli64			/* replaces ftello64 in VC2008 */
#define fseeko _fseeki64
#endif

#endif
/** END OF WIN32 DEFINES **/

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* Pick an appropriate POINTER_FMT; isn't there an ANSI standard for this? */
#ifdef __APPLE__
#define POINTER_FMT "%p"
#endif

#ifdef linux
#define POINTER_FMT "%p"
#endif

#ifndef POINTER_FMT
#define POINTER_FMT "%x"		// guess
#endif

/* Handle systems that are missing some #defines */

#ifndef O_BINARY
#define O_BINARY 0			// for Windows compatability
#endif

#ifndef ENOTSUP
#define ENOTSUP EOPNOTSUPP
#endif

#ifndef O_ACCMODE
#define O_ACCMODE 0x0003
#endif

/* If these functions do not exist, we need to create our own */

#ifndef HAVE_ERR
void err(int eval, const char *fmt, ...);
#endif

#ifndef HAVE_ERRX
void errx(int eval, const char *fmt, ...);
#endif

#ifndef HAVE_WARN
void	warn(const char *fmt, ...);
#endif

#ifndef HAVE_WARNX
void	warnx(const char *fmt, ...);
#endif

/* access function */
#ifndef F_OK
#define F_OK            0       /* test for existence of file */
#endif

#ifndef X_OK
#define X_OK            0x01    /* test for execute or search permission */
#endif

#ifndef W_OK
#define W_OK            0x02    /* test for write permission */
#endif

#ifndef R_OK
#define R_OK            0x04    /* test for read permission */
#endif


#if defined(WIN32) && !defined(HAVE__MINGW_H)
/****************************************************************
 *** Windows emulation of opendir()/readdir()
 *** From php
 ****************************************************************/

/* struct dirent - same as Unix */

struct dirent {
	long  d_ino;		       /* inode (always 1 in WIN32) */
	off_t d_off;			/* offset to this dirent */
	int   d_reclen;	    /* length of d_name; was unsigned short */
	char  d_name[_MAX_FNAME + 1];	/* filename (null terminated) */
};


/* typedef DIR - not the same as Unix */
typedef struct {
	long handle;				/* _findfirst/_findnext handle */
	short offset;				/* offset into directory */
	short finished;				/* 1 if there are not more files */
	struct _finddata_t fileinfo;	/* from _findfirst/_findnext */
	char *dir;					/* the dir we are reading */
	struct dirent dent;			/* the dirent to return */
} DIR;

/* Function prototypes */


#ifdef __cplusplus
extern "C" {
#endif

DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int readdir_r(DIR *, struct dirent *, struct dirent **);
int closedir(DIR *);
int rewinddir(DIR *);


#ifdef __cplusplus
}
#endif
#endif


/****************************************************************
 *** AFFLIB internal stuff follows.
 ****************************************************************/

#ifdef __cplusplus
extern "C" {
#endif
#ifdef NEVER_DEFINED
}
#endif

#if defined(HAVE_LIBEXPAT)
#define USE_AFFSIGS
#endif

struct _AFFILE {
    int     version;			// 2
    void   *tag;			// available to callers; unused by AFFLIB

    struct af_vnode *v;			// which function table to use.
    struct _AFFILE *parent;		// for AFF file inside an AFD

    /* For all files */
    int     openflags;			// how it was opened
    int     openmode;			// how we were asked to open it; more
    int     exists;			// did file exist before open was called?

    /* From URLs */
    char    *fname;			// Filename of file; be sure to free when done
    char    *protocol;			// could be "file" or "s3"
    char    *username;			// optionally specified in URL
    char    *password;			// from URL; erase after use.
    char    *hostname;			// from URL
    int     port;			// from URL

    /* Extended Logging */
    char    error_str[64];		// what went wrong

    /* Implement a stream abstraction */
    uint64_t    image_size;		// last mappable byte of disk image
    uint64_t    image_size_in_file;	// see if it was changed...
    uint32_t	image_pagesize;	// the size of image data segments in this file
    uint32_t	image_sectorsize;
    uint64_t	pos;			// location in stream; should be signed because of comparisons

    /* Page buffer cache */
    struct aff_pagebuf *pb;		// the current page buffer
    struct aff_pagebuf *pbcache;	// array of pagebufs
    int		num_pbufs;	   // number of pagebufs; default is 1
    int		afftime;		// for updating last
    int64_t	cur_page;		// used by vnode_raw to fake pages must be able to go negative.

    int		  debug;		// for debugging, of course
    unsigned int  badflag_set:1;	// is badflag set?
    unsigned char *badflag;		// bad sector flag


    /****************************************************************/
    /* Right now the instance variables for each implementation are here,
     * which is ugly but easier for development...
     */

    /* For AFF Segment Files; this could be moved into private storage... */
    FILE          *aseg;
    struct aff_toc_mem *toc;		// table of contents
    int	           toc_count;	       // number of directory elements

    /****************************************************************/

    unsigned int write_md5:1;		// automatically write the MD5 for each page
    unsigned int write_sha1:1;
    unsigned int write_sha256:1;


    /* These are for optimizing updates; really this should go away and we should just
     * exmaine the TOC to find a hole, but currently we don't do that.
     */
    unsigned int direction_changes;     // how many times have we changed directions?
    int          last_direction;	// should be 1 or -1
    unsigned int random_access:1;	// are we in random access mode?

    /* additional support for writing. */
    unsigned int compression_type;	// preferred compression type
    int		 compression_level;	// 0 is no compression


    /* w_callback:
     * A callback that is called before and after each segment is written.
     * Called with the arguments (i,0,0) at the beginning of the write operation.
     * Called with the arguments (i,j,k) at the end of the write operation.
     * i = segment number
     * j = segment length
     * If segment is being written with compresison, k = compressed length.
     * If segment is written w/o compression, k = 0
     */
    void (*w_callback)(struct affcallback_info *acbi);
    // called at start and end of compression.

    uint64_t	maxsize;		// maximum file size of a multi-segment files,
                                        // or 0 if this is not a multi-segment file

    /* Performance Counters */
    uint64_t	bytes_memcpy;		// total number of bytes memcpy'ed
    uint64_t    pages_written;		// total number of pages written
    uint64_t    pages_compressed;	// total number of pages compressed
    uint64_t	pages_decompressed;
    uint64_t    pages_read;		// total number of pages read
    uint64_t	bytes_written;
    uint64_t	cache_hits;
    uint64_t	cache_misses;		// total number of pages flushed from cache

    void	*vnodeprivate;	      // private storage for the vnode
    void	(*error_reporter)(const char *fmt, ...);
    struct af_crypto *crypto;
#ifdef HAVE_PTHREAD
    pthread_rwlock_t rwlock;		// automatically created and destroyed if pthread exists
#endif
    struct af_vnode_info *vni_cache;	// vstat cache
};



/* af_crypto:
 * copy of AES encrypt and decrypt keys.
 */
void af_crypto_allocate(AFFILE *af);
void af_crypto_deallocate(AFFILE *af);

struct af_crypto {
    uint32_t sealing_key_set:1;		// encryption key has been set
    uint32_t auto_encrypt:1;		// encrypt segments when we write
    uint32_t auto_decrypt:1;		// automatically decrypto when we read
#ifdef AES_BLOCK_SIZE
    AES_KEY	ekey;			// encrypt key
    AES_KEY	dkey;			// decrypt key
#endif
#ifdef HAVE_OPENSSL_EVP_H
    EVP_PKEY	*sign_privkey;		// signing private key (to write signatures)
    EVP_PKEY	*sign_pubkey;		// signing public key (to verify signatures)
    X509	*sign_cert;		// signing certificate (for verifying signatures)
    /* Sealing is kept locally and immediately turned into a dkey & ekey */
#endif
};


/* The AFF STREAM VNODE */
struct af_vnode {
    int type;				// numeric vnode type
    int flag;				// file system flag type
    const char *name;
    int (*identify)(const char *fname,int exists);	// returns 1 if file system is identified by implementation;
    int (*open)(AFFILE *af);
    int (*close)(AFFILE *af);
    int (*vstat)(AFFILE *af,struct af_vnode_info *);	// returns info about the vnode image file
    int (*get_seg)(AFFILE *af,const char *name,uint32_t *arg, uint8_t *data,size_t *datalen);
    int	(*get_next_seg)(AFFILE *af,char *segname,size_t segname_len,
			uint32_t *arg, uint8_t *data, size_t *datalen);
    int (*rewind_seg)(AFFILE *af);
    int (*update_seg)(AFFILE *af,const char *name,uint32_t arg,
		      const uint8_t *value,uint32_t vallen);
    int (*del_seg)(AFFILE *af,const char *name);
    int (*read)(AFFILE *af,uint8_t *buf,uint64_t offset,size_t count);
    int (*write)(AFFILE *af,uint8_t *buf,uint64_t offset,size_t count);
};

/* VNODE Flags */
#define AF_VNODE_TYPE_PRIMITIVE 0x01	// single-file implementation
#define AF_VNODE_TYPE_COMPOUND  0x02	// multi-file implementation
#define AF_VNODE_TYPE_RELIABLE  0x04	// writes are reliable; no need to verify them.
#define AF_VNODE_MAXSIZE_MULTIPLE 0x08  // maxsize must be multiple of pagesize (for AFM and splitraw)
#define AF_VNODE_NO_SIGNING     0x10	// vnode does not support signing (like raw)
#define AF_VNODE_NO_SEALING     0x20	// vnode does not support sealing (like raw and afd)

#define AF_SEALING_VNODE(af) (!(af->v->flag & AF_VNODE_NO_SEALING))
#define AF_SIGNING_VNODE(af) (!(af->v->flag & AF_VNODE_NO_SIGNING))
#define AF_SEALING(af) AF_SEALING_VNODE(af) && af->crypto && af->crypto->sealing_key_set


/* The header for an AFF file. All binary numbers are stored in network byte order. */
#define AF_HEADER "AFF10\r\n\000"
struct af_head {
    char header[8];			// "AFF10\r\n\000"
    /* segments follow */
};


/* The header of each segment */
#define AF_SEGHEAD "AFF\000"
struct af_segment_head {
    char magic[4];			// "AFF\000"
    uint32_t name_len:32;		// length of segment name
    uint32_t data_len:32;		// length of segment data, if any
    uint32_t flag:32;		// argument for name;
    /* name follows, then data */
};

/* The tail of each segment */
#define AF_SEGTAIL "ATT\000"
struct af_segment_tail {
    char magic[4];			// "ATT\000"
    uint32_t segment_len:32;      // includes head, tail, name & length
};


/* How 64-bit values are stored in a segment */
#pragma pack(1)
struct aff_quad {
    uint32_t low:32;
    uint32_t high:32;
};
#pragma pack()


/* As it is kept in memory */
struct aff_toc_mem {
    char *name;			        // name of this directory entry
    uint64_t offset;			// offset from beginning of file.
    uint64_t segment_len;		// includes head, tail, name & length
};

/* How encryption keys are stored */
struct affkey {
    uint8_t version[4];
    uint8_t affkey_aes256[32]; // AFF key encrypted with SHA-256 of passphrase
                              // encrypted as two codebooks in a row; no need for CBC
    uint8_t zeros_aes256[16];  // all zeros encrypted with SHA-256 of passphrase
};
#define AFFKEY_SIZE 4+32+16


void af_initialize();			// initialize the AFFLIB
                                        // automatically called by af_open()

/* Internal identification routines */
int af_identify_file_type(const char *filename,int exists); // returns type of a file; if exists=1, file must exist
const char *af_identify_file_name(const char *filename,int exists); // returns name of a file type;
int split_raw_increment_fname (char *fn); /* exposed for testing in aftest */

AFFILE *af_open_with(const char *filename,int flags,int mode, struct af_vnode *v);
extern struct af_vnode *af_vnode_array[]; // array of filesystems; last is a "0"

int	af_last_seg(AFFILE *af,char *last_segname,int last_segname_len,int64_t *pos);
int	af_make_badflag(AFFILE *af);	// creates a badflag and puts it
int	af_make_gid(AFFILE *af);	// created an AF_IMAGE_GID if it doesn't exist
extern  char af_error_str[64];


#define AFF_DEFAULT_PAGESIZE (1024*1024*16)


/* afflib_os.cpp:
 * Operating-system specific code.
 */

/* af_figure_media:
 * Returns information about the media in a structure.
 * Returns 0 if successful, -1 if error.
 */

struct af_figure_media_buf {
    int version;
    int sector_size;
    uint64_t total_sectors;
    uint64_t max_read_blocks;		// was previously 4-bytes; must be 8!
};
int	af_figure_media(int fd,struct af_figure_media_buf *);

/****************************************************************
 *** Lowest-level routines for manipulating the AFF File...
 ****************************************************************/

/* Navigating within the AFFILE */
/* probe the next segment.
 * Returns: 0 if success
 *          -1 if error
 *          -2 if segname_len was not large enough to hold segname
 *         - segname - the name of the next segment.
 *         - segsize - number of bytes the entire segment is.
 *
 * doesn't change af->aseg pointer if do_rewind is true, otherwise leaves stream
 *           positioned ready to read the data
 */

int	af_probe_next_seg(AFFILE *af,char *segname,size_t segname_len,
			   uint32_t *arg,size_t *datasize, size_t *segsize,int do_rewind);
int	af_backspace(AFFILE *af);	// back up one segment



/****************************************************************
 *** Reading functions
 ****************************************************************/


/* Support for data pages. This is what the stream system is built upon.
 * Note: pagename to string translation happens inside afflib.cpp, not inside
 * the vnode driver.
 */
int	af_page_size(AFFILE *af);	// legacy (now is af_get_pagesize)
void	af_read_sizes(AFFILE *af);	// sets up values if we can get them.
int	af_set_pagesize(AFFILE *af,uint32_t pagesize); // sets the pagesize; fails with -1 if imagesize >=0
int	af_set_sectorsize(AFFILE *AF,int sectorsize); // fails with -1 if imagesize>=0
int	af_get_sectorsize(AFFILE *AF);	// returns sector size
int	af_has_pages(AFFILE *af);	// does the underlying system support pages?
int	af_get_pagesize(AFFILE *af);	// returns page size, or -1
int	af_get_page_raw(AFFILE *af,int64_t pagenum,uint32_t *arg,uint8_t *data,size_t *bytes);
int	af_get_page(AFFILE *af,int64_t pagenum,uint8_t *data,size_t *bytes);
#define AF_SIGFLAG_NOSIG 0x0001	// do not write signatures with af_update_segf()
#define AF_SIGFLAG_NOSEAL 0x0002	// do not encrypt an af_update_segf()

/****************************************************************
 *** Writing functions
 ****************************************************************/

extern  int af_cache_debug;			 // sets level of verbosity */
int	af_update_page(AFFILE *af,int64_t pagenum,uint8_t *data,int datalen);
int	af_update_segf(AFFILE *af,const char *name,
		       uint32_t arg,const uint8_t *value,uint32_t vallen,uint32_t sigflag);

void	af_invalidate_vni_cache(AFFILE *af);
void	af_cache_writethrough(AFFILE *af,int64_t pagenum,
			      const uint8_t *buf,int bufflen);
int	af_cache_flush(AFFILE *af);		// write buffers to disk
struct aff_pagebuf *af_cache_alloc(AFFILE *af,int64_t pagenum);


/****************************************************************
 ***/

/* afflib_util.cpp
 */
uint64_t   af_decode_q(uint8_t buf[8]); // return buf[8] into an unsigned quad
const char *af_commas(char buf[64],int64_t val);
int       af_hasmeta(const char *buf);	// return 1 if buf has shell metacharacters
int	  af_is_filestream(const char *filename); // return 1 if file:// or filename
void      af_parse_url(const char *url,char **protocol,char **hostname,
		       char **username,char **password,int *port,char **path);

#ifndef HAVE_STRLCPY
size_t strlcpy(char *dest,const char *src,size_t dest_size);
#endif

#ifndef HAVE_STRLCAT
size_t strlcat(char *dest,const char *src,size_t dest_size);
#endif


/****************************************************************
 *** Table of Contents
 ****************************************************************/

/* Needs a rewrite for efficiency */

/* afflib_toc.cpp:
 * Table of contents management routines
 * Remember: all of these routines may fail, because the whole TOC may not
 * fit in memory...
 *
 * This is all experimental right now.
 */

int	aff_segment_overhead(const char *segname);
int	aff_toc_free(AFFILE *af);
void	aff_toc_print(AFFILE *af);
int	aff_toc_build(AFFILE *af);	// build by scanning the AFFILE
struct aff_toc_mem *aff_toc(AFFILE *af,const char *segname);
int	aff_toc_del(AFFILE *af,const char *segname);
void	aff_toc_update(AFFILE *af,const char *segname,uint64_t offset,uint64_t datalen);

struct aff_toc_mem *aff_toc_next_seg(AFFILE *af, uint64_t offset);
int aff_toc_find_hole(AFFILE *af, uint64_t min_size, uint64_t *offset, uint64_t *size);

/* lzma_glue.cpp:
 * For the LZMA compression engine
 */
int lzma_compress(uint8_t *dest,size_t *destLen, const uint8_t *data,size_t datalen,int level);
int lzma_uncompress(uint8_t *buf,size_t *buflen, const uint8_t *cbuf,size_t cbuf_size);

#ifdef NEVER_DEFINED
{
#endif
#ifdef __cplusplus
}
#endif
#endif


