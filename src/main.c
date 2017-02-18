
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <log4c.h>

#include "dbcache.h"
#include "driveapi.h"
#include "fscache.h"
#include "fuseapi.h"

struct _conf
{
    int setup;
    int daemonize;
    
    char basedir[PATH_MAX + 1];
    char cachedir[PATH_MAX + 1];
    char mountpoint[PATH_MAX + 1];
    char logdir[PATH_MAX + 1];

#define USER_MAX    63
    char user[USER_MAX + 1];
    char pidfile[PATH_MAX + 1];
    char dbfile[PATH_MAX + 1];
};
typedef struct _conf conf_t;

static void set_defaults(conf_t *);
static void parse_command_line(conf_t *, int, char *[]);
static void write_pid(const char *);

int main(int argc, char *argv[])
{
    conf_t conf;
    log4c_appender_t *appender;
    log4c_category_t *mainlog;
    
    set_defaults(&conf);
    parse_command_line(&conf, argc, argv);

    if(conf.setup) {
        dbcache_open(conf.dbfile);
        dbcache_setup_schema();
        dbcache_setup();
        drive_setup();
        dbcache_close();
    }

    if(conf.daemonize) {
        daemon(0, 0);
    }
    mkdir(conf.basedir, (mode_t)0700);
    mkdir(conf.cachedir, (mode_t)0700);
    mkdir(conf.mountpoint, (mode_t)0700);
    mkdir(conf.logdir, (mode_t)0700);

    log4c_init();

    appender = log4c_appender_new("rolling");
    log4c_appender_settype(appender, log4c_appender_type_rollingfile);

    mainlog = log4c_category_get("main");
    log4c_category_set_appender(mainlog, appender);

    log4c_category_debug(mainlog, "writing pidfile %s", conf.pidfile);
    write_pid(conf.pidfile);

    log4c_category_info(mainlog, "setting up filesystem cache %s",
            conf.cachedir);
    fscache_setup(conf.cachedir);

    dbcache_open(conf.dbfile);
    if(!conf.setup) {
        dbcache_setup();
    }

    drive_start();

    fuseapi_run(conf.mountpoint);

    drive_stop();

    dbcache_close();

    fscache_cleanup();

    log4c_fini();

    return 0;
}

static void set_defaults(conf_t *conf)
{
    const char *home;
    memset(conf, 0, sizeof(conf_t));
    home = getenv("HOME");
    if(home) {
        snprintf(conf->basedir, PATH_MAX, "%s/.drivefusesync", home);
        snprintf(conf->mountpoint, PATH_MAX, "%s/drive", home);
        snprintf(conf->logdir, PATH_MAX, "%s/log", home);
    }
}

static void parse_command_line(conf_t *conf, int argc, char *argv[])
{
    int o;
#define OPTS    "sdu:b:m:l:h"
    static struct option lopts[] = {
        {"setup", 0, NULL, 's'},
        {"daemonize", 0, NULL, 'd'},
        {"user-name", 1, NULL, 'u'},
        {"base-dir", 1, NULL, 'b'},
        {"mount-point", 1, NULL, 'm'},
        {"log-dir", 1, NULL, 'l'},
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
        case 'l':
            if(optarg) {
                strncpy(conf->logdir, optarg, PATH_MAX);
            }
            break;
        case 'h':
            printf("usage: %s "
                "[-s|--setup] "
                "[-d|--daemonize] "
                "[-b|--base-dir <BASEDIR>] "
                "[-m|--mount-point <MOUNTPOINT>] "
                "[-l|--log-dir <LOGDIR>] "
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

