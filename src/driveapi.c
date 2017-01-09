
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

#define TOKEN_MAX       127
static char access_token[TOKEN_MAX + 1];
static char refresh_token[TOKEN_MAX + 1];

static int expires_in;
static time_t expiration_time;

static int get_tokens(const char *);
static int refresh_tokens(void);

static pthread_t drive_thread;

static int keep_running;
static void *drive_run(void *);

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
    curl_global_init(CURL_GLOBAL_ALL);

    keep_running = 1;
    pthread_create(&drive_thread, NULL, drive_run, NULL);

    return 0;
}

int drive_stop(void)
{
    keep_running = 0;
    pthread_join(drive_thread, NULL);

    curl_global_cleanup();

    return 0;
}

int drive_download(const char *id, FILE *f)
{
    CURL *curl;
    CURLcode rc;
    struct curl_slist *chunk;
#define AUTH_MAX    127
    char auth[AUTH_MAX + 1];
#define FILEURL_MAX     255
        char fileurl[FILEURL_MAX + 1];

    chunk = NULL;
    memset(auth, 0, (AUTH_MAX + 1) * sizeof(char));
    snprintf(auth, AUTH_MAX, "Authorization: %s %s", token_type,
            access_token);
    chunk = curl_slist_append(chunk, auth);


    curl = curl_easy_init();
    if(curl) {
        memset(fileurl, 0, (FILEURL_MAX + 1) * sizeof(char));
        snprintf(fileurl, FILEURL_MAX,
                "https://www.googleapis.com/drive/v3/files/%s?alt=media", id);
        rc = curl_easy_setopt(curl, CURLOPT_URL, fileurl);
        rc = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
        rc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
        rc = curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
        rc = curl_easy_perform(curl);
        (void)rc;
        curl_easy_cleanup(curl);
    }

    return 0;
}

static void *drive_run(void *opaque)
{
    time_t now;
    struct curl_slist *chunk;
#define AUTH_MAX    127
    char auth[AUTH_MAX + 1];

#define DATA_MAX    511
    char data[DATA_MAX + 1];

    size_t writecb(void *ptr, size_t size, size_t n, void *stream)
    {
        size_t len;

        len = size * n;
        if(len > DATA_MAX) {
            len = DATA_MAX;
        }
        memcpy(stream, ptr, len);

        return len;
    }

    void recurse(const char *alias)
    {
        CURL *curl;
        CURLcode rc;

        FILE *tmp;
#define FILEURL_MAX     255
        char fileurl[FILEURL_MAX + 1];
        json_object *jdobj;
        struct json_object *val;
        json_bool found;
        const char *sval;

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

#define LINEBUF_MAX    255
        char linebuf[LINEBUF_MAX + 1];
        char *line;
        char *end;

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
            memset(fileurl, 0, (FILEURL_MAX + 1) * sizeof(char));
            snprintf(fileurl, FILEURL_MAX,
                "https://www.googleapis.com/drive/v3/files/%s?"
                "fields=id,name,mimeType,size,"
                "modifiedTime,createdTime,version,md5Checksum,parents", alias);
            printf("%s - %s\n", alias, fileurl);
            rc = curl_easy_setopt(curl, CURLOPT_URL, fileurl);
            rc = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
            rc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writecb);
            memset(data, 0, (DATA_MAX + 1) * sizeof(char));
            rc = curl_easy_setopt(curl, CURLOPT_WRITEDATA, data);
            rc = curl_easy_perform(curl);
            if(CURLE_OK == rc) {
                //printf("%s\n", data);
                jdobj = json_tokener_parse(data);
                if(jdobj) {
                    found = json_object_object_get_ex(jdobj, "id", &val);
                    if(found) {
                        sval = json_object_get_string(val);
                        strncpy(uuid, sval, DUUID_MAX);
                    }
                    found = json_object_object_get_ex(jdobj, "name", &val);
                    if(found) {
                        sval = json_object_get_string(val);
                        strncpy(name, sval, DNAME_MAX);
                    }
                    found = json_object_object_get_ex(jdobj, "mimeType", &val);
                    if(found) {
                        sval = json_object_get_string(val);
                        strncpy(mime, sval, DMIME_MAX);
                        if(0 == strcmp(mime,
                                "application/vnd.google-apps.folder")) {
                            isdir = 1;
                        } else if(0 == strncmp(mime,
                                "application/vnd.google-apps.", 20)) {
                            exclude = 1;
                        }
                    }
                    found = json_object_object_get_ex(jdobj, "size", &val);
                    if(found) {
                        size = json_object_get_int64(val);
                    }
                    found = json_object_object_get_ex(jdobj, "modifiedTime",
                            &val);
                    if(found) {
                        sval = json_object_get_string(val);
                        strncpy(mtimes, sval, DTIME_MAX);
                    }
                    found = json_object_object_get_ex(jdobj, "createdTime",
                            &val);
                    if(found) {
                        sval = json_object_get_string(val);
                        strncpy(ctimes, sval, DTIME_MAX);
                    }
                    found = json_object_object_get_ex(jdobj, "md5Checksum",
                            &val);
                    if(found) {
                        sval = json_object_get_string(val);
                        strncpy(cksum, sval, DCKSUM_MAX);
                    }
                    found = json_object_object_get_ex(jdobj, "parents", &val);
                    if(found) {
                        pn = json_object_array_length(val);
                        if(pn > 0) {
                            pitem = json_object_array_get_idx(val, 0);
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
            curl_easy_cleanup(curl);
        }

        if(!exclude) {
            dbcache_update(uuid, name, isdir, size, &mtime, &ctime, cksum,
                    parent);
        }

        /* scan directory */
        if(isdir) {

            /* load all files */
            tmp = tmpfile();
            if(tmp) {
                curl = curl_easy_init();
                if(curl) {
                    memset(fileurl, 0, (FILEURL_MAX + 1) * sizeof(char));
                    snprintf(fileurl, FILEURL_MAX,
                            "https://www.googleapis.com/drive/v3/files?"
                            "q=%%27%s%%27+in+parents", uuid);
                    rc = curl_easy_setopt(curl, CURLOPT_URL, fileurl);
                    rc = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
                    rc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
                    rc = curl_easy_setopt(curl, CURLOPT_WRITEDATA, tmp);
                    rc = curl_easy_perform(curl);
                    curl_easy_cleanup(curl);
                }

                fflush(tmp);

                /* no real need for a full json parser, just go for lines like:
                   "id": "2T65Ks-tHyNP_3bPdE4wgkHlKQIDqz586I", */
                rewind(tmp);
                memset(linebuf, 0, (LINEBUF_MAX + 1) * sizeof(char));
                while((line = fgets(linebuf, LINEBUF_MAX, tmp))) {
                    if(strncmp(line, "   \"id\": \"", 10)) {
                        continue;
                    }
                    line += 10;
                    end = strchr(line, '"');
                    if(end) {
                        *end = 0;
                        recurse(line);
                    }
                }
                fclose(tmp);
            }
        }
        
    }

    (void)opaque;

    while(keep_running) {
        dbcache_auth_load(token_type, TOKENTYPE_MAX, access_token, TOKEN_MAX,
                refresh_token, TOKEN_MAX, &expires_in, &expiration_time);
        time(&now);
        if(now >= expiration_time) {
            refresh_tokens();
            dbcache_auth_store(token_type, access_token, refresh_token,
                expires_in, &expiration_time);
        }

        /* setup authorization */
        chunk = NULL;
        memset(auth, 0, (AUTH_MAX + 1) * sizeof(char));
        snprintf(auth, AUTH_MAX, "Authorization: %s %s", token_type,
                access_token);
        chunk = curl_slist_append(chunk, auth);

        /* get a changeid */

        /* scan drive */
        recurse("root");

        /* listen to changes since changeid */

        while(keep_running)
        sleep(1);
    }

    return NULL;
}

static int authorize(const char *, const char *, const char *);

static int get_tokens(const char *code)
{
    return authorize("authorization_code", "code", code);
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
    json_object *jauth;
    struct json_object *val;
    json_bool found;
    const char *sval;

    size_t writecb(void *ptr, size_t size, size_t n, void *stream)
    {
        size_t len;

        len = size * n;
        if(len > DATA_MAX) {
            len = DATA_MAX;
        }
        memcpy(stream, ptr, len);

        return len;
    }

    memset(token_type, 0, (TOKENTYPE_MAX + 1) * sizeof(char));
    memset(access_token, 0, (TOKEN_MAX + 1) * sizeof(char));
    expires_in = 0;
    time(&expiration_time);

    curl = curl_easy_init();
    if(curl) {
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
        rc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writecb);
        memset(body, 0, (DATA_MAX + 1) * sizeof(char));
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, body);
        rc = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        printf("%s\n", body);

        jauth = json_tokener_parse(body);
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
    }
    
    expiration_time += expires_in;

    return 0;
}


