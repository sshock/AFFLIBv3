#include <windows.h>

#define PACKAGE_VERSION "3.7.16"

#undef USE_LIBEWF
#undef USE_QEMU
#undef USE_FUSE
#undef USE_S3

#define HAVE_AES_ENCRYPT 1
#define HAVE_ASSERT_H 1
#define HAVE_CSTRING 1
#define HAVE_CTYPE_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_ISDIGIT 1
#define HAVE_LIBCRYPTO 1
#define HAVE_MALLOC_H 1
#define HAVE_MD5 1
#define HAVE_OPENSSL_AES_H 1
#define HAVE_OPENSSL_BIO_H 1
#define HAVE_OPENSSL_EVP_H 1
#define HAVE_OPENSSL_HMAC_H 1
#define HAVE_OPENSSL_MD5_H 1
#define HAVE_OPENSSL_PEM_H 1
#define HAVE_OPENSSL_RAND_H 1
#define HAVE_OPENSSL_RSA_H 1
#define HAVE_OPENSSL_SHA_H 1
#define HAVE_OPENSSL_X509_H 1
#define HAVE_PEM_READ_BIO_RSA_PUBKEY 1
#define HAVE_SHA1 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_ZLIB_H 1

#if _MSC_VER >= 1600
#define HAVE_STDINT_H 1
#endif

#if _MSC_VER >= 1800
#define HAVE_INTTYPES_H 1
#endif

typedef SSIZE_T ssize_t;

