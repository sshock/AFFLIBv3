/**
 * affix.cpp
 *
 * Fix an aff file that is corrupt.
 * Current methodologies:
 *  - If file does not have a GUID, create one.
 * Distributed under the Berkeley 4-part license
 */


#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"
#include "vnode_aff.h"			// so we can use magical af_open_with...

#include <ctype.h>

#include <zlib.h>
#include <assert.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef WIN32
#include "unix4win32.h"
#include <malloc.h>
#endif

const char *progname = "affix";
int opt_fix = 0;


void usage()
{
    printf("usage: %s [options] file1 [...]\n",progname);
    printf("  -y = Actually modify the files; normally just reports the problems\n");
    printf("  -v = Just print the version number and exit.\n");
    exit(0);
}



/* Returns 0 if this is a valid AFF file, code if it isn't. */
int af_is_valid_afffile(const char *file)
{
    return 0;				// I should write this
}

int fix(const char *infile)
{
    char buf[1024];
    int flags = (opt_fix ? O_RDWR : O_RDONLY) | O_BINARY;
    switch(af_identify_file_type(infile,1)){
    case AF_IDENTIFY_ERR:
	perror(infile);
	return 0;
    default:
	fprintf(stderr,"%s is not an AFF file\n",infile);
	return 0;
    case AF_IDENTIFY_AFF:
	break;
    }

    printf("%s  ",infile);
    int r=0;

    /* First see if the if the file begins with an AFF flag */
    int fd = open(infile,flags,0666);
    if(fd<0) err(1,"fopen(%s)",infile);
    if(read(fd,buf,strlen(AF_HEADER)+1)!=strlen(AF_HEADER)+1)
	err(1,"can't read AFF file header. Stop.");
    if(strcmp(buf,AF_HEADER)!=0)
	err(1,"%s does not begin with an AF_HEADER. Stop.",infile);
    if(read(fd,buf,strlen(AF_SEGHEAD)+1)!=strlen(AF_SEGHEAD)+1)
	err(1,"Can't read AF_SEGHEAD after AF_HEADER. Stop.");
    if(strcmp(buf,AF_SEGHEAD)!=0)
	err(1,"%s does not have an AF_SEGHEAD after AF_SEGEADER. Stop.",infile);

    /* Figure out length */
    off_t len = lseek(fd,0,SEEK_END);
    if(len<0) err(1,"Can't seek to end of %s. Stop.",infile);
    close(fd);
    
    AFFILE *af = af_open_with(infile,AF_HALF_OPEN|flags,0,&vnode_aff);
    printf("Scanning AFF file...\n");
    r = (*af->v->open)(af);
    /* See if we can build a TOC */
    if(r<0){
	printf("AFF file corrupt at %" I64d " out of %" I64d " (%" I64d " bytes from end)\n",
	       ftello(af->aseg),(int64_t)len,len-ftello(af->aseg));
	if(opt_fix){
	    printf("Truncating... %d \n",fileno(af->aseg));
	    if(ftruncate(fileno(af->aseg),ftello(af->aseg))){
		err(1,"ftruncate");
	    }
	}
    }

    /* See if it has a GID or an encrypted GID */
    if(af_get_seg(af,AF_IMAGE_GID,0,0,0)!=0 &&
       af_get_seg(af,AF_IMAGE_GID AF_AES256_SUFFIX,0,0,0)!=0){
	printf("AFF file is missing a GID. ");
	if(opt_fix){
	    printf("Making one...");
	    if(af_make_gid(af)<0) af_err(1,"af_make_gid");
	}
	putchar('\n');
    }

    af_close(af);

    return 0;
}
#if 0
    

    /* See if it ends properly */
    off_t len;
    
    printf("File is %"I64d" bytes long\n",len);

    if(lseek(fd,len-4,SEEK_SET)<0)
	err(1,"Can't backup %d bytes. Stop.",-4);
    r = read(fd,buf,4);
    if(r!=4)
	err(1,"Can't read last %d bytes of file. Read %d. Stop. ", strlen(AF_SEGTAIL)+1,r);
    if(strcmp(buf,AF_SEGTAIL)!=0){
	printf("Does not end with an AF_SEGTAIL. Scanning backwards to find last complete AF_SEGTAIL.\n");
	for(off_t end=len-4;end>4;end--){
	    if(lseek(fd,end,SEEK_SET)<0) err(1,"lseek bad=%"I64d,end);
	    r = read(fd,buf,4);
	    if(r!=4) err(1,"Can't read 4 bytes at %"I64d);
	    if(strcmp(buf,AF_SEGTAIL)==0){
		printf("Valid AF_SEGTAIL found at %"I64d" (%"I64d" bytes in)\n",end,len-end);
		if(!opt_fix) errx(0,"Rerun with -y flag to fix");
		printf("Truncating at %"I64d"\n",end);
		if(ftruncate(fd,end+4)) err(0,"ftruncate");
		break;
	    }
	}
    }
    close(fd);
    exit(0);



    AFFILE *af = af_open(infile,O_RDONLY,0);
    if(!af) af_err(1,infile);
    int  fix = 0;
    bool fix_add_gid = false;

    if(af_get_seg(af,AF_IMAGE_GID,0,0,0)){
	printf("no GID (%s)",AF_IMAGE_GID);
	fix++;
	fix_add_gid = true;
    }
    af_close(af);
    if(opt_fix==0 || fix==0) return 0;

    if(fix){
	af = af_open(infile,O_RDWR,0);
	if(!af){
	    warn(infile);
	    return -1;
	}
	if(fix_add_gid) {
	    printf(" ... adding GID  ",infile);
	    unsigned char bit128[16];
	    RAND_bytes(bit128,sizeof(bit128));
	    if(af_update_seg(af,AF_IMAGE_GID,0,bit128,sizeof(bit128))){
		warn("Cannot write %s: ",AF_IMAGE_GID);
	    }
	}
	if(af_close(af)){
	    warn("Cannot close %s",infile);
	}
    }
    putchar('\n');
    return 0;
}
#endif

int main(int argc,char **argv)
{
    int ch;

    setvbuf(stdout,0,_IONBF,0);		// turn off buffering

    /* Figure out how many cols the screen has... */
    while ((ch = getopt(argc, argv, "yh?v")) != -1) {
	switch (ch) {
	case 'y':
	    opt_fix = 1;
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


    /* Loop through all of the files */
    while(*argv){
	fix(*argv++);		// get the file
	argc--;				// decrement argument counter
    }
    exit(0);
}


