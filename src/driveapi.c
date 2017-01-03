
#include "driveapi.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include <curl/curl.h>
#include <curl/easy.h>

static pthread_t drive_thread;

static int keep_running;
static void *drive_run(void *);

int drive_setup(void)
{
    CURL *curl;
    int rc;
#define SCOPE_MAX   127
    char scope[SCOPE_MAX + 1];
#define URL_MAX     511
    char url[URL_MAX + 1];
    char *eurl;
#define CODE_MAX    127
    char code[CODE_MAX + 1];
    int clen;
    int i;

    strncpy(scope, "https://www.googleapis.com/auth/drive.metadata.readonly",
            SCOPE_MAX);
    curl = curl_easy_init();
    if(curl) {
        eurl = curl_easy_escape(curl, scope, strlen(scope));
        if(eurl) {
            snprintf(url, URL_MAX,
                    "https://accounts.google.com/o/oauth2/auth?"
                    "client_id=429614641440-42ueklua1v9vnhpacs5ml9h68hh6bv1c."
                    "apps.googleusercontent.com&"
                    "response_type=code&"
                    "redirect_uri=urn:ietf:wg:oauth:2.0:oob&"
                    "access_type=offline&"
                    "scope=%s", eurl);

            printf("open a browser and go here: %s\n", url);
            curl_free(eurl);
        }
        curl_easy_cleanup(curl);
    }

    printf("when done, paste access code here (empty to abort): ");
    fflush(stdout);
    
    fgets(code, CODE_MAX, stdin);
    clen = strlen(code);
    for(i = 0; i < clen; i++) {
        switch(code[i]) {
        case '\r':
        case '\n':
        case '\t':
        case ' ':
            code[i] = 0;
            break;
        }
    }
    if(0 == strlen(code)) {
        exit(1);
    }
#undef URL_MAX

    printf("using acccess code: \"%s\"\n", code);

    return 0;
}

int drive_start(void)
{
    keep_running = 1;
    pthread_create(&drive_thread, NULL, drive_run, NULL);

    return 0;
}

int drive_stop(void)
{
    keep_running = 0;
    pthread_join(drive_thread, NULL);

    return 0;
}

static void *drive_run(void *opaque)
{
    CURL *curl;
    CURLcode rc;
#define URL_MAX 511
    char url[URL_MAX + 1];
    char *eurl;

    (void)opaque;

    while(keep_running) {
        /*curl = curl_easy_init();
        if(curl) {
            eurl = curl_easy_escape(curl, url, strlen(url));
            if(eurl) {
                rc = curl_easy_setopt(curl, CURLOPT_URL, url);
                if(CURLE_OK == rc) {
                    rc = curl_easy_perform(curl);
                    if(CURLE_OK == rc) {
                        
                    }
                }
                curl_free(eurl);
            }
            curl_easy_cleanup(curl);
        }*/
        sleep(1);
    }
#undef URL_MAX

    return NULL;
}


