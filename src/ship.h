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

#ifndef SHIP_H
#define SHIP_H

#include <pthread.h>
#include <inttypes.h>

#include <sylverant/config.h>
#include <sylverant/quest.h>

#include "gm.h"
#include "block.h"
#include "shipgate.h"

/* Forward declarations. */
struct client_queue;
struct ship_client;
struct block;

#ifndef SHIP_CLIENT_DEFINED
#define SHIP_CLIENT_DEFINED
typedef struct ship_client ship_client_t;
#endif

#ifndef BLOCK_DEFINED
#define BLOCK_DEFINED
typedef struct block block_t;
#endif

typedef struct miniship {
    char name[12];
    uint32_t ship_id;
    uint32_t ship_addr;
    uint32_t int_addr;
    uint16_t ship_port;
} miniship_t;

struct ship {
    sylverant_ship_t *cfg;

    pthread_t thd;
    block_t **blocks;
    struct client_queue *clients;

    int run;
    int dcsock;
    int pcsock;
    uint16_t num_clients;

    sylverant_quest_list_t quests;
    shipgate_conn_t sg;
    pthread_mutex_t qmutex;

    local_gm_t *gm_list;
    int gm_count;

    int ship_count;
    miniship_t *ships;
};

#ifndef SHIP_DEFINED
#define SHIP_DEFINED
typedef struct ship ship_t;
#endif

ship_t *ship_server_start(sylverant_ship_t *s);
void ship_server_stop(ship_t *s);
int ship_process_pkt(ship_client_t *c, uint8_t *pkt);

void ship_inc_clients(ship_t *s);
void ship_dec_clients(ship_t *s);

#endif /* !SHIP_H */
