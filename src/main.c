
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gd-api.h"
#include "dbcache.h"

struct _conf
{
    int daemonize;
    char pidfile[PATH_MAX + 1];
    char mountpoint[PATH_MAX + 1];
    char username[GD_USER_MAX + 1];
    char passwdfile[PATH_MAX + 1];
};
typedef struct _conf conf_t;

static int keep_running = 0;
static void killer(int);

static void set_defaults(conf_t *);
static void parse_command_line(conf_t *, int, char *[]);
static void write_pid(const conf_t *);
static void readpasswd(const conf_t *, char *, size_t);

int main(int argc, char *argv[])
{
    conf_t conf;
    char passwd[GD_PASSWD_MAX + 1];
    
    set_defaults(&conf);
    parse_command_line(&conf, argc, argv);

    if(conf.daemonize) {
        daemon(0, 0);
    }
    openlog(PACKAGE_NAME, LOG_CONS|LOG_PID, LOG_DAEMON);

    signal(SIGINT, killer);

    write_pid(&conf);

    syslog(LOG_INFO, "starting");

    dbcache_open("");

    memset(passwd, 0, (GD_PASSWD_MAX + 1) * sizeof(char));
    readpasswd(&conf, passwd, GD_PASSWD_MAX);
    gd_login(conf.username, passwd);
    memset(passwd, 0, (GD_PASSWD_MAX + 1) * sizeof(char));

    


    for(keep_running = 1; keep_running;) {
        /* check remote changes */
        /* check local changes */
        sleep(1);
    }

    gd_logout();

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
    strncpy(conf->pidfile, "/var/run/gdd.pid", PATH_MAX);
    home = getenv("HOME");
    if(home) {
        snprintf(conf->passwdfile, PATH_MAX, "%s/.gdd/passwd", home);
    }
}

static void parse_command_line(conf_t *conf, int argc, char *argv[])
{
    int o;
#define OPTS    "dpmUPh"
    static struct option lopts[] = {
        {"daemonize", 0, NULL, 'd'},
        {"pid-file", 1, NULL, 'p'},
        {"mount-point", 1, NULL, 'm'},
        {"user-name", 1, NULL, 'U'},
        {"password-file", 1, NULL, 'P'},
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
        case 'p':
            if(optarg) {
                strncpy(conf->pidfile, optarg, PATH_MAX);
            }
            break;
        case 'm':
            if(optarg) {
                strncpy(conf->mountpoint, optarg, PATH_MAX);
            }
            break;
        case 'U':
            if(optarg) {
                strncpy(conf->username, optarg, GD_USER_MAX);
            }
            break;
        case 'P':
            if(optarg) {
                strncpy(conf->passwdfile, optarg, PATH_MAX);
            }
            break;
        case 'h':
            printf("usage: %s "
                "[-d|--daemonize] "
                "[-p|--pid-file <PIDFILE>] "
                "-U|--user-name <USERNAME> "
                "-P|--password-file <PASSWORDFILE> "
                " | "
                "-h|--help\n", argv[0]);
            exit(0);
        }
    }
}

static void write_pid(const conf_t *conf)
{
    pid_t pid;
    FILE *f;

    pid = getpid();
    f = fopen(conf->pidfile, "w");
    if(f) {
        fprintf(f, "%d", (int)pid);
        fclose(f);
    }
}
static void readpasswd(const conf_t *conf, char *passwd, size_t len)
{
    FILE *f;
    f = fopen(conf->passwdfile, "r");
    if(f) {
        fread(passwd, sizeof(char), len, f);
        fclose(f);
    }
}

