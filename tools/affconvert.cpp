/*
 * affconvert.cpp:
 *
 * Convert raw -> aff
 *         aff -> raw
 *         aff -> aff (recompressing/uncompressing)
 *
 * Distributed under the Berkeley 4-part license
 */



#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"			// we do enough mucking, we need the internal version
#include "utils.h"

#include <openssl/md5.h>
#include <openssl/sha.h>

#ifdef WIN32
#include "unix4win32.h"
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include <sys/stat.h>
#include <string>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif



const char *progname = "affconvert";

int	image_pagesize = 16*1024*1024;	// default seg size --- 16MB
int	opt_compression_alg	= AF_COMPRESSION_ALG_ZLIB;
int	opt_compress_level	= AF_COMPRESSION_DEFAULT;
int64_t	 bytes_to_convert	= 0;
int	opt_batch = 1;
int	opt_zap   = 0;
int	opt_quiet = 0;
int	opt_write_raw		= 0;		// output
int	opt_probe_compressed = 1;		// probe for compressed files
const char	*opt_write_raw_ext	= "raw";
const char	*opt_outdir     = 0;
const char	*opt_aff_ext    = "aff";
int64_t	opt_maxsize     = 0;
int	opt_yes		= 0;
int	opt_debug       = 0;
std::string	command_line;


char *append(char *base,const char *str)
{
    base = (char *)realloc(base,strlen(base)+strlen(str)+1);
    strcat(base,str);			// can't fail
    return base;
}



void usage()
{
    printf("%s version %s\n",progname,PACKAGE_VERSION);
    printf("\n");
    printf("usage:   %s [options] file1 [... files] \n",progname);
    printf("\n");
    printf("Please, see more info in manpage.");
/*    printf("examples:\n");
    printf("  %s file1.iso --- convert file1.iso to file1.aff\n",progname);
    printf("  %s file1.iso file2.iso file3.iso...  --- batch convert files\n",progname);
    printf("  %s -r -e iso image.aff --- convert image.aff to image.iso\n",progname);
    printf("  %s -M4g -o/media/dvd.afd  bigfile.aff  --- split an AFF file into 4GB chunks for archiving to DVD\n",
	   progname);
    //printf("  %s -p image.aff --- recompress image.aff to maximum compression\n",progname);
    printf("\n");
    printf("\nGeneral options:\n");
    printf("      -q       -- Quiet mode. Don't ask questions, don't print status.\n");
    
    printf("\nAFF output options:\n");
    printf("      -a ext   -- use 'ext' for aff files (default is %s)\n",opt_aff_ext);
    printf("                  (use .afd for AFD files)\n");
    printf("      -Mn[kgm] -- set maximum size of output file. Suffix with g, m or k.\n");
    printf("      -sn      -- set the image_pagesize (default %d)\n",image_pagesize);
    printf("      -x       -- don't compress AFF file.\n");
    printf("      -O dir   -- use 'dir' as the output directory\n");
    printf("      -o file  -- output to 'file' (can only convert one at a time)\n");
    printf("                  File is AFF is file ends .aff; otherwise assumes raw.\n");
    printf("      -Xn      -- Set compression to n; default is 7\n");
    printf("      -L       -- Use the LZMA compression algorithm (better but slower)\n");
    
    printf("\nRaw output options:\n");
    printf("      -r       -- force raw output. \n");
    printf("      -e ext   -- use 'ext' for the raw files (default %s)\n",opt_write_raw_ext);
    printf("                  (implies -r)\n");

    printf("\nDangerous input options:\n");
    printf("      -z       -- zap; delete the output file if it already exists.\n");
    printf("      -Z       -- Do not automatically probe for gzip/bzip2 compression.\n");
    printf("      -y       -- Always answer yes/no questions 'yes.'\n");
    printf("      -V = Just print the version number and exit.\n");
*/
    printf("\n");
    exit(0);
}


/* probe_gzip():
 * Is this a gzip file?
 * Right now it just looks at the file extension.
 */

int probe_gzip(const char *infile)
{
    int len = strlen(infile);

    if(len>3 && strcmp(infile+len-3,".gz")==0){
	return 1;
    }
    return 0;
}

int probe_bzip2(const char *infile)
{
    int len = strlen(infile);

    if(len>4 && strcmp(infile+len-4,".bz2")==0){
	return 1;
    }
    return 0;
}

/* yesno():
 * As a yes/no question. Return 1 if yes, 0 if no.
 */

int yesno(const char *statement,const char *question,const char *affirmative)
{
    if(opt_yes){
	if(!opt_quiet) printf("%s. %s.\n",statement,affirmative);
	return 1;
    }

    printf("%s. ",statement);
    char buf[256];
    do {
	printf("%s [y/n]: ",question);
	memset(buf,0,sizeof(buf));
	if(fgets(buf,sizeof(buf)-1,stdin)==0) return 0;
	if(buf[0]=='y' || buf[0]=='Y'){
	    printf("%s.\n",affirmative);
	    return 1;
	}
    } while(buf[0]!='n' && buf[0]!='N');
    return 0;
}


/*
 * Basic conversion:
 * We have an input, which may be raw or aff,
 * and we have an output, which may be raw or aff.
 * We are going to want to read a segment at a time.
 */


#include <algorithm>
#include <cstdlib>
#include <vector>
#include <string>

#ifdef HAVE_CSTRING
#include <cstring>
#endif


using namespace std;

/** Do the conversion.
 * return 0 if success, code if fail.
 */
int convert(const char *infile,char *outfile)
{

    if(opt_debug) fprintf(stderr,"convert(%s,%s)\n",infile,outfile);

    if(infile && outfile && strcmp(infile,outfile)==0){
	errx(1,"Can't convert a file to itself\n");
    }

    /****************************************************************
     *** Open Input
     ****************************************************************/

    AFFILE *a_in = 0;			// input file, if aff

#ifdef UNIX
    /* Check to see if it is a gzip file... */
    if(opt_probe_compressed
       && probe_gzip(infile)
       && yesno("infile looks like a gzip file","Uncompress it","Uncompressing")){
	/* Open with a subprocess. We will need to use zlib when we move to Windows. */
	if(af_hasmeta(infile)) return -1;	// don't covert with shell metacharacters
	char buf[256];
	snprintf(buf,sizeof(buf),"gzcat %s",infile);
	a_in = af_popen(buf,"r");
    }

    /* Check to see if it is a bzip2 file... */
    if(!a_in
       && opt_probe_compressed
       && probe_bzip2(infile)
       && yesno("infile looks like a bzip2 file","Uncompress it","Uncompressing")){
	/* Open with a subprocess. We will need to use bzip2zlib when we move to Windows. */
	if(af_hasmeta(infile)) return -1;	// don't covert with shell metacharacters
	char buf[256];
	snprintf(buf,sizeof(buf),"bzcat %s",infile);
	a_in = af_popen(buf,"r");
    }
#endif

    /* If the file isn't open, try to open it... */
    if(!a_in){
	a_in = af_open(infile,O_RDONLY,0);
	if(!a_in) af_err(1,"%s",infile);	// give up
	if(af_identify(a_in)==AF_IDENTIFY_RAW){	
	    af_set_pagesize(a_in,image_pagesize); // match the page size we want to use
	}
	else {
	    image_pagesize = a_in->image_pagesize; // that's what we are using
	}
    }
    
    const char *ain_fn = af_filename(a_in);
    struct stat si;
    memset((char *)&si,0,sizeof(si));
    if(ain_fn && stat(ain_fn,&si)){
	warn("Cannot stat %s",ain_fn);
    }


    /****************************************************************
     *** Open Ouptut
     ****************************************************************/


    if(opt_zap) unlink(outfile);	// we were told to zap it

    AFFILE *a_out = 0;			// output file, if aff or raw...
    if(access(outfile,F_OK)==0){
	/* If outfile is a device, ask user... */
	struct stat so;
	if(stat(outfile,&so)){
	    err(1,"%s exists but can't be stat?",outfile);
	}
	if((so.st_mode & S_IFMT)==S_IFCHR ||
	   (so.st_mode & S_IFMT)==S_IFBLK){
	    char buf[1024];
	    snprintf(buf,sizeof(buf),"%s is a raw device.\n",outfile);
	    if(yesno(buf,"Overwrite raw device?","yes")){
		goto doit;
	    }
	}
	fprintf(stderr,"%s: file exists. Delete it before converting.\n",outfile);
	exit(-1);
    }
    /* Check for splitraw names */
    if(af_ext_is(outfile,"afm")){
	char file000[MAXPATHLEN+1];
	strlcpy(file000,outfile,sizeof(file000));
	char *cc = strrchr(file000,'.');
	if(!cc) err(1,"Cannot file '.' in %s\n",file000);
	for(int i=0;i<2;i++){
	    sprintf(cc,".%03d",i);
	    if(access(file000,F_OK)==0){
		fprintf(stderr,"%s: file exists. Delete it before converting.\n",file000);
		fprintf(stderr,"NOTE: -z option will not delete %s\n",file000);
		return -1;
	    }
	}
    }

 doit:;

    if(opt_write_raw){
	/* Easy way to make a raw output is to reopen an existing output file... */
	FILE *f = fopen(outfile,"w+b");
	if(!f){
	    err(1,"%s",outfile);
	}
	a_out = af_freopen(f);
    }
    else {
	a_out = af_open(outfile,O_RDWR|O_CREAT|O_BINARY,0777);
	if(!a_out) af_err(1,"%s",outfile);
	if(opt_maxsize){
	    af_set_maxsize(a_out,opt_maxsize);
	}

    }
    if(a_out == 0) af_err(1,"af_open: %s",outfile);

    if(!opt_quiet) printf("convert %s --> %s\n",infile,outfile);

    af_update_seg(a_out,AF_ACQUISITION_COMMAND_LINE,0,
		  (const u_char *)command_line.c_str(),
		  command_line.size());

    /****************************************************************
     *** Set up the AFF file (assuming it's an aff file)
     *** stuff that we keep at the beginning of the file...
     ****************************************************************/

    MD5_CTX md5;
    MD5_Init(&md5);

    SHA_CTX sha;
    SHA1_Init(&sha);

    /* Setup writing */
    if(a_in->image_pagesize){
	image_pagesize = a_in->image_pagesize;
    }
    af_set_pagesize(a_out,image_pagesize);
    af_set_sectorsize(a_out,a_in->image_sectorsize); 

    struct af_vnode_info vni;
    af_vstat(a_out,&vni);
    if(vni.supports_compression){
	if(opt_compression_alg){
	    af_enable_compression(a_out,opt_compression_alg,opt_compress_level);
	}
	else{
	    af_enable_compression(a_out,0,0);
	}
    }

    /* Get a list of all the metadata segments and the pages
     * (if this is a raw file, then the vnode raw driver will give us those segments)
     */

    char segname[AF_MAX_NAME_LEN];
    vector <string> metadata_segments;
    vector <int64_t> pages;
    af_rewind_seg(a_in);			// start at the beginning
    int64_t highest_pagenum = 0;
    while(af_get_next_seg(a_in,segname,sizeof(segname),0,0,0)==0){
	int64_t page_num = af_segname_page_number(segname);
	if(page_num>=0){
	    pages.push_back(page_num);
	    if(page_num>highest_pagenum) highest_pagenum = page_num;
	}
	else {
	    metadata_segments.push_back(segname);
	}
    }

    /* Copy over all of the metadata segments.
     * But don't bother if we are creating raw output
     */
    if(opt_write_raw==0){
	for(vector<string>::iterator i = metadata_segments.begin();
	    i != metadata_segments.end();
	    i++){
	    strlcpy(segname,i->c_str(),sizeof(segname));
	    size_t data_len = 0;
	    uint32_t arg;

	    /* First find out how big the segment is */
	    if(af_get_seg(a_in,segname,&arg,0,&data_len)){
		warn("af_get_seg_1");
		continue;
	    }
	    /* Now get the data */
	    unsigned char *data = (unsigned char *)malloc(data_len);
	    if(af_get_seg(a_in,segname,0,data,&data_len)){
		warn("af_get_seg_2");
		free(data);
		continue;
	    }
	    /* Now put the data */
	    if(af_update_seg(a_out,segname,arg,data,data_len)){
		err(1,"af_update_seg");
	    }
	    free(data);
	}
    }
	
    /* Now sort the pages and copy them over. If there is no break,
     * we can compute the hashes...
     */
    sort(pages.begin(),pages.end());
    
    int64_t  prev_pagenum = -1;
    bool   hash_valid = true;
    uint64_t last_byte_in_image = 0;
    uint64_t total_bytes_converted = 0;

    bool copy_by_pages = af_has_pages(a_in);

    unsigned char *data = (unsigned char *)malloc(image_pagesize);
    if(copy_by_pages){
	/* Copy over data one page at a time */
	for(vector<int64_t>::iterator i = pages.begin(); i != pages.end(); i++){
	    
	    int64_t pagenum = *i;
	    
	    if(!opt_quiet) { printf("Converting page %" I64d " of %" I64d "\r",pagenum,highest_pagenum); fflush(stdout); }
	    
	    size_t data_len = image_pagesize;
	    if(af_get_page(a_in,pagenum,data,&data_len)){
		err(1,"af_get_page(file=%s,page=%" I64d ")",
		    af_filename(a_in),pagenum);
	    }
	    if(af_update_page(a_out,pagenum,data,data_len)){
		err(1,"af_update_page(file=%s,page=%" I64d ")",
		    af_filename(a_out),pagenum);
	    }
	    
	    if(pagenum != prev_pagenum + 1) hash_valid = false;
	    
	    if(hash_valid && vni.supports_metadata){
		MD5_Update(&md5,data,data_len);
		SHA1_Update(&sha,data,data_len);
		prev_pagenum = pagenum;
	    }
	    last_byte_in_image = (int64_t)image_pagesize * pagenum + (int64_t)data_len;
	    total_bytes_converted += data_len;
	}
	/* Go back and update the image size (necessary since I have been writing page-by-page) */
	if(af_update_segq(a_out,AF_IMAGESIZE,last_byte_in_image)
	   && errno!=ENOTSUP){
	    err(1,"Could not update AF_IMAGESIZE");
	}
    } else {
	/* No page support; Copy from beginning to end */
	while(!af_eof(a_in)){
	    int data_len = af_read(a_in,data,image_pagesize);
	    if(data_len>0){
		if(!opt_quiet){
		    printf("Writing to page %" I64d " with %d bytes read from input...     \r",
			   total_bytes_converted / image_pagesize,data_len);
		    fflush(stdout);
		}
		if(af_write(a_out,data,data_len)!=data_len){
		    err(1,"af_write");
		}
		if(vni.supports_metadata){
		    MD5_Update(&md5,data,data_len);
		    SHA1_Update(&sha,data,data_len);
		}
	    }
	    if(data_len<0) err(1,"af_read");
	    if(data_len==0){
		if(!opt_quiet) printf("af_read returned 0. Reached a sparse region or end of pipe.\n");
		break;
	    }
	    last_byte_in_image += data_len;
	    total_bytes_converted += data_len;
	}
    }
    free(data);
    if(!opt_quiet) printf("\n");

    /* Write out the new hash if it is valid */
    if(hash_valid && vni.supports_metadata){
	u_char md5_buf[32],sha1_buf[40];
	char buf[256];
	MD5_Final(md5_buf,&md5);
	if(af_update_seg(a_out,AF_MD5,0,md5_buf,16) && errno!=ENOTSUP){
	    err(1,"Could not update AF_MD5");
	}
	if(!opt_quiet) printf("md5: %s\n",af_hexbuf(buf,sizeof(buf),md5_buf,16,1));
	
	SHA1_Final(sha1_buf,&sha);
	if(af_update_seg(a_out,AF_SHA1,0,sha1_buf,20) && errno!=ENOTSUP){
	    err(1,"Could not update AF_SHA1");
	}
	if(!opt_quiet) printf("sha1: %s\n",af_hexbuf(buf,sizeof(buf),sha1_buf,20,1));
    }

    /* Finish the hash calculations and write to the db */
    if(!opt_quiet){
	printf("bytes converted: %" I64d " \n",total_bytes_converted);
	/* If the vnode implementation tracked segments written, report it. */
	if(a_out->pages_written || a_out->pages_compressed){
	    printf("Total pages: %" I64u "  (%" I64u " compressed)\n",
		   a_out->pages_written,a_out->pages_compressed);
	}
    }

    if(vni.supports_metadata){
	/* make an AF_IMAGE_GID if it doesn't exist */
	af_make_gid(a_out);		
	af_set_acquisition_date(a_out,si.st_mtime);
    }

    /* Make a copy of the a_out filename if we can get it */
#ifdef HAVE_UTIMES
    char *a_out_fn=0;			 // output filename, to remember for utimes
    const char *a_ = af_filename(a_out); // remember the output filename
    if(a_){
	a_out_fn = strdup(a_);	// make a copy of it
    }
#endif
    if(af_close(a_out)) err(1,"af_close(a_out)");

    if(!opt_quiet){
	printf("Conversion finished.\n");
	if(af_cannot_decrypt(a_in)){
	    printf("*** encrypted pages are present which could not be decrypted ***\n");
	}
	printf("\n\n");
    }
    if(af_close(a_in)) err(1,"af_close(a_in)");

    /* Set the utime on the resulting file if we can stat it */
    struct timeval times[2];
    
    memset(times,0,sizeof(times));
    times[0].tv_sec = si.st_atime;
    times[1].tv_sec = si.st_mtime;
#ifdef HAVE_UTIMES
    if(a_out_fn){
	if(utimes(a_out_fn,times)) warn("utimes(%s):",outfile);
	free(a_out_fn);
	a_out_fn = 0;
    }
#endif    
    return(0);
}


int64_t atoi64(const char *buf)
{
    int64_t r=0;
    sscanf(buf,"%" I64d,&r);
    return r;
}

int64_t atoi64m(const char *optarg)
{
    int multiplier;
    switch(optarg[strlen(optarg)-1]){
    case 'g':
    case 'G':
	multiplier=1024*1024*1024;break;
    case 'm':
    case 'M':
	multiplier=1024*1024; break;
    case 'k':
    case 'K':
	multiplier=1024; break;
    case 'b':
    case 'B':
	multiplier=1;break;
    default:
	err(1,"Specify multiplier units of g, m, k or b in '%s'\n",optarg);
    }	
    return atoi64(optarg) * multiplier;
}


int main(int argc,char **argv)
{
    char *outfile = 0;
    int ch;

    command_line = aff::command_line(argc,argv);
    while ((ch = getopt(argc, argv, "a:e:Lo:zqrs:xX:Zh?M:O::ydV")) != -1) {
	switch (ch) {
	case 'a':
	    opt_aff_ext = optarg;
	    break;
	case 'e':
	    opt_write_raw++;
	    opt_write_raw_ext = optarg;
	    break;
	case 'o':
	    outfile = optarg;
	    break;
	case 'z':
	    opt_zap ++;
	    break;
	case 'q':
	    opt_quiet++;
	    break;
	case 'L':
	    opt_compression_alg = AF_COMPRESSION_ALG_LZMA;
	    break;
	case 'r':
	    opt_write_raw++;
	    break;
	case 's':
	    image_pagesize = atoi64m(optarg);
	    break;
	case 'x':
	    opt_compression_alg=AF_COMPRESSION_ALG_NONE;
	    break;
	case 'X':
	    opt_compress_level = atoi(optarg);
	    break;
	case 'Z':
	    opt_probe_compressed = 0;
	    break;
	case 'y':
	    opt_yes = 1;
	    break;
	case 'M':
	    opt_maxsize = atoi64m(optarg);
	    break;
	case 'O':
	    if(!optarg) err(1,"-O flag requires a directory");
	    opt_outdir = optarg;
	    break;
	case 'd':
	    opt_debug++;
	    break;
	case 'h':
	case '?':
	default:
	    usage();
	    exit(0);
	case 'V':
	    printf("%s version %s\n",progname,PACKAGE_VERSION);
	    exit(0);
	    
	}
    }
    argc -= optind;
    argv += optind;

    if(argc<1){
	usage();
    }

    if(outfile){
	return convert(*argv,outfile);
    }

    /* Check for "-o filename" at the end of the command line... */
    if(argc==3 && !strcmp(argv[1],"-o")){
	return convert(argv[0],argv[2]);
    }

    /* Convert each file*/

    while(*argv){
	char outfile[MAXPATHLEN+1];
	memset(outfile,0,sizeof(outfile));

	const char *ext = opt_write_raw ? opt_write_raw_ext : opt_aff_ext;
	char *infile = *argv;
	argv++;
	argc--;

	/* Copy over the filename and change the extension */
	strlcpy(outfile,infile,sizeof(outfile));
	char *cc = strrchr(outfile,'.'); // to strip off extension
	if(cc){
	    /* Found an extension; copy over mine. */
	    strlcpy(cc+1,ext,sizeof(outfile)-(cc+1-outfile));
	}
	else {
	    /* No extension; make one */
	    strlcat(outfile,".",sizeof(outfile));
	    strlcat(outfile,ext,sizeof(outfile));
	}
	
	/* The user might want us to put things
	 * in a different directory. Pull off the filename...
	 */
	if(opt_outdir){
	    cc = strrchr(outfile,'/');
	    char filename[MAXPATHLEN];
	    if(cc){
		strlcpy(filename,cc+1,sizeof(filename));	// just the filename
	    }
	    else{
		strlcpy(filename,outfile,sizeof(filename));	// the outfile is the filename
	    }
	    strlcpy(outfile,opt_outdir,sizeof(outfile));
	    strlcat(outfile,"/",sizeof(outfile));
	    strlcat(outfile,filename,sizeof(outfile));
	}
	if(convert(infile,outfile)){
	    exit(1);
	}
    }
    exit(0);
}
