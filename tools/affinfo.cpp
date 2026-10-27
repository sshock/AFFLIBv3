/*
 * affinfo.cpp:
 *
 * print information about an aff file
 * Distributed under the Berkeley 4-part license
 */



#include "affconfig.h"
#include "afflib.h"
#include "utils.h"
#include "afflib_i.h"

#ifdef USE_S3
#include "s3_glue.h"
#endif

#include <ctype.h>
#include <zlib.h>
#include <openssl/evp.h>
#include <assert.h>

#include <algorithm>
#include <cstdlib>
#include <vector>
#include <string>

#ifdef HAVE_CSTRING
#include <cstring>
#endif

using namespace std;

#ifdef WIN32
#include "unix4win32.h"
#endif

const char *progname = "affinfo";

#define VALIDATE_MD5  0x01
#define VALIDATE_SHA1 0x02

// error reporting
unsigned int affinfo_error_code = 0x00;
#define ERROR_MD5 0x01
#define ERROR_SHA1 0x02
#define ERROR_NOT_AFF 0x04
#define ERROR_NO_PAGES 0x08
#define ERROR_PAGE_GAP 0x10

int opt_validate = 0;
int opt_info = 1;
int opt_all  = 0;
int opt_wide = 0;
int opt_l = 0;
unsigned int cols = 80;				// default
int opt_x = 0;
int opt_b = 0;
int opt_identify = 1;
int opt_verbose = 0;
int opt_y = 0;
int opt_hexbuf = AF_HEXBUF_SPACE4 | AF_HEXBUF_UPPERCASE;
int opt_page_validate = 0;
int opt_no_preview = 0;
int opt_preview_md5 = 0;
int opt_debug = 0;
int opt_figure_media = 0;
const char *opt_passphrase = 0;

vector<string> opt_seglist;		// just info these segments
bool something_was_decrypted = false;

void usage()
{
    printf("%s version %s\n",progname,PACKAGE_VERSION);
    printf("usage: %s [options] infile\n",progname);
    printf("   -a = print ALL segments (normally data segments are suppressed)\n");
    printf("   -b = print how many bad blocks in each segment (implies -a)\n");
    printf("   -i = identify the files, don't do info on them.\n");
    printf("   -w = wide output; print more than 1 line if necessary.\n");
    printf("   -s segment =   Just print information about 'segment'.\n");
    printf("                    (may be repeated)\n");
    printf("   -m = validate MD5 hash of entire image\n");
    printf("   -S = validate SHA1 hash of entire image\n");
    printf("   -v = validate the hash of each page (if present)\n");
    printf("   -y = don't print segments of lengths 16 and 20 as hex)\n");
    printf("   -p<passphrase> = Specify <passphrase> to decrypt file\n");
    printf("   -l = Just print the segment names and exit\n");
    printf("   -V = Just print the version number and exit.\n");

    printf("\nPreview Options:\n");
    printf("   -X = no data preview; just print the segment names\n");
    printf("   -x = print binary values in hex (default is ASCII)\n");
    printf("\nMisc:\n");
    printf("   -d = debug\n");
    printf("   -A = if infile is a device, print the number of sectors\n");
    printf("        and sector size to stdout in XML. Otherwise error\n");
    printf("\nCompilation:\n");
    printf("    LZMA compression: Enabled\n");
#ifdef USE_LIBEWF    
    printf("    LIBEWF enabled\n");
#endif    
#ifdef USE_QEMU
    printf("    QEMU enabled\n");
#endif
#ifdef USE_FUSE
    printf("    FUSE enabled\n");
#endif
#ifdef USE_S3
    printf("    Amazon S3 enabled\n");
#endif
#ifdef HAVE_LIBEXPAT
    printf("    HAVE_LIBEXPAT ");
#endif
    printf("\n");

    exit(0);
}


AFFILE *af=0;

void sig_info(int arg)
{
    if(af==0) return;
    printf("Validating %" I64d" of %" I64d"\n", af->pos,af->image_size);
}




void validate(const char *infile)
{
    af = af_open(infile,O_RDONLY,0);
    if(!af) af_err(1,infile);
    switch(af_identify(af)){
    case AF_IDENTIFY_AFF:
    case AF_IDENTIFY_AFM:
    case AF_IDENTIFY_AFD:
	break;
    default:
	printf("%s is not an AFF file\n",infile);
    affinfo_error_code |= ERROR_NOT_AFF;
	af_close(af);
	return;
    }

    printf("\nValidating ");
    if(opt_validate & VALIDATE_MD5) printf("MD5 ");
    if(opt_validate == (VALIDATE_MD5|VALIDATE_SHA1)) printf("and ");
    if(opt_validate & VALIDATE_SHA1) printf("SHA1 ");
    printf("hash codes.\n");


#ifdef SIGINFO
    signal(SIGINFO,sig_info);
#endif

    /* Get a list of all the segments to see if there is a space */
    af_rewind_seg(af);
    char segname[AF_MAX_NAME_LEN];
    vector <int> pages;
    memset(segname,0,sizeof(segname));
    while(af_get_next_seg(af,segname,sizeof(segname),0,0,0)==0){
	int64_t page_num = af_segname_page_number(segname);
	if(page_num>=0) pages.push_back(page_num);
    }

    if(pages.size()==0){
	printf("No pages to validate.\n");
    affinfo_error_code |= ERROR_NO_PAGES;
	af_close(af);
        return;
    }

    sort(pages.begin(),pages.end());
    vector<int>::iterator i = pages.begin();
    int last = *i;
    i++;
    for(; i!= pages.end();i++){
	if(last+1 != *i){
	    printf("gap in pages (%d!=%d); %s can't be validated.\n",last+1,*i,infile);
        affinfo_error_code |= ERROR_PAGE_GAP;
	    af_close(af);
	    return;
	}
	last = *i;
    }

    /* Set up the hash machinery (via the stable EVP API; the low-level
     * MD5 and SHA1 state functions are deprecated since OpenSSL 3.0) */
    EVP_MD_CTX *md5ctx = EVP_MD_CTX_new();
    EVP_MD_CTX *sha1ctx = EVP_MD_CTX_new();
    if(opt_validate & VALIDATE_MD5){
        const EVP_MD *md5_md = EVP_get_digestbyname("MD5");
        if(!md5_md || EVP_DigestInit_ex(md5ctx,md5_md,NULL)!=1){
            af_close(af);
            return;
        }
    }
    if(opt_validate & VALIDATE_SHA1){
        const EVP_MD *sha1_md = EVP_get_digestbyname("SHA1");
        if(!sha1_md || EVP_DigestInit_ex(sha1ctx,sha1_md,NULL)!=1){
            EVP_MD_CTX_free(md5ctx);
            EVP_MD_CTX_free(sha1ctx);
            af_close(af);
            return;
        }
    }

    uint64_t total_bytes = 0;
    while(!af_eof(af)){
	unsigned char buf[65536];		// a decent size
	size_t bytes = af_read(af,buf,sizeof(buf));
	if(bytes==0) break;		// reached sparse region of file
	total_bytes += bytes;
	if(opt_validate & VALIDATE_MD5) EVP_DigestUpdate(md5ctx,buf,bytes);
	if(opt_validate & VALIDATE_SHA1) EVP_DigestUpdate(sha1ctx,buf,bytes);
    }
    
    /* Finish the hash calculations and write to the db */

    if(opt_validate & VALIDATE_MD5){
	unsigned char md5_stored[16];
	size_t md5len = sizeof(md5_stored);
	unsigned char md5_computed[16];
	char buf[256];

	EVP_DigestFinal_ex(md5ctx,md5_computed,NULL);
	printf("computed md5: %s\n",
	       af_hexbuf(buf,sizeof(buf),md5_computed,16,opt_hexbuf));
	if(af_get_seg(af,AF_MD5,0,md5_stored,&md5len)==0){
	    printf("  stored md5: %s ",
		   af_hexbuf(buf,sizeof(buf),md5_stored,16,opt_hexbuf));
	    if(md5len==16 && !memcmp((const char *)md5_stored,
				     (const char *)md5_computed,16)){
		printf(" MATCH\n");
	    }
	    else {
		printf(" NO MATCH!\n");
        affinfo_error_code |= ERROR_MD5;
	    }
	}
	else {
	    printf("(no MD5 in AFF file)\n");
	}
    }
    

    if(opt_validate & VALIDATE_SHA1){
	unsigned char sha1_stored[20];
	size_t sha1len = sizeof(sha1_stored);
	unsigned char sha1_computed[20];
	char buf[256];
	
	EVP_DigestFinal_ex(sha1ctx,sha1_computed,NULL);
	printf("computed sha1: %s \n",af_hexbuf(buf,sizeof(buf),sha1_computed,20,opt_hexbuf));
	if(af_get_seg(af,AF_SHA1,0,sha1_stored,&sha1len)==0){
	    printf("  stored sha1: %s ",af_hexbuf(buf,sizeof(buf),sha1_stored,20,opt_hexbuf));
	    if(sha1len==20 && !memcmp((const char *)sha1_stored,
				      (const char *)sha1_computed,20)){
		printf(" MATCH\n");
	    }
	    else {
		printf(" NO MATCH!\n");
        affinfo_error_code |= ERROR_SHA1;
	    }
	}
	else {
	    printf("(no SHA1 in AFF file)\n");
	}
    }

    EVP_MD_CTX_free(md5ctx);
    EVP_MD_CTX_free(sha1ctx);

    af_close(af);
}

#define OUTLINE_LEN 65536


bool display_as_time(const char *segname)
{
    if(strcmp(segname,AF_ACQUISITION_SECONDS)==0) return true;
    return false;
}

bool display_as_hex(const char *segname,int data_len)
{
    if(af_display_as_hex(segname)) return true;
    if(data_len==16 && strstr(segname,"md5")) return true;
    if(data_len==20 && strstr(segname,"sha1")) return true;
    if(opt_x) return true;
    if(opt_preview_md5) return true;
    return false;
}

void badscan(AFFILE *af,int page_number,size_t data_len)
{
    size_t page_size = af->image_pagesize;
    unsigned char *buf = (unsigned char *)malloc(page_size);
    if(af_get_page(af,page_number,buf,&page_size)){
	err(1,"Could not read page %d",page_number);
    }
    printf("page_size = %d\n",(int)page_size);
    int sectors = 0;
    int bad_sectors = 0;
    int funny_sectors = 0;
    for(unsigned int offset=0;offset<page_size;offset+=af->image_sectorsize){
	sectors++;
	if(af_is_badsector(af,buf+offset)){
	    bad_sectors ++;
	    continue;
	}
#ifdef __FreeBSD__
	/* Look for the part of the bad flag that we know and love */
	if(strnstr((char *)buf+offset,"BAD SECTOR",af->image_sectorsize)){
	    funny_sectors++;
	    continue;
	}
#endif
    }
    printf("           sectors scanned: %d    bad: %d   ", sectors,bad_sectors);
    if(funny_sectors){
	printf("suspicious: %d ",funny_sectors);
    }
    printf("\n");
    free(buf);
}


/* print_info:
 * Print the info on a given segment name
 */
void print_info(AFFILE *af,const char *segname)
{
    uint32_t arg;
    unsigned char *data = 0;
    int dots = 0;
    u_int display_len = 0;
    char *cc = 0;

    /* Check to see if this is a null page. */
    if(segname[0]==0 && opt_all==0){
	return;
    }
    if(segname[0]=='[' && opt_all){
	puts(segname);
	return;
    }

    /* Check to see if this is a data page */
    int64_t page_num = af_segname_page_number(segname);
	
    size_t data_len = 0;
    /* First find out how big the segment is, then get the data */
    if(af_get_seg(af,segname,&arg,0,&data_len)){
	printf("%-25s  SEGMENT NOT FOUND\n",segname);
	return;
    }

    /* is this an encrypted segment that I have decrypted?
     * Turn off automatic decryption and see if I can get it again...
     * If we can get it again, then it wasn't decrypted.
     */
    int prev = af_set_option(af,AF_OPTION_AUTO_DECRYPT,0);
    bool was_decrypted = ( af_get_seg(af,segname,0,0,0)!=0) ;
    af_set_option(af,AF_OPTION_AUTO_DECRYPT,prev);    

    if(was_decrypted){
	something_was_decrypted = true;	// print key at bottom
    }

    /* Start the output line */
    char output_line[OUTLINE_LEN];		
    memset(output_line,0,sizeof(output_line));

    /* Now append the arg and the data len */
    sprintf(output_line,"%-24s %8" PRIu32 "   %8d   ",segname,arg,(int)data_len);

    if(opt_no_preview){
	printf("%s\n",output_line);
	goto done;
    }

    data = (unsigned char *)malloc(data_len);
    if(af_get_seg(af,segname,0,data,&data_len)){
	warn("af_get_seg_2 failed: segname=%s data_len=%zd",segname,data_len);
	goto done;
    }

    /* Special handling of values that should be displayed as time */
    if(display_as_time(segname)){
	int hours   = arg / 3600;
	int minutes = (arg / 60) % 60;
	int seconds = arg % 60;
	printf("%s= %02d:%02d:%02d (hh:mm:ss)\n",output_line,hours,minutes,seconds);
	goto done;
    }

    /* Special handling of quadwords that should be printed as such? */
    if(((arg == AF_SEG_QUADWORD) && (data_len==8)) || af_display_as_quad(segname)){
	/* Print it as a 64-bit value.
	 * The strcmp is there because early AF_IMAGESIZE segs didn't set
	 * AF_SEG_QUADWORD...
	 */
	switch(data_len){
	case 8:
	    printf("%s= %" I64d" (64-bit value)\n",
		   output_line,af_decode_q(data));
	    break;
	case 0:
	    printf("%s= 0 (0-length segment)\n",output_line);
	    break;
	default:
	    printf("%s= CANNOT DECODE %d byte segment\n",output_line,(int)data_len);
	}
	goto done;
    }


    /* See if I need to truncate */
    display_len = data_len;
    if(opt_wide==0 && data_len>32){ // don't bother showing more than first 32 bytes
	dots = 1;
	display_len = 32;
    }
	    
    cc = output_line + strlen(output_line);

    if(opt_preview_md5){
        /* Show an MD5 preview of the segment (16 bytes).
         * (Previously this used an oversized 32-byte buffer and copied 16
         *  bytes of stack garbage after the hash; fixed while migrating
         *  off the deprecated one-shot MD5() API.) */
	u_char md5[16];
	af_MD5((const unsigned char *)data,data_len,md5);
	memcpy(data,md5,16);
	data_len = 16;
    }
    if(display_as_hex(segname,display_len)){
	char buf[82];
	snprintf(cc,sizeof(output_line)-strlen(output_line),
		 "%s%s",af_hexbuf(buf,sizeof(buf),data,display_len,opt_hexbuf),
		dots ? "..." : "");
	/* Special code for SHA1 */
	if(!opt_wide && strcmp(segname,AF_SHA1)==0){
	    int r = fwrite(output_line,1,82,stdout);
	    if(r!=82) fprintf(stderr,"fwrite(output_line,1,82,stdout) returned %d\n",r);
	    printf("\n%62s\n",output_line+82);
	    goto done;
	}
    }
    else {
	/* Fill it out with some printable data */
	unsigned int i;
	if(display_len > sizeof(output_line)-strlen(output_line)){
	    display_len = sizeof(output_line)-strlen(output_line);
	}
	for(i=0;i<display_len;i++){
	    *cc = data[i];
	    if(isprint(*cc)==0) *cc='.';
	    if(*cc=='\n' || *cc=='\r') *cc=' ';
	    cc++;
	}
	*cc = 0;
    }
	
    /* Now print the results... */
    if(!opt_wide){
	if(strlen(output_line)>cols){
	    output_line[cols-4] = '.';
	    output_line[cols-3] = '.';
	    output_line[cols-2] = '.';
	    output_line[cols-1] = '\000';
	}
    }
    fputs(output_line,stdout);
    if(page_num>=0 && opt_b){
	badscan(af,page_num,data_len);
    }
    if(opt_page_validate && page_num>=0){
	/* Get the page again; this may involve decompression */
	unsigned char *page_data = (unsigned char *)malloc(af->image_pagesize);
	size_t page_data_len = af->image_pagesize;
	if(af_get_page(af,page_num,page_data,&page_data_len)){
	    printf("** COULD NOT READ UNCOMPRESSED PAGE ");
	    goto skip1;
	}

	char hash_segname[32];
	unsigned char hash_buf[16];
	unsigned char hash_calc[16];
	size_t hash_len = sizeof(hash_buf);
	snprintf(hash_segname,sizeof(hash_segname),AF_PAGE_MD5,page_num);
	printf("          ");
	if(af_get_seg(af,hash_segname,0,hash_buf,&hash_len)){
	    printf("** NO SEGMENT %s ** ",hash_segname);
	    goto skip1;
	}
	
	af_MD5((const unsigned char *)page_data,page_data_len,hash_calc);
	if(memcmp(hash_buf,hash_calc,sizeof(hash_buf))!=0){
	    char hb[32];
	    printf("** HASH INVALID **\n%30s Calculated %s\n","",
		   af_hexbuf(hb,sizeof(hb),hash_calc,16,opt_hexbuf));
	    printf("%30s Wanted %s ","",af_hexbuf(hb,sizeof(hb),hash_buf,16,opt_hexbuf));
	    printf("data_len=%d\n",(int)data_len);
	} else{
	    printf("HASH OK ");
	}
	free(page_data);
    }
 skip1:;
    putchar('\n');
 done:
    if(data) free(data);
}


/* Print the information on a specific file. */
int info_file(const char *infile)
{
    uint32_t total_segs = 0;
    uint32_t total_pages = 0;
    uint32_t total_hashes = 0;
    uint32_t total_signatures =0;
    uint32_t total_nulls = 0;
    struct af_vnode_info vni;

    AFFILE *af = af_open(infile,O_RDONLY,0);
    if(!af) af_err(1,"Cannot open %s",infile);
    if(af_vstat(af,&vni)) err(1,"%s: af_vstat failed",infile);

    if(opt_l){
	/* Just list the segments and exit */
        aff::seglist sl;
	sl.get_seglist(af);
	for(aff::seglist::const_iterator i = sl.begin(); i!=sl.end(); i++){
	    printf("%s\n",(*i).name.c_str());
	}
	af_close(af);
	return 0;
    }

    if(vni.segment_count_encrypted>0 || vni.segment_count_signed>0){
	printf("%s: has %s%s%ssegments\n",infile,
	       (vni.segment_count_encrypted ? "encrypted " : ""),
	       ((vni.segment_count_encrypted && vni.segment_count_signed) ? "and ": ""),
	       (vni.segment_count_signed ? "signed " : ""));
    }


    if(opt_passphrase){
	if(af_use_aes_passphrase(af,opt_passphrase)){
	    errx(1,"%s: cannot use passphrase",opt_passphrase);
	}
    }
    
    printf("\n%s\n",af_filename(af));
    const char *v1 = "data";
    const char *v2 = "====";

    if(opt_all==0) printf("[skipping data segments]\n");
    if(opt_all==0 && vni.segment_count_encrypted) printf("[skipping encrypted segments]\n");
    if(opt_no_preview){
	v1 = "";
	v2 = "";
    }
    if(opt_preview_md5){
	v1 = "md5";
    }

    printf("                                        data       \n");
    printf("Segment                       arg      length    %s\n",v1);
    printf("=======                 =========    ========    %s\n",v2);

    /* If a list of segments was specified by the user, just use that list */
    if(opt_seglist.size()>0){
	for(vector<string>::iterator i = opt_seglist.begin();
	    i != opt_seglist.end();
	    i++){
	    const char *segname = i->c_str();
	    print_info(af,segname);
	}
	af_close(af);
	return 0;
    }

    /* Go through the whole file, get all of the segments, put them in a list */
    vector <string> segments;
    char segname[AF_MAX_NAME_LEN];
    af_rewind_seg(af);			// start at the beginning
    int64_t total_datalen = 0;
    size_t total_segname_len = 0;
    size_t datalen = 0;
    int aes_segs=0;
    while(af_get_next_seg(af,segname,sizeof(segname),0,0,&datalen)==0){
	total_segs ++;
	total_datalen += datalen;
	total_segname_len += strlen(segname);
	if(segname[0]==0) total_nulls++;

	/* Check to see if this is a regular page or a hash page */
	char hash[64];
	int64_t page_num = af_segname_page_number(segname);
	int64_t hash_num = af_segname_hash_page_number(segname,hash,sizeof(hash));
	if(page_num>=0) total_pages++;
	if(hash_num>=0) total_hashes++;
	if(strstr(segname,AF_SIG256_SUFFIX)) total_signatures++;
	if(strstr(segname,AF_AES256_SUFFIX)) aes_segs++;
	if(opt_all==0 && (page_num>=0||hash_num>=0)) continue;	// skip
	if(opt_all==0 && af_is_encrypted_segment(segname)) continue; // skip

	if(segname[0]==0 && datalen>0 && opt_all){
	    snprintf(segname,sizeof(segname),"[null %zd bytes]",datalen);
	}
	segments.push_back(segname);
    }

    /* Now process the segments */
    for(vector<string>::const_iterator i = segments.begin();
	i != segments.end(); i++){
	print_info(af,i->c_str());
    }

    /* Print the key */
    if(something_was_decrypted){
	printf("Some segments were decrypted.\n");
    }


    printf("\n");
    printf("Total segments:        %8" PRIu32 "   (%" PRIu32" real)\n", total_segs,(total_segs-total_nulls));
    if(aes_segs){
	printf("  Encrypted segments:  %8u\n",aes_segs);
    }
    printf("  Page  segments:      %8" PRIu32 "\n",total_pages);
    printf("  Hash  segments:      %8" PRIu32 "\n",total_hashes);
    printf("  Signature segments:  %8" PRIu32 "\n",total_signatures);
    printf("  Null segments:       %8" PRIu32 "\n",total_nulls);
    if(opt_all){
	printf("  Empty segments:      %8" PRIu32 "\n",total_nulls);
	printf("\n");
	printf("Total data bytes in segments: %" I64d"\n",total_datalen);

	printf("Total space in file dedicated to segment names: %zd\n",
	       total_segname_len);
	printf("Total overhead for %" PRIu32 " segments: %zd bytes (%" PRIu32 "*(%zd+%zd))\n",
	       total_segs,
	       (size_t) total_segs*(sizeof(struct af_segment_head) +sizeof(struct af_segment_tail)),
	       total_segs,
	       sizeof(struct af_segment_head),
	       sizeof(struct af_segment_tail));
	printf("Overhead for AFF file header: %zd bytes\n",sizeof(struct af_head));
    }

    int64_t device_sectors = 0;
    af_get_segq(af,AF_DEVICE_SECTORS,&device_sectors);
    if(device_sectors==0){
	/* See if we can fake it */
	uint32_t cylinders=0;
	uint32_t heads=0;
	uint32_t sectors_per_track=0;
	af_get_seg(af,AF_CYLINDERS,&cylinders,0,0);
	af_get_seg(af,AF_HEADS,&heads,0,0);
	af_get_seg(af,AF_SECTORS_PER_TRACK,&sectors_per_track,0,0);
	device_sectors = cylinders * heads * sectors_per_track;
    }
    //printf("device_sectors=%"I64d"\n",device_sectors);


    int some_missing_pages = 1;
    if(af->image_pagesize && af->image_sectorsize && device_sectors){
	int64_t device_bytes = (int64_t)device_sectors * af->image_sectorsize;
	int64_t device_pages = (device_bytes+af->image_pagesize-1) / af->image_pagesize;
	int64_t missing_pages = device_pages - total_pages;
	//printf("device_bytes=%"I64d"\n",device_bytes);
	//printf("device_pages=%"I64d"\n",device_pages);
	if(missing_pages!=0){
	    printf("Missing page segments: %8" I64u"\n",missing_pages);
	}
	else {
	    some_missing_pages=0;
	}
    }
    if (some_missing_pages){
	if(((total_pages-1) * af->image_pagesize <= af->image_size) &&
	   ((total_pages)   * af->image_pagesize >= af->image_size)){
	    some_missing_pages = 0;
	}
    }

    if(some_missing_pages && opt_debug){
	printf("Cannot calculate missing pages\n");
	printf("  device_sectors=%" I64d" image_pagesize=%" PRIu32 " sectorsize=%" PRIu32 "\n",
	       device_sectors,af->image_pagesize,af->image_sectorsize);
    }
    af_close(af);
    return 0;

}
	 

void figure_media(const char *fn)
{
    int fd = open(fn,O_RDONLY,0);
    if(fd<0) err(1,"open(%s)",fn);
    struct af_figure_media_buf afb;
    if(af_figure_media(fd,&afb)){
	err(1,"af_figure_media(%s)",fn);
    }
    printf("<?xml version='1.0' encoding='UTF-8'?>\n");
    printf("<!DOCTYPE Server >\n");
    printf("<device name='%s'>\n",fn);
    printf("   <sector_size>%d</sector_size>\n",afb.sector_size);
    printf("   <total_sectors>%" PRId64 "</total_sectors>\n",afb.total_sectors);
    printf("   <max_read_blocks>%" PRIu64 "</max_read_blocks>\n",afb.max_read_blocks);
    printf("</device>\n");
    close(fd);
}

int main(int argc,char **argv)
{
    int ch;
    const char *infile;

    while ((ch = getopt(argc, argv, "abh?s:SmiIwj:p:xvVX5dAl")) != -1) {
	switch (ch) {
	case 'a': opt_all++; break;
	case 'b': opt_all ++; opt_b   ++; break;
	case 'i': opt_info=0; opt_identify = 1; break;
	case 'w': opt_wide++; break;
	case 'X': opt_no_preview++;break;
	case 'x': opt_x++; break;
	case 'y': opt_y++; break;
	case 'l': opt_l++;break;
	case 'm': opt_validate |= VALIDATE_MD5; break;
	case 'S': opt_validate |= VALIDATE_SHA1; break;
	case 'v': opt_page_validate = 1;break;
	case 'p': opt_passphrase = optarg; break;
	case '5': opt_preview_md5 = 1;break;
	case 'd': opt_debug = 1;break;
	case 'A': opt_figure_media = 1 ; break;

	case 'h':
	case '?':
	default:
	    usage();
	    break;
	case 's':
	    opt_seglist.push_back(optarg); // add to the list of segments to info
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


    /* Loop through all of the files */
    while(*argv){
	infile = *argv++;		// get the file
	argc--;				// decrement argument counter
	 
	const char *name = af_identify_file_name(infile,1);
	if(!name) err(1,"%s",infile);

	if(opt_figure_media){ figure_media(infile);continue;}
	if(opt_identify) printf("%s is a %s file\n",infile,name);
	if(opt_info)	 info_file(infile);
	if(opt_validate) validate(infile);
    }
#ifdef USE_S3
    s3_audit(0);
#endif
    return(affinfo_error_code);
}


