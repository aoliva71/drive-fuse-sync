
#include "dbcache.h"

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

#define FUSE_USE_VERSION 26
#include <fuse_lowlevel.h>

static sqlite3 *sql = NULL;
static sqlite3_stmt *eins = NULL;
static sqlite3_stmt *erename = NULL;
static sqlite3_stmt *edel = NULL;
static sqlite3_stmt *epinpoint = NULL;
static sqlite3_stmt *elookup = NULL;
static sqlite3_stmt *ebrowse = NULL;
static sqlite3_stmt *echmod = NULL;
static sqlite3_stmt *eresize = NULL;
static sqlite3_stmt *echatime = NULL;
static sqlite3_stmt *echmtime = NULL;
static int schemaversion = -1;

static void setup(void);

static void ts2r(double *, const struct timespec *);
static void r2ts(struct timespec *, double);

#include <stdarg.h>
#define LOG(...) printf(__VA_ARGS__); printf("\n")

int dbcache_open(const char *path)
{
    int rc;

    sscanf(VERSION, "%d.%*s", &schemaversion);

    rc = sqlite3_open(path, &sql);
    if(rc != SQLITE_OK) {
        syslog(LOG_ERR, "unable to open db file %s", path);
        exit(1);
    }

    setup();

    return 0;
}

int dbcache_close(void)
{
    
    sqlite3_close(sql);
    return 0;
}

int dbcache_updatepasswd(char *passwd, size_t len)
{
    int rc;
    sqlite3_stmt *sel;
    sqlite3_stmt *ins;
    sqlite3_stmt *upd;
    const unsigned char *pfound;

    if(strlen(passwd)) {
        /* store to db */
        sqlite3_prepare_v2(sql, "SELECT password FROM dfs_config", -1, &sel,
            NULL);
        rc = sqlite3_step(sel);
        if(SQLITE_DONE == rc) {
            /* no existing record, insert */
            rc = sqlite3_prepare_v2(sql, "INSERT INTO dfs_config ( password ) "
                "VALUES ( ? )", -1, &ins, NULL);
            rc = sqlite3_bind_text(ins, 1, passwd, -1, NULL);
            rc = sqlite3_step(ins);
            sqlite3_finalize(ins);
        } else if(SQLITE_ROW == rc) {
            /* existing record, update */
            rc = sqlite3_prepare_v2(sql, "UPDATE dfs_config SET password = ?",
                -1, &upd, NULL);
            rc = sqlite3_bind_text(upd, 1, passwd, -1, NULL);
            rc = sqlite3_step(upd);
            sqlite3_finalize(upd);
        } else {
            syslog(LOG_ERR, "unable to read version");
            exit(1);
        }
        sqlite3_finalize(sel);
    }

    /* load from db */
    sqlite3_prepare_v2(sql, "SELECT password FROM dfs_config", -1, &sel,
        NULL);
    rc = sqlite3_step(sel);
    if(SQLITE_ROW == rc) {
        /* read */
        pfound = sqlite3_column_text(sel, 0);
        strncpy(passwd, (char *)pfound, len);
    } else {
        syslog(LOG_ERR, "unable to read password");
        exit(1);
    }
    sqlite3_finalize(sel);

    return 0;
}

int dbcache_createdir(int64_t *id, const char *extid, const char *name, mode_t mode,
        int sync, const char *checksum, int64_t parent)
{
    int type;
    size_t size;
    int rc;
    sqlite3_int64 lid;
    struct timespec ts;
    double atime, mtime, ctime;
    
    rc = sqlite3_reset(eins);
    rc = sqlite3_bind_text(eins, 1, extid, -1, NULL);
    rc = sqlite3_bind_text(eins, 2, name, -1, NULL);
    type = 1;
    rc = sqlite3_bind_int(eins, 3, type);
    size = 0;
    rc = sqlite3_bind_int64(eins, 4, size);
    rc = sqlite3_bind_int(eins, 5, mode);
    clock_gettime(CLOCK_REALTIME, &ts);
    ts2r(&atime, &ts);
    rc = sqlite3_bind_double(eins, 6, atime);
    mtime = atime;
    rc = sqlite3_bind_double(eins, 7, mtime);
    ctime = atime;
    rc = sqlite3_bind_double(eins, 8, ctime);

    rc = sqlite3_bind_int(eins, 9, sync);
    rc = sqlite3_bind_text(eins, 10, checksum, -1, NULL);
    if(parent > 0) {
        rc = sqlite3_bind_int64(eins, 11, parent);
    } else {
        rc = sqlite3_bind_null(eins, 11);
    }
    rc = sqlite3_step(eins);

    lid = sqlite3_last_insert_rowid(sql);
    *id = lid;
    return SQLITE_DONE == rc ? 0 : -1;
}

int dbcache_createfile(int64_t *id, const char *extid, const char *name, size_t size,
        mode_t mode, int sync, const char *checksum, int64_t parent)
{
    int type;
    int rc;
    sqlite3_int64 lid;
    struct timespec ts;
    double atime, mtime, ctime;
    
    rc = sqlite3_reset(eins);
    rc = sqlite3_bind_text(eins, 1, extid, -1, NULL);
    rc = sqlite3_bind_text(eins, 2, name, -1, NULL);
    type = 2;
    rc = sqlite3_bind_int(eins, 3, type);
    rc = sqlite3_bind_int64(eins, 4, size);
    rc = sqlite3_bind_int(eins, 5, mode);
    clock_gettime(CLOCK_REALTIME, &ts);
    ts2r(&atime, &ts);
    rc = sqlite3_bind_double(eins, 6, atime);
    mtime = atime;
    rc = sqlite3_bind_double(eins, 7, mtime);
    ctime = atime;
    rc = sqlite3_bind_double(eins, 8, ctime);
    rc = sqlite3_bind_int(eins, 9, sync);
    rc = sqlite3_bind_text(eins, 10, checksum, -1, NULL);
    rc = sqlite3_bind_int64(eins, 11, parent);
    rc = sqlite3_step(eins);

    lid = sqlite3_last_insert_rowid(sql);
    *id = lid;
    return SQLITE_DONE == rc ? 0 : -1;
}

int dbcache_pinpoint(int64_t id, dbcache_cb_t *cb)
{
    int rc;
    const char *extid;
    const char *name;
    int type;
    size_t size;
    mode_t mode;
    double r;
    struct timespec atime, mtime, ctime;
    int sync;
    const char *checksum;
    int64_t parent;
 
    rc = sqlite3_reset(epinpoint);
    rc = sqlite3_bind_int64(epinpoint, 1, id);
    rc = sqlite3_step(epinpoint);
    if(rc != SQLITE_ROW) {
        return -1;
    }
    
    extid = sqlite3_column_text(epinpoint, 0);
    name = sqlite3_column_text(epinpoint, 1);
    type = sqlite3_column_int(epinpoint, 2);
    size = sqlite3_column_int64(epinpoint, 3);
    mode = sqlite3_column_int(epinpoint, 4);
    r = sqlite3_column_double(epinpoint, 5);
    r2ts(&atime, r);
    r = sqlite3_column_double(epinpoint, 6);
    r2ts(&mtime, r);
    r = sqlite3_column_double(epinpoint, 7);
    r2ts(&ctime, r);
    sync = sqlite3_column_int(epinpoint, 8);
    checksum = sqlite3_column_text(epinpoint, 9);
    parent = sqlite3_column_int64(epinpoint, 10);

    rc = cb(id, extid, name, type, size, mode, &atime, &mtime, &ctime, sync,
            checksum, parent);

    return rc;
}

int dbcache_lookup(const char *name, int64_t parent, dbcache_cb_t *cb)
{
    int rc;
    int64_t id;
    const char *extid;
    int type;
    size_t size;
    mode_t mode;
    double r;
    struct timespec atime, mtime, ctime;
    int sync;
    const char *checksum;
 
    rc = sqlite3_reset(elookup);
    rc = sqlite3_bind_text(elookup, 1, name, -1, NULL);
    rc = sqlite3_bind_int64(elookup, 2, parent);
    rc = sqlite3_step(elookup);
    if(rc != SQLITE_ROW) {
        return -1;
    }
    
    id = sqlite3_column_int64(elookup, 0);
    extid = sqlite3_column_text(elookup, 1);
    type = sqlite3_column_int(elookup, 2);
    size = sqlite3_column_int64(elookup, 3);
    mode = sqlite3_column_int(elookup, 4);
    r = sqlite3_column_double(epinpoint, 5);
    r2ts(&atime, r);
    r = sqlite3_column_double(epinpoint, 6);
    r2ts(&mtime, r);
    r = sqlite3_column_double(epinpoint, 7);
    r2ts(&ctime, r);
    sync = sqlite3_column_int(elookup, 8);
    checksum = sqlite3_column_text(elookup, 9);

    rc = cb(id, extid, name, type, size, mode, &atime, &mtime, &ctime, sync,
            checksum, parent);

    return rc;
}


int dbcache_browse(int64_t parent, int64_t first, dbcache_cb_t *cb)
{
    int rc;
    int64_t id;
    const char *extid;
    const char *name;
    int type;
    size_t size;
    mode_t mode;
    double r;
    struct timespec atime, mtime, ctime;
    int sync;
    const char *checksum;

    rc = sqlite3_reset(ebrowse);
    rc = sqlite3_bind_int64(ebrowse, 1, parent);
    rc = sqlite3_bind_int64(ebrowse, 2, first);
    rc = sqlite3_step(ebrowse);
    if(SQLITE_ROW == rc) {
        id = sqlite3_column_int64(ebrowse, 0);
        extid = sqlite3_column_text(ebrowse, 1);
        name = sqlite3_column_text(ebrowse, 2);
        type = sqlite3_column_int(ebrowse, 3);
        size = sqlite3_column_int64(ebrowse, 4);
        mode = sqlite3_column_int(ebrowse, 5);
        r = sqlite3_column_double(epinpoint, 6);
        r2ts(&atime, r);
        r = sqlite3_column_double(epinpoint, 7);
        r2ts(&mtime, r);
        r = sqlite3_column_double(epinpoint, 8);
        r2ts(&ctime, r);
        sync = sqlite3_column_int(ebrowse, 9);
        checksum = sqlite3_column_text(ebrowse, 10);

        rc = cb(id, extid, name, type, size, mode, &atime, &mtime, &ctime,
                sync, checksum, parent);
        if(rc != 0) {
            return -1;
        }
    }

    return 0;
}

int dbcache_rename(int64_t id, const char *name)
{
    int rc;

    rc = sqlite3_reset(erename);
    rc = sqlite3_bind_text(erename, 1, name, -1, NULL);
    rc = sqlite3_bind_int64(erename, 2, id);
    rc = sqlite3_step(erename);

    return rc;
}

int dbcache_chmod(int64_t id, mode_t mode)
{
    int rc;

    rc = sqlite3_reset(echmod);
    mode &= 0777;
    rc = sqlite3_bind_int(echmod, 1, mode);
    rc = sqlite3_bind_int64(echmod, 2, id);
    rc = sqlite3_step(echmod);

    return rc;
}

int dbcache_resize(int64_t id, size_t size)
{
    int rc;

    rc = sqlite3_reset(eresize);
    rc = sqlite3_bind_int64(eresize, 1, size);
    rc = sqlite3_bind_int64(eresize, 2, id);
    rc = sqlite3_step(eresize);

    return rc;
}

int dbcache_chatime(int64_t id, const struct timespec *tv)
{
    int rc;
    double a;

    rc = sqlite3_reset(echatime);
    ts2r(&a, tv);
    rc = sqlite3_bind_double(echatime, 1, a);
    rc = sqlite3_bind_int64(echatime, 2, id);
    rc = sqlite3_step(echatime);

    return SQLITE_DONE == rc ? 0 : -1;
}

int dbcache_chmtime(int64_t id, const struct timespec *tv)
{
    int rc;
    double m;

    rc = sqlite3_reset(echmtime);
    ts2r(&m, tv);
    rc = sqlite3_bind_double(echmtime, 1, m);
    rc = sqlite3_bind_int64(echmtime, 2, id);
    rc = sqlite3_step(echmtime);

    return SQLITE_DONE == rc ? 0 : -1;
}

int dbcache_delete(int64_t id)
{
    int rc;

    rc = sqlite3_reset(edel);
    rc = sqlite3_bind_int64(edel, 1, id);
    rc = sqlite3_step(edel);

    return rc;
}

int dbcache_path(int64_t id, char *path, size_t len)
{
    int rc;
    off_t off;
    size_t l;
    int64_t parent;
    int i, hlen, flen;
    char tmp;

    /* write entry */
    off = 0;
    l = snprintf(path + off, len, "%lld", id);
    off += l;
    len -= l;
    strncpy(path + off, "/", len);
    off++;
    len--;
 
    /* recursively find parents */
    for(;;) {
        rc = sqlite3_reset(epinpoint);
        rc = sqlite3_bind_int64(epinpoint, 1, id);
        rc = sqlite3_step(epinpoint);
        if(rc != SQLITE_ROW) {
            return -1;
        }
        parent = sqlite3_column_int64(epinpoint, 10);
        if(0 == parent) {
            break;
        }
        l = snprintf(path + off, len, "%lld", parent);
        off += l;
        len -= l;
        strncpy(path + off, "/", len);
        off++;
        len--;
        id = parent;
    }

    /* now reverse */
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

static void setup(void)
{
    int rc;
    sqlite3_stmt *sel;
    sqlite3_stmt *ins;
    int vfound;

    sqlite3_exec(sql, "CREATE TABLE IF NOT EXISTS dfs_version ( "
            "version INTEGER NOT NULL )", NULL, NULL, NULL);
    sqlite3_prepare_v2(sql, "SELECT version FROM dfs_version", -1, &sel, NULL);
    rc = sqlite3_step(sel);
    if(SQLITE_DONE == rc) {
        rc = sqlite3_prepare_v2(sql, "INSERT INTO dfs_version ( version ) VALUES ( "
            "? )", -1, &ins, NULL);
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

    sqlite3_exec(sql, "CREATE TABLE IF NOT EXISTS dfs_config ( "
            "password TEXT NOT NULL )", NULL, NULL, NULL);
    
    sqlite3_exec(sql, "CREATE TABLE IF NOT EXISTS dfs_entity ( "
            "id INTEGER NOT NULL PRIMARY KEY, "
            "extid TEXT NOT NULL, "
            "name TEXT NOT NULL, "
            "type INTEGER NOT NULL, "
            "size INTEGER NOT NULL, "
            "mode INTEGER NOT NULL, "
            "atime REAL NOT NULL, "
            "mtime REAL NOT NULL, "
            "ctime REAL NOT NULL, "
            "sync INTEGER NOT NULL, "
            "checksum TEXT NOT NULL, "
            "parent INTEGER, "
            "FOREIGN KEY ( parent ) REFERENCES dfs_entity ( id ) "
            "ON DELETE CASCADE "
            ")", NULL, NULL, NULL);
    sqlite3_exec(sql, "CREATE INDEX IF NOT EXISTS dfs_entity_parent "
            "ON dfs_entity ( parent )", NULL, NULL, NULL);

    sqlite3_prepare_v2(sql, "SELECT extid FROM dfs_entity WHERE parent IS NULL",
            -1, &sel, NULL);
    rc = sqlite3_step(sel);
    if(SQLITE_DONE == rc) {
        rc = sqlite3_prepare_v2(sql, "INSERT INTO dfs_entity ( extid, name, "
            "type, size, mode, atime, mtime, ctime, sync, checksum ) VALUES ( "
            "'00000000-0000-0000-0000-000000000000', '/', 1, 0, 448, "
            "strftime('%s', 'now'), strftime('%s', 'now'), "
            "strftime('%s', 'now'), 1, 0)",
            -1, &ins, NULL);
        rc = sqlite3_bind_int(ins, 1, schemaversion);
        rc = sqlite3_step(ins);
        sqlite3_finalize(ins);
    }
    sqlite3_finalize(sel);

    sqlite3_prepare_v2(sql, "INSERT INTO dfs_entity ( extid, name, "
        "type, size, mode, atime, mtime, ctime, sync, checksum, parent ) "
        "VALUES ( ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        -1, &eins, NULL);

    sqlite3_prepare_v2(sql, "UPDATE dfs_entity SET name = ? WHERE id = ? ",
        -1, &erename, NULL);

    sqlite3_prepare_v2(sql, "DELETE FROM dfs_entity WHERE id = ? ",
        -1, &edel, NULL);

    sqlite3_prepare_v2(sql, "SELECT extid, name, type, size, mode, "
        "atime, mtime, ctime, sync, checksum, parent "
        "FROM dfs_entity WHERE id = ?", -1, &epinpoint,
        NULL);

    sqlite3_prepare_v2(sql, "SELECT id, extid, type, size, mode, "
        "atime, mtime, ctime, sync, checksum "
        "FROM dfs_entity WHERE name = ? AND parent = ?", -1, &elookup,
        NULL);

    sqlite3_prepare_v2(sql, "SELECT id, extid, name, type, size, mode, "
        "atime, mtime, ctime, sync, checksum "
        "FROM dfs_entity "
        "WHERE parent = ? AND id > ? "
        "ORDER BY id",
        -1, &ebrowse, NULL);

    sqlite3_prepare_v2(sql, "UPDATE dfs_entity SET mode = ? WHERE id = ? ",
        -1, &echmod, NULL);

    sqlite3_prepare_v2(sql, "UPDATE dfs_entity SET size = ? WHERE id = ? ",
        -1, &eresize, NULL);

    sqlite3_prepare_v2(sql, "UPDATE dfs_entity SET echatime = ? WHERE id = ? ",
        -1, &echatime, NULL);

    sqlite3_prepare_v2(sql, "UPDATE dfs_entity SET echmtime = ? WHERE id = ? ",
        -1, &echmtime, NULL);
}

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


