/*
 * The AFFLIB page abstraction.
 * Distributed under the Berkeley 4-part license
 */

#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"


/* af_read_sizes:
 * Get the page sizes if they are set in the file.
 */
void af_read_sizes(AFFILE *af)
{
    af_get_seg(af,AF_SECTORSIZE,&af->image_sectorsize,0,0);
    if(af->image_sectorsize==0) af->image_sectorsize = 512; // reasonable default

    if(af_get_seg(af,AF_PAGESIZE,&af->image_pagesize,0,0)){
	af_get_seg(af,AF_SEGSIZE_D,&af->image_pagesize,0,0); // try old name
    }

    /* Read the badflag if it is present.
     * Be sure to adjust badflag size to current sector size (which may have changed).
     */
    if(af->badflag!=0) free(af->badflag);
    af->badflag = (unsigned char *)malloc(af->image_sectorsize);
    size_t sectorsize = af->image_sectorsize;
    if(af_get_seg(af,AF_BADFLAG,0,af->badflag,(size_t *)&sectorsize)==0){
	af->badflag_set = 1;
    }

    /* Read the image file segment if it is present.
     * If it isn't, scan through the disk image to figure out the size of the disk image.
     */

    if(af_get_segq(af,AF_IMAGESIZE,(int64_t *)&af->image_size)){

	/* Calculate the imagesize by scanning all of the pages that are in
	 * the disk image and finding the highest page number.
	 * Then read that page to find the last allocated byte.
	 */
	char segname[AF_MAX_NAME_LEN];
	size_t datalen = 0;
	af_rewind_seg(af);		//  start at the beginning
	int64_t highest_page_number = 0;
	while(af_get_next_seg(af,segname,sizeof(segname),0,0,&datalen)==0){
	    if(segname[0]==0) continue;	// ignore sector
	    int64_t pagenum = af_segname_page_number(segname);
	    if(pagenum > highest_page_number) highest_page_number = pagenum;
	}
	size_t highest_page_len = 0;
	if(af_get_page(af,highest_page_number,0,&highest_page_len)==0){
	    af->image_size = af->image_pagesize * highest_page_number + highest_page_len;
	}
    }
    af->image_size_in_file = af->image_size;
}


int af_page_size(AFFILE *af)
{
    return af->image_pagesize;
}

int af_get_pagesize(AFFILE *af)
{
    return af->image_pagesize;
}

/* af_set_sectorsize:
 * Sets the sectorsize.
 * Fails with -1 if imagesize >=0 unless these changes permitted
 */
int af_set_sectorsize(AFFILE *af,int sectorsize)
{
    struct af_vnode_info vni;
    af_vstat(af,&vni);
    if(vni.changable_pagesize==0 && af->image_size>0){
	errno = EINVAL;
	return -1;
    }
    af->image_sectorsize =sectorsize;
    if(af->badflag==0) af->badflag = (unsigned char *)malloc(sectorsize);
    else af->badflag = (unsigned char *)realloc(af->badflag,sectorsize);
    af->badflag_set = 0;

    if(af_update_seg(af,AF_SECTORSIZE,sectorsize,0,0)){
	if(errno != ENOTSUP) return -1;
    }
    return 0;
}

int	af_get_sectorsize(AFFILE *af)	// returns sector size
{
    return af->image_sectorsize;
}

/*
 * af_set_pagesize:
 * Sets the pagesize. Fails with -1 if it can't be changed.
 */
int af_set_pagesize(AFFILE *af,uint32_t pagesize)
{
    /* Allow the pagesize to be changed if it hasn't been set yet
     * and if this format doesn't support metadata updating (which is the raw formats)
     */
    struct af_vnode_info vni;

    af_vstat(af,&vni);

    if(vni.changable_pagesize==0 && af->image_size>0){
	if(pagesize==af->image_pagesize) return 0; // it's already set to this, so let it pass
	errno = EINVAL;
	return -1;
    }
    if(pagesize % af->image_sectorsize != 0){
	(*af->error_reporter)("Cannot set pagesize to %d (sectorsize=%d)\n",
			      pagesize,af->image_sectorsize);
	errno = EINVAL;
	return -1;
    }

    af->image_pagesize = pagesize;
    if(af_update_seg(af,AF_PAGESIZE,pagesize,0,0)){
	if(errno != ENOTSUP) return -1;	// error updating (don't report ENOTSUP);
    }
    return 0;
}


/****************************************************************
 *** page-level interface
 ****************************************************************/

int af_get_page_raw(AFFILE *af,int64_t pagenum,uint32_t *arg,
		    unsigned char *data,size_t *bytes)
{
    char segname[AF_MAX_NAME_LEN];

    memset(segname,0,sizeof(segname));
    sprintf(segname,AF_PAGE,pagenum);
    int r = af_get_seg(af,segname,arg,data,bytes);
    if(r < 0 && errno == ENOENT)
    {
	/* Couldn't read with AF_PAGE; try AF_SEG_D.
	 * This is legacy for the old AFF files. Perhaps we should delete it.
	 */
	sprintf(segname,AF_SEG_D,pagenum);
	r = af_get_seg(af,segname,arg,data,bytes);
    }
    /* Update the counters */
    if(r==0 && bytes && *bytes>0) af->pages_read++; // note that we read a page
    return r;
}

/* af_get_page:
 * Get a page from its named segment.
 * If the page is compressed, uncompress it.
 * data points to a segmenet of at least *bytes;
 * *bytes is then modified to indicate the actual amount of bytes read.
 * Return 0 if success, -1 if fail.
 */

int af_get_page(AFFILE *af,int64_t pagenum,unsigned char *data,size_t *bytes)
{
    uint32_t arg=0;
    size_t page_len=0;

    if (af_trace){
	fprintf(af_trace,"af_get_page(%p,pagenum=%" I64d ",buf=%p,bytes=%u)\n",af,pagenum,data,(int)*bytes);
    }

    /* Find out the size of the segment and if it is compressed or not.
     * If we can't find it with new nomenclature, try the old one...
     */
    int r = af_get_page_raw(af,pagenum,&arg,0,&page_len);
    if(r){
	/* Segment doesn't exist.
	 * If we have been provided with a buffer,
	 * fill buffer with the 'bad segment' flag and return.
	 */
	if(data && (af->openmode & AF_BADBLOCK_FILL) && errno == ENOENT)
	{
	    for(size_t i = 0;i <= af->image_pagesize - af->image_sectorsize;
		i+= af->image_sectorsize){
		memcpy(data+i,af->badflag,af->image_sectorsize);
		af->bytes_memcpy += af->image_sectorsize;
	    }

	    r = 0;
	}
	return r;		// segment doesn't exist
    }


    /* If the segment isn't compressed, just get it*/
    uint32_t pageflag = 0;
    if((arg & AF_PAGE_COMPRESSED)==0){
	if(data==0){			// if no data provided, just return size of the segment if requested
	    if(bytes) *bytes = page_len;	// set the number of bytes in the page if requested
	    return 0;
	}
	int ret = af_get_page_raw(af,pagenum,&pageflag,data,bytes);
	if(*bytes > page_len) *bytes = page_len; // we only read this much
	if(ret!=0) return ret;		// some error happened?
    }
    else {
	/* Allocate memory to hold the compressed segment */
	unsigned char *compressed_data = (unsigned char *)malloc(page_len);
	size_t compressed_data_len = page_len;
	if(compressed_data==0){
	    return -2;			// memory error
	}

	/* Get the data */
	if(af_get_page_raw(af,pagenum,&pageflag,compressed_data,&compressed_data_len)){
	    free(compressed_data);
	    return -3;			// read error
	}

	/* Sanity check to avoid undefined behaviour when calling malloc below with pagesize from a corrupt AFF image. */
	if(af->image_pagesize <= 0 || af->image_pagesize > 16*1024*1024)
	    return -1;


	/* Now uncompress directly into the buffer provided by the caller, unless the caller didn't
	 * provide a buffer. If that happens, allocate our own...
	 */
	int res = -1;			// 0 is success
	bool free_data = false;
	if(data==0){
	    data = (unsigned char *)malloc(af->image_pagesize);
	    free_data = true;
	    *bytes = af->image_pagesize; // I can hold this much
	}

	switch((pageflag & AF_PAGE_COMP_ALG_MASK)){
	case AF_PAGE_COMP_ALG_ZERO:
	    if(compressed_data_len != 4){
		(*af->error_reporter)("ALG_ZERO compressed data is %d bytes, expected 4.",compressed_data_len);
		break;
	    }
	    memset(data,0,af->image_pagesize);
	    *bytes = ntohl(*(long *)compressed_data);
	    res = 0;			// not very hard to decompress with the ZERO compressor.
	    break;

	case AF_PAGE_COMP_ALG_ZLIB:
	    res = uncompress(data,(uLongf *)bytes,compressed_data,compressed_data_len);
	    switch(res){
	    case Z_OK:
		break;
	    case Z_ERRNO:
		(*af->error_reporter)("Z_ERRNOR decompressing segment %" I64d,pagenum);
	    case Z_STREAM_ERROR:
		(*af->error_reporter)("Z_STREAM_ERROR decompressing segment %" I64d,pagenum);
	    case Z_DATA_ERROR:
		(*af->error_reporter)("Z_DATA_ERROR decompressing segment %" I64d,pagenum);
	    case Z_MEM_ERROR:
		(*af->error_reporter)("Z_MEM_ERROR decompressing segment %" I64d,pagenum);
	    case Z_BUF_ERROR:
		(*af->error_reporter)("Z_BUF_ERROR decompressing segment %" I64d,pagenum);
	    case Z_VERSION_ERROR:
		(*af->error_reporter)("Z_VERSION_ERROR decompressing segment %" I64d,pagenum);
	    default:
		(*af->error_reporter)("uncompress returned an invalid value in get_segment");
	    }
	    break;

#ifdef USE_LZMA
	case AF_PAGE_COMP_ALG_LZMA:
	    res = lzma_uncompress(data,bytes,compressed_data,compressed_data_len);
	    if (af_trace) fprintf(af_trace,"   LZMA decompressed page %" I64d ". %d bytes => %u bytes\n",
				  pagenum,(int)compressed_data_len,(int)*bytes);
	    switch(res){
	    case 0:break;		// OK
	    case 1:(*af->error_reporter)("LZMA header error decompressing segment %" I64d "\n",pagenum);
		break;
	    case 2:(*af->error_reporter)("LZMA memory error decompressing segment %" I64d "\n",pagenum);
		break;
	    }
	    break;
#endif

	default:
	    (*af->error_reporter)("Unknown compression algorithm 0x%d",
				  pageflag & AF_PAGE_COMP_ALG_MASK);
	    break;
	}

	if(free_data){
	    free(data);
	    data = 0;			// restore the way it was
	}
	free(compressed_data);		// don't need this one anymore
	af->pages_decompressed++;
	if(res!=Z_OK) return -1;
    }

    /* If the page size is larger than the sector_size,
     * make sure that the rest of the sector is zeroed, and that the
     * rest after that has the 'bad block' notation.
     */
    if(data && (af->image_pagesize > af->image_sectorsize)){
	const int SECTOR_SIZE = af->image_sectorsize;	// for ease of typing
	size_t bytes_left_in_sector = (SECTOR_SIZE - (*bytes % SECTOR_SIZE)) % SECTOR_SIZE;
	for(size_t i=0;i<bytes_left_in_sector;i++){
	    data[*bytes + i] = 0;
	}
	size_t end_of_data = *bytes + bytes_left_in_sector;

	/* Now fill to the end of the page... */
	for(size_t i = end_of_data; i <= af->image_pagesize-SECTOR_SIZE; i+=SECTOR_SIZE){
	    memcpy(data+i,af->badflag,SECTOR_SIZE);
	    af->bytes_memcpy += SECTOR_SIZE;
	}
    }
    return 0;
}


static bool is_buffer_zero(unsigned char *buf,int buflen)
{
    if(buflen >= (int)sizeof(long))
    {
        // align to word boundary
        buflen -= (intptr_t)buf % sizeof(long);

        while((intptr_t)buf % sizeof(long))
        {
            if(*buf++)
                return false;
        }

        // read in words
        long *ptr = (long*)buf;
        buf += buflen - buflen % sizeof(long);
        buflen %= sizeof(long);

        while(ptr < (long*)buf)
        {
            if(*ptr++)
                return false;
        }
    }

    while(buflen--)
    {
        if(*buf++)
            return false;
    }

    return true;
}

/* Write a actual data segment to the disk and sign if necessary. */
int af_update_page(AFFILE *af,int64_t pagenum,unsigned char *data,int datalen)
{
    char segname_buf[32];
    snprintf(segname_buf,sizeof(segname_buf),AF_PAGE,pagenum); // determine segment name

#ifdef USE_AFFSIGS
    /* Write out the signature if we have a private key */
    if(af->crypto && af->crypto->sign_privkey){
	af_sign_seg3(af,segname_buf,0,data,datalen,AF_SIGNATURE_MODE1);
    }
#endif

#ifdef HAVE_MD5
    /* Write out MD5 if requested */
    if(af->write_md5){
	unsigned char md5_buf[16];
	char md5name_buf[32];
	MD5(data,datalen,md5_buf);
	snprintf(md5name_buf,sizeof(md5name_buf),AF_PAGE_MD5,pagenum);
	af_update_segf(af,md5name_buf,0,md5_buf,sizeof(md5_buf),AF_SIGFLAG_NOSIG); // ignore failure
    }
#endif
#ifdef HAVE_SHA1
    /* Write out SHA1 if requested */
    if(af->write_sha1){
	unsigned char sha1_buf[20];
	char sha1name_buf[32];
	SHA1(data,datalen,sha1_buf);
	snprintf(sha1name_buf,sizeof(sha1name_buf),AF_PAGE_SHA1,pagenum);
	af_update_segf(af,sha1name_buf,0,sha1_buf,sizeof(sha1_buf),AF_SIGFLAG_NOSIG); // ignore failure
    }
#endif
    /* Write out SHA256 if requested and if SHA256 is available */
    if(af->write_sha256){
	unsigned char sha256_buf[32];
	if(af_SHA256(data,datalen,sha256_buf)==0){
	    char sha256name_buf[32];
	    snprintf(sha256name_buf,sizeof(sha256name_buf),AF_PAGE_SHA256,pagenum);
	    af_update_segf(af,sha256name_buf,0,sha256_buf,sizeof(sha256_buf),AF_SIGFLAG_NOSIG); // ignore failure
	}
    }

    /* Check for bypass */
    if(af->v->write){
	int r = (*af->v->write)(af,data,af->image_pagesize * pagenum,datalen);
	if(r!=datalen) return -1;
	return 0;
    }

    struct affcallback_info acbi;
    int ret = 0;
    uint64_t starting_pages_written = af->pages_written;

    /* Setup the callback structure */
    memset(&acbi,0,sizeof(acbi));
    acbi.info_version = 1;
    acbi.af = af->parent ? af->parent : af;
    acbi.pagenum = pagenum;
    acbi.bytes_to_write = datalen;

    size_t destLen = af->image_pagesize;	// it could be this big.

    /* Compress and write the data, if we are allowed to compress */
    if(af->compression_type != AF_COMPRESSION_ALG_NONE){
	unsigned char *cdata = (unsigned char *)malloc(destLen); // compressed data
	uint32_t *ldata = (uint32_t *)cdata; // allows me to reference as a buffer of uint32_ts
	if(cdata!=0){		// If data could be allocated
	    int cres = -1;		// compression results
	    uint32_t flag = 0;	// flag for data segment
	    int dont_compress = 0;

	    /* Try zero compression first; it's the best algorithm we have  */
	    if(is_buffer_zero(data,datalen)){
		acbi.compression_alg   = AF_PAGE_COMP_ALG_ZERO;
		acbi.compression_level = AF_COMPRESSION_MAX;

		if(af->w_callback) { acbi.phase = 1; (*af->w_callback)(&acbi); }

		*ldata = htonl(datalen); // store the data length
		destLen = 4;		 // 4 bytes
		flag = AF_PAGE_COMPRESSED | AF_PAGE_COMP_ALG_ZERO | AF_PAGE_COMP_MAX;
		cres = 0;

		acbi.compressed = 1;		// it was compressed
		if(af->w_callback) {acbi.phase = 2;(*af->w_callback)(&acbi);}
	    }

#ifdef USE_LZMA
	    if(cres!=0 && af->compression_type==AF_COMPRESSION_ALG_LZMA){ // try to compress with LZMA
		acbi.compression_alg   = AF_PAGE_COMP_ALG_LZMA;
		acbi.compression_level = 7; // right now, this is the level we use
		if(af->w_callback) { acbi.phase = 1; (*af->w_callback)(&acbi); }

		cres = lzma_compress(cdata,&destLen,data,datalen,9);
#if 0
		switch(cres){
		case 0:break;		// OKAY
		case 1: (*af->error_reporter)("LZMA: Unspecified Error\n");break;
		case 2: (*af->error_reporter)("LZMA: Memory Allocating Error\n");break;
		case 3: (*af->error_reporter)("LZMA: Output buffer OVERFLOW\n"); break;
		default: (*af->error_reporter)("LZMA: Unknown error %d\n",cres);break;
		}
#endif
		if(cres==0){
		    flag = AF_PAGE_COMPRESSED | AF_PAGE_COMP_ALG_LZMA;
		    acbi.compressed = 1;
		    if(af->w_callback) {acbi.phase = 2;(*af->w_callback)(&acbi);}
		}
		else {
		    /* Don't bother reporting LZMA errors; we just won't compress */
		    dont_compress = 1;
		    if(af->w_callback) {acbi.phase = 2;(*af->w_callback)(&acbi);}
		}
	    }
#endif

	    if(cres!=0
	       && af->compression_type==AF_COMPRESSION_ALG_ZLIB
	       && dont_compress==0){ // try to compress with zlib
		acbi.compression_alg   = AF_PAGE_COMP_ALG_ZLIB; // only one that we support
		acbi.compression_level = af->compression_level;
		if(af->w_callback) { acbi.phase = 1; (*af->w_callback)(&acbi); }

		cres = compress2((Bytef *)cdata, (uLongf *)&destLen,
				 (Bytef *)data,datalen, af->compression_level);

		if(cres==0){
		    flag = AF_PAGE_COMPRESSED | AF_PAGE_COMP_ALG_ZLIB;
		    if(af->compression_level == AF_COMPRESSION_MAX){
			flag |= AF_PAGE_COMP_MAX; // useful to know it can't be better
		    }
		}
		acbi.compressed = 1;	// it was compressed (or not compressed)
		if(af->w_callback) {acbi.phase = 2;(*af->w_callback)(&acbi);}
	    }

	    if(cres==0 && destLen < af->image_pagesize){
		/* Prepare to write out the compressed segment with compression */
		if(af->w_callback) {acbi.phase = 3;(*af->w_callback)(&acbi);}
		ret = af_update_segf(af,segname_buf,flag,cdata,destLen,AF_SIGFLAG_NOSIG);
		acbi.bytes_written = destLen;
		if(af->w_callback) {acbi.phase = 4;(*af->w_callback)(&acbi);}
		if(ret==0){
		    af->pages_written++;
		    af->pages_compressed++;
		}
	    }
	    free(cdata);
	    cdata = 0;
	}
    }

    /* If a compressed segment was not written, write it uncompressed */
    if(af->pages_written == starting_pages_written){
	if(af->w_callback) {acbi.phase = 3;(*af->w_callback)(&acbi);}
	ret = af_update_segf(af,segname_buf,0,data,datalen,AF_SIGFLAG_NOSIG);
	acbi.bytes_written = datalen;
	if(af->w_callback) {acbi.phase = 4;(*af->w_callback)(&acbi);}
	if(ret==0){
	    acbi.bytes_written = datalen;	// because that is how much we wrote
	    af->pages_written++;
	}
    }
    return ret;
}

/****************************************************************
 *** Cache interface
 ****************************************************************/

/* The page cache is a read/write cache.
 *
 * Pages that are read are cached after they are decompressed.
 * When new pages are fetched, we check the cache first to see if they are there;
 * if so, they are satsfied by the cache.
 *
 * Modifications are written to the cache, then dumped to the disk.
 *
 * The cache is managed by two functions:
 * af_cache_flush(af) - (prevously af_purge)
 *      - Makes sure that all dirty buffers are written.
 *      - Sets af->pb=NULL (no current page)
 *      - (returns 0 if success, -1 if failure.)
 *
 * af_cache_writethrough(af,page,buf,buflen)
 *      - used for write bypass
 *
 */

static int cachetime = 0;


int af_cache_flush(AFFILE *af)
{
    if(af_trace) fprintf(af_trace,"af_cache_flush()\n");
    int ret = 0;
    for(int i=0;i<af->num_pbufs;i++){
	struct aff_pagebuf *p = &af->pbcache[i];
	if(p->pagebuf_valid && p->pagebuf_dirty){
	    if(af_update_page(af,p->pagenum,p->pagebuf,p->pagebuf_bytes)){
		ret = -1;		// got an error; keep going, though
	    }
	    p->pagebuf_dirty = 0;
	    if(af_trace) fprintf(af_trace,"af_cache_flush: slot %d page %" PRIu64 " flushed.\n",i,p->pagenum);
	}
    }
    return ret;				// now return the error that I might have gotten
}

/* If the page being written is in the cache, update it.
 * Question: would it make sense to copy the data anyway? I don't think so, because
 * the main use of writethrough is when imaging, and in that event you probably don't
 * want the extra memcpy.
 */
void af_cache_writethrough(AFFILE *af,int64_t pagenum,const unsigned char *buf,int bufflen)
{
    for(int i=0;i<af->num_pbufs;i++){
	struct aff_pagebuf *p = &af->pbcache[i];
	if(p->pagenum_valid && p->pagenum == pagenum){
	    if(p->pagebuf_dirty){
		(*af->error_reporter)("af_cache_writethrough: overwriting page %" I64u ".\n",pagenum);
		exit(-1);		// this shouldn't happen
	    }
	    memcpy(p->pagebuf,buf,bufflen);
	    memset(p->pagebuf+bufflen,0,af->image_pagesize-bufflen); // zero fill the rest
	    af->bytes_memcpy += bufflen;
	    p->pagebuf_valid = 1;	// we have a copy of it now.
	    p->pagebuf_dirty = 0;	// but it isn't dirty
	    p->last = cachetime++;
	}
    }
}

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#ifndef HAVE_VALLOC
#define valloc malloc
#endif

struct aff_pagebuf *af_cache_alloc(AFFILE *af,int64_t pagenum)
{
    if(af_trace) fprintf(af_trace,"af_cache_alloc(%p,%" I64d ")\n",af,pagenum);

    /* Make sure nothing in the cache is dirty */
    if(af_cache_flush(af) < 0)
	return 0;

    /* See if this page is already in the cache */
    for(int i=0;i<af->num_pbufs;i++){
	struct aff_pagebuf *p = &af->pbcache[i];
	if(p->pagenum_valid && p->pagenum==pagenum){
	    af->cache_hits++;
	    if(af_trace) fprintf(af_trace,"  page %" I64d " satisfied fromcache\n",pagenum);
	    p->last = cachetime++;
	    return p;
	}
    }

    af->cache_misses++;
    int slot = -1;
    /* See if there is an empty slot in the cache */
    for(int i=0;i<af->num_pbufs;i++){
	struct aff_pagebuf *p = &af->pbcache[i];
	if(p->pagenum_valid==0){
	    slot = i;
	    if(af_trace) fprintf(af_trace,"  slot %d given to page %" I64d "\n",slot,pagenum);
	    break;
	}
    }
    if(slot==-1){
	/* Find the oldest cache entry */
	int oldest_i = 0;
	int oldest_t = af->pbcache[0].last;
	for(int i=1;i<af->num_pbufs;i++){
	    if(af->pbcache[i].last < oldest_t){
		oldest_t = af->pbcache[i].last;
		oldest_i = i;
	    }
	}
	slot = oldest_i;
	if(af_trace) fprintf(af_trace,"  slot %d assigned to page %" I64d "\n",slot,pagenum);
    }
    /* take over this slot */
    struct aff_pagebuf *p = &af->pbcache[slot];
    if(p->pagebuf==0){
	p->pagebuf = (unsigned char *)valloc(af->image_pagesize); // allocate to a page boundary
	if(p->pagebuf==0){
	    /* Malloc failed; See if we can just use the first slot */
	    slot = 0;
	    if(af->pbcache[0].pagebuf==0) return 0; // ugh. Cannot malloc?

	    /* First slot is available. Just use it. */
	    p = &af->pbcache[0];
	}
    }
    memset(p->pagebuf,0,af->image_pagesize); // clean object reuse
    p->pagenum = pagenum;
    p->pagenum_valid = 1;
    p->pagebuf_valid = 0;
    p->pagebuf_dirty = 0;
    p->last = cachetime++;
    if(af_trace){
	fprintf(af_trace,"   current pages in cache: ");
	for(int i=0;i<af->num_pbufs;i++){
	    fprintf(af_trace," %" I64d,af->pbcache[i].pagenum);
	}
	fprintf(af_trace,"\n");
    }
    return p;
}




