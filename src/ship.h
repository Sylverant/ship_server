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

#ifndef SHIP_H
#define SHIP_H

#include <pthread.h>
#include <inttypes.h>
#include <sys/queue.h>

#include <sylverant/config.h>
#include <sylverant/quest.h>
#include <sylverant/items.h>

#include "gm.h"
#include "block.h"
#include "shipgate.h"

#define CLIENTS_H_COUNTS_ONLY
#include "clients.h"
#undef CLIENTS_H_COUNTS_ONLY

#include "quests.h"

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
    TAILQ_ENTRY(miniship) qentry;

    char name[12];
    uint32_t ship_id;
    uint32_t ship_addr;
    uint32_t int_addr;
    uint16_t ship_port;
    uint16_t clients;
    uint16_t games;
    uint16_t menu_code;
    uint32_t flags;
} miniship_t;

TAILQ_HEAD(miniship_queue, miniship);

struct ship {
    sylverant_ship_t *cfg;

    pthread_t thd;
    block_t **blocks;
    struct client_queue *clients;

    int run;
    int dcsock;
    int pcsock;
    int gcsock;
    int ep3sock;

    time_t shutdown_time;
    int pipes[2];

    uint16_t num_clients;
    uint16_t num_games;

    sylverant_quest_list_t quests;
    sylverant_quest_list_t qlist[CLIENT_VERSION_COUNT][CLIENT_LANG_COUNT];
    quest_map_t qmap;

    shipgate_conn_t sg;
    pthread_rwlock_t qlock;
    sylverant_limits_t *limits;

    local_gm_t *gm_list;
    int gm_count;

    struct miniship_queue ships;
    int mccount;
    uint16_t *menu_codes;

    char *motd;
};

#ifndef SHIP_DEFINED
#define SHIP_DEFINED
typedef struct ship ship_t;
#endif

ship_t *ship_server_start(sylverant_ship_t *s);
void ship_server_stop(ship_t *s);
void ship_server_shutdown(ship_t *s, time_t when);
int ship_process_pkt(ship_client_t *c, uint8_t *pkt);

void ship_inc_clients(ship_t *s);
void ship_dec_clients(ship_t *s);
void ship_inc_games(ship_t *s);
void ship_dec_games(ship_t *s);

#endif /* !SHIP_H */
