/************************************************************
 *
 * (c) 2007 Olivier Castan castan.o@free.fr
 * Modified by Simson Garfinkel, to fit into the AFFLIB build system.
 *
 * License: LGPL or BSD-4
 *
 * KISS: based on fuse hello.c example
 *
 * TODO: - use xattr to display informations from segments
 *       - use AF_ACQUISITION_DATE for creation date
 *       - option between BADFLAG and NULLs
 *       - ...
 *
 * *********************************************************/

#include "affconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef USE_FUSE

/* bool used in afflib.h but not defined within C */
#ifndef bool
#define bool int
#endif
#include "afflib.h"
#include <fuse3/fuse.h>
#include <fcntl.h>
#include <libgen.h>


#define XCALLOC(type, num)                                      \
        ((type *) xcalloc ((num), sizeof(type)))
#define XMALLOC(type, num)                                      \
        ((type *) xmalloc ((num) * sizeof(type)))
#define XFREE(stale)                    do {                    \
        if (stale) { free ((void *) stale);  stale = 0; }       \
                                        } while (0)



static char *raw_path = NULL;
static off_t raw_size = 0;
static AFFILE *af_image = NULL;
static const char *raw_ext = ".raw";

static void *
xmalloc (size_t num)
{
    void *alloc = malloc (num);
    if (!alloc) {
        perror ("Memory exhausted");
        exit(EXIT_FAILURE);
    }
    return alloc;
}

static void *
xcalloc (size_t num, size_t size)
{
    void *alloc = xmalloc (num * size);
    memset (alloc, 0, num * size);
    return alloc;
}

static char *
xstrdup(char *string)
{
    return strcpy((char *)xmalloc(strlen(string) + 1), string);
}

static int
affuse_getattr(const char *path, struct stat *stbuf,
               struct fuse_file_info *fi)
{
    (void) fi;
    int res = 0;

    memset(stbuf, 0, sizeof(struct stat));
    if(strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    }
    else if(strcmp(path, raw_path) == 0) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = raw_size;
    }
    else
        res = -ENOENT;

    return res;
}

static int
affuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi,
               enum fuse_readdir_flags flags)
{
    (void) offset;
    (void) fi;
    (void) flags;

    if(strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    filler(buf, raw_path + 1, NULL, 0, 0);

    return 0;
}

static int
affuse_open(const char *path, struct fuse_file_info *fi)
{
    if(strcmp(path, raw_path) != 0)
        return -ENOENT;

    if((fi->flags & 3) != O_RDONLY)
        return -EACCES;

    return 0;
}

static int
affuse_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    int res = 0;
    (void) fi;
    if(strcmp(path, raw_path) != 0){
        return -ENOENT;
    }

    /* TODO: change to sector aligned readings to write NULLs to bad
     * blocks... */
    /* looks like af_seek never fails */
    af_seek(af_image, (uint64_t)offset, SEEK_SET);
    errno = 0;
    res = af_read(af_image, (unsigned char *)buf, (int)size);
    if (res<0){
	if (errno==0) errno=-EIO;
	else res = -errno;
    }
    return res;
}

static void
affuse_destroy(void* param)
{
    af_close(af_image);
    XFREE(raw_path);
    return;
}

static struct fuse_operations affuse_oper = {
     .getattr    = affuse_getattr,
     .readdir    = affuse_readdir,
     .open       = affuse_open,
     .read       = affuse_read,
     .destroy    = affuse_destroy,
};

static void
usage(void)
{
    char *cmdline[] = {"affuse", "-ho"};
    printf("affuse version %s\n", PACKAGE_VERSION);
    printf("Usage: affuse [<FUSE library options>] af_image mount_point\n");
    /* dirty, just to get current libfuse option list */
    fuse_main(2, cmdline, &affuse_oper, NULL);
    printf("\nUse fusermount3 -u mount_point, to unmount\n");
}

int main(int argc, char **argv)
{
    char *af_path = NULL, *af_basename = NULL;
    size_t raw_path_len = 0;
    char **fargv = NULL;
    int fargc = 0;

    if (argc < 3) {
        usage();
	exit(EXIT_FAILURE);
    }

    /* Prepare fuse args, af_image is omitted, but "-s" is added */
    fargv = XCALLOC(char *, argc); /* usually not free'd */
    fargv[0] = argv[0];
    fargv[1] = argv[argc - 1];
    fargc = 2;
    while (fargc <= (argc - 2)) {
        fargv[fargc] = argv[fargc - 1];
	if (strcmp(fargv[fargc], "-h") == 0 ||
	    strcmp(fargv[fargc], "--help") == 0 ) {
	    usage();
	    XFREE(fargv);
	    exit(EXIT_SUCCESS);
	}
	fargc++;
    }
    /* disable multi-threaded operation
     * (we don't know if afflib is thread safe!)
     */
    fargv[fargc] = "-s";
    fargc++;

    if ((af_image = af_open(argv[argc - 2], O_RDONLY|O_EXCL, 0)) == NULL) {
        perror("Can't open image file");
	XFREE(fargv);
	exit(EXIT_FAILURE);
    }

    af_path = xstrdup(argv[argc - 2]);
    af_basename = basename(af_path);
    /*             "/"       af_basename            raw_ext  "/0"*/
    raw_path_len = 1 + strlen(af_basename) + strlen(raw_ext) + 1;
    raw_path = XCALLOC(char, raw_path_len);
    raw_path[0] = '/';
    strcat(raw_path, af_basename);
    strcat(raw_path, raw_ext);
    raw_path[raw_path_len -1] = 0;
    XFREE(af_path);
    raw_size = af_get_imagesize(af_image);

    return fuse_main(fargc, fargv, &affuse_oper, NULL);
}
#else
int main(int argc,char **argv)
{
    fprintf(stderr,"affuse: FUSE support is disabled.\n");
#ifndef linux
    fprintf(stderr,"affuse was compiled on a platform that does not support FUSE\n");
#else
    fprintf(stderr,"affuse was compiled on a Linux system that did not\n");
    fprintf(stderr,"have the FUSE developer libraries installed\n");
    fprintf(stderr,"You need to install the fuse-devl package.\n");
#endif
    exit(1);
}
#endif
