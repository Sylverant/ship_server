/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2012, 2013, 2014, 2016, 2018, 2019, 2020,
                  2021, 2022, 2023, 2024 Lawrence Sebald

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License version 3
    as published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <gnutls/gnutls.h>

#include <sylverant/config.h>
#include <sylverant/debug.h>

#include <libxml/parser.h>

#if NEED_PIDFILE == 1
/* From pidfile.c */
struct pidfh;
struct pidfh *pidfile_open(const char *path, mode_t mode, pid_t *pidptr);
int pidfile_write(struct pidfh *pfh);
int pidfile_remove(struct pidfh *pfh);
int pidfile_fileno(struct pidfh *pfh);
#elif HAVE_LIBUTIL_H == 1
#include <libutil.h>
#elif HAVE_BSD_LIBUTIL_H == 1
#include <bsd/libutil.h>
#else
#error No pidfile functionality found!
#endif

#include "ship.h"
#include "clients.h"
#include "shipgate.h"
#include "utils.h"
#include "scripts.h"
#include "mapdata.h"
#include "ptdata.h"
#include "pmtdata.h"
#include "rtdata.h"
#include "admin.h"
#include "smutdata.h"
#include "version.h"

#ifndef PID_DIR
#define PID_DIR "/var/run"
#endif

#ifndef RUNAS_DEFAULT
#define RUNAS_DEFAULT "sylverant"
#endif

/* The actual ship structures. */
ship_t *ship;
int enable_ipv6 = 1;
int restart_on_shutdown = 0;
uint32_t ship_ip4;
uint8_t ship_ip6[16];

/* TLS stuff */
gnutls_certificate_credentials_t tls_cred;
gnutls_priority_t tls_prio;
static gnutls_dh_params_t dh_params;

static const char *config_file = NULL;
static const char *custom_dir = NULL;
static int dont_daemonize = 0;
static int check_only = 0;
static const char *pidfile_name = NULL;
static struct pidfh *pf = NULL;
static const char *runas_user = RUNAS_DEFAULT;

/* Print information about this program to stdout. */
static void print_program_info(void) {
    printf("Sylverant Ship Server version %s\n", VERSION);
    printf("Git Build: %s (Changeset: %s)\n", GIT_BUILD, GIT_SHAID_SHORT);
    printf("Copyright (C) 2009-2024 Lawrence Sebald\n\n");
    printf("This program is free software: you can redistribute it and/or\n"
           "modify it under the terms of the GNU Affero General Public\n"
           "License version 3 as published by the Free Software Foundation.\n\n"
           "This program is distributed in the hope that it will be useful,\n"
           "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
           "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
           "GNU General Public License for more details.\n\n"
           "You should have received a copy of the GNU Affero General Public\n"
           "License along with this program.  If not, see "
           "<http://www.gnu.org/licenses/>.\n");
}

/* Print help to the user to stdout. */
static void print_help(const char *bin) {
    printf("Usage: %s [arguments]\n"
           "-----------------------------------------------------------------\n"
           "--version       Print version info and exit\n"
           "--verbose       Log many messages that might help debug a problem\n"
           "--quiet         Only log warning and error messages\n"
           "--reallyquiet   Only log error messages\n"
           "-C configfile   Use the specified configuration instead of the\n"
           "                default one.\n"
           "-D directory    Use the specified directory as the root\n"
           "--nodaemon      Don't daemonize\n"
#ifdef SYLVERANT_ENABLE_IPV6
           "--no-ipv6       Disable IPv6 support for incoming connections\n"
#endif
           "--check-config  Load and parse the configuration, but do not\n"
           "                actually start the ship server. This implies the\n"
           "                --nodaemon option as well.\n"
           "-P filename     Use the specified name for the pid file to write\n"
           "                instead of the default.\n"
           "-U username     Run as the specified user instead of '%s'\n"
           "--help          Print this help and exit\n\n"
           "Note that if more than one verbosity level is specified, the last\n"
           "one specified will be used. The default is --verbose.\n", bin,
           RUNAS_DEFAULT);
}

/* Parse any command-line arguments passed in. */
static void parse_command_line(int argc, char *argv[]) {
    int i;

    for(i = 1; i < argc; ++i) {
        if(!strcmp(argv[i], "--version")) {
            print_program_info();
            exit(0);
        }
        else if(!strcmp(argv[i], "--verbose")) {
            debug_set_threshold(DBG_LOG);
        }
        else if(!strcmp(argv[i], "--quiet")) {
            debug_set_threshold(DBG_WARN);
        }
        else if(!strcmp(argv[i], "--reallyquiet")) {
            debug_set_threshold(DBG_ERROR);
        }
        else if(!strcmp(argv[i], "-C")) {
            /* Save the config file's name. */
            if(i == argc - 1) {
                printf("-C requires an argument!\n\n");
                print_help(argv[0]);
                exit(EXIT_FAILURE);
            }

            config_file = argv[++i];
        }
        else if(!strcmp(argv[i], "-D")) {
            /* Save the custom dir */
            if(i == argc - 1) {
                printf("-D requires an argument!\n\n");
                print_help(argv[0]);
                exit(EXIT_FAILURE);
            }

            custom_dir = argv[++i];
        }
        else if(!strcmp(argv[i], "--nodaemon")) {
            dont_daemonize = 1;
        }
        else if(!strcmp(argv[i], "--no-ipv6")) {
            enable_ipv6 = 0;
        }
        else if(!strcmp(argv[i], "--check-config")) {
            check_only = 1;
            dont_daemonize = 1;
        }
        else if(!strcmp(argv[i], "-P")) {
            if(i == argc - 1) {
                printf("-P requires an argument!\n\n");
                print_help(argv[0]);
                exit(EXIT_FAILURE);
            }

            pidfile_name = argv[++i];
        }
        else if(!strcmp(argv[i], "-U")) {
            if(i == argc - 1) {
                printf("-U requires an argument!\n\n");
                print_help(argv[0]);
                exit(EXIT_FAILURE);
            }

            runas_user = argv[++i];
        }
        else if(!strcmp(argv[i], "--help")) {
            print_help(argv[0]);
            exit(EXIT_SUCCESS);
        }
        else {
            printf("Illegal command line argument: %s\n", argv[i]);
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }
    }
}

/* Load the configuration file and print out parameters with DBG_LOG. */
static sylverant_ship_t *load_config(void) {
    sylverant_ship_t *cfg;

    if(sylverant_read_ship_config(config_file, &cfg)) {
        debug(DBG_ERROR, "Cannot load Sylverant Ship configuration file!\n");
        exit(EXIT_FAILURE);
    }

    return cfg;
}

static void print_config(sylverant_ship_t *cfg) {
    int i;

    /* Print out the configuration. */
    debug(DBG_LOG, "Configured parameters:\n");

    debug(DBG_LOG, "Shipgate Host: %s\n", cfg->shipgate_host);
    debug(DBG_LOG, "Shipgate Port: %d\n", (int)cfg->shipgate_port);

    /* Print out the ship's information. */
    debug(DBG_LOG, "Ship Name: %s\n", cfg->name);

    debug(DBG_LOG, "Ship IPv4 Host: %s\n", cfg->ship_host);

    if(cfg->ship_host6)
        debug(DBG_LOG, "Ship IPv6 Host: %s\n", cfg->ship_host6);
    else
        debug(DBG_LOG, "Ship IPv6 Host: Autoconfig or None\n");

    debug(DBG_LOG, "Base Port: %d\n", (int)cfg->base_port);
    debug(DBG_LOG, "Blocks: %d\n", cfg->blocks);
    debug(DBG_LOG, "Default Lobby Event: %d\n", cfg->events[0].lobby_event);
    debug(DBG_LOG, "Default Game Event: %d\n", cfg->events[0].game_event);

    if(cfg->event_count != 1) {
        for(i = 1; i < cfg->event_count; ++i) {
            debug(DBG_LOG, "Event (%d-%d through %d-%d):\n",
                  cfg->events[i].start_month, cfg->events[i].start_day,
                  cfg->events[i].end_month, cfg->events[i].end_day);
            debug(DBG_LOG, "\tLobby: %d, Game: %d\n",
                  cfg->events[i].lobby_event, cfg->events[i].game_event);
        }
    }

    if(cfg->menu_code)
        debug(DBG_LOG, "Menu: %c%c\n", (char)cfg->menu_code,
              (char)(cfg->menu_code >> 8));
    else
        debug(DBG_LOG, "Menu: Main\n");

    if(cfg->v2_map_dir)
        debug(DBG_LOG, "v2 Map Directory: %s\n", cfg->v2_map_dir);

    if(cfg->gc_map_dir)
        debug(DBG_LOG, "GC Map Directory: %s\n", cfg->bb_map_dir);

    if(cfg->bb_param_dir)
        debug(DBG_LOG, "BB Param Directory: %s\n", cfg->bb_param_dir);

    if(cfg->v2_param_dir)
        debug(DBG_LOG, "v2 Param Directory: %s\n", cfg->v2_param_dir);

    if(cfg->bb_map_dir)
        debug(DBG_LOG, "BB Map Directory: %s\n", cfg->bb_map_dir);

    if(cfg->v2_ptdata_file)
        debug(DBG_LOG, "v2 ItemPT file: %s\n", cfg->v2_ptdata_file);

    if(cfg->gc_ptdata_file)
        debug(DBG_LOG, "GC ItemPT file: %s\n", cfg->gc_ptdata_file);

    if(cfg->bb_ptdata_file)
        debug(DBG_LOG, "BB ItemPT file: %s\n", cfg->bb_ptdata_file);

    if(cfg->v2_pmtdata_file)
        debug(DBG_LOG, "v2 ItemPMT file: %s\n", cfg->v2_pmtdata_file);

    if(cfg->gc_pmtdata_file)
        debug(DBG_LOG, "GC ItemPMT file: %s\n", cfg->gc_pmtdata_file);

    if(cfg->bb_pmtdata_file)
        debug(DBG_LOG, "BB ItemPMT file: %s\n", cfg->bb_pmtdata_file);

    debug(DBG_LOG, "Units +/- limit: v2: %s, GC: %s, BB: %s\n",
          (cfg->local_flags & SYLVERANT_SHIP_PMT_LIMITV2) ? "true" : "false",
          (cfg->local_flags & SYLVERANT_SHIP_PMT_LIMITGC) ? "true" : "false",
          (cfg->local_flags & SYLVERANT_SHIP_PMT_LIMITBB) ? "true" : "false");

    if(cfg->v2_rtdata_file)
        debug(DBG_LOG, "v2 ItemRT file: %s\n", cfg->v2_rtdata_file);

    if(cfg->gc_rtdata_file)
        debug(DBG_LOG, "GC ItemRT file: %s\n", cfg->gc_rtdata_file);

    if(cfg->bb_rtdata_file)
        debug(DBG_LOG, "BB ItemRT file: %s\n", cfg->bb_rtdata_file);

    if(cfg->v2_rtdata_file || cfg->gc_rtdata_file || cfg->bb_rtdata_file) {
        debug(DBG_LOG, "Rares drop in quests: %s\n",
              (cfg->local_flags & SYLVERANT_SHIP_QUEST_RARES) ? "true" :
              "false");
        debug(DBG_LOG, "Semi-rares drop in quests: %s\n",
              (cfg->local_flags & SYLVERANT_SHIP_QUEST_SRARES) ? "true" :
              "false");
    }

    if(cfg->smutdata_file)
        debug(DBG_LOG, "Smutdata file: %s\n", cfg->smutdata_file);

    if(cfg->limits_count) {
        debug(DBG_LOG, "%d /legit files configured:\n", cfg->limits_count);

        for(i = 0; i < cfg->limits_count; ++i) {
            debug(DBG_LOG, "%d: \"%s\": %s\n", i, cfg->limits[i].name,
                  cfg->limits[i].filename);
        }

        debug(DBG_LOG, "Default /legit file number: %d\n", cfg->limits_default);
    }

    debug(DBG_LOG, "Shipgate Flags: 0x%08X\n", cfg->shipgate_flags);
    debug(DBG_LOG, "Supported versions:\n");

    if(!(cfg->shipgate_flags & SHIPGATE_FLAG_NODCNTE))
        debug(DBG_LOG, "Dreamcast Network Trial Edition\n");
    if(!(cfg->shipgate_flags & SHIPGATE_FLAG_NOV1))
        debug(DBG_LOG, "Dreamcast Version 1\n");
    if(!(cfg->shipgate_flags & SHIPGATE_FLAG_NOV2))
        debug(DBG_LOG, "Dreamcast Version 2\n");
    if(!(cfg->shipgate_flags & SHIPGATE_FLAG_NOPCNTE))
        debug(DBG_LOG, "PSO for PC Network Trial Edition\n");
    if(!(cfg->shipgate_flags & SHIPGATE_FLAG_NOPC))
        debug(DBG_LOG, "PSO for PC\n");
    if(!(cfg->shipgate_flags & SHIPGATE_FLAG_NOEP12))
        debug(DBG_LOG, "Gamecube Episode I & II\n");
    if(!(cfg->shipgate_flags & SHIPGATE_FLAG_NOEP3))
        debug(DBG_LOG, "Gamecube Episode III\n");
    if(!(cfg->shipgate_flags & SHIPGATE_FLAG_NOPSOX))
        debug(DBG_LOG, "Xbox Episode I & II\n");
    if(!(cfg->shipgate_flags & SHIPGATE_FLAG_NOBB))
        debug(DBG_LOG, "Blue Burst\n");
}

static void open_log(sylverant_ship_t *cfg) {
    char fn[strlen(cfg->name) + 32];
    FILE *dbgfp;

    sprintf(fn, "logs/%s_debug.log", cfg->name);
    dbgfp = fopen(fn, "a");

    if(!dbgfp) {
        debug(DBG_ERROR, "Cannot open log file\n");
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    debug_set_file(dbgfp);
}

static void reopen_log(void) {
    char fn[strlen(ship->cfg->name) + 32];
    FILE *dbgfp, *ofp;

    sprintf(fn, "logs/%s_debug.log", ship->cfg->name);
    dbgfp = fopen(fn, "a");

    if(!dbgfp) {
        /* Uhh... Welp, guess we'll try to continue writing to the old one,
           then... */
        debug(DBG_ERROR, "Cannot reopen log file\n");
        perror("fopen");
    }
    else {
        ofp = debug_set_file(dbgfp);
        fclose(ofp);
    }
}

static void sighup_hnd(int signum, siginfo_t *inf, void *ptr) {
    (void)signum;
    (void)inf;
    (void)ptr;
    reopen_log();
}

static void sigterm_hnd(int signum, siginfo_t *inf, void *ptr) {
    (void)signum;
    (void)inf;
    (void)ptr;

    /* Now, shutdown with slightly more grace! */
    schedule_shutdown(NULL, 0, 0, NULL);
}

static void sigusr1_hnd(int signum, siginfo_t *inf, void *ptr) {
    (void)signum;
    (void)inf;
    (void)ptr;
    schedule_shutdown(NULL, 0, 1, NULL);
}

/* Install any handlers for signals we care about */
static void install_signal_handlers() {
    struct sigaction sa;

    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);

    /* Ignore SIGPIPEs */
    sa.sa_handler = SIG_IGN;

    if(sigaction(SIGPIPE, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    /* Set up a SIGHUP handler to reopen the log file, if we do log rotation. */
    if(!dont_daemonize) {
        sigemptyset(&sa.sa_mask);
        sa.sa_handler = NULL;
        sa.sa_sigaction = &sighup_hnd;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;

        if(sigaction(SIGHUP, &sa, NULL) < 0) {
            perror("sigaction");
            fprintf(stderr, "Can't set SIGHUP handler, log rotation may not"
                    "work.\n");
        }
    }

    /* Set up a SIGTERM and SIGINT handlers to somewhat gracefully shutdown. */
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = NULL;
    sa.sa_sigaction = &sigterm_hnd;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;

    if(sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("sigaction");
        fprintf(stderr, "Can't set SIGTERM handler.\n");
    }

    sigemptyset(&sa.sa_mask);
    sa.sa_handler = NULL;
    sa.sa_sigaction = &sigterm_hnd;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;

    if(sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction");
        fprintf(stderr, "Can't set SIGINT handler.\n");
    }

    /* Set up a SIGUSR1 handler to restart... */
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = NULL;
    sa.sa_sigaction = &sigusr1_hnd;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;

    if(sigaction(SIGUSR1, &sa, NULL) < 0) {
        perror("sigaction");
        fprintf(stderr, "Can't set SIGUSR1 handler.\n");
    }
}

static int init_gnutls(sylverant_ship_t *cfg) {
    int rv;

    /* Do the global init */
    gnutls_global_init();

    /* Set up our credentials */
    if((rv = gnutls_certificate_allocate_credentials(&tls_cred))) {
        debug(DBG_ERROR, "Cannot allocate GnuTLS credentials: %s (%s)\n",
              gnutls_strerror(rv), gnutls_strerror_name(rv));
        return -1;
    }

    if((rv = gnutls_certificate_set_x509_trust_file(tls_cred, cfg->shipgate_ca,
                                                    GNUTLS_X509_FMT_PEM) < 0)) {
        debug(DBG_ERROR, "Cannot set GnuTLS CA Certificate: %s (%s)\n",
              gnutls_strerror(rv), gnutls_strerror_name(rv));
        return -1;
    }

    if((rv = gnutls_certificate_set_x509_key_file(tls_cred, cfg->ship_cert,
                                                  cfg->ship_key,
                                                  GNUTLS_X509_FMT_PEM))) {
        debug(DBG_ERROR, "Cannot set GnuTLS key file: %s (%s)\n",
              gnutls_strerror(rv), gnutls_strerror_name(rv));
        return -1;
    }

    /* Generate Diffie-Hellman parameters */
    debug(DBG_LOG, "Generating Diffie-Hellman parameters...\n"
          "This may take a little while.\n");
    if((rv = gnutls_dh_params_init(&dh_params))) {
        debug(DBG_ERROR, "Cannot initialize GnuTLS DH parameters: %s (%s)\n",
              gnutls_strerror(rv), gnutls_strerror_name(rv));
        return -1;
    }

    if((rv = gnutls_dh_params_generate2(dh_params, 1024))) {
        debug(DBG_ERROR, "Cannot generate GnuTLS DH parameters: %s (%s)\n",
              gnutls_strerror(rv), gnutls_strerror_name(rv));
        return -1;
    }

    debug(DBG_LOG, "Done!\n");

    /* Set our priorities */
    if((rv = gnutls_priority_init(&tls_prio, "NORMAL:+COMP-DEFLATE", NULL))) {
        debug(DBG_ERROR, "Cannot initialize GnuTLS priorities: %s (%s)\n",
              gnutls_strerror(rv), gnutls_strerror_name(rv));
        return -1;
    }

    /* Set the Diffie-Hellman parameters */
    gnutls_certificate_set_dh_params(tls_cred, dh_params);

    return 0;
}

static void cleanup_gnutls() {
    gnutls_dh_params_deinit(dh_params);
    gnutls_certificate_free_credentials(tls_cred);
    gnutls_priority_deinit(tls_prio);
    gnutls_global_deinit();
}

int setup_addresses(sylverant_ship_t *cfg) {
    struct addrinfo hints;
    struct addrinfo *server, *j;
    char ipstr[INET6_ADDRSTRLEN];
    struct sockaddr_in *addr4;
    struct sockaddr_in6 *addr6;

    /* Clear the addresses */
    ship_ip4 = 0;
    memset(ship_ip6, 0, 16);

    debug(DBG_LOG, "Looking up ship address...\n");

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if(getaddrinfo(cfg->ship_host, "9000", &hints, &server)) {
        debug(DBG_ERROR, "Invalid ship address: %s\n", cfg->ship_host);
        return -1;
    }

    for(j = server; j != NULL; j = j->ai_next) {
        if(j->ai_family == AF_INET) {
            addr4 = (struct sockaddr_in *)j->ai_addr;
            inet_ntop(j->ai_family, &addr4->sin_addr, ipstr, INET6_ADDRSTRLEN);
            debug(DBG_LOG, "    Found IPv4: %s\n", ipstr);
            ship_ip4 = addr4->sin_addr.s_addr;
        }
        else if(j->ai_family == AF_INET6) {
            addr6 = (struct sockaddr_in6 *)j->ai_addr;
            inet_ntop(j->ai_family, &addr6->sin6_addr, ipstr, INET6_ADDRSTRLEN);
            debug(DBG_LOG, "    Found IPv6: %s\n", ipstr);
            memcpy(ship_ip6, &addr6->sin6_addr, 16);
        }
    }

    freeaddrinfo(server);

    /* Make sure we found at least an IPv4 address */
    if(!ship_ip4) {
        debug(DBG_ERROR, "No IPv4 Address found!\n");
        return -1;
    }

    /* If we don't have a separate IPv6 host set, we're done. */
    if(!cfg->ship_host6) {
        return 0;
    }

    /* Now try with IPv6 only */
    memset(ship_ip6, 0, 16);
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;

    if(getaddrinfo(cfg->ship_host6, "9000", &hints, &server)) {
        debug(DBG_ERROR, "Invalid ship address (v6): %s\n", cfg->ship_host);
        return -1;
    }

    for(j = server; j != NULL; j = j->ai_next) {
        if(j->ai_family == AF_INET6) {
            addr6 = (struct sockaddr_in6 *)j->ai_addr;
            inet_ntop(j->ai_family, &addr6->sin6_addr, ipstr, INET6_ADDRSTRLEN);
            debug(DBG_LOG, "    Found IPv6: %s\n", ipstr);
            memcpy(ship_ip6, &addr6->sin6_addr, 16);
        }
    }

    freeaddrinfo(server);

    if(!ship_ip6[0]) {
        debug(DBG_ERROR, "No IPv6 Address found (but addr6 configured)!\n");
        return -1;
    }

    return 0;
}

void cleanup_pidfile(void) {
    pidfile_remove(pf);
}

static int drop_privs(void) {
    struct passwd *pw;
    uid_t uid;
    gid_t gid;
    int gid_count = 0;
    gid_t *groups;

    /* Make sure we're actually root, otherwise some of this will fail. */
    if(getuid() && geteuid())
        return 0;

    /* Look for users. We're looking for the user "sylverant", generally. */
    if((pw = getpwnam(runas_user))) {
        uid = pw->pw_uid;
        gid = pw->pw_gid;
    }
    else {
        debug(DBG_ERROR, "Cannot find user \"%s\". Bailing out!\n", runas_user);
        return -1;
    }

    /* Change the pidfile's uid/gid now, before we drop privileges... */
    if(pf) {
        if(fchown(pidfile_fileno(pf), uid, gid)) {
            debug(DBG_WARN, "Cannot change pidfile owner: %s\n",
                  strerror(errno));
        }
    }

#ifdef HAVE_GETGROUPLIST
    /* Figure out what other groups the user is in... */
    getgrouplist(runas_user, gid, NULL, &gid_count);
    if(!(groups = malloc(gid_count * sizeof(gid_t)))) {
        perror("malloc");
        return -1;
    }

    if(getgrouplist(runas_user, gid, groups, &gid_count)) {
        perror("getgrouplist");
        free(groups);
        return -1;
    }

    if(setgroups(gid_count, groups)) {
        perror("setgroups");
        free(groups);
        return -1;
    }

    /* We're done with most of these, so clear this out now... */
    free(groups);
#else
    if(setgroups(1, &gid)) {
        perror("setgroups");
        return -1;
    }
#endif

    if(setgid(gid)) {
        perror("setgid");
        return -1;
    }

    if(setuid(uid)) {
        perror("setuid");
        return -1;
    }

    /* Make sure the privileges stick. */
    if(!getuid() || !geteuid()) {
        debug(DBG_ERROR, "Cannot set non-root privileges. Bailing out!\n");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    void *tmp;
    sylverant_ship_t *cfg;
    char *initial_path;
    long size;
    int rv;
    pid_t op;

    /* Parse the command line... */
    parse_command_line(argc, argv);

    /* Save the initial path, so that if /restart is used we'll be starting from
       the same directory. */
    size = pathconf(".", _PC_PATH_MAX);
    if(!(initial_path = (char *)malloc(size))) {
        debug(DBG_WARN, "Out of memory, bailing out!\n");
    }
    else if(!getcwd(initial_path, size)) {
        debug(DBG_WARN, "Cannot save initial path, /restart may not work!\n");
    }

    cfg = load_config();

    if(!custom_dir) {
        chdir(sylverant_directory);
    }
    else {
        chdir(custom_dir);
    }

    /* If we're still alive and we're supposed to daemonize, do it now. */
    if(!dont_daemonize) {
        /* Attempt to open and lock the pid file. */
        if(!pidfile_name) {
            char *pn = (char *)malloc(strlen(cfg->name) + strlen(PID_DIR) + 32);
            sprintf(pn, "%s/ship_server-%s.pid", PID_DIR, cfg->name);
            pidfile_name = pn;
        }

        pf = pidfile_open(pidfile_name, 0660, &op);

        if(!pf) {
            if(errno == EEXIST) {
                debug(DBG_ERROR, "Ship Server already running? (pid: %ld)\n",
                      (long)op);
                exit(EXIT_FAILURE);
            }

            debug(DBG_WARN, "Cannot create pidfile: %s!\n", strerror(errno));
        }
        else {
            atexit(&cleanup_pidfile);
        }

        if(daemon(1, 0)) {
            debug(DBG_ERROR, "Cannot daemonize\n");
            perror("daemon");
            exit(EXIT_FAILURE);
        }

        if(drop_privs())
            exit(EXIT_FAILURE);

        open_log(cfg);

        /* Write the pid file. */
        pidfile_write(pf);
    }
    else {
        if(drop_privs())
            exit(EXIT_FAILURE);
    }

restart:
    print_config(cfg);

    /* Parse the addresses */
    if(setup_addresses(cfg))
        exit(EXIT_FAILURE);

    /* Initialize GnuTLS stuff... */
    if(!check_only) {
        if(init_gnutls(cfg))
            exit(EXIT_FAILURE);

        /* Set up things for clients to connect. */
        if(client_init(cfg))
            exit(EXIT_FAILURE);
    }

    /* Try to read the v2 ItemPT data... */
    if(cfg->v2_ptdata_file) {
        debug(DBG_LOG, "Reading v2 ItemPT file: %s\n", cfg->v2_ptdata_file);
        if(pt_read_v2(cfg->v2_ptdata_file)) {
            debug(DBG_WARN, "Couldn't read v2 ItemPT data!\n");
        }
    }

    /* Read the v2 ItemPMT file... */
    if(cfg->v2_pmtdata_file) {
        debug(DBG_LOG, "Reading v2 ItemPMT file: %s\n", cfg->v2_pmtdata_file);
        if(pmt_read_v2(cfg->v2_pmtdata_file,
                       !(cfg->local_flags & SYLVERANT_SHIP_PMT_LIMITV2))) {
            debug(DBG_WARN, "Couldn't read v2 ItemPMT file!\n");
        }
    }

    /* Read the GC ItemPT file... */
    if(cfg->gc_ptdata_file) {
        debug(DBG_LOG, "Reading GC ItemPT file: %s\n", cfg->gc_ptdata_file);

        if(pt_read_v3(cfg->gc_ptdata_file, 0)) {
            debug(DBG_WARN, "Couldn't read GC ItemPT file!\n");
        }
    }

    /* Read the BB ItemPT data, which is needed for Blue Burst... */
    if(cfg->bb_ptdata_file) {
        debug(DBG_LOG, "Reading BB ItemPT file: %s\n", cfg->bb_ptdata_file);

        if(pt_read_v3(cfg->bb_ptdata_file, 1)) {
            debug(DBG_WARN, "Couldn't read BB ItemPT data, disabling Blue "
                  "Burst support!\n");
            cfg->shipgate_flags |= SHIPGATE_FLAG_NOBB;
        }
    }
    else {
        debug(DBG_WARN, "No BB ItemPT file specified, disabling Blue Burst "
              "support!\n");
        cfg->shipgate_flags |= SHIPGATE_FLAG_NOBB;
    }

    /* Read the GC ItemPMT file... */
    if(cfg->gc_pmtdata_file) {
        debug(DBG_LOG, "Reading GC ItemPMT file: %s\n", cfg->gc_pmtdata_file);
        if(pmt_read_gc(cfg->gc_pmtdata_file,
                       !(cfg->local_flags & SYLVERANT_SHIP_PMT_LIMITGC))) {
            debug(DBG_WARN, "Couldn't read GC ItemPMT file!\n");
        }
    }

    /* Read the BB ItemPMT file... */
    if(cfg->bb_pmtdata_file) {
        debug(DBG_LOG, "Reading BB ItemPMT file: %s\n", cfg->bb_pmtdata_file);
        if(pmt_read_bb(cfg->bb_pmtdata_file,
                       !(cfg->local_flags & SYLVERANT_SHIP_PMT_LIMITBB))) {
            debug(DBG_WARN, "Couldn't read BB ItemPMT file!\n");
            cfg->shipgate_flags |= SHIPGATE_FLAG_NOBB;
        }
    }
    else {
        debug(DBG_WARN, "No BB ItemPT file specified, disabling Blue Burst "
              "support!\n");
        cfg->shipgate_flags |= SHIPGATE_FLAG_NOBB;
    }

    /* If we have a v2 map dir set, try to read the maps. */
    if(cfg->v2_map_dir) {
        rv = v2_read_params(cfg);

        if(rv < 0)
            exit(EXIT_FAILURE);
    }

    /* If we have a GC map dir set, try to read the maps. */
    if(cfg->gc_map_dir) {
        rv = gc_read_params(cfg);

        if(rv < 0)
            exit(EXIT_FAILURE);
    }

    /* Read the v2 ItemRT file... */
    if(cfg->v2_rtdata_file) {
        debug(DBG_LOG, "Reading v2 ItemRT file: %s\n", cfg->v2_rtdata_file);
        if(rt_read_v2(cfg->v2_rtdata_file)) {
            debug(DBG_WARN, "Couldn't read v2 ItemRT file!\n");
        }
    }

    /* Read the GC ItemRT file... */
    if(cfg->gc_rtdata_file) {
        debug(DBG_LOG, "Reading GC ItemRT file: %s\n", cfg->gc_rtdata_file);
        if(rt_read_gc(cfg->gc_rtdata_file)) {
            debug(DBG_WARN, "Couldn't read GC ItemRT file!\n");
        }
    }

    /* If Blue Burst isn't disabled already, read the parameter data and map
       data... */
    if(!(cfg->shipgate_flags & SHIPGATE_FLAG_NOBB)) {
        rv = bb_read_params(cfg);

        /* Less than 0 = fatal error. Greater than 0 = Blue Burst problem. */
        if(rv > 0)
            cfg->shipgate_flags |= SHIPGATE_FLAG_NOBB;
        else if(rv < 0)
            exit(EXIT_FAILURE);
    }

    /* Set a few other shipgate flags, if appropriate. */
#ifdef ENABLE_LUA
    cfg->shipgate_flags |= LOGIN_FLAG_LUA;
#endif

#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__)
    cfg->shipgate_flags |= LOGIN_FLAG_BE;
#endif

#if (SIZEOF_VOID_P == 4)
    cfg->shipgate_flags |= LOGIN_FLAG_32BIT;
#endif

    /* Initialize all the iconv contexts we'll need */
    if(init_iconv())
        exit(EXIT_FAILURE);

    /* Init mini18n if we have it */
    init_i18n();

    /* Init the word censor. */
    if(cfg->smutdata_file) {
        debug(DBG_LOG, "Reading smutdata file: %s\n", cfg->smutdata_file);
        if(smutdata_read(cfg->smutdata_file)) {
            debug(DBG_WARN, "Couldn't read smutdata file!\n");
        }
    }

    if(!check_only) {
        /* Install signal handlers */
        install_signal_handlers();

        /* Set up the ship and start it. */
        ship = ship_server_start(cfg);
        if(ship)
            pthread_join(ship->thd, NULL);

        /* Clean up... */
        if((tmp = pthread_getspecific(sendbuf_key))) {
            free(tmp);
            pthread_setspecific(sendbuf_key, NULL);
        }

        if((tmp = pthread_getspecific(recvbuf_key))) {
            free(tmp);
            pthread_setspecific(recvbuf_key, NULL);
        }
    }
    else {
        ship_check_cfg(cfg);
    }

    smutdata_cleanup();
    cleanup_i18n();
    cleanup_iconv();

    if(!check_only) {
        client_shutdown();
        cleanup_gnutls();
    }

    sylverant_free_ship_config(cfg);
    bb_free_params();
    v2_free_params();
    gc_free_params();
    pmt_cleanup();

    if(restart_on_shutdown) {
        cfg = load_config();
        goto restart;
    }

    free(initial_path);
    xmlCleanupParser();

    return 0;
}
