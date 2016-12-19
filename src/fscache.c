
#include "fscache.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dbcache.h"

#include <stdarg.h>
#define LOG(...) printf(__VA_ARGS__); printf("\n")

static void *fscache_run(void *);
static char fscachedir[PATH_MAX + 1];
static void fscache_update(const char *);

int fscache_start(const char *cachedir)
{
    memset(fscachedir, 0, (PATH_MAX + 1) * sizeof(char));
    strncpy(fscachedir, cachedir, PATH_MAX);
    return 0;
}

int fscache_stop(void)
{
    return 0;
}

int fscache_mkdir(int64_t id)
{
    int rc;
    char path[PATH_MAX + 1];
    char relpath[PATH_MAX + 1];
    const char *tmp;
    char *slash;

    memset(path, 0, (PATH_MAX + 1) * sizeof(char));
    memset(relpath, 0, (PATH_MAX + 1) * sizeof(char));
    rc = dbcache_path(id, relpath, PATH_MAX);
    if(0 == rc) {
        snprintf(path, PATH_MAX, "%s%s", fscachedir, relpath);

        tmp = path;
        tmp += strlen(fscachedir);
        tmp++;

        while(*tmp) {
            slash = strchr(tmp, '/');
            if(NULL == slash) {
                break;
            }
            *slash = 0;
            /* do not check rc as these calls may fail when dir is already
               there */
            mkdir(path, (mode_t)0700);
            *slash = '/';
            tmp = slash;
            tmp++;
        }

        rc = mkdir(path, (mode_t)0700);
        if(rc != 0) {
            rc = errno;
        }
    }

    return rc;
}

int fscache_create(int64_t id, int *fd)
{
    int rc;
    char path[PATH_MAX + 1];
    char relpath[PATH_MAX + 1];

    memset(path, 0, (PATH_MAX + 1) * sizeof(char));
    memset(relpath, 0, (PATH_MAX + 1) * sizeof(char));
    rc = dbcache_path(id, relpath, PATH_MAX);
    if(0 == rc) {
        fscache_update(relpath);
        snprintf(path, PATH_MAX, "%s%s", fscachedir, relpath);
        
        *fd = creat(path, (mode_t)0600);
        if((*fd) < 0) {
            rc = errno;
        }
    }
    
    return rc;
}

int fscache_open(int64_t id, int flags, int *fd)
{
    int rc;
    char path[PATH_MAX + 1];
    char relpath[PATH_MAX + 1];

    memset(path, 0, (PATH_MAX + 1) * sizeof(char));
    memset(relpath, 0, (PATH_MAX + 1) * sizeof(char));
    rc = dbcache_path(id, relpath, PATH_MAX);
    if(0 == rc) {
        fscache_update(relpath);
        snprintf(path, PATH_MAX, "%s%s", fscachedir, relpath);

        LOG("opening: %s", path);
        LOG("flags: %d 0x%x 0%o", flags, flags, flags);
        *fd = open(path, flags);
        if((*fd) < 0) {
            rc = errno;
        }
    }
    
    return rc;
}

int fscache_close(int fd)
{
    if(fd >= 0) {
        close(fd);
    }
    return 0;
}

int fscache_read(int fd, fscache_read_cb_t *cb, off_t off, size_t len)
{
    int rc;
#define FSCACHE_BUFMAX  4096
    uint8_t buf[FSCACHE_BUFMAX];
    if(len < FSCACHE_BUFMAX) {
        len = FSCACHE_BUFMAX;
    }
    rc = lseek(fd, off, SEEK_SET);
    rc = read(fd, buf, len);
    rc = cb(buf, len);
    return 0;
#undef FSCACHE_BUFMAX
}

int fscache_write(int fd, fscache_write_cb_t *cb, const void *buf, off_t off,
        size_t len)
{
    int rc;

    rc = lseek(fd, off, SEEK_SET);
    rc = write(fd, buf, len);
    rc = cb(buf, len);

    return 0;
}

int fscache_size(int fd, size_t *sz)
{
    int rc;
    struct stat st;

    rc = fstat(fd, &st);
    if(0 == rc) {
        *sz = st.st_size;
    }
    return rc;
}

static void *fscache_run(void *opaque)
{
    (void)opaque;
}

static void fscache_update(const char *rel)
{
    int rc;
    char path[PATH_MAX + 1];
    char *slash;
    const char *tmp;
    struct stat st;
    FILE *f;

    /* create cache dirs if not exist */
    memset(path, 0, (PATH_MAX + 1) * sizeof(char));
    snprintf(path, PATH_MAX, "%s%s", fscachedir, rel);
    tmp = path;
    tmp += strlen(fscachedir);
    tmp++;

    while(*tmp) {
        slash = strchr(tmp, '/');
        if(NULL == slash) {
            break;
        }
        *slash = 0;
        mkdir(path, (mode_t)0700);
        *slash = '/';
        tmp = slash;
        tmp++;
    }
    
    rc = stat(path, &st);
    if(rc != 0) {
        /* file not found, downloading (doing nothing for now) */

    }
}

