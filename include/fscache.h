
#ifndef _FSCACHE_H_
#define _FSCACHE_H_

#include <sys/types.h>

int fscache_start(const char *);
int fscache_stop(void);

int fscache_open(int64_t, int *);
int fscache_close(int);

typedef int (fscache_read_cb_t)(const void *, size_t);
int fscache_read(int, fscache_read_cb_t *, off_t, size_t);

typedef int (fscache_write_cb_t)(const void *, size_t);
int fscache_write(int, fscache_write_cb_t *);

#endif /* _FSCACHE_H_ */

