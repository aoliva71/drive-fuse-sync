
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define FUSE_USE_VERSION 26
#include <fuse_lowlevel.h>

#include "dbcache.h"

#include <stdarg.h>
#define LOG(...) printf(__VA_ARGS__); printf("\n")

static const char *fuseapi_str = "Hello World!\n";
static const char *fuseapi_name = "hello";

static uid_t uid = 0;
static gid_t gid = 0;

static int fuseapi_stat(fuse_ino_t ino, struct stat *stbuf)
{
    int rc;

    LOG("fuseapi_stat: %lld", ino);

    int statcb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, int sync, const char *checksum,
            int64_t parent) {
        stbuf->st_ino = id;
        stbuf->st_mode = mode;
        switch(type) {
        case 1:
            stbuf->st_mode |= S_IFDIR;
            break;
        case 2:
            stbuf->st_mode |= S_IFREG;
            break;
        }
        stbuf->st_nlink = 1;
        stbuf->st_uid = uid;
        stbuf->st_gid = gid;
        switch(type) {
        case 1:
            stbuf->st_size = 4096;
            break;
        case 2:
            stbuf->st_size = size;
            break;
        }
        return 0;
    }

    rc = dbcache_pinpoint(ino, statcb);

    return rc;
}

static void fuseapi_getattr(fuse_req_t req, fuse_ino_t ino,
        struct fuse_file_info *fi)
{
    int rc;
    struct stat stbuf;

    LOG("fuseapi_getattr: %lld", ino);
    (void)fi;

    memset(&stbuf, 0, sizeof(stbuf));
    rc = fuseapi_stat(ino, &stbuf);
    if(0 == rc) {
        fuse_reply_attr(req, &stbuf, 1.0);
    } else {
        fuse_reply_err(req, ENOENT);
    }
}

static void fuseapi_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    int rc;
    struct fuse_entry_param e;

    LOG("fuseapi_lookup: %s", name);

    int lookupcb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, int sync, const char *checksum,
            int64_t parent) {
        e.ino = id;
        e.attr_timeout = 1.0;
        e.entry_timeout = 1.0;
        e.attr.st_ino = id;
        e.attr.st_mode = mode;
        switch(type) {
        case 1:
            e.attr.st_mode |= S_IFDIR;
            break;
        case 2:
            e.attr.st_mode |= S_IFREG;
            break;
        }
        e.attr.st_nlink = 1;
        e.attr.st_uid = uid;
        e.attr.st_gid = gid;
        switch(type) {
        case 1:
            e.attr.st_size = 4096;
            break;
        case 2:
            e.attr.st_size = size;
            break;
        }

        return 0;
    }

    memset(&e, 0, sizeof(struct fuse_entry_param));
    rc = dbcache_lookup(name, parent, lookupcb);
    if(0 == rc) {
        fuse_reply_entry(req, &e);
    } else {
        fuse_reply_err(req, ENOENT);
    }
}

static void fuseapi_readdir(fuse_req_t req, fuse_ino_t ino, size_t sz,
                 off_t off, struct fuse_file_info *fi)
{
    int rc;
    uint8_t buf[4096];
    struct stat st;
    size_t blen;
    size_t len;

    LOG("fuseapi_readdir - ino: %lld, size: %lld, off: %lld", ino, sz, off);
    (void)fi;

    if(sz > 4096) {
        sz = 4096;
    }

    len = blen = 0;

    int browsecb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, int sync, const char *checksum,
            int64_t parent) {
        memset(buf, 0, 4096 * sizeof(uint8_t));
        if(0 == off) {
            memset(&st, 0, sizeof(struct stat));
            st.st_mode = S_IFDIR|0x700;
            st.st_nlink = 1;
            st.st_uid = uid;
            st.st_gid = gid;
            blen = fuse_add_direntry(req, buf, sz, ".", &st, 1);
            len += blen;
            sz -= blen;
            blen = fuse_add_direntry(req, buf + len, sz, "..", &st, 1);
            len += blen;
            sz -= blen;
        }

        memset(&st, 0, sizeof(struct stat));
        st.st_ino = id;
        st.st_mode = mode;
        switch(type) {
        case 1:
            st.st_mode |= S_IFDIR;
            break;
        case 2:
            st.st_mode |= S_IFREG;
            break;
        }
        st.st_nlink = 1;
        st.st_uid = uid;
        st.st_gid = gid;
        switch(type) {
        case 1:
            st.st_size = 4096;
            break;
        case 2:
            st.st_size = size;
            break;
        }

        blen = fuse_add_direntry(req, buf + len, sz, name, &st, id);
        len += blen;
        fuse_reply_buf(req, buf, len);

        return 0;
    }
   
    rc = dbcache_browse(ino, off, browsecb);
    if(0 == rc) {
        fuse_reply_buf(req, NULL, 0);
    } else {
        fuse_reply_err(req, ENOTDIR);
    }
}

static void fuseapi_open(fuse_req_t req, fuse_ino_t ino,
              struct fuse_file_info *fi)
{
    LOG("fuseapi_open: %lld", ino);

    /*if (ino != 2)
        fuse_reply_err(req, EISDIR);
    else if ((fi->flags & 3) != O_RDONLY)
        fuse_reply_err(req, EACCES);
    else
        fuse_reply_open(req, fi);*/
    fuse_reply_err(req, EACCES);
}

static void fuseapi_read(fuse_req_t req, fuse_ino_t ino, size_t size,
              off_t off, struct fuse_file_info *fi)
{
    (void) fi;

    LOG("fuseapi_read: %lld", ino);

    assert(ino == 2);
    //reply_buf_limited(req, fuseapi_str, strlen(fuseapi_str), off, size);
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
    uid = getuid();
    gid = getgid();
    memset(fapi_mountpoint, 0, (PATH_MAX + 1) * sizeof(char));
    strncpy(fapi_mountpoint, mountpoint, PATH_MAX);
    fapi_ch = fuse_mount(fapi_mountpoint, &fapi_args);
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

#undef LOG


