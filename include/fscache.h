
#ifndef _FSCACHE_H_
#define _FSCACHE_H_

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

int fscache_start(const char *);
int fscache_stop(void);

int fscache_create(const char *);
int fscache_open(const char *, int);
int fscache_close(int);

int fscache_read(int, char *, off_t, size_t);
int fscache_write(int, const char *, off_t, size_t);

int fscache_size(int, size_t *);
int fscache_rm(const char *);

int fscache_stat(const char *, struct stat *);
FILE *fscache_fopen(const char *);
void fscache_fclose(FILE *);

#endif /* _FSCACHE_H_ */

