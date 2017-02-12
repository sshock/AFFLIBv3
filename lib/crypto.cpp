/*
 * This file is a work of a US government employee and as such is in the Public domain.
 * Simson L. Garfinkel, March 12, 2012
 */


#include "affconfig.h"
#include "afflib.h"
#include "afflib_i.h"
#include "utils.h"

#ifdef HAVE_OPENSSL_PEM_H
#include <openssl/pem.h>
#include <openssl/bio.h>
#endif

#ifdef HAVE_STL
#include <vector>
#include <set>
#include <string>
using namespace std;
#endif

#ifdef HAVE_CSTRING
#include <cstring>
#endif

/* Support OpenSSL before 1.1.0 */
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
#define EVP_MD_CTX_new EVP_MD_CTX_create
#define EVP_MD_CTX_free EVP_MD_CTX_destroy
#endif


/****************************************************************
 *** LOW LEVEL ROUTINES
 ****************************************************************/

/**
 * Returns TRUE if the segment named 'buf' has the suffixi indicating
 * that it is an encrypted segment.
 */
int af_is_encrypted_segment(const char *segname){
    if(strcmp(segname,AF_AFFKEY)==0) return 1;
    if(aff::ends_with(segname,AF_AES256_SUFFIX)) return 1;
    if(strncmp(segname,AF_AFFKEY_EVP,strlen(AF_AFFKEY_EVP)-1)==0) return 1;
    return 0;
}

/**
 * Returns TRUE if the segment named 'buf' has the suffix indicating
 * that it is a signature segment.
 *
 * @param segname - segment to check
 */
int af_is_signature_segment(const char *segname){
    int num = 0;
    char cc;
    if(aff::ends_with(segname,AF_SIG256_SUFFIX)) return 1;
    if(sscanf(segname,"affbom%d%c",&num,&cc)==1) return 1; // it's a bom segment
    return 0;
}


/****************************************************************
 *** AES ENCRYPTION LAYER
 ****************************************************************/

static const char *aff_cannot_sign = "AFFLIB: OpenSSL does not have SHA256! "\
    "AFF segments cannot be signed. "\
    "See http://www.afflib.org/requirements.php for additional information.";

void af_crypto_allocate(AFFILE *af)
{
    af->crypto = (struct af_crypto *)calloc(sizeof(struct af_crypto),1); // give space
}


/** compute SHA256.
 * Return 0 if success, -1 if error.
 */
int af_SHA256(const unsigned char *data,size_t datalen,unsigned char md[32])
{
    const EVP_MD *sha256 = EVP_get_digestbyname("SHA256");
    if(!sha256) return -1;

    uint32_t sha256_buflen = 32;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit(ctx,sha256);
    EVP_DigestUpdate(ctx,data,datalen);
    if(EVP_DigestFinal(ctx,md,&sha256_buflen)!=1){ // EVP_DigestFinal returns 1 for success
	EVP_MD_CTX_free(ctx);
	return -1;
    }
    EVP_MD_CTX_free(ctx);
    return 0;
}

void af_crypto_deallocate(AFFILE *af)
{
#ifdef AES_BLOCK_SIZE
    memset(&af->crypto->ekey,0,sizeof(af->crypto->ekey));
    memset(&af->crypto->dkey,0,sizeof(af->crypto->dkey));
#endif
#ifdef HAVE_PEM_READ_BIO_RSA_PUBKEY
    if(af->crypto->sign_privkey){
	EVP_PKEY_free(af->crypto->sign_privkey);
	af->crypto->sign_privkey = 0;
    }
    if(af->crypto->sign_pubkey){
	EVP_PKEY_free(af->crypto->sign_pubkey);
	af->crypto->sign_pubkey = 0;
    }
    if(af->crypto->sign_cert){
	X509_free(af->crypto->sign_cert);
	af->crypto->sign_cert = 0;
    }
#endif
    free(af->crypto);
    af->crypto = 0;
}


int af_set_aes_key(AFFILE *af,const unsigned char *userKey,const int bits)
{
#ifdef HAVE_AES_ENCRYPT
    if(af->crypto->sealing_key_set){
	if(userKey==0){			// key was set and it is being cleared
	    af->crypto->sealing_key_set = 0;
	    return 0;
	}
	return AF_ERROR_KEY_SET;		// key is already set
    }
    int r;
    r = AES_set_encrypt_key(userKey,bits,&af->crypto->ekey);
    if(r) return r;

    r = AES_set_decrypt_key(userKey,bits,&af->crypto->dkey);
    if(r) return r;

    af->crypto->sealing_key_set = 1;
    af->crypto->auto_encrypt = 1;	// default
    af->crypto->auto_decrypt = 1;	// default
    af_invalidate_vni_cache(af);	// invalidate the cache, because now we can read encrypted values
    return 0;
#else
    return AF_ERROR_NO_AES;
#endif
}



/**
 * Take an unencrypted AFFKEY, encrypt it with the SHA256 of the passphrase,
 * and save it in the appropriate segment.
 */

int af_save_aes_key_with_passphrase(AFFILE *af,const char *passphrase, const u_char affkey[32])
{
#if defined(HAVE_AES_ENCRYPT)
    if(af->crypto->sealing_key_set) return AF_ERROR_KEY_SET;		// already enabled

    /* Make an encrypted copy of the AFFkey */
    unsigned char passphrase_hash[32];
    af_SHA256((const unsigned char *)passphrase, strlen(passphrase), passphrase_hash);

    struct affkey affkey_seg;
    assert(sizeof(affkey_seg)==AFFKEY_SIZE);
    memset((unsigned char *)&affkey_seg,0,sizeof(affkey_seg));

    uint32_t version_number = htonl(1);	// version 1
    memcpy(affkey_seg.version,(u_char *)&version_number,4);
    memcpy(affkey_seg.affkey_aes256,affkey,32);

    /* Use the hash to encrypt the key and all zeros */
    AES_KEY ekey;
    AES_set_encrypt_key(passphrase_hash,256,&ekey);
    AES_encrypt(affkey_seg.affkey_aes256,
		affkey_seg.affkey_aes256,&ekey);
    AES_encrypt(affkey_seg.affkey_aes256+AES_BLOCK_SIZE,
		affkey_seg.affkey_aes256+AES_BLOCK_SIZE,&ekey);
    AES_encrypt(affkey_seg.zeros_aes256,affkey_seg.zeros_aes256,&ekey);

    /* Write this to a segment */
    if(af_update_seg(af,AF_AFFKEY,0,(const u_char *)&affkey_seg,sizeof(affkey_seg))) return -1;
    memset((unsigned char *)&affkey_seg,0,sizeof(affkey_seg)); // erase the temp data
    return 0;
#endif
#if !defined(HAVE_AES_ENCRYPT)
    return AF_ERROR_NO_AES;
#endif
}

/** MacOS 10.5 with GCC 4.0.1 packed affkey at 52 bytes.
 ** Linux GCC 4.1.2 packed affkey at 56 bytes. It should be 52 bytes
 ** --- 4 bytes for the version number, 32 bytes for the affkey, 16 bytes for encryption of zeros.
 ** original code specified the version as uint32_t version:32, for which the
 ** compiler allocated 64 bits...
 ** So this code needs to be willing to accept a 52-byte or 56-byte affkey.
 **/
/* Legacy - this version of the structure was improperly used in AFFLIB prior to
 * 3.1.6. Unfortunately, the structure didn't pack properly, resulting in some images
 * in which the affkey structure was too large.
 */
struct affkey_legacy {
    uint32_t version:32;
    u_char affkey_aes256[32]; // AFF key encrypted with SHA-256 of passphrase
                              // encrypted as two codebooks in a row; no need for CBC
    u_char zeros_aes256[16];  // all zeros encrypted with SHA-256 of passphrase
};

int  af_get_aes_key_from_passphrase(AFFILE *af,const char *passphrase,
					   unsigned char affkey[32])
{
#if defined(HAVE_AES_ENCRYPT)
    if(af->crypto->sealing_key_set) return AF_ERROR_KEY_SET;		// already enabled

    /* Get the segment with the key in it. It should be AFFKEY_SIZE
     * but there are a few images out there with the wrong key size due
     * to a compiler packing bug. Automatically handle those.
     */
    struct affkey affkey_seg;		// in-memory copy
    u_char kbuf[1024];
    size_t klen=sizeof(kbuf);
    uint32_t version;
    int kversion=0;

    /* Try to get the segment */
    if(af_get_seg(af,AF_AFFKEY,0,kbuf,&klen)) return AF_ERROR_AFFKEY_NOT_EXIST;

    if(sizeof(affkey_seg)==klen){
	// On-disk structure is correct; copy it over
	memcpy(&affkey_seg,kbuf,klen);
	memcpy((char *)&version,affkey_seg.version,4);
	kversion = ntohl(version);
    } else {
	// Try to figure it out manually
	memcpy((char *)&version,kbuf,4);
	kversion = ntohl(version);
	memcpy(affkey_seg.affkey_aes256,kbuf+4,sizeof(affkey_seg.affkey_aes256));
	memcpy(affkey_seg.zeros_aes256,kbuf+36,sizeof(affkey_seg.zeros_aes256));
    }

    /* make sure version is correct */
    if(kversion != 1){
	errno = EINVAL;
	return AF_ERROR_AFFKEY_WRONG_VERSION;
    }

    /* hash the passphrase */
    unsigned char passphrase_hash[32];
    if(af_SHA256((const unsigned char *)passphrase,strlen(passphrase), passphrase_hash)){
	return AF_ERROR_NO_SHA256;
    }

    /* Try to decrypt the key */

    AES_KEY dkey;
    AES_set_decrypt_key(passphrase_hash,256,&dkey);
    AES_decrypt(affkey_seg.affkey_aes256,
		affkey_seg.affkey_aes256,&dkey);
    AES_decrypt(affkey_seg.affkey_aes256+AES_BLOCK_SIZE,
		affkey_seg.affkey_aes256+AES_BLOCK_SIZE,&dkey);
    AES_decrypt(affkey_seg.zeros_aes256,affkey_seg.zeros_aes256,&dkey);

    /* See if its zero? */
    for(u_int i=0;i<sizeof(affkey_seg.zeros_aes256);i++){
	if(affkey_seg.zeros_aes256[i]) return AF_ERROR_WRONG_PASSPHRASE;
    }


    memcpy(affkey,affkey_seg.affkey_aes256,32);    /* copy out the result */
    memset((unsigned char *)&affkey_seg,0,sizeof(affkey_seg)); // erase the temp data
    return 0;
#endif
#if !defined(HAVE_AES_ENCRYPT)
    return AF_ERROR_NO_AES;
#endif
}

/**
 * make a random affkey and encrypt it with passphrase.
 */
int  af_establish_aes_passphrase(AFFILE *af,const char *passphrase)
{
#ifdef HAVE_AES_ENCRYPT
    if(af->crypto->sealing_key_set) return AF_ERROR_KEY_SET;		// already enabled

    /* Can only establish a passphrase if the encryption segment doesn't exist */
    if(af_get_seg(af,AF_AFFKEY,0,0,0)==0) return AF_ERROR_AFFKEY_EXISTS;

    /* Check to make sure it wasn't public key encrypted */
    char segname[AF_MAX_NAME_LEN];
    snprintf(segname,sizeof(segname),AF_AFFKEY_EVP,0);
    if(af_get_seg(af,segname,0,0,0)==0) return AF_ERROR_AFFKEY_EXISTS;

    /* Okay; make a random key and encrypt it with the passphrase */
    unsigned char affkey[32];
    int r = RAND_bytes(affkey,sizeof(affkey)); // makes a random key; with REAL random bytes
    if(r!=1) return AF_ERROR_RNG_FAIL; // pretty bad...

    /* I have the key, now save it */
    r = af_save_aes_key_with_passphrase(af,passphrase,affkey);
    memset(affkey,0,sizeof(affkey)); /* Erase the encryption key in memory */
    return r;
#else
    return AF_ERROR_NO_AES;
#endif
}


/** Like the one above, this public interface actually wipes the key after it is created.
 * @param passphrase - Passphrae, use NULL to erase the encryption key.
 *                     This can only be done if the file is opened read-only.
 */
int  af_use_aes_passphrase(AFFILE *af,const char *passphrase)
{
    af_invalidate_vni_cache(af);
    if(passphrase==0 && !(af->openflags & O_RDWR)){
	af->crypto->sealing_key_set = 0;
	return 0;
    }

    if(af->crypto->sealing_key_set) return AF_ERROR_KEY_SET;		// already enabled

    unsigned char affkey[32];
    int r = af_get_aes_key_from_passphrase(af,passphrase,affkey);
    if(r) return r;			  // wrong keyphrase
    r = af_set_aes_key(af,affkey,256);    /* Set the encryption key */
    memset(affkey,0,sizeof(affkey)); /* Erase the encryption key in memory */
    return r;
}


/* gets the key with the old phrase and then changes it to the new one */
int  af_change_aes_passphrase(AFFILE *af,const char *oldphrase,const char *newphrase)
{
    if(af->crypto->sealing_key_set) return AF_ERROR_KEY_SET;		// already enabled

    unsigned char affkey[32];
    int r = af_get_aes_key_from_passphrase(af,oldphrase,affkey);

    if(r) return r;
    r = af_save_aes_key_with_passphrase(af,newphrase,affkey);
    memset(affkey,0,sizeof(affkey));	// erase the temp data
    return r;
}


int af_has_encrypted_segments(AFFILE *af)
{
    struct af_vnode_info vni;
    af_vstat(af,&vni);
    return vni.segment_count_encrypted>0;
}

/**
 * Returns true if there are segments that cannot be decrypted
 * (other than key segments)
 */
int af_cannot_decrypt(AFFILE *af){
    if(af_has_encrypted_segments(af)==0) return 0; // no encrypted segments to decrypt
    /* Now start at the beginning and see if any segments are read which are encrypted.
     * If they are encrypted, then we don't have the encryption key.
     */
    if(af_rewind_seg(af)) return -1;
    char segname[AF_MAX_NAME_LEN];
    memset(segname,0,sizeof(segname));
    while(af_get_next_seg(af,segname,sizeof(segname),0,0,0)==0){
	if(aff::ends_with(segname,AF_AES256_SUFFIX)) return 1; // we shouldn't see these.
    }
    return 0;
}

/****************************************************************
 ***
 *** Signature Routines
 ***
 ****************************************************************/

/** See if the public key and private key match by dial a trial encryption and decryption.
 *
 * @param pubkey
 * @param privkey
 * @returns 0 if successful, -1 if failure.
 */
static int check_keys(EVP_PKEY *privkey,EVP_PKEY *pubkey)
{
    char ptext[16];			/* plaintext of a 128-bit message */
    unsigned char sig[1024];		/* signature; bigger than needed */
    uint32_t siglen = sizeof(sig);	/* length of signature */

    const EVP_MD *sha256 = EVP_get_digestbyname("SHA256");
    if(!sha256) return -1;		// no SHA256.

    EVP_MD_CTX *md = EVP_MD_CTX_new();	/* EVP message digest */

    /* make the plaintext message */
    memset(ptext,0,sizeof(ptext));
    strcpy(ptext,"Test Message");
    EVP_SignInit(md,sha256);
    EVP_SignUpdate(md,ptext,sizeof(ptext));
    EVP_SignFinal(md,sig,&siglen,privkey);

    /* Verify the message */
    EVP_VerifyInit(md,sha256);
    EVP_VerifyUpdate(md,ptext,sizeof(ptext));
    if(EVP_VerifyFinal(md,sig,siglen,pubkey)!=1){
	EVP_MD_CTX_free(md);
	return -3;
    }
    EVP_MD_CTX_free(md);
    return 0;
}


/**
 * af_set_sign_files:
 *
 * Load the private key & certificate, make sure they are matched, and
 * write to the AFF.  This requirest not just AES256, but EVP_SHA256
 * because we use the openSSL signature functions.
 *
 * @param af - The open AFFILE
 * @param keyfile - The filename of the key file to read
 * @param certfile - The filename of the certificate file to read
 */


int  af_set_sign_files(AFFILE *af,const char *keyfile,const char *certfile)
{
    const EVP_MD *sha256 = EVP_get_digestbyname("SHA256");
    if(!sha256){
	(*af->error_reporter)(aff_cannot_sign);
	return AF_ERROR_NO_SHA256;			//
    }

    BIO *bp = BIO_new_file(keyfile,"r");
    if(!bp) return -1;
    af->crypto->sign_privkey = PEM_read_bio_PrivateKey(bp,0,0,NULL);
    BIO_free(bp);
    if(!af->crypto->sign_privkey) return -2;	// can't decode keyfile

    bp = BIO_new_file(certfile,"r");
    if(!bp) return -1;
    PEM_read_bio_X509(bp,&af->crypto->sign_cert,0,0);
    if(af->crypto->sign_cert==0){
	EVP_PKEY_free(af->crypto->sign_privkey);
	af->crypto->sign_privkey = 0;
	return -3;
    }
    af->crypto->sign_pubkey = X509_get_pubkey(af->crypto->sign_cert);
    BIO_free(bp);

    if(check_keys(af->crypto->sign_privkey,af->crypto->sign_pubkey)){
	/* private key doesn't match certificate */
	EVP_PKEY_free(af->crypto->sign_privkey); af->crypto->sign_privkey = 0;
	EVP_PKEY_free(af->crypto->sign_pubkey);  af->crypto->sign_pubkey = 0;
	return -4;
    }

    /* Looks good; save the cert in a segment */
    BIO *xbp = BIO_new(BIO_s_mem());	// where we are writing
    PEM_write_bio_X509(xbp,af->crypto->sign_cert);
    af_update_seg_frombio(af,AF_SIGN256_CERT,0,xbp);
    BIO_free(xbp);
    return 0;
}

/* Sign the segment with the signing key.  Signatures are calculated
 * by taking the SHA256 of the following concatenated together:
 * segment name
 * segment arg (in network byte order)
 * segment data
 */
int af_sign_seg3(AFFILE *af,const char *segname,
		 uint32_t arg,const unsigned char *data,uint32_t datalen,
		 uint32_t signmode)
{
    const EVP_MD *sha256 = EVP_get_digestbyname("SHA256");
    if(!sha256){
	(*af->error_reporter)(aff_cannot_sign);
	return AF_ERROR_NO_SHA256;			//
    }


    if(af->crypto->sign_privkey==0) return -1;		// can't sign; no signing key

    if(strlen(segname)+strlen(AF_SIG256_SUFFIX)+1 > AF_MAX_NAME_LEN) return -1;	// too long

    char signed_segname[AF_MAX_NAME_LEN];
    strlcpy(signed_segname,segname,AF_MAX_NAME_LEN);
    strlcat(signed_segname,AF_SIG256_SUFFIX,AF_MAX_NAME_LEN);

    if(signmode==AF_SIGNATURE_DELETE){
	af_del_seg(af,signed_segname);
	return 0;
    }

    uint32_t arg_net = htonl(arg);
    unsigned char sig[1024];		/* signature; bigger than needed */
    uint32_t siglen = sizeof(sig);	/* length of signature */

    EVP_MD_CTX *md = EVP_MD_CTX_new();	/* EVP message digest */
    EVP_SignInit(md,sha256);
    EVP_SignUpdate(md,(const unsigned char *)segname,strlen(segname)+1);
    EVP_SignUpdate(md,(const unsigned char *)&arg_net,sizeof(arg_net));
    EVP_SignUpdate(md,data,datalen);
    EVP_SignFinal(md,sig,&siglen,af->crypto->sign_privkey);
    EVP_MD_CTX_free(md);
    return (*af->v->update_seg)(af,signed_segname,signmode,sig,siglen);
}


int af_sign_seg(AFFILE *af,const char *segname)
{
    size_t datalen = 0;

    /* Now get the data to verify */
    if(af_get_seg(af,segname,0,0,&datalen)){
	return AF_ERROR_SIG_DATAREAD_ERROR; // can't read the segment length
    }

    /* Now read the segment */
    unsigned char *data=(unsigned char *)malloc(datalen);
    if(data==0) return AF_ERROR_SIG_MALLOC;

    uint32_t arg=0;
    if(af_get_seg(af,segname,&arg,data,&datalen)){
	free(data);
	return AF_ERROR_SIG_DATAREAD_ERROR; // can't read the segment length
    }

    /* Note: it woudl be wrong to detect pages and sign them in mode1, because we don't really
     * have access to the uncompressed data...
     */
    int r = af_sign_seg3(af,segname,arg,data,datalen,AF_SIGNATURE_MODE0);
    free(data);
    return r;
}


#ifdef HAVE_STL
/** Returns number of segments that were signed.
 * Returns -1 if there is an error.
 */
int af_sign_all_unsigned_segments(AFFILE *af)
{
    vector<string> segs;
    set<string>sigs;
    char name[AF_MAX_NAME_LEN];
    int count=0;

    /* Get a list of all the segments and all the signatures */
    if(af_rewind_seg(af)) return -1;
    while(af_get_next_seg(af,name,sizeof(name),0,0,0)==0){
	if(name[0]==0) continue;	// don't sign the empty segments
	if(aff::ends_with(name,AF_SIG256_SUFFIX)==0){
	    segs.push_back(name);
	}
	else{
	    sigs.insert(name);
	}
    }
    /* Sign the ones that are unsigned. */
    for(vector<string>::const_iterator s = segs.begin();
	s != segs.end();
	s++){
	/* Compute name of the signature */
	string signame = *s + AF_SIG256_SUFFIX;
	if(sigs.find(signame) == sigs.end()){
	    if(af_sign_seg(af,s->c_str())){
		(*af->error_reporter)("AFFLIB: Could not sign segment '%s'",s->c_str());
		return -1;
	    }
	    count++;
	}
    }
    return count;
}
#endif

/* Verify a segment against a particular signature and public key */
int af_hash_verify_seg2(AFFILE *af,const char *segname,u_char *sigbuf_,size_t sigbuf_len_,int sigmode)
{
    const EVP_MD *sha256 = EVP_get_digestbyname("SHA256");
    if(!sha256){
	(*af->error_reporter)(aff_cannot_sign);
	return AF_ERROR_NO_SHA256;			//
    }

    /* Now get the data to verify */
    size_t seglen = 0;
    unsigned char *segbuf = 0;
    uint32_t arg=0;

    /* Do we need to get the page */
    if(sigmode==AF_SIGNATURE_MODE1){
	int64_t pagenumber = af_segname_page_number(segname);
	if(pagenumber>=0){
	    seglen = af_page_size(af);
	    segbuf    = (unsigned char *)malloc(seglen);
	    if(segbuf==0) return AF_ERROR_SIG_MALLOC;
	    if(af_get_page(af,pagenumber,segbuf,&seglen)){
		free(segbuf);
		return -1;
	    }
	}
    }
    if(segbuf==0){			// get the raw segment
	if(af_get_seg(af,segname,0,0,&seglen)){
	    return AF_ERROR_SIG_DATAREAD_ERROR; // can't read the segment length
	}

	/* Now read the segment */
	segbuf=(unsigned char *)malloc(seglen);
	if(segbuf==0) return AF_ERROR_SIG_MALLOC;

	if(af_get_seg(af,segname,&arg,segbuf,&seglen)){
	    free(segbuf);
	    return AF_ERROR_SIG_DATAREAD_ERROR; // can't read the segment length
	}
    }

    /* Verify the signature*/
    uint8_t sigbuf[1024];
    uint32_t sigbuf_len = sizeof(sigbuf);
    uint32_t arg_net = htonl(arg);
    EVP_MD_CTX *md = EVP_MD_CTX_new();	/* EVP message digest */
    EVP_DigestInit(md,sha256);
    EVP_DigestUpdate(md,(const unsigned char *)segname,strlen(segname)+1);
    EVP_DigestUpdate(md,(const unsigned char *)&arg_net,sizeof(arg_net));
    EVP_DigestUpdate(md,segbuf,seglen);
    EVP_DigestFinal(md,sigbuf,&sigbuf_len);
    EVP_MD_CTX_free(md);
    int r = memcmp(sigbuf,sigbuf_,sigbuf_len);
    if(sigbuf_len != sigbuf_len_) r = -1; // doesn't match
    free(segbuf);

    if(r==0) return 0;			// verifies
    return AF_ERROR_SIG_BAD;		// doesn't verify
}

/* Verify a segment against a particular signature and public key */
int af_sig_verify_seg2(AFFILE *af,const char *segname,EVP_PKEY * /*pubkey*/,u_char *sigbuf,size_t sigbuf_len,int sigmode)
{
    const EVP_MD *sha256 = EVP_get_digestbyname("SHA256");
    if(!sha256){
	(*af->error_reporter)(aff_cannot_sign);
	return AF_ERROR_NO_SHA256;			//
    }


    /* Now get the data to verify */
    size_t seglen = 0;
    unsigned char *segbuf = 0;
    uint32_t arg=0;

    /* Do we need to get the page */
    if(sigmode==AF_SIGNATURE_MODE1){
	int64_t pagenumber = af_segname_page_number(segname);
	if(pagenumber>=0){
	    seglen = af_page_size(af);
	    segbuf    = (unsigned char *)malloc(seglen);
	    if(segbuf==0) return AF_ERROR_SIG_MALLOC;
	    if(af_get_page(af,pagenumber,segbuf,&seglen)){
		free(segbuf);
		return -1;
	    }
	}
    }
    if(segbuf==0){			// get the raw segment
	if(af_get_seg(af,segname,0,0,&seglen)){
	    return AF_ERROR_SIG_DATAREAD_ERROR; // can't read the segment length
	}

	/* Now read the segment */
	segbuf=(unsigned char *)malloc(seglen);
	if(segbuf==0) return AF_ERROR_SIG_MALLOC;

	if(af_get_seg(af,segname,&arg,segbuf,&seglen)){
	    free(segbuf);
	    return AF_ERROR_SIG_DATAREAD_ERROR; // can't read the segment length
	}
    }

    /* Verify the signature*/
    uint32_t arg_net = htonl(arg);
    EVP_MD_CTX *md = EVP_MD_CTX_new();	/* EVP message digest */
    EVP_VerifyInit(md,sha256);
    EVP_VerifyUpdate(md,(const unsigned char *)segname,strlen(segname)+1);
    EVP_VerifyUpdate(md,(const unsigned char *)&arg_net,sizeof(arg_net));
    EVP_VerifyUpdate(md,segbuf,seglen);
    EVP_MD_CTX_free(md);
    int r = EVP_VerifyFinal(md,sigbuf,sigbuf_len,af->crypto->sign_pubkey);
    free(segbuf);

    if(r==1) return 0;			// verifies
    return AF_ERROR_SIG_BAD;		// doesn't verify
}



int af_sig_verify_seg(AFFILE *af,const char *segname)
{
#ifdef USE_AFFSIGS
    if(aff::ends_with(segname,AF_SIG256_SUFFIX)){
	return AF_ERROR_SIG_SIG_SEG; 			// don't verify the signature segments
    }

    /* Need the public key if I don't have it */
    if(af->crypto->sign_pubkey==0){
	unsigned char certbuf[65536];
	size_t certbuf_len = sizeof(certbuf);
	if(af_get_seg(af,AF_SIGN256_CERT,0,certbuf,&certbuf_len)!=0){
	    return AF_ERROR_SIG_NO_CERT;
	}
	af->crypto->sign_cert = 0;
	BIO *cert_bio = BIO_new_mem_buf(certbuf,certbuf_len);
	PEM_read_bio_X509(cert_bio,&af->crypto->sign_cert,0,0);
	BIO_free(cert_bio);
	af->crypto->sign_pubkey = X509_get_pubkey(af->crypto->sign_cert);
    }

    /* Figure out the signature segment name */
    char sigseg[AF_MAX_NAME_LEN + 1 + sizeof(AF_SIG256_SUFFIX)];
    strlcpy(sigseg,segname,sizeof(sigseg));
    strlcat(sigseg,AF_SIG256_SUFFIX,sizeof(sigseg));

    /* Get the signature (it says how we need to handle the data) */
    unsigned char sigbuf[2048];		// big enough to hold any conceivable signature
    size_t sigbuf_len=sizeof(sigbuf);
    uint32_t sigmode=0;
    if(af_get_seg(af,sigseg,&sigmode,sigbuf,&sigbuf_len)){
	return AF_ERROR_SIG_READ_ERROR;
    }

    return af_sig_verify_seg2(af,segname,af->crypto->sign_pubkey,sigbuf,sigbuf_len,sigmode);
#else
    return AF_ERROR_SIG_NOT_COMPILED;				// sig support not compiled in
#endif
}

/****************************************************************
 *** PUBLIC KEY ENCRYPION ROUTINES
 ****************************************************************/

/**
 * af_set_seal_certfiles
 *
 * Specifies the certific file(s) to use for creating a new affkey.
 * If an affkey is already on the disk, this returns with an error.
 *
 * @param af - The open AFFILE
 * @param certfile - The filename of the certificate file to read
 */

int  af_set_seal_certificates(AFFILE *af,const char *certfiles[],int numcertfiles)
{
    const EVP_MD *sha256 = EVP_get_digestbyname("SHA256");
    if(!sha256){
	(*af->error_reporter)(aff_cannot_sign);
	return AF_ERROR_NO_SHA256;			//
    }

    char evp0[AF_MAX_NAME_LEN];		// segment where we will store the encrypted session key
    snprintf(evp0,sizeof(evp0),AF_AFFKEY_EVP,0);

    /* If an affkey has not been created, create one if there is a public key(s)...
     * todo: this should probably see if there is ANY evp segment.
     */
    if(af_get_seg(af,evp0,0,0,0)==0) return -1; // make sure no encrypted EVP exists
    if(af_get_seg(af,AF_AFFKEY,0,0,0)==0) return -1; // make sure no passphrase exists
    if(certfiles==0 || numcertfiles==0) return -1;   // make sure the user supplied a certificate

    /* First make the affkey */
    unsigned char affkey[32];
    int r = RAND_bytes(affkey,sizeof(affkey));
    if(r!=1) return AF_ERROR_RNG_FAIL; // pretty bad...

    af_seal_affkey_using_certificates(af, certfiles, numcertfiles, affkey);
    return 0;
}

/**
 * af_seal_affkey_using_certificates
 *
 * Encrypt the provided affkey.
 *
 *
 */

int  af_seal_affkey_using_certificates(AFFILE *af,const char *certfiles[],int numcertfiles, unsigned char affkey[32])
{
    /* Repeat for each public key.. */
    int r;
    for(int i=0;i<numcertfiles;i++){

	EVP_PKEY	*seal_pubkey=0;		// encrypting public key (for encrypting the affkey)
	X509	*seal_cert=0;		// encrypting certificate that was used...

	BIO *bp = BIO_new_file(certfiles[i],"r");
	if(!bp) return -1;
	PEM_read_bio_X509(bp,&seal_cert,0,0);
	BIO_free(bp);
	if(seal_cert==0){
	    return -2;
	}
	seal_pubkey = X509_get_pubkey(seal_cert);

	/* Create the next encrypted key. First make a copy of it... */
	unsigned char affkey_copy[32];
	memcpy(affkey_copy,affkey,32);

	EVP_CIPHER_CTX *cipher_ctx = EVP_CIPHER_CTX_new();

	/* IV */
	unsigned char iv[EVP_MAX_IV_LENGTH];
	r = RAND_bytes(iv, EVP_MAX_IV_LENGTH); /* make a random iv */
	if(r!=1) return AF_ERROR_RNG_FAIL; // pretty bad...

	/* EK */
	unsigned char *ek=0;
	unsigned char *ek_array[1];

	int ek_size = EVP_PKEY_size(seal_pubkey);
	ek = (unsigned char *)malloc(ek_size);
	ek_array[0] = ek;

	/* Destination for encrypted AFF key */
	unsigned char encrypted_affkey[1024];
	int encrypted_bytes = 0;
	memset(encrypted_affkey,0,sizeof(encrypted_affkey));

	r = EVP_SealInit(cipher_ctx,EVP_aes_256_cbc(),ek_array,&ek_size,&iv[0],&seal_pubkey,1);
	if(r!=1){
	    EVP_CIPHER_CTX_free(cipher_ctx);
	    return -10;		// bad
	}

	r = EVP_SealUpdate(cipher_ctx,encrypted_affkey,&encrypted_bytes,affkey_copy,sizeof(affkey_copy));
	if(r!=1){
	    EVP_CIPHER_CTX_free(cipher_ctx);
	    return -11;		// bad
	}

	int total_encrypted_bytes = encrypted_bytes;
	r = EVP_SealFinal(cipher_ctx,encrypted_affkey+total_encrypted_bytes,&encrypted_bytes);
	if(r!=1){
	    EVP_CIPHER_CTX_free(cipher_ctx);
	    return -12;
	}

	EVP_CIPHER_CTX_free(cipher_ctx);

	total_encrypted_bytes += encrypted_bytes;

	/* Now we need to combine the IV, encrypted key, and the encrypted aff key onto a single structure
	 * and write it out
	 */
	const int int1 = sizeof(int)*1;
	const int int2 = sizeof(int)*2;
	const int int3 = sizeof(int)*3;
	const int buflen = int3+EVP_MAX_IV_LENGTH+ek_size+total_encrypted_bytes;
	unsigned char *buf = (unsigned char *)malloc(buflen);
	*(u_int *)(buf)      = htonl(1); // version 1.0
	*(u_int *)(buf+int1) = htonl(ek_size);
	*(u_int *)(buf+int2) = htonl(total_encrypted_bytes);
	memcpy(buf+int3,iv,EVP_MAX_IV_LENGTH);
	memcpy(buf+int3+EVP_MAX_IV_LENGTH,ek,ek_size);
	memcpy(buf+int3+EVP_MAX_IV_LENGTH+ek_size,encrypted_affkey,total_encrypted_bytes);

	/* Write this into the seg */
	char segname[AF_MAX_NAME_LEN];
	snprintf(segname,sizeof(segname),AF_AFFKEY_EVP,i);
	if(af_update_segf(af,segname,0,buf,buflen,AF_SIGFLAG_NOSEAL)){
	    return -1;		// update seg failed?
	}
	EVP_PKEY_free(seal_pubkey);
	seal_pubkey = 0;
	memset(affkey_copy,0,sizeof(affkey_copy)); // overwrite
	memset(buf,0,buflen);	// overwrite
	free(buf);
    }
    /* Start using this key */
    if(af_set_aes_key(af,affkey,256)) return -100; // hm. That's weird.
    return 0;				       // good to go
}



/**
 * Given a private key in a file:
 *  1 - Scan all of the encrypted AFFKEYs to see if any can be decrypted.
 *  2 - When the one is found that can be decrypted, put the AFFKEY in a buffer
 *  3 - Return that buffer.
 *
 * @param af The open AFFILE
 * @param private_keyfile  - The filename of the key file to read
 * @param affkey - The decrypted AFFkey (output)
 *
 * Load the private and/or public key files.
 * Try to decrypt the affkey with the private key.p
 *
 */

int af_get_affkey_using_keyfile(AFFILE *af, const char *private_keyfile,u_char affkey[32])
{
    if(!private_keyfile) return -1;
    BIO *bp = BIO_new_file(private_keyfile,"r");
    if(!bp) return -2;
    EVP_PKEY *seal_privkey = PEM_read_bio_PrivateKey(bp,0,0,0);
    BIO_free(bp);
    if(!seal_privkey) return -3;

    int i = 0;
    int ret = -1;			// return code; set to 0 when successful
    while(i<1000 && ret!=0){ // hopefully there aren't more than 1000 keys...
	char segname[AF_MAX_NAME_LEN];

	sprintf(segname,AF_AFFKEY_EVP,i++);
	size_t buflen=0;
	if(af_get_seg(af,segname,0,0,&buflen)){
	    return -1;		// guess none of the keys work
	}
	unsigned char *buf = (unsigned char *)malloc(buflen);
	if(buf==0) return -1;		// malloc failed
	if(af_get_seg(af,segname,0,buf,&buflen)){
	    free(buf);
	    return -1;		// could not get the segment
	}

	/* Try to get and decrypt the segment */
	unsigned char *decrypted = 0;	//
	if (*(u_int *)buf == htonl(1)){	// check to see if the encrypted EVP is rev 1
	    /* Handle rev 1 */
	    const u_int int1 = sizeof(int)*1; // offset #1
	    const u_int int2 = sizeof(int)*2; // offset #2
	    const u_int int3 = sizeof(int)*3; // offset #3
	    int ek_size               = ntohl(*(u_int *)(buf+int1));
	    int total_encrypted_bytes = ntohl(*(u_int *)(buf+int2));
	    if(int3+EVP_MAX_IV_LENGTH+ek_size+total_encrypted_bytes != buflen){
		goto next;
	    }
	    unsigned char *iv = buf+int3;
	    unsigned char *ek = buf+int3+EVP_MAX_IV_LENGTH;
	    unsigned char *encrypted_affkey = buf+int3+EVP_MAX_IV_LENGTH+ek_size;

	    /* Now let's see if we can decode it*/
	    EVP_CIPHER_CTX *cipher_ctx = EVP_CIPHER_CTX_new();
	    int r = EVP_OpenInit(cipher_ctx,EVP_aes_256_cbc(),ek,ek_size,iv,seal_privkey);
	    if(r==1){
		/* allocate a buffer for the decrypted data */
		decrypted = (unsigned char *)malloc(total_encrypted_bytes);
		if(!decrypted){
		    EVP_CIPHER_CTX_free(cipher_ctx);
		    return -1; // shouldn't fail
		}

		int decrypted_len;
		r = EVP_OpenUpdate(cipher_ctx,decrypted,&decrypted_len,encrypted_affkey,total_encrypted_bytes);
		if(r==1){
		    /* OpenSSL requires that we call EVP_OpenFinal to finish the decryption */
		    unsigned char *decrypted2 = decrypted+decrypted_len; // where the decryption continues
		    int decrypted2_len = 0;
		    r = EVP_OpenFinal(cipher_ctx,decrypted2,&decrypted2_len);
		    if(r==1){
			memcpy(affkey,decrypted,32);
			ret = 0;		// successful return
		    }
		}
		memset(decrypted,0,total_encrypted_bytes); // overwrite our temp buffer
		free(decrypted);
	    }
	    EVP_CIPHER_CTX_free(cipher_ctx);
	}
    next:;
	free(buf);
    }
    return ret;				// return the code
}


/**
 *
 * Given a private key in a file:
 *  1 - Scan all of the encrypted AFFKEYs to see if any can be decrypted.
 *  2 - When the one is found that can be decrypted, put the AFFKEY in a buffer
 *  3 - Set that buffer to be the active AFFKEY so that the AFF file can be read and written.
 *
 * @param af - The open AFFILE
 * @param private_keyfile  - The filename of the key file to read
 * @param certfile - The filename of the certificate file to read
 */

int  af_set_unseal_keyfile(AFFILE *af,const char *private_keyfile)
{
    u_char affkey[32];			// place to put the decrypted affkey
    if(af_get_affkey_using_keyfile(af,private_keyfile,affkey)){
	return -1;			// couldn't get the affkey
    }
    /* It decrypted. Looks like we got an AFF key */
    return af_set_aes_key(af,affkey,256);
}
