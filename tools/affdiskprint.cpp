/*
 * affdiskprint.cpp:
 *
 * Creates a diskprint AFF structure
 */

/*
 * PUBLIC DOMAIN
 * By Simson L. Garfinkel
 *
 * The software provided here is released by the Naval Postgraduate
 * School (NPS), an agency of the U.S. Department of Navy.The software
 * bears no warranty, either expressed or implied. NPS does not assume
 * legal liability nor responsibility for a User's use of the software
 * or the results of such use.
 *
 * Please note that within the United States, copyright protection,
 * under Section 105 of the United States Code, Title 17, is not
 * available for any work of the United States Government and/or for
 * any works created by United States Government employees. User
 * acknowledges that this software contains work which was created by
 * NPS employees and is therefore in the public domain and not
 * subject to copyright.  
 */


#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"
#include "base64.h"
#include "hashextent.h"

#ifdef HAVE_LIBEXPAT

#include <openssl/evp.h>

#ifdef WIN32
#include "unix4win32.h"
#endif

#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <map>
#include <set>

#ifdef HAVE_CSTRING
#include <cstring>
#endif

const char *hashes[] = {"SHA256","SHA1",0}; // what should we hash?

using namespace std;

#if HAVE_CTYPE_H
#include <ctype.h>
#endif

#if !defined(HAVE_ISALPHANUM) && defined(HAVE_ISALNUM)
#define isalphanum(c) isalnum(c)
#endif

#if !defined(HAVE_ISALPHANUM) && !defined(HAVE_ISALNUM)
#define isalphanum(c) (isalpha(c)||isdigit(c))
#endif

#if !defined(O_BINARY)
#define O_BINARY 0
#endif

const char *progname = "affdiskprint";
const char *xml_special_chars = "<>\r\n&'\"";

void usage()
{
    printf("%s version %s\n",progname,PACKAGE_VERSION);
    printf("usage: %s [options] infile \n",progname);
    printf("   -x XML     =   Verify the diskprint\n");
    printf("   -V         =   Just print the version number and exit.\n");
    printf("   -h         =   Print this help.\n");
    exit(0);
}

/****************************************************************
 ** Support routines...
 */

/**
 * Return a random 64-bit number
 */
uint64_t random64()
{
    return (((uint64_t)random())<<32) | random();
}

uint64_t atoi64(const char *buf)
{
    uint64_t ret=0;
    sscanf(buf,"%"PRIu64,&ret);
    return ret;
}


static int *hexcharvals = 0;
static void nsrl_bloom_init()
{
    if(hexcharvals==0){
	/* Need to initialize this */
	int i;
	hexcharvals = (int *)calloc(sizeof(int),256);
	for(i=0;i<10;i++){
	    hexcharvals['0'+i] = i;
	}
	for(i=10;i<16;i++){
	    hexcharvals['A'+i-10] = i;
	    hexcharvals['a'+i-10] = i;
	}
    }
}


/**
 * Convert a hex representation to binary, and return
 * the number of bits converted.
 * @param binbuf output buffer
 * @param binbuf_size size of output buffer in bytes.
 * @param hex    input buffer (in hex)
 */
int nsrl_hex2bin(unsigned char *binbuf,size_t hexbuf_size,const char *hex)
{
    int bits = 0;
    if(hexcharvals==0) nsrl_bloom_init();
    while(hex[0] && hex[1] && hexbuf_size>0){
	*binbuf++ = ((hexcharvals[(unsigned char)hex[0]]<<4) |
		     hexcharvals[(unsigned char)hex[1]]);
	hex  += 2;
	bits += 8;
	hexbuf_size -= 1;
    }
    if(hexbuf_size>0) binbuf[0] = 0;	// might as well null-terminate if there is room
    return bits;
}



/**
 * Strip an XML string as necessary for a tag name.
 */

void out_xmlstr(ostream &ostr,int indent,const char *tag,const char *value)
{
    for(int i=0;i<indent;i++) ostr << " ";
    ostr << "<" << tag << ">";
    for(const char *ch=value;*ch;ch++){
	if(isprint(*ch) && !strchr(xml_special_chars,*ch)){
	    ostr << *ch;
	}
    }
    ostr << "</" << tag << ">";
}

void out_xmlhex(ostream &ostr,int indent,const char *tag,const char **attribs,
		  unsigned char *md,int len)
{
    for(int i=0;i<indent;i++) ostr << ' ';
    ostr << "<" << tag << " coding='base16'";
    for(int i=0;attribs && attribs[i];i++){
	ostr << " ";
	ostr << attribs[i];
    }
    ostr << ">" << hashextent::bin2hex(md,len) << "</" << tag << ">\n";
}

/**
 * Calculate the disk fingerprint for a spcific file and output it to stdout.
 * Includes other named segments.
 *
 * @param infile the file to process.
 */

int diskprint(const char *infile)
{
    /** segments to include in output.
     */
    const char *segments[] = {AF_MD5,AF_SHA1,AF_SHA256,AF_CREATOR,AF_CASE_NUM,AF_IMAGE_GID,
			      AF_ACQUISITION_ISO_COUNTRY,
			      AF_ACQUISITION_COMMAND_LINE,AF_ACQUISITION_DATE,
			      AF_ACQUISITION_NOTES,AF_ACQUISITION_TECHNICIAN,
			      AF_BATCH_NAME,AF_BATCH_ITEM_NAME,0};
    AFFILE *af = af_open(infile,O_RDONLY,0);
    if(!af){
	warn("%s",infile);
	return -1;
    }

    cout << "<!-- XML generated by " << progname << " version " << PACKAGE_VERSION << " -->\n";
    cout << "<diskprint image_filename='" << infile << "'>\n";

    /* First handle the imagesize */
    int64_t imagesize = af_get_imagesize(af);
    if(imagesize>0){
	char buf[32];
	snprintf(buf,sizeof(buf),"%"PRIu64,imagesize);
	out_xmlstr(cout,2,AF_IMAGESIZE,buf);
	cout << "\n";
    }
    /* Get sector size and number of sectors */
    uint32_t sectorsize=512;		// default sectorsize
    af_get_seg(af,AF_SECTORSIZE,&sectorsize,0,0);
    if(sectorsize==0) sectorsize=512;	// default sectorsize
    int64_t sectors = imagesize/sectorsize;
    if(sectors>0){
	char buf[32];
	snprintf(buf,sizeof(buf),"%"PRIu32"",sectorsize);
	out_xmlstr(cout,2,AF_SECTORSIZE,buf);
	cout << "\n";
    }

    /* Output specific named segments */
    for(int i=0;segments[i];i++){
	char buf[65536];
	size_t buflen = sizeof(buf);
	if(af_get_seg(af,segments[i],0,(u_char *)buf,&buflen)==0){
	    buf[buflen] = 0;		// null terminate it
	    if(af_display_as_hex(segments[i])){
		out_xmlhex(cout,2,segments[i],0,(u_char *)buf,buflen);
	    } else {
		out_xmlstr(cout,2,segments[i],buf);
	    }
	}
    }

    /**
     * The list of segments to hash is defined by:
     * 1. The first 128K sectors.
     * 2. The last 128K sectors.
     * 3. A random set of 64K sectors.
     */
    hashvector hashextents;

    for(int i=0;i<8;i++){
	hashextents.push_back(hashextent(131072*i,131072));
    }
    for(int i=0;i<8;i++){
	hashextents.push_back(hashextent(imagesize-131072*(8-i),131072));
    }

    /* Pick some random hashextents as well */
    for(int i=0;i<100;i++){
	uint64_t sector = random64() % (sectors-128);
	hashextents.push_back(hashextent(sector*sectorsize,65536));
    }

    /** Sort the segments for maximal seek efficiency.
     */
    sort(hashextents.begin(),hashextents.end(),hashextent::compare);

    /** Send the hashes to stdout using print_hash.
     */
    cout << "  <hashes>\n";
    for(hashvector::iterator it=hashextents.begin(); it!=hashextents.end(); it++){
	for(int i=0;hashes[i];i++){
	    if((*it).compute_digest(af,hashes[i])==0){
		cout << "  " << (*it).toXML() << "\n";
	    }
	}
    }
    cout << "  </hashes>\n";
    cout << "</diskprint>\n";
    af_close(af);
    return 0;
}
	 

/**
 * Code for reading the hashvector XML structure.
 */
#include <expat.h>

class diskprintReader {
public:
    /* General EXPAT stuff */
    XML_Parser parser;
    bool get_cdata;			// this is cdata worth getting
    string cdata;			// the cdata that has been gotten
    diskprintReader():get_cdata(false),hash(0){
	parser = XML_ParserCreate(NULL);
	XML_SetUserData(parser,this);
	XML_SetElementHandler(parser,startElement,endElement);
	XML_SetCharacterDataHandler(parser,cHandler);
    }
    int parse(const char *buf,int len) { return XML_Parse(parser, buf, len, 1);}
    void clear(){
	cdata = "";
	get_cdata = false;
    }

    /* Specific stuff for XML diskprint */
    hashextent *hash;		// current hash
    hashvector hashextents;	// all discovered hashes
    /* Turn the static functions into method calls */
    static void startElement(void *userData,const char *name,const char **attrs){
	((diskprintReader *)userData)->startElement(name,attrs);
    }
    static void endElement(void *userData,const char *name){
	((diskprintReader *)userData)->endElement(name);
    }
    static void cHandler(void *userData,const XML_Char *s,int len){
	diskprintReader *dh = (diskprintReader *)userData;
	if(dh->get_cdata) dh->cdata.append(s,len);
    }
    void startElement(string name,const char **attrs){
	clear();
	/* If this is an element that we want, indicate such */
	if(name=="hash"){
	    hash = new hashextent();
	    for(int i=0;attrs[i];i+=2){
		if(strcmp(attrs[i],"coding")==0){ hash->coding = attrs[i+1]; continue;}
		if(strcmp(attrs[i],"start")==0){ hash->start = atoi64(attrs[i+1]); continue;}
		if(strcmp(attrs[i],"bytes")==0){ hash->bytes = atoi64(attrs[i+1]); continue;}
		if(strcmp(attrs[i],"alg")==0){  hash->digest_name = attrs[i+1]; continue;}
	    }
	    get_cdata = true;
	}
    }
    void endElement(const char *name){
	if(get_cdata==false) return;	// don't care about it.
	if(!strcmp(name,"hash")){
	    if(hash->coding=="base16"){
		hash->hexdigest = cdata;
	    }
	    hashextents.push_back(*hash);
	}
	if(!strcmp(name,"diskprint")){
	    XML_StopParser(parser,0);	// stop the parser
	    return;
	}
	get_cdata = false;
    }
};

void diskprint_verify(const char *filename,const char *xmlfile)
{
    AFFILE *af = af_open(filename,O_RDONLY,0);
    if(!af) err(1,"af_open(%s): ",filename);

    /* Let's read the XML file */
    int fd = open(xmlfile,O_RDONLY|O_BINARY);
    if(!fd) err(1,"open: %s",xmlfile);
    struct stat st;
    if(fstat(fd,&st)) err(1,"stat: %s",xmlfile);
    char *buf = (char *)malloc(st.st_size+1);
    if(!buf) err(1,"malloc");

    if(read(fd,buf,st.st_size)!=st.st_size) err(1,"cannot read XML file");
    buf[st.st_size]=0;			// terminate the buffer (not strictly needed)

    diskprintReader dp;
    dp.parse(buf,st.st_size);
    cout << "Number of digests read: "<< dp.hashextents.size() << "\n";
    const EVP_MD *strongest = dp.hashextents.strongest_available();
    cout << "Strongest hash available: " << EVP_MD_name(strongest) << "\n";
    /* Now verify each hash */
    int matched=0;
    int notmatched=0;
    for(hashvector::iterator it = dp.hashextents.begin(); it!=dp.hashextents.end() && notmatched==0;it++){
	if(EVP_MD_name(strongest) == (*it).digest_name){
	    hashextent &hp = (*it);	// hash print
	    hashextent hs(af,hp.digest_name,hp.start,hp.bytes);
	    //cout << "hp: " << hp << "\n";
	    //cout << "hs: " << hs << "\n";
	    if(hp==hs){
		matched++;
	    } else {
		notmatched++;
	    }
	}
    }
    if(notmatched){
	cout << "Diskprint does not match.\n";
    } 
    if(notmatched==0 && matched){
	cout << "Diskprint matches.\n";
    }
    if(notmatched==0 && matched==0){
	cout << "Cannot verify Diskprint; no available hash functions.\n";
    }
    exit(0);
}

int main(int argc,char **argv)
{
    int ch;
    const char *opt_x=0;

    /* Initialize */
#ifdef HAVE_SRANDOMEDEV
    srandomdev();
#endif
    OpenSSL_add_all_digests();/* Dynamically loads the digests */


    /* Parse arguments */
    while ((ch = getopt(argc, argv, "x:h?V")) != -1) {
	switch (ch) {
	case 'h':
	case '?':
	default:
	    usage();
	    break;
	case 'x': opt_x = optarg; break;
	case 'V':
	    printf("%s version %s\n",progname,PACKAGE_VERSION);
	    exit(0);
	}
    }
    argc -= optind;
    argv += optind;

    if(argc!=1){			// currently only generates for one file
	usage();
    }

    if(opt_x){
	diskprint_verify(argv[0],opt_x);
	return(0);
    }

    /* Loop through all of the files */
    printf("<?xml version='1.0' encoding='UTF-8'?>\n");
    printf("<diskprints>\n");
    while(*argv){
	if(opt_x){
	    diskprint_verify(*argv,opt_x);
	}
	else {
	    diskprint(*argv);
	}
	argv++;
	argc--;		
    }
    printf("</diskprints>\n");
    exit(0);
}
#else
int main(int argc,char **argv)
{
    fprintf(stderr,"affdiskprint requires EXPAT. Cannot continue.\n");
    exit(-1);
}
#endif

