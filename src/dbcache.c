
#include "dbcache.h"

#include <errno.h>
#include <limits.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#ifdef HAVE_CONFIG
#include "config.h"
#else
#define VERSION "0.0"
#endif

static sqlite3 *sql = NULL;

static sqlite3_stmt *tupdate = NULL;
static sqlite3_stmt *tselect = NULL;

static sqlite3_stmt *dpinpoint = NULL;
static sqlite3_stmt *dupdate = NULL;

static sqlite3_stmt *iinsert = NULL;
static sqlite3_stmt *irename = NULL;
static sqlite3_stmt *idelete = NULL;
static sqlite3_stmt *ipinpoint = NULL;
static sqlite3_stmt *ilookup = NULL;
static sqlite3_stmt *ibrowse = NULL;
static sqlite3_stmt *ichmod = NULL;
static sqlite3_stmt *iresize = NULL;
static sqlite3_stmt *ichatime = NULL;
static sqlite3_stmt *ichmtime = NULL;

static void ts2r(double *, const struct timespec *);
static void r2ts(struct timespec *, double);

#include <stdarg.h>
#define LOG(...) printf(__VA_ARGS__); printf("\n")

int dbcache_open(const char *path)
{
    int rc;

    rc = sqlite3_open(path, &sql);
    if(rc != SQLITE_OK) {
        syslog(LOG_ERR, "unable to open db file %s", path);
        exit(1);
    }

    return 0;
}

int dbcache_close(void)
{
    sqlite3_close(sql);
    return 0;
}

int dbcache_setup_schema(void)
{
    int rc;
    sqlite3_stmt *sel;
    sqlite3_stmt *ins;
    int vfound;
    int schemaversion;

    sscanf(VERSION, "%d.%*s", &schemaversion);

    sqlite3_exec(sql, "CREATE TABLE IF NOT EXISTS dfs_version ( "
            "version INTEGER NOT NULL )", NULL, NULL, NULL);
    sqlite3_prepare_v2(sql, "SELECT version FROM dfs_version", -1, &sel, NULL);
    rc = sqlite3_step(sel);
    if(SQLITE_DONE == rc) {
        rc = sqlite3_prepare_v2(sql, "INSERT INTO dfs_version ( version ) "
            "VALUES ( ? )", -1, &ins, NULL);
        rc = sqlite3_bind_int(ins, 1, schemaversion);
        rc = sqlite3_step(ins);
        sqlite3_finalize(ins);
    } else if(SQLITE_ROW == rc) {
        vfound = sqlite3_column_int(sel, 0);
        if(vfound != schemaversion) {
            syslog(LOG_ERR, "version mismatch");
            exit(1);
        }
    } else {
        syslog(LOG_ERR, "unable to read version");
        exit(1);
    }
    sqlite3_finalize(sel);

    sqlite3_exec(sql, "CREATE TABLE IF NOT EXISTS dfs_entry ( "
            "id INTEGER NOT NULL PRIMARY KEY, "
            "uuid TEXT, "
            "name TEXT NOT NULL, "
            "type INTEGER NOT NULL, "
            "size INTEGER NOT NULL, "
            "mode INTEGER NOT NULL, "
            "atime REAL NOT NULL, "
            "mtime REAL NOT NULL, "
            "ctime REAL NOT NULL, "
            "sync INTEGER NOT NULL, "
            "checksum TEXT, "
            "parent INTEGER, "
            "FOREIGN KEY ( parent ) REFERENCES dfs_entry ( id ) "
            "ON DELETE CASCADE "
            ")", NULL, NULL, NULL);
    sqlite3_exec(sql, "CREATE INDEX IF NOT EXISTS dfs_entry_uuid "
            "ON dfs_entry ( uuid )", NULL, NULL, NULL);
    sqlite3_exec(sql, "CREATE INDEX IF NOT EXISTS dfs_entry_parent "
            "ON dfs_entry ( parent )", NULL, NULL, NULL);
    sqlite3_exec(sql, "CREATE TABLE IF NOT EXISTS dfs_token ( "
            "id INTEGER NOT NULL PRIMARY KEY, "
            "token_type TEXT NOT NULL, "
            "access_token TEXT NOT NULL, "
            "refresh_token TEXT NOT NULL, "
            "expires_in INTEGER NOT NULL, "
            "ts INTEGER NOT NULL "
            ")", NULL, NULL, NULL);

    sqlite3_prepare_v2(sql, "SELECT uuid FROM dfs_entry WHERE parent IS NULL",
            -1, &sel, NULL);
    rc = sqlite3_step(sel);
    if(SQLITE_DONE == rc) {
        rc = sqlite3_exec(sql, "INSERT INTO dfs_entry ( name, type, size, "
            "mode, atime, mtime, ctime, sync ) "
            "VALUES ( '/', 1, 0, 448, "
            "strftime('%s', 'now'), strftime('%s', 'now'), "
            "strftime('%s', 'now'), 0 )",
            NULL, NULL, NULL);
    }
    sqlite3_finalize(sel);

    sqlite3_prepare_v2(sql, "SELECT * FROM dfs_token",
            -1, &sel, NULL);
    rc = sqlite3_step(sel);
    if(SQLITE_DONE == rc) {
        rc = sqlite3_exec(sql, "INSERT INTO dfs_token ( token_type, "
            "access_token, refresh_token, expires_in, ts ) "
            "VALUES ( '(invalid)', '(invalid)', '(invalid)', 0, 0 )",
            NULL, NULL, NULL);
    }
    sqlite3_finalize(sel);

    return 0;
}

int dbcache_setup(void)
{
    /* tokens */
    sqlite3_prepare_v2(sql, "UPDATE dfs_token SET token_type = ?, "
        "access_token = ?, refresh_token = ?, expires_in = ?, ts = ?",
        -1, &tupdate, NULL);

    sqlite3_prepare_v2(sql, "SELECT token_type, access_token, refresh_token, "
        "expires_in, ts FROM dfs_token",
        -1, &tselect, NULL);

    /* drive */
    sqlite3_prepare_v2(sql, "SELECT id, name, type, size, mode, "
        "atime, mtime, ctime, sync, checksum, parent "
        "FROM dfs_entry WHERE uuid = ?", -1, &dpinpoint,
        NULL);

    sqlite3_prepare_v2(sql, "UPDATE dfs_entry SET uuid = ?, parent = ? "
        "WHERE id = ?", -1, &dupdate, NULL);

    /* entries */
    sqlite3_prepare_v2(sql, "INSERT INTO dfs_entry ( uuid, name, "
        "type, size, mode, atime, mtime, ctime, sync, checksum, "
        "parent ) VALUES ( ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ? )",
        -1, &iinsert, NULL);

    sqlite3_prepare_v2(sql, "UPDATE dfs_entry SET name = ? WHERE id = ? ",
        -1, &irename, NULL);

    sqlite3_prepare_v2(sql, "DELETE FROM dfs_entry WHERE id = ? ",
        -1, &idelete, NULL);

    sqlite3_prepare_v2(sql, "SELECT id, uuid, name, type, size, mode, "
        "atime, mtime, ctime, sync, checksum, parent "
        "FROM dfs_entry WHERE name = ? AND parent = ?", -1, &ipinpoint,
        NULL);

    sqlite3_prepare_v2(sql, "SELECT id, uuid, type, size, mode, "
        "atime, mtime, ctime, sync, checksum "
        "FROM dfs_entry WHERE name = ? AND parent = ?", -1, &ilookup,
        NULL);

    sqlite3_prepare_v2(sql, "SELECT id, uuid, name, type, size, mode, "
        "atime, mtime, ctime, sync, checksum "
        "FROM dfs_entry "
        "WHERE parent = ? AND id > ? "
        "ORDER BY id",
        -1, &ibrowse, NULL);

    sqlite3_prepare_v2(sql, "UPDATE dfs_entry SET mode = ? WHERE id = ? ",
        -1, &ichmod, NULL);

    sqlite3_prepare_v2(sql, "UPDATE dfs_entry SET size = ? WHERE id = ? ",
        -1, &iresize, NULL);

    sqlite3_prepare_v2(sql, "UPDATE dfs_entry SET atime = ? WHERE id = ? ",
        -1, &ichatime, NULL);

    sqlite3_prepare_v2(sql, "UPDATE dfs_entry SET mtime = ? WHERE id = ? ",
        -1, &ichmtime, NULL);

    return 0;
}

int dbcache_auth_load(char *token_type, size_t ttlen, char *access_token,
        size_t atlen, char *refresh_token, size_t rtlen, int *expires_in,
        time_t *expiration_time)
{
    int rc;
    const char *ctmp;
    int itmp;

    rc = sqlite3_reset(tselect);
    rc = sqlite3_step(tselect);
    if(rc != SQLITE_ROW) {
        return -1;
    }
    ctmp = (const char *)sqlite3_column_text(tselect, 0);
    strncpy(token_type, ctmp, ttlen);
    ctmp = (const char *)sqlite3_column_text(tselect, 1);
    strncpy(access_token, ctmp, atlen);
    ctmp = (const char *)sqlite3_column_text(tselect, 2);
    strncpy(refresh_token, ctmp, rtlen);
    itmp = sqlite3_column_int(tselect, 3);
    *expires_in = itmp;
    itmp = sqlite3_column_int(tselect, 4);
    *expiration_time = (time_t)itmp;

    return 0;
}

int dbcache_auth_store(const char *token_type, const char *access_token,
        const char *refresh_token, int expires_in,
        const time_t *expiration_time)
{
    int rc;
    int itmp;

    rc = sqlite3_reset(tupdate);
    rc = sqlite3_bind_text(tupdate, 1, token_type, -1, NULL);
    rc = sqlite3_bind_text(tupdate, 2, access_token, -1, NULL);
    rc = sqlite3_bind_text(tupdate, 3, refresh_token, -1, NULL);
    rc = sqlite3_bind_int(tupdate, 4, expires_in);
    itmp = (int)*expiration_time;
    rc = sqlite3_bind_int(tupdate, 5, itmp);
    rc = sqlite3_step(tupdate);

    return SQLITE_DONE == rc ? 0 : -1;
}

/*int dbcache_update(const char *uuid, const char *name, int isdir, int64_t size,
                const struct timespec *mtime, const struct timespec *ctime,
                const char *cksum, const char *parent)
{
    int rc;
    int64_t id;
    int type;
    int mode;
    double datime, dmtime, dctime;
    int sync;
    int64_t parentid;

    if(strlen(parent)) {
        rc = sqlite3_reset(dpinpoint);
        rc = sqlite3_bind_text(dpinpoint, 1, uuid, -1, NULL);
        rc = sqlite3_step(dpinpoint);
        if(SQLITE_ROW == rc) {
            * uuid found, updating *
            id = (int64_t)sqlite3_column_int64(dpinpoint, 0);

            rc = sqlite3_reset(dpinpoint);
            rc = sqlite3_bind_text(dpinpoint, 1, parent, -1, NULL);
            rc = sqlite3_step(dpinpoint);
            if(rc != SQLITE_ROW) {
                return -1;
            }
            parentid = (int64_t)sqlite3_column_int64(dpinpoint, 0);

            rc = sqlite3_reset(dupdate);
            rc = sqlite3_bind_text(dupdate, 1, uuid, -1, NULL);
            rc = sqlite3_bind_int64(dupdate, 2, (sqlite3_int64)parentid);
            rc = sqlite3_bind_int64(dupdate, 3, (sqlite3_int64)id);
            rc = sqlite3_step(dupdate);

            return SQLITE_DONE == rc ? 0 : -1;
        } else if (SQLITE_DONE == rc) {
            * uuid not found, inserting *
            rc = sqlite3_reset(dpinpoint);
            rc = sqlite3_bind_text(dpinpoint, 1, parent, -1, NULL);
            rc = sqlite3_step(dpinpoint);
            if(rc != SQLITE_ROW) {
                return -1;
            }
            parentid = (int64_t)sqlite3_column_int64(dpinpoint, 0);

            rc = sqlite3_reset(iinsert);
            rc = sqlite3_bind_text(iinsert, 1, uuid, -1, NULL);
            rc = sqlite3_bind_text(iinsert, 2, name, -1, NULL);
            type = isdir ? 1 : 2;
            rc = sqlite3_bind_int(iinsert, 3, type);
            rc = sqlite3_bind_int64(iinsert, 4, size);
            mode = isdir ? 0700 : 0600;
            rc = sqlite3_bind_int(iinsert, 5, mode);
            ts2r(&dmtime, mtime);
            datime = dmtime;
            rc = sqlite3_bind_double(iinsert, 6, datime);
            rc = sqlite3_bind_double(iinsert, 7, dmtime);
            ts2r(&dctime, ctime);
            rc = sqlite3_bind_double(iinsert, 8, dctime);
            sync = 1;
            rc = sqlite3_bind_int(iinsert, 9, sync);
            rc = sqlite3_bind_text(iinsert, 10, cksum, -1, NULL);
            rc = sqlite3_bind_int64(iinsert, 11, parentid);

            rc = sqlite3_step(iinsert);

            return SQLITE_DONE == rc ? 0 : -1;
        } else {
            return -1;
        }
    } else {
        * updating root *
        id = 1LL;
        rc = sqlite3_reset(dupdate);
        rc = sqlite3_bind_text(dupdate, 1, uuid, -1, NULL);
        rc = sqlite3_bind_null(dupdate, 2);
        rc = sqlite3_bind_int64(dupdate, 3, (sqlite3_int64)id);
        rc = sqlite3_step(dupdate);
        return SQLITE_DONE == rc ? 0 : -1;
    }
    return 0;
}*/

int dbcache_createdir(const char *cpath, mode_t mode, dbcache_cb_t *cb)
{
    char path[PATH_MAX + 1];
    const char *pbegin;
    char *pend;
    int rc;
    int64_t id;
    const char *uuid;
    const char *name;
    int type;
    size_t size;
    mode_t _mode;
    double r;
    struct timespec ts, atime, mtime, ctime;
    double atimed, mtimed, ctimed;
    int sync;
    const char *checksum;
    int64_t parent;

    memset(path, 0, (PATH_MAX + 1) * sizeof(char));
    strncpy(path, cpath, PATH_MAX);
    pbegin = path;
    pbegin++;
    parent = 1;     /* root */
    for(;;) {
        pend = strchr(pbegin, '/');
        if(pend) {
            *pend = 0;
        } else {
            rc = sqlite3_reset(iinsert);
            rc = sqlite3_bind_text(iinsert, 1, uuid, -1, NULL);
            rc = sqlite3_bind_text(iinsert, 2, name, -1, NULL);
            type = 1;
            rc = sqlite3_bind_int(iinsert, 3, type);
            size = 0;
            rc = sqlite3_bind_int64(iinsert, 4, size);
            rc = sqlite3_bind_int(iinsert, 5, mode);
            clock_gettime(CLOCK_REALTIME, &ts);
            ts2r(&atimed, &ts);
            rc = sqlite3_bind_double(iinsert, 6, atimed);
            mtimed = atimed;
            rc = sqlite3_bind_double(iinsert, 7, mtimed);
            ctimed = atimed;
            rc = sqlite3_bind_double(iinsert, 8, ctimed);
            sync = 0;
            rc = sqlite3_bind_int(iinsert, 9, sync);
            rc = sqlite3_bind_null(iinsert, 10);
            rc = sqlite3_bind_int64(iinsert, 11, parent);
            rc = sqlite3_step(iinsert);

            rc = (SQLITE_DONE == rc) ? 0 : -EIO;
            break;
        }

        rc = sqlite3_reset(ipinpoint);
        rc = sqlite3_bind_text(ipinpoint, 1, pbegin, -1, NULL);
        rc = sqlite3_bind_int64(ipinpoint, 2, parent);
        rc = sqlite3_step(ipinpoint);
        if(rc != SQLITE_ROW) {
            return -ENOENT;
        }
        id = (int64_t)sqlite3_column_int64(ipinpoint, 0);
        uuid = (const char *)sqlite3_column_text(ipinpoint, 1);
        name = (const char *)sqlite3_column_text(ipinpoint, 2);
        type = sqlite3_column_int(ipinpoint, 3);
        size = sqlite3_column_int64(ipinpoint, 4);
        _mode = sqlite3_column_int(ipinpoint, 5);
        r = sqlite3_column_double(ipinpoint, 6);
        r2ts(&atime, r);
        r = sqlite3_column_double(ipinpoint, 7);
        r2ts(&mtime, r);
        r = sqlite3_column_double(ipinpoint, 8);
        r2ts(&ctime, r);
        sync = sqlite3_column_int(ipinpoint, 9);
        checksum = (const char *)sqlite3_column_text(ipinpoint, 10);
        parent = sqlite3_column_int64(ipinpoint, 11);
        if(!pend) {
            rc = cb(id, uuid, name, type, size, _mode, &atime, &mtime, &ctime,
                    checksum, parent);
            break;
        }
    }
    return rc;
}

/*int dbcache_createfile(const char *path, size_t size,
        mode_t mode, int sync)
{
    char path[PATH_MAX + 1];
    const char *pbegin;
    char *pend;
    int rc;
    int64_t id;
    const char *uuid;
    const char *name;
    int type;
    size_t size;
    mode_t mode;
    double r;
    struct timespec atime, mtime, ctime;
    double atimed, mtimed, ctimed;
    int sync;
    int refcount;
    const char *checksum;
    int64_t parent;

    memset(path, 0, (PATH_MAX + 1) * sizeof(char));
    strncpy(path, cpath, PATH_MAX);
    pbegin = path;
    pbegin++;
    parent = 1;     * root *
    for(;;) {
        pend = strchr(pbegin, '/');
        if(pend) {
            *pend = 0;
        } else {
            rc = sqlite3_reset(iinsert);
            rc = sqlite3_bind_text(iinsert, 1, uuid, -1, NULL);
            rc = sqlite3_bind_text(iinsert, 2, name, -1, NULL);
            type = 1;
            rc = sqlite3_bind_int(iinsert, 3, type);
            size = 0;
            rc = sqlite3_bind_int64(iinsert, 4, size);
            rc = sqlite3_bind_int(iinsert, 5, mode);
            clock_gettime(CLOCK_REALTIME, &ts);
            ts2r(&atimed, &ts);
            rc = sqlite3_bind_double(iinsert, 6, atimed);
            mtimed = atimed;
            rc = sqlite3_bind_double(iinsert, 7, mtimed);
            ctimed = atimed;
            rc = sqlite3_bind_double(iinsert, 8, ctimed);
            sync = 0;
            rc = sqlite3_bind_int(iinsert, 9, sync);
            rc = sqlite3_bind_null(iinsert, 10);
            rc = sqlite3_bind_int64(iinsert, 11, parent);
            rc = sqlite3_step(iinsert);

            *id = (int64_t)sqlite3_last_insert_rowid(sql);
            rc = SQLITE_DONE == rc ? 0 : -EIO;
            break;
        }

        rc = sqlite3_reset(ipinpoint);
        rc = sqlite3_bind_text(ipinpoint, 1, pbegin);
        rc = sqlite3_bind_int64(ipinpoint, 2, parent);
        rc = sqlite3_step(ipinpoint);
        if(rc != SQLITE_ROW) {
            return -ENOENT;
        }
        id = sqlite3_column_int64(ipinpoint, 0);
        uuid = (const char *)sqlite3_column_text(ipinpoint, 1);
        name = (const char *)sqlite3_column_text(ipinpoint, 2);
        type = sqlite3_column_int(ipinpoint, 3);
        size = sqlite3_column_int64(ipinpoint, 4);
        mode = sqlite3_column_int(ipinpoint, 5);
        r = sqlite3_column_double(ipinpoint, 6);
        r2ts(&atime, r);
        r = sqlite3_column_double(ipinpoint, 7);
        r2ts(&mtime, r);
        r = sqlite3_column_double(ipinpoint, 8);
        r2ts(&ctime, r);
        sync = sqlite3_column_int(ipinpoint, 9);
        checksum = (const char *)sqlite3_column_text(ipinpoint, 10);
        parent = sqlite3_column_int64(ipinpoint, 11);
        if(!pend) {
            rc = cb(id, uuid, name, type, size, mode, &atime, &mtime, &ctime,
                    sync, checksum, parent);
            break;
        }
    }
    return rc;
}*/

int dbcache_pinpoint(const char *cpath, dbcache_cb_t *cb)
{
    char path[PATH_MAX + 1];
    const char *pbegin;
    char *pend;
    int rc;
    int64_t id;
    const char *uuid;
    const char *name;
    int type;
    size_t size;
    mode_t mode;
    double r;
    struct timespec atime, mtime, ctime;
    const char *checksum;
    int64_t parent;

    memset(path, 0, (PATH_MAX + 1) * sizeof(char));
    strncpy(path, cpath, PATH_MAX);
    pbegin = path;
    pbegin++;
    parent = 1;     /* root */
    for(;;) {
        pend = strchr(pbegin, '/');
        if(pend) {
            *pend = 0;
        }
        rc = sqlite3_reset(ipinpoint);
        rc = sqlite3_bind_text(ipinpoint, 1, pbegin, -1, NULL);
        rc = sqlite3_bind_int64(ipinpoint, 2, parent);
        rc = sqlite3_step(ipinpoint);
        if(rc != SQLITE_ROW) {
            return -ENOENT;
        }
        id = sqlite3_column_int64(ipinpoint, 0);
        uuid = (const char *)sqlite3_column_text(ipinpoint, 1);
        name = (const char *)sqlite3_column_text(ipinpoint, 2);
        type = sqlite3_column_int(ipinpoint, 3);
        size = sqlite3_column_int64(ipinpoint, 4);
        mode = sqlite3_column_int(ipinpoint, 5);
        r = sqlite3_column_double(ipinpoint, 6);
        r2ts(&atime, r);
        r = sqlite3_column_double(ipinpoint, 7);
        r2ts(&mtime, r);
        r = sqlite3_column_double(ipinpoint, 8);
        r2ts(&ctime, r);
        checksum = (const char *)sqlite3_column_text(ipinpoint, 9);
        parent = sqlite3_column_int64(ipinpoint, 10);
        if(!pend) {
            rc = cb(id, uuid, name, type, size, mode, &atime, &mtime, &ctime,
                    checksum, parent);
            break;
        }
    }
    return rc;
}

/*int dbcache_lookup(const char *name, int64_t parent, dbcache_cb_t *cb)
{
    int rc;
    int64_t id;
    const char *uuid;
    int type;
    size_t size;
    mode_t mode;
    double r;
    struct timespec atime, mtime, ctime;
    int sync;
    int refcount;
    const char *checksum;
 
    rc = sqlite3_reset(ilookup);
    rc = sqlite3_bind_text(ilookup, 1, name, -1, NULL);
    rc = sqlite3_bind_int64(ilookup, 2, parent);
    rc = sqlite3_step(ilookup);
    if(rc != SQLITE_ROW) {
        return -1;
    }
    
    id = sqlite3_column_int64(ilookup, 0);
    uuid = (const char *)sqlite3_column_text(ilookup, 1);
    type = sqlite3_column_int(ilookup, 2);
    size = sqlite3_column_int64(ilookup, 3);
    mode = sqlite3_column_int(ilookup, 4);
    r = sqlite3_column_double(ipinpoint, 5);
    r2ts(&atime, r);
    r = sqlite3_column_double(ipinpoint, 6);
    r2ts(&mtime, r);
    r = sqlite3_column_double(ipinpoint, 7);
    r2ts(&ctime, r);
    sync = sqlite3_column_int(ilookup, 8);
    refcount = sqlite3_column_int(ilookup, 9);
    checksum = (const char *)sqlite3_column_text(ilookup, 10);

    rc = cb(id, uuid, name, type, size, mode, &atime, &mtime, &ctime, sync,
            refcount, checksum, parent);

    return rc;
}*/


int dbcache_browse(int *err, int64_t parent, int64_t first, dbcache_cb_t *cb)
{
    int rc;
    int64_t id;
    const char *uuid;
    const char *name;
    int type;
    size_t size;
    mode_t mode;
    double r;
    struct timespec atime, mtime, ctime;
    const char *checksum;

    rc = sqlite3_reset(ibrowse);
    rc = sqlite3_bind_int64(ibrowse, 1, parent);
    rc = sqlite3_bind_int64(ibrowse, 2, first);
    rc = sqlite3_step(ibrowse);
    if(SQLITE_ROW == rc) {
        id = sqlite3_column_int64(ibrowse, 0);
        uuid = (const char *)sqlite3_column_text(ibrowse, 1);
        name = (const char *)sqlite3_column_text(ibrowse, 2);
        type = sqlite3_column_int(ibrowse, 3);
        size = sqlite3_column_int64(ibrowse, 4);
        mode = sqlite3_column_int(ibrowse, 5);
        r = sqlite3_column_double(ipinpoint, 6);
        r2ts(&atime, r);
        r = sqlite3_column_double(ipinpoint, 7);
        r2ts(&mtime, r);
        r = sqlite3_column_double(ipinpoint, 8);
        r2ts(&ctime, r);
        checksum = (const char *)sqlite3_column_text(ibrowse, 9);

        rc = cb(id, uuid, name, type, size, mode, &atime, &mtime, &ctime,
                checksum, parent);
        if(rc != 0) {
            return -1;
        }
    } else {
        *err = 1;
    }

    return 0;
}

/*int dbcache_rename(int64_t id, const char *name)
{
    int rc;

    rc = sqlite3_reset(irename);
    rc = sqlite3_bind_text(irename, 1, name, -1, NULL);
    rc = sqlite3_bind_int64(irename, 2, id);
    rc = sqlite3_step(irename);

    return rc;
}

int dbcache_chmod(int64_t id, mode_t mode)
{
    int rc;

    rc = sqlite3_reset(ichmod);
    mode &= 0777;
    rc = sqlite3_bind_int(ichmod, 1, mode);
    rc = sqlite3_bind_int64(ichmod, 2, id);
    rc = sqlite3_step(ichmod);

    return rc;
}

int dbcache_resize(int64_t id, size_t size)
{
    int rc;

    rc = sqlite3_reset(iresize);
    rc = sqlite3_bind_int64(iresize, 1, size);
    rc = sqlite3_bind_int64(iresize, 2, id);
    rc = sqlite3_step(iresize);

    return rc;
}

int dbcache_chatime(int64_t id, const struct timespec *tv)
{
    int rc;
    double a;

    rc = sqlite3_reset(ichatime);
    ts2r(&a, tv);
    rc = sqlite3_bind_double(ichatime, 1, a);
    rc = sqlite3_bind_int64(ichatime, 2, id);
    rc = sqlite3_step(ichatime);

    return SQLITE_DONE == rc ? 0 : -1;
}

int dbcache_chmtime(int64_t id, const struct timespec *tv)
{
    int rc;
    double m;

    rc = sqlite3_reset(ichmtime);
    ts2r(&m, tv);
    rc = sqlite3_bind_double(ichmtime, 1, m);
    rc = sqlite3_bind_int64(ichmtime, 2, id);
    rc = sqlite3_step(ichmtime);

    return SQLITE_DONE == rc ? 0 : -1;
}

int dbcache_delete(int64_t id)
{
    int rc;

    rc = sqlite3_reset(idelete);
    rc = sqlite3_bind_int64(idelete, 1, id);
    rc = sqlite3_step(idelete);

    return rc;
}

int dbcache_rm(const char *name, int64_t parent, dbcache_cb_t *cb)
{
    int rc;
    sqlite3_int64 id;
    const char *uuid;
    int type;
    size_t size;
    mode_t mode;
    double r;
    struct timespec atime, mtime, ctime;
    int sync;
    int refcount;
    const char *checksum;

    rc = sqlite3_reset(ilookup);
    rc = sqlite3_bind_text(ilookup, 1, name, -1, NULL);
    rc = sqlite3_bind_int64(ilookup, 2, parent);
    rc = sqlite3_step(ilookup);
    if(rc != SQLITE_ROW) {
        * noentry *
        return -1;
    }

    id = sqlite3_column_int64(ilookup, 0);
    uuid = (const char *)sqlite3_column_text(ilookup, 1);
    type = sqlite3_column_int(ilookup, 2);
    if(type != 2) {
        * notfile *
        return -1;
    }
    size = sqlite3_column_int64(ilookup, 3);
    mode = sqlite3_column_int(ilookup, 4);
    r = sqlite3_column_double(ipinpoint, 5);
    r2ts(&atime, r);
    r = sqlite3_column_double(ipinpoint, 6);
    r2ts(&mtime, r);
    r = sqlite3_column_double(ipinpoint, 7);
    r2ts(&ctime, r);
    sync = sqlite3_column_int(ilookup, 8);
    refcount = sqlite3_column_int(ilookup, 9);
    checksum = (const char *)sqlite3_column_text(ilookup, 10);

    rc = cb(id, uuid, name, type, size, mode, &atime, &mtime, &ctime, sync,
            refcount, checksum, parent);
    if(0 == rc) {
        rc = sqlite3_reset(idelete);
        rc = sqlite3_bind_int64(idelete, 1, id);
        rc = sqlite3_step(idelete);
    }

    return SQLITE_DONE == rc ? 0 : -1;
}*/

int dbcache_rmdir(const char *name, int64_t parent, dbcache_cb_t *cb)
{
    int rc;
    sqlite3_int64 id;
    const char *uuid;
    int type;
    size_t size;
    mode_t mode;
    double r;
    struct timespec atime, mtime, ctime;
    const char *checksum;

    rc = sqlite3_reset(ilookup);
    rc = sqlite3_bind_text(ilookup, 1, name, -1, NULL);
    rc = sqlite3_bind_int64(ilookup, 2, parent);
    rc = sqlite3_step(ilookup);
    if(rc != SQLITE_ROW) {
        /* noentry */
        return -1;
    }
    
    id = sqlite3_column_int64(ilookup, 0);
    uuid = (const char *)sqlite3_column_text(ilookup, 1);
    type = sqlite3_column_int(ilookup, 2);
    size = sqlite3_column_int64(ilookup, 3);
    mode = sqlite3_column_int(ilookup, 4);
    r = sqlite3_column_double(ipinpoint, 5);
    r2ts(&atime, r);
    r = sqlite3_column_double(ipinpoint, 6);
    r2ts(&mtime, r);
    r = sqlite3_column_double(ipinpoint, 7);
    r2ts(&ctime, r);
    checksum = (const char *)sqlite3_column_text(ilookup, 8);

    rc = sqlite3_reset(ibrowse);
    rc = sqlite3_bind_int64(ibrowse, 1, id);
    rc = sqlite3_bind_int64(ibrowse, 2, 0LL);
    rc = sqlite3_step(ibrowse);
    if(SQLITE_ROW == rc) {
        /* notempty */
        return -1;
    }

    rc = cb(id, uuid, name, type, size, mode, &atime, &mtime, &ctime,
            checksum, parent);
    if(0 == rc) {
        rc = sqlite3_reset(idelete);
        rc = sqlite3_bind_int64(idelete, 1, id);
        rc = sqlite3_step(idelete);
    }

    return SQLITE_DONE == rc ? 0 : -1;
}


/*int dbcache_path(int64_t id, char *path, size_t len)
{
    int rc;
    off_t off;
    size_t l;
    int64_t parent;
    int i, hlen, flen;
    char tmp;

    * write entry *
    off = 0;
    l = snprintf(path + off, len, "%lld", (long long int)id);
    off += l;
    len -= l;
    strncpy(path + off, "/", len);
    off++;
    len--;
 
    * recursively find parents *
    for(;;) {
        rc = sqlite3_reset(ipinpoint);
        rc = sqlite3_bind_int64(ipinpoint, 1, id);
        rc = sqlite3_step(ipinpoint);
        if(rc != SQLITE_ROW) {
            return -1;
        }
        parent = sqlite3_column_int64(ipinpoint, 11);
        if(0 == parent) {
            break;
        }
        l = snprintf(path + off, len, "%lld", (long long int)parent);
        off += l;
        len -= l;
        strncpy(path + off, "/", len);
        off++;
        len--;
        id = parent;
    }

    * now reverse *
    flen = strlen(path);
    hlen = flen / 2;
    flen--;
    for(i = 0; i < hlen; i++) {
        tmp = path[i];
        path[i] = path[flen - i];
        path[flen - i] = tmp;
    }

    return 0;
}

int dbcache_addref(int64_t id)
{
    int rc;

    rc = sqlite3_reset(iaddref);
    rc = sqlite3_bind_int64(iaddref, 1, id);
    rc = sqlite3_step(iaddref);

    return rc;
}

int dbcache_rmref(int64_t id)
{
    int rc;

    rc = sqlite3_reset(irmref);
    rc = sqlite3_bind_int64(irmref, 1, id);
    rc = sqlite3_step(irmref);

    return rc;
}*/

static void ts2r(double *r, const struct timespec *tv)
{
    *r = tv->tv_sec + tv->tv_nsec / 1000000000.0;
}

static void r2ts(struct timespec *tv, double r)
{
    tv->tv_sec = (time_t)r;
    tv->tv_nsec = (r - (double)tv->tv_sec) * 1000000000L;
}

#undef LOG


