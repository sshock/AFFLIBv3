/*
 * affcopy.cpp:
 *
 * Copy one AFF file to another. 
 * Resulting file is re-ordered and possibly re-compressed.
 * Distributed under the Berkeley 4-part license
 */


#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"
#include "utils.h"
#include "base64.h"
#include "aff_bom.h"

using namespace std;
using namespace aff;

#ifdef HAVE_SYS_SIGNAL_H
#include <sys/signal.h>
#endif

#ifdef HAVE_TIME_H
#include <time.h>
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include <ctype.h>
#include <zlib.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <assert.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef WIN32
#include "unix4win32.h"
#endif

const char *progname = "affcopy";

int opt_verbose = 0;
int opt_debug = 0;
int opt_x = 0;
int opt_X = AF_COMPRESSION_DEFAULT;
int opt_noverify = 0;
int opt_preen = 0;
int opt_zap =0;
int opt_missing = 0;
int opt_preen_alg_arg = 0;			// algorithm for recompressing
int opt_preen_alg_flag = 0;
int opt_sign = 0;

int opt_note = 0;
const char *opt_sign_key_file = 0;
const char *opt_sign_cert_file = 0;

void usage()
{
    printf("%s version %s\n",progname,PACKAGE_VERSION);
    printf("usage: %s [options] file1 file\n",progname);
    printf("                    Copies file1 to file2\n");
    printf("       %s [options] file1 file2 file3 ... dir\n",progname);
    printf("                    Copies file1.. into dir\n");
    printf("       %s [options] file1 file2 file3 ... dir1 dir2...\n",progname);
    printf("                    Copies file1.. into dirs1, dir2, ...\n");
    printf("\n");
    printf("By default, all page MACs are verified on read and all segments\n");
    printf("are verified after write.\n");
    
    printf("Options:\n");
    printf("   -v = verbose: print each file as it is copied\n");
    printf("   -vv = very verbose: print each segment as it is copied\n");
    printf("   -d = print debugging information as well\n");
    printf("   -x = don't verify hashes on reads\n");
    printf("   -y = don't verify writes\n");
    printf("   -Xn = recompress pages (preen) with zlib level n\n");
    printf("   -L  = recompress pages (preen) with LZMA (smaller but slower)\n");
    printf("\n");
    printf("   -h = help; print this message.\n");
    printf("   -V = print the program version and exit.\n");
    printf("   -z = zap; copy even if the destination exists.\n");
    printf("   -m = just copy the missing segments\n");
    printf("\nSignature Options:\n");
    printf("   -k filename.key   = specify private key for signing\n");
    printf("   -c filename.cer   = specify a X.509 certificate that matches the private key\n");
    printf("                       (by default, the file is assumed to be the same one\n");
    printf("                       provided with the -k option.)\n");
    printf("   -n  = read notes to accompany the copy from standard in.\n");
    printf("\n");
    printf("\nEncryption Options:");
    printf("   Specify passphrase encryption for filename.aff with:\n");
    printf("      file://:passphrase@/filename.aff\n");
    printf("\n");
    printf("Examples:\n");
    printf("       %s  file.aff   file://:mypassword@/file-encrypted.aff   - encrypt file.aff\n",progname);
#ifdef USE_S3
    printf("       %s -vy -X9 *.aff s3:///     Copy all files in current\n",progname);
    printf("                               directory to S3 default bucket with X9 compression\n");
#endif
    exit(1);
}


const char *current_source = 0;
const char *current_dest = 0;
const char *current_seg  = 0;
void sig_info(int arg)
{
    if(current_source){
	printf("Copying %s ",current_source);
	if(current_dest){
	    printf("--> %s",current_dest);
	    if(current_seg) printf(" (%s) ",current_seg);
	}
    }
    printf("\n");
}


void unlink_outfiles(vector<string> outfiles)
{
    int failure=0;
    for(vector<string>::const_iterator o = outfiles.begin();
	o != outfiles.end();
	o++){
	char *protocol=0;
	char *path = 0;
	af_parse_url(o->c_str(),&protocol,0,0,0,0,&path);
	if(strcmp(protocol,"file")==0){
	    unlink(path);
	}
	else{
	    fprintf(stderr,"Cannot unlink %s\n",o->c_str());
	    failure=1;
	}
	if(protocol) free(protocol);
	if(path) free(path);
    }
    if(failure) exit(1);
}

#if (!defined(__BSD_VISIBLE) || defined(__CYGWIN__)) && !defined(isnumber)
#define isnumber(x) isdigit(x)
#endif

#ifdef WIN32
#include <windows.h>
#include <windowsx.h>
int gettimeofday (struct timeval *tv, void* tz)
{
    union {
	int64_t ns100; /*time since 1 Jan 1601 in 100ns units */
	FILETIME ft;
    } now;

    GetSystemTimeAsFileTime (&now.ft);
    tv->tv_usec = (long) ((now.ns100 / 10LL) % 1000000LL);
    tv->tv_sec = (long) ((now.ns100 - 116444736000000000LL) / 10000000LL);
    return (0);
} 
#endif

void open_outfiles(AFFILE *ain,outlist &afouts,const vector<string> &outfiles)
{
    /* Open every output file */
    for(vector<string>::const_iterator o = outfiles.begin();
	o != outfiles.end(); o++){

	const char *outfilename = o->c_str();
	outelement out;

	/* First see if the output file exists */
	out.af = 0;
	int ident = af_identify_file_type(outfilename,1);
	if(ident!=AF_IDENTIFY_NOEXIST){
	    fprintf(stderr,"%s: file exists...  ",outfilename);
	    if(opt_zap==0 && opt_missing==0){
		fprintf(stderr,"\n   Will not overwrite; use -m or -z\n");
		continue;
	    }
	    if(opt_missing){
		fprintf(stderr,"Will fill in missing segments...\n");
		out.af = af_open(outfilename,O_RDWR|O_EXCL,0666);
		if(!out.af) af_err(1,outfilename);
		if(af_page_size(ain) != af_page_size(out.af)){
		    fprintf(stderr,"%s and %s have different page sizes (%d != %d)\n",
			    af_filename(ain),
			    af_filename(out.af),
			    af_page_size(ain),
			    af_page_size(out.af));
		    af_close(out.af);
		    out.af=0;
		    continue;
		}
	    }
	}



	if(out.af==0){
	    out.af = af_open(outfilename,O_RDWR|O_EXCL|O_CREAT,0666);
	    if(!out.af){
		warn("%s",outfilename);
		continue;
	    }
	    if(af_set_pagesize(out.af,af_page_size(ain))){
		errx(1,"%s: cannot set page size to %d\n", af_filename(out.af),af_page_size(ain));
	    }
	}
	if(o != outfiles.begin()) printf("\t ");
	if(opt_verbose){
	    printf(" => %s ",outfilename);
	    if(opt_preen) printf(" (preening) ");
	    printf("\n");
	}
	if(opt_missing) out.segs.get_seglist(out.af);
	afouts.push_back(out);
    }
}

/* Copy pagenumber from ain to aout.
 * Return 0 if success, -1 if can't do it.
 * Properly handles signing and preening if requested.
 */
int copy_page(AFFILE *ain,AFFILE *aout,int64_t pagenum,uint32_t arg,u_char *seghash,u_int *seghash_len)
{
    /* If we are preening but not signing, see if we can get out fast */
    if(opt_sign==0 && opt_preen==0) return -1; // not preening and not signing

    /* If we are not signing, don't bother decompressing and recompressing*/
    if(opt_sign==0 && opt_preen){
	int alg = (arg & AF_PAGE_COMP_ALG_MASK);
	if(alg==AF_PAGE_COMP_ALG_ZERO) return -1; // don't preen ZERO
	if(alg==opt_preen_alg_arg) return -1;     // don't decompress then re-compress with old alg
    }
    
    /* If we get here, page must be read into memory and decompressed */
    size_t pagesize = af_page_size(ain);
    if(pagesize<=0) return -1;		      // couldn't get pagesize

    u_char *pagebuf = (unsigned char *)malloc(pagesize);
    if(!pagebuf) return -1;		      // couldn't allocate memory for page?

    if(af_get_page(ain,pagenum,pagebuf,&pagesize)){ // note --- this may make pagesize smaller
	free(pagebuf);
	return -1;
    }

    if(opt_preen){			// set compression if we are preening
	af_enable_compression(aout,opt_preen_alg_flag,opt_X);
    }

#ifdef USE_AFFSIGS
    /* If calculating a bom, calculate the bom! */
    if(opt_sign){
	char segname[AF_MAX_NAME_LEN];
	sprintf(segname,AF_PAGE,pagenum);
	aff_bom::make_hash(seghash,arg,segname,pagebuf,pagesize);
    }
#endif

    /* Write out the page */
    int ret = af_update_page(aout,pagenum,pagebuf,pagesize);
    free(pagebuf);
    return ret;
}

string base64(const u_char *buf,size_t buflen)
{
    size_t len = buflen*2+1;
    char *str = (char *)malloc(len);
    b64_ntop(buf,buflen,str,len);
    string ret = string(str);
    free(str);
    return ret;
}

#ifndef HAVE_ISATTY
int	isatty(int fd)
{
    return 1;				// have to assume it's a tty
}
#endif

int affcopy(char *infile,vector<string> &outfiles)
{
#ifdef SIGINFO
    signal(SIGINFO,sig_info);
#endif
    hashMapT hashMap;
    
    /* Open the input file */
    AFFILE *ain = af_open(infile,O_RDONLY,0);
    if(opt_debug) printf("af_open(%s,O_RDONLY)=%p\n",infile,ain);
    if(!ain) af_err(1,"%s",infile);
    seglist segments(ain);

    if(opt_zap) unlink_outfiles(outfiles);

    outlist afouts;				    // vector of output AFFs
    vector<int64_t>preened_pages;
    open_outfiles(ain,afouts,outfiles);
    
    /* Now, try to open the output files, to see if they exist */
    current_source = infile;
    if(opt_verbose) printf("%s: ",infile);
    if(opt_verbose>1) putchar('\n');

    /* If we couldn't open any output files, return */
    if(afouts.size()==0){
	af_close(ain);			// close the input file 
	return -1;
    }

#ifdef USE_AFFSIGS
    /* If we are signing, initialize the signing machinery */
    aff_bom bom(opt_note);
    if(opt_sign){
	if(bom.read_files(opt_sign_cert_file,opt_sign_key_file)){
	    opt_sign = 0;		// can't sign
	}
    }
#endif

    /* Now the files are open. For each output file:
     * 1. Initialize signing if options were set and the segments aren't already signed.
     * 2. Sign all of the segments that are unsigned
     */
    for(outlist::iterator aout = afouts.begin(); aout != afouts.end(); aout++){
	if(opt_sign_key_file && segments.has_signed_segments()==false){
	    if(af_set_sign_files(aout->af,opt_sign_key_file,opt_sign_cert_file)){
		err(1,"%s",opt_sign_key_file);
	    }
	    af_sign_all_unsigned_segments(aout->af);
	    opt_sign = true;
	}
    }

    /* Start the copying */
    struct timeval t0,t1;
    gettimeofday(&t0,0);
    for(seglist::const_iterator seg = segments.begin(); seg!= segments.end();seg++){
	/* For each segment, get the size of the segment */
	const char *segname = seg->name.c_str();
	current_seg = segname;		// for printing
	size_t seglen=0;

	if(af_get_seg(ain,segname,0,0,&seglen)){
	    unlink_outfiles(outfiles);
	    err(1,"Cannot read length of segment '%s' on input file %s", segname,af_filename(ain));
	}
	unsigned char *segbuf = (unsigned char *)malloc(seglen);
	if(!segbuf){
	    unlink_outfiles(outfiles);
	    err(1,"Cannot allocated %d bytes for segment '%s' in %s",
		(int)seglen,segname,af_filename(ain));
	}

	/* Now get the raw source segment */
	uint32_t arg=0;
	if(af_get_seg(ain,segname,&arg,segbuf,&seglen)){
	    unlink_outfiles(outfiles);	// failure; unlink the output files
	    err(1,"Cannot read segment '%s' in %s. Deleting output file", segname,af_filename(ain));
	}

	/* Calculate the MD5 of this segment and remember it in the map */
	md5blob md5;
	MD5(segbuf,seglen,md5.buf);
	hashMap[segname] = md5;

	/* See if this is a page; if so, it is handled specially */
	int64_t pagenumber = af_segname_page_number(segname);

	/* Write the segment to each file */
	for(outlist::iterator aout = afouts.begin(); aout != afouts.end(); aout++){
	    current_dest = af_filename(aout->af);
	    if(opt_verbose>1 || opt_debug){
		if(aout != afouts.begin()) printf("\n  ");
		printf("  %s -> %s:%s ...", segname,af_filename(aout->af),segname);
	    }

	    /** COPY THE DATA **/

	    u_char seghash[32]; /* resultant message digest; could be any size */
	    unsigned int seghash_len = sizeof(seghash); /* big enough to hold SHA256 */
#ifdef USE_AFFSIGS
	    int sigmode = AF_SIGNATURE_MODE0;
#endif

	    memset(seghash,0,sizeof(seghash));

	    bool copied = false;

	    /* If we are preening, signing, or building a ToC, we need to copy the raw page */
	    if(pagenumber>=0 && (opt_preen || opt_sign_key_file)){
		if(copy_page(ain,aout->af,pagenumber,arg,seghash,&seghash_len)==0){
		    preened_pages.push_back(pagenumber); // preened pages won't be verified by md5
		    if(opt_debug && opt_preen) printf(" (PREENED) ");
#ifdef USE_AFFSIGS
		    sigmode = AF_SIGNATURE_MODE1;
#endif
		    copied = true;
		}
	    }

	    /* Copy the page if it is not in the destination */
	    if(copied==false){
		if(!aout->segs.contains(segname)){
		    if(af_update_seg(aout->af,segname,arg,segbuf,seglen)){
			unlink_outfiles(outfiles);
			err(1,"Cannot write segment '%s' to %s.", segname,af_filename(aout->af));
		    }
		    
#ifdef USE_AFFSIGS
		    if(opt_sign){
			aff_bom::make_hash(seghash,arg,segname,segbuf,seglen);
		    }
#endif
		}
		else{
		    if(opt_verbose>1 || opt_debug) printf(" [already in %s] ",af_filename(aout->af));
		}
	    }
#ifdef USE_AFFSIGS
	    if(opt_sign) bom.add(segname,sigmode,seghash,seghash_len);
#endif
	}
	free(segbuf);
	current_dest = 0;
	if(opt_verbose>1 || opt_debug) putchar('\n');
    }
    current_seg = 0;

#ifdef USE_AFFSIGS
    /* For each open file, make an AF_IMAGE_GID if one doesn't exist */
    for(outlist::iterator aout = afouts.begin(); aout != afouts.end(); aout++){
	if(af_make_gid(aout->af)>0){
	    if(opt_sign){
		af_sign_seg(aout->af,AF_IMAGE_GID); // make sure the GID is signed
		bom.add(aout->af,AF_IMAGE_GID);
		bom.add(aout->af,AF_IMAGE_GID AF_SIG256_SUFFIX);

	    }
	}
    }

    if(opt_sign){
	bom.close();
	/* Now write to each of the output files */
	for(outlist::iterator aout = afouts.begin(); aout != afouts.end(); aout++){
	    bom.write(aout->af,segments);
	}
    }
#endif

    gettimeofday(&t1,0);
    if(afouts.size()==1){
	AFFILE *af = afouts.begin()->af;
	uint64_t w = af->bytes_written;
	double sec = ((t1.tv_sec-t0.tv_sec)+(t1.tv_usec-t0.tv_usec)/1000000.0);
	printf("%s: %" I64d " bytes transferred in %.2f seconds. xfer rate: %.2f MBytes/sec\n",
	       af_filename(af),w,sec,(w/1000000.0) / sec);
    }
	
    if(opt_noverify==0){
	current_seg = "VERIFYING";
	/* Now verify all of the hashes */
	if(opt_verbose || opt_debug) printf("\n\nFiles copied. Verifying...\n");
	for(seglist::const_iterator seg = segments.begin(); seg!= segments.end();seg++){

	    const char *segname = seg->name.c_str();
	    for(outlist::iterator aout = afouts.begin(); aout != afouts.end(); aout++){
		size_t seglen=0;
		char b2[1024];

		if((aout->af)->v->flag & AF_VNODE_TYPE_RELIABLE){
		    continue;		// no need to verify a reliable write
		}
		if(opt_verbose>1 || opt_debug) printf("  verifying %s...\n",segname);

	    again:
		if(af_get_seg(aout->af,segname,0,0,&seglen)){
		    if(segname != b2 &&
		       segname[0]=='s' && segname[1]=='e' && segname[2]=='g' &&
		       isnumber(segname[3])){
			/* Looks like a legacy segname name was renamed.
			 * Try the new name
			 */
			snprintf(b2,sizeof(b2),"page%s",segname+3);
			if(opt_verbose) printf("  Couldn't read %s; looking for %s\n",
					       segname,b2);
			segname = b2;
			goto again;
		    }
		    unlink_outfiles(outfiles);
		    errx(1,"Cannot read length of segment '%s' in output file %s",
			 segname,af_filename(aout->af));
		}
		int64_t pagenumber = af_segname_page_number(segname);
		if(find(preened_pages.begin(),preened_pages.end(),pagenumber) !=preened_pages.end()){
		    /* TK: page pagenumber was preened.
		     * It should probably be checked against the original hash...
		     */
		    continue;
		}
	    
		unsigned char *segbuf = (unsigned char *)malloc(seglen);
		if(!segbuf){
		    err(1,"Cannot allocated %d bytes for segment '%s' in %s",
			(int)seglen,segname,af_filename(ain));
		}
		uint32_t arg;
		if(af_get_seg(aout->af,segname,&arg,segbuf,&seglen)){
		    err(1,"Cannot read segment '%s' in %s",
			segname,af_filename(aout->af));
		}

		/* Calculate the MD5 of this segment and see if it matches the map.
		 * (But don't do this for preened segments.
		 */
		unsigned char md5_read[16];
		MD5(segbuf,seglen,md5_read);
		if(memcmp(hashMap[segname].buf,md5_read,16)!=0){
		    unlink_outfiles(outfiles);
		    errx(1,"Hash read from %s for segment %s doesn't validate.",
			 af_filename(aout->af),segname);
		}
		free(segbuf);		// free the buffer
	    }
	}
    }

    /* Finally, close the output files*/
    for(outlist::iterator aout = afouts.begin(); aout != afouts.end(); aout++){
	af_close(aout->af);
    }
    af_close(ain);
    if(opt_verbose>1 || opt_debug) printf("==============================\n");
    current_source = 0;
    return 0;
}

int main(int argc,char **argv)
{
    int ch;

    setvbuf(stdout,0,_IONBF,0);		// turn off buffering on stdout
    while ((ch = getopt(argc, argv, "vdVxyh?zmX:Lp:P:k:c:n")) != -1) {
	switch (ch) {
	case 'v': opt_verbose++; break;
	case 'd': opt_debug++; break;
	case 'X':
	    opt_preen =1;
	    opt_X = optarg[0] - '0';
	    opt_preen_alg_arg  = AF_PAGE_COMP_ALG_ZLIB;
	    opt_preen_alg_flag = AF_COMPRESSION_ALG_ZLIB;
	    if(opt_X<0 || opt_X>9) opt_X = AF_COMPRESSION_DEFAULT;
	    break;
	case 'L':
	    opt_preen=1;
	    opt_preen_alg_arg = AF_PAGE_COMP_ALG_LZMA;
	    opt_preen_alg_flag = AF_COMPRESSION_ALG_LZMA;
	    break;
	case 'x': opt_x++;break;
	case 'y': opt_noverify++;break;
	case 'z': opt_zap++;break;
	case 'm': opt_missing++;break;
	case 'n': opt_note++;break;
	case 'k':
	    if(access(optarg,R_OK)) err(1,"%s",optarg);
	    opt_sign_key_file = optarg;
	    if(opt_sign_cert_file==0) opt_sign_cert_file=optarg;
	    opt_sign = true;
	    break;
	case 'c':
	    if(access(optarg,R_OK)) err(1,"%s",optarg);
	    opt_sign_cert_file = optarg;
	    break;
	case 'V':
	    printf("%s version %s\n",progname,PACKAGE_VERSION);
	    exit(0);
	case 'h':
	case '?':
	default:
	    usage();
	    break;
	}
    }
    argc -= optind;
    argv += optind;

    if(argc<2){				// at this point, we need at least two args
	usage();
    }

    /* We either need both a key file and a cert file, or neither */
    if((opt_sign_key_file==0) != (opt_sign_cert_file==0)){
	errx(1,"Both a private key and a certificate must be specified.");
    }

    /* Find any directories */
    vector<string> dirlist;
    for(int i=argc-1;i>0;i--){
	struct stat st;

	// s3 names that do not end with ".aff" are directories
	const char *last4 = strlen(argv[i])>4 ? argv[i]+strlen(argv[i])-4 : "";
	if(strncmp(argv[i],"s3://",5)==0 &&
	   strcmp(last4,".aff")!=0){
	    dirlist.push_back(argv[i]);
	    argc--;
	    continue;		
	}

	if(stat(argv[i],&st)!=0) break; // out of directories
	if((st.st_mode & S_IFMT)!=S_IFDIR) break; // found a non-dir
	dirlist.push_back(argv[i]);
	argc--;			// ignore the last
    }

    /* If I found no directories, then there better just be two values */
    if(dirlist.size()==0){
	if(argc!=2){
	    fprintf(stderr,"Please specify a directory or just two AFF files.\n\n");
	    usage();
	}
	/* Must be copying from file1 to file2. Make sure file2 does not exist */
	if(access(argv[1],R_OK)==0){
	    fprintf(stderr,"File exists: %s\n",argv[1]);
	    if(!opt_zap) exit(1);
	}
	
	vector<string> outfiles;
	outfiles.push_back(argv[1]);
	return affcopy(argv[0],outfiles);
    }

    /* Loop for each file and each directory */

    while(argc--){
	/* Open the output files */
	vector<string> outfiles;
	for(u_int i=0;i<dirlist.size();i++){
	    string outfilename;
	    const char *name = strrchr(*argv,'/');
	    if(name) name++;
	    else name = *argv;
	    
	    outfilename.append(dirlist[i]);
	    if(outfilename[outfilename.size()-1]!='/') {
		outfilename.append("/");
	    }
	    outfilename.append(name);
	    outfiles.push_back(outfilename);
	}
	affcopy(argv[0],outfiles);	   // old outfiles will get GCed
	argv++;
    }
    exit(0);
}


