
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
    CURLcode rc;
#define DATA_MAX    511
    char data[DATA_MAX + 1];
    char body[DATA_MAX + 1];
    char *eurl;
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
            "scope=https%%3A%%2F%%2Fwww.googleapis.com%%2Fauth%%2Fdrive.metadata.readonly");

    printf("open a browser and go here: %s\n", data);

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

    printf("using access code: \"%s\"\n", code);

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

    curl_global_init(CURL_GLOBAL_ALL);

    curl = curl_easy_init();
    if(curl) {
        rc = curl_easy_setopt(curl, CURLOPT_URL,
                "https://accounts.google.com/o/oauth2/token");
        memset(data, 0, (DATA_MAX + 1) * sizeof(char));
        snprintf(data, DATA_MAX,
                "scope=https%%3A%%2F%%2Fwww.googleapis.com%%2Fauth%%2F"
                "drive.metadata.readonly&"
                "grant_type=authorization_code&"
                "client_id=429614641440-42ueklua1v9vnhpacs5ml9h68hh6bv1c."
                "apps.googleusercontent.com&"
                "client_secret=lRBm6Lyt-I8V8cF29BR6ivqR&"
                "redirect_uri=urn:ietf:wg:oauth:2.0:oob&"
                "code=%s", code);
        rc = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
        rc = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writecb);
        memset(body, 0, (DATA_MAX + 1) * sizeof(char));
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, body);
        rc = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        printf("%s\n", body);
    }
    
    curl_global_cleanup();

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


