/*
 * affcat.cpp:
 *
 * cat the contents of an AFF file...
 * Distributed under the Berkeley 4-part license
 */

#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"

#include <stdio.h>
#include <algorithm>
#include <vector>
#include <string>
#ifdef HAVE_CSTRING
#include <cstring>
#endif


using namespace std;

vector <int64_t> pages;

const char *progname = "affcat";
int  opt_info     = 0;
char *opt_segname=0;
int64_t  opt_pagenum = -1;
int opt_quiet = 1;
int opt_list= 0 ;
int opt_list_long = 0;
int opt_debug = 0;
int64_t opt_sector = -1;
int opt_badflag = 0;
vector<string> opt_r;


void usage()
{
    printf("affcat version %s\n",PACKAGE_VERSION);
    printf("usage: affcat [options] infile [... more infiles]\n");
    printf("options:\n");
    printf("    -s name --- Just output segment name\n");
    printf("    -p ###  --- just output data page number ###\n");
    printf("    -S ###  --- Just output data sector ### (assumes 512-byte sectors). Sector #0 is first\n");
    printf("    -q      --- quiet; don't print to STDERR if a page is skipped\n");
    printf("    -n      --- noisy; tell when pages are skipped.\n");
    printf("    -l      --- List all of the segment names\n");
    printf("    -L      --- List segment names, lengths, and args\n");
    printf("    -d      --- debug. Print the page numbers to stderr as data goes to stdout\n");
    printf("    -b      --- Output BADFALG for bad blocks (default is NULLs)\n");
    printf("    -v      --- Just print the version number and exit.\n");
    printf("    -r offset:count --- seek to offset and output count characters in each file; may be repeated\n");
    exit(0);
}


const char *current_fname = 0;
int64_t current_page = -1;
void sig_info(int arg)
{
    fprintf(stderr,"affcat ");
    if(current_fname) fprintf(stderr,"%s: ",current_fname);
    if(current_page>=0) fprintf(stderr,"[%" PRId64 "] ",current_page);
    fflush(stderr);
}



int compar(const void *a_,const void *b_)
{
    int64_t a = *(int *)a_;
    int64_t b = *(int *)b_;
    if(a<b) return -1;
    if(a>b) return 1;
    return 0;
}

struct afm_private {
    AFFILE *aff;			// the AFFILE we use for the actual metadata
    AFFILE *sr;				// the AFFILE we use for the splitraw
    int sr_initialized;			// has the split-raw been setup from AFM?
};

int output_page(AFFILE *af,FILE *outfile,int64_t pagenum)
{
    current_fname = af_filename(af);
    current_page = pagenum;
    unsigned char *buf = (unsigned char *)malloc(af->image_pagesize);
    if(buf==0){
	err(1,"malloc(%d) failed",(int)af->image_pagesize);
    }
    uint64_t offset = (uint64_t)pagenum * af->image_pagesize; // go to that location


    af_seek(af,offset,SEEK_SET);


    int bytes = af_read(af,buf,af->image_pagesize); // read what we can

    if(bytes<0){
	if(opt_debug) fprintf(stderr,"affcat: cannot read page %" I64d "\n",pagenum);
	return -1;
    }

    if(opt_debug){
	fprintf(stderr,"affcat: page:%" I64d" bytes: %d offset:%" I64d "\n",
		pagenum, bytes,offset);
    }

    /* Check each sector to see if it is badflag or not.
     * If it is and if opt_badflag is not set, make it all NULs.
     */
    for(unsigned char *cc=buf;cc<buf+bytes;cc+=af->image_sectorsize){
	if(af_is_badsector(af,cc) && opt_badflag==0){
	    memset(cc,0,af->image_sectorsize);
	}
    }

    if(opt_debug) fprintf(stderr,"  outputting %d bytes\n",bytes);
    int count = fwrite(buf,1,bytes,outfile);	// send to the output
    if(count!=bytes) fprintf(stderr,"fwrite(buf,1,%d,outfile) only wrote %d bytes\n",bytes,count);
    free(buf);
    return bytes;
}


int affcat(AFFILE *af)
{
    int64_t total_bytes_written = 0;

    /* Read all of the pages from beginning to end and capture
     * all the segment numbers...
     */

#ifdef WIN32
    _setmode(fileno(stdout),_O_BINARY);
#endif
    if(opt_debug) fprintf(stderr,"affcat(%s)\n",af_filename(af));

    if(opt_segname){
	/* First figure out how big the segment is */
	size_t datalen = 0;
	if(af_get_seg(af,opt_segname,0,0,&datalen)){
	    fprintf(stderr,"%s: segment '%s' does not exist\n",
		    af_filename(af),opt_segname);
	    return -1;
	}
	unsigned char *data = (unsigned char *)malloc(datalen);
	if(data==0) err(1,"malloc");
	if(af_get_seg(af,opt_segname,0,data,&datalen)){
	    free(data);
	    fprintf(stderr,"%s: could not read segment '%s'\n",
		    af_filename(af),opt_segname);
	    return -1;
	}
	int count = fwrite(data,1,datalen,stdout);
	if(count!=(ssize_t)datalen){
	    fprintf(stderr,"fwrite(buf,1,%d,outfile) only wrote %d bytes\n",(int)datalen,count);
	}
	free(data);
	return 0;
    }

    if(opt_pagenum != -1){		// just write a particular page?
	int r = output_page(af,stdout,opt_pagenum);
	return r>=0 ? 0 : -1;
    }

    if(opt_sector>=0){
	unsigned char *buf = (unsigned char *)malloc(af->image_sectorsize);
	af_seek(af,(uint64_t)opt_sector*af->image_sectorsize,SEEK_SET);
	int bytes_read = af_read(af,buf,af->image_sectorsize);
	if(bytes_read>0){
	    int bytes_written =  fwrite(buf,1,bytes_read,stdout);
	    if(bytes_read!=bytes_written){
		fprintf(stderr,"fwrite(buf,1,%d,outfile) only wrote %d bytes\n",
			bytes_read,bytes_written);
	    }
	}
	free(buf);
	return 0;
    }

    /* Get a list of all the segments. If we are doing a list, just print them.
     * If we are not doing a list, capture the data pages and put their numbers
     * into an array.
     */

    if(opt_debug) fprintf(stderr,"af_rewind_seg()\n");

    if(opt_r.size()>0){
	unsigned char *buf = (unsigned char *)malloc(af->image_pagesize);
	for(vector<string>::const_iterator offset_count=opt_r.begin(); offset_count != opt_r.end(); offset_count++){
	    string opts = *offset_count;
	    const char *opt = opts.c_str();
	    uint64_t offset=0;
	    int count=0;
	    if(sscanf(opt,"%" I64u ":%d",&offset,&count)!=2){
		err(1,"Cannot decode '%s'\n",opt);
	    }
	    af_seek(af,offset,SEEK_SET);
	    int r= af_read(af,buf,count);
	    if(r>0){
		int bytes_written = fwrite(buf,1,r,stdout);
		if(bytes_written!=r) {
		    fprintf(stderr,"fwrite(buf,1,%d,outfile) only wrote %d bytes\n",r,bytes_written);
		}
		
	    }
	}
	free(buf);
	return 0;
    }


    af_rewind_seg(af);			// start at the beginning
    char segname[AF_MAX_NAME_LEN];
    uint32_t arg;
    size_t datalen = 0;
    memset(segname,0,sizeof(segname));

    int encrypted_segments = 0;
    while(af_get_next_seg(af,segname,sizeof(segname),&arg,0,&datalen)==0){
	if(opt_debug) fprintf(stderr,"af_get_next_seg found segment %s\n",segname);
	if(segname[0]==0) continue;	// ignore sector
	if(opt_list){
	    printf("%s",segname);
	    if(opt_list_long){
		printf("\targ:%" PRIu32 "\tlen:%d",arg,(int)datalen);
	    }
	    putchar('\n');
	}
	else {
	    int64_t pagenum = af_segname_page_number(segname);
	    if(pagenum>=0) pages.push_back(pagenum);
	    if(af_is_encrypted_segment(segname)) encrypted_segments++;
	}
	datalen = 0;			// allow to get the next one
    }
    if(opt_list) return 0;		// that's all that was wanted.


    sort(pages.begin(),pages.end());

    if(pages.size()==0 && encrypted_segments){
	fprintf(stderr,"affcat: This file has %d encrypted segments.\n",encrypted_segments);
	fprintf(stderr,"affcat: No unencrypted pages could be found.\n");
    }
	
    /* Now I have a list of pages; cat each one */
    int next_page = 0;			// starting page number
    int64_t imagesize = af_get_imagesize(af);
    for(vector<int64_t>::iterator i = pages.begin(); i != pages.end(); i++){

	int page = *i;
	if(page != next_page && opt_quiet==0){
	    if(page == next_page+1 ){
		fprintf(stderr,"affcat: page %d not in file\n",next_page);
	    }
	    else{
		fprintf(stderr,"affcat: pages %d through %d not in file\n",
			next_page,page-1);
	    }
	}
	int r = output_page(af,stdout,page);
	if(r<0) return -1;
	total_bytes_written += r;
	next_page = page + 1;	// note what should be next

	//fprintf(stderr,"bytes written=%qd imagesize=%qd\n",total_bytes_written,imagesize);
	if((total_bytes_written > imagesize) && (imagesize>0)){
	    err(1,"affcat internal error. bytes written=%" I64d " imagesize=%" I64d,
		(int64_t)total_bytes_written,
		(int64_t)imagesize);
	    return -1;
	}
    }
    return 0;
}


int64_t atoi64(const char *buf)
{
    int64_t r=0;
    char ch;
    if(sscanf(buf,"%" I64d "%c",&r,&ch)==1) return r;
    fprintf(stderr,"Cannot parse '%s'\n",buf);
    exit(0);
}


int main(int argc,char **argv)
{
    int ch;

#ifdef SIGINFO
    signal(SIGINFO,sig_info);
#endif

    while ((ch = getopt(argc, argv, "s:S:p:lLh?dqnbvr:")) != -1) {
	switch (ch) {
	case 's':
	    opt_segname = optarg;
	    break;
	case 'S':
	    opt_sector = atoi64(optarg);
	    break;
	case 'p':
	    opt_pagenum = atoi64(optarg);
	    break;
	case 'q':
	    opt_quiet = 1;
	    break;
	case 'n':
	    opt_quiet = 0;
	    break;
	case 'l':
	    opt_list = 1;
	    break;
	case 'r':
	    opt_r.push_back(optarg);
	    break;
	case 'L':
	    opt_list = 1;
	    opt_list_long = 1;
	    break;
	case 'b':
	    opt_badflag = 1;
	    break;
	case 'd':
	    opt_debug++;
	    break;
	case 'h':
	case '?':
	default:
	    usage();
	    break;
	case 'v':
	    printf("%s version %s\n",progname,PACKAGE_VERSION);
	    exit(0);
	}
    }
    argc -= optind;
    argv += optind;

    if(argc<1){
	usage();
    }

    while(*argv){
	AFFILE *af = af_open(*argv,O_RDONLY,0);
	if(!af) af_err(1,"affcat(%s)",*argv);
	if(affcat(af)) err(1,"affcat");
	af_close(af);
	argv++;
	argc--;
    }
}
