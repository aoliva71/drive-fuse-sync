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

#ifndef _DBCACHE_H_
#define _DBCACHE_H_

#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef int (dbcache_cb_t)(int64_t, const char *, const char *, int, size_t,
        mode_t, const struct timespec *, const struct timespec *,
        const struct timespec *, const char *, int64_t);

int dbcache_open(const char *);
int dbcache_close(void);

int dbcache_setup_schema(void);
int dbcache_setup(void);

int dbcache_auth_load(char *, size_t, char *, size_t, char *, size_t, int *,
        time_t *);
int dbcache_auth_store(const char *, const char *, const char *, int,
        const time_t *);

int dbcache_update(const char *, const char *, int, int64_t,
                const struct timespec *, const struct timespec *,
                const char *, const char *);

int dbcache_mkdir(const char *, mode_t, dbcache_cb_t *);
int dbcache_rmdir(const char *, dbcache_cb_t *);

/*int dbcache_creat(int64_t *, const char *, const char *, size_t, mode_t,
        int, const char *, int64_t);*/

int dbcache_findbypath(const char *, dbcache_cb_t *);
int dbcache_listdir(const char *, dbcache_cb_t *);

/*int dbcache_rename(int64_t, const char *);
int dbcache_chmod(int64_t, mode_t);
int dbcache_resize(int64_t, size_t);
int dbcache_chatime(int64_t, const struct timespec *);
int dbcache_chmtime(int64_t, const struct timespec *);*/
/*int dbcache_rm(const char *, int64_t, dbcache_cb_t *);
int dbcache_delete(int64_t);

int dbcache_addref(int64_t);
int dbcache_rmref(int64_t);

int dbcache_path(int64_t, char *, size_t);*/

int dbcache_change_load(char *, size_t);
int dbcache_change_store(const char *);

#endif /* _DBCACHE_H_ */

