#ifndef HASHEXTENT_H
#define HASHEXTENT_H

/**
 * hashextent: class to track a hash request or value
 * 
 * Simson L. Garfinkel
 * 2009-09-18: SLG - Added to repository
 * This file is a work of a US government employee and as such is in the Public domain.
 * Simson L. Garfinkel, March 12, 2012
 */

#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <map>
#include <set>
#include <algorithm>

/* Support OpenSSL before 1.1.0 */
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
#define EVP_MD_CTX_new EVP_MD_CTX_create
#define EVP_MD_CTX_free EVP_MD_CTX_destroy
#endif

using std::string;
using std::ostream;
using std::vector;
using std::map;

class hashextent {
public:
    hashextent():digest(0),digest_bits_(0){}
    ~hashextent(){
	if(digest) free(digest);
    }
    uint64_t start;
    uint64_t bytes;
    hashextent(uint64_t aStart,uint64_t aBytes):start(aStart),bytes(aBytes),digest(0){}
    hashextent(AFFILE *af,string alg,uint64_t aStart,uint64_t aBytes):start(aStart),bytes(aBytes),digest(0){
	compute_digest(af,alg);
    }
    static bool compare(const hashextent &e1,const hashextent &e2){
	return e1.start < e2.start;
    }

    static int hexcharval(char hex){
	if(hex>='0' && hex<='9') return hex-'0';
	if(hex>='A' && hex<='F') return hex+10-'A';
	if(hex>='a' && hex<='f') return hex+10-'a';
	return 0;
    }

    static string bin2hex(unsigned char *md,int len){
	std::stringstream sbuf;
	while(len>0){
	    char buf[3];
	    snprintf(buf,sizeof(buf),"%02x",md[0]);
	    sbuf << buf;
	    md ++;
	    len --;
	}
	return sbuf.str();
    }

    static int hex2bin(unsigned char *binbuf,size_t hexbuf_size,const char *hex){
	int bits = 0;
	while(hex[0] && hex[1] && hexbuf_size>0){
	    *binbuf++ = ((hexcharval(hex[0])<<4) |
			 hexcharval(hex[1]));
	    hex  += 2;
	    bits += 8;
	    hexbuf_size -= 1;
	}
	if(hexbuf_size>0) binbuf[0] = 0;	// might as well null-terminate if there is room
	return bits;
    }


    u_char *get_digest(){
	if(!digest){
	    int bytes = hexdigest.size()/2;
	    digest = (u_char *)malloc(bytes);
	    digest_bits_ = hex2bin(digest,bytes,hexdigest.c_str());
	}
	return digest;
    }
    /* These parameters are for when the structure is read */
    int digest_bits() {
	if(!digest) get_digest();
	return digest_bits_;
    }
    string digest_name;
    string coding;
    string hexdigest;

    /** Compute the digest from the disk and set all the fields.
     * Return 0 if success, -1 if failure.
     */
    int compute_digest(AFFILE *af,string digestToUse){
	const EVP_MD *md = EVP_get_digestbyname(digestToUse.c_str());
	if(!md) return -1;		// digest not available
	if(af_seek(af,start,0)!=start) return -1; // can't seek
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	EVP_DigestInit(ctx,md);

	uint64_t bytes_read = 0;
	while(bytes_read < this->bytes){
	    u_char buf[65536];
	    int to_read = (this->bytes-bytes_read) < sizeof(buf) ? (this->bytes-bytes_read) : sizeof(buf);
	    if(af_read(af,buf,to_read)!=to_read){
		EVP_MD_CTX_free(ctx);
		return -1; // error reading
	    }
	    /* compute the hash */
	    EVP_DigestUpdate(ctx,buf,to_read);
	    bytes_read += to_read;
	}
	/* Compute the results */
	if(digest!=0) free(digest);
	u_int digest_bytes = 1024;
	digest = (u_char *)malloc(digest_bytes);		// big enough for any conceivable digest
	EVP_DigestFinal(ctx,digest,&digest_bytes);
	EVP_MD_CTX_free(ctx);
	digest_bits_ = digest_bytes*8;
	digest_name  = digestToUse;
	hexdigest    = bin2hex(digest,digest_bits_/8);
	return 0;
    }
    /** Return XML for the digest */
    string toXML(){
	std::stringstream sstart,sbytes;
	sstart << start;
	sbytes << bytes;
	return string("<hash coding='base16' start='") + sstart.str() + "' bytes='" + sbytes.str() +
	    "' alg='"+digest_name+"'>"+hexdigest+"</hash>";
    }
private:;
    u_char *digest;
    u_int  digest_bits_;
};
ostream & operator << (ostream &os, const hashextent &he){
    os << "[" << he.digest_name << " @ " << he.start << "(" << he.bytes << " bytes) " << he.hexdigest << "]";
    return os;
}

bool operator == (const hashextent &h1,const hashextent &h2) {
    return h1.start==h2.start && h1.bytes==h2.bytes && h1.hexdigest==h2.hexdigest;
}

class hashvector:public vector<hashextent>  {
public:
    static int ireverse(int a,int b){
	return a<b ? 1 : -1;
    }

    /**
     * return a list of digests, sorted by inverse bitlength,
     * in the hashvector
     */
    vector<string> digests(){
	vector<int> bits_vector;
	std::set<int> bits_set; 		// why isn't find working on vector? set shouldn't be needed
	map<int,string> bits_to_hash;
	for(hashvector::iterator it = begin();it!=end();it++){
	    (*it).get_digest();		// parse the digest to determine length
	    int bits = (*it).digest_bits();
	    if(bits_set.find(bits)==bits_set.end()){
		bits_set.insert(bits);
		bits_vector.push_back(bits);
		bits_to_hash[bits] = (*it).digest_name;
	    }
	}
	/* Now reverse sort it */
	sort(bits_vector.begin(),bits_vector.end(),ireverse);
	//sort(bits_vector.begin(),bits_vector.end());
	//reverse(bits_vector.begin(),bits_vector.end());
	/* Generate the result */
	vector<string> ret;
	for(vector<int>::const_iterator it = bits_vector.begin();it != bits_vector.end(); it++){
	    ret.push_back(bits_to_hash[*it]);
	}
	return ret;
    }
    /**
     * Return the strongest digest in the hashvector that OpenSSL
     * makes avilable on the runtime system.
     */
    const EVP_MD *strongest_available(){
	vector<std::string> algs = digests();
	for(vector<std::string>::const_iterator it = algs.begin(); it!=algs.end(); it++){
	    const EVP_MD *ret = EVP_get_digestbyname((*it).c_str());
	    if(ret) return ret;
	}
	return 0;			// no digest available
    }
};


#endif
