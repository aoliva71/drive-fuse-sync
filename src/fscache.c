
#include "fscache.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dbcache.h"

static void *fscache_run(void *);
static char fscachedir[PATH_MAX + 1];

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

int fscache_open(int64_t id, int *fd)
{
    int rc;
    char path[PATH_MAX + 1];
    char relpath[PATH_MAX + 1];

    memset(path, 0, (PATH_MAX + 1) * sizeof(char));
    memset(relpath, 0, (PATH_MAX + 1) * sizeof(char));
    rc = dbcache_path(id, relpath, PATH_MAX);
    if(0 == rc) {
        snprintf(path, PATH_MAX, "%s%s", fscachedir, relpath);
        *fd = open(path, O_RDWR);
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

int fscache_write(int fd, fscache_write_cb_t *cb)
{
    return 0;
}

static void *fscache_run(void *opaque)
{
    (void)opaque;
}


