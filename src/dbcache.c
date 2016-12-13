
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

static sqlite3 *sql = NULL;
static sqlite3_stmt *insertentity = NULL;
static sqlite3_stmt *renameentity = NULL;
static sqlite3_stmt *deleteentity = NULL;
static sqlite3_stmt *selectentity = NULL;
static int schemaversion = -1;

static void setup(void);
static int find(const char *, const char *, char *, size_t, size_t *,
        mode_t *);

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

int dbcache_createdir(const char *uuid, const char *name, mode_t mode,
        int sync, const char *checksum, const char *parent)
{
    int type;
    int size;
    int rc;
    
    rc = sqlite3_reset(insertentity);
    rc = sqlite3_bind_text(insertentity, 1, uuid, -1, NULL);
    rc = sqlite3_bind_text(insertentity, 2, name, -1, NULL);
    type = 1;
    rc = sqlite3_bind_int(insertentity, 3, type);
    size = 0;
    rc = sqlite3_bind_int(insertentity, 4, size);
    rc = sqlite3_bind_int(insertentity, 5, mode);
    rc = sqlite3_bind_int(insertentity, 6, sync);
    rc = sqlite3_bind_text(insertentity, 7, checksum, -1, NULL);
    rc = sqlite3_bind_text(insertentity, 8, parent, -1, NULL);
    rc = sqlite3_step(insertentity);
    return rc;
}

int dbcache_createfile(const char *uuid, const char *name, size_t size,
        mode_t mode, int sync, const char *checksum, const char *parent)
{
    int type;
    int rc;
    
    rc = sqlite3_reset(insertentity);
    rc = sqlite3_bind_text(insertentity, 1, uuid, -1, NULL);
    rc = sqlite3_bind_text(insertentity, 2, name, -1, NULL);
    type = 2;
    rc = sqlite3_bind_int(insertentity, 3, type);
    rc = sqlite3_bind_int(insertentity, 4, size);
    rc = sqlite3_bind_int(insertentity, 5, mode);
    rc = sqlite3_bind_int(insertentity, 6, sync);
    rc = sqlite3_bind_text(insertentity, 7, checksum, -1, NULL);
    rc = sqlite3_bind_text(insertentity, 8, parent, -1, NULL);
    rc = sqlite3_step(insertentity);

    return rc;
}

int dbcache_find(const char *path, char *uuid, size_t len, size_t *size, mode_t *mode)
{
    int rc;
    const char *pb;
    const char *pe;
    size_t l;
    char fpath[PATH_MAX + 1];
    size_t s;
    mode_t m;
#define UUID_MAX    63
    char entity[UUID_MAX + 1];
    char parent[UUID_MAX + 1];
    
    memset(parent, 0, (UUID_MAX + 1) * sizeof(char));
    strncpy(parent, "00000000-0000-0000-0000-000000000000", UUID_MAX);
    pb = path;
    for(;;) {
        memset(entity, 0, (UUID_MAX + 1) * sizeof(char));
        memset(fpath, 0, (PATH_MAX + 1) * sizeof(char));
        if('/' == *pb) {
            pb++;
        }
        pe = strchr(pb, '/');
        if(pe) {
            l = pe - pb;
            l--;
            if(l > PATH_MAX) {
                l = PATH_MAX;
            }
            strncpy(fpath, pb, l);
            rc = find(fpath, parent, entity, UUID_MAX, &s, &m);
            if(rc != 0) {
                return -1;
            }
        } else {
            strncpy(fpath, pb, PATH_MAX);
            rc = find(fpath, parent, entity, UUID_MAX, &s, &m);
            if(rc != 0) {
                return -1;
            }
            if(uuid) {
                strncpy(uuid, entity, len);
            }
            *size = s;
            *mode = m;
            return 0;
        }
    }
    
#undef UUID_MAX
}


int dbcache_browse(const char *path, void *f)
{
    typedef int (*fuse_fill_dir_t)(void *, const char *, const struct stat *,
        off_t);
    int rc;
    fuse_fill_dir_t filler;
#define UUID_MAX    63
    char uuid[UUID_MAX + 1];
    size_t s;
    mode_t m;

    filler = (fuse_fill_dir_t)f;
    rc = dbcache_find(path, uuid, UUID_MAX, &s, &m);
    if(rc != 0) {
        return -1;
    }
    /* find siblings */
#undef UUID_MAX
    return 0;
}

int dbcache_renameentry(const char *uuid, const char *name)
{
    int rc;

    rc = sqlite3_reset(renameentity);
    rc = sqlite3_bind_text(renameentity, 1, name, -1, NULL);
    rc = sqlite3_bind_text(renameentity, 2, uuid, -1, NULL);
    rc = sqlite3_step(renameentity);

    return rc;
}

int dbcache_modifymode(const char *uuid, mode_t mode)
{
    return -1;
}

int dbcache_modifysize(const char *uuid, size_t size)
{
    return -1;
}

int dbcache_deleteentry(const char *uuid)
{
    int rc;

    rc = sqlite3_reset(deleteentity);
    rc = sqlite3_bind_text(deleteentity, 1, uuid, -1, NULL);
    rc = sqlite3_step(deleteentity);

    return rc;
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
            "uuid TEXT NOT NULL PRIMARY KEY, "
            "name TEXT NOT NULL, "
            "type INTEGER NOT NULL, "
            "size INTEGER NOT NULL, "
            "attr INTEGER NOT NULL, "
            "sync INTEGER NOT NULL, "
            "checksum TEXT NOT NULL, "
            "parent TEXT, "
            "FOREIGN KEY ( parent ) REFERENCES dfs_entity ( uuid ) "
            "ON DELETE CASCADE "
            ")", NULL, NULL, NULL);
    sqlite3_exec(sql, "CREATE INDEX IF NOT EXISTS dfs_entity_name ON "
            "dfs_entity ( name )", NULL, NULL, NULL);

    sqlite3_prepare_v2(sql, "SELECT uuid FROM dfs_entity WHERE parent IS NULL",
            -1, &sel, NULL);
    rc = sqlite3_step(sel);
    if(SQLITE_DONE == rc) {
        rc = sqlite3_prepare_v2(sql, "INSERT INTO dfs_entity ( uuid, name, "
            "type, size, attr, sync, checksum ) VALUES ( "
            "'00000000-0000-0000-0000-000000000000', 'root', 1, 0, 448, 1, 0)",
            -1, &ins, NULL);
        rc = sqlite3_bind_int(ins, 1, schemaversion);
        rc = sqlite3_step(ins);
        sqlite3_finalize(ins);
    }
    sqlite3_finalize(sel);

    sqlite3_prepare_v2(sql, "INSERT INTO dfs_entity ( uuid, name, "
        "type, size, attr, sync, checksum, parent ) VALUES ( "
        "?, ?, ?, ?, ?, ?, ?, ?)",
        -1, &insertentity, NULL);

    sqlite3_prepare_v2(sql, "UPDATE dfs_entity SET name = ? WHERE uuid = ? ",
        -1, &renameentity, NULL);

    sqlite3_prepare_v2(sql, "DELETE FROM dfs_entity WHERE uuid = ? ",
        -1, &deleteentity, NULL);

    sqlite3_prepare_v2(sql, "SELECT uuid, type, size, attr, sync, checksum "
        "FROM dfs_entity WHERE name = ? AND parent = ?", -1, &selectentity,
        NULL);
}

static int find(const char *name, const char *parent, char *entity,
        size_t len, size_t *size, mode_t *mode)
{
    int rc;
    const unsigned char *e;
    int s, m;

    rc = sqlite3_reset(selectentity);
    rc = sqlite3_bind_text(selectentity, 1, name, -1, NULL);
    rc = sqlite3_bind_text(selectentity, 2, parent, -1, NULL);
    rc = sqlite3_step(selectentity);
    if(rc != SQLITE_ROW) {
        return -1;
    }
    e = sqlite3_column_text(selectentity, 0);
    s = sqlite3_column_int(selectentity, 2);
    m = sqlite3_column_int(selectentity, 3);
    strncpy(entity, e, len);
    *size = (size_t)s;
    *mode = (mode_t)m;
    return 0;
}

