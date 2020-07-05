/*
 * afflib.cpp
 * Distributed under the Berkeley 4-part license
 */

#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"

#ifdef HAVE_OPENSSL_PEM_H
#include <openssl/pem.h>
#include <openssl/bio.h>
#endif

#include "vnode_raw.h"
#include "vnode_split_raw.h"
#include "vnode_afm.h"
#include "vnode_aff.h"
#include "vnode_afd.h"

#ifdef USE_QEMU
#include "vnode_qemu.h"
#endif

#ifdef USE_S3
#include "vnode_s3.h"
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include <assert.h>
#include <errno.h>


// vnode implementations.
// order matters
struct af_vnode *af_vnode_array[] = {
#ifdef USE_S3
    &vnode_s3,				// must be first for s3:// interpertation
#endif
    &vnode_afd,
    &vnode_afm,				// must be before aff
    &vnode_aff,
#ifdef USE_QEMU
    &vnode_vmdk,
    &vnode_dmg,
#endif
#ifdef USE_SPARSEIMAGE
    &vnode_sparseimage,
#endif
    &vnode_split_raw,			// must be before raw
    &vnode_raw,				// greedy; must be last
    0};



/****************************************************************
 *** Support functions that don't use the "af"
 ****************************************************************/

static int aff_initialized = 0;
int af_cache_debug = 0;
FILE *af_trace = 0;

void af_initialize()
{
    if(aff_initialized) return;

    /* make sure things were compiled properly */
    assert(sizeof(struct af_head)==8);
    assert(sizeof(struct af_segment_head)==16);
    assert(sizeof(struct af_segment_tail)==8);
    assert(sizeof(struct affkey)==AFFKEY_SIZE);

    /* Make sure OpenSSL is working */
    OpenSSL_add_all_algorithms();

    const char *val = getenv(AFFLIB_CACHE_DEBUG);
    if(val) af_cache_debug = atoi(val);

    val = getenv(AFFLIB_TRACEFILE);
    if(val){
	af_trace = fopen(val,"wa");
	fprintf(af_trace,"============================\n");
	fprintf(af_trace,"AFFLIB trace started\n");
	setvbuf(af_trace,0,_IOLBF,0);
    }
    aff_initialized = 1;
}


/****************************************************************
 *** Other functions that don't use the af
 ****************************************************************/


const char *af_version(void)
{
    return PACKAGE_VERSION;
}


/* Return 1 if a file is probably an AFF file
 * 0 if it is not.
 * -1 if failure.
 */

int af_identify_file_type(const char *filename,int exists)
{
    for(int i = 0; af_vnode_array[i]; i++){
	if( (*af_vnode_array[i]->identify)(filename,exists)==1 ){
	    return (af_vnode_array[i]->type);
	}
    }
    return exists ? AF_IDENTIFY_NOEXIST : AF_IDENTIFY_ERR;
}

const char *af_identify_file_name(const char *filename,int exists)
{
    for(int i = 0; af_vnode_array[i]; i++){
	if( (*af_vnode_array[i]->identify)(filename,exists)==1 ){
	    return (af_vnode_array[i]->name);
	}
    }
    return 0;
}

char    af_error_str[64];		// in case af_perror is called
void	af_perror(const char *str)
{
#ifdef HAVE_GETPROGNAME
    fprintf(stderr,"%s: %s: %s\n",getprogname(),str,af_error_str);
#else
    fprintf(stderr,"%s: %s\n",str,af_error_str);
#endif
}


/* Return the 'extension' of str.
 * af_str("filename.aff") = ".aff"
 */
const char *af_ext(const char *str)
{
    int len = strlen(str);
    if(len==0) return str;		// no extension
    for(int i=len-1;i>0;i--){
	if(str[i]=='.') return &str[i+1];
    }
    return str;
}

int af_ext_is(const char *filename,const char *ext)
{
    return strcasecmp(af_ext(filename),ext)==0;
}

static int ends_with(const char *buf,const char *with)
{
    if(buf && with){
	size_t buflen = strlen(buf);
	size_t withlen = strlen(with);
	if(buflen>withlen && strcmp(buf+buflen-withlen,with)==0) return 1;
    }
    return 0;
}

/****************************************************************
 *** GET FUNCTIONS
 ****************************************************************/

/****************************************************************
 *** Probe the next segment:
 *** Return its name and argument, but don't advance the pointer....
 *** Returns 0 on success, -1 on end of file or other error.
 ****************************************************************/

/****************************************************************
 *** AFF creation functions
 ****************************************************************/

static void af_deallocate(AFFILE *af)
{
     /* Clear out the cache */
    if(af->pbcache){
	for(int i=0;i<af->num_pbufs;i++){
	    struct aff_pagebuf *p = &af->pbcache[i];
	    if(p->pagebuf){
		memset(p->pagebuf,0,af->image_pagesize); // clean object reuse
		free(p->pagebuf);
	    }
	}
	free(af->pbcache);
    }
#ifdef HAVE_PTHREAD
    AF_UNLOCK(af);
    pthread_rwlock_destroy(&af->rwlock);
#endif
    if(af->protocol)    free(af->protocol);
    if(af->fname)	free(af->fname);
    if(af->username)    free(af->username);
    if(af->password)    free(af->password);
    if(af->hostname)    free(af->hostname);
    if(af->badflag)	free(af->badflag);
    if(af->toc)         free(af->toc);
    if(af->crypto)	af_crypto_deallocate(af);
    if(af->vni_cache)   free(af->vni_cache);
    memset(af,0,sizeof(*af));		// clean object reuse
    free(af);
}

static void af_sanitize_password(AFFILE *af)
{
    for(char *cc = af->password;*cc;cc++){
	*cc = 'X';
    }
    free(af->password);
    af->password = 0;
}


/* af_open_with is the real open routine.
 * It opens a particular file with a particular vnode implementation.
 */
AFFILE *af_open_with(const char *url,int flags,int mode, struct af_vnode *v)
{
    /* Alloate the space for the AFFILE structure */
    AFFILE *af = (AFFILE *)calloc(sizeof(AFFILE),1);
    af_crypto_allocate(af);
#ifdef HAVE_PTHREAD
    pthread_rwlock_init(&af->rwlock, 0);
    AF_WRLOCK(af);
#endif
    af->v	  = v;
    af->version   = 2;
    af->openflags = flags | O_BINARY;	// make sure that we ask for binray
    af->openmode  = mode;
    af->image_sectorsize = 512;		// default size
    af->error_reporter = warnx;

    /* Decode URL */
    af_parse_url(url,&af->protocol,&af->hostname,&af->username,&af->password,
		 &af->port,&af->fname);

    /* A null passphrase is the same as no passphrase*/
    if(af->password && af->password[0]==0){
	free(af->password);
	af->password=0;
    }
    /* If no password was set and the AFFLIB_PASSPHRASE environment variable is set, use that */
    if(af->password==0 && getenv(AFFLIB_PASSPHRASE)){
	af->password = strdup(getenv(AFFLIB_PASSPHRASE));
    }
    /* If no password is set and its in a file, get it there */
    if(af->password==0 && getenv(AFFLIB_PASSPHRASE_FILE)){
	int fd = open(AFFLIB_PASSPHRASE_FILE,O_RDONLY,0);
	if(fd>0){
	    struct stat sb;
	    if(fstat(fd,&sb)==0){
		af->password = (char *)malloc(sb.st_size);
		int r = read(fd,af->password,sb.st_size);
		if(r!=sb.st_size){
		    free(af->password);
		    af->password=0;	// couldn't read it
		}
		close(fd);
	    }
	}
    }
    /* If no password is set and its in a file, get it there */
    if(af->password==0 && getenv(AFFLIB_PASSPHRASE_FD)){
	int fd = atoi(AFFLIB_PASSPHRASE_FD);
	af->password = (char *)malloc(1);
	int buflen = 0;
	int rlen = 0;
	char mybuf[1024];

	while((rlen=read(fd,mybuf,sizeof(mybuf)))>0){
	    af->password = (char *)realloc(af->password,buflen+rlen+1);
	    memcpy(af->password+buflen,mybuf,rlen);
	    buflen += rlen;
	    af->password[buflen] = '\000';
	}
    }

    /* TK: If no password was set and the AFFLIB_ASK_PASS is set, ask for a passphrase */

    /* Note things for hard files */
    af->exists    = (access(af->fname,R_OK) == 0);	// does the file exist?

    /* Right now just set up the cache by hand */
    const char *cache_pages = getenv(AFFLIB_CACHE_PAGES);
    if(cache_pages) af->num_pbufs = atoi(cache_pages);
    if(af->num_pbufs<1) af->num_pbufs = AFFLIB_CACHE_PAGES_DEFAULT; // default valuen

    af->pbcache   = (struct aff_pagebuf *)calloc(af->num_pbufs,sizeof(struct aff_pagebuf));
    if(af->pbcache==0){			// if can't allocate the full amount
	af->num_pbufs = 2;		// try a significantly smaller cache
	af->pbcache   = (struct aff_pagebuf *)calloc(af->num_pbufs,sizeof(struct aff_pagebuf));
    }

    if(flags & AF_HALF_OPEN) return af;	// for low-level tools

    /* Try opening it! */
    if((*af->v->open)(af)){
	strlcpy(af_error_str,af->error_str,sizeof(af_error_str)); // make a copy of the error string
	af_deallocate(af);
	return 0;
    }

    /* If there is no AFFKEY and the file is read-only, don't use a password */
    if(af->password && (af_get_seg(af,AF_AFFKEY,0,0,0)!=0) && ((af->openflags & O_ACCMODE)==O_RDONLY)){
	af_sanitize_password(af);
    }

    /* Set up the encryption if requested and if this support metadata */
    if(AF_SEALING_VNODE(af) && ((flags & AF_NO_CRYPTO)==0)){
	bool can_decrypt = false;
	if(af->password){
	    struct af_vnode_info vni;
	    memset(&vni,0,sizeof(vni));
	    if((*af->v->vstat)(af,&vni)==0 && vni.supports_metadata){
		int r = 0;
		if(af_get_seg(af,AF_AFFKEY,0,0,0)!=0){ // it does not have a password
		    r = af_establish_aes_passphrase(af,af->password);
		}
		if(r==0){
		    r = af_use_aes_passphrase(af,af->password);
		    if(r==0) {
			can_decrypt = true;
		    } else {
			(*af->error_reporter)("af_open: invalid passphrase: '%s'",af->password);
		    }
		}
		af_sanitize_password(af);
	    }
	}

	/* Try public key... */
	if(can_decrypt==false){
	    const char *kf = getenv(AFFLIB_DECRYPTING_PRIVATE_KEYFILE);
	    if(kf){
		af_set_unseal_keyfile(af,kf);
	    }
	}
    }

    af_read_sizes(af);		// set up the metadata
    if(af_trace) fprintf(af_trace,"af_open_with(%s,%o,%o,%s)\n",url,flags,mode,v->name);
    return af;
}



AFFILE *af_open(const char *filename,int flags,int mode)
{
    if(!aff_initialized) af_initialize();
    if(ends_with(filename,".E01") || ends_with(filename,".e01")){
	errno = EINVAL;
	return 0;
    }

    /* Note on Hurd O_RDWR == O_WRONLY|O_RDONLY, hence the need to check that as well */
    if((flags & O_WRONLY) && !(flags & O_RDWR)){
	errno = EINVAL;
	return 0;			// this flag not supported
    }
    int exists = (flags & O_CREAT) ? 0 : 1; // file must exist if O_CREAT not specified


    /* Figure out it's format, then hand off to the correct subsystem. */
    for(int i = 0; af_vnode_array[i]; i++){
	/* Check to see if the implementation identifies the file */
	if( (*af_vnode_array[i]->identify)(filename,exists)==1 ){
	    AFFILE *af = af_open_with(filename,flags,mode,af_vnode_array[i]);
	    return af;
	}
    }
    errno = EINVAL;
    if(exists && access(filename, R_OK) != 0) errno = ENOENT;
    return 0;				// can't figure it out; must be an invalid extension
}

/** Set an option and return the previous value */
int af_set_option(AFFILE *af,int option,int value)
{
    int prev = 0;
    switch(option){
    case AF_OPTION_AUTO_ENCRYPT:
	prev = af->crypto->auto_encrypt;
	af->crypto->auto_encrypt = value;
	return prev;
    case AF_OPTION_AUTO_DECRYPT:
	prev = af->crypto->auto_decrypt;
	af->crypto->auto_decrypt = value;
	return prev;
    }
    return -1;
}

/* Open a regular file as an affile.
 * Can only be a raw file...
 */
AFFILE *af_freopen(FILE *file)
{
    if(!aff_initialized) af_initialize();

    AFFILE *af = (AFFILE *)calloc(sizeof(AFFILE),1);
    af->v = &vnode_raw;
    af->image_sectorsize = 512;		// default
    raw_freopen(af,file);
    return af;
}

#ifdef UNIX
/* Open a regular file as an affile */
AFFILE *af_popen(const char *command,const char *type)
{
    if(!aff_initialized) af_initialize();
    AFFILE *af = (AFFILE *)calloc(sizeof(AFFILE),1);
    af->v   = &vnode_raw;
    raw_popen(af,command,type);
    af->image_sectorsize = 512;		// default
    af->openflags = O_RDONLY;
    af->fname     = strdup(command);
    return af;
}
#endif


/* Close the image and unallocate fields */
int af_close(AFFILE *af)
{
    int ret = 0;

    AF_WRLOCK(af);
    af_cache_flush(af);			// flush the cache (if writing)

    if(af->image_size != af->image_size_in_file){
	af_update_segq(af,AF_IMAGESIZE,(int64_t)af->image_size);
	af->image_size_in_file = af->image_size;
    }
    if(getenv(AFFLIB_CACHE_STATS)){
	fputc('\n',stderr);
	af_stats(af,stderr);
    }

    (*af->v->close)(af);
    af_deallocate(af);
    return ret;
}


/* Seek in the virtual file */
uint64_t af_seek(AFFILE *af,int64_t pos,int whence)
{
    AF_WRLOCK(af);
    if(af_trace) fprintf(af_trace,"af_seek(%p,%" I64d ",%d)\n",af,pos,whence);
    uint64_t new_pos=0;
    switch(whence){
    case SEEK_SET:
	new_pos = pos;
	break;
    case SEEK_CUR:
	if(pos<0 && ((uint64_t)(-pos)) > af->pos) new_pos=0;
	else new_pos = af->pos + pos;
	break;
    case SEEK_END:
	if((uint64_t)pos > af->image_size) new_pos=0;
	else new_pos = af->image_size - pos;
	break;
    }

    /* Note if the direction has changed */
    int direction = (new_pos > af->pos)  ? 1 : ((new_pos < af->pos) ? -1 : 0);
    if(af->last_direction != direction) af->direction_changes++;
    if(af->direction_changes > 5 && af->random_access==0){
	af->random_access=1;
    }
    af->last_direction = direction;
   /*****************************************************************/

    /* Finally update the direction */
    af->pos = new_pos;			// set the new position
    AF_UNLOCK(af);
    return af->pos;
}

/* Returns the name and offset of the last segment */
int	af_last_seg(AFFILE *af,char *last_segname,int last_segname_len,int64_t *pos)
{
    AF_WRLOCK(af);
    /* Find the name of the last segment */
    fseeko(af->aseg,0,SEEK_END);
    af_backspace(af);		// back up one segment
    *pos = ftello(af->aseg);	// remember where it is
    last_segname[0] = 0;
    int ret = af_probe_next_seg(af,last_segname,last_segname_len,0,0,0,0);
    AF_UNLOCK(af);
    return ret;
}


uint64_t  af_tell(AFFILE *af)
{
    AF_READLOCK(af);
    uint64_t ret = af->pos;
    AF_UNLOCK(af);
    return ret;
}

/* Return if we are at the end of the file */
int af_eof(AFFILE *af)
{
    AF_READLOCK(af);
    af_vnode_info vni;

    if(af_vstat(af,&vni)) return -1;	// this is bad; we need vstat...
    if(vni.use_eof) return vni.at_eof;	// if implementation wants to use it, use it

    int ret = (int64_t)af->pos >= (int64_t)vni.imagesize;
    AF_UNLOCK(af);
    return ret;
}

void af_set_callback(AFFILE *af,void (*wcb)(struct affcallback_info *))
{
    AF_WRLOCK(af);
    af->w_callback	  = wcb;
    AF_UNLOCK(af);
}


void af_enable_compression(AFFILE *af,int type,int level)
{
    AF_WRLOCK(af);
    af->compression_type  = type;
    af->compression_level = level;
    AF_UNLOCK(af);
}

int	af_compression_type(AFFILE *af)
{
    AF_READLOCK(af);
    int ret = af->compression_type;
    AF_UNLOCK(af);
    return ret;
}


/* Doesn't need locking because it won't change */
const char *af_filename(AFFILE *af)
{
    return af->fname;
}

/* Doesn't need locking because it won't change */
int	af_identify(AFFILE *af)
{
    return af->v->type;
}

/* af_get_imagesize:
 * Return the byte # of last mapped byte in image, or size of device;
 * No locking is needed because individual elements of the af structure are not accessed.
 */
int64_t af_get_imagesize(AFFILE *af)
{
    int64_t ret = -1;
    struct af_vnode_info vni;
    memset(&vni,0,sizeof(vni));
    if(af_vstat(af,&vni)==0){
	/* If vni.imagesize is 0 and if there are encrypted segments and if there
	 * is no imagesize segment but there is an encrypted one, then we can't read this encrypted file...
	 */
	if(vni.imagesize<=0 && vni.segment_count_encrypted>0){
	    if(af_get_seg(af,AF_IMAGESIZE,0,0,0)!=0){
		errno = EPERM;
		goto done;
	    }
	}
	ret = vni.imagesize;
    }
 done:;
    return ret;
}

/*
 * af_make_badflag:
 * Create a randomized bag flag and
 * leave an empty segment of how many badsectors there are
 * in the image...
 */
int af_make_badflag(AFFILE *af)
{
    if(af->badflag!=0) free(af->badflag);
    af->badflag = (unsigned char *)malloc(af->image_sectorsize); // current sector size

    RAND_bytes(af->badflag,af->image_sectorsize);
    strcpy((char *)af->badflag,"BAD SECTOR");

    AF_WRLOCK(af);
    af->badflag_set = 1;
    if(af_update_seg(af,AF_BADFLAG,0,af->badflag,af->image_sectorsize)){
	AF_UNLOCK(af);
	return -1;
    }
    if(af_update_segq(af,AF_BADSECTORS,0)){
	AF_UNLOCK(af);
	return -1;
    }
    AF_UNLOCK(af);
    return 0;
}


/*
 * make the IMAGE_GID segment if it doesn't exist
 * Returns -1 if an error, 0 if the GID exists, and 1 if one is made.
 */
int af_make_gid(AFFILE *af)
{
    int ret = 0;
    AF_WRLOCK(af);
    if(af_get_seg(af,AF_IMAGE_GID,0,0,0)!=0){
	unsigned char bit128[16];
	RAND_bytes(bit128,sizeof(bit128));
	int r = af_update_seg(af,AF_IMAGE_GID,0,bit128,sizeof(bit128));
	if(r<0) ret = -1;
	else ret = 1;
    }
    AF_UNLOCK(af);
    return ret;
}



/* Decrypt data and perform unblocking if necessary.
 * This could eliminate a memory copy by doing the decryption for everything
 * but the last block in place, and just doing a copy for the last block.
 */
void af_aes_decrypt(AFFILE *af,const char *segname,unsigned char *data,size_t *datalen)
{
#ifdef HAVE_AES_ENCRYPT
    if(datalen==0) return;		// can't decrypt; no clue how long it is

    /* An encrypted segment was retrieved; decrypt and trunc the length as necessary */
    uint32_t extra = (*datalen) % AES_BLOCK_SIZE;
    uint32_t pad = (AES_BLOCK_SIZE - extra) % AES_BLOCK_SIZE;

    if(data==0){			// just wants to find out new length
	if(extra>0){
	    *datalen -= AES_BLOCK_SIZE;
	}
	return;
    }

    if(extra!=0 && *datalen < AES_BLOCK_SIZE){
	*datalen = 0;			// something is wrong
	return;
    }

    if(data==0){			// just report the new size
	if(extra!=0) *datalen -= AES_BLOCK_SIZE; // a block was added
	return;
    }

    *datalen -= extra;	// *datalen is now a multiple of AES_BLOCK_SIZE
    /* Create an IV */
    unsigned char iv[AES_BLOCK_SIZE];
    memset(iv,0,sizeof(iv));
    strlcpy((char *)iv,segname,sizeof(iv));

    /* Decrypt! */
    AF_READLOCK(af);
    AES_cbc_encrypt(data,data,*datalen,&af->crypto->dkey,iv,AES_DECRYPT);
    AF_UNLOCK(af);
    *datalen -= pad;	// remove the padding
#endif
}


int af_get_seg(AFFILE *af,const char *segname,uint32_t *arg,unsigned char *data,size_t *datalen)
{
    AF_READLOCK(af);
    if(af->v->get_seg==0){
	errno = ENOTSUP;
	return -1;	// not supported by this file system
    }
#ifdef HAVE_AES_ENCRYPT
    /* If we have encryption and it is turned on, check for encrypted segment first */
    if(AF_SEALING_VNODE(af) && af->crypto->auto_decrypt){
	size_t datalen_orig = datalen ? *datalen : 0;
	char aesname[AF_MAX_NAME_LEN];
	strlcpy(aesname,segname,sizeof(aesname));
	strlcat(aesname,AF_AES256_SUFFIX,sizeof(aesname));
	int r = (*af->v->get_seg)(af,aesname,arg,data,datalen);
	if(r==0){
	    af_aes_decrypt(af,segname,data,datalen);
	    return 0;
	}
	if(r==AF_ERROR_DATASMALL && datalen && (*datalen % AES_BLOCK_SIZE != 0)){
	    /* Not enough space was provided to decrypt, probably because this was blocked out */
	    size_t bigger_data_len = datalen_orig + AES_BLOCK_SIZE;
	    unsigned char *bigger_data = (unsigned char *)malloc(bigger_data_len);
	    if(!bigger_data) return -1;	// Malloc failed
	    r = (*af->v->get_seg)(af,aesname,arg,bigger_data,&bigger_data_len);
	    if(r!=0){
		free(bigger_data);
		return -1;		// something deeper is wrong
	    }
	    af_aes_decrypt(af,segname,bigger_data,&bigger_data_len);
	    if(bigger_data_len > datalen_orig){
		free(bigger_data);
		return -1;		// it's still too big
	    }
	    memcpy(data,bigger_data,bigger_data_len);
	    *datalen = bigger_data_len;
	    free(bigger_data);
	    return 0;			// finally it fits
	}
    }
#endif
    /* Try for the unencrypted segment */
    int ret = (*af->v->get_seg)(af,segname,arg,data,datalen);
    AF_UNLOCK(af);
    return ret;
}

int af_get_next_seg(AFFILE *af,char *segname,size_t segname_len,uint32_t *arg,
			unsigned char *data,size_t *datalen)
{
    size_t datalen_orig = datalen ? *datalen : 0;
    AF_READLOCK(af);
    if(af->v->get_next_seg==0){
	errno = ENOTSUP;
	AF_UNLOCK(af);
	return -1;
    }
    int r = (*af->v->get_next_seg)(af,segname,segname_len,arg,data,datalen);
#ifdef HAVE_AES_ENCRYPT
    if(AF_SEALING_VNODE(af)
       && ends_with(segname,AF_AES256_SUFFIX)
       && af->crypto->auto_decrypt){
	segname[strlen(segname)-strlen(AF_AES256_SUFFIX)] = 0;
	/* An encrypted segment was retrieved.
	 * If it fit, decrypt and return.
	 * If it doesn't fit, try to get it again (which will use our adaptive blocksize)
	 *
	 * Normaly it will fit becuase the caller doesn't know how long the 'next' segment is,
	 * so the caller normally leaves enough space.
	 */
	if(r==0){
	    af_aes_decrypt(af,segname,data,datalen);
	    AF_UNLOCK(af);
	    return 0;
	}
	if(r==AF_ERROR_DATASMALL && datalen && (*datalen % AES_BLOCK_SIZE !=0)){
	    *datalen = datalen_orig;
	    AF_UNLOCK(af);
	    return af_get_seg(af,segname,arg,data,datalen);
	}
	AF_UNLOCK(af);
	return r;			// not sure why we got this error
    }
#endif
    AF_UNLOCK(af);
    return r;
}

int af_rewind_seg(AFFILE *af)
{
    if(af_trace) fprintf(af_trace,"af_rewind_seg(%p)\n",af);
    AF_READLOCK(af);
    if(af->v->rewind_seg==0){
	errno = ENOTSUP;
	AF_UNLOCK(af);
	return -1;
    }
    int ret = (*af->v->rewind_seg)(af);
    AF_UNLOCK(af);
    return ret;
}

/** Main routine for writing segments
 */

int af_update_segf(AFFILE *af, const char *segname,
		  uint32_t arg,const u_char *data,uint32_t datalen,uint32_t flag)
{
    if(af_trace) fprintf(af_trace,"af_update_segf(%p,segname=%s,arg=%" PRIu32 ",datalen=%d)\n",
			 af,segname,arg,datalen);
    AF_WRLOCK(af);
    if(af->v->update_seg==0){
	errno = ENOTSUP;
	AF_UNLOCK(af);
	return -1;	// not supported by this file system
    }

    af_invalidate_vni_cache(af);

    /* See if we need to encrypt. New memory might need to be allocated.
     * This isn't a big deal, because encryption requires copying memory
     * in any event; it's either an in-place copy or a copy to another location.
     */
#ifdef HAVE_AES_ENCRYPT
    const char *oldname = 0;
    unsigned char *newdata = 0;
    char aesname[AF_MAX_NAME_LEN];
    if(AF_SEALING(af) && ((flag & AF_SIGFLAG_NOSEAL)==0) && af->crypto->auto_encrypt){
	/* Create an IV */
	unsigned char iv[AES_BLOCK_SIZE];
	memset(iv,0,sizeof(iv));
	strlcpy((char *)iv,segname,sizeof(iv));

	/* Figure out the real segment name */
	strlcpy(aesname,segname,sizeof(aesname));
	strlcat(aesname,AF_AES256_SUFFIX,sizeof(aesname));
	oldname = segname;
	segname = aesname;

	/* Figure out if we need to padd out for encryption. Allocate space and
	 */
	uint32_t extra = (datalen) % AES_BLOCK_SIZE;
	uint32_t pad = (AES_BLOCK_SIZE - extra) % AES_BLOCK_SIZE;
	newdata = (unsigned char *)malloc(datalen+pad+extra);
	memset(newdata+datalen,pad+extra,pad); // PKCS7 uses 01 for one pad byte, 02 02 for two, etc.
	/* Encrypt */
	AES_cbc_encrypt((const unsigned char *)data,
			newdata,
			datalen+pad,&af->crypto->ekey,iv,AES_ENCRYPT);
	data = newdata;			// we will write this out
	datalen += pad + extra;
    }
#endif
    int r = (*af->v->update_seg)(af,segname,arg,data,datalen); // actually update the segment
    if(r < 0)
        { AF_UNLOCK(af); return r; }

    af->bytes_written += datalen;
#ifdef HAVE_AES_ENCRYPT
    /* if we encrypted, make sure the unencrypted segment is deleted */
    if(oldname && af->v->del_seg) (*af->v->del_seg)(af,oldname);
    if(newdata){
	free(newdata);		// free any allocated data
	newdata = 0;
    }
#endif
    /* If we wrote out an unencrypted segment, make sure that the corresopnding encrypted
     * segment is deleted.
     */
    char encrypted_name[AF_MAX_NAME_LEN];
    strlcpy(encrypted_name,segname,sizeof(encrypted_name));
    strlcat(encrypted_name,AF_AES256_SUFFIX,sizeof(encrypted_name));
    if(af->v->del_seg) (*af->v->del_seg)(af,encrypted_name); // no need to check error return


    /* Sign the segment if:
     * - there is a signing private key
     * - the data structure and flag not set
     * - This is not a signature segment
     */
#ifdef USE_AFFSIGS
    const u_char *signdata = data;	// remember the original data location
    if(AF_SEALING(af)
       && af->crypto->sign_privkey
       && ((flag & AF_SIGFLAG_NOSIG)==0)
       && !ends_with(segname,AF_SIG256_SUFFIX)){
	// return code doesn't matter; it's either signed or not.
	af_sign_seg3(af,segname,arg,signdata,datalen,AF_SIGNATURE_MODE0);
    }
#endif
    AF_UNLOCK(af);
    return r;
}

/* Requires no locking because locking is done in af_update_segf */
int af_update_seg(AFFILE *af, const char *segname,
		  uint32_t arg,const u_char *data,uint32_t datalen)
{
    return af_update_segf(af,segname,arg,data,datalen,0);
}

#ifdef HAVE_OPENSSL_BIO_H
/* Requires no locking */
int	af_update_seg_frombio(AFFILE *af,const char *segname,uint32_t /*arg*/,BIO *bio)
{
    /* Get the buffer to write out */
    u_char *buf=0;
    size_t buflen = BIO_get_mem_data(bio,&buf);
    return af_update_seg(af,segname,0,buf,buflen);
}
#endif

int af_del_seg(AFFILE *af,const char *segname)
{
    AF_WRLOCK(af);
    if(af->v->del_seg==0){
	errno = ENOTSUP;
	AF_UNLOCK(af);
	return -1;	// not supported
    }
#ifdef HAVE_AES_ENCRYPT
    if(AF_SEALING(af)){
	/* Delete the encrypted segment if it exists */
	char aesname[AF_MAX_NAME_LEN];
	strlcpy(aesname,segname,sizeof(aesname));
	strlcat(aesname,AF_AES256_SUFFIX,sizeof(aesname));
	(*af->v->del_seg)(af,aesname);
    }
#endif
    /* Delete the unencrypted segment */
    int ret = (*af->v->del_seg)(af,segname);
    AF_UNLOCK(af);
    return ret;
}

void af_invalidate_vni_cache(AFFILE *af)
{
    if(af->vni_cache){
	free(af->vni_cache);
	af->vni_cache = 0;
    }
}

int af_vstat(AFFILE *af,struct af_vnode_info *vni)
{
    AF_READLOCK(af);
    if(af->v->vstat==0){
	errno = ENOTSUP;
	AF_UNLOCK(af);
	return -1;	// not supported
    }
    int ret = 0;
    if(af->vni_cache==0){		// no cached copy?
	af->vni_cache = (struct af_vnode_info *)calloc(1,sizeof(struct af_vnode_info)); // allocate a space
	ret = (*af->v->vstat)(af,af->vni_cache);
    }
    if(ret==0) memcpy(vni,af->vni_cache,sizeof(*vni));
    AF_UNLOCK(af);
    return ret;
}

/* Requires no locking */
int af_has_pages(AFFILE *af)
{
    struct af_vnode_info vni;
    if(af_vstat(af,&vni)) return -1;	// can't figure it out
    return vni.has_pages;		// will return 0 or 1
}


void af_stats(AFFILE *af,FILE *f)
{
    AF_READLOCK(af);
    fprintf(f,"AFSTATS for %s\n",af_filename(af));
    fprintf(f,"Pages read: %" I64u "\n",af->pages_read);
    fprintf(f,"Pages written: %" I64u "\n",af->pages_written);
    fprintf(f,"Pages compressed: %" I64u "\n",af->pages_compressed);
    fprintf(f,"Pages decompressed: %" I64u "\n",af->pages_decompressed);
    fprintf(f,"Cache hits: %" I64u "\n",af->cache_hits);
    fprintf(f,"Cache misses: %" I64u "\n",af->cache_misses);
    fprintf(f,"Bytes copied: %" I64u "\n",af->bytes_memcpy);
    AF_UNLOCK(af);
}


int af_set_acquisition_date(AFFILE *af,time_t t)
{
    char timebuf[64];
    strftime(timebuf,sizeof(timebuf),"%Y-%m-%d %H:%M:%S\n",localtime(&t));
    return af_update_seg(af,AF_ACQUISITION_DATE,0,(const u_char *)timebuf,strlen(timebuf));
}
