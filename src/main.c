
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
#include "fuseapi.h"
#include "inotifyapi.h"

struct _conf
{
    int daemonize;
    char username[DRIVE_USER_MAX + 1];
    char passwd[DRIVE_PASSWD_MAX + 1];
    char basedir[PATH_MAX + 1];
    char mountpoint[PATH_MAX + 1];

    
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
    int64_t id;
    
    set_defaults(&conf);
    parse_command_line(&conf, argc, argv);

    if(conf.daemonize) {
        daemon(0, 0);
    }
    mkdir(conf.basedir, (mode_t)0700);
    mkdir(conf.mountpoint, (mode_t)0700);
    openlog(PACKAGE_NAME, LOG_CONS|LOG_PID, LOG_DAEMON);

    signal(SIGINT, killer);

    write_pid(conf.pidfile);

    syslog(LOG_INFO, "starting");

    dbcache_open(conf.dbfile);
    dbcache_updatepasswd(conf.passwd, DRIVE_PASSWD_MAX);

    dbcache_createdir(&id, "0000000a-000a-000a-000a-00000000000a",
        "a", 0755, 1, "a========", "00000000-0000-0000-0000-000000000000");
    dbcache_createdir(&id, "000000aa-00aa-00aa-00aa-0000000000aa",
        "aa", 0755, 1, "aa=======", "0000000a-000a-000a-000a-00000000000a");
    dbcache_createdir(&id, "000000ab-00ab-00ab-00ab-0000000000ab",
        "ab", 0755, 1, "ab=======", "0000000a-000a-000a-000a-00000000000a");
    dbcache_createdir(&id, "000000ac-00ac-00ac-00ac-0000000000ac",
        "ac", 0755, 1, "ac=======", "0000000a-000a-000a-000a-00000000000a");
    dbcache_createdir(&id, "0000000b-000b-000b-000b-00000000000b",
        "b", 0755, 1, "b========", "00000000-0000-0000-0000-000000000000");
    dbcache_createdir(&id, "0000000c-000c-000c-000c-00000000000c",
        "c", 0755, 1, "c========", "00000000-0000-0000-0000-000000000000");

    fuse_start(conf.mountpoint);
    

    drive_login(conf.username, conf.passwd);
    /* forget passwd immediately */
    memset(conf.passwd, 0, (DRIVE_PASSWD_MAX + 1) * sizeof(char));

    for(keep_running = 1; keep_running;) {
        /* check remote changes */
        /* check local changes */
        sleep(1);
    }

    drive_logout();

    fuse_stop();

    dbcache_close();

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
#define OPTS    "dU:P:b:m:h"
    static struct option lopts[] = {
        {"daemonize", 0, NULL, 'd'},
        {"user-name", 1, NULL, 'U'},
        {"password-file", 1, NULL, 'P'},
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
        case 'd':
            conf->daemonize = 1;
            break;
        case 'U':
            if(optarg) {
                strncpy(conf->username, optarg, DRIVE_USER_MAX);
            }
            break;
        case 'P':
            if(optarg) {
                strncpy(conf->passwd, optarg, DRIVE_PASSWD_MAX);
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
                "[-d|--daemonize] "
                "[-b|--base-dir <BASEDIR>] "
                "[-m|--mount-point <MOUNTPOINT>] "
                "-U|--user-name <USERNAME> "
                "[-P|--password <PASSWORD>] "
                " | "
                "-h|--help\n"
                "\n"
                "BASEDIR defaults to ${HOME}/.drivefusesync\n"
                "USER is the drive user\n"
                "PASSWORD will overwrite existing configuration (if any)\n"
                "\n", argv[0]);
            exit(0);
        }
    }

    if(0 == strlen(conf->username)) {
        fprintf(stderr, "no username provided\n");
        exit(1);
    }
    snprintf(conf->pidfile, PATH_MAX, "%s/%s.pid", conf->basedir, conf->username);
    snprintf(conf->dbfile, PATH_MAX, "%s/%s.db", conf->basedir, conf->username);
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

