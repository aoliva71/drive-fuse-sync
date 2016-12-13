
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
static sqlite3_stmt *selectentity = NULL;
static sqlite3_stmt *selectchildren = NULL;
static int schemaversion = -1;

static void setup(void);

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

int dbcache_createdir(int64_t *id, const char *uuid, const char *name, mode_t mode,
        int sync, const char *checksum, const char *parent)
{
    int type;
    int size;
    int rc;
    sqlite3_int64 lid;
    
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

    lid = sqlite3_last_insert_rowid(sql);
    *id = lid;
    return rc;
}

int dbcache_createfile(int64_t *id, const char *uuid, const char *name, size_t size,
        mode_t mode, int sync, const char *checksum, const char *parent)
{
    int type;
    int rc;
    sqlite3_int64 lid;
    
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

    lid = sqlite3_last_insert_rowid(sql);
    *id = lid;
    return rc;
}

int dbcache_find(int64_t id, char *uuid, size_t ulen, char *name, size_t nlen,
        size_t *size, mode_t *mode)
{
    int rc;
    const unsigned char *u;
    const unsigned char *n;
    int64_t s;
    int m;

    rc = sqlite3_reset(selectentity);
    rc = sqlite3_bind_int64(selectentity, 1, id);
    rc = sqlite3_step(selectentity);
    if(rc != SQLITE_ROW) {
        return -1;
    }
    u = sqlite3_column_text(selectentity, 0);
    n = sqlite3_column_text(selectentity, 1);
    s = sqlite3_column_int64(selectentity, 2);
    m = sqlite3_column_int(selectentity, 3);
    strncpy(uuid, u, ulen);
    strncpy(name, n, nlen);
    *size = (size_t)s;
    *mode = (mode_t)m;
    return 0;
}


int dbcache_browse(int64_t parent, void *req, int what)
{
    /*typedef int (*fuse_fill_dir_t)(void *, const char *, const struct stat *,
        off_t);*/
    struct fuse_entry_param e;
    struct dirbuf b;
    int64_t cid;
    size_t s;
    mode_t m;
    int rc;

    rc = sqlite3_reset(selectchildren);
    rc = sqlite3_bind_int64(selectchildren, 1, parent);
    rc = sqlite3_step(renameentity);
    while(SQLITE_ROW == rc) {
        cid = sqlite3_column_int64(selectchildren, 0);
        s = sqlite3_column_int64(selectchildren, 4);
        m = sqlite3_column_int(selectchildren, 5);

        switch(what) {
        case LOOKUP:
            memset(&e, 0, sizeof(struct fuse_entry_param));
            e.ino = cid;
            e.attr_timeout = 1.0;
            e.entry_timeout = 1.0;
            e.attr.st_ino = cid;
            e.attr.st_mode = m;
            e.attr.nlink = 1;
            e.attr.size = s;

            fuse_reply_entry((fuse_req_t)req, &e);
            break;
        case READDIR:
            break;
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
    return -1;
}

int dbcache_deleteentry(int64_t id)
{
    int rc;

    rc = sqlite3_reset(deleteentity);
    rc = sqlite3_bind_int64(deleteentity, 1, id);
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
            "id AUTOINCREMENT NOT NULL PRIMARY KEY, "
            "uuid TEXT NOT NULL, "
            "name TEXT NOT NULL, "
            "type INTEGER NOT NULL, "
            "size INTEGER NOT NULL, "
            "attr INTEGER NOT NULL, "
            "sync INTEGER NOT NULL, "
            "checksum TEXT NOT NULL, "
            "parent INTEGER, "
            "FOREIGN KEY ( parent ) REFERENCES dfs_entity ( id ) "
            "ON DELETE CASCADE "
            ")", NULL, NULL, NULL);
    sqlite3_exec(sql, "CREATE INDEX IF NOT EXISTS dfs_entity_parent "
            "ON dfs_entity ( parent )", NULL, NULL, NULL);

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

    sqlite3_prepare_v2(sql, "UPDATE dfs_entity SET name = ? WHERE id = ? ",
        -1, &renameentity, NULL);

    sqlite3_prepare_v2(sql, "DELETE FROM dfs_entity WHERE id = ? ",
        -1, &deleteentity, NULL);

    sqlite3_prepare_v2(sql, "SELECT uuid, name, type, size, attr, sync, "
        "checksum "
        "FROM dfs_entity WHERE id = ?", -1, &selectentity,
        NULL);

    sqlite3_prepare_v2(sql, "SELECT id, uuid, name, type, size, attr, sync, "
        "checksum "
        "FROM dfs_entity "
        "WHERE parent = ?",
        -1, &selectchildren, NULL);
}


