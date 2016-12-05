
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct _conf
{
    int daemonize;
};
typedef struct _conf conf_t;

static int keep_running = 0;
static void killer(int);

static void set_defaults(conf_t *);
static void parse_command_line(conf_t *, int, char *[]);

int main(int argc, char *argv[])
{
    conf_t conf;
    
    set_defaults(&conf);
    parse_command_line(&conf, argc, argv);

    if(conf.daemonize) {
        daemon(0, 0);
    }

    signal(SIGINT, killer);

    for(keep_running = 1; keep_running;) {
        /* check remote changes */
        /* check local changes */
        sleep(1);
    }

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
    memset(conf, 0, sizeof(conf_t));
}

static void parse_command_line(conf_t *conf, int argc, char *argv[])
{
    int o;
#define OPTS    "dh"
    static struct option lopts[] = {
        {"daemonize", 0, NULL, 'd'},
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
        case 'h':
            printf("usage: %s "
                "-d|--daemonize "
                " | "
                "-h|--help\n", argv[0]);
            exit(0);
        }
    }
}


