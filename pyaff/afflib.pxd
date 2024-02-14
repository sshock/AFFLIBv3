from libc.stdint cimport int64_t, uint32_t, uint64_t
from posix.fcntl cimport O_RDONLY

cdef extern from "afflib/afflib.h":
    struct _AFFILE
    ctypedef _AFFILE AFFILE

    enum: AF_MAX_NAME_LEN

    AFFILE *af_open(const char *filename, int flags, int mode)
    int af_close(AFFILE *af)

    int64_t af_get_imagesize(AFFILE *af)

    ssize_t af_read(AFFILE *af, unsigned char *buf, ssize_t count)
    uint64_t af_seek(AFFILE *af, int64_t pos, int whence)
    uint64_t af_tell(AFFILE *af)


    int af_get_seg(AFFILE *af, const char *name, uint32_t *arg,
                   unsigned char *data, size_t *datalen)
    int af_get_next_seg(AFFILE *af, char *segname, size_t segname_len,
                        uint32_t *arg, unsigned char *data, size_t *datalen)
    int af_rewind_seg(AFFILE *af)

