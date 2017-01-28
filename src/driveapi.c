
#include "driveapi.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include <curl/curl.h>
#include <curl/easy.h>

#include <json.h>

#include "dbcache.h"

#define TOKENTYPE_MAX   31
static char token_type[TOKENTYPE_MAX + 1];

#define TOKEN_MAX       1023
static char access_token[TOKEN_MAX + 1];
static char refresh_token[TOKEN_MAX + 1];

static int expires_in;
static time_t expiration_time;

static int get_tokens(const char *);
static int refresh_tokens(void);
static int get_start_token(char *, size_t);
static int get_changes(char *, size_t, int *);

static pthread_t drive_thread;

static int keep_running;
static void *drive_run(void *);

static pthread_mutex_t auth_mutex;
static struct curl_slist *auth_chunk;

struct _json_context
{
    json_tokener *tokener;
    json_object **pointer;
};
typedef struct _json_context json_context_t;
size_t parse_json(void *, size_t, size_t, void *);

int drive_setup(void)
{
#define DATA_MAX    511
    char data[DATA_MAX + 1];
#define CODE_MAX    127
    char code[CODE_MAX + 1];
    int clen;
    int i;

    memset(data, 0, (DATA_MAX + 1) * sizeof(char));
    snprintf(data, DATA_MAX,
            "https://accounts.google.com/o/oauth2/auth?"
            "client_id=429614641440-42ueklua1v9vnhpacs5ml9h68hh6bv1c."
            "apps.googleusercontent.com&"
            "response_type=code&"
            "redirect_uri=urn:ietf:wg:oauth:2.0:oob&"
            "access_type=offline&"
            "scope=https%%3A%%2F%%2Fwww.googleapis.com%%2Fauth%%2Fdrive");

    printf("open a browser and go here: %s\n", data);

    printf("when done, paste access code here (empty to abort): ");
    fflush(stdout);
    
    fgets(code, CODE_MAX, stdin);
    clen = strlen(code);
    for(i = 0; i < clen; i++) {
        switch(code[i]) {
        case '\r':
        case '\n':
            code[i] = 0;
            break;
        }
    }
    if(0 == strlen(code)) {
        exit(1);
    }
#undef URL_MAX

    printf("using access code: \"%s\"\n", code);

    curl_global_init(CURL_GLOBAL_ALL);
    get_tokens(code);
    curl_global_cleanup();

    dbcache_auth_store(token_type, access_token, refresh_token, expires_in,
            &expiration_time);

    return 0;
}

int drive_start(void)
{
    auth_chunk = NULL;
    curl_global_init(CURL_GLOBAL_ALL);

    pthread_mutex_init(&auth_mutex, NULL);

    keep_running = 1;
    pthread_create(&drive_thread, NULL, drive_run, NULL);

    return 0;
}

int drive_stop(void)
{
    keep_running = 0;
    pthread_join(drive_thread, NULL);

    pthread_mutex_destroy(&auth_mutex);

    curl_global_cleanup();

    return 0;
}

int drive_download(const char *id, FILE *f)
{
    CURL *curl;
    CURLcode rc;
#define FILEURL_MAX     255
    char fileurl[FILEURL_MAX + 1];

    curl = curl_easy_init();
    if(curl) {
        /*rc = curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);*/
        memset(fileurl, 0, (FILEURL_MAX + 1) * sizeof(char));
        snprintf(fileurl, FILEURL_MAX,
                "https://www.googleapis.com/drive/v3/files/%s?alt=media", id);
        rc = curl_easy_setopt(curl, CURLOPT_URL, fileurl);
        pthread_mutex_lock(&auth_mutex);
        rc = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, auth_chunk);
        pthread_mutex_unlock(&auth_mutex);
        rc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
        rc = curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
        rc = curl_easy_perform(curl);
        (void)rc;
        curl_easy_cleanup(curl);
    }

    return 0;
}

void recurse(const char *alias)
{
    CURL *curl;
    CURLcode rc;

#define FILEURL_MAX     255
    char fileurl[FILEURL_MAX + 1];
    json_context_t context;
    json_tokener *tokener;
    json_object *jroot;
    json_object *jfiles;
    json_object *jfile;
    json_object *jval;
    json_bool found;
    const char *sval;
    int i, nfiles;

#define DUUID_MAX       63
    char uuid[DUUID_MAX + 1];
#define DNAME_MAX       255
    char name[DNAME_MAX + 1];
#define DMIME_MAX       63
    char mime[DMIME_MAX + 1];
    int isdir;
    int exclude;
    int64_t size;
#define DTIME_MAX       31
    char mtimes[DTIME_MAX + 1];
    char ctimes[DTIME_MAX + 1];
    struct timespec mtime;
    struct timespec ctime;
#define DCKSUM_MAX      63
    char cksum[DCKSUM_MAX + 1];
    int pn;
    struct json_object *pitem;
    char parent[DUUID_MAX + 1];

    memset(uuid, 0, (DUUID_MAX + 1) * sizeof(char));
    memset(name, 0, (DNAME_MAX + 1) * sizeof(char));
    memset(mime, 0, (DMIME_MAX + 1) * sizeof(char));
    isdir = 0;
    exclude = 0;
    size = 0LL;
    memset(mtimes, 0, (DTIME_MAX + 1) * sizeof(char));
    memset(ctimes, 0, (DTIME_MAX + 1) * sizeof(char));
    memset(&mtime, 0, sizeof(struct timeval));
    memset(&ctime, 0, sizeof(struct timeval));
    memset(cksum, 0, (DCKSUM_MAX + 1) * sizeof(char));
    pn = 0;
    pitem = NULL;
    memset(parent, 0, (DUUID_MAX + 1) * sizeof(char));
    

    /* query details */
    curl = curl_easy_init();
    if(curl) {
        tokener = json_tokener_new();
        if(tokener) {
            memset(fileurl, 0, (FILEURL_MAX + 1) * sizeof(char));
            snprintf(fileurl, FILEURL_MAX,
                    "https://www.googleapis.com/drive/v3/files/%s?"
                    "fields=id,name,mimeType,size,"
                    "modifiedTime,createdTime,version,md5Checksum,parents",
                    alias);
            printf("%s - %s\n", alias, fileurl);
            rc = curl_easy_setopt(curl, CURLOPT_URL, fileurl);
            pthread_mutex_lock(&auth_mutex);
            rc = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, auth_chunk);
            pthread_mutex_unlock(&auth_mutex);
            rc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parse_json);
            context.tokener = tokener;
            context.pointer = &jroot;
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
            rc = curl_easy_perform(curl);
            if(CURLE_OK == rc) {
                if(jroot) {
                    found = json_object_object_get_ex(jroot, "id", &jval);
                    if(found) {
                        sval = json_object_get_string(jval);
                        strncpy(uuid, sval, DUUID_MAX);
                    }
                    found = json_object_object_get_ex(jroot, "name", &jval);
                    if(found) {
                        sval = json_object_get_string(jval);
                        strncpy(name, sval, DNAME_MAX);
                    }
                    found = json_object_object_get_ex(jroot, "mimeType", &jval);
                    if(found) {
                        sval = json_object_get_string(jval);
                        strncpy(mime, sval, DMIME_MAX);
                        if(0 == strcmp(mime,
                                    "application/vnd.google-apps.folder")) {
                            isdir = 1;
                        } else if(0 == strncmp(mime,
                                    "application/vnd.google-apps.", 20)) {
                            exclude = 1;
                        }
                    }
                    found = json_object_object_get_ex(jroot, "size", &jval);
                    if(found) {
                        size = json_object_get_int64(jval);
                    }
                    found = json_object_object_get_ex(jroot, "modifiedTime",
                            &jval);
                    if(found) {
                        sval = json_object_get_string(jval);
                        strncpy(mtimes, sval, DTIME_MAX);
                    }
                    found = json_object_object_get_ex(jroot, "createdTime",
                            &jval);
                    if(found) {
                        sval = json_object_get_string(jval);
                        strncpy(ctimes, sval, DTIME_MAX);
                    }
                    found = json_object_object_get_ex(jroot, "md5Checksum",
                            &jval);
                    if(found) {
                        sval = json_object_get_string(jval);
                        strncpy(cksum, sval, DCKSUM_MAX);
                    }
                    found = json_object_object_get_ex(jroot, "parents", &jval);
                    if(found) {
                        pn = json_object_array_length(jval);
                        if(pn > 0) {
                            pitem = json_object_array_get_idx(jval, 0);
                            sval = json_object_get_string(pitem);
                            strncpy(parent, sval, DUUID_MAX);
                        }
                    }

                    /* insert/update entry */
                    /*printf("id: %s\n", id);
                      printf("name: %s\n", name);
                      printf("isdir: %d\n", isdir);
                      printf("size: %lld\n", (long long int)size);
                      printf("mtime: %s\n", mtimes);
                      printf("ctime: %s\n", ctimes);
                      printf("version: %d\n", version);
                      printf("cksum: %s\n", cksum);
                      printf("parent: %s\n", parent);
                      printf("\n");*/
                }
            }
            json_tokener_free(tokener);
        }
        curl_easy_cleanup(curl);
    }

    if(!exclude) {
        dbcache_update(uuid, name, isdir, size, &mtime, &ctime, cksum,
                parent);
    }

    /* scan directory */
    if(isdir) {

        /* load all files */
        curl = curl_easy_init();
        if(curl) {
            tokener = json_tokener_new();
            if(tokener) {
                memset(fileurl, 0, (FILEURL_MAX + 1) * sizeof(char));
                snprintf(fileurl, FILEURL_MAX,
                        "https://www.googleapis.com/drive/v3/files?"
                        "q=%%27%s%%27+in+parents", uuid);
                rc = curl_easy_setopt(curl, CURLOPT_URL, fileurl);
                pthread_mutex_lock(&auth_mutex);
                rc = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, auth_chunk);
                pthread_mutex_unlock(&auth_mutex);
                rc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parse_json);
                context.tokener = tokener;
                context.pointer = &jroot;
                rc = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
                rc = curl_easy_perform(curl);

                if(jroot) {
                    found = json_object_object_get_ex(jroot, "kind", &jval);
                    /* drive#fileList */

                    found = json_object_object_get_ex(jroot, "nextPageToken",
                            &jval);
                    /* save for next run */
                    
                    found = json_object_object_get_ex(jroot, "files", &jfiles);
                    if(found) {
                        nfiles = json_object_array_length(jfiles);
                        for(i = 0; i < nfiles; i++) {
                            jfile = json_object_array_get_idx(jfiles, i);
                            if(jfile) {
                                found = json_object_object_get_ex(jfile,
                                        "id", &jval);
                                if(found) {
                                    sval = json_object_get_string(jval);
                                    if(sval && strlen(sval)) {
                                        recurse(sval);
                                    }
                                }
                            }
                        }
                    }
                }

                json_tokener_free(tokener);
            }
            curl_easy_cleanup(curl);
        }
    }
    
}

static void *drive_run(void *opaque)
{
    time_t now;
#define AUTH_MAX    1023
    char auth[AUTH_MAX + 1];

#define CHANGETOKEN_MAX 63
    char changeid[CHANGETOKEN_MAX + 1];
    int anychange;
    int idle_loops;


    (void)opaque;

    memset(changeid, 0, (CHANGETOKEN_MAX + 1) * sizeof(char));

    dbcache_auth_load(token_type, TOKENTYPE_MAX, access_token, TOKEN_MAX,
            refresh_token, TOKEN_MAX, &expires_in, &expiration_time);

    while(keep_running) {
        time(&now);
        if(now >= expiration_time) {
            refresh_tokens();
            dbcache_auth_store(token_type, access_token, refresh_token,
                expires_in, &expiration_time);
        }

        /* setup authorization */
        pthread_mutex_lock(&auth_mutex);
        auth_chunk = NULL;
        memset(auth, 0, (AUTH_MAX + 1) * sizeof(char));
        snprintf(auth, AUTH_MAX, "Authorization: %s %s", token_type,
                access_token);
        auth_chunk = curl_slist_append(auth_chunk, auth);
        pthread_mutex_unlock(&auth_mutex);

        /* load changeid from db */
        dbcache_change_load(changeid, CHANGETOKEN_MAX);

        if(0 == strlen(changeid)) {
            /* get start token */
            get_start_token(changeid, CHANGETOKEN_MAX);
            /* no changeid, scan drive */
            recurse("root");
            /* save start token */
            dbcache_change_store(changeid);
        }

        while(keep_running) {
            /* get changes since changeid */
            anychange = 0;
            get_changes(changeid, CHANGETOKEN_MAX, &anychange);
            if(anychange) {
                dbcache_change_store(changeid);
                continue;
            }

            idle_loops = 0;
            while(keep_running) {
                /* TODO: if activity detected exit loop */

                /* exit every minute */
                if(idle_loops >= 60) {
                    break;
                }
                sleep(1);
                idle_loops++;
            }

            /* TODO: if activity detected, reset flag and exit loop */

            /* if token time expired, exit to outer loop */
            time(&now);
            if(now >= expiration_time) {
                break;
            }
        }
    }

    return NULL;
}

static int authorize(const char *, const char *, const char *);

static int get_tokens(const char *code)
{
    return authorize("authorization_code", "code", code);
}

size_t parse_json(void *ptr, size_t size, size_t n, void *stream)
{
    json_context_t *context;
    json_tokener *tokener;
    json_object *pointer;
    size_t len;
    enum json_tokener_error err;

    context = (json_context_t *)stream;
    tokener = context->tokener;
    len = size * n;
    pointer = json_tokener_parse_ex(tokener, ptr, len);
    if(NULL == pointer) {
        err = json_tokener_get_error(tokener);
        if(err != json_tokener_continue) {
            len = 0;
        }
    }
    *context->pointer = pointer;

    return len;
}

static int refresh_tokens(void)
{
    char token[TOKEN_MAX + 1];
    memset(token, 0, (TOKEN_MAX + 1) * sizeof(char));
    strncpy(token, refresh_token, TOKEN_MAX);
    return authorize("refresh_token", "refresh_token", token);
}

static int authorize(const char *grant_type, const char *key, const char *token)
{
    CURL *curl;
    CURLcode rc;
#define DATA_MAX    511
    char data[DATA_MAX + 1];
    char body[DATA_MAX + 1];
    json_context_t context;
    json_tokener *tokener;
    json_object *jauth;
    json_object *val;
    json_bool found;
    const char *sval;

    memset(token_type, 0, (TOKENTYPE_MAX + 1) * sizeof(char));
    memset(access_token, 0, (TOKEN_MAX + 1) * sizeof(char));
    expires_in = 0;
    time(&expiration_time);

    curl = curl_easy_init();
    if(curl) {
        tokener = json_tokener_new();
        if(tokener) {
            rc = curl_easy_setopt(curl, CURLOPT_URL,
                    "https://accounts.google.com/o/oauth2/token");
            (void)rc;
            memset(data, 0, (DATA_MAX + 1) * sizeof(char));
            snprintf(data, DATA_MAX,
                    "scope="
                    "https%%3A%%2F%%2Fwww.googleapis.com%%2Fauth%%2Fdrive&"
                    "grant_type=%s&"
                    "client_id=429614641440-42ueklua1v9vnhpacs5ml9h68hh6bv1c."
                    "apps.googleusercontent.com&"
                    "client_secret=lRBm6Lyt-I8V8cF29BR6ivqR&"
                    "redirect_uri=urn:ietf:wg:oauth:2.0:oob&"
                    "%s=%s", grant_type, key, token);
            rc = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
            rc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parse_json);
            memset(body, 0, (DATA_MAX + 1) * sizeof(char));
            context.tokener = tokener;
            context.pointer = &jauth;
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
            rc = curl_easy_perform(curl);

            if(jauth) {
                found = json_object_object_get_ex(jauth, "token_type", &val);
                if(found) {
                    sval = json_object_get_string(val);
                    strncpy(token_type, sval, TOKENTYPE_MAX);
                }
                found = json_object_object_get_ex(jauth, "access_token", &val);
                if(found) {
                    sval = json_object_get_string(val);
                    strncpy(access_token, sval, TOKEN_MAX);
                }
                found = json_object_object_get_ex(jauth, "refresh_token", &val);
                if(found) {
                    sval = json_object_get_string(val);
                    memset(refresh_token, 0, (TOKEN_MAX + 1) * sizeof(char));
                    strncpy(refresh_token, sval, TOKEN_MAX);
                }
                found = json_object_object_get_ex(jauth, "expires_in", &val);
                if(found) {
                    expires_in = json_object_get_int(val);
                }
            }
            json_tokener_free(tokener);
        }
        curl_easy_cleanup(curl);
    }
    
    /* do not wait until last minute before refreshing */
    expiration_time += (5 * expires_in) / 6;

    return 0;
}

static int get_start_token(char *changeid, size_t len)
{
    CURL *curl;
    CURLcode cc;
    int rc;
    char fileurl[FILEURL_MAX + 1];
    json_context_t context;
    json_tokener *tokener;
    json_object *jbody;
    json_object *jval;
    json_bool found;
    const char *sval;

    (void)rc;

    curl = curl_easy_init();
    if(curl) {
        tokener = json_tokener_new();
        if(tokener) {
            memset(fileurl, 0, (FILEURL_MAX + 1) * sizeof(char));
            snprintf(fileurl, FILEURL_MAX,
                    "https://www.googleapis.com/drive/v3/changes/"
                    "startPageToken");
            cc = curl_easy_setopt(curl, CURLOPT_URL, fileurl);
            pthread_mutex_lock(&auth_mutex);
            cc = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, auth_chunk);
            pthread_mutex_unlock(&auth_mutex);
            cc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parse_json);
            context.tokener = tokener;
            context.pointer = &jbody;
            cc = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
            cc = curl_easy_perform(curl);

            if(jbody) {
                found = json_object_object_get_ex(jbody, "kind", &jval);
                if(found) {
                    sval = json_object_get_string(jval);
                    /* must match "drive#startPageToken" */
                }
                found = json_object_object_get_ex(jbody, "startPageToken",
                        &jval);
                if(found) {
                    sval = json_object_get_string(jval);
                    strncpy(changeid, sval, len);
                }
            }
            json_tokener_free(tokener);
        }
        curl_easy_cleanup(curl);
    }
    return 0;
}

static int get_changes(char *changeid, size_t len, int *anychange)
{
    CURL *curl;
    CURLcode rc;
#define FILEURL_MAX     255
    char fileurl[FILEURL_MAX + 1];
    FILE *f;

    (void)changeid;
    (void)len;

    *anychange = 0;
    curl = curl_easy_init();
    if(curl) {
        memset(fileurl, 0, (FILEURL_MAX + 1) * sizeof(char));
        snprintf(fileurl, FILEURL_MAX, "/tmp/%s.change", changeid);
        f = fopen(fileurl, "w");
        /*rc = curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);*/
        memset(fileurl, 0, (FILEURL_MAX + 1) * sizeof(char));
        snprintf(fileurl, FILEURL_MAX,
                "https://www.googleapis.com/drive/v3/changes?"
                "pageToken=%s&includeRemoved=true&"
                "pageSize=1&restrictToMyDrive=true&"
                "spaces=drive", changeid);
        rc = curl_easy_setopt(curl, CURLOPT_URL, fileurl);
        pthread_mutex_lock(&auth_mutex);
        rc = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, auth_chunk);
        pthread_mutex_unlock(&auth_mutex);
        rc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
        rc = curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
        rc = curl_easy_perform(curl);
        (void)rc;
        fclose(f);
        curl_easy_cleanup(curl);
    }

    return 0;
}

