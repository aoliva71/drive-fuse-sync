
#ifndef _DBCACHE_H_
#define _DBCACHE_H_

#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef int (dbcache_cb_t)(int64_t, const char *, const char *, int, size_t,
        mode_t, const struct timespec *, const struct timespec *,
        const struct timespec *, int, const char *, int64_t);

int dbcache_open(const char *);
int dbcache_close(void);

int dbcache_updatepasswd(char *, size_t);

int dbcache_createdir(int64_t *, const char *, const char *, mode_t, int,
        const char *, int64_t);
int dbcache_createfile(int64_t *, const char *, const char *, size_t, mode_t,
        int, const char *, int64_t);

int dbcache_pinpoint(int64_t, dbcache_cb_t *);
int dbcache_lookup(const char *, int64_t, dbcache_cb_t *);
int dbcache_browse(int64_t, int64_t, dbcache_cb_t *);

int dbcache_renameentry(int64_t, const char *);
int dbcache_modifymode(int64_t, mode_t);
int dbcache_modifysize(int64_t, size_t);
int dbcache_deleteentry(int64_t);

int dbcache_path(int64_t, char *, size_t);

#endif /* _DBCACHE_H_ */

