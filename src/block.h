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

#ifndef BLOCK_H
#define BLOCK_H

#include <pthread.h>
#include <inttypes.h>
#include <sylverant/config.h>

#include "lobby.h"

/* Forward declarations. */
struct ship;
struct client_queue;
struct ship_client;

#ifndef SHIP_CLIENT_DEFINED
#define SHIP_CLIENT_DEFINED
typedef struct ship_client ship_client_t;
#endif

#ifndef SHIP_DEFINED
#define SHIP_DEFINED
typedef struct ship ship_t;
#endif

struct block {
    ship_t *ship;

    pthread_t thd;

    /* Reader-writer lock for the client tailqueue */
    pthread_rwlock_t lock;
    struct client_queue *clients;
    int num_clients;

    int b;
    int run;
    int dcsock[2];
    int pcsock[2];
    int gcsock[2];
    int ep3sock[2];
    int bbsock[2];

    int pipes[2];

    uint16_t dc_port;
    uint16_t pc_port;
    uint16_t gc_port;
    uint16_t ep3_port;
    uint16_t bb_port;

    /* Reader-writer lock for the lobby tailqueue */
    pthread_rwlock_t lobby_lock;
    struct lobby_queue lobbies;
    int num_games;
};

#ifndef BLOCK_DEFINED
#define BLOCK_DEFINED
typedef struct block block_t;
#endif

block_t *block_server_start(ship_t *s, int b, uint16_t port);
void block_server_stop(block_t *b);
int block_process_pkt(ship_client_t *c, uint8_t *pkt);

lobby_t *block_get_lobby(block_t *b, uint32_t lobby_id);
int block_info_reply(ship_client_t *c, uint32_t block);

int send_motd(ship_client_t *c);

#endif /* !SHIP_H */
