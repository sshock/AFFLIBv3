/*
 * acompare.cpp:
 *
 * Compare the contents of an ISO file to an AFF file.
 * Optionally, if they are equal, delete the ISO file
 *
 * Distributed under the Berkeley 4-part license
 */

#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"
#include "utils.h"

using namespace std;
using namespace aff;


#ifdef WIN32
#include "unix4win32.h"
#endif


#ifdef UNIX
#include <sys/signal.h>
#include <unistd.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <zlib.h>
#include <assert.h>

#ifdef HAVE_CSTRING
#include <cstring>
#endif

#ifdef linux
#include <sys/time.h>
#endif



const char *progname = "affcompare";

int opt_quiet  = 0;
int opt_all    = 0;
int opt_print_sectors    = 0;
int opt_print_sector_contents    = 0;
int opt_page   = -1;
int opt_preen  = 0;
int opt_exist  = 0;
int opt_ignore_metadata = 0;
int opt_s3 = 0;
int opt_verbose = 0;
const char *batch_ext = "";

vector<string> errors;

const char *current_source = 0;

void sig_info(int arg)
{
    if(current_source){
	printf("%s... ",current_source);
    }
    printf("\n");
    fflush(stdout);
}

void print_title(char *title)
{
    if(title[0]){
	puts(title);
	title[0] = 0;
    }
}

void usage()
{
    printf("affcompare version %s\n",PACKAGE_VERSION);
    printf("\n");
    printf("usage: affcompare [options] file1 file2\n");
    printf("       compares file1 with file2\n");
    printf("\n");
    printf("or     affcompare [options] -r dir1 dir2\n");
    printf("       compares similarly-named files in dir1 and dir2\n");
    printf("\n");
    printf("or     affcompare [options] -s file1 file2...\n");
    printf("       Reports if file was successfully copied to Amazon S3\n");
    printf("       checking only for existence, not reading back the bytes.\n");
    printf("       (Because all writes to S3 are validated by the MD5 of the object\n");
#ifndef USE_S3
    printf("       NOTE: S3 support is not provided in this version\n");
#endif
    
    printf("fast options:\n");
    printf("(These compare segments but not their contents.)\n");
    printf("       -p        --- report about the results of preening\n");
    printf("       -e        --- Just report about existence (use with -r)\n");
    printf("       -s        --- Just see if all of the segments are present, but don't\n");
    printf("                     validate the contents. (Primarily for use with Amazon S3)\n");

    printf("other options:\n");
    printf("       -V        --- just print the version number and exit\n");
    printf("       -v        --- Verbose; each file as it is compared.\n");
    printf("       -q        --- Quiet. No output except for errors\n");
    printf("       -a        --- print what's the same (all)\n");
    printf("       -b        --- print the numbers of differing sectors\n");
    printf("       -c        --- print the contents of differing sectors\n");
    printf("       -m        --- Just report about the data (ignore metadata)\n");
    printf("       -P ###    --- Just examine the differences on page ###\n");
    printf("\n");
    printf("Options documented above:\n");
    printf("       -r dir1 dir2 --- recursively compare what's in dir1 with dir2, and\n");
    printf("                       report what's in dir1 that's not in dir2\n");
    printf("       -s        --- Check to see if named files are on Amazon S3\n");
    printf("\n");
    printf("  affcompare file1.aff file2.aff           --- compare file1.aff and file2.aff\n");
    printf("  affcompare f1.aff f2.aff dir1/           --- compare f1.aff with dir1/f1.aff and f2.aff with dir2/f2.aff\n");
    printf("                                              note: dir1/ must end with a slash.\n");
    printf("  affcompare -b img file.aff               --- compare file.aff and file.img\n");
    printf("  affcompare -b img file1.aff file2.aff... --- compare file1.aff, file1.img, etc.\n");
    printf("  affcompare -re dir1 dir2                --- report AFF files in dir1 but not in dir2\n");
    printf("  affcompare -rse dir1 s3:///             --- report AFF files in dir1 but not on S3 (low bandwidth)\n");
    printf("  affcompare -rs dir1 s3:///              --- report AFF files in dir1 but incomplete on on S3 (more bandwidth)\n");
    printf("\n");
    exit(0);
}

void print_sector(AFFILE *af,unsigned char *buf)
{
    for(unsigned int i=0;i<af->image_sectorsize;i++){
	if(isprint(buf[i])){
	    putchar(buf[i]);
	}
	else {
	    putchar('.');
	}
	if(i%64==63) putchar('\n');
    }
}


void print_info(char dir,const char *segname,uint32_t arg,size_t len,
		unsigned char *data,int mcr)
{
    printf("    %c %s arg=%" PRIu32 " len=%d\n",dir,segname,arg,(int)len);
    printf("          ");
    if((arg == AF_SEG_QUADWORD) && (len==8)){
	printf("data=%" I64d " as a 64-bit value\n",af_decode_q(data));
	return;
    }
    /* Otherwise, just print some stuff... */
    for(unsigned int i=0;i<len && i<60;i++){
	if(data[i]==' '){
	    putchar(' ');
	    continue;
	}
	if(!isprint(data[i])){
	    putchar('.');
	    continue;
	}
	putchar(data[i]);
    }
    putchar('\n');
}

int  compare_aff_metadata_segments(char *title,AFFILE *af1,AFFILE *af2,const char *segname,int mode)
{
    int ret = 0;

    uint32_t arg1 = 0;
    size_t data1_len = 0;
    int r1 = af_get_seg(af1,segname,&arg1,0,&data1_len);

    uint32_t arg2 = 0;
    size_t data2_len = 0;
    int r2 = af_get_seg(af2,segname,&arg2,0,&data2_len);
    
    if(r1==0 && r2!=0){
	if(mode==1){
	    print_title(title);
	    printf("   %s  \n",segname);
	}
	return 1;
    }
    
    if(r1!=0 && r2==0){
	if(mode==2){
	    print_title(title);
	    printf("   %s  \n",segname);
	}
	return 1;
    }
    if(mode!=3) return 0;			// only report differences in mode 3
    /* Get the actual data... */

    unsigned char *data1 = (unsigned char *)malloc(data1_len);
    unsigned char *data2 = (unsigned char *)malloc(data2_len);

    int s1 = af_get_seg(af1,segname,&arg1,data1,&data1_len);
    if(s1!=0) err(1,"Couldn't read data segment %s in %s",segname,af_filename(af1));

    int s2 = af_get_seg(af2,segname,&arg2,data2,&data2_len);
    if(s2!=0) err(1,"Couldn't read data segment %s in %s",segname,af_filename(af2));

    int mcr = 0;

    if(data1_len != data2_len) mcr = 1;
    else mcr = memcmp(data1,data2,data1_len);
    if(arg1!=arg2 || data1_len!=data2_len || mcr!=0){
	print_title(title);
	print_info('<',segname,arg1,data1_len,data1,mcr);
	print_info('>',segname,arg2,data2_len,data2,mcr);
	if(mcr){
	    printf("        *** Metadata segment are different ");
	    if(strcmp(segname,AF_BADFLAG)==0){
		printf("(bad flags should be different!)");
	    }
	    putchar('\n');
	}
	putchar('\n');
	ret = 1;
    }
    else {
	if(opt_all){
	    print_title(title);
	    printf("   %s (same in both) \n",segname);
	}
    }
    free(data1);
    free(data2);
    return ret;
}

int  compare_aff_data_segments(char *title,AFFILE *af1,AFFILE *af2,int64_t pagenum,int mode)
{
    int ret = 0;
    char pagename[65];
    snprintf(pagename,sizeof(pagename),AF_PAGE,pagenum);

    char segname[65];
    snprintf(segname,sizeof(segname),AF_SEG_D,pagenum);

    uint32_t arg1=0;
    size_t data1_len=0;
    int r1 = af_get_seg(af1,pagename,&arg1,0,&data1_len);
    if(r1==-1) r1=af_get_seg(af1,segname,&arg1,0,&data1_len);
    
    uint32_t arg2=0;
    size_t data2_len=0;
    int r2 = af_get_seg(af2,pagename,&arg2,0,&data2_len);
    if(r2 == -1) r2=af_get_seg(af2,segname,&arg2,0,&data2_len);
    
    if(r1<0 && r2<0) return 0;		// no data segment in either file
    if(r1==0 && r2!=0){
	if(mode==1){
	    print_title(title);
	    printf("   %s  \n",pagename);
	}
	return 1;
    }
    
    if(r2==0 && r1!=0){
	if(mode==2){
	    print_title(title);
	    printf("   %s  \n",pagename);
	}
	return 1;
    }
    if(mode!=3) return 0;		// only report differences in mode 3

    /* Get the actual data... */
    unsigned char *data1 = (unsigned char *)malloc(af_page_size(af1));
    unsigned char *data2 = (unsigned char *)malloc(af_page_size(af2));

    data1_len = af_page_size(af1);
    data2_len = af_page_size(af2);

    uint64_t start_sector_number = (pagenum * data1_len) / af1->image_sectorsize;

    /* Find the size of each page, then get the page */
    if(af_get_page(af1,pagenum,0,&data1_len)<0)
	err(1,"Cannot read page %" I64d " size from %s\n",pagenum,af_filename(af1));
    if(af_get_page(af1,pagenum,data1,&data1_len)<0)
	err(1,"Cannot read page %" I64d " from %s",pagenum,af_filename(af1));

    if(af_get_page(af2,pagenum,0,&data2_len)<0)
	err(1,"Cannot read page %" I64d " size from %s\n",pagenum,af_filename(af2));
    if(af_get_page(af2,pagenum,data2,&data2_len)<0)
	err(1,"Cannot read page %" I64d " from %s",pagenum,af_filename(af2));

    if(data1_len != data2_len){
	printf("page %" I64d " size %d != size %d\n",pagenum,(int)data1_len,(int)data2_len);
	return 1;
    }

    /* Now look at the pages sector-by-sector. */
    int af1_bad=0;
    int af2_bad=0;
    int matching_bad_sectors = 0;
    int matching_sectors = 0;
    int total_sectors = 0;
    int no_match = 0;
    vector<uint64_t> different_sectors;

    for(unsigned int offset=0;offset<data1_len;offset+=af1->image_sectorsize){
	uint64_t this_sector = start_sector_number + offset/af1->image_sectorsize;
	total_sectors++;
	if(af_is_badsector(af1,data1+offset) &&
	   af_is_badsector(af2,data2+offset)){
	    matching_bad_sectors++;
	    continue;
	}
	if(af_is_badsector(af1,data1+offset)){
	    af1_bad++;
	    continue;
	}
	if(af_is_badsector(af2,data2+offset)){
	    af2_bad++;
	    continue;
	}
	if(memcmp(data1+offset,data2+offset,af1->image_sectorsize)==0){
	    matching_sectors++;
	    continue;
	}
	no_match++;
	different_sectors.push_back(this_sector);
    }

    char outline[256];
    outline[0] = 0;
    if(opt_all || (no_match>0) || af1_bad || af2_bad){
	snprintf(outline,sizeof(outline),
		"   page%" I64d " sectors:%4d  matching: %3d  different:%3d",
		pagenum,total_sectors,matching_sectors,no_match);
    }
    if(af1_bad){
	snprintf(outline+strlen(outline),sizeof(outline)-strlen(outline),
		 "    file 1 bad: %3d ",af1_bad);
    }
    if(af2_bad){
	snprintf(outline+strlen(outline),sizeof(outline)-strlen(outline),
		 "    file 2 bad: %3d ",af2_bad);
    }
    if(matching_bad_sectors){
	if(opt_all){
	    snprintf(outline+strlen(outline),sizeof(outline)-strlen(outline),
		    "    bad both:%3d ",matching_bad_sectors);
	}
    }

    if(outline[0]){
	print_title(title);
	puts(outline);
    }
    if(opt_print_sectors && different_sectors.size()>0){
	print_title(title);
	printf("  Sectors with differences:");
	int i=0;
	for(vector<uint64_t>::iterator j = different_sectors.begin();
	    j != different_sectors.end();
	    j++){
	    if(i==0){
		printf("\n   ");
	    }
	    printf(" %" I64d,*j);
	    i = (i+1) % 10;
	}
	putchar('\n');
	ret = 1;
    }
    if(opt_print_sector_contents && different_sectors.size()>0){
	print_title(title);
	printf("  Sectors with differences:");
	for(vector<uint64_t>::iterator j = different_sectors.begin();
	    j != different_sectors.end(); j++){
	    int offset = (*j - start_sector_number)*af1->image_sectorsize;
	    char b2[16];
	    printf("offset=%d\n",offset);

	    memcpy(b2,data1+offset,16);
	    b2[15]=0;

	    printf("===  sector %" I64d " (offset=%d) ===\n",*j,offset);
	    printf("   %s:\n",af_filename(af1));
	    print_sector(af1,data1+offset);
	    printf("-------------------------------------\n");
	    printf("   %s:\n",af_filename(af2));
	    print_sector(af2,data2+offset);
	    printf("=====================================\n\n");
	}
	ret = 1;
    }
    free(data1);
    free(data2);
    return ret;
}

/* Compare the results of two files that were preened */
int compare_preen(AFFILE *af1,AFFILE *af2)
{
    vector<int64_t> pages;
    int comp_zero=0;
    int comp_lzma=0;
    int comp_unchanged=0;
    uint64_t bytes_old = 0;
    uint64_t bytes_new = 0;

    af_rewind_seg(af1);
    /* Build a list of all the pages */
    char segname[AF_MAX_NAME_LEN];
    while(af_get_next_seg(af1,segname,sizeof(segname),0,0,0)==0){
	int64_t pagenumber = af_segname_page_number(segname);
	if(pagenumber>=0) pages.push_back(pagenumber);
    }
    /* Now, compare each one */
    for(vector<int64_t>::const_iterator i = pages.begin(); i != pages.end(); i++){
	uint32_t arg1,arg2;
	size_t len1,len2;

	if(af_get_page_raw(af1,*i,&arg1,0,&len1)){
	    err(1,"Could not read page %" I64d " in file %s\n",*i,af_filename(af1));
	}
	if(af_get_page_raw(af2,*i,&arg2,0,&len2)){
	    err(1,"Page %" I64d " is in file %s but not in %s\n",*i,af_filename(af1),
		af_filename(af2));
	}
	if(arg1==arg2 && len1==len2){
	    comp_unchanged++;
	    continue;
	}
	if((arg2 & AF_PAGE_COMP_ALG_MASK)==AF_PAGE_COMP_ALG_ZERO){
	    comp_zero++;
	    continue;
	}
	if((arg2 & AF_PAGE_COMP_ALG_MASK)==AF_PAGE_COMP_ALG_LZMA){
	    comp_lzma++;
	    bytes_old += len1;
	    bytes_new += len2;
	    continue;
	}
    }
    printf("%s -> %s Nochg: %d  NUL: %d  LZMA: %d  old: %" I64d " new: %" I64d " LZred: %6.2f%%\n",
	   af_filename(af1),
	   af_filename(af2),
	   comp_unchanged,comp_zero,comp_lzma,bytes_old,bytes_new,(bytes_old-bytes_new)*100.0/bytes_old);
    return 0;
}


/* Compare two AFF files.
 * Return 0 if they are equal.
 */
int compare_aff_aff(const char *file1,const char *file2)
{
    bool no_data_segments = false;
    int  ret = 0;

    current_source = file1;

    if(opt_all) printf("compare %s and %s:\n",file1,file2);

    AFFILE *af1 = af_open(file1,O_RDONLY,0);
    if(!af1) af_err(1,"af_open(%s)",file1);

    AFFILE *af2 = af_open(file2,O_RDONLY,0);
    if(!af2) af_err(1,"af_open(%s)",file2);

    af_vnode_info vni1,vni2;

    if(af_vstat(af1,&vni1) || af_vstat(af2,&vni2)){
	err(1,"af_vstat failed?");
    }

    if(af_cannot_decrypt(af1) != af_cannot_decrypt(af2)){
	printf("%s: %s decrypt\n",file1,af_cannot_decrypt(af1) ? "cannot" : "can");
	printf("%s: %s decrypt\n",file2,af_cannot_decrypt(af2) ? "cannot" : "can");
	fprintf(stderr,"affcompare must be able to decrypt both files or neither of the files.\n");
	exit(1);
    }

    if(af1->image_pagesize != af2->image_pagesize){
	fprintf(stderr,"Currently, %s requires that both images have the "
		"same image datsegsize.\n"
		"  pagesize(%s)=%" PRIu32 "\n"
		"  pagesize(%s)=%" PRIu32 "\n",
		progname,file1,af1->image_pagesize, file2,af2->image_pagesize);
	fprintf(stderr,"Data segments will be ignored.\n");
	no_data_segments = true;
    }

    if(af1->image_sectorsize != af2->image_sectorsize){
	fprintf(stderr,"Currently, %s requires that both images have the "
		"same image sectorsize.\n"
		"  sectorsize(%s)=%" PRIu32 "\n"
		"  sectorsize(%s)=%" PRIu32 "\n",
		progname,file1,af1->image_sectorsize, file2,af2->image_sectorsize);
	fprintf(stderr,"Data segments will be ignored.\n");
	no_data_segments = true;
    }
    
    if(opt_preen){
	compare_preen(af1,af2);
	af_close(af1);
	af_close(af2);
	return 0;
    }

    if(opt_s3){
	printf("bypass\n");
	seglist list1(af1);
	seglist list2(af2);

	/* Just compare the presence/absence of each segment */
	char title[1024];
	snprintf(title,sizeof(title),"\nPresent in %s but not %s:",af_filename(af1),af_filename(af2));
	for(seglist::const_iterator i=list1.begin(); i!=list1.end(); i++){
	    if(find(list2.begin(),list2.end(),*i)==list2.end()){
		print_title(title);
		printf("  %s\n",(*i).name.c_str());
	    }
	}
	snprintf(title,sizeof(title),"\nPresent in %s but not %s:",af_filename(af2),af_filename(af1));
	for(seglist::const_iterator i=list2.begin(); i!=list2.end(); i++){
	    if(find(list1.begin(),list1.end(),*i)==list1.end()){
		print_title(title);
		printf("  %s\n",(*i).name.c_str());
	    }
	}
	return 0;
    }

    /* Compare all of the metadata segments in af1 with a2.
     * Report those that are missing or different. Then report
     * all of the segments in a2 but not in af1
     */

    /* First build a list of the segments in each */

    vector <string> segs_with_dups;

    AFFILE *af[2] = {af1,af2};
    for(int i=0;i<2;i++){
	if(opt_verbose) printf("\n%s:\n",af_filename(af[i]));
	af_rewind_seg(af[i]);
	char segname[AF_MAX_NAME_LEN];
	while(af_get_next_seg(af[i],segname,sizeof(segname),0,0,0)==0){
	    if(segname[0]){
		string s;
		s = segname;
		segs_with_dups.push_back(s);	// may give duplicates
		if(opt_verbose) printf("     %s\n",segname);
	    }
	}
    }
    sort(segs_with_dups.begin(),segs_with_dups.end());
    vector<string>segs;

    /* Make a list of segs without duplicates */
    string last;
    for(vector<string>::iterator i = segs_with_dups.begin();
	i != segs_with_dups.end(); i++){
	if(last != *i){
	    segs.push_back(*i);
	}
	last = *i;
    }

    int lowest_page = -1;
    int highest_page = -1;
    /* Scan for the lowest and highest numbers */
    for(vector<string>::iterator i = segs.begin();i != segs.end(); i++){
	int64_t num = af_segname_page_number(i->c_str());
	if(num!=-1){
	    if(num<lowest_page ||lowest_page==-1)  lowest_page = num;
	    if(num>highest_page||highest_page==-1) highest_page = num;
	}
    }


    if(opt_page != -1){
	lowest_page  = opt_page;
	highest_page = opt_page;
    }


    if(opt_page == -1
       && vni1.supports_metadata
       && vni2.supports_metadata
       && opt_ignore_metadata==0 ){
	if(opt_all) puts("Inspecting metadata...");
	for(int mode=1;mode<=3;mode++){
	    const char *title = "Metadata segments ";
	    char mode_title[1024];
	    switch(mode){
	    case 1:
		snprintf(mode_title,sizeof(mode_title),"  %s only in %s:\n",
			 title,af_filename(af1));
		break;
	    case 2:
		snprintf(mode_title,sizeof(mode_title),"  %s only in %s:\n",
			 title,af_filename(af2));
		break;
	    case 3:
		snprintf(mode_title,sizeof(mode_title),"  %s in both files:\n",title);
		break;
	    }
	    
	    for(vector<string>::iterator i = segs.begin();i != segs.end();i++){
		int64_t num = af_segname_page_number(i->c_str());
		if(num==-1){
		    int r = compare_aff_metadata_segments(mode_title, af1,af2,
							  i->c_str(),mode);
		    if(r!=0) ret = r;
		}
	    }
	}
    }

    if(no_data_segments==false){
	if(opt_all) puts("Inspecting data...");
	for(int mode=1;mode<=3;mode++){
	    char mode_title[1024];
	    switch(mode){
	    case 1: snprintf(mode_title,sizeof(mode_title),
			     "  Pages only in %s:\n", af_filename(af1));break;
	    case 2: snprintf(mode_title,sizeof(mode_title),
			     "  Pages only in %s:\n", af_filename(af2));break;
	    case 3: snprintf(mode_title,sizeof(mode_title),"  Pages in both files:\n");break;
	    }
	    
	    for(int i=lowest_page;i<=highest_page;i++){
		int r = compare_aff_data_segments(mode_title,af1,af2,i,mode);
		if(r!=0) ret = r;
	    }
	}
    }
    current_source = 0;
#ifdef HAVE_ISATTY
    if(ret==0 && isatty(fileno(stdout))) printf("%s and %s: files compare okay\n",file1,file2);
#endif
    return ret;
}

int recurse(const char *dir1,const char *dir2)
{
    vector<string> only_in_dir1;

    DIR *dirp = opendir(dir1);
    struct dirent *dp;
    if(!dirp) err(1,"opendir: %s",dir1);
    while ((dp = readdir(dirp)) != NULL){

	char fn1[MAXPATHLEN+1]; memset(fn1,0,sizeof(fn1));
	char fn2[MAXPATHLEN+1]; memset(fn2,0,sizeof(fn2));

	strlcpy(fn1,dir1,sizeof(fn1));
	if(fn1[strlen(fn1)-1]!='/') strlcat(fn1,"/",sizeof(fn1));
	strlcat(fn1,dp->d_name,sizeof(fn1));

	current_source = fn1;
	if(opt_verbose) printf("%s...\n",fn1);

	switch(af_identify_file_type(fn1,1)){
	case AF_IDENTIFY_ERR:
	case AF_IDENTIFY_NOEXIST:
	    only_in_dir1.push_back(fn1);
	    break;
	case AF_IDENTIFY_AFF:
	case AF_IDENTIFY_AFD:
	case AF_IDENTIFY_AFM:
	    strlcpy(fn2,dir2,sizeof(fn2));
	    if(fn2[strlen(fn2)-1]!='/') strlcat(fn2,"/",sizeof(fn2));
	    strlcat(fn2,dp->d_name,sizeof(fn2));
	    if(af_identify_file_type(fn2,1)<0){
		char buf[1024];
		snprintf(buf,sizeof(buf),"%s not in %s\n",dp->d_name,dir2);
		errors.push_back(buf);
		break;
	    }
	    if(opt_exist==0){
		compare_aff_aff(fn1,fn2);
	    }
	    break;
	default:
	    break;
	}
    }
    closedir(dirp);
    printf("========================\n");
    printf("Only in %s\n",dir1);
    for(vector<string>::const_iterator i = only_in_dir1.begin();
	i != only_in_dir1.end();
	i++){
	printf("%s\n",i->c_str());
    }
    return 0;
}

int main(int argc,char **argv)
{
    int ch;
    int opt_recurse=0;

#ifdef SIGINFO
    signal(SIGINFO,sig_info);
#endif

    while ((ch = getopt(argc, argv, "P:Vabcempqrsh?v")) != -1) {
	switch (ch) {
	case 'P': opt_page = atoi(optarg); break;
	case 'V': printf("%s version %s\n",progname,PACKAGE_VERSION); exit(0);
	case 'a': opt_all++; break;
	case 'b': opt_print_sectors=1; break;
	case 'c': opt_print_sector_contents=1; break;
	case 'e': opt_exist++; break;
	case 'm': opt_ignore_metadata++; break;
	case 'p': opt_preen++; break;
	case 'q': opt_quiet++; break;
	case 'r': opt_recurse++; break;
	case 's': opt_s3++;break;
	case 'v': opt_verbose++;break;
	case 'h':
	case '?':
	default:
	    usage();
	}
    }
    argc -= optind;
    argv += optind;

    if(opt_recurse){
	if(argc!=2) usage();
	char *dir1 = *argv++;
	char *dir2 = *argv++;
	recurse(dir1,dir2);
	if(errors.size()>0){
	    fprintf(stderr,"================================\n");
	    fprintf(stderr,"%d affcompare errors:\n",(int)errors.size());
	    for(vector<string>::const_iterator i=errors.begin();
		i!=errors.end();
		i++){
		fputs(i->c_str(),stderr);
	    }
	    exit(1);
	}
	exit(0);
    }

    if(argc>1){
	char *last = argv[argc-1];
	if(last[strlen(last)-1]=='/'){
	    while(argc>1){
		char *file1 = *argv;
		char *name1 = file1;
		char *cc;

		cc = strrchr(file1,'/');
		if(cc) name1 = cc+1;

		char file2[MAXPATHLEN+1];
		strlcpy(file2,last,sizeof(file2));
		strlcat(file2,name1,sizeof(file2));
		int e_code = compare_aff_aff(file1,file2);
		if(e_code) exit(e_code);
		argv++;
		argc--;
	    }
	}
    }

    if(argc!=2) usage();			// if just 2, compare them
    
    char *file1 = *argv++;
    char *file2 = *argv++;
	 
    if(opt_verbose) printf("%s...\n",file1);
    int e_code = compare_aff_aff(file1,file2);
    exit(e_code);
}
