/*
 * AFFLIB(tm)
 *
 * AFF and AFFLIB is a trademark of Simson Garfinkel and Basis Technology Corp.
 *
 * Distributed under the Berkeley 4-part license
 */
#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"
#include "vnode_split_raw.h"

#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif

#ifndef HAVE_ISDIGIT
static int isdigit(char ch)
{
    return ch>='0' && ch<='9';
}
#endif



/* split raw file implementation with optional metadata support */
struct split_raw_private {
    u_int   num_raw_files;	// number of raw files
    int    *fds;		// array of file descriptors for each open raw file
    uint64_t *pos;		// where we are in each file
    char *first_raw_fname;    /* The filename of the first raw file. */
    char *next_raw_fname;     /* The filename of the next raw file, or 0
				 when one big file is used. */
    int64_t cur_page; // current page number, used for split_raw_get_next_seg
};

static inline struct split_raw_private *SPLIT_RAW_PRIVATE(AFFILE *af)
{
    assert(af->v == &vnode_split_raw);
    return (struct split_raw_private *)(af->vnodeprivate);
}

/* Return 1 if a file is the first of a split-raw series*/
static int split_raw_identify_file(const char *filename,int exists)
{
    if(exists && access(filename,R_OK)!=0) return 0;	// needs to exist and it doesn't
    return af_ext_is(filename,"000") || af_ext_is(filename,"001") ||
	af_ext_is(filename,"aaa") || af_ext_is(filename,"AAA");
}

/* split_raw_close:
 * Close each of the split files.
 */

static int split_raw_close(AFFILE *af)
{
    struct split_raw_private *srp = SPLIT_RAW_PRIVATE(af);

    for (uint64_t i = 0; i < srp->num_raw_files; i++){
	if(srp->fds[i]){
	    close(srp->fds[i]);
	    srp->fds[i] = 0;
	}
    }
    if (srp->fds)    free (srp->fds);
    if (srp->pos)    free (srp->pos);
    if (srp->first_raw_fname) free (srp->first_raw_fname);
    if (srp->next_raw_fname)  free (srp->next_raw_fname);
    free(srp);
    af->vnodeprivate = 0;
    return 0;
}


/**
 * increment_fname(filename):
 * "filename.000" => "filename.001"
 * "filename.123" => "filename.124"
 * "filename.999" => "filename.AAA"
 * "filename.AZZ" => "filename.BAA"
 * "filename.aaa" => "filename.aab" (legacy support)
 * fn must be at least 4 characters long and must have a 3-character extension
 *
 * @param fn filename to increment (modified in place)
 * @return 0 if successful.
 *         -1 for invalid filename or no more namespace.
 */
/** increase the character and return true if carry */
static bool incval(char &ch,int base)
{
    if(base==10){
	if(ch=='9'){
	    ch='0';
	    return true;
	}
	ch++;
	return false;
    }

    /* Assume base 36 */
    switch(ch){
    case 'Z':
	ch='0';				// go back to 0
	return true;			// and carry
    case '9':
	ch='A';
	return false;
    default:
	ch++;				// normal increment
	return false;
    }
}

int split_raw_increment_fname (char *fn)
{
    size_t len = strlen(fn);
    if(len<4 || fn[len-4]!='.') return -1;
    char *ext = fn+len-3;

    /* See if it is a number */
    if(isdigit(ext[0]) && isdigit(ext[1]) && isdigit(ext[2])){
	unsigned int num = atoi(ext);
	if(num==999){
	    strcpy(ext,"A00");
	    return 0;
	} else if(num >= 0 && num < 999) {
	    snprintf(ext,4,"%03d",(num+1) % 1000);
	    return 0;
	} else {
	    return EINVAL;
	}
    }

    /* First digit goes A-Z, second and third go 0-9A-Z */

    /* Get the case */
    int lower = islower(ext[0]);

    /* Convert to all uppercase */
    for(int i=0;i<3;i++){
	if(isalpha(ext[i])) ext[i] = toupper(ext[i]);
    }

    /* Increment */
    if(incval(ext[2],36)){
	if(incval(ext[1],36)){
	    if(incval(ext[0],36)){
		return EINVAL;
	    }
	}
    }

    /* Convert back to lowercase if necessary */
    for(int i=0;i<3;i++){
	if(isalpha(ext[i]) && lower) ext[i] = tolower(ext[i]);
    }
    return 0;
}


void srp_validate(AFFILE *af)
{
    struct split_raw_private *srp = SPLIT_RAW_PRIVATE(af);
    for(uint32_t i=0;i<srp->num_raw_files;i++){
	assert(srp->fds[i]!=0);
    }
}

/** Debugging routine.
 */
void srp_dump(AFFILE *af)
{
    struct split_raw_private *srp = SPLIT_RAW_PRIVATE(af);
    for(uint32_t i=0;i<srp->num_raw_files;i++){
	fprintf(stderr,"   fds[%d]=%d   pos[%d]=%" I64d "\n",i,srp->fds[i],i,srp->pos[i]);
    }
    srp_validate(af);
    fprintf(stderr,"===================\n");
}

static void srp_add_fd(AFFILE *af,int fd)
{
    struct split_raw_private *srp = SPLIT_RAW_PRIVATE(af);
    srp->num_raw_files++;
    srp->fds = (int *)realloc (srp->fds, sizeof (int) * (srp->num_raw_files));
    srp->fds[srp->num_raw_files - 1] = fd;
    srp->pos = (uint64_t *)realloc (srp->pos, sizeof (uint64_t) * (srp->num_raw_files));
    srp->pos[srp->num_raw_files - 1] = 0;
}


static int split_raw_open_internal(AFFILE *af, uint64_t *image_size)
{
    struct split_raw_private *srp = SPLIT_RAW_PRIVATE(af);
    int fd;

    fd = open(srp->first_raw_fname, af->openflags|O_BINARY, af->openmode);
    if (fd < 0) {
      (*af->error_reporter)("split_raw_open_internal: open(%s): ",af->fname);
	return -1;
    }

    srp->num_raw_files = 1;
    srp->fds = (int *)malloc (sizeof (int));
    srp->fds[0] = fd;
    srp->pos = (uint64_t *)malloc (sizeof (uint64_t));
#ifdef _WIN32
	struct _stat64 sb;
	if (_fstat64(fd, &sb) != 0) {
#else
	struct stat sb;
    if (fstat (fd, &sb) != 0) {
#endif
      (*af->error_reporter)("split_raw_open_internal: fstat(%s): ",af->fname);
	close (fd);
	srp->fds[0] = 0;
	return -1;
    }

    af->maxsize = 0;

    /* If there's a next_raw_fname set by the caller of this function, we
     * have a split file; otherwise we have one big file.
     */
    if (srp->next_raw_fname==0) {
	(*image_size) = sb.st_size;
	return 0;
    }

    /* This gets set to 1 the first time we find a file whose size doesn't
       match the size of the first file.  If we successfully open a file
       when this flag is already 1, then our sanity checks fail. */
    int current_file_must_be_last = 0;

    do {
	if (split_raw_increment_fname (srp->next_raw_fname) != 0) {
	    (*af->error_reporter)("split_raw_open_internal: too many files\n");
	    errno = EINVAL;
	    return -1;
	}
	fd = open(srp->next_raw_fname,
		   af->openflags & O_RDWR ? (O_RDWR|O_BINARY) : (O_RDONLY|O_BINARY));

	if (fd < 0) {
	    if (errno != ENOENT) {
		(af->error_reporter)("split_raw_open_internal errno=%d",errno);
		return -1;
	    }
	    (*image_size) = sb.st_size + af->maxsize * (srp->num_raw_files - 1);
	    errno = 0;		// reset errno
	    return 0;		// end of files
	}
	srp_add_fd(af,fd);
	if (current_file_must_be_last) {
	    (*af->error_reporter)("split_raw_open_internal: %s exists, "
				  "but previous file didn't match expected file size\n",af->fname);
	    return -1;
	}
	/* Set af->maxsize to the size of the first file, but only
	   if a second file exists.  If no second file exists, then we want
	   to use af->maxsize, which cannot be set until after
	   af_open returns.  */
	if (!af->maxsize)
	    af->maxsize = sb.st_size;
#ifdef _WIN32
	if (_fstat64(fd, &sb) != 0) {
#else
	if (fstat (fd, &sb) != 0) {
#endif
	  (*af->error_reporter)("split_raw_open_internal: fstat(%s): ",af->fname);
	    return -1;
	}
	if ((uint64_t)sb.st_size != af->maxsize){
	    current_file_must_be_last = 1;
	}
    } while (1);
    return -1;
}

static int split_raw_open(AFFILE *af)
{
    int ret;

    af->vnodeprivate = (void *)calloc(sizeof(struct split_raw_private),1);
    struct split_raw_private *srp = SPLIT_RAW_PRIVATE(af);

    srp->first_raw_fname = strdup (af->fname);
    srp->next_raw_fname  = strdup (af->fname);
    ret = split_raw_open_internal (af, &(af->image_size));

    if (ret != 0) {
	split_raw_close (af);
	return ret;
    }

    /* Adaptively find the largest pagesize we can use that fits within maxsize */
    af->image_pagesize = 512;
    while ((af->image_pagesize < (16 * 1024 * 1024))
	   && !(af->maxsize % (af->image_pagesize * 2)))
	af->image_pagesize *= 2;

    if ((ret == 0) && (af->maxsize % af->image_pagesize!=0)) {
	(*af->error_reporter)("split_raw_open: %s: raw_file_size (%" I64d " not a multiple of pagesize %lu\n",
			      af->fname, af->maxsize,af->image_pagesize);
	split_raw_close (af);
	return -1;
    }

    return 0;
}

static int split_raw_vstat(AFFILE *af,struct af_vnode_info *vni)
{
    memset(vni,0,sizeof(*vni));		// clear it
    vni->imagesize = af->image_size;
    vni->pagesize  = af->image_pagesize;
    vni->supports_compression = 0;
    vni->supports_metadata    = 0;
    vni->is_raw               = 1;
    vni->changable_pagesize   = 1;	// change it at any time
    vni->changable_sectorsize = 1;	// change it at any time
    return 0;
}

static int split_raw_read(AFFILE *af, unsigned char *buf, uint64_t pos,size_t count)
{
    struct split_raw_private *srp = SPLIT_RAW_PRIVATE(af);
    off_t c3;
    int ret = 0;				// how many bytes read

    if ((af->image_size - pos) < (unsigned)count){
	count = (off_t)(af->image_size - pos);
    }

    while (count > 0) {
	int filenum = -1;
	off_t file_offset = 0;

	if (af->maxsize) {		// if we do file segments
	    filenum     = (int)(pos / af->maxsize);
	    file_offset = (off_t)(pos % af->maxsize);
	} else {
	    filenum     = 0;
	    file_offset = (off_t)pos;
	}
	if (file_offset != (off_t) srp->pos[filenum]) {
	    off_t c2 = lseek (srp->fds[filenum], file_offset, SEEK_SET);
	    if (file_offset != c2) {	// seek failed; return work to date
		if (ret) return ret;	// some bytes were read; return that
		else return -1;		// no bytes read; return error
	    }
	    srp->pos[filenum] = c2;	// this file starts here
	}
	if (af->maxsize && ((af->maxsize - file_offset) < (unsigned) count))
	    c3 = (off_t)(af->maxsize - file_offset);
	else
	    c3 = count;
	off_t c4 = read (srp->fds[filenum], buf, c3);
	if (c4 <= 0) {			// got an error
	    if (ret)	return ret;		// return how many bytes we read
	    else return -1;			// otherwise, return -1
	}
	buf += c4;
	count -= c4;
	ret += c4;
	pos += c4;
	srp->pos[filenum] += c4;	// position of this file pointer
	if (c3 != c4) return ret;		// incomplete?
    }
    return ret;
}

/*
 * split_raw_write_internal2:
 * If buf==0, assume we are writing zeros to the end of the file,
 * and just seek to the last character and write a single NUL.
 */

int split_raw_write_internal2(AFFILE *af, unsigned char *buf, uint64_t pos,size_t count)
{
    struct split_raw_private *srp = SPLIT_RAW_PRIVATE(af);
    off_t c1, c3;
    int i;
    int ret = 0;
    struct affcallback_info acbi;

    /* Setup the callback structure */
    memset(&acbi,0,sizeof(acbi));
    acbi.info_version = 1;
    acbi.af = af->parent ? af->parent : af;
    acbi.pagenum = af->image_pagesize ? pos / af->image_pagesize : 0;
    acbi.bytes_to_write = count;

    while (count > 0) {
	if (af->maxsize) {	// do we need to possibly split into multiple file writes?
	    /* Figure out which file number we will need to write to... */
	    if (pos >= (af->maxsize * srp->num_raw_files)) {
		int fd = open(srp->next_raw_fname, O_RDWR | O_CREAT | O_EXCL | O_BINARY, af->openmode);
		if (fd < 0) {
		  (*af->error_reporter)("split_raw_write: open(%s): ",af->fname);
		    if (ret) return ret;
		    else return -1;
		}
		srp_add_fd(af,fd);
		if (split_raw_increment_fname (srp->next_raw_fname) != 0) {
		  (*af->error_reporter)("split_raw_write: too many files\n");
		    if (ret)
			return ret;
		    else
			return -1;
		}
	    }
	    i  = (int)(pos / af->maxsize);
	    c1 = (off_t)(pos % af->maxsize);
	} else {
	    i = 0;
	    c1 = (off_t)pos;
	}
	if (c1 != (off_t)srp->pos[i]) {	// do we need to seek this file?
	    off_t c2 = lseek (srp->fds[i], c1, SEEK_SET); // try to seek
	    if (c1 != c2) {		// hm. Ended up in the wrong place. That's an error
		if (ret>0) {		// return how many bytes we got
		    return ret;
		}
		else {
		    return -1;
		}
	    }
	    srp->pos[i] = c2;
	}
	if (af->maxsize && ((af->maxsize - c1) < (unsigned)count))
	  c3 = (off_t)(af->maxsize - c1);
	else
	    c3 = count;
	if(af->w_callback) {acbi.phase = 3;(*af->w_callback)(&acbi);}

	/* WRITE THE DATA! */
	off_t c4 = 0;

	if(buf){
	    c4 = write (srp->fds[i], buf, c3);
	}
	else {
	    /* Extend with lseek() and write a single byte */
	    char z = 0;

	    lseek(srp->fds[i],c3-1,SEEK_CUR);
	    if(write(srp->fds[i],&z,1)!=1) return -1; // failure
	    c4 = c3;
	}

	/* DONE! */

	acbi.bytes_written = c4;
	if(af->w_callback) {acbi.phase = 4;(*af->w_callback)(&acbi);}
	if (c4 <= 0) {			// some error writing?
	    if (ret)
		return ret;
	    else
		return -1;
	}
	buf   += c4;
	count -= c4;
	ret   += c4;
	pos   += c4;
	srp->pos[i] += c4;
	if (af->image_size < pos) af->image_size = pos;	// image was extended
	if (c3 != c4){			// amount written doesn't equal request; return
	    return ret;
	}
    }
    return ret;
}

int split_raw_write(AFFILE *af, unsigned char *buf, uint64_t pos,size_t count)
{
  /* If we are being asked to start writing beyond the end of the file
   * pad out the file (and possibly create one or more new image files.)
   */

  if (af->maxsize) {
      if (pos > af->image_size) {        // writing beyond the end...
	  while(pos > af->image_size){

	      /* repeat until file is as big as where we should be writing */
	      int64_t bytes_left   = pos - af->image_size;
	      int bytes_to_write = (int)(af->maxsize - (af->image_size % af->maxsize));
	      if(bytes_to_write > bytes_left) bytes_to_write = (int)bytes_left;
	      int bytes_written = split_raw_write_internal2(af,0,af->image_size,bytes_to_write);
	      if(bytes_to_write != bytes_written){
		  return -1;		// some kind of internal error
	      }
	  }
      }
  }

  return split_raw_write_internal2 (af, buf, pos,count);
}



/* Get a segment; if a data page is being asked for, then fake it.
 * Otherwise, return an error.
 */

static int split_raw_get_seg(AFFILE *af,const char *name,uint32_t *arg,unsigned char *data,
		       size_t *datalen)
{
    int64_t page_num = af_segname_page_number(name);
    if(page_num<0){
	/* See if PAGESIZE or IMAGESIZE is being requested; we can fake those */
	if(strcmp(name,AF_PAGESIZE)==0){
	    if(arg) *arg = af->image_pagesize;
	    if(datalen) *datalen = 0;
	    return 0;
	}
	if(strcmp(name,AF_IMAGESIZE)==0){
	    struct aff_quad q;
	    if(data && *datalen>=8){
		q.low = htonl((uint32_t)(af->image_size & 0xffffffff));
		q.high = htonl((uint32_t)(af->image_size >> 32));
		memcpy(data,&q,8);
		*datalen = 8;
	    }
	    return 0;
	}
	if(strcmp(name,AF_SECTORSIZE)==0){
	    if(arg) *arg = af->image_sectorsize;
	    if(datalen) *datalen = 0;
	    return 0;
	}
	if(strcmp(name,AF_DEVICE_SECTORS)==0){
	    int64_t devicesectors = af->image_size / af->image_sectorsize;
	    struct aff_quad q;
	    if(data && *datalen>=8){
		q.low = htonl((uint32_t)(devicesectors & 0xffffffff));
		q.high = htonl((uint32_t)(devicesectors >> 32));
		memcpy(data,&q,8);
		*datalen = 8;
	    }
	    return 0;
	}
	errno = ENOTSUP;		// sorry! We don't store metadata
	return -1;
    }

    uint64_t pos = page_num * af->image_pagesize; // where we are to start reading
    uint64_t bytes_left = af->image_size - pos;	// how many bytes left in the file

    uint32_t bytes_to_read = af->image_pagesize; // copy this many bytes, unless
    if(bytes_to_read > bytes_left) bytes_to_read = (uint32_t)bytes_left; // only this much is left

    if(arg) *arg = 0;			// arg is always 0
    if(datalen){
	if(data==0){ // asked for 0 bytes, so give the actual size
	    *datalen = bytes_to_read;
	    return 0;
	}
	if(*datalen < (unsigned)bytes_to_read){
	    *datalen = bytes_to_read;
	    return AF_ERROR_DATASMALL;
	}
    }
    if(data){
	int bytes_read = split_raw_read(af,data,pos,bytes_to_read);
	if(bytes_read>=0){
	    if(datalen) *datalen = bytes_read;
	    return 0;
	}
	return -1;			// some kind of EOF?
    }
    return 0;				// no problems!
}

/*
 * split_raw_get_next_seg:
 * Try get_next_seg on the AFF file first. If that fails,
 * create the next virtual segment
 */

static int split_raw_get_next_seg(AFFILE *af,char *segname,size_t segname_len,uint32_t *arg,
				  unsigned char *data,size_t *datalen_)
{
    struct split_raw_private *srp = SPLIT_RAW_PRIVATE(af);

    int64_t total_pages = (af->image_size + af->image_pagesize - 1) / af->image_pagesize;
    if(srp->cur_page >= total_pages) return -1; // that's all there are

    /* Make the segment name */
    char pagename[AF_MAX_NAME_LEN];
    memset(pagename,0,sizeof(pagename));
    snprintf(pagename,sizeof(pagename),AF_PAGE,srp->cur_page++);

    /* Get the segment, if we can */
    int r = split_raw_get_seg(af,pagename,arg,data,datalen_);

    /* If r==0 and there is room for copying in the segment name, return it */
    if(r==0){
	if(strlen(pagename)+1 < segname_len){
	    strcpy(segname,pagename);
	    return 0;
	}
	/* segname wasn't big enough */
	return -2;
    }
    return r;				// some other error
}


/* Rewind all of the segments */
static int split_raw_rewind_seg(AFFILE *af)
{
    struct split_raw_private *srp = SPLIT_RAW_PRIVATE(af);
    srp->cur_page = 0;
    return 0;
}

static int split_raw_update_seg(AFFILE *af, const char *name,
                                uint32_t /*arg*/,const u_char *value,uint32_t vallen)

{
    int64_t page_num = af_segname_page_number(name);
    if(page_num<0){
	errno = ENOTSUP;		// sorry! We don't store metadata
	return -1;
    }

    uint64_t pos = page_num * af->image_pagesize; // where we are to start reading
    int written = split_raw_write(af, (unsigned char *)value, pos,vallen);
    if(written==(int)vallen) return 0;	// success
    return -1;
}


struct af_vnode vnode_split_raw = {
    AF_IDENTIFY_SPLIT_RAW,
    AF_VNODE_TYPE_COMPOUND|AF_VNODE_TYPE_RELIABLE|AF_VNODE_MAXSIZE_MULTIPLE|AF_VNODE_NO_SIGNING|AF_VNODE_NO_SEALING,
    "Split Raw",
    split_raw_identify_file,
    split_raw_open,
    split_raw_close,
    split_raw_vstat,
    split_raw_get_seg,			// get seg
    split_raw_get_next_seg,		// get_next_seg
    split_raw_rewind_seg,		// rewind_seg
    split_raw_update_seg,		// update_seg
    0,					// del_seg
    split_raw_read,			// read
    split_raw_write			// write
};


