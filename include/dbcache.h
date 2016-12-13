
#ifndef _DBCACHE_H_
#define _DBCACHE_H_

#include <sys/stat.h>
#include <sys/types.h>

int dbcache_open(const char *);
int dbcache_close(void);

int dbcache_updatepasswd(char *, size_t);

int dbcache_createdir(const char *, const char *, mode_t, int, const char *,
        const char *);
int dbcache_createfile(const char *, const char *, size_t, mode_t,
        int, const char *, const char *);

int dbcache_find(const char *, char *, size_t, size_t *, mode_t *);
int dbcache_browse(const char *, void *);

int dbcache_renameentry(const char *, const char *);
int dbcache_modifymode(const char *, mode_t);
int dbcache_modifysize(const char *, size_t);
int dbcache_deleteentry(const char *);

#endif /* _DBCACHE_H_ */

