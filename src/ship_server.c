/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2012 Lawrence Sebald

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
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <gnutls/gnutls.h>

#include <sylverant/config.h>
#include <sylverant/debug.h>
#include <sylverant/mtwist.h>

#include <libxml/parser.h>

#include "ship.h"
#include "clients.h"
#include "shipgate.h"
#include "utils.h"
#include "scripts.h"
#include "mapdata.h"
#include "ptdata.h"

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

/* Print information about this program to stdout. */
static void print_program_info(void) {
    printf("Sylverant Ship Server version %s\n", VERSION);
    printf("SVN Revision: %s\n", SVN_REVISION);
    printf("Copyright (C) 2009, 2010, 2011, 2012 Lawrence Sebald\n\n");
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
           "--help          Print this help and exit\n\n"
           "Note that if more than one verbosity level is specified, the last\n"
           "one specified will be used. The default is --verbose.\n", bin);
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
            config_file = argv[++i];
        }
        else if(!strcmp(argv[i], "-D")) {
            /* Save the custom dir */
            custom_dir = argv[++i];
        }
        else if(!strcmp(argv[i], "--nodaemon")) {
            dont_daemonize = 1;
        }
        else if(!strcmp(argv[i], "--no-ipv6")) {
            enable_ipv6 = 0;
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

    if(cfg->ship_host6) {
        debug(DBG_LOG, "Ship IPv6 Host: %s\n", cfg->ship_host6);
    }
    else {
        debug(DBG_LOG, "Ship IPv6 Host: Autoconfig or None\n");
    }

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

    if(cfg->menu_code) {
        debug(DBG_LOG, "Menu: %c%c\n", (char)cfg->menu_code,
              (char)(cfg->menu_code >> 8));
    }
    else {
        debug(DBG_LOG, "Menu: Main\n");
    }

    if(cfg->bb_param_dir) {
        debug(DBG_LOG, "BB Param Directory: %s\n", cfg->bb_param_dir);
    }

    if(cfg->bb_map_dir) {
        debug(DBG_LOG, "BB Map Directory: %s\n", cfg->bb_map_dir);
    }

    if(cfg->v2_ptdata_file) {
        debug(DBG_LOG, "v2 ItemPT file: %s\n", cfg->v2_ptdata_file);
    }

    if(cfg->v3_ptdata_file) {
        debug(DBG_LOG, "v3 ItemPT file: %s\n", cfg->v3_ptdata_file);
    }

    debug(DBG_LOG, "Flags: 0x%08X\n", cfg->shipgate_flags);
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
}

static int init_gnutls(sylverant_ship_t *cfg) {
    int rv;

    /* XXXX: Should really check return values in here */
    /* Do the global init */
    gnutls_global_init();

    /* Set up our credentials */
    rv = gnutls_certificate_allocate_credentials(&tls_cred);
    rv = gnutls_certificate_set_x509_trust_file(tls_cred, cfg->shipgate_ca,
                                                GNUTLS_X509_FMT_PEM);
    rv = gnutls_certificate_set_x509_key_file(tls_cred, cfg->ship_cert,
                                              cfg->ship_key,
                                              GNUTLS_X509_FMT_PEM);

    /* Generate Diffie-Hellman parameters */
    debug(DBG_LOG, "Generating Diffie-Hellman parameters...\n"
          "This may take a little while.\n");
    rv = gnutls_dh_params_init(&dh_params);
    rv = gnutls_dh_params_generate2(dh_params, 1024);
    debug(DBG_LOG, "Done!\n");

    /* Set our priorities */
    rv = gnutls_priority_init(&tls_prio, "NORMAL:+COMP-DEFLATE", NULL);

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

int main(int argc, char *argv[]) {
    void *tmp;
    sylverant_ship_t *cfg;
    char *initial_path;
    long size;
    int rv;

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
        open_log(cfg);

        if(daemon(1, 0)) {
            debug(DBG_ERROR, "Cannot daemonize\n");
            perror("daemon");
            exit(EXIT_FAILURE);
        }
    }

    print_config(cfg);

    /* Parse the addresses */
    if(setup_addresses(cfg)) {
        exit(EXIT_FAILURE);
    }

    /* Initialize GnuTLS stuff... */
    if(init_gnutls(cfg)) {
        exit(EXIT_FAILURE);
    }

    /* Set up things for clients to connect. */
    if(client_init(cfg)) {
        exit(EXIT_FAILURE);
    }

    /* Try to read the v2 ItemPT data... */
    if(cfg->v2_ptdata_file) {
        debug(DBG_LOG, "Reading v2 ItemPT file: %s\n", cfg->v2_ptdata_file);
        if(pt_read_v2(cfg->v2_ptdata_file)) {
            debug(DBG_WARN, "Couldn't read v2 ItemPT data!\n");
        }
    }

    /* Read the v3 ItemPT data, which is needed for Blue Burst... */
    if(cfg->v3_ptdata_file) {
        debug(DBG_LOG, "Reading v3 ItemPT file: %s\n", cfg->v3_ptdata_file);

        if(pt_read_v3(cfg->v3_ptdata_file)) {
            debug(DBG_WARN, "Couldn't read v3 ItemPT data, disabling Blue "
                  "Burst support!\n");
            cfg->shipgate_flags |= SHIPGATE_FLAG_NOBB;
        }
    }
    else {
        debug(DBG_WARN, "No v3 ItemPT file specified, disabling Blue Burst "
              "support!\n");
        cfg->shipgate_flags |= SHIPGATE_FLAG_NOBB;
    }

    /* If Blue Burst isn't disabled already, read the parameter data and map
       data... */
    if(!(cfg->shipgate_flags & SHIPGATE_FLAG_NOBB)) {
        rv = bb_read_params(cfg);

        /* Less than 0 = fatal error. Greater than 0 = Blue Burst problem. */
        if(rv > 0) {
            cfg->shipgate_flags |= SHIPGATE_FLAG_NOBB;
        }
        else if(rv < 0) {
            exit(EXIT_FAILURE);
        }
    }

    /* Initialize all the iconv contexts we'll need */
    if(init_iconv()) {
        exit(EXIT_FAILURE);
    }

    /* Init mini18n if we have it */
    init_i18n();

    /* Initialize the random number generator and install signal handlers */
    init_genrand(time(NULL));
    install_signal_handlers();

    /* Set up the ship and start it. */
    ship = ship_server_start(cfg);
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

    cleanup_i18n();
    cleanup_iconv();
    client_shutdown();
    cleanup_gnutls();
    sylverant_free_ship_config(cfg);
    bb_free_params();

    if(restart_on_shutdown) {
        chdir(initial_path);
        free(initial_path);
        execvp(argv[0], argv);

        /* This should never be reached, since execvp should replace us. If we
           get here, there was a serious problem... */
        debug(DBG_ERROR, "Restart failed: %s\n", strerror(errno));
        return -1;
    }

    free(initial_path);
    xmlCleanupParser();

    return 0;
}
