/*
 * vnode_aff.cpp:
 *
 * Functions for the manipulation of AFF files...
 * Distributed under the Berkeley 4-part license
 */

#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"
#include "vnode_aff.h"
#include "aff_db.h"

#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif

#define xstr(s) str(s)
#define str(s) #s



static int      aff_write_ignore(AFFILE *af,size_t bytes);
static int	aff_write_seg(AFFILE *af,const char *name,uint32_t arg,
			      const u_char *value,size_t vallen);
static int	aff_get_seg(AFFILE *af,const char *name,uint32_t *arg,
			    unsigned char *data,size_t *datalen);
#ifdef KERNEL_LIBRARY
static int	aff_write_seg_no_data(AFFILE *af,const char *name,uint32_t arg, size_t vallen);
#endif
static int	aff_get_next_seg(AFFILE *af,char *segname,size_t segname_len,
				 uint32_t *arg, unsigned char *data, size_t *datalen);

/** aff_segment_overhead:
 * @param segname - the name of a segment
 * @return The number of bytes in the AFF file that the segment takes up without the data.
 */

int aff_segment_overhead(const char *segname)
{
    return sizeof(struct af_segment_head)+sizeof(struct af_segment_tail)+(segname?strlen(segname):0);
}

static int aff_write_ignore2(AFFILE *af,size_t bytes)
{
#ifdef KERNEL_LIBRARY
    aff_write_seg_no_data(af,AF_IGNORE,0,bytes);
#else
    if(af_trace) fprintf(af_trace,"aff_write_ignore2(%p,%d)\n",af,(int)bytes);
    unsigned char *invalidate_data = (unsigned char *)calloc(bytes,1);
    aff_write_seg(af,AF_IGNORE,0,invalidate_data,bytes); // overwrite with NULLs
    free(invalidate_data);
#endif
    return 0;
}

static int aff_write_ignore(AFFILE *af,size_t bytes)
{
    int64_t startpos = ftello(af->aseg);	// remember start position
    int r = 0;

    if(af_trace) fprintf(af_trace,"aff_write_ignore(%p,%d)\n",af,(int)bytes);

    /* First write the ignore */
    r = aff_write_ignore2(af,bytes);

    /* If the next one is also an ignore,
     * then we should go back and make the ignore_size bigger.
     * We could do this recursively,
     * but it's probably not worth the added complexity.
     */
    char next[AF_MAX_NAME_LEN];
    size_t segsize2=0;
    int count=0;
    while(af_probe_next_seg(af,next,sizeof(next),0,0,&segsize2,1)==0 && next[0]==0){
	count++;
	if(count>10) break;		// something is wrong; just get out.
	//printf("*** next %d segment at %qd len=%d will be deleted\n",count,ftello(af->aseg),segsize2);
	bytes += segsize2;
	fseeko(af->aseg,startpos,SEEK_SET);
	r = aff_write_ignore2(af,bytes);
	if(r!=0) return r;
    }

    /* See if the previous segment is also blank; if so, collapse them */
    fseeko(af->aseg,startpos,SEEK_SET);
    if(af_backspace(af)==0){
	uint64_t prev_segment_loc = ftello(af->aseg);	// remember where we are
	char   prev_segment_name[AF_MAX_NAME_LEN];
	size_t prev_segment_size=0;
	if(af_probe_next_seg(af,prev_segment_name,sizeof(prev_segment_name),0,0,&prev_segment_size,1)==0){
	    //printf("** prev segment name='%s' len=%d\n",prev_segment_name,prev_segment_size);
	    if(prev_segment_name[0]==0){
		bytes += prev_segment_size;
		fseeko(af->aseg,prev_segment_loc,SEEK_SET);
		r = aff_write_ignore2(af,bytes);
		fseeko(af->aseg,prev_segment_loc,SEEK_SET);
	    }
	}
    }

    return(r);
}


/* aff_write_seg:
 * put the given named segment at the current position in the file.
 * Return 0 for success, -1 for failure (probably disk full?)
 * This is the only place where a segment actually gets written
 */

int aff_write_seg(AFFILE *af, const char *segname,uint32_t arg,const u_char *data,size_t datalen)
{
    if(af_trace) fprintf(af_trace,"aff_write_seg(%p,%s,%" PRIu32 ",%p,len=%u)\n",
			 af,segname,arg,data,(int)datalen);

    struct af_segment_head segh;
    struct af_segment_tail segt;

    if(af->debug){
      (*af->error_reporter)("aff_write_seg(" POINTER_FMT ",'%s',%lu,data=" POINTER_FMT ",datalen=%u)",
			    af,segname,arg,data,datalen);
    }

    assert(sizeof(segh)==16);
    assert(sizeof(segt)==8);

    /* If the last command was not a probe (so we know where we are), and
     * we are not at the end of the file, something is very wrong.
     */

    uint32_t segname_len = strlen(segname);

    strcpy(segh.magic,AF_SEGHEAD);
    segh.name_len = htonl(segname_len);
    segh.data_len = htonl(datalen);
    segh.flag      = htonl(arg);

    strcpy(segt.magic,AF_SEGTAIL);
    segt.segment_len = htonl(sizeof(segh)+segname_len + datalen + sizeof(segt));
    aff_toc_update(af,segname,ftello(af->aseg),datalen);


    if(af_trace) fprintf(af_trace,"aff_write_seg: putting segment %s (datalen=%d) offset=%" PRId64 "\n",
			 segname,(int)datalen,ftello(af->aseg));

    if(fwrite(&segh,sizeof(segh),1,af->aseg)!=1) return -10;
    if(fwrite(segname,1,segname_len,af->aseg)!=segname_len) return -11;
    if(fwrite(data,1,datalen,af->aseg)!=datalen) return -12;
    if(fwrite(&segt,sizeof(segt),1,af->aseg)!=1) return -13;
    fflush(af->aseg);			// make sure it is on the disk
    return 0;
}


#ifdef KERNEL_LIBRARY
/* aff_write_seg_no_data:
 * put the given named segment at the current position in the file but don't write any data.
 * <km> this is an attempt at optimizing the write performance
 * Return 0 for success, -1 for failure (probably disk full?)
 */

int aff_write_seg_no_data(AFFILE *af, const char *segname,uint32_t arg,size_t datalen)
{
    struct af_segment_head segh;
    struct af_segment_tail segt;

    assert(sizeof(segh)==16);
    assert(sizeof(segt)==8);

    /* If the last command was not a probe (so we know where we are), and
     * we are not at the end of the file, something is very wrong.
     */

    uint32_t segname_len = strlen(segname);

    strcpy(segh.magic,AF_SEGHEAD);
    segh.name_len = htonl(segname_len);
    segh.data_len = htonl(datalen);
    segh.flag      = htonl(arg);

    strcpy(segt.magic,AF_SEGTAIL);
    segt.segment_len = htonl(sizeof(segh)+segname_len + datalen + sizeof(segt));
    aff_toc_update(af,segname,ftello(af->aseg),datalen);


    if(af_trace) fprintf(af_trace,"aff_write_seg: putting segment %s (datalen=%zd) offset=%"PRId64"\n",
			 segname,datalen,ftello(af->aseg));

    if(fwrite(&segh,sizeof(segh),1,af->aseg)!=1) return -10;
    if(fwrite(segname,1,segname_len,af->aseg)!=segname_len) return -11;
    //if(fwrite(data,1,datalen,af->aseg)!=datalen) return -12;
    if(fseeko(af->aseg,datalen,SEEK_CUR)!=0) return -12;
    if(fwrite(&segt,sizeof(segt),1,af->aseg)!=1) return -13;
    fflush(af->aseg);			// make sure it is on the disk
    return 0;
}
#endif


/****************************************************************
 *** low-level routines for reading
 ****************************************************************/

/* aff_get_segment:
 * Get the named segment, using the toc cache.
 */

static int aff_get_seg(AFFILE *af,const char *name,
		       uint32_t *arg,unsigned char *data,size_t *datalen)
{
    if(af_trace) fprintf(af_trace,"aff_get_seg(%p,%s,arg=%p,data=%p,datalen=%p)\n",af,name,arg,data,datalen);

    char next[AF_MAX_NAME_LEN];

    /* If the segment is in the directory, then seek the file to that location.
     * Otherwise, we'll probe the next segment, and if it is not there,
     * we will rewind to the beginning and go to the end.
     */
    struct aff_toc_mem *adm = aff_toc(af,name);
    if(!adm)
	{ errno = ENOENT; return -1; }

    if(!arg && !data && !datalen)
	return 0; // caller only wants to know whether the segment exists

    fseeko(af->aseg,adm->offset,SEEK_SET);
    int ret = aff_get_next_seg(af,next,sizeof(next),arg,data,datalen);
    assert(ret!=0 || strcmp(next,name)==0);	// hopefully this is what they asked for
    return ret;
}



/**
 * Get the next segment.
 * @param af          - The AFF file pointer
 * @param segname     - Array to hold the name of the segment.
 * @param segname_len - Available space in the segname array.
 * @param arg         - pointer to the arg
 * @param data        - pointer to the data
 * @param datalen_    - length of the data_ array. If *datalen_==0, set to the length of the data.
 *
 * @return
 *    0 =  success.
 *  -1  = end of file. (AF_ERROR_EOF)
 *  -2  = *data is not large enough to hold the segment (AF_ERROR_DATASMALL)
 *  -3  = af file is corrupt; no tail (AF_ERROR_TAIL)
 */
static int aff_get_next_seg(AFFILE *af,char *segname,size_t segname_len,uint32_t *arg,
			unsigned char *data,size_t *datalen_)
{
    if(af_trace) fprintf(af_trace,"aff_get_next_seg()\n");
    if(!af->aseg){
	snprintf(af->error_str,sizeof(af->error_str),"af_get_next_segv only works with aff files");
	return AF_ERROR_INVALID_ARG;
    }

    uint64_t start = ftello(af->aseg);
    size_t data_len;

    int r = af_probe_next_seg(af,segname,segname_len,arg,&data_len,0,0);
    if(r<0) return r;			// propigate error code
    if(data){				/* Read the data? */
	if(datalen_ == 0){
	    snprintf(af->error_str,sizeof(af->error_str),"af_get_next_seg: data provided but datalen is NULL");
	    return AF_ERROR_INVALID_ARG;
	}
	size_t read_size = data_len<=*datalen_ ? data_len : *datalen_;

	if(fread(data,1,read_size,af->aseg)!=read_size){
	    snprintf(af->error_str,sizeof(af->error_str),"af_get_next_segv: EOF on reading segment? File is corrupt.");
	    return AF_ERROR_SEGH;
	}
	if(data_len > *datalen_){
	    /* Read was incomplete;
	     * go back to the beginning of the segment and return
	     * the incomplete code.
	     */
	    fseeko(af->aseg,start,SEEK_SET);	// go back
	    errno = E2BIG;
	    return AF_ERROR_DATASMALL;
	}
    } else {
	fseeko(af->aseg,data_len,SEEK_CUR); // skip past the data
    }
    if(datalen_) *datalen_ = data_len;

    /* Now read the tail */
    struct af_segment_tail segt;
    memset(&segt,0,sizeof(segt));	// zero before reading
    if(fread(&segt,sizeof(segt),1,af->aseg)!=1){
	snprintf(af->error_str,sizeof(af->error_str),
		 "af_get_next_segv: AFF file is truncated (AF_ERROR_TAIL)");
	return AF_ERROR_TAIL;
    }
    /* Validate tail */
    uint32_t stl = ntohl(segt.segment_len);
    uint32_t calculated_segment_len =
	sizeof(struct af_segment_head)
	+ strlen(segname)
	+ data_len + sizeof(struct af_segment_tail);

    if(strcmp(segt.magic,AF_SEGTAIL)!=0){
	snprintf(af->error_str,sizeof(af->error_str),"af_get_next_segv: AFF file is truncated (AF_ERROR_TAIL).");
	fseeko(af->aseg,start,SEEK_SET); // go back to last good position
	return AF_ERROR_TAIL;
    }
    if(stl != calculated_segment_len){
	snprintf(af->error_str,sizeof(af->error_str),"af_get_next_segv: AFF file corrupt (%" PRIu32 "!=%" PRIu32 ")/!",
		 stl,calculated_segment_len);
	fseeko(af->aseg,start,SEEK_SET); // go back to last good position
	return AF_ERROR_TAIL;
    }
    return 0;
}


static int aff_rewind_seg(AFFILE *af)
{
    if(af_trace) fprintf(af_trace,"aff_rewind_seg()\n");
    fseeko(af->aseg,sizeof(struct af_head),SEEK_SET); // go to the beginning
    return 0;
}


/* Removes the last segment of an AFF file if it is blank.
 * @return 0 for success, -1 for error */
int af_truncate_blank(AFFILE *af)
{
    uint64_t last_loc = ftello(af->aseg);	// remember where we are
    if(af_backspace(af)==0){
	uint64_t backspace_loc = ftello(af->aseg);	// remember where we are
	char   next_segment_name[AF_MAX_NAME_LEN];
	if(af_probe_next_seg(af,next_segment_name,sizeof(next_segment_name),0,0,0,1)==0){
	    if(next_segment_name[0]==0){
		/* Remove it */
		fflush(af->aseg);
		if(ftruncate(fileno(af->aseg),backspace_loc)<0) return -1;
		return 0;
	    }
	}
    }
    fseeko(af->aseg,last_loc,SEEK_SET);	// return to where we were
    return -1;				// say that we couldn't do it.
}




/****************************************************************
 *** Update functions
 ****************************************************************/

/*
 * af_update_seg:
 * Update the given named segment with the new value.
 */

static int aff_update_seg(AFFILE *af, const char *name,
		    uint32_t arg,const u_char *value,uint32_t vallen)
{
    size_t size_needed = vallen+aff_segment_overhead(name);
    struct aff_toc_mem *adm = aff_toc(af,name);

    if(af_trace) fprintf(af_trace,"aff_update_seg(name=%s,arg=%" PRIu32 ",vallen=%u)\n",name,arg,vallen);

    if(adm)
    {
	/* segment already exists */
	if(fseeko(af->aseg, adm->offset, SEEK_SET) < 0)
	    return -1;

	/* if its size matches, just overwrite it */
	if(adm->segment_len == size_needed)
	    return aff_write_seg(af, name, arg, value, vallen);

	/* otherwise, invalidate it */
	if(aff_write_ignore(af, adm->segment_len - aff_segment_overhead(0)) < 0)
	    return -1;

	aff_toc_del(af, name);
    }

    /* search through TOC for a hole */
    /* need space for a new AF_IGNORE segment also */
    uint64_t hole_offset, hole_size;
    if(aff_toc_find_hole(af, size_needed + aff_segment_overhead(0), &hole_offset, &hole_size) == 0)
    {
	/* found a large enough hole */
	if(fseeko(af->aseg, hole_offset, SEEK_SET) < 0)
	    return -1;

	/* write segment */
	if(aff_write_seg(af, name, arg, value, vallen) < 0)
	    return -1;

	/* fill in any remaining space with AF_IGNORE */
	return aff_write_ignore(af, hole_size - size_needed - aff_segment_overhead(0));
    }

    /* no holes; seek to end of file and truncate any trailing AF_IGNORE */
    if(fseeko(af->aseg, 0, SEEK_END) < 0)
	return -1;

    while(af_truncate_blank(af) == 0) {}

    /* write segment at end of file */
    if(fseeko(af->aseg, 0, SEEK_END) < 0)
	return -1;

    return aff_write_seg(af, name, arg, value, vallen);
}



/* Delete the first occurance of the named segment.
 * Special case code: See if the segment being deleted
 * is the last segment. If it is, truncate the file...
 * This handles the case of AF_DIRECTORY and possibly other cases
 * as well...
 */

static int aff_del_seg(AFFILE *af,const char *segname)
{
    if(af_trace) fprintf(af_trace,"aff_del_seg(%p,%s)\n",af,segname);

    if(aff_toc_del(af,segname)){	// if del fails
	return 0;			// it's not present.
    }

    /* Find out if the last segment is the one we are deleting;
     * If so, we can just truncate the file.
     */
    char last_segname[AF_MAX_NAME_LEN];
    int64_t last_pos;
    af_last_seg(af,last_segname,sizeof(last_segname),&last_pos);
    if(strcmp(segname,last_segname)==0){
	fflush(af->aseg);		// flush any ouput
	if(ftruncate(fileno(af->aseg),last_pos)) return -1; // make the file shorter
	return 0;
    }

    size_t datasize=0,segsize=0;
    if(aff_find_seg(af,segname,0,&datasize,&segsize)!=0){
	return -1;			// nothing to delete?
    }
    /* Now wipe it out */
    size_t ignore_size = datasize+strlen(segname);
    aff_write_ignore(af,ignore_size);

    return 0;
}



#ifdef HAVE_OPENSSL_RAND_H
#include <openssl/rand.h>
#endif

/* aff_create:
 * af is an empty file that is being set up.
 */
static int aff_create(AFFILE *af)
{
    fwrite(AF_HEADER,1,8,af->aseg);  // writes the header
    aff_toc_build(af);	             // build the toc (will be pretty small)
    af_make_badflag(af);	     // writes the flag for bad blocks

    const char *version = xstr(PACKAGE_VERSION);
    aff_update_seg(af,AF_AFFLIB_VERSION,0,(const u_char *)version,strlen(version));

#ifdef HAVE_GETPROGNAME
    const char *progname = getprogname();
    if(aff_update_seg(af,AF_CREATOR,0,(const u_char *)progname,strlen(progname))) return -1;
#endif
    if(aff_update_seg(af,AF_AFF_FILE_TYPE,0,(const u_char *)"AFF",3)) return -1;

    return 0;
}


/****************************************************************
 *** VNODE implementation functions
 ****************************************************************/

/* Return 1 if a file is an AFF file */
static int aff_identify_file(const char *filename,int exists)
{
    if(af_is_filestream(filename)==0) return 0; // not a file stream
    if(strncmp(filename,"file://",7)==0){
	/* Move file pointer past file:// then find a '/' and take the next character  */
	filename += 7;
	while(*filename && *filename!='/'){
	    filename++;
	}
	/* At this point if *filename==0 then we never found the end of the URL.
	 * return 0, since it's not an AFF file.
	 */
	if(*filename==0) return 0;

	/* So *filename must == '/' */
	assert(*filename == '/');
	filename++;
    }

    if(exists && access(filename,R_OK)!=0) return 0;	// needs to exist and it doesn't
    int fd = open(filename,O_RDONLY | O_BINARY);
    if(fd<0){
	/* File doesn't exist. Is this an AFF name? */
	if(af_ext_is(filename,"aff")) return 1;
	return 0;
    }

    if(fd>0){
	int len = strlen(AF_HEADER)+1;
	char buf[64];
	int r = read(fd,buf,len);
	close(fd);
	if(r==len){			// if I could read the header
	    if(strcmp(buf,AF_HEADER)==0) return 1; // must be an AFF file
	    return 0;			// not an AFF file
	}
	/* If it is a zero-length file and the file extension ends AFF,
	 * then let it be an AFF file...
	 */
	if(r==0 && af_ext_is(filename,"aff")) return 1;
	return 0;			// must not be an aff file
    }
    return 0;
}

static int err_close(int fd)
{
    int tmp = errno;
    close(fd);
    errno = tmp;
    return -1;
}

static int err_close(AFFILE *af)
{
    int tmp = errno;
    fclose(af->aseg);
    af->aseg = 0;
    errno = tmp;
    return -1;
}

static int aff_open(AFFILE *af)
{
    // must be a file stream with read access
    if(!af_is_filestream(af->fname) || (af->openflags & O_ACCMODE) == O_WRONLY)
	{ errno = EINVAL; return -1; }

    bool canWrite = (af->openflags & O_ACCMODE) == O_RDWR;

    /* Open the raw file */
    int fd = open(af->fname,af->openflags | O_BINARY,af->openmode);
    if(fd < 0)
	return -1;

    /* Lock the file if writing */
#ifdef HAVE_FLOCK
    if(flock(fd, canWrite ? LOCK_EX : LOCK_SH) < 0)
	return err_close(fd);
#endif

    /* Open the FILE for the AFFILE */
    af->aseg = fdopen(fd, canWrite ? "w+b" : "rb");
    if(!af->aseg)
	return err_close(fd);

    /* Get file size */
#ifdef _WIN32
    struct _stat64 sb;
    if(_fstat64(fd, &sb) < 0)
	return err_close(af);
#else
    struct stat sb;
    if (fstat(fd, &sb) < 0)
        return err_close(af);
#endif

    /* If file is empty, then put out an AFF header, badflag, and AFF version */
    if(canWrite && sb.st_size == 0)
	return aff_create(af);

    /* We are opening an existing file. Verify once more than it is an AFF file
     * and skip past the header...
     */
    char buf[8]; errno = 0;
    size_t itemsRead = fread(buf, sizeof(buf), 1, af->aseg);
    if(itemsRead != 1 || strcmp(buf, AF_HEADER))
    {
	if(!errno)
	    errno = EIO;
	return err_close(af);
    }

    /* File has been validated */
    if(aff_toc_build(af) < 0)
	return err_close(af);

    return 0;
}


/*
 * aff_close:
 * If the imagesize changed, write out a new value.
 */
static int aff_close(AFFILE *af)
{
    aff_toc_free(af);
    fclose(af->aseg);
    return 0;
}


static int aff_vstat(AFFILE *af,struct af_vnode_info *vni)
{
    memset(vni,0,sizeof(*vni));		// clear it
    vni->imagesize = af->image_size;	// we can just return this
    vni->pagesize = af->image_pagesize;
    vni->supports_compression = 1;
    vni->has_pages            = 1;
    vni->supports_metadata    = 1;
    vni->cannot_decrypt       = af_cannot_decrypt(af) ? 1 : 0;

    /* Check for an encrypted page */
    if(af->toc){
	for(int i=0;i<af->toc_count;i++){
	    if(af->toc[i].name){
		bool is_page = false;
		vni->segment_count_total++;
		if(af_segname_page_number(af->toc[i].name)>=0){
		    vni->page_count_total++;
		    is_page = true;
		}
		if(af_is_encrypted_segment(af->toc[i].name)){
		    vni->segment_count_encrypted++;
		    if(is_page) vni->page_count_encrypted++;
		}
		if(af_is_signature_segment(af->toc[i].name)){
		    vni->segment_count_signed++;
		}
	    }
	}
    }
    return 0;
}


struct af_vnode vnode_aff = {
    AF_IDENTIFY_AFF,
    AF_VNODE_TYPE_PRIMITIVE|AF_VNODE_TYPE_RELIABLE,
    "AFF",
    aff_identify_file,
    aff_open,
    aff_close,
    aff_vstat,
    aff_get_seg,
    aff_get_next_seg,
    aff_rewind_seg,
    aff_update_seg,
    aff_del_seg,
    0,				// read; keep 0
    0				// write
};

