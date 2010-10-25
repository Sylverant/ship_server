/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010 Lawrence Sebald

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
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <ifaddrs.h>

#include <sylverant/config.h>
#include <sylverant/debug.h>

#include "ship.h"
#include "clients.h"
#include "shipgate.h"
#include "utils.h"

/* Configuration data for the server. */
sylverant_shipcfg_t *cfg;

in_addr_t local_addr;
in_addr_t netmask;

/* The actual ship structures. */
ship_t **ships;
char *config_file = NULL;

/* Print information about this program to stdout. */
static void print_program_info() {
    printf("Sylverant Ship Server version %s\n", VERSION);
    printf("Copyright (C) 2009, 2010 Lawrence Sebald\n\n");
    printf("This program is free software: you can redistribute it and/or\n"
           "modify it under the terms of the GNU General Public License\n"
           "version 3 as published by the Free Software Foundation.\n\n"
           "This program is distributed in the hope that it will be useful,\n"
           "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
           "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
           "GNU General Public License for more details.\n\n"
           "You should have received a copy of the GNU General Public License\n"
           "along with this program.  If not, see "
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
        else if(!strcmp(argv[i], "--help")) {
            print_help(argv[0]);
            exit(0);
        }
        else {
            printf("Illegal command line argument: %s\n", argv[i]);
            print_help(argv[0]);
            exit(1);
        }
    }
}

/* Load the configuration file and print out parameters with DBG_LOG. */
static void load_config() {
    struct in_addr tmp;
    int i;

    debug(DBG_LOG, "Loading Sylverant Ship configuration file... ");

    if(sylverant_read_ship_config(config_file, &cfg)) {
        debug(DBG_ERROR, "Cannot load Sylverant Ship configuration file!\n");
        exit(1);
    }

    /* Allocate space for the ships. */
    ships = (ship_t **)malloc(sizeof(ship_t *) * cfg->ship_count);

    if(!ships) {
        debug(DBG_ERROR, "Cannot allocate memory!\n");
        exit(1);
    }

    debug(DBG_LOG, "Ok\n");

    /* Print out the configuration. */
    debug(DBG_LOG, "Configured parameters:\n");

    tmp.s_addr = cfg->shipgate_ip;
    debug(DBG_LOG, "Shipgate IP: %s\n", inet_ntoa(tmp));
    debug(DBG_LOG, "Shipgate Port: %d\n", (int)cfg->shipgate_port);
    debug(DBG_LOG, "Number of Ships: %d\n", cfg->ship_count);

    /* Print out each ship's information. */
    for(i = 0; i < cfg->ship_count; ++i) {
        debug(DBG_LOG, "Ship Name: %s\n", cfg->ships[i].name);

        tmp.s_addr = cfg->ships[i].ship_ip;
        debug(DBG_LOG, "Ship IP: %s\n", inet_ntoa(tmp));
        debug(DBG_LOG, "Base Port: %d\n", (int)cfg->ships[i].base_port);
        debug(DBG_LOG, "Blocks: %d\n", cfg->ships[i].blocks);
        debug(DBG_LOG, "Event: %d\n", cfg->ships[i].event);
        if(cfg->ships[i].menu_code) {
            debug(DBG_LOG, "Menu: %c%c\n", (char)cfg->ships[i].menu_code,
                  (char)(cfg->ships[i].menu_code >> 8));
        }
        else {
            debug(DBG_LOG, "Menu: Main\n");
        }
    }
}

/* Fetch the local address and netmask of the host. */
static int get_ip_info() {
    int rv;
    struct addrinfo hints, *servinfo;
    struct sockaddr_in *addr;
    char hostname[256];
    struct ifaddrs *ifaddr, *ifa;

    /* Get the host name for passing to getaddrinfo */
    gethostname(hostname, 255);

    /* Clear the hints out, we'll fill in what we want below. */
    memset(&hints, 0, sizeof(struct addrinfo));

    /* We want a IPv4 address... */
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    /* Query the OS for what we want. */
    rv = getaddrinfo(hostname, NULL, &hints, &servinfo);

    if(rv) {
        debug(DBG_ERROR, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    /* For now, assume we want the first one. */
    local_addr = ((struct sockaddr_in *)servinfo->ai_addr)->sin_addr.s_addr;

    freeaddrinfo(servinfo);

    /* We've got the IP address, now attempt to get the netmask associated with
       that IP. */
    if(getifaddrs(&ifaddr)) {
        perror("getifaddrs");
        return -2;
    }

    /* Look through the list for the interface we want. */
    for(ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if(ifa->ifa_addr->sa_family == AF_INET) {
            addr = (struct sockaddr_in *)ifa->ifa_addr;

            if(addr->sin_addr.s_addr == local_addr) {
                addr = (struct sockaddr_in *)ifa->ifa_netmask;
                netmask = addr->sin_addr.s_addr;
                break;
            }
        }
    }

    /* Clean up the data allocated by getifaddrs. */
    freeifaddrs(ifaddr);

    return 0;
}

int main(int argc, char *argv[]) {
    int i;

    /* Parse the command line and read our configuration. */
    parse_command_line(argc, argv);
    load_config();

    if(get_ip_info()) {
        debug(DBG_ERROR, "Could not fetch host information\n");
        exit(1);
    }

    chdir(sylverant_directory);

    /* Set up things for clients to connect. */
    if(client_init()) {
        exit(1);
    }

    /* Init mini18n if we have it */
    init_i18n();

    /* Start up the servers for the ships we've configured. */
    for(i = 0; i < cfg->ship_count; ++i) {
        ships[i] = ship_server_start(&cfg->ships[i]);
    }

    if(ships[0]) {
        /* Run the ship server. */
        /* XXXX: NO! This is not the right way to do this! */
        pthread_join(ships[0]->thd, NULL);
    }

    /* Wait for the ships to exit. */
    for(i = 0; i < cfg->ship_count; ++i) {
        if(ships[i]) {
            ship_server_stop(ships[i]);
        }
    }

    /* Clean up... */
    cleanup_i18n();
    client_shutdown();
    free(ships);
    free(cfg);

    return 0;
}
