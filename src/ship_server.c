/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011 Lawrence Sebald

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

#include <sylverant/config.h>
#include <sylverant/debug.h>
#include <sylverant/mtwist.h>

#include "ship.h"
#include "clients.h"
#include "shipgate.h"
#include "utils.h"
#include "scripts.h"

/* The actual ship structures. */
ship_t *ship;
static char *config_file = NULL;
static const char *custom_dir = NULL;
static int dont_daemonize = 0;

/* Print information about this program to stdout. */
static void print_program_info(void) {
    printf("Sylverant Ship Server version %s\n", VERSION);
    printf("Copyright (C) 2009, 2010, 2011 Lawrence Sebald\n\n");
    printf("This program is free software: you can redistribute it and/or\n"
           "modify it under the terms of the GNU Affero General Public\n"
           "License version 3 as published by the Free Software Foundation.\n\n"
           "This program is distributed in the hope that it will be useful,\n"
           "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
           "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
           "GNU General Public License for more details.\n\n"
           "You should have received a copy of the GNU Affero General Public\n"
           "License along with this program.  If not, see"
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
    char ipstr[INET6_ADDRSTRLEN] = { 0 };

    /* Print out the configuration. */
    debug(DBG_LOG, "Configured parameters:\n");

    if(cfg->shipgate_ip) {
        inet_ntop(AF_INET, &cfg->shipgate_ip, ipstr, INET6_ADDRSTRLEN);
    }
    else {
        inet_ntop(AF_INET6, &cfg->shipgate_ip6, ipstr, INET6_ADDRSTRLEN);
    }

    debug(DBG_LOG, "Shipgate IP: %s\n", ipstr);
    debug(DBG_LOG, "Shipgate Port: %d\n", (int)cfg->shipgate_port);

    /* Print out the ship's information. */
    debug(DBG_LOG, "Ship Name: %s\n", cfg->name);

    inet_ntop(AF_INET, &cfg->ship_ip, ipstr, INET6_ADDRSTRLEN);
    debug(DBG_LOG, "Ship IPv4 Address: %s\n", ipstr);

    if(cfg->ship_ip6[0]) {
        inet_ntop(AF_INET6, &cfg->ship_ip6, ipstr, INET6_ADDRSTRLEN);
        debug(DBG_LOG, "Ship IPv6 Address: %s\n", ipstr);
    }
    else {
        debug(DBG_LOG, "Ship IPv6 Address: None\n");
    }

    debug(DBG_LOG, "Base Port: %d\n", (int)cfg->base_port);
    debug(DBG_LOG, "Blocks: %d\n", cfg->blocks);
    debug(DBG_LOG, "Lobby Event: %d\n", cfg->lobby_event);
    debug(DBG_LOG, "Game Event: %d\n", cfg->game_event);

    if(cfg->menu_code) {
        debug(DBG_LOG, "Menu: %c%c\n", (char)cfg->menu_code,
              (char)(cfg->menu_code >> 8));
    }
    else {
        debug(DBG_LOG, "Menu: Main\n");
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

    sigemptyset(&sa.sa_mask);

    /* Ignore SIGPIPEs */
    sa.sa_handler = SIG_IGN;

    if(sigaction(SIGPIPE, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    void *tmp;
    sylverant_ship_t *cfg;

    /* Parse the command line... */
    parse_command_line(argc, argv);

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

    /* Set up things for clients to connect. */
    if(client_init()) {
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
    client_shutdown();
    sylverant_free_ship_config(cfg);

    return 0;
}
