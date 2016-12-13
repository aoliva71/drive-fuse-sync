
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define FUSE_USE_VERSION 26
#include <fuse_lowlevel.h>

#include "dbcache.h"

static const char *fuseapi_str = "Hello World!\n";
static const char *fuseapi_name = "hello";

static int fuseapi_stat(fuse_ino_t ino, struct stat *stbuf)
{
    int rc;

    int statcb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, int sync, const char *checksum,
            int64_t parent) {
        stbuf->st_ino = id;
        stbuf->st_mode = mode;
        stbuf->st_nlink = 1;
        stbuf->st_size = size;
    }

    rc = dbcache_pinpoint(ino, statcb);

    return rc;
}

static void fuseapi_getattr(fuse_req_t req, fuse_ino_t ino,
                 struct fuse_file_info *fi)
{
    struct stat stbuf;

    (void)fi;

    memset(&stbuf, 0, sizeof(stbuf));
    if (fuseapi_stat(ino, &stbuf) == -1) {
        fuse_reply_err(req, ENOENT);
    } else {
        fuse_reply_attr(req, &stbuf, 1.0);
    }
}

static void fuseapi_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    int rc;
    struct fuse_entry_param e;

    int lookupcb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, int sync, const char *checksum,
            int64_t parent) {
        e.ino = id;
        e.attr_timeout = 1.0;
        e.entry_timeout = 1.0;
        e.attr.st_ino = id;
        e.attr.st_mode = mode;
        e.attr.st_nlink = 1;
        e.attr.st_size = size;

        fuse_reply_entry(req, &e);
    }

    memset(&e, 0, sizeof(struct fuse_entry_param));
    rc = dbcache_lookup(name, parent, lookupcb);
    if(rc != 0) {
        fuse_reply_err(req, ENOENT);
    }
}

struct dirbuf {
    char *p;
    size_t size;
};

static void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name,
               fuse_ino_t ino)
{
    struct stat stbuf;
    size_t oldsize = b->size;
    b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
    b->p = (char *) realloc(b->p, b->size);
    memset(&stbuf, 0, sizeof(stbuf));
    stbuf.st_ino = ino;
    fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf,
              b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
                 off_t off, size_t maxsize)
{
    if (off < bufsize)
        return fuse_reply_buf(req, buf + off,
                      min(bufsize - off, maxsize));
    else
        return fuse_reply_buf(req, NULL, 0);
}

static void fuseapi_readdir(fuse_req_t req, fuse_ino_t ino, size_t sz,
                 off_t off, struct fuse_file_info *fi)
{
    int rc;
    int trivial;
    struct dirbuf b;

    (void)fi;

    int browsecb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, int sync, const char *checksum,
            int64_t parent) {
        memset(&b, 0, sizeof(struct dirbuf));
        if(trivial) {
            dirbuf_add(req, &b, ".", 1);
            dirbuf_add(req, &b, "..", 1);
        }
        dirbuf_add(req, &b, name, id);
        reply_buf_limited(req, b.p, b.size, off, sz);
        free(b.p);
    }
   
    trivial = 1;
    rc = dbcache_browse(ino, browsecb);
    if(rc != 0) {
        fuse_reply_err(req, ENOTDIR);
    }
}

static void fuseapi_open(fuse_req_t req, fuse_ino_t ino,
              struct fuse_file_info *fi)
{
    if (ino != 2)
        fuse_reply_err(req, EISDIR);
    else if ((fi->flags & 3) != O_RDONLY)
        fuse_reply_err(req, EACCES);
    else
        fuse_reply_open(req, fi);
}

static void fuseapi_read(fuse_req_t req, fuse_ino_t ino, size_t size,
              off_t off, struct fuse_file_info *fi)
{
    (void) fi;

    assert(ino == 2);
    reply_buf_limited(req, fuseapi_str, strlen(fuseapi_str), off, size);
}

static struct fuse_lowlevel_ops fapi_ll_ops = {
    .lookup = fuseapi_lookup,
    .getattr = fuseapi_getattr,
    .readdir = fuseapi_readdir,
    .open = fuseapi_open,
    .read = fuseapi_read,
};

static pthread_t fapi_ft;
static char fapi_mountpoint[PATH_MAX + 1];
static char *fapi_argv[] = {"-f", "-ofsname=drive", NULL};
struct fuse_args fapi_args = FUSE_ARGS_INIT(2, fapi_argv);
static struct fuse_chan *fapi_ch = NULL;
static struct fuse_session *fapi_fs = NULL;
static void *fuseapi_thread(void *);

int fuse_start(const char *mountpoint)
{
    memset(fapi_mountpoint, 0, (PATH_MAX + 1) * sizeof(char));
    strncpy(fapi_mountpoint, mountpoint, PATH_MAX);
    fapi_ch = fuse_mount(mountpoint, &fapi_args);
    fapi_fs = fuse_lowlevel_new(&fapi_args, &fapi_ll_ops,
            sizeof(struct fuse_lowlevel_ops), NULL);
    fuse_session_add_chan(fapi_fs, fapi_ch);
    pthread_create(&fapi_ft, NULL, fuseapi_thread, NULL);
}

int fuse_stop(void)
{
    fuse_session_exit(fapi_fs);
    fuse_session_remove_chan(fapi_ch);
    fuse_session_destroy(fapi_fs);
    fuse_unmount(fapi_mountpoint, fapi_ch);
    pthread_join(fapi_ft, NULL);
}

static void *fuseapi_thread(void *raw)
{
    (void)raw;
    fuse_session_loop(fapi_fs);
}


