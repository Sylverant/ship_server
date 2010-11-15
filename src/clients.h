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

#ifndef CLIENTS_H
#define CLIENTS_H

#include <time.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/queue.h>
#include <netinet/in.h>

#include "ship.h"
#include "block.h"
#include "player.h"

/* Pull in the packet header types. */
#define PACKETS_H_HEADERS_ONLY
#include "packets.h"
#undef PACKETS_H_HEADERS_ONLY

#include <sylverant/encryption.h>

/* Forward declarations. */
struct lobby;

#ifndef LOBBY_DEFINED
#define LOBBY_DEFINED
typedef struct lobby lobby_t;
#endif

/* Ship server client structure. */
struct ship_client {
    TAILQ_ENTRY(ship_client) qentry;

    int type;
    int version;
    int sock;
    int disconnected;

    int hdr_size;
    in_addr_t addr;
    uint32_t guildcard;
    uint32_t flags;

    ship_t *cur_ship;
    block_t *cur_block;
    lobby_t *cur_lobby;
    player_t *pl;

    int client_id;
    uint32_t arrow;
    uint32_t privilege;
    time_t last_message;

    time_t last_sent;
    time_t join_time;
    int language_code;

    uint32_t next_item[4];

    pthread_mutex_t mutex;

    CRYPT_SETUP ckey;
    CRYPT_SETUP skey;

    unsigned char *recvbuf;
    int recvbuf_cur;
    int recvbuf_size;
    pkt_header_t pkt;

    unsigned char *sendbuf;
    int sendbuf_cur;
    int sendbuf_size;
    int sendbuf_start;

    char *infoboard;                    /* Points into the player struct. */
    uint8_t *c_rank;                    /* Points into the player struct. */
    lobby_t *create_lobby;
    uint32_t *blacklist;                /* Points into the player struct. */

    char *autoreply;
    FILE *logfile;
    time_t sent_motd;
};

#define CLIENT_PRIV_LOCAL_GM    0x00000001
#define CLIENT_PRIV_GLOBAL_GM   0x00000002
#define CLIENT_PRIV_LOCAL_ROOT  0x00000004
#define CLIENT_PRIV_GLOBAL_ROOT 0x00000008

/* String versions of the character classes. */
static const char *classes[12] __attribute__((unused)) = {
    "HUmar", "HUnewearl", "HUcast",
    "RAmar", "RAcast", "RAcaseal",
    "FOmarl", "FOnewm", "FOnewearl",
    "HUcaseal", "FOmar", "RAmarl"
};

#ifndef SHIP_CLIENT_DEFINED
#define SHIP_CLIENT_DEFINED
typedef struct ship_client ship_client_t;
#endif

TAILQ_HEAD(client_queue, ship_client);

/* The key used for the thread-specific send buffer. */
extern pthread_key_t sendbuf_key;

/* Possible values for the type field of ship_client_t */
#define CLIENT_TYPE_SHIP        0
#define CLIENT_TYPE_BLOCK       1

/* Possible values for the version field of ship_client_t */
#define CLIENT_VERSION_DCV1     0
#define CLIENT_VERSION_DCV2     1
#define CLIENT_VERSION_PC       2
#define CLIENT_VERSION_GC       3

/* Language codes. */
#define CLIENT_LANG_JAPANESE        0
#define CLIENT_LANG_ENGLISH         1
#define CLIENT_LANG_GERMAN          2
#define CLIENT_LANG_FRENCH          3
#define CLIENT_LANG_SPANISH         4
#define CLIENT_LANG_CHINESE_SIMP    5
#define CLIENT_LANG_CHINESE_TRAD    6
#define CLIENT_LANG_KOREAN          7

#define CLIENT_LANG_COUNT           8

#define CLIENT_FLAG_HDR_READ        0x00000001
#define CLIENT_FLAG_GOT_05          0x00000002
#define CLIENT_FLAG_INVULNERABLE    0x00000004
#define CLIENT_FLAG_INFINITE_TP     0x00000008

/* The list of language codes for the quest directories. */
static const char language_codes[][3] __attribute__((unused)) = {
    "jp", "en", "de", "fr", "sp", "cs", "ct", "kr"
};

/* Initialize the clients system, allocating any thread specific keys */
int client_init();

/* Clean up the clients system. */
void client_shutdown();

/* Create a new connection, storing it in the list of clients. */
ship_client_t *client_create_connection(int sock, int version, int type,
                                        struct client_queue *clients,
                                        ship_t *ship, block_t *block,
                                        in_addr_t addr);

/* Destroy a connection, closing the socket and removing it from the list. */
void client_destroy_connection(ship_client_t *c, struct client_queue *clients);

/* Read data from a client that is connected to any port. */
int client_process_pkt(ship_client_t *c);

/* Retrieve the thread-specific recvbuf for the current thread. */
uint8_t *get_recvbuf();

/* Set up a simple mail autoreply. */
int client_set_autoreply(ship_client_t *c, dc_pkt_hdr_t *pkt);

/* Clear the simple mail autoreply from a client (if set). */
int client_clear_autoreply(ship_client_t *c);

/* Check if a client has blacklisted someone. */
int client_has_blacklisted(ship_client_t *c, uint32_t gc);

/* Send a message to a client telling them that a friend has logged on/off */
void client_send_friendmsg(ship_client_t *c, int on, const char *fname,
                           const char *ship, uint32_t block);

#endif /* !CLIENTS_H */
