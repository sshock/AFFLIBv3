/*
 * affverify.cpp:
 *
 * Verify the digital signature on a signed file
 * This file is a work of a US government employee and as such is in the Public domain.
 * Simson L. Garfinkel, March 12, 2012
 */

#ifdef WIN32
#  include <winsock2.h>
#  include <windows.h>			
#endif

#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"

#include "utils.h"
#include "base64.h"
#include "aff_bom.h"
#include "aftimer.h"

#include <stdio.h>
#include <algorithm>
#include <vector>
#include <iostream>
#include <openssl/pem.h>
#include <openssl/x509.h>

/* Support OpenSSL before 1.1.0 */
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
#define EVP_MD_CTX_new EVP_MD_CTX_create
#define EVP_MD_CTX_free EVP_MD_CTX_destroy
#endif

using namespace std;
using namespace aff;

const char *progname = "affverify";
int opt_change = 0;
int opt_verbose = 0;
int opt_all = 0;

void usage()
{
    printf("affverify version %s\n",PACKAGE_VERSION);
    printf("usage: affverify [options] filename.aff\n");
    printf("Verifies the digital signatures on a file\n");
    printf("options:\n");
    printf("    -a      --- print all segments\n");
    printf("    -V      --- Just print the version number and exit.\n");
    printf("    -v      --- verbose\n");

    OpenSSL_add_all_digests();
    const EVP_MD *sha256 = EVP_get_digestbyname("sha256");
    if(sha256){
	printf("  SHA256 is operational\n");
    } else {
	printf("Warning: EVP_get_digestbyname(\"sha256\") fails\n");
    }
    exit(0);
}

void print_x509_info(X509 *cert)
{
    printf("SIGNING CERTIFICATE :\n");
    printf("   Subject: "); X509_NAME_print_ex_fp(stdout,X509_get_subject_name(cert),0,XN_FLAG_SEP_CPLUS_SPC);
    printf("\n");
    printf("   Issuer: "); X509_NAME_print_ex_fp(stdout,X509_get_issuer_name(cert),0,XN_FLAG_SEP_CPLUS_SPC);
    printf("\n");
    ASN1_INTEGER *sn = X509_get_serialNumber(cert);
    if(sn){
	long num = ASN1_INTEGER_get(sn);
	if(num>0) printf("   Certificate serial number: %ld\n",num);
    }
    printf("\n");
}

#ifdef USE_AFFSIGS
#include "expat.h"
void startElement(void *userData, const char *name, const char **atts);
void endElement(void *userData, const char *name);
void cHandler(void *userData,const XML_Char *s,int len);

class segmenthash {
public:
    segmenthash():total_validated(0),total_invalid(0),sigmode(0),in_cert(false),
		 in_seghash(false),get_cdata(false),arg(0),seglen(0),
		 get_cdata_segment(0),af(0),cert(0),pubkey(0) {
	
	parser = XML_ParserCreate(NULL);
	XML_SetUserData(parser, this);
	XML_SetElementHandler(parser, ::startElement, ::endElement);
	XML_SetCharacterDataHandler(parser,cHandler);
    };
    int parse(const char *buf,int len) { return XML_Parse(parser, buf, len, 1);}
    XML_Parser parser;
    int total_validated;
    int total_invalid;
    int sigmode;
    bool in_cert;
    bool in_seghash;
    bool get_cdata;
    string segname;
    string alg;
    string cdata;
    int arg;
    int seglen;
    const char *get_cdata_segment;	// just get this segment
    AFFILE *af;				// if set, we are parsing crypto
    X509 *cert;				// public key used to sign
    EVP_PKEY *pubkey;
    void clear(){
	segname="";
	cdata="";
	sigmode=0;
	alg="";
	seglen=0;
    }
    ~segmenthash(){
	if(cert) X509_free(cert);
	if(parser) XML_ParserFree(parser);
    }
    void startElement(const char *name,const char **atts);
    void endElement(const char *name);
};

int count=0;
void startElement(void *userData, const char *name, const char **atts)
{
    segmenthash *sh = (segmenthash *)userData;
    sh->startElement(name,atts);
}

void segmenthash::startElement(const char *name,const char **atts)
{
    clear();
    if(strcmp(name,AF_XML_SEGMENT_HASH)==0){
	for(int i=0;atts[i];i+=2){
	    const char *name = atts[i];
	    const char *value = atts[i+1];
	    if(!strcmp(name,"segname")) segname = value;
	    if(!strcmp(name,"sigmode")) sigmode = atoi(value);
	    if(!strcmp(name,"alg")) alg = value;
	    if(!strcmp(name,"seglen")) seglen = atoi(value);
	}
	in_seghash = true;
	get_cdata = true;
	return;
    }
    if(strcmp(name,"signingcertificate")==0){
	in_cert = true;
	get_cdata = true;
	return;
    }
    if(get_cdata_segment && strcmp(name,get_cdata_segment)==0){
	get_cdata = true;
	return;
    }
}

void cHandler(void *userData,const XML_Char *s,int len)
{
    segmenthash *sh = (segmenthash *)userData;
    if(sh->get_cdata==false) return;	// don't want cdata
    sh->cdata.append(s,len);
}

void endElement(void *userData, const char *name)
{
    segmenthash *sh = (segmenthash *)userData;
    sh->endElement(name);
}


void segmenthash::endElement(const char *name)
{
    if(get_cdata_segment && strcmp(name,get_cdata_segment)==0){
	get_cdata = false;
	XML_StopParser(parser,0);
	return;
    }
    if(in_seghash && af){
	if(segname.size()==0) return;	// don't have a segment name
	/* Try to validate this one */
	size_t  hashbuf_len = cdata.size() + 2;
	u_char *hashbuf = (u_char *)malloc(hashbuf_len);
	hashbuf_len = b64_pton_slg((char *)cdata.c_str(),cdata.size(),hashbuf,hashbuf_len);
	if(alg=="sha256"){
	    /* TODO: Don't re-validate something that's already validated */
	    int r = af_hash_verify_seg2(af,segname.c_str(),hashbuf,hashbuf_len,sigmode);
	    if(r==AF_HASH_VERIFIES){
		total_validated++;
	    }
	    else total_invalid++;
	}
	free(hashbuf);
	in_seghash = false;
    }
    if(in_cert && af){
	BIO *cert_bio = BIO_new_mem_buf((char *)cdata.c_str(),cdata.size());
	PEM_read_bio_X509(cert_bio,&cert,0,0);
	BIO_free(cert_bio);
	pubkey = X509_get_pubkey(cert);
	in_cert = false;
    }
    cdata = "";				// erase it
}

string get_xml_field(const char *buf,const char *field) 
{
    segmenthash sh;
    sh.get_cdata_segment = field;
    sh.parse(buf,strlen(buf));
    return sh.cdata;
}

/* verify the chain signature; return 0 if successful, -1 if failed.
 * The signature is a block of XML with a base64 encoded at the end.
 */
int  verify_bom_signature(AFFILE *af,const char *buf)
{
    OpenSSL_add_all_digests();
    const EVP_MD *sha256 = EVP_get_digestbyname("sha256");

    if(!sha256){
	fprintf(stderr,"OpenSSL does not have SHA256; signatures cannot be verified.\n");
	return -1;
    }

    const char *cce = "</" AF_XML_AFFBOM ">\n";
    const char *chain_end = strstr(buf,cce);
    if(!chain_end){
	warn("end of chain XML can't be found\n");
	return -1;		// can't find it
    }
    const char *sig_start = chain_end + strlen(cce);

    BIO *seg = BIO_new_mem_buf((void *)buf,strlen(buf));
    if(BIO_seek(seg,0)!=0){
	printf("Cannot seek to beginning of BIO mem?");
	return -1;
    }
    X509 *cert = 0;
    PEM_read_bio_X509(seg,&cert,0,0);	// get the contained x509 cert
    BIO_free(seg);

    /* Now get the binary signature */
    u_char sigbuf[1024];
    int sigbuf_len = b64_pton_slg(sig_start,strlen(sig_start),sigbuf,sizeof(sigbuf));
    if(sigbuf_len<80){
	warn("BOM is not signed");
	return -1;
    }

    /* Try to verify it */
    EVP_MD_CTX *md = EVP_MD_CTX_new();
    EVP_VerifyInit(md,sha256);
    EVP_VerifyUpdate(md,buf,sig_start-buf);
    int r = EVP_VerifyFinal(md,sigbuf,sigbuf_len,X509_get_pubkey(cert));
    EVP_MD_CTX_free(md);
    if(r!=1){ 
	printf("BAD SIGNATURE ON BOM\n");
	return -1;
    }
    
    print_x509_info(cert);
    printf("Date: %s\n",get_xml_field(buf,"date").c_str());
    printf("Notes: \n%s\n",get_xml_field(buf,"notes").c_str());
    
    /* Now extract the XML block, terminating at the beginning of the XML signature */
    char *buffer_without_signature = strdup(buf);
    char *sigend = strstr(buffer_without_signature,cce);
    if(sigend){
	sigend[strlen(cce)] = 0;/* terminate the XML to remove the signature */
    }

    segmenthash sh;
    sh.af = af;
    if (!sh.parse(buffer_without_signature, strlen(buffer_without_signature))){
	fprintf(stderr, "expat error: %s at line %d\n",
		XML_ErrorString(XML_GetErrorCode(sh.parser)),
		(int)XML_GetCurrentLineNumber(sh.parser));
	fprintf(stderr,"buffer without signature:\n%s\n",buffer_without_signature);
	return 1;
    }
    free(buffer_without_signature);
    return 0;
}
#endif

int crypto_verify(AFFILE *af,u_char *certbuf,size_t certbuf_len)
{

    seglist segments(af);
    seglist no_sigs;
    seglist bad_sigs;
    seglist good_sigs;
    seglist unknown_errors;

    for(seglist::const_iterator seg = segments.begin();
	seg != segments.end();
	seg++){

	if(parse_chain(seg->name)>=0) continue; // chain of custody segments don't need signatures
	
	const char *segname = seg->name.c_str();
	int i =af_sig_verify_seg(af,segname);
	if(opt_verbose){
	    printf("af_sig_verify_seg(af,%s)=%d\n",segname,i);
	}
	switch(i){
	case AF_ERROR_SIG_NO_CERT:
	    err(1,"%s: no public key in AFF file\n",af_filename(af));
	case AF_ERROR_SIG_BAD:
	    bad_sigs.push_back(*seg);
	    break;
	case AF_ERROR_SIG_READ_ERROR:
	    no_sigs.push_back(*seg);
	    break;
	case AF_SIG_GOOD:
	    good_sigs.push_back(*seg);
	    break;
	case AF_ERROR_SIG_SIG_SEG:
	    break;			// can't verify the sig on a sig seg
	case AF_ERROR_SIG_NOT_COMPILED:
	    errx(1,"AFFLIB was compiled without signature support. Cannot continue.\n");
	default:
	    unknown_errors.push_back(*seg);
	    break;
	}
    }
    const char *prn = "";
    /* Tell us something about the certificate */
    BIO *cert_bio = BIO_new_mem_buf(certbuf,certbuf_len);
    X509 *cert = 0;
    PEM_read_bio_X509(cert_bio,&cert,0,0);
    if(!cert) errx(1,"Cannot decode certificate");
    printf("\n");
    printf("Filename: %s\n",af_filename(af));
    printf("# Segments signed and Verified:       %d\n",(int)good_sigs.size());
    printf("# Segments unsigned:                  %d\n",(int)no_sigs.size());
    printf("# Segments with corrupted signatures: %d\n",(int)bad_sigs.size());
    printf("\n");
    print_x509_info(cert);

    int compromised = 0;
    for(seglist::const_iterator seg = good_sigs.begin(); seg != good_sigs.end() && opt_all;
	seg++){
	if(*seg==good_sigs.front()) printf("%sSegments with valid signatures:\n",prn);
	printf("\t%s\n",seg->name.c_str());
	prn = "\n";
    }
    for(seglist::const_iterator seg = no_sigs.begin();
	seg != no_sigs.end();
	seg++){
	if(*seg==no_sigs.front()) printf("%sUnsigned segments:\n",prn);
	printf("\t%s\n",seg->name.c_str());
	prn = "\n";

	/* Only unsigned data segments are a problem */
	if(af_segname_page_number(seg->name.c_str())>=0){
	    compromised++;
	}
    }
    for(seglist::const_iterator seg = bad_sigs.begin();
	seg != bad_sigs.end();
	seg++){
	if(*seg==bad_sigs.front()) printf("%sBad signature segments:\n",prn);
	printf("\t%s\n",seg->name.c_str());
	prn = "\n";
	compromised++;
    }
    for(seglist::const_iterator seg = unknown_errors.begin();
	seg != unknown_errors.end();
	seg++){
	if(*seg==unknown_errors.front()) printf("%sUnknown error segments:\n",prn);
	printf("\t%s\n",seg->name.c_str());
	prn = "\n";
	compromised++;
    }

    int highest = highest_chain(segments);
    printf("\nNumber of custody chains: %d\n",highest+1);
    for(int i=0;i<=highest;i++){
	/* Now print each one */
	printf("---------------------\n");
	printf("Signed Bill of Material #%d:\n\n",i+1);

	/* Get the segment and verify */
	size_t chainbuf_len = 0;
	char segname[AF_MAX_NAME_LEN];
	snprintf(segname,sizeof(segname),AF_BOM_SEG,i);
	if(af_get_seg(af,segname,0,0,&chainbuf_len)){
	    printf("*** BOM MISSING ***\n");
	    compromised++;
	}
	char *chainbuf = (char *)malloc(chainbuf_len+1);
	if(af_get_seg(af,segname,0,(u_char *)chainbuf,&chainbuf_len)){
	    printf("*** CANNOT READ BOM ***\n");
	    compromised++;
	}
		
	chainbuf[chainbuf_len]=0;	// terminate
#ifdef USE_AFFSIGS
	if(verify_bom_signature(af,chainbuf)){
	    printf("*** BOM SIGNATURE INVALID ***\n");
	    compromised++;
	}
#else
	printf("BOM signature cannot be verified beause libxpat is not available.\n");
#endif
    }
    printf("---------------------\n");
    af_close(af);
#ifdef USE_AFFSIGS
    if(compromised){
	printf("\nEVIDENCE FILE DOES NOT VERIFY.\n");
	printf("ERRORS DETECTED: %d\n",compromised);
	printf("EVIDENTUARY VALUE MAY BE COMPROMISED.\n");
	return -1;
    }
    printf("\nEVIDENCE FILE VERIFIES.\n");
    return 0;
#endif
    printf("\n");
    return -1;
}

int hash_verify(AFFILE *af)
{
    /* See if there is a SHA1 segment */
    unsigned char sha1_buf[20];
    unsigned char md5_buf[16];
    char hexbuf[256];
    size_t sha1_len = sizeof(sha1_buf);
    size_t md5_len  =sizeof(md5_buf);
    const EVP_MD *md5_evp = 0;
    const EVP_MD *sha1_evp = 0;
    EVP_MD_CTX *md5 = EVP_MD_CTX_new();
    EVP_MD_CTX *sha1 = EVP_MD_CTX_new();
    if(af_get_seg(af,AF_SHA1,0,sha1_buf,&sha1_len)==0){
	printf("SHA1 stored in file:     %s\n",af_hexbuf(hexbuf,sizeof(hexbuf),sha1_buf,sha1_len,0));
	sha1_evp = EVP_get_digestbyname("sha1");
	EVP_DigestInit(sha1,sha1_evp);
    }
    if(af_get_seg(af,AF_MD5,0,md5_buf,&md5_len)==0){
	printf("MD5 stored in file:      %s\n",af_hexbuf(hexbuf,sizeof(hexbuf),md5_buf,md5_len,0));
	md5_evp = EVP_get_digestbyname("md5");
	EVP_DigestInit(md5,md5_evp);
    }
    /* Might as well read this puppy */
    u_char *buf = (u_char *)malloc(af_get_pagesize(af));
    ssize_t readsize = 0;
    ssize_t total_read = 0;
    af_seek(af,0L,0);
    aftimer t;
    t.start();
    printf("\n");
    do {
	double frac = (double)total_read / af_get_imagesize(af);
	printf("  Read %14zd/%14" PRId64" bytes; done in %s\n",
	       total_read,
	       af_get_imagesize(af),
	       t.eta_text(frac).c_str());
	readsize = af_read(af,buf,af_get_pagesize(af));
	if(readsize<1) break;
	if(md5_evp) EVP_DigestUpdate(md5,buf,readsize);
	if(sha1_evp) EVP_DigestUpdate(sha1,buf,readsize);
	total_read += readsize;
    } while(total_read < af_get_imagesize(af));
	
    printf("\n");

    if(sha1_evp){
	unsigned char sha1_calc[32];
	unsigned int sha1_calc_len = sizeof(sha1_calc);
	
	EVP_DigestFinal(sha1,sha1_calc,(unsigned int *)&sha1_calc_len);
	printf("Calculated SHA1: %s  ",af_hexbuf(hexbuf,sizeof(hexbuf),sha1_calc,sha1_calc_len,0));
	if(memcmp(sha1_buf,sha1_calc,sha1_len)==0){
	    printf("VERIFIES\n");
	} else {
	    printf("INVALID\n");
	}
    }

    if(md5_evp){
	unsigned char md5_calc[32];
	unsigned int md5_calc_len = sizeof(md5_calc);
	
	EVP_DigestFinal(md5,md5_calc,(unsigned int *)&md5_calc_len);
	printf("Calculated MD5:  %s          ",af_hexbuf(hexbuf,sizeof(hexbuf),md5_calc,md5_calc_len,0));
	if(memcmp(md5_buf,md5_calc,md5_len)==0){
	    printf("VERIFIES\n");
	} else {
	    printf("INVALID\n");
	}
    }

    EVP_MD_CTX_free(md5);
    EVP_MD_CTX_free(sha1);
    af_close(af);
    return 0;
}

int process(const char *fn)
{
    AFFILE *af = af_open(fn,O_RDONLY,0666);
    if(!af) af_err(1,fn);

    /* Get the public key */
    unsigned char certbuf[65536];
    size_t certbuf_len = sizeof(certbuf);
    if(af_get_seg(af,AF_SIGN256_CERT,0,certbuf,&certbuf_len)){
	/* See if it is present, but encrypted */
	if(af_get_seg(af,AF_SIGN256_CERT AF_AES256_SUFFIX,0,0,0)==0){
	    errx(1,"%s: signed file is encrypted; present decryption key to verify signature",fn);
	}
	printf("%s: no signing certificate present. \n\n",fn);
	return hash_verify(af);
    }
    return crypto_verify(af,certbuf,certbuf_len);
}


int main(int argc,char **argv)
{
    int ch;

    while ((ch = getopt(argc, argv, "ach?vV")) != -1) {
	switch (ch) {
	case 'a': opt_all = 1;    break;
	case 'c': opt_change = 1; break;
	case 'v': opt_verbose++;  break;
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

    if(argc!=1){
	usage();
    }

    OpenSSL_add_all_digests();
    return process(argv[0]);
}
