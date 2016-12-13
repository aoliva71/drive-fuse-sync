
#ifndef _DBCACHE_H_
#define _DBCACHE_H_

#include <sys/stat.h>
#include <sys/types.h>

int dbcache_open(const char *);
int dbcache_close(void);

int dbcache_updatepasswd(char *, size_t);

int dbcache_createdir(int64_t *, const char *, const char *, mode_t, int,
        const char *, const char *);
int dbcache_createfile(int64_t *, const char *, const char *, size_t, mode_t,
        int, const char *, const char *);

int dbcache_find(int64_t, char *, size_t, char *, size_t, size_t *, mode_t *);
int dbcache_browse(int64_t, void *);

int dbcache_renameentry(int64_t, const char *);
int dbcache_modifymode(int64_t, mode_t);
int dbcache_modifysize(int64_t, size_t);
int dbcache_deleteentry(int64_t);

#endif /* _DBCACHE_H_ */

