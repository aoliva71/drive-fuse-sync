
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
static sqlite3_stmt *insertentity = NULL;
static sqlite3_stmt *renameentity = NULL;
static sqlite3_stmt *deleteentity = NULL;
static sqlite3_stmt *pinpoint = NULL;
static sqlite3_stmt *lookup = NULL;
static sqlite3_stmt *browse = NULL;
static sqlite3_stmt *resize = NULL;
static int schemaversion = -1;

static void setup(void);

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
    
    rc = sqlite3_reset(insertentity);
    rc = sqlite3_bind_text(insertentity, 1, extid, -1, NULL);
    rc = sqlite3_bind_text(insertentity, 2, name, -1, NULL);
    type = 1;
    rc = sqlite3_bind_int(insertentity, 3, type);
    size = 0;
    rc = sqlite3_bind_int64(insertentity, 4, size);
    rc = sqlite3_bind_int(insertentity, 5, mode);
    clock_gettime(CLOCK_REALTIME, &ts);
    atime = ts.tv_sec + ts.tv_nsec / 1000000000.0;
    rc = sqlite3_bind_int(insertentity, 6, atime);
    mtime = atime;
    rc = sqlite3_bind_int(insertentity, 7, mtime);
    ctime = atime;
    rc = sqlite3_bind_int(insertentity, 8, ctime);

    rc = sqlite3_bind_int(insertentity, 9, sync);
    rc = sqlite3_bind_text(insertentity, 10, checksum, -1, NULL);
    if(parent > 0) {
        rc = sqlite3_bind_int64(insertentity, 11, parent);
    } else {
        rc = sqlite3_bind_null(insertentity, 11);
    }
    rc = sqlite3_step(insertentity);

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
    
    rc = sqlite3_reset(insertentity);
    rc = sqlite3_bind_text(insertentity, 1, extid, -1, NULL);
    rc = sqlite3_bind_text(insertentity, 2, name, -1, NULL);
    type = 2;
    rc = sqlite3_bind_int(insertentity, 3, type);
    rc = sqlite3_bind_int64(insertentity, 4, size);
    rc = sqlite3_bind_int(insertentity, 5, mode);
    clock_gettime(CLOCK_REALTIME, &ts);
    atime = ts.tv_sec + ts.tv_nsec / 1000000000.0;
    rc = sqlite3_bind_int(insertentity, 6, atime);
    mtime = atime;
    rc = sqlite3_bind_int(insertentity, 7, mtime);
    ctime = atime;
    rc = sqlite3_bind_int(insertentity, 8, ctime);
    rc = sqlite3_bind_int(insertentity, 9, sync);
    rc = sqlite3_bind_text(insertentity, 10, checksum, -1, NULL);
    rc = sqlite3_bind_int64(insertentity, 11, parent);
    rc = sqlite3_step(insertentity);

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
    double ts;
    struct timespec atime, mtime, ctime;
    int sync;
    const char *checksum;
    int64_t parent;
 
    rc = sqlite3_reset(pinpoint);
    rc = sqlite3_bind_int64(pinpoint, 1, id);
    rc = sqlite3_step(pinpoint);
    if(rc != SQLITE_ROW) {
        return -1;
    }
    
    extid = sqlite3_column_text(pinpoint, 0);
    name = sqlite3_column_text(pinpoint, 1);
    type = sqlite3_column_int(pinpoint, 2);
    size = sqlite3_column_int64(pinpoint, 3);
    mode = sqlite3_column_int(pinpoint, 4);
    ts = sqlite3_column_int(pinpoint, 5);
    atime.tv_sec = (time_t)ts;
    atime.tv_nsec = (ts - (double)atime.tv_sec) * 1000000000L;
    ts = sqlite3_column_int(pinpoint, 6);
    mtime.tv_sec = (time_t)ts;
    mtime.tv_nsec = (ts - (double)mtime.tv_sec) * 1000000000L;
    ts = sqlite3_column_int(pinpoint, 7);
    ctime.tv_sec = (time_t)ts;
    ctime.tv_nsec = (ts - (double)ctime.tv_sec) * 1000000000L;
    sync = sqlite3_column_int(pinpoint, 8);
    checksum = sqlite3_column_text(pinpoint, 9);
    parent = sqlite3_column_int64(pinpoint, 10);

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
    double ts;
    struct timespec atime, mtime, ctime;
    int sync;
    const char *checksum;
 
    rc = sqlite3_reset(lookup);
    rc = sqlite3_bind_text(lookup, 1, name, -1, NULL);
    rc = sqlite3_bind_int64(lookup, 2, parent);
    rc = sqlite3_step(lookup);
    if(rc != SQLITE_ROW) {
        return -1;
    }
    
    id = sqlite3_column_int64(lookup, 0);
    extid = sqlite3_column_text(lookup, 1);
    type = sqlite3_column_int(lookup, 2);
    size = sqlite3_column_int64(lookup, 3);
    mode = sqlite3_column_int(lookup, 4);
    ts = sqlite3_column_int(pinpoint, 5);
    atime.tv_sec = (time_t)ts;
    atime.tv_nsec = (ts - (double)atime.tv_sec) * 1000000000L;
    ts = sqlite3_column_int(pinpoint, 6);
    mtime.tv_sec = (time_t)ts;
    mtime.tv_nsec = (ts - (double)mtime.tv_sec) * 1000000000L;
    ts = sqlite3_column_int(pinpoint, 7);
    ctime.tv_sec = (time_t)ts;
    ctime.tv_nsec = (ts - (double)ctime.tv_sec) * 1000000000L;
    sync = sqlite3_column_int(lookup, 8);
    checksum = sqlite3_column_text(lookup, 9);

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
    double ts;
    struct timespec atime, mtime, ctime;
    int sync;
    const char *checksum;

    rc = sqlite3_reset(browse);
    rc = sqlite3_bind_int64(browse, 1, parent);
    rc = sqlite3_bind_int64(browse, 2, first);
    rc = sqlite3_step(browse);
    if(SQLITE_ROW == rc) {
        id = sqlite3_column_int64(browse, 0);
        extid = sqlite3_column_text(browse, 1);
        name = sqlite3_column_text(browse, 2);
        type = sqlite3_column_int(browse, 3);
        size = sqlite3_column_int64(browse, 4);
        mode = sqlite3_column_int(browse, 5);
        ts = sqlite3_column_int(pinpoint, 6);
        atime.tv_sec = (time_t)ts;
        atime.tv_nsec = (ts - (double)atime.tv_sec) * 1000000000L;
        ts = sqlite3_column_int(pinpoint, 7);
        mtime.tv_sec = (time_t)ts;
        mtime.tv_nsec = (ts - (double)mtime.tv_sec) * 1000000000L;
        ts = sqlite3_column_int(pinpoint, 8);
        ctime.tv_sec = (time_t)ts;
        ctime.tv_nsec = (ts - (double)ctime.tv_sec) * 1000000000L;
        sync = sqlite3_column_int(browse, 9);
        checksum = sqlite3_column_text(browse, 10);

        rc = cb(id, extid, name, type, size, mode, &atime, &mtime, &ctime,
                sync, checksum, parent);
        if(rc != 0) {
            return -1;
        }
    }

    return 0;
}

int dbcache_renameentry(int64_t id, const char *name)
{
    int rc;

    rc = sqlite3_reset(renameentity);
    rc = sqlite3_bind_text(renameentity, 1, name, -1, NULL);
    rc = sqlite3_bind_int64(renameentity, 2, id);
    rc = sqlite3_step(renameentity);

    return rc;
}

int dbcache_modifymode(int64_t id, mode_t mode)
{
    return -1;
}

int dbcache_modifysize(int64_t id, size_t size)
{
    int rc;

    rc = sqlite3_reset(resize);
    rc = sqlite3_bind_int64(resize, 1, size);
    rc = sqlite3_bind_int64(resize, 2, id);
    rc = sqlite3_step(resize);

    return rc;
}

int dbcache_deleteentry(int64_t id)
{
    int rc;

    rc = sqlite3_reset(deleteentity);
    rc = sqlite3_bind_int64(deleteentity, 1, id);
    rc = sqlite3_step(deleteentity);

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
        rc = sqlite3_reset(pinpoint);
        rc = sqlite3_bind_int64(pinpoint, 1, id);
        rc = sqlite3_step(pinpoint);
        if(rc != SQLITE_ROW) {
            return -1;
        }
        parent = sqlite3_column_int64(pinpoint, 10);
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
        -1, &insertentity, NULL);

    sqlite3_prepare_v2(sql, "UPDATE dfs_entity SET name = ? WHERE id = ? ",
        -1, &renameentity, NULL);

    sqlite3_prepare_v2(sql, "DELETE FROM dfs_entity WHERE id = ? ",
        -1, &deleteentity, NULL);

    sqlite3_prepare_v2(sql, "SELECT extid, name, type, size, mode, "
        "atime, mtime, ctime, sync, checksum, parent "
        "FROM dfs_entity WHERE id = ?", -1, &pinpoint,
        NULL);

    sqlite3_prepare_v2(sql, "SELECT id, extid, type, size, mode, "
        "atime, mtime, ctime, sync, checksum "
        "FROM dfs_entity WHERE name = ? AND parent = ?", -1, &lookup,
        NULL);

    sqlite3_prepare_v2(sql, "SELECT id, extid, name, type, size, mode, "
        "atime, mtime, ctime, sync, checksum "
        "FROM dfs_entity "
        "WHERE parent = ? AND id > ? "
        "ORDER BY id",
        -1, &browse, NULL);

    sqlite3_prepare_v2(sql, "UPDATE dfs_entity SET size = ? WHERE id = ? ",
        -1, &resize, NULL);
}

#undef LOG


