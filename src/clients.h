/*
    Sylverant Ship Server
    Copyright (C) 2009 Lawrence Sebald

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3 as
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
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

#include <sylverant/encryption.h>

/* Forward declarations. */
struct lobby;

#ifndef LOBBY_DEFINED
#define LOBBY_DEFINED
typedef struct lobby lobby_t;
#endif

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

typedef struct dc_pkt_hdr {
    uint8_t pkt_type;
    uint8_t flags;
    uint16_t pkt_len;
} PACKED dc_pkt_hdr_t;

typedef struct pc_pkt_hdr {
    uint16_t pkt_len;
    uint8_t pkt_type;
    uint8_t flags;
} PACKED pc_pkt_hdr_t;

typedef union pkt_header {
    dc_pkt_hdr_t dc;
    pc_pkt_hdr_t pc;
} pkt_header_t;

#undef PACKED

/* Ship server client structure. */
struct ship_client {
    TAILQ_ENTRY(ship_client) qentry;

    int type;
    int version;
    int sock;
    int disconnected;

    int hdr_size;
    int hdr_read;
    in_addr_t addr;
    uint32_t guildcard;

    ship_t *cur_ship;
    block_t *cur_block;
    lobby_t *cur_lobby;
    player_t *pl;

    int client_id;
    uint32_t arrow;
    int is_gm;
    time_t last_message;

    time_t last_sent;
    time_t join_time;

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

#endif /* !CLIENTS_H */
