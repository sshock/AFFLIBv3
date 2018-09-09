/*
 * affstats.cpp:
 *
 * print specific statistics about one or more AFF files.
 * Ideally, we can get the stats from the metadata, but this program will
 * calculate it if necessary.
 */

/*
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
 * 3. [omitted]
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


#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"

#include <openssl/md5.h>
#include <openssl/sha.h>
#include <zlib.h>
#include <assert.h>

#ifdef HAVE_ERR_H
#include <err.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef WIN32
#include "unix4win32.h"
#include <malloc.h>
#endif

const char *progname = "affstats";
int  opt_m = 0;

void usage()
{
    printf("%s version %s\n\n",progname,PACKAGE_VERSION);
    printf("usage: %s [options] infile(s)\n",progname);
    printf("      -m = print all output in megabytes\n");
    printf("      -V = Just print the version number and exit.\n");
    exit(0);
}


void title()
{
    printf("fname\tbytes\tcompressed\n");
}

void print_size(uint64_t s)
{
    if(opt_m){
	printf("%u",(unsigned int)(s/(1024*1024)));
	return;
    }
    printf("%" I64u,s);    
}

void affstats_title()
{
    printf("Name\tAF_IMAGESIZE\tCompressed\tUncompressed\tBlank\tBad\n");
}

void affstats(const char *fname)
{
    AFFILE *af = af_open(fname,O_RDONLY,0);
    if(!af) af_err(1,"af_open(%s)",fname);

    printf("%s\t",fname);

    uint32_t segsize=0;

    int64_t imagesize=0;
    int64_t blanksectors=0;
    int64_t badsectors=0;
    af_get_segq(af,AF_IMAGESIZE,&imagesize);
    if(af_get_seg(af,AF_PAGESIZE,&segsize,0,0)){
	af_get_seg(af,AF_SEGSIZE_D,&segsize,0,0); // check for oldstype
    }
    af_get_segq(af,AF_BADSECTORS,&badsectors);
    af_get_segq(af,AF_BLANKSECTORS,&blanksectors);

    print_size(imagesize);
    printf("\t");
    fflush(stdout);

    int64_t compressed_bytes = 0;
    int64_t uncompressed_bytes = 0;

    /* Now read through all of the segments and count the number of
     * data segments. We know the uncompressed size...
     */
    af_rewind_seg(af);
    char segname[AF_MAX_NAME_LEN+1];
    size_t datalen;
    while(af_get_next_seg(af,segname,sizeof(segname),0,0,&datalen)==0){
	int64_t page_num = af_segname_page_number(segname);
	if(page_num>=0){
	    compressed_bytes += datalen;
	    uncompressed_bytes += segsize;
	}
    }
    if(uncompressed_bytes > imagesize) uncompressed_bytes = imagesize;

    print_size(compressed_bytes);
    printf("\t");
    print_size(uncompressed_bytes);
    printf(" %" I64d " %" I64d,blanksectors,badsectors);
    putchar('\n');
    
    
}
    



int main(int argc,char **argv)
{
    int ch;
    while ((ch = getopt(argc, argv, "mh?V")) != -1) {
	switch (ch) {
	case 'm':
	    opt_m = 1;
	    break;
	case 'h':
	case '?':
	default:
	    usage();
	    break;
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

    /* Each argument is now a file. Process each one */
    affstats_title();
    while(*argv){
	affstats(*argv++);
	argc--;
    }
    exit(0);
}


