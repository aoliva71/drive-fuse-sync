
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "dbcache.h"
#include "fscache.h"
#include "driveapi.h"
#include "log.h"

#define SECTSIZE    512L
#define BLOCKSIZE   4096L

static uid_t uid = 0;
static gid_t gid = 0;

static int fuseapi_getattr(const char *path, struct stat *st)
{
    log_debug("fuseapi_getattr: %s", path);

    memset(st, 0, sizeof(struct stat));
    int cb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, const struct timespec *atime,
            const struct timespec *mtime, const struct timespec *ctime,
            const char *checksum, int64_t parent) {
        (void)uuid;
        (void)name;
        (void)checksum;
        (void)parent;
        st->st_ino = id;
        st->st_mode = mode;
        switch(type) {
        case 1:
            st->st_mode |= S_IFDIR;
            break;
        case 2:
            st->st_mode |= S_IFREG;
            break;
        }
        st->st_nlink = 1;
        st->st_uid = uid;
        st->st_gid = gid;
        switch(type) {
        case 1:
            st->st_size = BLOCKSIZE;
            break;
        case 2:
            st->st_size = size;
            break;
        }
        st->st_blksize = BLOCKSIZE;
        st->st_blocks = st->st_size / SECTSIZE +
                (st->st_size % SECTSIZE ? 1L : 0L);
        if(0 == st->st_size) {
            st->st_blocks++;
        }
        memcpy(&st->st_atim, atime, sizeof(struct timespec));
        memcpy(&st->st_mtim, mtime, sizeof(struct timespec));
        memcpy(&st->st_ctim, ctime, sizeof(struct timespec));

        return 0;
    }

    return dbcache_findbypath(path, cb);
}

static int fuseapi_mkdir(const char *path, mode_t mode)
{
    log_debug("fuseapi_mkdir: %s", path);

    int cb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, const struct timespec *atime,
            const struct timespec *mtime, const struct timespec *ctime,
            const char *checksum, int64_t parent)
    {
        (void)id;
        (void)uuid;
        (void)name;
        (void)type;
        (void)size;
        (void)mode;
        (void)atime;
        (void)mtime;
        (void)ctime;
        (void)checksum;
        (void)parent;
        return 0;
    }

    return dbcache_mkdir(path, mode, cb);
}

/*static void fuseapi_unlink(const char *path)
{
    log_debug("fuseapi_unlink: %s", path);

    int cb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, const struct timespec *atime,
            const struct timespec *mtime, const struct timespec *ctime,
            int sync, const char *checksum, int64_t parent)
    {
        (void)id;
        (void)name;
        (void)type;
        (void)size;
        (void)mode;
        (void)atime;
        (void)mtime;
        (void)ctime;
        (void)sync;
        (void)checksum;
        (void)parent;
        return fscache_rm(uuid);
    }
    return dbcache_rm(path, cb);
}*/

static int fuseapi_rmdir(const char *path)
{
    log_debug("fuseapi_rmdir: %s", path);
    int cb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, const struct timespec *atime,
            const struct timespec *mtime, const struct timespec *ctime,
            const char *checksum, int64_t parent) {
        (void)id;
        (void)uuid;
        (void)name;
        (void)type;
        (void)size;
        (void)mode;
        (void)atime;
        (void)mtime;
        (void)ctime;
        (void)checksum;
        (void)parent;
        return 0;
    }
    return dbcache_rmdir(path, cb);
}

/*static int fuseapi_rename(const char *oldpath, const char *path)
{
    log_debug("fuseapi_rename: %s -> %s", oldpath, path);
    int cb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, const struct timespec *atime,
            const struct timespec *mtime, const struct timespec *ctime,
            int sync, const char *checksum, int64_t parent) {
        (void)uuid;
        (void)name;
        (void)type;
        (void)size;
        (void)mode;
        (void)atime;
        (void)mtime;
        (void)ctime;
        (void)sync;
        (void)checksum;
        (void)parent;

        if(0 == refcount) {
            fscache_rmdir(id);
        }
        return 0;
    }
    return dbcache_rename(oldpath, newpath, cb);
}*/

static int fuseapi_open(const char *path,
              struct fuse_file_info *fi)
{
    int rc;

    log_debug("fuseapi_open: %s", path);

    int cb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, const struct timespec *atime,
            const struct timespec *mtime, const struct timespec *ctime,
            const char *checksum, int64_t parent) {
        struct stat st;
        FILE *f;

        (void)id;
        (void)name;
        (void)type;
        (void)size;
        (void)mode;
        (void)atime;
        (void)mtime;
        (void)ctime;
        (void)checksum;
        (void)parent;

        rc = fscache_stat(uuid, &st);
        if(-ENOENT == rc) {
            f = fscache_fopen(uuid);
            if(f) {
                rc = drive_download(uuid, f);
                fscache_fclose(f);
            }
        }
        if(0 == rc) {
            rc = fscache_open(uuid, fi->flags);
            if(rc >= 0) {
                fi->fh = rc;
                rc = 0;
            }
        }
        return rc;
    }
    
    return dbcache_findbypath(path, cb);
}

static int fuseapi_read(const char *path, char *buf, size_t size,
        off_t off, struct fuse_file_info *fi)
{
    log_debug("fuseapi_read: %s", path);
    
    return fscache_read(fi->fh, buf, off, size);
}

/*static int fuseapi_write(const char *path, const char *buf,
               size_t size, off_t off, struct fuse_file_info *fi)
{
    log_debug("fuseapi_write: %s", path);

    return fscache_write(fi->fh, buf, off, size);
}*/

static int fuseapi_release(const char *path,
              struct fuse_file_info *fi)
{
    size_t size;

    log_debug("fuseapi_release: %s", path);

    if(fi->flags & O_ACCMODE) {
        fscache_size(fi->fh, &size);
        //dbcache_resize(ino, size);
    }

    return fscache_close(fi->fh);
}

static int fuseapi_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t off, struct fuse_file_info *fi)
{
    struct stat st;
    log_debug("fuseapi_readdir: %s", path);
    (void)off;
    (void)fi;

    int cb(int64_t id, const char *uuid, const char *name, int type,
            size_t size, mode_t mode, const struct timespec *atime,
            const struct timespec *mtime, const struct timespec *ctime,
            const char *checksum, int64_t parent) {

        (void)uuid;
        (void)checksum;
        (void)parent;

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
        st.st_blocks = st.st_size / SECTSIZE +
                (st.st_size % SECTSIZE ? 1L : 0L);
        if(0 == st.st_size) {
            st.st_blocks++;
        }
        memcpy(&st.st_atim, atime, sizeof(struct timespec));
        memcpy(&st.st_mtim, mtime, sizeof(struct timespec));
        memcpy(&st.st_ctim, ctime, sizeof(struct timespec));
        filler(buf, name, &st, 0);

        return 0;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    return dbcache_listdir(path, cb);
}

/*static void fuseapi_create(const char *path,
            mode_t mode, struct fuse_file_info *fi)
{
    int rc;
    int64_t id;
    int fd;
    struct timespec tv;

    log_debug("fuseapi_create: %%s", path);

    rc = dbcache_createfile(path, mode);
    if(rc != 0) {
        fuse_reply_err(req, EACCES);
        return;
    }

    rc = fscache_create(id, &fd);
    if(0 == rc) {
        fi->fh = fd;
        fuse_reply_create(req, &e, fi);
    } else {
        fuse_reply_err(req, EACCES);
    }
}

static void fuseapi_flush(const char *path,
              struct fuse_file_info *fi)
{
    (void)ino;
    (void)fi;

    log_debug("fuseapi_flush: %lld", (long long int)ino);
    // nothing to be done
    fuse_reply_buf(req, NULL, 0);
}*/

static struct fuse_operations fapi_ops = {
    .getattr = fuseapi_getattr,
    .mkdir = fuseapi_mkdir,
//    .unlink = fuseapi_unlink,
    .rmdir = fuseapi_rmdir,
//    .rename = fuseapi_rename,
//    .chmod = fuseapi_chmod,
//    .chown = fuseapi_chown,
//    .truncate
    .open = fuseapi_open,
    .read = fuseapi_read,
//    .write = fuseapi_write,
//    .statfs = fuseapi_statfs,
//    .flush = fuseapi_flush,
    .release = fuseapi_release,
//    .fsync
//    .opendir
    .readdir = fuseapi_readdir,
//    .releasedir
//    .fsyncdir
//    .init
//    .destroy
//    .access
//    .create = fuseapi_create,
//    .ftruncate
//    .fgetattr
//    .lock
//    .utimens
};

static char fapi_mountpoint[PATH_MAX + 1];
static char *fapi_argv[] = {"dfs", "-f", "-ofsname=drive",
        fapi_mountpoint, NULL};
struct fuse_args fapi_args = FUSE_ARGS_INIT(2, fapi_argv);

int fuseapi_run(const char *mountpoint)
{
    uid = getuid();
    gid = getgid();
    memset(fapi_mountpoint, 0, (PATH_MAX + 1) * sizeof(char));
    strncpy(fapi_mountpoint, mountpoint, PATH_MAX);
    fuse_main(4, fapi_argv, &fapi_ops, NULL);

    return 0;
}


