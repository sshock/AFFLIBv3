/*
 * utils.h:
 * Some useful utilities for building AFF-aware programs.
 * Distributed under the Berkeley 4-part license
 */

#ifndef AFF_UTILS_H
#define AFF_UTILS_H

#ifdef __cplusplus
#ifdef HAVE_STL
#include <algorithm>
#include <cstdlib>
#include <vector>
#include <string>
#include <cstring>
#include <map>
#include <iostream>
#endif

#ifdef HAVE_OPENSSL_PEM_H
#include <openssl/x509.h>
#include <openssl/pem.h>
#else
typedef void X509;
typedef void EVP_PKEY;
typedef void BIO;
#define BIO_free free
#endif

namespace aff {

#ifdef HAVE_STL
    std::string command_line(int argc,char **argv);
    bool ends_with(std::string str,std::string ending);
#endif
    bool ends_with(const char *buf,const char *with);

    /* Structure for hash map */
    struct less_c_str
    {
	inline bool operator()( const char* x, const char* y) const
	{     return ( strcmp( x,y ) < 0 );
	}
    };

    struct md5blob {
	unsigned char buf[16];
    };

#ifdef HAVE_STL
    typedef std::map< const char*, struct md5blob, less_c_str > hashMapT;

    /* The seginfo stores information about a segment other than its data*/
    class seginfo {
    public:
	seginfo(std::string n1,size_t l1,unsigned int a1): name(n1),len(l1),arg(a1) {}
	std::string name;
	size_t len;
	unsigned long arg;
	/** pagenumber returns <0 for invalid pages, >= for a page */
	int64_t pagenumber() const {return af_segname_page_number(name.c_str());}
	bool inline operator==(const class seginfo &b) const {
	    return name == b.name;
	}
	virtual ~seginfo(){};
    };

    /* the seglist provides AFF internal functions and tools an easy way to get
     * a list of all of the segments in the currently open AFF file.
     * Use the seglist(af) constructor to populate it with all the segments
     * when you create. Each element is populated with the name, length and arg.
     */
    class seglist : public std::vector<seginfo> {
    public:
	bool contains(std::string segname);
	bool has_signed_segments();
	int get_seglist(AFFILE *af);
	seglist(){}
	virtual ~seglist(){}
	seglist(AFFILE *af){
	    get_seglist(af);
	}
    };
#endif
}
#endif

#endif
