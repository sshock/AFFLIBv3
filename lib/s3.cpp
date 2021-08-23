/*
 * s3.cpp:
 * The stand-alone S3 program.
 *
 * These features would be nice:
 * have "ls" use Delimiter option to just list the drives
 * Give "ls" an option to list just the AFF files.
 * Have implementation read a list of all the segments on open, and cache this.
 *
 * Distributed under the Berkeley 4-part license.
 * Simson L. Garfinkel, March 12, 2012
 */

#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"
#include "aftimer.h"

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#ifdef USE_S3
#include "s3_glue.h"

#include <errno.h>

#ifdef HAVE_OPENSSL_MD5_H
#include <openssl/md5.h>
#endif



char *outfile = 0;
char *opt_bucket = 0;
int opt_flag  = 0;
int opt_meta = 0;
int verbose = 0;
int tag = 0;

using namespace s3;

#define BANDWIDTH_PREFIX ".bandwidth_test"
#define BANDWIDTH_DEFAULT_SIZE 1000000
int bandwidth_offset = 0;

#ifndef HAVE_STRLCPY
size_t strlcpy(char *dest,const char *src,size_t dest_size)
{
    strncpy(dest,src,dest_size);
    dest[dest_size-1] = '\000';
    return strlen(dest);
}
#endif

#ifndef HAVE_STRLCAT
size_t strlcat(char *dest,const char *src,size_t dest_size)
{
    int dest_len = strlen(dest);
    int src_len  = strlen(src);
    int room     = dest_size - (dest_len +src_len+1);
    if(room>0){
	/* There is room; just copy over what we have and return */
	strcat(dest,src);
	return strlen(dest);
    }
    /* Not room; figure out how many bytes we can copy... */
    int left = dest_size - (dest_len+1);
    strncpy(dest+dest_len,src,left);
    dest[dest_len-1] = '\000';
    return strlen(dest);
}
#endif


void s3_df()
{
    class s3_result *e = list_buckets();
    if(!e->lambr) errx(1,"S3 did not return ListAllMyBucketsResult.");
    printf("Owner ID: %s\n",e->lambr->OwnerID.c_str());
    printf("Owner Display Name: %s\n",e->lambr->OwnerDisplayName.c_str());
    printf("\n");
    for(vector<Bucket *>::const_iterator i = e->lambr->Buckets.begin();
	i != e->lambr->Buckets.end();
	i++){
	printf("%s %s\n",(*i)->CreationDate.c_str(),(*i)->Name.c_str());
    }
    exit(0);
}

typedef vector <Contents> cvector;
void s3_ls(FILE *out,const char *prefix,cvector *cv)
{
    uint64_t total=0;
    string bucket = opt_bucket;
    if(out) fprintf(out,"S3 BUCKET %s:",bucket.c_str());
    if(strlen(prefix)>0){
	if(out) fprintf(out,"PREFIX %s",prefix);
    }
    if(out) fprintf(out,"\n\n");
    string marker;
    class s3_result  *e;
    bool isTruncated = false;
    do {
	e = list_bucket(bucket,prefix,marker,00);
	if(e==0) err(1,"Error loading bucket.");
	if(e->lbr==0) err(1,"Error: no LBR");
	if(e->lbr->contents.size()==0){
	    delete e;
	    break;			//
	}
	for(vector<Contents *>::const_iterator i = e->lbr->contents.begin();
	    i != e->lbr->contents.end();
	    i++){

	    if(cv) cv->push_back(**i);
	    /* Make date nice */
	    char tstamp[64];
	    strlcpy(tstamp,(*i)->LastModified.c_str(),sizeof(tstamp));
	    tstamp[10] = ' ';
	    tstamp[19] = '\000';

	    if(out){
		fprintf(out,"%s ",(*i)->OwnerDisplayName.c_str());
		fprintf(out,"%8d  ",(int)(*i)->Size);
		fprintf(out,"%s ",tstamp);
		fprintf(out,"%s ",(*i)->Key.c_str());
		fprintf(out,"\n");
	    }
	    total += (*i)->Size;
	}

	/* "To get the next page of results use the last key of the
	 *  current page as the marker."
	 */

	marker = e->lbr->contents.back()->Key;
	isTruncated = e->lbr->IsTruncated;
	delete e;
    } while(isTruncated);

    if(out) fprintf(out,"Total: %" PRId64 "\n",total);
}

void s3_cat(int argc, char **argv)
{
    argc--;argv++;
    while(*argv){
	string key(*argv);
	class response_buffer *b = 0;
	if(opt_meta==0){
	    b = object_get(opt_bucket,key,0);
	}
	else {
	    b = object_head(opt_bucket,key,0);
	}
	if(!b) errx(1,"HTTP transport error");
	if(b->result == 404) errx(1,"S3: %s not found",key.c_str());
	fwrite(b->base,1,b->len,stdout);
	delete b;
	argv++;
	argc--;
    }
}

/* s3 doesn't give an error if you try to delete an object that doesn't exist */
void s3_rm(int argc,char **argv)
{
    argc--;argv++;
    while(*argv){
	printf("s3 rm %s\n",*argv);
	int r = object_rm(opt_bucket,*argv);
	if(r) errx(1,"HTTP transport error");
	argv++;
	argc--;
    }
    exit(0);
}

/* del prefix */
void s3_delp(int argc,char **argv)
{
    argc--;argv++;

    FILE *f = stdout;
    char line[80];
    cvector cv;
    if(!strcmp(argv[0],"-q")){
	f = fopen("/dev/null","w");
	argv++;
	argc--;
    }


    if(argc!=1) errx(1,"delp requires a single argument");
    s3_ls(f,*argv,&cv);
    if(cv.size()==0) errx(0,"No items to delete");
    printf("Really delete %d item%s?\n",cv.size(),cv.size()==1 ? "" : "s");
    if(fgets(line,sizeof(line),stdin)==NULL) errx(1,"fgets failed");
    if(line[0]!='y' && line[0]!='Y') errx(1,"Aborted");
    for(cvector::iterator i=cv.begin();
	i!=cv.end();
	i++){
	printf("s3 rm %s\n",i->Key.c_str());
	if(object_rm(opt_bucket,i->Key.c_str())){
	    warn("HTTP error");
	}
    }
}

void s3_cp(int argc, char **argv)
{
    argc--;
    argv++;
    char * fname = argv[0];
    char * key = argv[1];
    struct s3headers meta[2] = {{0,0},{0,0}};
    char buf[64];

    if(opt_flag){
	snprintf(buf,sizeof(buf),"%d",opt_flag);
	meta[0].name = AMAZON_METADATA_PREFIX "arg";
	meta[0].value = buf;
    }

    /* Read from fname into a buffer.
     * Note that we do this with read, so that we can read from stdin
     */
    FILE *f = fopen(fname,"r");
    if(!f) err(1,"%s",fname);
    class response_buffer inbuf;
    while(!feof(f)){
	char buf[65536];
	int  count;
	count = fread(buf,1,sizeof(buf),f);
	if(count>0){
	    inbuf.write(buf,count);
	}
    }
    if(object_put(opt_bucket,key,inbuf.base,inbuf.len,meta)){
	errx(1,"%s: ",fname);
    }
    exit(0);
}

void s3_mkdir(int argc,char **argv)
{
    argc--;argv++;
    while(argc>0){
	if(bucket_mkdir(*argv)) err(1,"%s",*argv);
	argc--;
	argv++;
    }
    exit(0);
}

void s3_rmdir(int argc, char **argv)
{
    argc--;argv++;
    while(argc>0){
	string bucket(*argv);
	if(bucket_rmdir(bucket)) errx(1,"%s",bucket.c_str());
	argc--;
	argv++;
    }
    exit(0);
}



void usage()
{
    printf("s3 testing program.\n\n");
    printf("Usage:\n");
    printf("s3 ls (or dir) [prefix] - list the bucket's contents\n");
    printf("s3 mkdir            - make a bucket\n");
    printf("s3 rmdir            - delete a bucket\n");
    printf("s3 df               - display all of the buckets\n");
    printf("s3 rm key           - delete key from the bucket\n");
    printf("s3 cat key          - send the contents of the key to stdout\n");
    printf("s3 cp fname key     - copy local file fname to key\n");
    printf("s3 delp [-q] prefix - Delete all keys with 'prefix' as a prefix\n");
    printf("\n");
    printf("Debugging commands:\n");
    printf("s3 regress            - run regression tests\n");
    printf("s3 bandwidth [-m] nn  - measure bandwidth \n");
    printf("                      Use -m to make the files that are needed for read testing.\n");
    printf("\n");
    printf("Options:\n");
    printf("   -d = enable HTTP debugging\n");
    printf("   -b <bucket>  = Specifies bucket\n");
    printf("   -fnn = specify a 32-bit metadata 'flag'\n");
    printf("   -m = just report the metadata (for cat)\n");
    printf("   -O = set bandwidth offset (default 0)\n");
    printf("   -u url = go to url instead of %s\n",aws_base_url);
    printf("   -o fn = append output to file fn\n");
    printf("   -V  verbose\n");
    printf("   -v  print version number and exit\n");
    printf("   -t tag = tag to append to output\n");
    exit(0);
}

/****************************************************************/

/* Regression testing */
int regress()
{
    /* Make some data */
    for(int i=0;i<100;i++){
	char name[1024];
	char value[1024];
	snprintf(name,sizeof(name),"bucket%d",i);
	snprintf(value,sizeof(value),"This is the contents of bucket %d\n",i);
	object_put(opt_bucket,name,value,strlen(value),0);
    }

    for(int i=0;i<100;i++){
	if(i%10==0) printf("\n");
	class s3_result  *e = list_bucket(opt_bucket,"","",0);
	if(!e->lbr) err(1,"Error loading bucket pass %d\n",i);
	delete e;
	printf("%d ",i);
	fflush(stdout);
    }
    printf("Done. Check for memory leaks, then press any key...\n");
    getchar();
    exit(0);
}

void s3_bandwidth(int argc,char **argv)
{
    int opt_make = opt_meta;	// in case it was set
    int read_retry=0;
    int write_retry=0;
    int write_err=0;
    int opt_write_test=1;
    const char *opt_url = 0;

    argc--;argv++;

    if(argc>0 && strcmp(argv[0],"-m")==0){
	if(verbose) fprintf(stderr,"opt_make\n");
	opt_make = 1;
	argc--;
	argv++;
    }

    /* Bandwidth testing requires bandwidth_test0 through 9.
     * If they don't exist, make them.
     */
#if defined(HAVE_SRANDOMDEV)
    srandomdev();
#endif
#if !defined(HAVE_SRANDOMDEV) && defined(HAVE_SRANDOM)
    srandom(time(0));
#endif

    size_t size = BANDWIDTH_DEFAULT_SIZE;
    if(argc>0) size=atoi(argv[0]);
    if(verbose) fprintf(stderr,"size=%d\n",size);
    if(size==0) err(1,"size=0");
    char *buf = (char *)malloc(size);
    if(!buf) err(1,"malloc");
    memset(buf,'E',size);

    if(argc>0) size=atoi(argv[0]);

    if(strncmp(argv[0],"http://",7)==0){
	opt_url = argv[0];
	opt_write_test = 0;
    }

    char base[1024];
    char randp[1024];
    snprintf(base,sizeof(base),"%s.%d",BANDWIDTH_PREFIX,size);
    snprintf(randp,sizeof(randp),"%s.%d.%d",BANDWIDTH_PREFIX,size,(int)(random() % 1000));


    aftimer twrite;

    if(opt_write_test || opt_make){
	/* Measure the write bandwidth */
	twrite.start();

	if(opt_make) strcpy(randp,base);		// just write to this one
	char wkey[1024];
	snprintf(wkey,sizeof(wkey),"%s.%d",randp,bandwidth_offset);
	object_put(opt_bucket,wkey,buf,size,0);
	if(verbose) fprintf(stderr," wrote %s/%s\n",opt_bucket,wkey);
	write_retry += s3_request_retry_count;
	write_err   += s3_object_put_retry_count;

	twrite.stop();

	if(opt_make) exit(0);		// file made

	/* Delete the writes */
	if(object_rm(opt_bucket,wkey)){
	    err(1,"object_rm failed\n");
	}
	if(verbose) fprintf(stderr," deleted %s/%s\n",opt_bucket,wkey);

    }

    /* Now measure the read bandwidth */
    aftimer tread;
    tread.start();

    char rkey[1024];
    snprintf(rkey,sizeof(rkey),"%s.%d",base,bandwidth_offset);
    response_buffer *r=0;

    if(opt_url==0){
	r = object_get(opt_bucket,rkey,0);
	if(!r || r->result==404){
	    err(1,"object_get(%s/%s) failed (%d)",opt_bucket,rkey,errno);
	}
    }
    else {
	r = s3::get_url(opt_url);
	if(r) size = r->len;
    }
    read_retry += s3_request_retry_count;
    tread.stop();
    if(verbose) fprintf(stderr," read %s/%s\n",opt_bucket,rkey);

    char line[1024];
    memset(line,0,sizeof(line));
    time_t t = time(0);
    struct tm *tm = gmtime(&t);
    char tbuf[64];
    strftime(tbuf,sizeof(tbuf),"%F %T",tm);

    char hextag[64];
    memset(hextag,0,sizeof(hextag));
    for(int i=0;i<16;i++){
	sprintf(hextag+i*2,"%02x",r->ETag[i]);
    }

    FILE *out = outfile ? fopen(outfile,"a") : stdout;

    //snprintf(line,sizeof(line),"v3: %s\t%d\t%d\t%f\t%f\t%d\t%d\t%d\t%s
    fprintf(out,"v3: %s\t",tbuf);

    fprintf(out,"%d\t%d\t",size,bandwidth_offset);
    fprintf(out,"%f\t",twrite.elapsed_seconds());
    fprintf(out,"%f\t",tread.elapsed_seconds());
    fprintf(out,"%d\t%d\t%d\t",write_retry,write_err,read_retry);
    fprintf(out," ");
    /* Now verify the MD5 */
    unsigned char md5[16];
    memset(md5,0,sizeof(md5));
    MD5((const unsigned char *)r->base,r->len,md5);
    if(memcmp(r->ETag,md5,16)!=0){
	fprintf(out," FAILED != ");
	for(int i=0;i<16;i++) fprintf(out,"%02x",md5[i]);
    }

    if(tag) fprintf(out,"\t%d ",tag);
    fprintf(out,"\n");
    fclose(out);
    if(r) delete r;
    exit(0);
}


/****************************************************************/


int main(int argc,char **argv)
{
    int ch;

    opt_bucket = getenv(S3_DEFAULT_BUCKET);
    while ((ch = getopt(argc, argv, "b:dVh?f:mc:O:u:o:vt:")) != -1) {
	switch (ch) {
	case 'O': bandwidth_offset = atoi(optarg);break;
	case 'd': s3_debug++;break;
	case 'b': opt_bucket = optarg;break;
	case 'f': opt_flag = atoi(optarg);break;
	case 'm': opt_meta = 1;break;
	case 'v':
	    printf("%s version %s\n",argv[0],PACKAGE_VERSION);
	    exit(0);
	case 'u': aws_base_url = optarg;break;
	case 'o': outfile = optarg;break;
	case 'V': verbose++;break;
	case 't': tag = atoi(optarg);break;
	case 'h':
	case '?':
	default:
	    usage();
	}
    }
    argc -= optind;
    argv += optind;

    if(argc<1){
	usage();
    }

    if(getenv(S3_DEBUG)){
	s3_debug = atoi(getenv(S3_DEBUG));
	fprintf(stderr,"s3_debug set to %d\n",s3_debug);
#ifdef HAVE_ERR_SET_EXIT
	err_set_exit(s3_audit);
#endif
    }

    aws_access_key_id     = getenv(AWS_ACCESS_KEY_ID);
    aws_secret_access_key = getenv(AWS_SECRET_ACCESS_KEY);
    if(!aws_access_key_id) fprintf(stderr,"s3: AWS_ACCESS_KEY_ID not defined\n");
    if(!aws_secret_access_key) fprintf(stderr,"s3: AWS_SECRET_ACCESS_KEY not defined\n");
    if(!aws_access_key_id || !aws_secret_access_key) return -1; /* can't open */
    if(!opt_bucket) {
	fprintf(stderr,"No bucket. Please setenv S3_DEFAULT_BUCKET or specify -b option.\n");
	exit(1);
    }


    char *cmd = argv[0];

    if(!strcmp(cmd,"ls") || !strcmp(cmd,"dir")){
	const char *prefix = argc>1 ? argv[1] : "";
	s3_ls(stdout,prefix,0);exit(0);
    }
    if(!strcmp(cmd,"df")) {s3_df();exit(0);}
    if(!strcmp(cmd,"cat")) {s3_cat(argc,argv);exit(0);}
    if(!strcmp(cmd,"rm")) {s3_rm(argc,argv);exit(0);}
    if(!strcmp(cmd,"cp") || !strcmp(cmd,"put") || !strcmp(cmd,"send")) {
	if ( argc != 3 ) {usage();exit(1);}
	s3_cp(argc,argv);exit(0);
    }
    if(!strcmp(cmd,"mkdir")) {s3_mkdir(argc,argv);exit(0);}
    if(!strcmp(cmd,"rmdir")) {s3_rmdir(argc,argv);exit(0);}
    if(!strcmp(cmd,"regress")) {regress();exit(0);}
    if(!strcmp(cmd,"delp")) {s3_delp(argc,argv);exit(0);}
    if(!strcmp(cmd,"bandwidth")) {
	s3_bandwidth(argc,argv);exit(0);
    }
    usage();
    exit(0);
}
#else
int main(int argc,char **argv)
{
    fprintf(stderr,"S3 is not compiled in.\n");
    exit(0);
}
#endif
