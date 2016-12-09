
#ifndef _DBCACHE_H_
#define _DBCACHE_H_

#include <sys/types.h>

int dbcache_open(const char *);
int dbcache_close(void);

int dbcache_updatepasswd(char *, size_t);

#endif /* _DBCACHE_H_ */

