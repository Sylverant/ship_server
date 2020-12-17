/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2016, 2020 Lawrence Sebald

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
#include <sylverant/mtwist.h>

#ifdef ENABLE_LUA
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#endif

#include "gm.h"
#include "block.h"
#include "shipgate.h"

#define CLIENTS_H_COUNTS_ONLY
#include "clients.h"
#undef CLIENTS_H_COUNTS_ONLY

#include "quests.h"
#include "bans.h"

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
    uint8_t ship_addr6[16];
    uint16_t ship_port;
    uint16_t clients;
    uint16_t games;
    uint16_t menu_code;
    uint32_t flags;
    int ship_number;
    uint32_t privileges;
} miniship_t;

TAILQ_HEAD(miniship_queue, miniship);

typedef struct limits_entry {
    TAILQ_ENTRY(limits_entry) qentry;

    char *name;
    sylverant_limits_t *limits;
} limits_entry_t;

TAILQ_HEAD(limits_queue, limits_entry);

struct ship {
    sylverant_ship_t *cfg;

    pthread_t thd;
    block_t **blocks;
    struct client_queue *clients;

    int run;
    int dcsock[2];
    int pcsock[2];
    int gcsock[2];
    int ep3sock[2];
    int bbsock[2];
    int xbsock[2];

    time_t shutdown_time;
    int pipes[2];

    uint16_t num_clients;
    uint16_t num_games;
    uint8_t lobby_event;
    uint8_t game_event;

    sylverant_quest_list_t qlist[CLIENT_VERSION_COUNT][CLIENT_LANG_COUNT];
    quest_map_t qmap;

    shipgate_conn_t sg;
    pthread_rwlock_t qlock;
    pthread_rwlock_t llock;

    local_gm_t *gm_list;
    int gm_count;

    pthread_rwlock_t banlock;
    struct gcban_queue guildcard_bans;
    struct ipban_queue ip_bans;

    struct miniship_queue ships;
    int mccount;
    uint16_t *menu_codes;

    struct mt19937_state rng;

    struct limits_queue all_limits;
    sylverant_limits_t *def_limits;

#ifdef ENABLE_LUA
    lua_State *lstate;
#endif

    int script_ref;
};

#ifndef SHIP_DEFINED
#define SHIP_DEFINED
typedef struct ship ship_t;
#endif

/* The one global ship structure */
extern ship_t *ship;

miniship_t *ship_find_ship(ship_t *s, uint32_t sid);

ship_t *ship_server_start(sylverant_ship_t *s);
void ship_check_cfg(sylverant_ship_t *s);

void ship_server_stop(ship_t *s);
void ship_server_shutdown(ship_t *s, time_t when);
int ship_process_pkt(ship_client_t *c, uint8_t *pkt);

void ship_inc_clients(ship_t *s);
void ship_dec_clients(ship_t *s);
void ship_inc_games(ship_t *s);
void ship_dec_games(ship_t *s);

void ship_free_limits(ship_t *s);
void ship_free_limits_ex(struct limits_queue *l);

/* This function assumes that you already hold the read lock! */
sylverant_limits_t *ship_lookup_limits(const char *name);

#ifdef ENABLE_LUA
int ship_register_lua(lua_State *l);
#endif

#endif /* !SHIP_H */
