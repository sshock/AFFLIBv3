/*
 * aff_bom.cpp:
 *
 * PUBLIC DOMAIN SOFTWARE.
 *
 * The software provided here is released by the Naval Postgraduate
 * School (NPS), an agency of the US Department of the Navy, USA.  The
 * software bears no warranty, either expressed or implied. NPS does
 * not assume legal liability nor responsibility for a User's use of
 * the software or the results of such use.  Please note that within
 * the United States, copyright protection, under Section 105 of the
 * United States Code, Title 17, is not available for any work of the
 * United States Government and/or for any works created by United
 * States Government employees. User acknowledges that this software
 * contains work which was created by NPS employee(s) and is therefore
 * in the public domain and not subject to copyright.
 * --------------------------------------------------------------------
 *
 * Change History:
 * Simson L. Garfinkel - 2008 - Created
 */


#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"
#include "utils.h"
#ifdef HAVE_ERR_H
#include "err.h"
#endif

#include "aff_bom.h"

#ifdef HAVE_READLINE_READLINE_H
#include <readline/readline.h>
#endif

/* Support OpenSSL before 1.1.0 */
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
#define EVP_MD_CTX_new EVP_MD_CTX_create
#define EVP_MD_CTX_free EVP_MD_CTX_destroy
#endif

using namespace std;

int parse_chain(const string &name)
{
    char ch;
    int num;
    if(sscanf(name.c_str(),AF_BOM_SEG"%c",&num,&ch)==1) return num;
    return -1;
}

int highest_chain(aff::seglist &segments)
{
    int  highest_chain   = -1;
    for(aff::seglist::const_iterator seg = segments.begin(); seg!=segments.end() ;seg++){
	/* Are any of the segments in the input file signed?
	 * If so, we can't use the AFFLIB signing mechanisms, only the
	 * chain-of-custody mechanisms in this program.
	 */
	/* Are there any chain of custody segments? */
	int num= parse_chain(seg->name);
	if(num>highest_chain) highest_chain = num;
    }
    return highest_chain;

}

#ifdef HAVE_OPENSSL_BIO_H
/* BIO_xmlescape:
 * sends str to the bio, escaping for XML.
 */
int BIO_write_xml_escape(BIO *bio,const char *str,int parsed)
{
    while(*str){
	switch(*str){
	case '&': BIO_write(bio,"&amp;",5);break;
	case '<': BIO_write(bio,"&lt;",4);break;
	case '>': BIO_write(bio,"&gt;",4);break;
	case '"': BIO_write(bio,"&quot;",6);break;
	case '\'': BIO_write(bio,"&apos;",6);break;
	case '\\':
	    if(parsed) BIO_write(bio,str,1);
	    else BIO_write(bio,"\\\\",2);
	    break;
	default: BIO_write(bio,str,1);break;
	}
	str++;
    }
    return 0;
}
#endif

#ifdef USE_AFFSIGS
char *aff_bom::get_notes()
{
    if(isatty(fileno(stdin))){
	printf("Enter notes. Terminate input with a '.' on a line by itself:\n");
    }
    if(notes) return notes;
    notes = (char *)calloc(1,1);
    while(notes){
	char buf2[1024];
	char *val=0;

#ifdef HAVE_LIBREADLINE
	if(isatty(fileno(stdin))){
	    val = readline("");
	}
#endif
	if(val==0){
	    memset(buf2,0,sizeof(buf2));
	    val = fgets(buf2,sizeof(buf2)-1,stdin);
	    if(val==0) break;
	}
	if(strcmp(val,".")==0) break;
	notes = (char *)realloc(notes,strlen(notes)+strlen(val)+1);
	strcat(notes,val);
    }
    printf("Thank you.\n");
    return notes;
}

#ifdef HAVE_OPENSSL_BIO_H
int aff_bom::read_files(const char *cert_file,const char *key_file)
{
    BIO *bp_cert = BIO_new_file(cert_file,"r"); // read the certfile
    if (!bp_cert)
      return -1;
    PEM_read_bio_X509(bp_cert,&cert,0,0); // get an x509 cert
    BIO_free(bp_cert);
    if(!cert) return -1;		// can't read certificate file

    /* Now read the private key */
    BIO *bp_privkey = BIO_new_file(key_file,"r");
    privkey = PEM_read_bio_PrivateKey(bp_privkey,0,0,0);
    BIO_free(bp_privkey);
    if(privkey==0){
	X509_free(cert);
	cert = 0;
	return -1;
    }

    bom_open = true;
    xml = BIO_new(BIO_s_mem());	// where we are writing
    time_t clock = time(0);
    struct tm *tm = localtime(&clock);
    char timebuf[1024];
    strftime(timebuf,sizeof(timebuf),"<date type='ISO 8601'>%FT%T</date>",tm);

    BIO_printf(xml,"<%s version=\"1\">\n",AF_XML_AFFBOM);
    BIO_printf(xml,"  %s\n",timebuf);
    BIO_printf(xml,"  <program>afcopy</program>\n");
    if(opt_note){
	BIO_printf(xml,"  <notes>");
	BIO_write_xml_escape(xml,get_notes(),0);
	BIO_printf(xml,"  </notes>\n");
    }
    BIO_printf(xml,"  <signingcertificate>\n");
    PEM_write_bio_X509(xml,cert);
    BIO_printf(xml,"  </signingcertificate>\n");
    BIO_printf(xml,"  <affsegments>\n");
    return 0;
}

/* Add to the Bill of Materials */
void aff_bom::add(const char *segname,int sigmode,const u_char *seghash,size_t seghash_len)
{
    BIO_printf(xml,"<%s segname='%s' sigmode='%d' alg='sha256'>\n",
	       AF_XML_SEGMENT_HASH,segname,sigmode);
    if(BIO_flush(xml)!=1) return;	// something is wrong
    BIO *b64 = BIO_new(BIO_f_base64());
    xml = BIO_push(b64,xml);
    BIO_write(xml,seghash,seghash_len);
    if(BIO_flush(xml)!=1) return;	// another error...
    xml = BIO_pop(b64);
    BIO_printf(xml,"</%s>\n",AF_XML_SEGMENT_HASH);
}

void aff_bom::close()
{
    /* Terminate the XML block*/
    BIO_printf(xml,"</affsegments>\n");
    BIO_printf(xml,"</%s>\n",AF_XML_AFFBOM);

    OpenSSL_add_all_digests();
    const EVP_MD *sha256 = EVP_get_digestbyname("SHA256");

    if(sha256){
	/* now sign the XML */
	char *xbuf=0;
	size_t xlen = BIO_get_mem_data(xml,&xbuf);
	unsigned char sig[1024];
	u_int  siglen = sizeof(sig);

	EVP_MD_CTX *md = EVP_MD_CTX_new();
	EVP_SignInit(md,sha256);
	EVP_SignUpdate(md,xbuf,xlen);
	EVP_SignFinal(md,sig,&siglen,privkey);
	EVP_MD_CTX_free(md);

	/* Write the signature in base64 encoding... */
	BIO *b64 = BIO_new(BIO_f_base64());
	xml = BIO_push(b64,xml);
	BIO_write(xml,sig,siglen);
	if(BIO_flush(xml)!=1) return;	// something wrong

	/* Remove the base64 bio */
	xml = BIO_pop(b64);
    }
    bom_open = false;
}

int  aff_bom::write(AFFILE *af,aff::seglist &segments)
{
    assert(!bom_open);
    char segname[AF_MAX_NAME_LEN];
    snprintf(segname,sizeof(segname),AF_BOM_SEG,highest_chain(segments)+1);
    return af_update_seg_frombio(af,segname,0,xml);
}


#define SHA256_SIZE 32
void aff_bom::make_hash(u_char seghash[SHA256_SIZE], uint32_t arg,const char *segname,
			const u_char *segbuf, uint32_t segsize)
{
    OpenSSL_add_all_digests();		// probably a good idea
    const EVP_MD *sha256 = EVP_get_digestbyname("SHA256");

    if(sha256){
	unsigned int seghash_len = SHA256_SIZE;
	uint32_t arg_net = htonl(arg);
	EVP_MD_CTX *md = EVP_MD_CTX_new();		/* EVP message digest */
	EVP_DigestInit(md,sha256);
	EVP_DigestUpdate(md,(const unsigned char *)segname,strlen(segname)+1);
	EVP_DigestUpdate(md,(const unsigned char *)&arg_net,sizeof(arg_net));
	EVP_DigestUpdate(md,segbuf,segsize);
	EVP_DigestFinal(md,seghash,&seghash_len);
	EVP_MD_CTX_free(md);
    }
}

int aff_bom::add(AFFILE *af,const char *segname)
{
    /* Get the segment length first */
    size_t datalen = 0;
    if(af_get_seg(af,segname,0,0,&datalen)<0) return -1;
    uint32_t arg;
    u_char *segdata = (u_char *)malloc(datalen);
    if(!segdata) return -1;
    if(af_get_seg(af,segname,&arg,segdata,&datalen)<0){
	free(segdata);
	return -1;
    }
    u_char seghash[32];
    make_hash(seghash,arg,segname,segdata,datalen);
    add(segname,AF_SIGNATURE_MODE0,seghash,sizeof(seghash));
    free(segdata);
    return(0);

}


#endif   /* have_openssl_bio_h */
#endif   /* use_affsigs */


