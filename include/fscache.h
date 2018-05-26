This file is part of drive-fuse-sync.

drive-fuse-sync is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

drive-fuse-sync is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with drive-fuse-sync.  If not, see <http://www.gnu.org/licenses/>.

#ifndef _FSCACHE_H_
#define _FSCACHE_H_

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

int fscache_setup(const char *);
int fscache_cleanup(void);

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

