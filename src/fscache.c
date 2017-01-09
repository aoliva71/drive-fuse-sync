
#include "fscache.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dbcache.h"
#include "driveapi.h"

#include <stdarg.h>
#define LOG(...) printf(__VA_ARGS__); printf("\n")

static pthread_t fscache_thread;
static void *fscache_run(void *);
static char fscachedir[PATH_MAX + 1];

int fscache_start(const char *cachedir)
{
    memset(fscachedir, 0, (PATH_MAX + 1) * sizeof(char));
    strncpy(fscachedir, cachedir, PATH_MAX);
    pthread_create(&fscache_thread, NULL, fscache_run, NULL);
    return 0;
}

int fscache_stop(void)
{
    pthread_join(fscache_thread, NULL);
    return 0;
}

int fscache_create(const char *uuid, int *fd)
{
    int rc;
    char path[PATH_MAX + 1];

    memset(path, 0, (PATH_MAX + 1) * sizeof(char));
    snprintf(path, PATH_MAX, "%s%s", fscachedir, uuid);
        
    rc = creat(path, (mode_t)0600);
    *fd = rc;
    if(rc > 0) {
        rc = 0;
    } else {
        rc = -errno;
    }
    
    return rc;
}

int fscache_open(const char *uuid, int flags, int *fd)
{
    int rc;
    char path[PATH_MAX + 1];

    memset(path, 0, (PATH_MAX + 1) * sizeof(char));
    snprintf(path, PATH_MAX, "%s%s", fscachedir, uuid);

    LOG("opening: %s", path);
    rc = open(path, flags);
    *fd = rc;
    if(rc > 0) {
        rc = 0;
    } else {
        rc = -errno;
    }
    
    return rc;
}

int fscache_close(int fd)
{
    int rc;
    if(fd >= 0) {
        rc = close(fd);
        if(rc < 0) {
            rc = -errno;
        }
    } else {
        rc = -EIO;
    }
    return rc;
}

int fscache_read(int fd, fscache_read_cb_t *cb, off_t off, size_t len)
{
    int rc;
    void *buf;
    off_t noff;
    size_t l;

    LOG("off=%lld, len=%lld", (long long int)off, (long long int)len);
    buf = malloc(len);
    if(NULL == buf) {
        return -1;
    }

    noff = lseek(fd, off, SEEK_SET);
    if(noff != off) {
        LOG("unable to seek");
        free(buf);
        return -1;
    }

    LOG("read %d, %p, %lld", fd, buf, (long long int)len);
    rc = read(fd, buf, len);
    if(rc < 0) {
        LOG("errno: %d", errno);
        free(buf);
        return -1;
    }
    l = rc;
    rc = cb(buf, l);
    free(buf);
    if(rc != 0) {
        return -1;
    }

    return 0;
}

int fscache_write(int fd, fscache_write_cb_t *cb, const void *buf, off_t off,
        size_t len)
{
    int rc;

    rc = lseek(fd, off, SEEK_SET);
    rc = write(fd, buf, len);
    rc = cb(buf, len);
    (void)rc;

    return 0;
}

int fscache_rm(const char *uuid)
{
    int rc;
    char path[PATH_MAX + 1];

    memset(path, 0, (PATH_MAX + 1) * sizeof(char));
    snprintf(path, PATH_MAX, "%s%s", fscachedir, uuid);

    rc = unlink(path);
    if(rc != 0) {
        rc = -errno;
    }

    return rc;
}

int fscache_size(int fd, size_t *sz)
{
    int rc;
    struct stat st;

    rc = fstat(fd, &st);
    if(0 == rc) {
        *sz = st.st_size;
    } else {
        rc = -errno;
    }
    return rc;
}

static void *fscache_run(void *opaque)
{
    (void)opaque;
    return NULL;
}

