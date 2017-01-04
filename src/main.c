
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbcache.h"
#include "driveapi.h"
#include "fscache.h"
#include "fuseapi.h"
#include "inotifyapi.h"

struct _conf
{
    int setup;
    int daemonize;
    
    char basedir[PATH_MAX + 1];
    char cachedir[PATH_MAX + 1];
    char mountpoint[PATH_MAX + 1];

#define USER_MAX    63
    char user[USER_MAX + 1];
    char pidfile[PATH_MAX + 1];
    char dbfile[PATH_MAX + 1];
};
typedef struct _conf conf_t;

static int keep_running = 0;
static void killer(int);

static void set_defaults(conf_t *);
static void parse_command_line(conf_t *, int, char *[]);
static void write_pid(const char *);

int main(int argc, char *argv[])
{
    conf_t conf;
    
    set_defaults(&conf);
    parse_command_line(&conf, argc, argv);

    if(conf.setup) {
        dbcache_open(conf.dbfile);
        dbcache_setup_schema();
        drive_setup();
        dbcache_close();
    }

    if(conf.daemonize) {
        daemon(0, 0);
    }
    mkdir(conf.basedir, (mode_t)0700);
    mkdir(conf.cachedir, (mode_t)0700);
    mkdir(conf.mountpoint, (mode_t)0700);
    openlog(PACKAGE_NAME, LOG_CONS|LOG_PID, LOG_DAEMON);

    signal(SIGINT, killer);

    write_pid(conf.pidfile);

    syslog(LOG_INFO, "starting");

    fscache_start(conf.cachedir);

    dbcache_open(conf.dbfile);
    dbcache_setup();

    /*
    dbcache_createdir(&parent, "0000000a-000a-000a-000a-00000000000a",
        "a", 0755, 1, "a========", 1);
    dbcache_createdir(&id, "000000aa-00aa-00aa-00aa-0000000000aa",
        "aa", 0755, 1, "aa=======", parent);
    dbcache_createdir(&id, "000000ab-00ab-00ab-00ab-0000000000ab",
        "ab", 0755, 1, "ab=======", parent);
    dbcache_createdir(&id, "000000ac-00ac-00ac-00ac-0000000000ac",
        "ac", 0755, 1, "ac=======", parent);
    dbcache_createdir(&id, "0000000b-000b-000b-000b-00000000000b",
        "b", 0755, 1, "b========", 1);
    dbcache_createdir(&id, "0000000c-000c-000c-000c-00000000000c",
        "c", 0755, 1, "c========", 1);
    */

    fuse_start(conf.mountpoint);

    drive_start();

    for(keep_running = 1; keep_running;) {
        /* check remote changes */
        /* check local changes */
        sleep(1);
    }

    drive_stop();

    fuse_stop();

    dbcache_close();

    fscache_stop();

    syslog(LOG_INFO, "stopping");
    closelog();

    return 0;
}

static void killer(int s)
{
    if(SIGINT == s) {
        if(keep_running) {
            keep_running = 0;
        } else {
            exit(0);
        }
    }
}

static void set_defaults(conf_t *conf)
{
    const char *home;
    memset(conf, 0, sizeof(conf_t));
    home = getenv("HOME");
    if(home) {
        snprintf(conf->basedir, PATH_MAX, "%s/.drivefusesync", home);
        snprintf(conf->mountpoint, PATH_MAX, "%s/drive", home);
    }
}

static void parse_command_line(conf_t *conf, int argc, char *argv[])
{
    int o;
#define OPTS    "sdu:b:m:h"
    static struct option lopts[] = {
        {"setup", 0, NULL, 's'},
        {"daemonize", 0, NULL, 'd'},
        {"user-name", 1, NULL, 'u'},
        {"base-dir", 1, NULL, 'b'},
        {"mount-point", 1, NULL, 'm'},
        {"help", 0, NULL, 'h'},
        {0, 0, 0, 0}
    };

    for(;;) {
        o = getopt_long(argc, argv, OPTS, lopts, NULL);
        if(-1 == o) {
            break;
        }
        switch(o) {
        case 's':
            conf->setup = 1;
            break;
        case 'd':
            conf->daemonize = 1;
            break;
        case 'u':
            if(optarg) {
                strncpy(conf->user, optarg, USER_MAX);
            }
            break;
        case 'b':
            if(optarg) {
                strncpy(conf->basedir, optarg, PATH_MAX);
            }
            break;
        case 'm':
            if(optarg) {
                strncpy(conf->mountpoint, optarg, PATH_MAX);
            }
            break;
        case 'h':
            printf("usage: %s "
                "[-s|--setup] "
                "[-d|--daemonize] "
                "[-b|--base-dir <BASEDIR>] "
                "[-m|--mount-point <MOUNTPOINT>] "
                "-u|--user <USERNAME> "
                " | "
                "-h|--help\n"
                "\n"
                "BASEDIR defaults to ${HOME}/.drivefusesync\n"
                "USER is the drive user\n"
                "\n", argv[0]);
            exit(0);
        }
    }

    if(0 == strlen(conf->user)) {
        fprintf(stderr, "no username provided\n");
        exit(1);
    }
    snprintf(conf->cachedir, PATH_MAX, "%s/%s.cache", conf->basedir, conf->user);
    snprintf(conf->pidfile, PATH_MAX, "%s/%s.pid", conf->basedir, conf->user);
    snprintf(conf->dbfile, PATH_MAX, "%s/%s.db", conf->basedir, conf->user);
}

static void write_pid(const char *pidfile)
{
    pid_t pid;
    FILE *f;

    pid = getpid();
    f = fopen(pidfile, "w");
    if(f) {
        fprintf(f, "%d", (int)pid);
        fclose(f);
    }
}

