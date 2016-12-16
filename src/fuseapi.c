
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

#define DIRSIZE     4096

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
            stbuf->st_size = DIRSIZE;
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
            e.attr.st_size = DIRSIZE;
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

static void fuseapi_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
               mode_t mode)
{
    int rc;
    int64_t id;
    int fd;
    struct fuse_entry_param e;

    LOG("fuseapi_mkdir: %lld, %s", parent, name);

    rc = dbcache_createdir(&id, "ffffffff-ffff-ffff-ffff-ffffffffffff", name,
            mode, 1, "@", parent);
    if(rc != 0) {
        fuse_reply_err(req, EACCES);
        return;
    }

    memset(&e, 0, sizeof(struct fuse_entry_param));
    e.ino = id;
    e.attr_timeout = 1.0;
    e.entry_timeout = 1.0;
    e.attr.st_ino = id;
    e.attr.st_mode = S_IFDIR|mode;
    e.attr.st_nlink = 1;
    e.attr.st_uid = uid;
    e.attr.st_gid = gid;
    e.attr.st_size = DIRSIZE;

    rc = fscache_mkdir(id);
    if(0 == rc) {
        fuse_reply_entry(req, &e);
    } else {
        fuse_reply_err(req, EACCES);
    }
}

static void fuseapi_readdir(fuse_req_t req, fuse_ino_t ino, size_t sz,
                 off_t off, struct fuse_file_info *fi)
{
    int rc;
#define RDDIRBUF_SIZE   4096
    uint8_t buf[RDDIRBUF_SIZE];
    struct stat st;
    size_t blen;
    size_t len;

    LOG("fuseapi_readdir - ino: %lld, size: %lld, off: %lld", ino, sz, off);
    (void)fi;

    if(sz > RDDIRBUF_SIZE) {
        sz = RDDIRBUF_SIZE;
    }

    len = blen = 0;

    int browsecb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, int sync, const char *checksum,
            int64_t parent) {
        memset(buf, 0, RDDIRBUF_SIZE * sizeof(uint8_t));
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
            st.st_size = DIRSIZE;
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
#undef RDDIRBUF_SIZE
}

static void fuseapi_create(fuse_req_t req, fuse_ino_t parent, const char *name,
            mode_t mode, struct fuse_file_info *fi)
{
    int rc;
    int64_t id;
    int fd;
    struct fuse_entry_param e;

    LOG("fuseapi_open: %lld, %s", parent, name);

    rc = dbcache_createfile(&id, "ffffffff-ffff-ffff-ffff-ffffffffffff", name,
            0, mode, 1, "@", parent);
    if(rc != 0) {
        fuse_reply_err(req, EACCES);
        return;
    }

    memset(&e, 0, sizeof(struct fuse_entry_param));
    e.ino = id;
    e.attr_timeout = 1.0;
    e.entry_timeout = 1.0;
    e.attr.st_ino = id;
    e.attr.st_mode = mode;
    e.attr.st_nlink = 1;
    e.attr.st_uid = uid;
    e.attr.st_gid = gid;
    e.attr.st_size = 0;

    rc = fscache_create(id, &fd);
    if(0 == rc) {
        fi->fh = fd;
        fuse_reply_create(req, &e, fi);
    } else {
        fuse_reply_err(req, EACCES);
    }
}

static void fuseapi_open(fuse_req_t req, fuse_ino_t ino,
              struct fuse_file_info *fi)
{
    int rc;
    int fd;

    LOG("fuseapi_open: %lld", ino);

    rc = fscache_open(ino, &fd);
    if(0 == rc) {
        fi->fh = fd;
        fuse_reply_open(req, fi);
    } else {
        fuse_reply_err(req, EACCES);
    }
}

static void fuseapi_release(fuse_req_t req, fuse_ino_t ino,
              struct fuse_file_info *fi)
{
    int rc;
    size_t size;

    LOG("fuseapi_release: %lld", ino);

    if(fi->flags & O_WRONLY) {
        fscache_size(fi->fh, &size);
        dbcache_modifysize(ino, size);
    }

    rc = fscache_close(fi->fh);
    if(rc != 0) {
        fuse_reply_err(req, EACCES);
    }
}

static void fuseapi_read(fuse_req_t req, fuse_ino_t ino, size_t size,
              off_t off, struct fuse_file_info *fi)
{
    int rc;

    (void) fi;

    LOG("fuseapi_read: %lld", ino);

    int readcb(const void *data, size_t len)
    {
        fuse_reply_buf(req, data, len);
        return 0;
    }

    rc = fscache_read(fi->fh, readcb, off, size);
    if(rc != 0) {
        fuse_reply_err(req, EACCES);
    }
}

static void fuseapi_write(fuse_req_t req, fuse_ino_t ino, const char *buf,
               size_t size, off_t off, struct fuse_file_info *fi)
{
    int rc;

    LOG("fuseapi_write: %lld", ino);

    int writecb(const void *data, size_t len)
    {
        (void)data;
        fuse_reply_write(req, size);
        return 0;
    }

    rc = fscache_write(fi->fh, writecb, buf, off, size);
    if(rc != 0) {
        fuse_reply_err(req, EACCES);
    }
}

static struct fuse_lowlevel_ops fapi_ll_ops = {
    .lookup = fuseapi_lookup,
    .getattr = fuseapi_getattr,
    .mkdir = fuseapi_mkdir,
    .readdir = fuseapi_readdir,
    .create = fuseapi_create,
    .open = fuseapi_open,
    .release = fuseapi_release,
    .read = fuseapi_read,
    .write = fuseapi_write,
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

    /* join will keep pending :( */
    /*pthread_join(fapi_ft, NULL);*/
}

static void *fuseapi_thread(void *opaque)
{
    (void)opaque;
    fuse_session_loop(fapi_fs);
}

#undef LOG


