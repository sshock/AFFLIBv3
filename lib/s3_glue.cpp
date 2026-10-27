#include "affconfig.h"

/*
 * Distributed under the Berkeley 4-part license
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#ifdef USE_S3

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#if (defined(__FreeBSD_version)) && (__FreeBSD_version<500000) && (!defined(BAD_STL))
#define BAD_STL
#endif


#include "s3_glue.h"
#include "curl/curl.h"
#include "base64.h"

#include <expat.h>
#include <time.h>
#include <netinet/in.h>

#ifdef HAVE_MD5
#include <openssl/evp.h>
#endif

#ifdef HAVE_OPENSSL_HMAC_H
#include <openssl/hmac.h>
#endif

#ifdef HAVE_ERR_H
#include <err.h>
#endif

#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif

#if !defined(HAVE_MD5)
#error S3 support requires MD5 support
#endif

int s3_debug = 0;
int s3_retry_max   = 5;			// read by the code

/* debug levels:
 *  1 - print retries
 *  2- print queries
 *  3- print full results
 */

/* Counters that are used; they aren't threadsafe, but they are never referenced */
int s3_request_retry_count = 0;
int s3_object_put_retry_count = 0;
long long s3_bytes_written=0;
long long s3_bytes_read=0;

using namespace std;
using namespace s3;

const char *aws_access_key_id;
const char *aws_secret_access_key;
const char *aws_base_url = "http://s3.amazonaws.com/";

/* Simson's S3 implementation in C++.
 * Note that libcurl and expat will both handle data in chunks, so
 * technically we don't need to create a single buffer with the entire response
 * from AWS. For AFFLIB, though, we want to work on data as buffers.
 * As a result, we create and use a buffer for all work.
 */

namespace s3 {

static string itos(int i)
{
    char buf[64];
    snprintf(buf,sizeof(buf),"%d",i);
    return string(buf);
}

size_t buffer::write(const char *b,size_t count){
    if(!writable) return false;
    base = (char *)realloc(base,len+count);
    if(base){
	memcpy(base+len,b,count);	// copy the memory over
	len += count;
	return count;
    }
    return 0;
}
size_t buffer::read(char *b,size_t count){
    if(base){
	if(count>len-ptr) count=len-ptr;
	memcpy(b,base+ptr,count);
	ptr += count;
	return count;
    }
    return 0;
}

void buffer::print() {
    fwrite(base,1,len,stdout);
}

void buffer::clear(){
    if(base){
	free(base);
	base = 0;
    }
    len = 0;
}


static size_t  buffer_write(void  *buffer,  size_t  size,  size_t  nmemb,  void *userp)
{
    return ((class buffer *)userp)->write((const char *)buffer,size * nmemb);
}

static size_t  buffer_read(void  *buffer,  size_t  size,  size_t  nmemb,  void *userp)
{
    return ((class buffer *)userp)->read((char *)buffer,size * nmemb);
}


static void startElement(void *userData, const char *name, const char **atts)
{
    class s3_result *einfo = (class s3_result *)userData;
    einfo->depth++;
    switch(einfo->depth){
    case 1:
	if(!strcmp(name,"ListBucketResult")) {einfo->lbr = new ListBucketResult();break;}
	if(!strcmp(name,"ListAllMyBucketsResult")) {einfo->lambr = new ListAllMyBucketsResult();break;}
	fprintf(stderr,"\ns3 buffer:\n%s",einfo->buf->base);
	errx(1,"Unknown XML element from S3: '%s'",name);
	break;
    case 2:
	if(einfo->lbr && !strcmp(name,"Contents")){ einfo->lbr->contents.push_back(new Contents());break;}
	break;
    case 3:
	if(einfo->lambr && !strcmp(name,"Bucket")){ einfo->lambr->Buckets.push_back(new Bucket());break;}
	break;
    }
}

static void endElement(void *userData, const char *name)
{
    class s3_result *einfo = (class s3_result *)userData;
    if(einfo->lambr){
	switch(einfo->depth){
	case 3:
	    if(!strcmp(name,"ID")){ einfo->lambr->OwnerID = einfo->cbuf;break;}
	    if(!strcmp(name,"DisplayName")){ einfo->lambr->OwnerDisplayName = einfo->cbuf;break;}
	    break;
	case 4:
	    if(!strcmp(name,"Name")) { einfo->lambr->Buckets.back()->Name = einfo->cbuf;break;}
	    if(!strcmp(name,"CreationDate")) { einfo->lambr->Buckets.back()->CreationDate = einfo->cbuf;break;}
	}
    }
    if(einfo->lbr){
	switch(einfo->depth){
	case 2:
	    if(!strcmp(name,"Name")){	     einfo->lbr->Name = einfo->cbuf; break;}
	    if(!strcmp(name,"Prefix")){      einfo->lbr->Prefix = einfo->cbuf;break;}
	    if(!strcmp(name,"Marker")){      einfo->lbr->Marker = einfo->cbuf;break;}
	    if(!strcmp(name,"MaxKeys")){     einfo->lbr->MaxKeys = atoi(einfo->cbuf.c_str());break;}
	    if(!strcmp(name,"IsTruncated")){ einfo->lbr->IsTruncated = tolower(einfo->cbuf[0]) == 't';break;}
	    break;
	case 3:
	    if(!strcmp(name,"Key")){         einfo->lbr->contents.back()->Key = einfo->cbuf; break;}
	    if(!strcmp(name,"LastModified")){einfo->lbr->contents.back()->LastModified = einfo->cbuf;break;}
	    if(!strcmp(name,"ETag")){        einfo->lbr->contents.back()->ETag = einfo->cbuf;break;}
	    if(!strcmp(name,"Size")){        einfo->lbr->contents.back()->Size = atoi(einfo->cbuf.c_str());break;}
	    break;
	case 4:
	    if(!strcmp(name,"ID")){          einfo->lbr->contents.back()->OwnerID = einfo->cbuf;break;}
	    if(!strcmp(name,"DisplayName")){ einfo->lbr->contents.back()->OwnerDisplayName = einfo->cbuf;break;}
	    break;
	default:;
	}
    }
#ifdef BAD_STL
    einfo->cbuf = "";
#else
    einfo->cbuf.clear();
#endif
    einfo->depth--;
}

static void characterDataHandler(void *userData,const XML_Char *s,int len)
{
    class s3_result *einfo = (class s3_result *)userData;
    einfo->cbuf.append((const char *)s,len);
}


static class s3_result *xml_extract_response(const class buffer *buf)
{
    class s3_result *e = new s3_result();

    e->buf = buf;
    XML_Parser parser = XML_ParserCreate(NULL);
    XML_SetUserData(parser, e);
    XML_SetElementHandler(parser, startElement, endElement);
    XML_SetCharacterDataHandler(parser,characterDataHandler);

    if (!XML_Parse(parser, (const char *)buf->base, buf->len, 1)) {
	char buf2[2048];
	snprintf(buf2,sizeof(buf2),
		 "XML Error: %s at line %d",
		 XML_ErrorString(XML_GetErrorCode(parser)),(int)XML_GetCurrentLineNumber(parser));
	fprintf(stderr,"%s:\n",buf2);
	XML_ParserFree(parser);
	return 0;
    }
    XML_ParserFree(parser);
    return e;
}



/* Create the cannonical string for the headers */
static string canonical_string(string method,string path,curl_slist *headers, time_t expires)
{
    /* Iterate through the headers a line at a time */
    map<string,string> interesting_headers;

    for(;headers;headers = headers->next){
	char *line = strdup(headers->data);
	char *word;
	char *brk2;
	word = strtok_r(line,": ",&brk2);
	if(word){
	    if(strcasecmp(word,"Date")==0 ||
	       strcasecmp(word,"Range")==0 ||
	       strncmp(word,AMAZON_METADATA_PREFIX,strlen(AMAZON_METADATA_PREFIX))==0){
		char *value = strtok_r(NULL,"",&brk2);
		while(value && isspace(*value)) value++;
		interesting_headers[word] = value;
	    }
	}
	free(line);
    }
    /* Add the headers that we don't have */

    /* handle the date */

    /* Get the sorted headers */
    vector<string> sorted_header_keys;
    for(map<string,string>::const_iterator i = interesting_headers.begin();
	i!=interesting_headers.end();
	i++){
	sorted_header_keys.push_back(i->first);
    }

#ifndef BAD_STL
    sort(sorted_header_keys.begin(),sorted_header_keys.end());
#endif

    string buf = method + "\n";
    buf += "\n";			// content-md5 value
    buf += "\n";			// content-type value

    /* Either put in a date header or else do the expires */
    if(expires){
	char b[64];
	snprintf(b,sizeof(b),"%d\n",(int)expires);
	buf += b;
    }
    else {
	buf += interesting_headers["Date"] + "\n"; //  date
    }

    /* AMAON_HEADER_PREFIX headers only... */
    for(vector<string>::const_iterator i = sorted_header_keys.begin();
	i != sorted_header_keys.end();
	i++){
	if(i->substr(0,strlen(AMAZON_METADATA_PREFIX))==AMAZON_METADATA_PREFIX){
	    buf += *i + ":" + interesting_headers[*i] + "\n";
	}
    }
    buf += "/" + path;			// the resource

    //printf("canonical: \n===========\n%s\n=========\n",buf.c_str());

    return buf;

}

static string encode(const char *aws_secret_access_key,string str)
{
    unsigned char md[20];
    uint32_t md_len = sizeof(md);

    /* Note: This MUST be sha1() */
    HMAC(EVP_sha1(),aws_secret_access_key,strlen(aws_secret_access_key),
	 (const unsigned char *)str.c_str(),str.size(),
	 md,&md_len);
    /* Now encode this to base64 */
    char b64str[64];
    memset(b64str,0,sizeof(b64str));
    b64_ntop(md,md_len,b64str,sizeof(b64str));
    return string(b64str);
}



static string quote_plus(string &url)
{
    /* encode the URL */
    string eurl;
    char buf[6];
    for(string::const_iterator c = url.begin(); c != url.end(); c++){
	switch(*c){
	case '%':
	case ';':
	case '/':
	case '?':
	case '@':
	case '&':
	case '=':
	case '+':
	case '$':
	case ',':
	    sprintf(buf,"%%%02X",*c);
	    eurl += buf;
	    continue;
	case ' ':
	    eurl += "+";
	    continue;
	default:
	    eurl += *c;
	    continue;
	}
    }
    return eurl;
}

#ifndef HAVE_ISDIGIT
static int isdigit(char ch)
{
    return ch>='0' && ch<='9';
}
#endif

static int hexval(int ch) { return (isdigit(ch) ? ch-'0' : ch-'a'+10);}

/*
 * Execute an S3 request:
 * method  - method to execute.
 * path    - path for the object.
 * query   - anything optional after the "?" in the path
 * expires - When the authorization URL should expire.
 * sendbuf - if we are sending something ,this is what is being sent.
 * sendbuflen - how long that buffer is
 * extraheaders - any additional headers that should be sent; useful for metadata
 *
 * Returns a response buffer
 */

/* CURLINFO_RESPONSE_CODE is the new name for the option previously known as
 * CURLINFO_HTTP_CODE.
 */


#ifndef CURLINFO_RESPONSE_CODE
#define CURLINFO_RESPONSE_CODE CURLINFO_HTTP_CODE
#endif

class response_buffer *request(string method,string path,string query,time_t expires,
			       const char *sendbuf,size_t sendbuflen,
			       const s3headers *extraheaders)
{
    /* Note: this function is not threadsafe */
    static bool curl_initted = false;
    if(!curl_initted){
	curl_global_init(CURL_GLOBAL_ALL);
	curl_initted=true;
    }

    int retry_count=0;
    class response_buffer *b = 0;
    class buffer *h = 0;
    do {

	if(s3_debug>1) printf("==================================================\n");
	if(s3_debug && retry_count>0) printf("=== S3 RETRY %d ===\n",retry_count);

	CURL *c = curl_easy_init();
	struct curl_slist *headers=NULL;

	if(expires==0){
	    /* Add the Date: field to the header */
	    struct tm tm;
	    time_t t = time(0);
	    char date[64];
	    strftime(date,sizeof(date),"Date: %a, %d %b %Y %X GMT",gmtime_r(&t,&tm));
	    headers = curl_slist_append(headers, date);
	}

	/* Add the extra headers */
	while(extraheaders  && extraheaders[0].name){
	    int len = strlen(extraheaders[0].name)+strlen(extraheaders[0].value)+4;
	    char *buf = (char *)alloca(len);
	    snprintf(buf,len,"%s: %s",extraheaders[0].name,extraheaders[0].value);
	    headers = curl_slist_append(headers, buf);
	    extraheaders++;
	}

	string url = aws_base_url + path;
	string canonical_str     = canonical_string(method,path,headers,expires);
	string encoded_canonical = encode(aws_secret_access_key,canonical_str);

	if(expires==0){
	    /* Create an Authorization header */

	    char authorization[96];

	    snprintf(authorization,sizeof(authorization),"Authorization: AWS %s:%s",
		     aws_access_key_id,encoded_canonical.c_str());
	    headers = curl_slist_append(headers, authorization);
	    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
	}

	if(expires){
	    /* Add authorization to the URL*/
	    if(query.size()>0) query += "&";
	    query += "Signature=" + quote_plus(encoded_canonical);
	    query += "&Expires=" + itos(expires);
	    query += "&AWSAccessKeyId=" + string(aws_access_key_id);
	}

	if(query.size()>0){
	    url += "?" + query;
	}

	if(b) delete b;
	b = new response_buffer();
	memset(b->ETag,0,sizeof(b->ETag));
	if(s3_debug>1) curl_easy_setopt(c,CURLOPT_VERBOSE,1);
	if(method != "GET"){
	    curl_easy_setopt(c,CURLOPT_CUSTOMREQUEST,method.c_str());
	}

	if(method == "HEAD"){
	    curl_easy_setopt(c,CURLOPT_NOBODY,1);
	}

	/* Queries that take longer than an hour should timeout */
	curl_easy_setopt(c,CURLOPT_TIMEOUT,60*60);

	/* Disable DNS cache */
	curl_easy_setopt(c,CURLOPT_DNS_CACHE_TIMEOUT,0); // per amazon specification
	curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,buffer_write);
	curl_easy_setopt(c,CURLOPT_WRITEDATA,b); // fourth argument
	curl_easy_setopt(c,CURLOPT_URL,url.c_str());

	/* Are we sending data */
	class buffer *sendbuffer = 0;
	if(sendbuf){
	    sendbuffer = new buffer(sendbuf,sendbuflen);
	    curl_easy_setopt(c,CURLOPT_READFUNCTION,buffer_read);
	    curl_easy_setopt(c,CURLOPT_READDATA,sendbuffer);
	    curl_easy_setopt(c,CURLOPT_UPLOAD,1);
	    curl_easy_setopt(c,CURLOPT_INFILESIZE,sendbuflen);
	    //fprintf(stderr,"***** sendbuflen= %d %qd\n",sizeof(sendbuflen),sendbuflen);
	}

	/* Make provisions to get the response headers */
	if(h) delete h;
	h = new buffer();
	curl_easy_setopt(c,CURLOPT_HEADERFUNCTION,buffer_write);
	curl_easy_setopt(c,CURLOPT_WRITEHEADER,h); // fourth argument

	/* Make provisions for getting the headers */

	int success = curl_easy_perform(c);

	if(sendbuffer){
	    delete sendbuffer;
	    sendbuffer = 0;
	    if(success==0) s3_bytes_written += sendbuflen;
	}

	s3_bytes_read += h->len;
	s3_bytes_read += b->len;

	// CURL API says do not assume NULL terminate, so terminate it
	h->write("\000",1);
	curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&b->result);

	/* Now clean up */
	s3_request_retry_count = retry_count;
	if(headers) curl_slist_free_all(headers);
	curl_easy_cleanup(c);

	/* Process the results */
	if(success!=0){
	    delete h;
	    delete b;
	    s3_request_retry_count = retry_count;
	    return 0;			// internal CURL error
	}
	if(s3_debug>2){
	    printf("Header results:\n");
	    h->print();
	    printf("Data results:\n");
	    b->print();
	    printf("\n");
	}
    } while(b->result==500 && ++retry_count<s3_retry_max);

    if(b->result==404) errno=ENOENT;

    /* Pull out the headers */
    char *line,*brkt;
    for(line = strtok_r(h->base,"\r\n",&brkt);
	line;
	line = strtok_r(NULL,"\r\n",&brkt)){
	char *cc = strchr(line,':');
	if(cc){
	    *cc++ = '\000';
	    while(*cc && isspace(*cc)) cc++;
	    b->rheaders[line] = cc;
	}
    }

    /* Find the ETag in the header and put in the buffer */
    const char *e = b->rheaders["ETag"].c_str();
    if(strlen(e)==34){
	for(int i=0;i<16;i++){
	    b->ETag[i] = (hexval(e[i*2+1])<<4) + hexval(e[i*2+2]);
	}
    }

    delete h;				// we don't care about it
    if(s3_debug>1) printf(".\n\n");
    return b;
}

response_buffer *get_url(const char *url)
{
    int retry_count = 0;
    response_buffer *b = new response_buffer();
    do {
	CURL *c = curl_easy_init();
	curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,buffer_write);
	curl_easy_setopt(c,CURLOPT_WRITEDATA,b);
	curl_easy_setopt(c,CURLOPT_URL,url);
	curl_easy_perform(c);
	curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&b->result);
	curl_easy_cleanup(c);
    } while(b->result!=200 && ++retry_count<s3_retry_max);
    s3_request_retry_count = retry_count;
    return b;
}




class s3_result *list_buckets()
{
    time_t expires = time(0)+60;
    expires = 0;
    class response_buffer *b = request("GET","","",expires,0,0,0);

    class s3_result *r = xml_extract_response(b);
    delete b;
    return r;
}

class s3_result *list_bucket(string bucket,string prefix,string marker,int max_keys)
{
    string query;


    if(prefix.size()>0) query += "prefix=" + prefix;
    if(marker.size()>0){
	if(query.size()>0) query += "&";
	query += "marker=" + marker;
    }
    if(max_keys>0){
	if(query.size()>0) query += "&";;
	query += "max-keys=" + itos(max_keys);
    }
    class response_buffer *b = request("GET",bucket,query,0,0,0,0);
    if(!b) return 0;
    class s3_result *r = xml_extract_response(b);
    delete b;
    return r;
}

/*
 * af_hexbuf:
 * Turn a binay string into a hex string, optionally with spaces.
 */

#define HEXBUF_NO_SPACES 0
#define HEXBUF_SPACE2    0x0001	// space every 2 characters
#define HEXBUF_SPACE4    0x0002	// space every 4 characters
#define HEXBUF_UPPERCASE 0x1000	// uppercase
static const char *hexbuf(char *dst,int dst_len,const unsigned char *bin,int bytes,int flag)
{
    int charcount = 0;
    const char *start = dst;		// remember where the start of the string is
    const char *fmt = (flag & HEXBUF_UPPERCASE) ? "%02X" : "%02x";

    *dst = 0;				// begin with null termination
    while(bytes>0 && dst_len > 3){
	sprintf(dst,fmt,*bin); // convert the next byte
	dst += 2;
	bin += 1;
	dst_len -= 2;
	bytes--;
	charcount++;			// how many characters

	if((flag & HEXBUF_SPACE2) || ((flag & HEXBUF_SPACE4) && charcount%2==0)){
	    *dst++ = ' ';
	    *dst   = '\000';
	    dst_len -= 1;
	}
    }
    return start;			// return the start
}


/* object_put:
 * Put an object. Make sure that the MD5 of the response matches..
 * Makes a few retry attempts
 * Return 0 if success, -1 if failure.
 */
int object_put(string bucket,string path,
		  const char *buf,size_t buflen,
		  const struct s3headers *extraheaders)
{
    unsigned char md5[16];
    memset(md5,0,sizeof(md5));
    af_MD5((const unsigned char *)buf,buflen,md5);
    for(int i=0;i<s3_retry_max;i++){
	s3_object_put_retry_count = i;
	if(i>0){
	    fprintf(stderr,"S3: Attempt to write object '%s' failed. Retrying...\n",
		    path.c_str());
	}

	response_buffer *res = request("PUT",bucket + "/" + path,"",0,buf,buflen,extraheaders);
	if(!res) {
	    fprintf(stderr,"S3 request: No response.\n");
	    continue;
	}
	if(memcmp(res->ETag,md5,16)==0){	/* Check the MD5 of the response */
	    delete res;
	    return 0;
	}
	char buf0[64],buf1[64];
	fprintf(stderr,"S3: Expected ETag '%s' got '%s'\n",
		hexbuf(buf0,sizeof(buf0),md5,16,HEXBUF_SPACE4),
		hexbuf(buf1,sizeof(buf1),res->ETag,16,HEXBUF_SPACE4));
	delete res;
    }
    /* Write failed. Delete the written object and return */
    response_buffer *res = request("DELETE",bucket + "/" + path,"",0,0,0,0);
    if(res) delete res;
    errno = EIO;
    return -1;
}

int bucket_mkdir(string bucket)
{
    class response_buffer *b =  request("PUT",bucket,"",0,0,0,0);
    int result = b->result;
    delete b;
    switch(result){
    case 409:errno=EEXIST; return -1;
    case 200:errno=0;return 0;
    }
    return -1;			// some unknown error
}

int bucket_rmdir(string bucket)
{
    class response_buffer *b = request("DELETE",bucket,"",0,0,0,0);
    int result = b->result;
    delete b;
    switch(result){
    case 403:errno=EACCES; return -1;
    case 404:errno=ENOENT; return -1;
    case 409:errno=ENOTEMPTY; return -1;
    case 204:errno=0;return 0;		// no content is actually what it gives
    case 200:errno=0;return 0;		// doesn't seem to give this one
    }
    return -1;			// some unknown error
}

class response_buffer *object_get(string bucket,string path,const s3headers *extra_headers)
{
    return request("GET",bucket + "/" + path,"",0,0,0,extra_headers);
}

class response_buffer *object_head(string bucket,string path,const s3headers *extra_headers)
{
    return request("HEAD",bucket + "/" + path,"",0,0,0,extra_headers);
}

int object_rm(string bucket,string path)
{
    class response_buffer *b = request("DELETE",bucket + "/" + path,"",0,0,0,0);
    if(b){
	delete b;
	return 0;
    }
    return -1;
}

}

void s3_audit(int i)
{
    if(i>0 || s3_bytes_written>0 || s3_bytes_read>0){
	fprintf(stderr,"\n");
	fprintf(stderr,"S3 bytes written: %qu\n",s3_bytes_written);
	fprintf(stderr,"S3 bytes read: %qu\n",s3_bytes_read);
    }
}
#endif /* USE_S3 */
