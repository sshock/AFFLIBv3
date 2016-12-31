#ifndef _AFFLIB_H_
#define _AFFLIB_H_

/*
 * afflib.h:
 *
 * This file describes the public AFFLIB interface.
 * The interface to reading AFF files and  Raw files.
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

/* Figure out what kind of OS we are running on */

/* These are both needed; no need to bother with affconfig.h #defines */
#include <stdio.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef _MSC_VER
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
#endif
#endif
#include <sys/types.h>

#ifdef HAVE_SYS_CDEFS_H
#include <sys/cdefs.h>
#endif

#ifdef linux
/* Horrible lossage stuff for largefile support under Linux */
#define _LARGEFILE_SOURCE 1
#define _FILE_OFFSET_BITS 64
#endif

#ifdef HAVE_INTTYPES_H
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>
#else
#ifdef _MSC_VER
#define PRIu32 "I32u"
#endif
#endif

/** WIN32 is defined by the NMAKE makefile for Visual C++ under Windows and by mingw **/
#ifdef WIN32
#include <basetsd.h>
#include <io.h>				// gets isatty

/* These aren't needed for mingw */
#if !defined(__MINGW_H)
#ifndef _UINT64_T_DECLARED
typedef unsigned __int64 uint64_t;	/* 64-bit types Types */
#define _UINT64_T_DECLARED
#endif

#ifndef _INT64_T_DECLARED
typedef __int64 int64_t;
#define _INT64_T_DECLARED
#endif
#endif

#ifndef PRId64
#define PRId64 "I64d"
#endif

#ifndef PRIi64
#define PRIi64 "I64i"
#endif

#ifndef PRIu64
#define PRIu64 "I64u"
#endif

#endif
/** END OF WIN32 DEFINES **/

#define I64d PRIi64
#define I64u PRIu64

/* If our types still aren't defined, give some kind of error
 */
struct affcallback_info;
struct aff_pagebuf {
    int64_t       pagenum;		// -1 means no page loaded
    unsigned char *pagebuf;		// where the data is; size is image_pagesize
    size_t        pagebuf_bytes;        // number of bytes in the pagebuf that are valid.
    uint32_t  pagenum_valid:1;	// buffer contains data
    uint32_t  pagebuf_valid:1;	// buffer contains data
    uint32_t  pagebuf_dirty:1;	// data was modified
    int		  last;			// when the page was last visited
};

struct af_vnode_info {
    uint64_t imagesize;			// size of this image
    int   pagesize;			// what is the natural page size?
    uint32_t supports_compression:1; // supports writing compressed segments
    uint32_t has_pages:1;		 // does system support page segments?
    uint32_t supports_metadata:1;		// does it support metadata?
    uint32_t is_raw:1;			// file is raw
    uint32_t use_eof:1;			// should we use the EOF flag?
    uint32_t at_eof:1;			// are we at the EOF?
    uint32_t changable_pagesize:1;	// pagesize can be changed at any time
    uint32_t changable_sectorsize:1; // sectorsize can be changed at any time
    uint32_t cannot_decrypt:1; // encrypted pages cannot be decrypted becuase passphrase is invalid
    uint32_t segment_count_total;
    uint32_t page_count_total;
    uint32_t segment_count_signed;
    uint32_t segment_count_encrypted;
    uint32_t page_count_encrypted;
};					//


/* All of this stuff should be hidden inside a single private structure... */
typedef struct _AFFILE AFFILE;

/* The information that is provided in the aff callback */
struct affcallback_info {
    int info_version;			// version number for this segment
    AFFILE *af;				// v1: the AFFILE responsibile for the callback
    int phase;				// v1: 1 = before compress; 2 = after compressing;
					//     3 = before writing; 4 = after writing
    int64_t pagenum;			// v1: page number being written
    int bytes_to_write;			// v1: >0 if we are going to write bytes
    int bytes_written;			// v1: >0 if bytes were written
    int compressed;			// v1: >0 if bytes were/will be compressed
    int compression_alg;		// v1: compression algorithm
    int compression_level;		// v1: compression level
};

/* Utility Functions */

#ifdef __cplusplus
extern "C" {
#endif
#ifdef __never_defined__
}
#endif

/****************************************************************
 ***
 *** Intended user AFF interface
 ***
 ****************************************************************/

const char * af_version(void);		// returns AFF Version Number

/* af_file stream functions */
AFFILE *af_open(const char *filename,int flags,int mode);
AFFILE *af_freopen(FILE *file);		// reopen a raw file as an AFFILE
AFFILE *af_popen(const char *command,const char *type);	// no need to use pclose(); af_close() is fine
int	af_close(AFFILE *af);
void	af_set_error_reporter(AFFILE *af,void (*reporter)(const char *fmt,...));
void	af_stats(AFFILE *af,FILE *f);	// print stats to f
void	af_set_cachesize(AFFILE *af,int max); // how much memory can the cache use?
int     af_vstat(AFFILE *af,struct af_vnode_info *vni); // does the stat
void	af_perror(const char *fname);				// print the error string to stderr
void	af_err(int code,const char *fname,...);	// like err(), but will also print AFF-specific errors



/* Generic set/get option routines; this replaces individual options in previous implementations.
 * af==0 to set global options. Return the previous value.
 */
int	af_set_option(AFFILE *af,int option,int value);

#define AF_OPTION_AUTO_ENCRYPT     1	// 1 = auto-encrypt
#define AF_OPTION_AUTO_DECRYPT     2	// 1 = auto-decrypt
// The following are not implemented yet
#define AF_OPTION_PIECEWISE_MD5    3	// 1 = automatically write pagen_md5 segments
#define AF_OPTION_PIECEWISE_SHA1   4	// 1 = automatically write pagen_md5 segments
#define AF_OPTION_PIECEWISE_SHA256 5	// 1 = automatically write pagen_md5 segments
#define AF_OPTION_DISABLE_RDLOCK   6    // 1 = do not read lock, but report that it should have locked.


/* Special AFOPEN flags for af_open_with */
#define AF_OPEN_PRIMITIVE (1<<31)	// only open primtive, not compound files
#define AF_BADBLOCK_FILL  (1<<30)	// fill unallocated (sparse) with BADBLOCK flag
#define AF_HALF_OPEN      (1<<29)       // return af before calling af->v->open;
#define AF_NO_CRYPTO      (1<<28)       // disable encryption layer

/* navigating within the data segments as if they were a single file */
#ifdef _WIN32
SSIZE_T   af_read(AFFILE *af,unsigned char *buf,SSIZE_T count);
#else
ssize_t   af_read(AFFILE *af,unsigned char *buf,ssize_t count);
#endif
uint64_t  af_seek(AFFILE *af,int64_t pos,int whence); // returns new position
uint64_t  af_tell(AFFILE *af);
int	  af_eof(AFFILE *af);		// is the virtual file at the end?

/* Additional routines for writing */
int	af_set_maxsize(AFFILE *af,int64_t size); // sets maximum AFF file size
void	af_set_callback(AFFILE *af, void (*cb)(struct affcallback_info *acbi));
void	af_enable_compression(AFFILE *af,int type,int level); // set/gunset compression for writing
int	af_compression_type(AFFILE *af);
int	af_write(AFFILE *af,unsigned char *buf,size_t count);
const unsigned char *af_badflag(AFFILE *af); // return the pattern used to identify bad sectors
int	af_is_badsector(AFFILE *af,const unsigned char *buf); // 0 if not, 1 if it is, -1 if error


/* Misc. Functions */
const char *af_ext(const char *filename);	// return the extension of str including the dot
int	    af_ext_is(const char *filename,const char *ext);
const char *af_filename(AFFILE *af);	// returns the filename of an open stream.
int	    af_identify(AFFILE *af);	// returns type of AFFILE pointer

/* AFF implementation types returned by af_identify() and af_identify_file_type() */

#define AF_IDENTIFY_RAW 0		// file is a raw file
#define AF_IDENTIFY_AFF 1		// file is an AFF file
#define AF_IDENTIFY_AFD 2		// file is a directory of AFF files
#define AF_IDENTIFY_EVF 3		// file is an EnCase file
#define AF_IDENTIFY_EVD 4		// file is a .E01 file when there are more files following
#define AF_IDENTIFY_SPLIT_RAW 5		// file is a split raw file
#define AF_IDENTIFY_AFM 6               // file is raw file with metadata
#define AF_IDENTIFY_EWF 7		// libewf; deprecated
#define AF_IDENTIFY_S3  8		// is an s3:/// file
#define AF_IDENTIFY_VMDK 9		// QEMU support for VMDK format
#define AF_IDENTIFY_DMG 10		// QEMU support for Apple DMG format
#define AF_IDENTIFY_SPARSEIMAGE 11	// QEMU support for Apple SPARSEIMAGE format

#define AF_IDENTIFY_ERR -1		// error encountered on identify
#define AF_IDENTIFY_NOEXIST -2		// file does not exist


/* Accessor Functions */
int64_t af_get_imagesize(AFFILE *af);	// byte # of last mapped byte in image, or size of device;
					// returns -1 if error
int	af_get_pagesize(AFFILE *af);	// returns page size, or -1
int	af_set_acquisition_date(AFFILE *af,time_t t); // sets AF_ACQUISITION_DATE

#define af_imagesize(af) af_get_imagesize(af) // backwards compatiability
int	    af_get_segq(AFFILE *af,const char *name,int64_t *quad);/* Get/set 8-byte values */
int	    af_update_segq(AFFILE *af,const char *name,int64_t quad);


/****************************************************************
 * Functions for manipulating the AFFILE as if it were a name/value database.
 ****************************************************************/

/* get functions:
 * get the named segment.
 * If arg!=0, set *arg to be the segment's flag.
 * if data==0, don't return it.
 * if datalen && *datalen==0, return the size of the data segment.
 *** Returns 0 on success,
 *** -1 on end of file. (AF_ERROR_EOF)
 *** -2 if *data is not large enough to hold the segment (AF_ERROR_DATASMALL)
 *** -3 file is corrupt or other internal error. (AF_ERROR_CORRUPT)
 */

int	af_get_seg(AFFILE *af,const char *name,uint32_t *arg,
		   unsigned char *data,size_t *datalen);
int	af_get_next_seg(AFFILE *af,char *segname,size_t segname_len,
			uint32_t *arg, unsigned char *data, size_t *datalen);

int	af_rewind_seg(AFFILE *af); // rewind seg pointer to beginning

/*
 * af_update_seg() should be your primary routine for writing new values.
 */

/* Writing arbitrary name/value pairs */
int	af_update_seg(AFFILE *af,const char *segname,uint32_t arg,
		      const unsigned char *value,uint32_t vallen);
#ifdef HAVE_OPENSSL_BIO_H
/* Write a memory bio to a segment */
#include <openssl/bio.h>
int	af_update_seg_frombio(AFFILE *af,const char *segname,uint32_t arg,BIO *bio);
#endif


/* Delete functions */

int	af_del_seg(AFFILE *af,const char *name); // complete delete of first name
                                                 // returns 0 if success, -1 if seg not found

/* Segname parse functions.
 * af_segname_page_number:
 *   - Returns page number if segment name is a page #, and -1 if it is not
 * af_segname_hash_page_number:
 *   - Returns page number if segment name is a page hash, sets hash function
 *     to be the function used.
 */
int64_t	af_segname_page_number(const char *name); // return -1 if it is not a page number
int64_t	af_segname_hash_page_number(const char *name,char *hash,int hashlen); // return -1 if it is not a hash page #

int af_display_as_quad(const char *segname); // afflib recommends displaying this segment as an 8-byte quad
int af_display_as_hex(const char *segname); // afflib recommends displaying this segment as a hex-string

/****************************************************************/

/* Crypto */
/* AFF Base Encryption */
int  af_SHA256(const unsigned char *buf,size_t buflen,unsigned char md[32]); // return 0 if success, -1 if no cipher
int  af_set_aes_key(AFFILE *af,const unsigned char *userKey,const int bits);
int  af_cannot_decrypt(AFFILE *af);	// encrypted pages are present which cannot be decrypted
int  af_has_encrypted_segments(AFFILE *af);
int  af_is_encrypted_segment(const char *segname);

/* AFF Passphrase Encryption */
int  af_establish_aes_passphrase(AFFILE *af,const char *passphrase);
int  af_change_aes_passphrase(AFFILE *af,const char *oldphrase,const char *newphrase);
int  af_use_aes_passphrase(AFFILE *af,const char *passphrase);
int  af_save_aes_key_with_passphrase(AFFILE *af,const char *passphrase, const unsigned char affkey[32]);
int  af_get_aes_key_from_passphrase(AFFILE *af,const char *passphrase, unsigned char affkey[32]);


/* PKI Signing */
int  af_set_sign_files(AFFILE *af,const char *keyfile,const char *certfile);
int  af_sign_seg3(AFFILE *af,const char *segname, uint32_t arg,
		  const unsigned char *data,uint32_t datalen,uint32_t signmode);
int  af_sign_seg(AFFILE *af,const char *segname);
int  af_sign_all_unsigned_segments(AFFILE *af);	//
int  af_sig_verify_seg(AFFILE *af,const char *segname);	// see below for return codes

int  af_is_signature_segment(const char *segname);

/* PKI sealing */
int  af_set_seal_certificates(AFFILE *af,const char *certfiles[],int numcertfiles);
int  af_seal_affkey_using_certificates(AFFILE *af,const char *certfiles[],int numcertfiles, unsigned char affkey[32]);//
int  af_set_unseal_keybuffer(AFFILE *af,const char *key); // take key from a buffer
int  af_set_unseal_keyfile(AFFILE *af,const char *keyfile); // take key from a file
int  af_get_affkey_using_keyfile(AFFILE *af, const char *private_keyfile,unsigned char affkey[32]);



#ifdef HAVE_OPENSSL_EVP_H
#include <openssl/evp.h>
int  af_sig_verify_seg2(AFFILE *af,const char *segname,EVP_PKEY *pubkey,unsigned char *sigbuf,
			size_t sigbuf_len,int sigmode);
int af_hash_verify_seg2(AFFILE *af,const char *segname,unsigned char *sigbuf_,size_t sigbuf_len_,int sigmode);
#define AF_HASH_VERIFIES 0

#endif
#define AF_SIGNATURE_MODE0 0x0000 // signature is for segname, arg, data in segment
#define AF_SIGNATURE_MODE1 0x0001 // signature is for segname, 0 arg, uncompressed data in segment
#define AF_SIGNATURE_DELETE 0xFFFF // signature is invalid; delete segment

/* Metadata access */

/* Compression amounts */

#define AF_COMPRESSION_MIN  1
#define AF_COMPRESSION_DEFAULT -1
#define AF_COMPRESSION_MAX 9
#define AF_COMPRESSION_MIN 1


/****************************************************************
 *** AF segment names that you might be interested in...
 ****************************************************************/

#define AF_IGNORE       ""		// ignore segments with 0-length name
#define AF_DIRECTORY    "dir"		// the directory
#define AF_RAW_IMAGE_FILE_EXTENSION "raw_image_file_extension"
#define AF_PAGES_PER_RAW_IMAGE_FILE "pages_per_raw_image_file"

#define AF_PAGESIZE	"pagesize"	// page data size, in bytes, stored in arg
#define AF_IMAGESIZE	"imagesize"	// last logical byte in image, stored as a 64-bit number
#define AF_BADSECTORS	"badsectors"	// number of bad sectors
#define AF_SECTORSIZE	"sectorsize"	// in bytes, stored in arg
#define AF_DEVICE_SECTORS "devicesectors"// stored as a 64-bit number
#define AF_BADFLAG      "badflag"	// data used to mark a bad sector
#define AF_PAGE		"page%" I64d	// segment flag indicates compression (replaces seg%d)
#define AF_PAGE_MD5	AF_PAGE"_md5"	// md5 hash of page
#define AF_PAGE_SHA1	AF_PAGE"_sha1"	// sha1 hash of page
#define AF_PAGE_SHA256	AF_PAGE"_sha256"// sha256 hash of page
#define AF_PARITY0      "parity0"	// parity page of all bytes
#define AF_BATCH_NAME		"batch_name"
#define AF_BATCH_ITEM_NAME	"batch_item_name"

#define AF_BLANKSECTORS "blanksectors"	// all NULs; 8-bytes
#define AF_AFF_FILE_TYPE "aff_file_type" // contents should be "AFF", "AFM" or "AFD"

#define AF_AFFKEY	 "affkey_aes256" // segment for AES256 session key encrypted with sha of the passphrase
#define AF_AFFKEY_EVP    "affkey_evp%d"  // segment for encrypted affkey
#define AF_AES256_SUFFIX "/aes256"	// suffix for encrypted segments
#define AF_SIG256_SUFFIX "/sha256"	// suffix for signature segments
#define AF_SIGN256_CERT  "cert-sha256"   // segment name for image creator's public key
#define AF_PARITY0_SIG   "parity0/sha256"   // signature for parity segment

/* Chain of custody segments */
#define AF_BOM_SEG "affbom%d"

/* Deprecated terminology; pages were originally called data segments */
#define AF_SEG_D        "seg%" I64d	// segment flag indicates compression (deprecated)
#define AF_SEGSIZE_D	"segsize"	// segment data size (deprecated)

/* Bill of Materials */
#define AF_XML_AFFBOM "affbom"
#define AF_XML_DATE "date"
#define AF_XML_SIGNING_CER	"signingcert"
#define AF_XML_SEGMENT_HASH	"segmenthash"


/* AFF Flags */
/* Flags for 8-byte segments */
#define AF_SEG_QUADWORD        0x0002

/* Flags for selecting compression algorithm to try */
#define AF_COMPRESSION_ALG_NONE 0	// don't compress
#define AF_COMPRESSION_ALG_ZLIB 1	// try to compress with zlib
#define AF_COMPRESSION_ALG_LZMA 2	// try to compress with LZMA

/* Arg Flags for data pages; this is stored in 'flag' of data segment */
#define AF_PAGE_COMPRESSED      0x0001
#define AF_PAGE_COMP_MAX        0x0002	// compressed at maximum; nice to know
#define AF_PAGE_COMP_ALG_MASK   0x00F0	// up to 16 compression algorithms may be used
#define AF_PAGE_COMP_ALG_ZLIB   0x0000
#define AF_PAGE_COMP_ALG_BZIP   0x0010	// not implemented; why bother?
#define AF_PAGE_COMP_ALG_LZMA   0x0020	// high compression but pretty slow
#define AF_PAGE_COMP_ALG_ZERO   0x0030  // Data segment is a 4-byte value of # of NULLs.

#define AF_MD5    "md5"			// stores image md5
#define AF_SHA1   "sha1"			// stores image sha1
#define AF_SHA256 "sha256"		// stores image sha256

#define AF_CREATOR	"creator"	// progname of the program that created the AFF file

/* segment names: imaging */
#define AF_CASE_NUM			"case_num"      // case number
#define AF_IMAGE_GID			"image_gid"      // 128-bit unique number
#define AF_ACQUISITION_ISO_COUNTRY  "acquisition_iso_country" // ISO country code
#define AF_ACQUISITION_COMMAND_LINE "acquisition_commandline" // actual command line used to create the image
#define AF_ACQUISITION_DATE	    "acquisition_date" // YYYY-MM-DD HH:MM:SS TZT
#define AF_ACQUISITION_NOTES	    "acquisition_notes" // notes made while imaging
#define AF_ACQUISITION_DEVICE	    "acquisition_device" // device used to do the imaging
#define AF_ACQUISITION_SECONDS      "acquisition_seconds" // stored in arg
#define AF_ACQUISITION_TECHNICIAN   "acquisition_tecnician"
#define AF_ACQUISITION_MACADDR      "acquisition_macaddr"
#define AF_ACQUISITION_DMESG	    "acquisition_dmesg"


//  mac addresses are store in ASCII as a list of lines that end with \n,
//  for example, "00:03:93:14:c5:04\n"
//  It is all the mac addresses that were on the acquisition system

// DMESG is the output from the "dmesg" command at the time of acquisition


/* segment names: device hardware */

#define AF_AFFLIB_VERSION	"afflib_version" // version of AFFLIB that made this file
#define AF_DEVICE_MANUFACTURER  "device_manufacturer"
#define AF_DEVICE_MODEL		"device_model"  // string for ident from drive
#define AF_DEVICE_SN		"device_sn"     // string of drive capabilities
#define AF_DEVICE_FIRMWARE	"device_firmware"	// string of drive capabilities
#define AF_DEVICE_SOURCE        "device_source" // string
#define AF_CYLINDERS		"cylinders"     // quad with # cylinders
#define AF_HEADS		"heads"	        // quad with # heads
#define AF_SECTORS_PER_TRACK	"sectors_per_track"// quad with # sectors/track
#define AF_LBA_SIZE		"lbasize"
#define AF_HPA_PRESENT          "hpa_present"   // flag = 1 or 0
#define AF_DCO_PRESENT          "dco_present"   // flag = 1 or 0
#define AF_LOCATION_IN_COMPUTER "location_in_computer" // text, where it was found
#define AF_DEVICE_CAPABILITIES	"device_capabilities" // string; human-readable

#define AF_MAX_NAME_LEN 64	// segment names should not be larger than this

/* AFF error codes */
#define AF_ERROR_NO_ERROR 0
#define AF_ERROR_EOF -1
#define AF_ERROR_DATASMALL -2
#define AF_ERROR_TAIL  -3		// no tail, or error reading tail
#define AF_ERROR_SEGH  -4		// no head, or error reading head
#define AF_ERROR_NAME  -5		// segment name invalid
#define AF_ERROR_INVALID_ARG -6		// argument invalid
#define AF_ERROR_NO_AES -7		// AES support is not compiled in
#define AF_ERROR_AES_TOO_SMALL -8	// and AES-encrypted segment was too small
#define AF_ERROR_KEY_SET -9		// a key was already set
#define AF_ERROR_AFFKEY_EXISTS -10	// a key already exists in file an attempt was made to establish
#define AF_ERROR_AFFKEY_NOT_EXIST -11	// a key does not exist and an attempt was made to use it.
#define AF_ERROR_AFFKEY_WRONG_VERSION -12
#define AF_ERROR_WRONG_PASSPHRASE -13
#define AF_ERROR_RNG_FAIL -13
#define AF_ERROR_HASH_FAIL -14
#define AF_ERROR_NO_SHA256 -15

#define AF_SIG_GOOD 0
#define AF_ERROR_SIG_BAD   -15
#define AF_ERROR_SIG_NO_CERT -16
#define AF_ERROR_SIG_CANNOT_READ_PUBLIC_KEY -17
#define AF_ERROR_SIG_DATAREAD_ERROR -18
#define AF_ERROR_SIG_MALLOC -19
#define AF_ERROR_SIG_READ_ERROR -20
#define AF_ERROR_SIG_SIG_SEG  -21	// can't verify the signature on a signature segment
#define AF_ERROR_SIG_NOT_COMPILED -22	// afflib compiled without signature support


/* AFF environment variables */
#define AFFLIB_CACHE_STATS      "AFFLIB_CACHE_STATS" // make non-zero to dump stats to STDERR at end
#define AFFLIB_CACHE_DEBUG      "AFFLIB_CACHE_DEBUG" // make "1" to dump a trace of cache events to stderr
#define AFFLIB_CACHE_PAGES      "AFFLIB_CACHE_PAGES" // Size of the page cache
#define AFFLIB_CACHE_PAGES_DEFAULT 32	// default number of cache pages
#define AFFLIB_BIGTMP           "AFFLIB_BIGTMP" // default directory to put very big files for test programs
#define AFFLIB_TRACEFILE        "AFFLIB_TRACEFILE" // If set, send a record of all activity to the location
/* passphrases for single-key cryptography */
#define AFFLIB_PASSPHRASE	"AFFLIB_PASSPHRASE"
#define AFFLIB_PASSPHRASE_FILE  "AFFLIB_PASSPHRASE_FILE"
#define AFFLIB_PASSPHRASE_FD    "AFFLIB_PASSPHRASE_FD"

/* passphrases for signing keys */
#define AFFLIB_PEM_SIGNING_PASSPHRASE "AFFLIB_PEM_SIGNING_PASSPHRASE"

/* passphrases for sealing keys */

#define AFFLIB_DECRYPTING_PRIVATE_KEYFILE "AFFLIB_DECRYPTING_PRIVATE_KEYFILE"

extern FILE *af_trace;		// fd to trace to

/****************************************************************
 *** Not AFF functions at all, but placed here for convenience.
 ****************************************************************/
const char *af_hexbuf(char *dst,int dst_len,const unsigned char *bin,int bytes,int format_flag);

/* af_hexbuf formats: */
#define AF_HEXBUF_NO_SPACES 0
#define AF_HEXBUF_SPACE2    0x0001	// space every 2 characters
#define AF_HEXBUF_SPACE4    0x0002	// space every 4 characters
#define AF_HEXBUF_UPPERCASE 0x1000	// uppercase
#define AF_HEXBUF_LINEBREAK 0x2000	// break every 80 cols


/****************************************************************
 *** Internal implementation details below.
 ****************************************************************/


#ifdef __never_defined__
{
#endif
#ifdef __cplusplus
}
#endif
#endif


