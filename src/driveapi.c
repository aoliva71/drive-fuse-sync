
#include "driveapi.h"

#include <easy.h>

static ptherad_t drive_thread;

static int keep_running;
static void *drive_run(void *);

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
    CURL_EXTERN rc;
#define URL_MAX 511
    char url[URL_MAX + 1];

    (void)opaque;

    curl = curl_easy_init();
    rc = curl_easy_setopt(curl, CURLOPT_URL, url);
    rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
#undef URL_MAX
}

int drive_login(const char *un, const char *passwd)
{
    (void)un;
    (void)passwd;
    return 0;
}

int drive_logout(void)
{
    return 0;
}

