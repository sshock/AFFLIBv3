#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"
#include "vnode_raw.h"

/*
 * Distributed under the Berkeley 4-part license
 */

/* the RAW_PAGESIZE is visible outside the module, but it's kind of irrevellant */
#define RAW_PAGESIZE 16*1024*1024

/* raw file implementation */
struct raw_private {
    /* For Raw files */
    FILE *raw;				// if it is a raw file
    int raw_popen;			// opened with popen
};

#define RAW_PRIVATE(af) ((struct raw_private *)(af->vnodeprivate))

/* Return 1 if a file is a raw file... */
static int raw_identify_file(const char *filename,int /*exists*/)
{
    return af_ext_is(filename, "raw") || af_ext_is(filename, "iso");
}


/* Return the size of the raw file */
static int64_t raw_filesize(AFFILE *af)
{
    struct raw_private *rp = RAW_PRIVATE(af);

#ifdef _WIN32
    struct _stat64 sb;
    if (_fstat64(fileno(rp->raw), &sb) ==0) {
#else
    struct stat sb;
    if(fstat(fileno(rp->raw),&sb)==0){
#endif
	if(sb.st_mode & S_IFREG){	// only do this for regular files
	    return sb.st_size;
	}

	/* See if this is a device that we can figure */
	struct af_figure_media_buf afb;
	if(af_figure_media(fileno(rp->raw),&afb)==0){
	    if(afb.total_sectors>0 && afb.sector_size>0){
		return afb.total_sectors * afb.sector_size;
	    }
	}
    }
    return 0;				// no clue
}

static int raw_open(AFFILE *af)
{
    /* Raw is the passthrough system.
     */
    int fd = open(af->fname, af->openflags | O_BINARY, af->openmode);
    if(fd < 0)
        return -1;

    int accmode = (af->openflags & O_ACCMODE);
    FILE *file = fdopen(fd, (accmode == O_RDWR || accmode == O_WRONLY) ? "r+b" : "rb");
    if(!file)
    {
        close(fd);
        return -1;
    }

    af->vnodeprivate = (void *)calloc(1,sizeof(struct raw_private));
    struct raw_private *rp = RAW_PRIVATE(af);
    rp->raw = file;
    af->image_size	= raw_filesize(af);
    af->image_pagesize	= RAW_PAGESIZE;
    af->cur_page	= 0;
    return 0;
}

int raw_freopen(AFFILE *af,FILE *file)
{
    af->fname = 0;
    af->vnodeprivate = (void *)calloc(1,sizeof(struct raw_private));
    struct raw_private *rp = RAW_PRIVATE(af);
    rp->raw = file;
    af->image_size = raw_filesize(af);
    af->image_pagesize = RAW_PAGESIZE;
    af->cur_page = 0;
    return 0;
}


int raw_popen(AFFILE *af,const char *command,const char *type)
{
#ifdef HAVE_POPEN
    if(strcmp(type,"r")!=0){
	(*af->error_reporter)("af_popen: only type 'r' supported");
	return -1;
    }
    /* If shell metacharacters exist in command, don't open it */
    if(af_hasmeta(command)){
	(*af->error_reporter)("raw_popen: invalid shell metacharacters in command '%s'",
			      command);
	return -1;
    }
    af->fname = 0;
    af->vnodeprivate = (void *)calloc(1,sizeof(struct raw_private));
    struct raw_private *rp = RAW_PRIVATE(af);
    rp->raw = popen(command,"r");
    rp->raw_popen = 1;
    return 0;
#else
    (*af->error_reporter)("af_popen: popen not supported on this platform.");
    return -1;
#endif
}


static int raw_close(AFFILE *af)
{
    struct raw_private *rp = RAW_PRIVATE(af);

    if(rp->raw_popen){
#ifdef HAVE_POPEN
	pclose(rp->raw);
#endif
    }
    else {
	fclose(rp->raw);
    }
    memset(rp,0,sizeof(*rp));		// clean object reuse
    free(rp);				// won't need it again
    return 0;
}

static int raw_get_seg(AFFILE *af,const char *name,
		       uint32_t *arg,unsigned char *data,size_t *datalen)
{
    struct raw_private *rp = RAW_PRIVATE(af);

    int64_t segnum = af_segname_page_number(name);
    if(segnum<0){
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

	return -1;		// don't know how to fake this
    }

    fflush(rp->raw);			// make sure that any buffers are flushed

    int64_t pos = (int64_t)segnum * af->image_pagesize; // where we are to start reading
    int64_t bytes_left = af->image_size - pos;	// how many bytes left in the file

    if(bytes_left<0) bytes_left = 0;

    int bytes_to_read = af->image_pagesize; // copy this many bytes, unless
    if(bytes_to_read > bytes_left) bytes_to_read = bytes_left; // only this much is left

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
	fseeko(rp->raw,pos,SEEK_SET);
	int bytes_read = fread(data,1,bytes_to_read,rp->raw);
	if(bytes_read==bytes_to_read){
	    if(datalen) *datalen = bytes_read;
	    return 0;
	}
	return -1;			// some kind of EOF?
    }
    return 0;				// no problems!
}


int raw_update_seg(AFFILE *af, const char *name,
                   uint32_t /*arg*/,const u_char *value,uint32_t vallen)
{
    struct raw_private *rp = RAW_PRIVATE(af);

    /* Simple implementation; only updates data segments */
    int64_t pagenum = af_segname_page_number(name);
    if(pagenum<0){
	errno = ENOTSUP;
	return -1;			// not a segment number
    }
    int64_t pos = pagenum * af->image_pagesize; // where we are to start reading
    fseeko(rp->raw,pos,SEEK_SET);

    if(fwrite(value,vallen,1,rp->raw)==1){
	return 0;
    }
    return -1;				// some kind of error...
}


static int raw_vstat(AFFILE *af,struct af_vnode_info *vni)
{
    struct raw_private *rp = RAW_PRIVATE(af);

    vni->imagesize            = -1;
    vni->pagesize	      = RAW_PAGESIZE;	// decent page size
    vni->supports_metadata    = 0;
    vni->is_raw               = 1;
    vni->changable_pagesize   = 1;	// change it at any time
    vni->changable_sectorsize = 1;	// change it at any time

    /* If we can stat the file, use that. */
    fflush(rp->raw);
    vni->imagesize = raw_filesize(af);
    vni->supports_compression = 0;
    vni->has_pages = 1;

    if(rp->raw_popen){
	/* popen files require special handling */
	vni->has_pages = 0;
	vni->use_eof   = 1;
	vni->at_eof    = feof(rp->raw);	// are we there yet?
    }
    return 0;
}

static int raw_rewind_seg(AFFILE *af)
{
    af->cur_page = 0;
    return 0;
}


static int raw_get_next_seg(AFFILE *af,char *segname,size_t segname_len,uint32_t *arg,
			unsigned char *data,size_t *datalen)
{

    /* See if we are at the end of the "virtual" segment list */
    if((uint64_t)af->cur_page * af->image_pagesize >= af->image_size) return -1;

    /* Make the segment name */
    char pagename[AF_MAX_NAME_LEN];		//
    memset(pagename,0,sizeof(pagename));
    snprintf(pagename,sizeof(pagename),AF_PAGE,af->cur_page++);

    /* Get the segment, if we can */
    int r = raw_get_seg(af,pagename,arg,data,datalen);

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

static int raw_read(AFFILE *af, unsigned char *buf, uint64_t pos,size_t count)
{
    struct raw_private *rp = RAW_PRIVATE(af);
    if(fseeko(rp->raw, pos, SEEK_SET) < 0)
        return -1;

    errno = 0;
    count = fread(buf, 1, count, rp->raw);
    return (!count && errno) ? -1 : count;
}

static int raw_write(AFFILE *af, unsigned char *buf, uint64_t pos,size_t count)
{
    struct raw_private *rp = RAW_PRIVATE(af);
    if(fseeko(rp->raw, pos, SEEK_SET) < 0)
        return -1;

    errno = 0;
    count = fwrite(buf, 1, count, rp->raw);
    return (!count && errno) ? -1 : count;
}



struct af_vnode vnode_raw = {
    AF_IDENTIFY_RAW,
    AF_VNODE_TYPE_PRIMITIVE|AF_VNODE_TYPE_RELIABLE|AF_VNODE_NO_SIGNING|AF_VNODE_NO_SEALING,
    "Raw",
    raw_identify_file,
    raw_open,
    raw_close,
    raw_vstat,
    raw_get_seg,			// get seg
    raw_get_next_seg,			// get_next_seg
    raw_rewind_seg,			// rewind_seg
    raw_update_seg,			// update_seg
    0,					// del_seg
    raw_read,				// read
    raw_write				// write
};

