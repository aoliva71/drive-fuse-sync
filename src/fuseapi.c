
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

#define BLOCKSIZE     4096

static struct fuse_chan *fapi_ch = NULL;

static uid_t uid = 0;
static gid_t gid = 0;

static void fuseapi_getattr(fuse_req_t req, fuse_ino_t ino,
        struct fuse_file_info *fi)
{
    int rc;
    struct stat st;

    LOG("fuseapi_getattr: %lld", ino);
    (void)fi;

    memset(&st, 0, sizeof(struct stat));
    int statcb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, const struct timespec *atime,
            const struct timespec *mtime, const struct timespec *ctime,
            int sync, int refcount, const char *checksum, int64_t parent) {
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
            st.st_size = BLOCKSIZE;
            break;
        case 2:
            st.st_size = size;
            break;
        }
        st.st_blksize = BLOCKSIZE;
        st.st_blocks = st.st_size / 512L + (st.st_size % 512L ? 1L : 0L);
        if(0 == st.st_size) {
            st.st_blocks++;
        }
        memcpy(&st.st_atim, atime, sizeof(struct timespec));
        memcpy(&st.st_mtim, mtime, sizeof(struct timespec));
        memcpy(&st.st_ctim, ctime, sizeof(struct timespec));

        return 0;
    }

    rc = dbcache_pinpoint(ino, statcb);
    if(0 == rc) {
        fuse_reply_attr(req, &st, 1.0);
    } else {
        fuse_reply_err(req, ENOENT);
    }
}

static void fuseapi_setattr(fuse_req_t req, fuse_ino_t ino,
        struct stat *attr, int to_set, struct fuse_file_info *fi)
{
    int rc;
    struct timespec tv;
    struct stat st;

    LOG("fuseapi_setattr: %lld", ino);
    (void)fi;

    rc = -1;
    if(to_set & FUSE_SET_ATTR_MODE) {
        rc = dbcache_chmod(ino, attr->st_mode);
        if(rc != 0) {
            fuse_reply_err(req, ENOENT);
            return;
        }
    }
    if(to_set & FUSE_SET_ATTR_SIZE) {
        rc = dbcache_resize(ino, attr->st_size);
        if(rc != 0) {
            fuse_reply_err(req, ENOENT);
            return;
        }
    }
    if(to_set & FUSE_SET_ATTR_ATIME) {
        if(to_set & FUSE_SET_ATTR_ATIME_NOW) {
            clock_gettime(CLOCK_REALTIME, &tv);
            memcpy(&attr->st_atim, &tv, sizeof(struct timespec));
        }
        rc = dbcache_chatime(ino, &attr->st_atim);
        if(rc != 0) {
            fuse_reply_err(req, ENOENT);
            return;
        }
    }
    if(to_set & FUSE_SET_ATTR_MTIME) {
        if(to_set & FUSE_SET_ATTR_MTIME_NOW) {
            clock_gettime(CLOCK_REALTIME, &tv);
            memcpy(&attr->st_mtim, &tv, sizeof(struct timespec));
        }
        rc = dbcache_chmtime(ino, &attr->st_mtim);
        if(rc != 0) {
            fuse_reply_err(req, ENOENT);
            return;
        }
    }

    memset(&st, 0, sizeof(struct stat));
    int statcb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, const struct timespec *atime,
            const struct timespec *mtime, const struct timespec *ctime,
            int sync, int refcount, const char *checksum, int64_t parent) {
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
            st.st_size = BLOCKSIZE;
            break;
        case 2:
            st.st_size = size;
            break;
        }
        st.st_blksize = BLOCKSIZE;
        st.st_blocks = st.st_size / 512L + (st.st_size % 512L ? 1L : 0L);
        if(0 == st.st_size) {
            st.st_blocks++;
        }
        memcpy(&st.st_atim, atime, sizeof(struct timespec));
        memcpy(&st.st_mtim, mtime, sizeof(struct timespec));
        memcpy(&st.st_ctim, ctime, sizeof(struct timespec));

        return 0;
    }

    rc = dbcache_pinpoint(ino, statcb);
    if(0 == rc) {
        fuse_reply_attr(req, &st, 1.0);
        rc = fuse_lowlevel_notify_inval_inode(fapi_ch, ino, -1, 0);
        LOG("invalidate: %d", rc);
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
            size_t size, mode_t mode, const struct timespec *atime,
            const struct timespec *mtime, const struct timespec *ctime,
            int sync, int refcount, const char *checksum, int64_t parent) {
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
            e.attr.st_size = BLOCKSIZE;
            break;
        case 2:
            e.attr.st_size = size;
            break;
        }
        e.attr.st_blksize = BLOCKSIZE;
        e.attr.st_blocks = e.attr.st_size / 512L +
                (e.attr.st_size % 512L ? 1L : 0L);
        if(0 == e.attr.st_size) {
            e.attr.st_blocks++;
        }
        memcpy(&e.attr.st_atim, atime, sizeof(struct timespec));
        memcpy(&e.attr.st_mtim, mtime, sizeof(struct timespec));
        memcpy(&e.attr.st_ctim, ctime, sizeof(struct timespec));

        return 0;
    }

    memset(&e, 0, sizeof(struct fuse_entry_param));
    rc = dbcache_lookup(name, parent, lookupcb);
    if(0 == rc) {
        fuse_reply_entry(req, &e);
    } else {
        fuse_reply_err(req, ENOENT);
    }
    LOG("fuseapi_lookup exit");
}

static void fuseapi_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
               mode_t mode)
{
    int rc;
    int64_t id;
    int fd;
    struct fuse_entry_param e;
    struct timespec tv;

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
    e.attr.st_size = BLOCKSIZE;
    e.attr.st_blksize = BLOCKSIZE;
    e.attr.st_blocks = e.attr.st_size / 512L +
            (e.attr.st_size % 512L ? 1L : 0L);
    if(0 == e.attr.st_size) {
        e.attr.st_blocks++;
    }
    clock_gettime(CLOCK_REALTIME, &tv);
    memcpy(&e.attr.st_atim, &tv, sizeof(struct timespec));
    memcpy(&e.attr.st_mtim, &tv, sizeof(struct timespec));
    memcpy(&e.attr.st_ctim, &tv, sizeof(struct timespec));

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
            size_t size, mode_t mode, const struct timespec *atime,
            const struct timespec *mtime, const struct timespec *ctime,
            int sync, int refcount, const char *checksum, int64_t parent) {
        memset(buf, 0, RDDIRBUF_SIZE * sizeof(uint8_t));
        if(0 == off) {
            memset(&st, 0, sizeof(struct stat));
            st.st_mode = S_IFDIR|0x700;
            st.st_nlink = 1;
            st.st_uid = uid;
            st.st_gid = gid;
            switch(type) {
            case 1:
                st.st_size = BLOCKSIZE;
                break;
            case 2:
                st.st_size = size;
                break;
            }
            st.st_blksize = BLOCKSIZE;
            st.st_blocks = st.st_size / 512L + (st.st_size % 512L ? 1L : 0L);
            if(0 == st.st_size) {
                st.st_blocks++;
            }
            memcpy(&st.st_atim, atime, sizeof(struct timespec));
            memcpy(&st.st_mtim, mtime, sizeof(struct timespec));
            memcpy(&st.st_ctim, ctime, sizeof(struct timespec));
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
            st.st_size = BLOCKSIZE;
            break;
        case 2:
            st.st_size = size;
            break;
        }
        st.st_blksize = BLOCKSIZE;
        st.st_blocks = st.st_size / 512L + (st.st_size % 512L ? 1L : 0L);
        if(0 == st.st_size) {
            st.st_blocks++;
        }
        memcpy(&st.st_atim, atime, sizeof(struct timespec));
        memcpy(&st.st_mtim, mtime, sizeof(struct timespec));
        memcpy(&st.st_ctim, ctime, sizeof(struct timespec));

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

static void fuseapi_rmdir(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    int rc;

    LOG("fuseapi_rmdir: %lld, %s", parent, name);
    int rmdircb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, const struct timespec *atime,
            const struct timespec *mtime, const struct timespec *ctime,
            int sync, int refcount, const char *checksum, int64_t parent) {
        if(0 == refcount) {
            fscache_rmdir(id);
        }
    }
    rc = dbcache_rmdir(name, parent, rmdircb);
    if(0 == rc) {
        /* need an empty buffer to move from pending read */
        fuse_reply_buf(req, NULL, 0);
    } else {
        fuse_reply_err(req, EACCES);
    }
}

static void fuseapi_create(fuse_req_t req, fuse_ino_t parent, const char *name,
            mode_t mode, struct fuse_file_info *fi)
{
    int rc;
    int64_t id;
    int fd;
    struct fuse_entry_param e;
    struct timespec tv;

    LOG("fuseapi_create: %lld, %s", parent, name);

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
    e.attr.st_mode |= S_IFREG;
    e.attr.st_nlink = 1;
    e.attr.st_uid = uid;
    e.attr.st_gid = gid;
    e.attr.st_size = 0;
    e.attr.st_blksize = BLOCKSIZE;
    e.attr.st_blocks = e.attr.st_size / 512L + (e.attr.st_size % 512L ? 1 : 0);
    if(0 == e.attr.st_size) {
        e.attr.st_blocks++;
    }
    clock_gettime(CLOCK_REALTIME, &tv);
    memcpy(&e.attr.st_atim, &tv, sizeof(struct timespec));
    memcpy(&e.attr.st_mtim, &tv, sizeof(struct timespec));
    memcpy(&e.attr.st_ctim, &tv, sizeof(struct timespec));

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

    rc = fscache_open(ino, fi->flags, &fd);
    if(0 == rc) {
        fi->fh = fd;
        fuse_reply_open(req, fi);
    } else {
        fuse_reply_err(req, EACCES);
    }
}

static void fuseapi_flush(fuse_req_t req, fuse_ino_t ino,
              struct fuse_file_info *fi)
{
    (void)req;
    (void)ino;
    (void)fi;

    LOG("fuseapi_flush: %lld", ino);
    // nothing to be done
}

static void fuseapi_release(fuse_req_t req, fuse_ino_t ino,
              struct fuse_file_info *fi)
{
    int rc;
    size_t size;

    LOG("fuseapi_release: %lld", ino);

    if(fi) {
        if(fi->flags & O_ACCMODE) {
            fscache_size(fi->fh, &size);
            dbcache_resize(ino, size);
            fuse_lowlevel_notify_inval_inode(fapi_ch, ino, 0, 0);
        }

        rc = fscache_close(fi->fh);
        if(rc != 0) {
            fuse_reply_err(req, EACCES);
        }
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
        LOG("calling fuse_reply_buf(%p, %p, %lld);", req, data, len);
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
    .setattr = fuseapi_setattr,
    .mkdir = fuseapi_mkdir,
    .readdir = fuseapi_readdir,
    .rmdir = fuseapi_rmdir,
    .create = fuseapi_create,
    .open = fuseapi_open,
//    .flush = fuseapi_flush,
    .release = fuseapi_release,
    .read = fuseapi_read,
    .write = fuseapi_write,
};

static pthread_t fapi_ft;
static char fapi_mountpoint[PATH_MAX + 1];
static char *fapi_argv[] = {"-f", "-ofsname=drive", NULL};
struct fuse_args fapi_args = FUSE_ARGS_INIT(2, fapi_argv);
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


