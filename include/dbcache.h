
#ifndef _DBCACHE_H_
#define _DBCACHE_H_

#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef int (dbcache_cb_t)(int64_t, const char *, const char *, int, size_t,
        mode_t, const struct timespec *, const struct timespec *,
        const struct timespec *, int, int, const char *, int64_t);

int dbcache_open(const char *);
int dbcache_close(void);

int dbcache_setup(void);

int dbcache_createdir(int64_t *, const char *, const char *, mode_t, int,
        const char *, int64_t);
int dbcache_createfile(int64_t *, const char *, const char *, size_t, mode_t,
        int, const char *, int64_t);

int dbcache_pinpoint(int64_t, dbcache_cb_t *);
int dbcache_lookup(const char *, int64_t, dbcache_cb_t *);
int dbcache_browse(int *, int64_t, int64_t, dbcache_cb_t *);

int dbcache_rename(int64_t, const char *);
int dbcache_chmod(int64_t, mode_t);
int dbcache_resize(int64_t, size_t);
int dbcache_chatime(int64_t, const struct timespec *);
int dbcache_chmtime(int64_t, const struct timespec *);
int dbcache_rmdir(const char *, int64_t, dbcache_cb_t *);
int dbcache_rm(const char *, int64_t, dbcache_cb_t *);
int dbcache_delete(int64_t);

int dbcache_addref(int64_t);
int dbcache_rmref(int64_t);

int dbcache_path(int64_t, char *, size_t);

#endif /* _DBCACHE_H_ */

