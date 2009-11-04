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
    pthread_mutex_t mutex;
    struct client_queue *clients;

    int b;
    int run;
    int dcsock;
    int pcsock;

    uint16_t dc_port;
    uint16_t pc_port;

    struct lobby_queue lobbies;
};

#ifndef BLOCK_DEFINED
#define BLOCK_DEFINED
typedef struct block block_t;
#endif

block_t *block_server_start(ship_t *s, int b, uint16_t port);
void block_server_stop(block_t *b);
int block_process_pkt(ship_client_t *c, uint8_t *pkt);

lobby_t *block_get_lobby(block_t *b, uint32_t lobby_id);
int block_info_reply(ship_client_t *c, int block);

#endif /* !SHIP_H */
