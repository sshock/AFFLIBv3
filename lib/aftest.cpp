/*
 * atest.cpp:
 * test suite for the AFF Library.
 * This file is a work of a US government employee and as such is in the Public domain.
 * Simson L. Garfinkel, March 12, 2012
 */

#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"
#include "base64.h"
#include "aftimer.h"

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#include "LzmaRam.h"
extern "C" {
#include "LzmaRamDecode.h"
}

int MAX_FMTS = 10000;		// how many formats we should write

const char *fmt = "%8d Another format string.\n"; // must be constant size
const char *progname = 0;

const char *opt_protocol = "file:///";
const char *opt_ext = "aff";
int   opt_compression_level = AF_COMPRESSION_DEFAULT;// default compression level
int   opt_compression_type  = AF_COMPRESSION_ALG_ZLIB;	//
const char *tempdir = "/tmp/";


/* Create the segment that we need */

#ifndef MIN
#define MIN(x,y) ((x)<(y)?(x):(y))
#endif

/* Support OpenSSL before 1.1.0 */
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
#define EVP_MD_CTX_new EVP_MD_CTX_create
#define EVP_MD_CTX_free EVP_MD_CTX_destroy
#endif

const char *filename(char *buf,int buflen,const char *base)
{
    snprintf(buf,buflen,"%s%s%s.%s",opt_protocol,tempdir,base,opt_ext);
    return buf;
}

AFFILE *open_testfile(const char *base,int wipe)
{
    int flags = O_CREAT|O_RDWR;
    char fn[1024];

    filename(fn,sizeof(fn),base);
    printf("%s = %s\n",base,fn);
    if(wipe){
	unlink(fn);				// make sure it is gone
	flags |= O_TRUNC;
    }
    AFFILE *af = af_open(fn,flags,0666);
    if(!af) err(1,"af_open");
    if(wipe){
	af_enable_compression(af,opt_compression_type,opt_compression_level);
	af_set_pagesize(af,1024);
	af_set_maxsize(af,(int64_t)65536);		// force splitting of raw and afd files
    }
    return af;
}

int sequential_test()
{
    char buf[1024];
    const char *fmt = "this is line %d\n";

    printf("Sequential test...\n");

    AFFILE *af = open_testfile("test_sequential",1);
    for(int i=0;i<MAX_FMTS;i++){
	if(i%250==0) printf("\rwriting %d/%d...",i,MAX_FMTS);
	sprintf(buf,fmt,i);
	if(af_write(af,(unsigned char *)buf,strlen(buf))!=(int)strlen(buf)){
	    err(1,"Attempt to write buffer %d failed\n",i);
	}
    }
    /* Test for a random bug that was reported */
    af_update_seg(af,"test",0,(const u_char *)"foo",3);
    af_update_seg(af,"test",0,(const u_char *)"bar",3);
    af_del_seg(af,"test");
    af_del_seg(af,"test");
    af_close(af);

    printf("\nSequential file written.\n");
    printf("\n");
    printf("Now verifying the string...\n");
    af = open_testfile("test_sequential",0);
    if(!af) err(1,"af_open");
    for(int i=0;i<MAX_FMTS;i++){
	char rbuf[1024];
	sprintf(buf,fmt,i);
	int len = strlen(buf);
	if(af_read(af,(unsigned char *)rbuf,len)!=len){
	    err(1,"Attempt to read entry %d failed\n",i);
	}
	rbuf[len] = 0;			// terminate the string
	if(strcmp(buf,rbuf)!=0){
	    err(1,"Attempt to verify entry %d failed.\nExpected: (len=%zd) '%s'\nGot: (len=%zd) '%s'\n",
		i,strlen(buf),buf,strlen(rbuf),rbuf);
	}
    }
    af_close(af);

    printf("===========================\n\n");
    return 0;
}

int reverse_test()
{
    char wbuf[1024];
    char rbuf[1024];

    printf("Reverse write test...\n");
    for(int pass=1;pass<=2;pass++){

	AFFILE *af = open_testfile("test_reverse",pass==1);
	for(int i=MAX_FMTS-1;i>=0;i--){
	    sprintf(wbuf,fmt,i);
	    af_seek(af,strlen(wbuf)*i,SEEK_SET);
	    if(pass==1){
		if(af_write(af,(unsigned char *)wbuf,strlen(wbuf))!=(int)strlen(wbuf)){
		    err(1,"Attempt to write buffer %d failed\n",i);
		}
	    }
	    if(pass==2){
		memset(rbuf,0,sizeof(rbuf));
		if(af_read(af,(unsigned char *)rbuf,strlen(wbuf))!=(int)strlen(wbuf)){
		    err(1,"Attempt to read buffer %d failed\n",i);
		}
		if(strcmp(rbuf,wbuf)!=0){
		    errx(1,"Data doesn't verify.\nWrote: '%s'\nRead: '%s'\n",wbuf,rbuf);
		}
	    }
	}
	af_close(af);
    }

    printf("\nReverse test passes.\n");
    printf("======================\n\n");
    return 0;
}


int random_write_test()
{
    char buf[1024];
    char *tally = (char *)calloc(MAX_FMTS,1);
    int i;

    memset(tally,0,sizeof(tally));

    /* Create the AFF file */
    sprintf(buf,fmt,0);		// figure out how big fmt string is
    int fmt_size = strlen(buf);

    printf("Random write test...\n");
    printf("Creating test file with  %d byte records.\n", fmt_size);

    AFFILE *af = open_testfile("test_random",1);

    if(af_write(af,(unsigned char *)buf,fmt_size)!=fmt_size){
	err(1,"af_write");
    }
    for(i=0;i<MAX_FMTS;i++){
	/* Find a random spot that's available */
	int pos = rand() % MAX_FMTS;
	while(tally[pos]==1){		//  if this one is used, find next
	    pos = (pos + 1) % MAX_FMTS;
	}
	tally[pos] = 1;
	sprintf(buf,fmt,pos);
	assert((int)strlen(buf)==fmt_size);	// make sure
	af_seek(af,fmt_size*pos,SEEK_SET);
	int wrote = af_write(af,(unsigned char *)buf,fmt_size);
	if(wrote !=fmt_size){
	    fprintf(stderr,"Attempt to write buffer #%d \n",pos);
	    fprintf(stderr,"wrote %d bytes instead of %d bytes\n",wrote,fmt_size);
	    exit(1);
	}
	if(i%250==0) printf("\r%d ...",i);
	fflush(stdout);
    }
    af_close(af);

    /* Now verify what was written */
    printf("Verifying write test...\n");
    af = open_testfile("test_random",0);

    for(i=0;i<MAX_FMTS;i++){
	char should[256];		// what we should get
	sprintf(should,fmt,i);
	int got = af_read(af,(unsigned char *)buf,fmt_size);
	if(got != fmt_size){
	    fprintf(stderr,"Attempt to read %d bytes; got %d\n",fmt_size,got);
	    exit(1);
	}
	if(i%250==24) printf("\r%d .. %d okay",i-24,i);
    }
    af_close(af);
    printf("\n");
    printf("\nRandom write test passes.\n");
    printf("======================\n");
    return 0;
}

int random_read_test(int total_bytes,int data_page_size)
{
    printf("\n\n\nrandom read test. filesize=%d, page_size=%d\n",
	   total_bytes,data_page_size);

    /* Create a regular file and an AFF file */

    printf("Creating random_contents.img and random_contents.%s, "
	   "both with %d bytes of user data...\n",
	   opt_ext,total_bytes);

    int    fd = open("test_random_contents.img",
		     O_CREAT|O_RDWR|O_TRUNC|O_BINARY,0666);
    if(fd<0) err(1,"fopen");

    AFFILE *af = open_testfile("test_random_contents",1);

    /* Just write it out as one big write */

    unsigned char *buf = (unsigned char *)malloc(total_bytes);
    unsigned char *buf2 = (unsigned char *)malloc(total_bytes);

    /* First half is random */
    RAND_bytes(buf,total_bytes/2);

    /* Second half is a bit more predictable */
    for(int i=total_bytes/2;i<total_bytes;i++){
	buf[i] = ((i % 256) + (i / 256)) % 256;
    }

    if(write(fd,buf,total_bytes)!=total_bytes) err(1,"fwrite");
    if(af_write(af,buf,total_bytes)!=(int)total_bytes) err(1,"af_write");

    /* Now try lots of seeks and reads */
    for(int i=0;i<MAX_FMTS;i++){
	uint32_t loc = rand() % total_bytes;
	uint32_t len = rand() % total_bytes;
	memset(buf,0,total_bytes);
	memset(buf2,0,total_bytes);

	if(i%250==0) printf("\r#%d  reading %"PRIu32" bytes at %"PRIu32"    ...",i,loc,len);
	fflush(stdout);

	uint32_t l1 = (uint32_t)lseek(fd,loc,SEEK_SET);
	uint32_t l2 = (uint32_t)af_seek(af,loc,SEEK_SET);


	if(l1!=l2){
	    err(1,"l1 (%"PRIu32") != l2 (%"PRIu32")",l1,l2);
	}

	int r1 = read(fd,buf,len);
	int r2 = af_read(af,buf2,len);

	if(r1!=r2){
	    err(1,"r1 (%d) != r2 (%d)",r1,r2);
	}
    }
    af_close(af);
    close(fd);
    printf("\nRandom read test passes\n");
    return 0;
}

void large_file_test()
{
    int pagesize = 1024*1024;		// megabyte sized segments
    int64_t num_segments = 5000;
    int64_t i;
    char fn[1024];

    printf("Large file test... Creating a %"I64d"MB file...\n",pagesize*num_segments/(1024*1024));
    filename(fn,sizeof(fn),"large_file");
    AFFILE *af = af_open(fn,O_CREAT|O_RDWR|O_TRUNC,0666);

    unsigned char *buf = (unsigned char *)malloc(pagesize);

    memset(buf,'E', pagesize);
    af_enable_compression(af,opt_compression_type,opt_compression_level);
    af_set_pagesize(af,pagesize);
    af_set_maxsize(af,(int64_t)pagesize * 600);

    for(i=0;i<num_segments;i++){
	sprintf((char *)buf,"%"I64d" page is put here",i);
	if(i%25==0) printf("\rWriting page %"I64d"\r",i);
	if(af_write(af,buf,pagesize)!=pagesize){
	    err(1,"Can't write page %"I64d,i);
	}
    }
    printf("\n\n");
    /* Now let's just read some test locations */
    for(i=0;i<num_segments;i+=num_segments/25){	// check a few places
	int r;
	af_seek(af,pagesize*i,SEEK_SET);
	r = af_read(af,buf,1024);		// just read a bit
	if(r!=1024){
	    err(1,"Tried to read 1024 bytes; got %d\n",r);
	}
	if(atoi((char *)buf)!=i){
	    err(1,"at page %"I64d", expected %"I64d", got %s\n",i,i,buf);
	}
	printf("Page %"I64d" validates\n",i);
    }

    af_close(af);
    if(unlink("large_file.aff")){
	err(1,"Can't delete large_file.aff");
    }
    printf("Large file test passes\n");
}

void maxsize_test()
{
    printf("Maxsize test. This test is designed to test creation of files\n");
    printf("Larger than 4GB. Currently it's disabled, though.\n");
#if 0
    char segname[16];
    char buf[1024];
    char fn[1024];
    int numpages = 1000;

    AFFILE *af = af_open(filename(fn,sizeof(fn),"maxsize"),O_CREAT|O_RDWR|O_TRUNC,0666);
    memset(buf,0,sizeof(buf));
    for(int64_t i=0;i<numpages;i++){
	sprintf(buf,"This is page %"I64d". ****************************************************\n",i);
	sprintf(segname,AF_PAGE,i);
	af_update_seg(af,segname,0,buf,sizeof(buf));
    }
    af_close(af);
    printf("\nMaxsize test passes.\n");
#endif
    printf("\n====================\n");
}

void sparse_test()
{
    printf("Sparse test...\n");

    char buf[1024];
    char fn[1024];

    uint64_t mult = (uint64_t)3 * (uint64_t)1000000000;		// 3GB in

    AFFILE *af = af_open(filename(fn,sizeof(fn),"sparse"),O_CREAT|O_RDWR|O_TRUNC,0666);
    af_enable_compression(af,opt_compression_type,opt_compression_level);
    af_set_maxsize(af,(int64_t)1024*1024*256);
    af_set_pagesize(af,1024*1024*16);

    for(u_int i=0;i<10;i++){
	uint64_t pos = mult*i;
	memset(buf,0,sizeof(buf));
	snprintf(buf,sizeof(buf),"This is at location=%"I64u"\n",pos);
	af_seek(af,pos,SEEK_SET);
	af_write(af,(unsigned char *)buf,sizeof(buf));
    }

    /* Now verify */
    for(u_int i=0;i<10;i++){
	uint64_t pos = mult*i;
	uint64_t q;
	af_seek(af,pos,SEEK_SET);
	af_read(af,(unsigned char *)buf,sizeof(buf));
	char *cc = strchr(buf,'=');
	if(!cc){
	    printf("Garbage read at location %"I64u"\n.",pos);
	    exit(1);
	}
	if(sscanf(cc+1,"%"I64u,&q)!=1){
	    printf("Could not decode value at location %"I64u"(%s)\n",pos,cc+1);
	    exit(1);
	}
	if(pos!=q){
	    printf("Wrong value at location %"I64u"; read %"I64u" in error.\n", pos,q);
	    exit(1);
	}
    }

    /* Now seek to somewhere that no data has been written and see if we get 0s. */
    memset(buf,'g',sizeof(buf));
    af_seek(af,mult/2,SEEK_SET);
    ssize_t r = af_read(af,(unsigned char *)buf,sizeof(buf));
    if(r==0){
        printf("sparse test produces a read of 0 bytes. I guess this is the new behavior\n");
    } else {
        if(r!=sizeof(buf)){
            errx(1,"Tried to read %zd bytes at mult/2; got %zd bytes\n",sizeof(buf),r);
        }
        for(u_int i=0;i<sizeof(buf);i++){
            if(buf[i]!=0) err(1,"data error; buf[%d]=%d\n",i,buf[i]);
        }
    }

    /* Now try to read the last page in the file */
    unsigned char big_buf[65536];
    af_seek(af,9*mult,SEEK_SET);
    r = af_read(af,big_buf,sizeof(big_buf));
    if(r!=sizeof(buf)){
	errx(1,"Tried to read %zd bytes at the end of the file; got %zd bytes (should get %zd)",
	    sizeof(big_buf),r,sizeof(buf));
    }


    /* Now see if we can read past the end of the file */
    af_seek(af,11*mult,SEEK_SET);
    r = af_read(af,(unsigned char *)buf,sizeof(buf));
    if(r!=0) errx(1,"Tried to read past end of file; got %zd bytes (should get 0)",r);

    af_close(af);
    printf("\nSprase test passes.\n");
    printf("=====================\n\n");
}

void figure(const char *fn)
{
    struct af_figure_media_buf afb;

    int fd = open(fn,O_RDONLY);
    if(fd<0) err(1,"%s",fn);
    if(af_figure_media(fd,&afb)){
	err(1,"af_figure_media");
    }
    printf("sector size: %d\n",afb.sector_size);
    printf("total sectors: %"PRId64"\n",afb.total_sectors);
    printf("max read blocks: %"PRId64"\n",afb.max_read_blocks);
    exit(0);
}


void compress(const char *fname)
{
    int fd = open(fname,O_RDONLY,0666);
    if(fd<0) err(1,"%s",fname);

    struct stat st;
    if(fstat(fd,&st)) err(1,"stat");

    /* Allocate memory */
    char *buf = (char *)malloc(st.st_size);
    if(buf==0) errx(1,"malloc");

    if(read(fd,buf,st.st_size)!=st.st_size) err(1,"read");
    //size_t outSize = (int)((double)st.st_size * 1.05);
    //char *outBuffer = (char *)malloc(outSize);
    //size_t outSizeProcessed = 0;
}

void lzma_test()
{
#if defined(__FreeBSD__) && !defined(__APPLE__)
    _malloc_options = "XARV";
#endif
    //char *fn = "/usr/share/dict/web2";
    const char *fn = "/etc/motd";

    printf("starting up\n");
    FILE *f = fopen(fn,"r");
    if(!f) err(1,"%s",fn);

    struct stat st;
    if(fstat(fileno(f),&st)) err(1,"stat");

    /* Allocate memory */
    size_t buflen = st.st_size;
    printf("size=%qd\n",(long long)buflen);
    unsigned char *buf = (unsigned char *)malloc(buflen);
    if(buf==0) errx(1,"malloc");
    if(fread(buf,1,st.st_size,f)!=(size_t)st.st_size) err(1,"read");

    /* Allocate memory for the compressed buffer */
    size_t cbufsize = (int)(buflen*1.05);
    size_t cbuf_actual=0;
    unsigned char *cbuf = (unsigned char *)malloc(cbufsize);

#ifdef USE_LZMA
    lzma_compress(cbuf,&cbufsize,buf,st.st_size,9);
#endif
    printf("cbuf_actual=%d\n",(int)cbuf_actual);

    /* Now try to decompress */
    size_t outbuf_size = buflen*2;
    unsigned char *outbuf = (unsigned char *)malloc(outbuf_size);

#ifdef USE_LZMA
    lzma_uncompress(outbuf,&outbuf_size,cbuf,cbufsize);
#endif
    printf("cbuf[0]=%d\n",cbuf[0]);

    if(memcmp(buf,outbuf,outbuf_size)==0){
	printf("Decompression works!\n");
    }
}


void make_test_seg(u_char buf[1024],int num)
{
    memset(buf,0,sizeof(buf));
    sprintf((char *)buf,"This test %d. This is just a test. Stop thinking.\n",num);
}

int aestest()
{
    unsigned char keyblock[32];

    /* Make a key; doesn't need to be a good key; make it 256 bits */
    for(int i=0;i<32;i++){
	keyblock[i] = i;
    }

    AFFILE *af = af_open("crypto.aff",O_CREAT|O_RDWR|O_TRUNC,0666);
    if(!af) err(1,"af_open");
    if(af_set_aes_key(af,keyblock,256)) err(1,"af_set_aes_key");
    af_set_pagesize(af,65536);

    /* Now, let's write some data of various sizes */

    u_char test[1024],buf[1024],rbuf[1024];
    size_t  buflen = sizeof(buf);
    make_test_seg(test,0);
    for(u_int len=0;len<=strlen((const char *)test);len++){
	if(af_update_seg(af,"page0",0,test,len)) err(1,"af_update_seg len=%d",len);

	/* Now try to read the segment */
	memset(buf,0,sizeof(buf));
	buflen = sizeof(buf);
	if(af_get_seg(af,"page0",0,(unsigned char *)buf,&buflen)){
	    err(1,"Could not read encrypted segment with length %d.\n",len);
	}
	if(buflen!=len){
	    printf("size of returned segment = %zd ",buflen);
	    printf("(should be %d) \n",len);
	    exit(0);
	}
	if(memcmp(buf,test,len)!=0){
	    printf("does not match\n");
	    printf("  wanted: %s\n",test);
	    printf("  got: %s\n",buf);
	    exit(0);
	}
    }
    if(af_close(af)) err(1,"af_close");

    /* Now re-open the file, do not set the encryption key, and see if we can read it */
    int r;
    memset(buf,0,sizeof(buf));
    af = af_open("crypto.aff",O_RDONLY,0666);
    buflen = sizeof(buf);
    r = af_get_seg(af,"page0",0,(unsigned char *)buf,&buflen);
    if(r!=-1) {
	errx(1,"Error; attempt to read segment 'encrypted' succeded. It should have failed.");
    }

    /* Try to read 'encrypted/aes' */
    r = af_get_seg(af,"encrypted/aes",0,(unsigned char *)buf,&buflen);
    if(memcmp(buf,test,buflen)==0){
	errx(1,"Error: segment encrypted/aes wasn't actually encrypted.");
    }
    af_close(af);

    /* Now set the correct encryption key and see if we can read it */
    af = af_open("crypto.aff",O_RDONLY,0666);
    if(af_set_aes_key(af,keyblock,256)) err(1,"af_set_aes_key");
    buflen = sizeof(buf);
    memset(buf,0,sizeof(buf));
    r = af_get_seg(af,"page0",0,(unsigned char *)buf,&buflen);
    if(buflen != strlen((const char *)test)){
	errx(1,"Error: Could not read encrypted segment after re-opening file");
    }
    if(memcmp(buf,test,buflen)!=0) errx(1,"Error: Re-read of file produces wrong data.");
    printf("encrypted data read and decrypted: '%s'\n",buf);
    /* Try to read a segment that doesn't eixst */
    buflen = 0;
    if(af_get_seg(af,"encrypted2",0,0,&buflen)==0){
	errx(1,"Error: Attempt to get size of non-existant segment 'encrypted2' got %zd\n",buflen);
    }
    af_close(af);


    /* Now set the wrong encryption key and see if we can read it */
    memset(buf,0,sizeof(buf));
    af = af_open("crypto.aff",O_RDONLY,0666);
    keyblock[3] = 42;
    if(af_set_aes_key(af,keyblock,256)) err(1,"af_set_aes_key");
    buflen = sizeof(buf);
    r = af_get_seg(af,"page0",0,(unsigned char *)buf,&buflen);
    if(memcmp(buf,test,buflen)==0) errx(1,"Error: Setting wrong key still produces correct data.");
    af_close(af);

    printf("Basic crypto checks. Now check passphrase....\n");

    /* Write the data with a passphrase and try to read it back */
    af = af_open("crypto_pass.aff",O_CREAT|O_RDWR|O_TRUNC,0666);
    if(!af) err(1,"af_open 3");
    af_set_pagesize(af,65536);
    if(af_establish_aes_passphrase(af,"yummy")) err(1,"af_establish_aes_passphrase");
    if(af_use_aes_passphrase(af,"yummy")) err(1,"af_use_aes_passphrase");
    if(af_update_seg(af,"page0",0,(const u_char *)test,strlen((const char *)test))) err(1,"af_update_seg failed at 3");
    if(af_close(af)) err(1,"af_close at 3");


    /* Now try to read it back */
    memset(rbuf,0,sizeof(rbuf));
    size_t rbuflen = sizeof(rbuf);
    af = af_open("crypto_pass.aff",O_RDONLY,0666);
    if(!af) err(1,"af_open 4");
    if(af_get_seg(af,"page0",0,(unsigned char *)buf,&buflen)==0){
	errx(1,"af_get_seg should have failed and didn't");
    }
    if(af_use_aes_passphrase(af,"yummy")) err(1,"af_set_passphrase 2");
    rbuflen=sizeof(rbuf);
    if(af_get_seg(af,"page0",0,(unsigned char *)rbuf,&rbuflen)){
	errx(1,"af_get_seg failed");
    }
    if(rbuflen!=strlen((const char *)test)) errx(1,"Reading encrypted data returned wrong size");
    if(memcmp(rbuf,test,rbuflen)!=0) errx(1,"Error: wrong data");
    printf("encrypted data read with passphrase 'yummy': %s\n",rbuf);
    af_close(af);

    /* Try to change the passphrase */
    af = af_open("crypto_pass.aff",O_RDWR,0666);
    if(!af) err(1,"af_open 5");
    if(af_change_aes_passphrase(af,"yummy","dummy")) err(1,"could not change passphrase");
    af_close(af);

    /* Try to read with new passphrase */
    af = af_open("crypto_pass.aff",O_RDONLY,0666);
    if(!af) err(1,"af_open 5");
    memset(rbuf,0,sizeof(rbuf));
    rbuflen = sizeof(rbuf);
    if(af_use_aes_passphrase(af,"dummy")) err(1,"af_set_passphrase 2");
    rbuflen=sizeof(rbuf);
    if(af_get_seg(af,"page0",0,(unsigned char *)rbuf,&rbuflen)){
	errx(1,"af_get_seg failed");
    }
    if(rbuflen!=strlen((const char *)test)) errx(1,"Reading encrypted with new passphrase data returned wrong size");
    if(memcmp(rbuf,test,rbuflen)!=0) errx(1,"Error: wrong data");
    printf("encrypted data read with new passphrase 'dummy': %s\n",rbuf);
    af_close(af);
    exit(0);


    /* Now try to read with the wrong passphrase */
    af = af_open("crypto.aff",O_RDONLY,0666);
    if(af_use_aes_passphrase(af,"yummy2")) err(1,"af_set_passphrase 3");
    buflen=sizeof(buf);
    memset(buf,0,sizeof(buf));
    if(af_get_seg(af,"page0",0,(unsigned char *)buf,&buflen)){
	printf("Couldn't get data with wrong passphrase (that's good)\n");
    }
    printf("data read with wrong passphrase: %s\n",buf);
    if(buflen>0 && memcmp(buf,test,buflen)==0){
	errx(1,"Error: data fetched with wrong passphrase was not scrambled.");
    }
    af_close(af);
    exit(0);
}

void readfile_test(const char *fname)
{
    unsigned char buf[1024];
    memset(buf,0,sizeof(buf));
    AFFILE *af = af_open(fname,O_RDONLY,0666);
    if(!af){
	af_perror(fname);
	err(1,"af_open(%s)",fname);
    }
    printf("using '%s'\n",af->v->name);
    printf("af_get_imagesize()=%"PRId64" errno=%d\n",af_get_imagesize(af),errno);

    int r = af_read(af,buf,sizeof(buf));
    printf("af_read(af,buf,1024)=%d  errno=%d\n",r,errno);
    r = fwrite(buf,1,512,stdout);
    assert(r==512);
    af_close(af);
    exit(0);
}

void zap(const char *fn)
{
    unsigned char buf[1024*1024];
    AFFILE *af = af_open(fn,O_RDWR,0666);
    if(!af) err(1,"af_open(%s)",fn);
    memset(buf,0,sizeof(buf));
    if(af_write(af,buf,sizeof(buf))!=sizeof(buf)){
	err(1,"af_write()");
    }
    af_close(af);
}

const char *fnames[] = {"foo.000","foo.001",
			"foo.100","foo.101",
			"bizmark.999","bizmark.A00",
			"nutter.A99","nutter.AA0",
			"bizmark.AZ9","bizmark.B00",
			"glutten.afj","glutten.afk",
			0,0};

void bugs_test()
{
    for(int i=0;fnames[i];i+=2){
	char buf[256];
	strcpy(buf,fnames[i]);
	if(split_raw_increment_fname(buf)){
	    err(1,"split_raw_increment_fname(%s) failed",fnames[i]);
	}
	printf("%s=>%s\n",fnames[i],buf);
	if(strcmp(buf,fnames[i+1])!=0){
	    err(1,"split_raw_increment_fname(%s) should have returned %s",
		fnames[i],fnames[i+1]);
	}
    }


    const char *buf = "This is a test\n";
    int len = strlen(buf);

    AFFILE *af = af_open("bugs.aff",O_RDWR|O_CREAT|O_TRUNC,0666);
    if(!af) err(1,"bugs.aff");
    int r = af_write(af,(unsigned char *)buf,strlen(buf));
    if(r!=len) err(1,"r=%d len=%d\n",r,len);
    af_close(af);
}


void rsatest();
void xmltest(const char *fn);

void time_test()
{
    exit(0);
}


#include <openssl/pem.h>
#include <openssl/bio.h>

void rsatest()
{
    const EVP_MD *sha256 = EVP_get_digestbyname("sha256");

    if(!sha256){
	fprintf(stderr,"SHA256 not available\n");
	return;
    }

    printf("Now try signing with X.509 certificates and EVP\n");

    char ptext[16];
    memset(ptext,0,sizeof(ptext));
    strcpy(ptext,"Simson");

    unsigned char sig[1024];
    uint32_t  siglen = sizeof(sig);

    BIO *bp = BIO_new_file("signing_key.pem","r");
    if (!bp) {
      perror("BIO_new_file");
      return;
    }

    EVP_MD_CTX *md = EVP_MD_CTX_new();
    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bp,0,0,0);

    EVP_SignInit(md,sha256);
    EVP_SignUpdate(md,ptext,sizeof(ptext));
    EVP_SignFinal(md,sig,&siglen,pkey);

    /* let's try to verify it */
    bp = BIO_new_file("signing_cert.pem","r");
    if (!bp) {
      perror("BIO_new_file");
      return;
    }

    X509 *x = 0;
    PEM_read_bio_X509(bp,&x,0,0);
    EVP_PKEY *pubkey = X509_get_pubkey(x);

    printf("pubkey=%p\n",pubkey);

    EVP_VerifyInit(md,sha256);
    EVP_VerifyUpdate(md,ptext,sizeof(ptext));
    int r = EVP_VerifyFinal(md,sig,siglen,pubkey);
    printf("r=%d\n",r);

    printf("do it again...\n");
    EVP_VerifyInit(md,sha256);
    EVP_VerifyUpdate(md,ptext,sizeof(ptext));
    r = EVP_VerifyFinal(md,sig,siglen,pubkey);
    printf("r=%d\n",r);

    printf("make a tiny change...\n");
    ptext[0]='f';
    EVP_VerifyInit(md,sha256);
    EVP_VerifyUpdate(md,ptext,sizeof(ptext));
    r = EVP_VerifyFinal(md,sig,siglen,pubkey);
    printf("r=%d\n",r);

    EVP_MD_CTX_free(md);
}

void xmlseg(BIO *bp,AFFILE *af,const char *segname)
{
    BIO_printf(bp,"  <segment>\n");
    BIO_printf(bp,"    <name>%s</name>\n",segname);
    /* Get the signature and base64 it (if we can) */
    u_char sigbuf[1024];
    size_t sigbuf_len = sizeof(sigbuf);
    char segname_sig[1024];
    strlcpy(segname_sig,segname,sizeof(segname_sig));
    strlcat(segname_sig,AF_SIG256_SUFFIX,sizeof(segname_sig));
    if(af_get_seg(af,segname_sig,0,sigbuf,&sigbuf_len)==0){
	char sigbuf48[2048];
	int  sigbuf48_len = b64_ntop(sigbuf,sigbuf_len,sigbuf48,sizeof(sigbuf48));
	sigbuf48[sigbuf48_len] = 0;	// null terminate
	BIO_printf(bp,"    <sig>%s</sig>\n",sigbuf48);
    }
    BIO_printf(bp,"  </segment>\n");
}

void xmltest(const char *fn)
{
    BIO *bp = BIO_new(BIO_s_mem());
    AFFILE *af = af_open(fn,O_RDONLY,0);
    if(!af) err(1,"%s",fn);
    char segname[AF_MAX_NAME_LEN];
    while(af_get_next_seg(af,segname,sizeof(segname),0,0,0)==0){
	xmlseg(bp,af,segname);
    }
    char *buf=0;
    ssize_t len = BIO_get_mem_data(bp,&buf);
    int r = fwrite(buf,1,len,stdout);
    assert(r==len);
}


void image_test()
{
    char fn[1024];
    printf("Imaging test...\n");
    filename(fn,sizeof(fn),"test_image");
    unlink(fn);				// make sure it is gone
    AFFILE *af = af_open(fn,O_CREAT|O_RDWR|O_TRUNC,0666);
    if(!af) err(1,"af_open");
}

void usage()
{
    printf("usage: %s [options]\n",progname);
    printf("    -e ext = use ext for extension (default is %s)\n",opt_ext);
    printf("    -p protocol = use protocol for protocol (default is %s)\n",opt_protocol);
    printf("    -a = do all tests (except -L)\n");
    printf("    -b = do the bugs test (tests things that were reported and fixed)\n");
    printf("    -1 = do sequential test\n");
    printf("    -2 = do reverse test\n");
    printf("    -3 = do random write test\n");
    printf("    -4 = do random read test\n");
    printf("    -5 = do maxsize multi-file test\n");
    printf("    -6 = sparse file test\n");
    printf("    -B = run large file test (needs 5GB of disk)\n");
    printf("    -L = use LZMA compression\n");
    printf("    -r = perform RSA tests\n");
    printf("    -d<dir> = use <dir> as the working dir for files (default is %s)\n",tempdir);
    printf("    -D<filename> = write debugging trace to <filename>\n");
    printf("    -f<dev> = run af_figure_media on dev and print the results\n");
    printf("    -c filename = compress filename and output to stdout\n");
    printf("    -T = just test the LZMA compression\n");
    printf("    -q = quite; just report errors\n");
    printf("    -R filename = just try to read the first 1024 bytes of a file and print what happens...\n");
    printf("    -t = run some timing tests\n");
    printf("    -n nn = sets MAX_FMTS (default %d)\n",MAX_FMTS);
    printf("    -i image write speed test (lots of small pages)\n");
    printf("    -v = verbose\n");
    printf("    -S filename = perform split-raw tests on filename\n");
#ifdef HAVE_AES_ENCRYPT
    printf("    -C = just test AES encryption\n");
#endif
    printf("    -x fn = xml test\n");
    printf("    -z fn = open up fn for writing and zap it.\n");
}

int split_raw_test(const char *fn)
{
    void srp_dump(AFFILE *af);
    AFFILE *af = af_open(fn,O_RDONLY,0666);
    if(!af) err(1,"af_open:%s",fn);
    printf("split_raw imagesize: %"PRId64"\n",af_get_imagesize(af));
    srp_dump(af);
    af_close(af);
    return 0;
}

int main(int argc,char **argv)
{
    progname = argv[0];
    int do_bugs = 0;
    int do_sequential = 0;
    int do_reverse    = 0;
    int do_random_write_test = 0;
    int do_random_read_test  = 0;
    int do_large_file = 0;
    int do_maxsize_test = 0;
    int random_repeat = 1;
    int do_sparse_test = 0;
    int do_all=0;
    int do_image_test =1;
    int ch;

    const char *bigdir = getenv(AFFLIB_BIGTMP);	// use by default

    if(bigdir) tempdir = bigdir;

    setvbuf(stdout,0,_IONBF,0);
    //putenv(AFFLIB_CACHE_STATS"=1");

    if(argc==1){
	printf("running all tests with -a option (exception bigfile test)\n");
	do_all = 1;
    }

    while ((ch = getopt(argc, argv, "b123456aBLd:h?f:e:c:TCp:rx:R:z:tn:D:S:")) != -1) {
	switch(ch){
	case 'D': af_trace = fopen(optarg,"w");break;
	case 'R': readfile_test(optarg); break;
	case 'b':
	    do_bugs = 1;
	    break;
	case '1':
	    do_sequential = 1;
	    break;
	case '2':
	    do_reverse = 1;
	    break;
	case '3':
	    do_random_write_test = 1;
	    break;
	case '4':
	    do_random_read_test = 1;
	    break;
	case '5':
	    do_maxsize_test = 1;
	    break;
	case '6': do_sparse_test = 1; break;
	case 'l':
	    random_repeat = atoi(optarg);
	    break;
	case 'B':
	    do_large_file = 1;
	    break;
	case 'n': MAX_FMTS = atoi(optarg); break;
	case 't': time_test(); break;
	case 'L': opt_compression_type = AF_COMPRESSION_ALG_LZMA; break;
	case 'T': lzma_test(); break;
	case 'r': rsatest(); break;
	case 'a':
	    do_all = 1;
	    break;
	case 'z':
	    zap(optarg);break;
	case 'd': tempdir = optarg; break;
	case 'f': figure(optarg); break;
	case 'e': opt_ext = optarg; break;
	case 'c': compress(optarg); break;

	case 'p': opt_protocol = optarg; break;
	case 'x': xmltest(optarg);break;
	case 'C': aestest(); break;
	case 'i': do_image_test=1;break;
	case 'S': split_raw_test(optarg);exit(0);
	case 'h':
	case '?':
	default:
	    usage();
	}
    }

    if(do_bugs || do_all) bugs_test();
    if(do_sequential || do_all) sequential_test();
    if(do_reverse || do_all ) reverse_test();
    if(do_maxsize_test || do_all) maxsize_test();
    if(do_sparse_test || do_all) sparse_test();
    if(do_image_test || do_all) image_test();

    for(int i=0;i<random_repeat;i++){
	if(do_random_read_test  || do_all) random_read_test(256*1024,rand() % 65536);
	if(do_random_write_test || do_all) random_write_test();
    }

    if(do_large_file) large_file_test();

    /* Now erase the files ... */
    unlink("test_random.aff");
    unlink("test_reverse.aff");
    unlink("test_random_contents.aff");
    unlink("test_sequential.aff");
    unlink("bugs.aff");
    unlink("test_random_contents.img");
    unlink("sparse.aff");
    return 0;
}


