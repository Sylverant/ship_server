/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018,
                  2019, 2020 Lawrence Sebald

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
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <sylverant/mtwist.h>
#include <sylverant/debug.h>
#include <sylverant/checksum.h>
#include <sylverant/memory.h>

#include "lobby.h"
#include "utils.h"
#include "block.h"
#include "clients.h"
#include "ship_packets.h"
#include "subcmd.h"
#include "ship.h"
#include "shipgate.h"
#include "items.h"
#include "ptdata.h"
#include "pmtdata.h"
#include "rtdata.h"
#include "scripts.h"

#ifdef ENABLE_LUA
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#endif

static pthread_key_t id_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

#ifdef DEBUG
static FILE *logfp = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static int td(ship_client_t *c, lobby_t *l, void *req);

lobby_t *lobby_create_default(block_t *block, uint32_t lobby_id, uint8_t ev) {
    lobby_t *l = (lobby_t *)malloc(sizeof(lobby_t));

    /* If we don't have a lobby, bail. */
    if(!l) {
        debug(DBG_WARN, "Couldn't allocate space for default lobby!\n");
        debug(DBG_WARN, "%s", strerror(errno));
        return NULL;
    }

    /* Clear it, and set up the specified parameters. */
    memset(l, 0, sizeof(lobby_t));

    l->lobby_id = lobby_id;
    l->type = LOBBY_TYPE_DEFAULT;
    l->max_clients = LOBBY_MAX_CLIENTS;
    l->block = block;
    l->min_level = 0;
    l->max_level = 9001;                /* Its OVER 9000! */
    l->event = ev;

    /* Fill in the name of the lobby. */
    if(lobby_id <= 15) {
        sprintf(l->name, "BLOCK%02d-%02d", block->b, lobby_id);
    }
    else {
        l->flags |= LOBBY_FLAG_EP3;
        sprintf(l->name, "BLOCK%02d-C%d", block->b, lobby_id - 15);
    }

    /* Initialize the (unused) packet queue */
    STAILQ_INIT(&l->pkt_queue);

#ifdef ENABLE_LUA
    /* Initialize the script table */
    lua_newtable(block->ship->lstate);
    l->script_ref = luaL_ref(block->ship->lstate, LUA_REGISTRYINDEX);
#endif

    /* Initialize the lobby mutex. */
    pthread_mutex_init(&l->mutex, NULL);

    return l;
}

static void lobby_setup_drops(ship_client_t *c, lobby_t *l, uint32_t rs) {
#ifdef DEBUG
    /* See if we have server drop debugging turned on... */
    if(c->flags & CLIENT_FLAG_DBG_SDROPS) {
        l->flags |= LOBBY_FLAG_SERVER_DROPS | LOBBY_FLAG_DBG_SDROPS;
        c->flags &= ~CLIENT_FLAG_DBG_SDROPS;

        switch(c->sdrops_ver) {
            case CLIENT_VERSION_DCV2:
                l->dropfunc = pt_generate_v2_drop;
                break;

            case CLIENT_VERSION_GC:
                l->dropfunc = pt_generate_gc_drop;
                break;
        }

        l->sdrops_ep = c->sdrops_ep;
        l->sdrops_diff = c->sdrops_diff;
        l->sdrops_section = c->sdrops_section;
        return;
    }
#endif

    if(l->version == CLIENT_VERSION_BB) {
        l->dropfunc = pt_generate_bb_drop;
        l->flags |= LOBBY_FLAG_SERVER_DROPS;
        return;
    }

    if(rs == 0x9C350DD4) {
        l->dropfunc = td;
        l->flags |= LOBBY_FLAG_SERVER_DROPS;
        return;
    }

    /* See if the client enabled server-side drops. */
    if(c->flags & CLIENT_FLAG_SERVER_DROPS) {
        /* Sanity check... */
        switch(c->version) {
            case CLIENT_VERSION_DCV1:
            case CLIENT_VERSION_DCV2:
            case CLIENT_VERSION_PC:
                if(pt_v2_enabled() && map_have_v2_maps() && pmt_v2_enabled() &&
                   rt_v2_enabled() && !l->battle && !l->challenge) {
                    l->dropfunc = pt_generate_v2_drop;
                    l->flags |= LOBBY_FLAG_SERVER_DROPS;
                }
                return;

            case CLIENT_VERSION_GC:
                if(pt_gc_enabled() && map_have_gc_maps() && pmt_gc_enabled() &&
                   rt_gc_enabled() && !l->battle && !l->challenge) {
                    l->dropfunc = pt_generate_gc_drop;
                    l->flags |= LOBBY_FLAG_SERVER_DROPS;
                }
                return;
        }
    }
}

/* This list of numbers was borrowed from newserv. Hopefully Fuzziqer won't
   mind too much. */
static const uint32_t maps[3][0x20] = {
    {1,1,1,5,1,5,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,1,1,1,1,1,1,1,1,1,1},
    {1,1,2,1,2,1,2,1,2,1,1,3,1,3,1,3,2,2,1,3,2,2,2,2,1,1,1,1,1,1,1,1},
    {1,1,1,3,1,3,1,3,1,3,1,3,3,1,1,3,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

static const uint32_t sp_maps[3][0x20] = {
    {1,1,1,3,1,3,3,1,3,1,3,1,3,2,3,2,3,2,3,2,3,2,1,1,1,1,1,1,1,1,1,1},
    {1,1,2,1,2,1,2,1,2,1,1,3,1,3,1,3,2,2,1,3,2,1,2,1,1,1,1,1,1,1,1,1},
    {1,1,1,3,1,3,1,3,1,3,1,3,3,1,1,3,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

static const uint32_t dcnte_maps[0x20] =
    {1,1,1,2,1,2,2,2,2,2,2,2,1,2,1,2,2,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1};

static void create_key(void) {
    pthread_key_create(&id_key, NULL);
}

void lobby_print_info(lobby_t *l, FILE *fp) {
    int i;

    fdebug(fp, DBG_LOG, "         Name: %s\n", l->name);
    fdebug(fp, DBG_LOG, "         Flags: %08" PRIx32 "\n", l->flags);
    fdebug(fp, DBG_LOG, "         Version: %d (v2: %d)\n", l->version,
           (int)l->v2);
    fdebug(fp, DBG_LOG, "         Battle/Challenge: %d/%d\n",
           (int)l->battle, (int)l->challenge);
    fdebug(fp, DBG_LOG, "         Difficulty: %d\n", (int)l->difficulty);
    fdebug(fp, DBG_LOG, "         Enemies Array: %p\n", l->map_enemies);
    fdebug(fp, DBG_LOG, "         Object Array: %p\n", l->map_objs);

    if(l->qid)
        fdebug(fp, DBG_LOG, "         Quest ID: %" PRIu32 "\n", l->qid);

#ifdef ENABLE_LUA
    fdebug(fp, DBG_LOG, "         Lua table: %d\n", l->script_ref);
#endif /* ENABLE_LUA */

    fdebug(fp, DBG_LOG, "         Maps in use:\n");
    for(i = 0; i < 0x10; ++i) {
        fdebug(fp, DBG_LOG, "             %d: %d %d\n", i, (int)l->maps[i << 1],
               (int)l->maps[(i << 1) + 1]);
    }
}

lobby_t *lobby_create_game(block_t *block, char *name, char *passwd,
                           uint8_t difficulty, uint8_t battle, uint8_t chal,
                           uint8_t v2, int version, uint8_t section,
                           uint8_t event, uint8_t episode, ship_client_t *c,
                           uint8_t single_player) {
    lobby_t *l = (lobby_t *)malloc(sizeof(lobby_t));
    uint32_t *pid, id;
    int i;

    /* If we don't have a lobby, bail. */
    if(!l) {
        debug(DBG_WARN, "Couldn't allocate space for team!\n");
        debug(DBG_WARN, "%s", strerror(errno));
        return NULL;
    }

    /* Grab the ID. */
    pthread_once(&key_once, create_key);
    if(!(pid = (uint32_t *)pthread_getspecific(id_key))) {
        if(!(pid = (uint32_t *)malloc(sizeof(uint32_t)))) {
            debug(DBG_WARN, "Couldn't allocate key: %s\n", strerror(errno));
            free(l);
            return NULL;
        }

        *pid = 0x20;
        pthread_setspecific(id_key, pid);
    }

    id = *pid;

    /* Make sure our ID is unused... */
    while(block_get_lobby(block, id)) {
        ++id;

        /* If the next id has gotten huge somehow, reset it back to the starting
           value. */
        if(id > 0xFFFF) {
            id = 0x20;
        }
    }

    if(id != 0xFFFF)
        *pid = id + 1;
    else
        *pid = 0x20;

    /* Clear it. */
    memset(l, 0, sizeof(lobby_t));

    /* Set up the specified parameters. */
    l->lobby_id = id;
    l->type = LOBBY_TYPE_GAME;

    if(!single_player)
        l->max_clients = 4;
    else
        l->max_clients = 1;

    l->block = block;

    if(version == CLIENT_VERSION_BB)
        l->item_id = 0x00810000;
    else
        l->item_id = 0xF0000000;

    l->leader_id = 1;
    l->difficulty = difficulty;
    l->battle = battle;
    l->challenge = chal;
    l->v2 = v2;
    l->episode = episode;
    l->version = (version == CLIENT_VERSION_DCV2 && !v2) ?
        CLIENT_VERSION_DCV1 : version;
    l->section = section;
    l->event = event;
    l->min_level = game_required_level[difficulty];
    l->max_level = 200;
    l->max_chal = 0xFF;
    l->create_time = time(NULL);
    l->flags = LOBBY_FLAG_ONLY_ONE;

    if(single_player)
        l->flags |= LOBBY_FLAG_SINGLEPLAYER;

    if(c->flags & CLIENT_FLAG_IS_NTE)
        l->flags |= LOBBY_FLAG_NTE;

    if(c->flags & CLIENT_FLAG_LEGIT && c->limits) {
        /* Steal the client's reference to the legit list. */
        l->flags |= LOBBY_FLAG_LEGIT_MODE;
        l->limits_list = c->limits;
        c->flags &= ~CLIENT_FLAG_LEGIT;
        c->limits = NULL;
    }

    /* Copy the game name and password. */
    strncpy(l->name, name, 64);
    strncpy(l->passwd, passwd, 64);
    l->name[64] = 0;
    l->passwd[64] = 0;

    /* Initialize the packet queue */
    STAILQ_INIT(&l->pkt_queue);
    TAILQ_INIT(&l->item_queue);
    STAILQ_INIT(&l->burst_queue);

    /* Initialize the lobby mutex. */
    pthread_mutex_init(&l->mutex, NULL);

    /* We need episode to be either 1 or 2 for the below map selection code to
       work. On PSODC and PSOPC, it'll be 0 at this point, so make it 1 (as it
       would be expected to be). */
    if(version < CLIENT_VERSION_GC)
        episode = 1;

    /* Generate the random maps we'll be using for this game, assuming the
       client hasn't set a maps string. */
    if(!c->next_maps) {
        if(c->version == CLIENT_VERSION_DCV1 &&
           (c->flags & CLIENT_FLAG_IS_NTE)) {
            for(i = 0; i < 0x20; ++i) {
                if(dcnte_maps[i] != 1) {
                    l->maps[i] = mt19937_genrand_int32(&block->rng) %
                        dcnte_maps[i];
                }
            }
        }
        else if(!single_player) {
            for(i = 0; i < 0x20; ++i) {
                if(maps[episode - 1][i] != 1) {
                    l->maps[i] = mt19937_genrand_int32(&block->rng) %
                        maps[episode - 1][i];
                }
            }
        }
        else {
            for(i = 0; i < 0x20; ++i) {
                if(sp_maps[episode - 1][i] != 1) {
                    l->maps[i] = mt19937_genrand_int32(&block->rng) %
                        sp_maps[episode - 1][i];
                }
            }
        }
    }
    else {
        if(c->version == CLIENT_VERSION_DCV1 &&
           (c->flags & CLIENT_FLAG_IS_NTE)) {
            for(i = 0; i < 0x20; ++i) {
                if(c->next_maps[i] < dcnte_maps[i]) {
                    l->maps[i] = c->next_maps[i];
                }
                else {
                    l->maps[i] = mt19937_genrand_int32(&block->rng) %
                        dcnte_maps[i];
                }
            }
        }
        else if(!single_player) {
            for(i = 0; i < 0x20; ++i) {
                if(c->next_maps[i] < maps[episode - 1][i]) {
                    l->maps[i] = c->next_maps[i];
                }
                else {
                    l->maps[i] = mt19937_genrand_int32(&block->rng) %
                        maps[episode - 1][i];
                }
            }
        }
        else {
            for(i = 0; i < 0x20; ++i) {
                if(c->next_maps[i] < sp_maps[episode - 1][i]) {
                    l->maps[i] = c->next_maps[i];
                }
                else {
                    l->maps[i] = mt19937_genrand_int32(&block->rng) %
                        sp_maps[episode - 1][i];
                }
            }
        }

        free(c->next_maps);
        c->next_maps = NULL;
    }

    /* If its a Blue Burst lobby, set up the enemy data. */
    if(version == CLIENT_VERSION_BB && bb_load_game_enemies(l)) {
        debug(DBG_WARN, "Error setting up blue burst enemies!\n");

        if(l->limits_list)
            release(l->limits_list);

        pthread_mutex_destroy(&l->mutex);
        free(l);
        return NULL;
    }
    else if(version <= CLIENT_VERSION_PC && map_have_v2_maps() && !battle &&
            !chal && v2_load_game_enemies(l)) {
        debug(DBG_WARN, "Error setting up v1/v2 enemy data!\n");

        if(l->limits_list)
            release(l->limits_list);

        pthread_mutex_destroy(&l->mutex);
        free(l);
        return NULL;
    }
    else if(version == CLIENT_VERSION_GC && map_have_gc_maps() && !battle &&
            !chal && gc_load_game_enemies(l)) {
        debug(DBG_WARN, "Error setting up GC enemy data!\n");

        if(l->limits_list)
            release(l->limits_list);

        pthread_mutex_destroy(&l->mutex);
        free(l);
        return NULL;
    }

    /* Add it to the list of lobbies, and increment the game count. */
    if(version != CLIENT_VERSION_PC || battle || chal || difficulty == 3 ||
       (c->flags & CLIENT_FLAG_IS_NTE)) {
        pthread_rwlock_wrlock(&block->lobby_lock);
        TAILQ_INSERT_TAIL(&block->lobbies, l, qentry);
        ++block->num_games;
        pthread_rwlock_unlock(&block->lobby_lock);

        ship_inc_games(block->ship);
    }

    l->rand_seed = mt19937_genrand_int32(&block->rng);

    if(!chal && !battle)
        lobby_setup_drops(c, l, sylverant_crc32((uint8_t *)l->name, 16));

#ifdef ENABLE_LUA
    /* Initialize the script table */
    lua_newtable(block->ship->lstate);
    l->script_ref = luaL_ref(block->ship->lstate, LUA_REGISTRYINDEX);

    if(!(l->script_ids = (int *)malloc(sizeof(int) * ScriptActionCount)))
        debug(DBG_WARN, "Couldn't allocate team script list!\n");
    else
        memset(l->script_ids, 0, sizeof(int) * ScriptActionCount);
#endif

    /* Run the team creation script, if one exists. */
    script_execute(ScriptActionTeamCreate, c, SCRIPT_ARG_PTR, c, SCRIPT_ARG_PTR,
                   l, SCRIPT_ARG_END);

#ifdef DEBUG
    pthread_mutex_lock(&log_mutex);

    /* Open the log file if we haven't done so yet. */
    if(!logfp) {
        char fn[strlen(ship->cfg->name) + 32];

        sprintf(fn, "logs/%s_team.log", ship->cfg->name);
        logfp = fopen(fn, "a");

        if(!logfp) {
            /* Uhh... That's a problem... */
            debug(DBG_ERROR, "Cannot open team log!\n");
            perror("fopen");
            goto out;
        }

        fdebug(logfp, DBG_LOG, "***************************************\n");
        fdebug(logfp, DBG_LOG, "Starting log for new session...\n");
        fdebug(logfp, DBG_LOG, "***************************************\n");
    }

    fdebug(logfp, DBG_LOG, "BLOCK%02d: Created team with id %" PRIu32
           " at %p\n", block->b, id, l);
    lobby_print_info(l, logfp);
out:
    pthread_mutex_unlock(&log_mutex);
#endif

    return l;
}

lobby_t *lobby_create_ep3_game(block_t *block, char *name, char *passwd,
                               uint8_t view_battle, uint8_t section,
                               ship_client_t *c) {
    lobby_t *l = (lobby_t *)malloc(sizeof(lobby_t));
    uint32_t id = 0x20;

    /* If we don't have a lobby, bail. */
    if(!l) {
        debug(DBG_WARN, "Couldn't allocate space for Episode 3 game lobby!\n");
        debug(DBG_WARN, "%s", strerror(errno));
        return NULL;
    }

    /* Clear it. */
    memset(l, 0, sizeof(lobby_t));

    /* Select an unused ID. */
    do {
        ++id;
    } while(block_get_lobby(block, id));

    /* Set up the specified parameters. */
    l->lobby_id = id;
    l->type = LOBBY_TYPE_EP3_GAME;
    l->max_clients = 4;
    l->block = block;

    l->leader_id = 0;
    l->battle = view_battle;
    l->episode = 3;
    l->version = CLIENT_VERSION_EP3;
    l->section = section;
    l->min_level = 1;
    l->max_level = 200;
    l->rand_seed = mt19937_genrand_int32(&block->rng);
    l->create_time = time(NULL);
    l->flags |= LOBBY_FLAG_EP3;

    /* Copy the game name and password. */
    strncpy(l->name, name, 32);
    strncpy(l->passwd, passwd, 16);
    l->name[33] = 0;
    l->passwd[16] = 0;

    /* Initialize the packet queue */
    STAILQ_INIT(&l->pkt_queue);
    STAILQ_INIT(&l->burst_queue);

    /* Initialize the lobby mutex. */
    pthread_mutex_init(&l->mutex, NULL);

    /* Add it to the list of lobbies, and increment the game count. */
    pthread_rwlock_wrlock(&block->lobby_lock);
    TAILQ_INSERT_TAIL(&block->lobbies, l, qentry);
    ++block->num_games;
    pthread_rwlock_unlock(&block->lobby_lock);
    ship_inc_games(block->ship);

#ifdef ENABLE_LUA
    /* Initialize the script table */
    lua_newtable(block->ship->lstate);
    l->script_ref = luaL_ref(block->ship->lstate, LUA_REGISTRYINDEX);

    if(!(l->script_ids = (int *)malloc(sizeof(int) * ScriptActionCount)))
        debug(DBG_WARN, "Couldn't allocate team script list!\n");
    else
        memset(l->script_ids, 0, sizeof(int) * ScriptActionCount);
#endif

    /* Run the team creation script, if one exists. */
    script_execute(ScriptActionTeamCreate, c, SCRIPT_ARG_PTR, c, SCRIPT_ARG_PTR,
                   l, SCRIPT_ARG_END);

    return l;
}

static void lobby_empty_pkt_queue(lobby_t *l) {
    lobby_pkt_t *i;

    while((i = STAILQ_FIRST(&l->pkt_queue))) {
        STAILQ_REMOVE_HEAD(&l->pkt_queue, qentry);
        free(i->pkt);
        free(i);
    }

    while((i = STAILQ_FIRST(&l->burst_queue))) {
        STAILQ_REMOVE_HEAD(&l->burst_queue, qentry);
        free(i->pkt);
        free(i);
    }
}

static void lobby_destroy_locked(lobby_t *l, int remove) {
    pthread_mutex_t m = l->mutex;
    lobby_item_t *i, *tmp;
    int j;

#ifdef DEBUG
    pthread_mutex_lock(&log_mutex);

    if(logfp) {
        fdebug(logfp, DBG_LOG, "BLOCK%02d: Destroying team with id %" PRIu32
               " at %p\n", l->block->b, l->lobby_id, l);
        lobby_print_info(l, logfp);
    }

    pthread_mutex_unlock(&log_mutex);
#endif

    /* If this is a team that's being logged, clean that up. */
    if(l->logfp)
        team_log_stop(l);

    /* Run the team deletion script, if one exists. */
    script_execute(ScriptActionTeamDestroy, NULL, SCRIPT_ARG_PTR, l,
                   SCRIPT_ARG_END);

#ifdef ENABLE_LUA
    /* Clean up any scripts. */
    for(j = 0; j < ScriptActionCount; ++j) {
        if(l->script_ids[j])
            luaL_unref(l->block->ship->lstate, LUA_REGISTRYINDEX,
                       l->script_ids[j]);
    }

    free(l->script_ids);

    /* Remove the table from the registry */
    luaL_unref(l->block->ship->lstate, LUA_REGISTRYINDEX, l->script_ref);
#endif

    /* TAILQ_REMOVE may or may not be safe to use if the item was never actually
       inserted in a list, so don't remove it if it wasn't. */
    if(remove) {
        TAILQ_REMOVE(&l->block->lobbies, l, qentry);

        /* Decrement the game count if it got incremented for this lobby */
        if(l->type != LOBBY_TYPE_DEFAULT) {
            --l->block->num_games;
            ship_dec_games(l->block->ship);
        }
    }

    lobby_empty_pkt_queue(l);

    /* Clear any limits lists we have attached to this lobby. */
    if(l->limits_list)
        release(l->limits_list);

    /* Free up any items left in the lobby for Blue Burst. */
    i = TAILQ_FIRST(&l->item_queue);
    while(i) {
        tmp = TAILQ_NEXT(i, qentry);
        free(i);
        i = tmp;
    }

    /* Free up the enemy data */
    if(l->map_enemies) {
        free_game_enemies(l);
    }

    free(l);

    pthread_mutex_unlock(&m);
    pthread_mutex_destroy(&m);
}

void lobby_destroy(lobby_t *l) {
    pthread_mutex_lock(&l->mutex);
    lobby_destroy_locked(l, 1);
}

void lobby_destroy_noremove(lobby_t *l) {
    pthread_mutex_lock(&l->mutex);
    lobby_destroy_locked(l, 0);
}

static uint8_t lobby_find_max_challenge(lobby_t *l) {
    int min_lev = 255, min_lev2 = 255, i, j, k;
    ship_client_t *c;

    if(!l->challenge)
        return 0;

    /* Look through everyone's list of completed challenge levels to figure out
       what is the max level for the lobby. */
    for(j = 0; j < l->max_clients; ++j) {
        c = l->clients[j];

        if(c != NULL) {
            switch(c->version) {
                case CLIENT_VERSION_DCV2:
                    k = 0;
                    for(i = 0; i < 9; ++i) {
                        if(c->pl->v2.c_rank.part.times[i] == 0) {
                            break;
                        }
                    }

                    break;

                case CLIENT_VERSION_PC:
                    k = 0;
                    for(i = 0; i < 9; ++i) {
                        if(c->pl->pc.c_rank.part.times[i] == 0) {
                            break;
                        }
                    }

                    break;

                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_BB:
                    for(i = 0; i < 9; ++i) {
                        if(c->pl->v3.c_rank.part.times[i] == 0) {
                            break;
                        }
                    }

                    for(k = 0; k < 5; ++k) {
                        if(c->pl->v3.c_rank.part.times_ep2[k] == 0)
                            break;
                    }

                    break;

                default:
                    /* We shouldn't get here... */
                    return -1;
            }

            if(i < min_lev)
                min_lev = i;

            if(k < min_lev2)
                min_lev2 = k;
        }
    }

    if(l->version >= CLIENT_VERSION_GC)
        return (uint8_t)((min_lev + 1) | ((min_lev2 + 1) << 4));
    else
        return (uint8_t)(min_lev + 1);
}

static int td(ship_client_t *c, lobby_t *l, void *req) {
    uint32_t r = mt19937_genrand_int32(&c->cur_block->rng);
    uint32_t i[4] = { 4, 0, 0, 0 };

    if((r & 15) != 2) {
        return 0;
    }

    r = mt19937_genrand_int32(&c->cur_block->rng);

    switch(l->difficulty) {
        case 0:
            i[3] = r & 0x1F;
            break;

        case 1:
            i[3] = r & 0x3F;
            break;

        case 2:
            i[3] = r & 0x7F;
            break;

        case 3:
            i[3] = r & 0xFF;
            break;

        default:
            return 0;
    }

    if(i[3]) {
        return subcmd_send_lobby_item(l, (subcmd_itemreq_t *)req, i);
    }

    return 0;
}

static int lobby_add_client_locked(ship_client_t *c, lobby_t *l) {
    int i;

    /* Sanity check: Do we have space? */
    if(l->num_clients >= l->max_clients)
        return -1;

    if(l->num_clients)
        l->flags &= ~LOBBY_FLAG_ONLY_ONE;

    /* If this is a team, run the team join script, if it exists. */
    if(l->type != LOBBY_TYPE_DEFAULT)
        script_execute(ScriptActionTeamJoin, c, SCRIPT_ARG_PTR, c,
                       SCRIPT_ARG_PTR, l, SCRIPT_ARG_END);

    /* First client goes in slot 1, not 0 on DC/PC. Why Sega did this, who
       knows? Also slot 1 gets priority for all DC/PC teams, even if slot 0 is
       empty. */
    if(!l->clients[1] && l->version < CLIENT_VERSION_GC &&
       l->type == LOBBY_TYPE_GAME) {
        l->clients[1] = c;
        c->cur_lobby = l;
        c->client_id = 1;
        c->arrow = 0;
        c->join_time = time(NULL);
        ++l->num_clients;

        /* Update the challenge level as needed. */
        if(l->challenge)
            l->max_chal = lobby_find_max_challenge(l);

        if(l->num_clients == 1)
            l->leader_id = c->client_id;

        return 0;
    }

    /* Find a place to put the client. New clients go in the smallest numbered
       empty slot. */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] == NULL) {
            l->clients[i] = c;
            c->cur_lobby = l;
            c->client_id = i;
            c->arrow = 0;
            c->join_time = time(NULL);
            ++l->num_clients;

            /* Update the challenge level as needed. */
            if(l->challenge)
                l->max_chal = lobby_find_max_challenge(l);

            if(l->num_clients == 1)
                l->leader_id = c->client_id;

            return 0;
        }
    }

    /* If we get here, something went terribly wrong... */
    return -1;
}

static int lobby_elect_leader_cross_locked(lobby_t *l) {
    int i, earliest_i = -1;
    time_t earliest = time(NULL);

    /* Go through and look for a new leader. The new leader will be the person
       who has been in the lobby the longest amount of time. This version will
       prefer someone who's on Dreamcast or PC over Gamecube for the sake of a
       cross-platform game. */
    for(i = 0; i < l->max_clients; ++i) {
        /* We obviously can't give it to the old leader, they're gone now. */
        if(i == l->leader_id) {
            continue;
        }
        /* Check if this person joined before the current earliest. */
        else if(l->clients[i] && l->clients[i]->join_time < earliest &&
                l->clients[i]->version < CLIENT_VERSION_GC) {
            earliest_i = i;
            earliest = l->clients[i]->join_time;
        }
    }

    /* Are we stuck with someone on Gamecube? If so, we can't support cross-
       platform play anymore... */
    if(earliest_i == -1) {
        l->flags &= ~LOBBY_FLAG_GC_ALLOWED;
        l->version = CLIENT_VERSION_GC;
        l->episode = 1;

        /* Same loop as above, but without the requirement of not on Gamecube.
           If we're here, everyone's obviously on Gamecube, if anyone's even
           left in the lobby at all. */
        for(i = 0; i < l->max_clients; ++i) {
            if(i == l->leader_id) {
                continue;
            }
            else if(l->clients[i] && l->clients[i]->join_time < earliest) {
                earliest_i = i;
                earliest = l->clients[i]->join_time;
            }
        }
    }

    return earliest_i;
}

static int lobby_elect_leader_locked(lobby_t *l) {
    int i, earliest_i = -1;
    time_t earliest = time(NULL);

    /* Go through and look for a new leader. The new leader will be the person
       who has been in the lobby the longest amount of time. */
    for(i = 0; i < l->max_clients; ++i) {
        /* We obviously can't give it to the old leader, they're gone now. */
        if(i == l->leader_id) {
            continue;
        }
        /* Check if this person joined before the current earliest. */
        else if(l->clients[i] && l->clients[i]->join_time < earliest) {
            earliest_i = i;
            earliest = l->clients[i]->join_time;
        }
    }

    return earliest_i;
}

/* Remove a client from a lobby, returns 0 if the lobby should stay, -1 on
   failure. */
static int lobby_remove_client_locked(ship_client_t *c, int client_id,
                                      lobby_t *l) {
    int new_leader;

    /* Sanity check... Was the client where it said it was? */
    if(l->clients[client_id] != c) {
        return -1;
    }

    /* The client was the leader... we need to fix that. */
    if(client_id == l->leader_id) {
        if(l->version < CLIENT_VERSION_GC &&
           (l->flags & LOBBY_FLAG_GC_ALLOWED)) {
            new_leader = lobby_elect_leader_cross_locked(l);
        }
        else {
            new_leader = lobby_elect_leader_locked(l);
        }

        /* Check if we didn't get a new leader. */
        if(new_leader == -1) {
            /* We should probably remove the lobby in this case. */
            l->leader_id = 0;
        }
        else {
            /* And, we have a winner! */
            l->leader_id = new_leader;
        }
    }

    /* Remove the client from our list, and we're done. */
    l->clients[client_id] = NULL;
    --l->num_clients;

    /* Make sure the maximum challenge level available hasn't changed... */
    if(l->challenge)
        l->max_chal = lobby_find_max_challenge(l);

    /* If this is the player's current lobby, fix that. */
    if(c->cur_lobby == l) {
        c->cur_lobby = NULL;
        c->client_id = 0;
    }

    /* If this is a team, run the team leave script, if it exists. */
    if(l->type != LOBBY_TYPE_DEFAULT) {
        script_execute(ScriptActionTeamLeave, c, SCRIPT_ARG_PTR, c,
                       SCRIPT_ARG_PTR, l, SCRIPT_ARG_END);
        l->qpos_regs[0][client_id] = 0;
        l->qpos_regs[1][client_id] = 0;
        l->qpos_regs[2][client_id] = 0;
        l->qpos_regs[3][client_id] = 0;
    }

    return l->type == LOBBY_TYPE_DEFAULT ? 0 : !l->num_clients;
}

/* Add the client to any available lobby on the current block. */
int lobby_add_to_any(ship_client_t *c, lobby_t *req) {
    block_t *b = c->cur_block;
    lobby_t *l;
    int added = 0;

    /* If a specific lobby was requested, try that one first. */
    if(req) {
        pthread_mutex_lock(&req->mutex);

        if(req->type == LOBBY_TYPE_DEFAULT &&
           req->num_clients < req->max_clients) {
            /* They should be OK to join this one... */
            if(!lobby_add_client_locked(c, req)) {
                c->lobby_id = req->lobby_id;
                pthread_mutex_unlock(&req->mutex);
                return 0;
            }
        }

        pthread_mutex_unlock(&req->mutex);
    }

    /* Add to the first available default lobby. */
    TAILQ_FOREACH(l, &b->lobbies, qentry) {
        /* Don't look at lobbies we can't see. */
        if(c->version == CLIENT_VERSION_DCV1 && l->lobby_id > 10) {
            continue;
        }

        pthread_mutex_lock(&l->mutex);

        if(l->type == LOBBY_TYPE_DEFAULT && l->num_clients < l->max_clients) {
            /* We've got a candidate, add away. */
            if(!lobby_add_client_locked(c, l)) {
                added = 1;
                c->lobby_id = l->lobby_id;
            }
        }

        pthread_mutex_unlock(&l->mutex);

        if(added) {
            break;
        }
    }

    return !added;
}

int lobby_change_lobby(ship_client_t *c, lobby_t *req) {
    lobby_t *l = c->cur_lobby;
    int rv = 0;
    int old_cid = c->client_id;
    int delete_lobby = 0;
    int override = (c->flags & CLIENT_FLAG_OVERRIDE_GAME);

    /* Clear the override flag */
    c->flags &= ~CLIENT_FLAG_OVERRIDE_GAME;

    /* If they're not in a lobby, add them to the first available default
       lobby. */
    if(!l) {
        if(lobby_add_to_any(c, req)) {
            return -11;
        }

        l = c->cur_lobby;

        if(send_lobby_join(c, l)) {
            return -11;
        }

        if(send_lobby_add_player(l, c)) {
            return -11;
        }

        c->lobby_id = l->lobby_id;

        /* Send the message to the shipgate */
        shipgate_send_lobby_chg(&ship->sg, c->guildcard, l->lobby_id,
                                l->name);

        return 0;
    }

    /* Swap the data out on the server end before we do anything rash. */
    pthread_mutex_lock(&l->mutex);

    if(l != req) {
        pthread_mutex_lock(&req->mutex);
    }

    /* Don't allow HUcaseal, FOmar, or RAmarl characters in v1 games. */
    if(req->type == LOBBY_TYPE_GAME && req->version == CLIENT_VERSION_DCV1 &&
       c->pl->v1.ch_class > DCPCClassMax) {
        rv = -15;
        goto out;
    }

    /* Make sure this isn't a single-player lobby. */
    if((req->flags & LOBBY_FLAG_SINGLEPLAYER) && req->num_clients) {
        rv = -14;
        goto out;
    }

    /* See if the lobby doesn't allow this player by policy. */
    if((req->flags & LOBBY_FLAG_PCONLY) && c->version != CLIENT_VERSION_PC &&
       !override) {
        rv = -13;
        goto out;
    }

    if((req->flags & LOBBY_FLAG_V1ONLY) && c->version != CLIENT_VERSION_DCV1 &&
       !override) {
        rv = -12;
        goto out;
    }

    if((req->flags & LOBBY_FLAG_DCONLY) && c->version != CLIENT_VERSION_DCV1 &&
       c->version != CLIENT_VERSION_DCV2 && !override) {
        rv = -11;
        goto out;
    }

    /* Make sure the lobby is actually available at the moment. */
    if((req->flags & LOBBY_FLAG_TEMP_UNAVAIL)) {
        rv = -10;
        goto out;
    }

    /* Make sure there isn't currently a client bursting */
    if((req->flags & LOBBY_FLAG_BURSTING)) {
        rv = -3;
        goto out;
    }

    /* Make sure a quest isn't in progress. */
    if((req->flags & LOBBY_FLAG_QUESTING) &&
       !(req->q_flags & LOBBY_QFLAG_JOIN)) {
        rv = -7;
        goto out;
    }
    else if((req->flags & LOBBY_FLAG_QUESTSEL)) {
        rv = -8;
        goto out;
    }

    /* Make sure the character is in the correct level range. */
    if(req->min_level > (LE32(c->pl->v1.level) + 1) && !override) {
        /* Too low. */
        rv = -4;
        goto out;
    }

    if(req->max_level < (LE32(c->pl->v1.level) + 1) && !override) {
        /* Too high. */
        rv = -5;
        goto out;
    }

    /* Make sure a V1 client isn't trying to join a V2 only lobby. */
    if(c->version == CLIENT_VERSION_DCV1 && req->v2) {
        rv = -6;
        goto out;
    }

    /* Make sure that the client is legit enough to be there. */
    if((req->type == LOBBY_TYPE_GAME) && (req->flags & LOBBY_FLAG_LEGIT_MODE) &&
       !lobby_check_client_legit(req, ship, c) && !override) {
        rv = -9;
        goto out;
    }

    if(l != req) {
        /* Attempt to add the client to the new lobby first. */
        if(lobby_add_client_locked(c, req)) {
            /* Nope... we can't do that, the lobby's probably full. */
            rv = -1;
            goto out;
        }

        /* The client is in the new lobby so we still need to remove them from
           the old lobby. */
        delete_lobby = lobby_remove_client_locked(c, old_cid, l);

        if(delete_lobby < 0) {
            /* Uhh... what do we do about this... */
            rv = -2;
            goto out;
        }
    }

    /* The client is now happily in their new home, update the clients in the
       old lobby so that they know the requester has gone... */
    send_lobby_leave(l, c, old_cid);

    /* ...tell the client they've changed lobbies successfully... */
    if(c->cur_lobby->type == LOBBY_TYPE_DEFAULT) {
        send_lobby_join(c, c->cur_lobby);
        c->lobby_id = c->cur_lobby->lobby_id;
    }
    else {
        memset(c->enemy_kills, 0, sizeof(uint32_t) * 0x60);
        memset(c->q_stack, 0, sizeof(uint32_t) * CLIENT_MAX_QSTACK);
        c->q_stack_top = 0;
        send_game_join(c, c->cur_lobby);
        c->cur_lobby->flags |= LOBBY_FLAG_BURSTING;
        c->flags |= CLIENT_FLAG_BURSTING;
    }

    /* ...and let his/her new lobby know that he/she has arrived. */
    send_lobby_add_player(c->cur_lobby, c);

    /* If the old lobby is empty (and not a default lobby), remove it. */
    if(delete_lobby) {
        pthread_rwlock_wrlock(&c->cur_block->lobby_lock);
        lobby_destroy_locked(l, 1);
        pthread_rwlock_unlock(&c->cur_block->lobby_lock);
    }

    /* Send the message to the shipgate */
    shipgate_send_lobby_chg(&ship->sg, c->guildcard, c->cur_lobby->lobby_id,
                            c->cur_lobby->name);

out:
    /* We're done, unlock the locks. */
    if(l != req) {
        pthread_mutex_unlock(&req->mutex);
    }

    if(delete_lobby < 1) {
        pthread_mutex_unlock(&l->mutex);
    }

    return rv;
}

/* Remove a player from a lobby without changing their lobby (for instance, if
   they disconnected). */
int lobby_remove_player(ship_client_t *c) {
    lobby_t *l = c->cur_lobby;
    int rv = 0, delete_lobby, client_id;

    /* They're not in a lobby, so we're done. */
    if(!l) {
        return 0;
    }

    /* Lock the mutex before we try anything funny. */
    pthread_mutex_lock(&l->mutex);

    /* If they were bursting, unlock the lobby... */
    if((c->flags & CLIENT_FLAG_BURSTING)) {
        l->flags &= ~LOBBY_FLAG_BURSTING;
        lobby_handle_done_burst(l, NULL);
    }

    /* If the client is leaving a game lobby, then send their monster stats
       up to the shipgate. */
    if(l->type == LOBBY_TYPE_GAME && (c->flags & CLIENT_FLAG_TRACK_KILLS))
        shipgate_send_mkill(&ship->sg, c->guildcard, c->cur_block->b, c, l);

    /* We have a nice function to handle most of the heavy lifting... */
    client_id = c->client_id;
    delete_lobby = lobby_remove_client_locked(c, client_id, l);

    if(delete_lobby < 0) {
        /* Uhh... what do we do about this... */
        rv = -1;
        goto out;
    }

    /* The client has now gone completely away, update the clients in the lobby
       so that they know the requester has gone. */
    send_lobby_leave(l, c, client_id);

    if(delete_lobby) {
        pthread_rwlock_wrlock(&c->cur_block->lobby_lock);
        lobby_destroy_locked(l, 1);
        pthread_rwlock_unlock(&c->cur_block->lobby_lock);
    }

    c->cur_lobby = NULL;

out:
    /* We're done, clean up. */
    if(delete_lobby < 1) {
        pthread_mutex_unlock(&l->mutex);
    }

    return rv;
}

int lobby_send_pkt_dcnte(lobby_t *l, ship_client_t *c, void *h, void *h2,
                         int igcheck) {
    dc_pkt_hdr_t *hdr = (dc_pkt_hdr_t *)h, *hdr2 = (dc_pkt_hdr_t *)h2;
    int i;

    /* Send the packet to every connected client. */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] && l->clients[i] != c) {
            /* If we're supposed to check the ignore list, and this client is on
               it, don't send the packet. */
            if(igcheck && client_has_ignored(l->clients[i], c->guildcard)) {
                continue;
            }

            if(c->version == CLIENT_VERSION_DCV1 &&
               (c->flags & CLIENT_FLAG_IS_NTE))
                send_pkt_dc(l->clients[i], hdr);
            else
                send_pkt_dc(l->clients[i], hdr2);
        }
    }

    return 0;
}

int lobby_send_pkt_dc(lobby_t *l, ship_client_t *c, void *h, int igcheck) {
    dc_pkt_hdr_t *hdr = (dc_pkt_hdr_t *)h;
    int i;

    /* Send the packet to every connected client. */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] && l->clients[i] != c) {
            /* If we're supposed to check the ignore list, and this client is on
               it, don't send the packet. */
            if(igcheck && client_has_ignored(l->clients[i], c->guildcard)) {
                continue;
            }

            send_pkt_dc(l->clients[i], hdr);
        }
    }

    return 0;
}

int lobby_send_pkt_bb(lobby_t *l, ship_client_t *c, void *h, int igcheck) {
    bb_pkt_hdr_t *hdr = (bb_pkt_hdr_t *)h;
    int i;

    /* Send the packet to every connected client. */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] && l->clients[i] != c) {
            /* If we're supposed to check the ignore list, and this client is on
               it, don't send the packet. */
            if(igcheck && client_has_ignored(l->clients[i], c->guildcard)) {
                continue;
            }

            send_pkt_bb(l->clients[i], hdr);
        }
    }

    return 0;
}

int lobby_send_pkt_ep3(lobby_t *l, ship_client_t *c, void *h) {
    dc_pkt_hdr_t *hdr = (dc_pkt_hdr_t *)h;
    int i;

    /* Send the packet to every connected Episode 3 client. */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] && l->clients[i] != c &&
           l->version == CLIENT_VERSION_EP3) {
            send_pkt_dc(l->clients[i], hdr);
        }
    }

    return 0;
}

static const char mini_language_codes[][3] = {
    "J", "E", "G", "F", "S", "CS", "CT", "K"
};

/* Send an information reply packet with information about the lobby. */
int lobby_info_reply(ship_client_t *c, uint32_t lobby) {
    char msg[512] = { 0 };
    lobby_t *l = block_get_lobby(c->cur_block, lobby);
    int i, lang;
    player_t *pl;
    time_t t;
    int h, m, s;
    int legit, questing, drops;
    quest_map_elem_t *qelem;
    sylverant_quest_t *quest;

    if(!l) {
        return send_info_reply(c, __(c, "\tEThis team is no\nlonger active."));
    }

    /* Lock the lobby */
    pthread_mutex_lock(&l->mutex);

    /* Check if we should be on page 2 of the info or on the first page. */
    if(c->last_info_req == lobby) {
        /* Calculate any statistics we want for this */
        t = time(NULL) - l->create_time;
        h = t / 3600;
        m = (t % 3600) / 60;
        s = t % 60;
        legit = l->flags & LOBBY_FLAG_LEGIT_MODE;
        questing = l->flags & LOBBY_FLAG_QUESTING;
        drops = l->flags & LOBBY_FLAG_SERVER_DROPS;

        sprintf(msg, "%s: %d:%02d:%02d\n"   /* Game time */
                "%s: %s\n"                  /* Legit/normal mode */
                "%s: %d-%d\n"               /* Levels allowed */
                "%s: %s\n"                  /* Client/Server Drops */
                "%s:\n",                    /* Versions allowed */
                __(c, "\tETime"), h, m, s,
                __(c, "Mode"), legit ? __(c, "Legit") : __(c, "Normal"),
                __(c, "Levels"), l->min_level, l->max_level,
                __(c, "Drops"), drops ? __(c, "Server") : __(c, "Client"),
                __(c, "Versions Allowed"));

        /* Figure out what versions are allowed. */
        if(l->version == CLIENT_VERSION_BB) {
            /* Blue Burst is easy... */
            strcat(msg, " Blue Burst");
        }
        else if(l->version == CLIENT_VERSION_GC) {
            /* Easy one here, GC games can only have GC chars */
            strcat(msg, " GC");
        }
        else if(l->version == CLIENT_VERSION_EP3) {
            /* Also easy, since Episode 3 games are completely different */
            strcat(msg, " Episode 3");
        }
        else {
            /* Slightly more interesting here... */
            if((l->flags & LOBBY_FLAG_NTE)) {
                if(l->v2)
                    strcat(msg, " PC-NTE");
                else
                    strcat(msg, " DC-NTE");
            }
            else if(l->v2) {
                if(!(l->flags & LOBBY_FLAG_PCONLY)) {
                    strcat(msg, " V2");
                }
                if(!(l->flags & LOBBY_FLAG_DCONLY)) {
                    strcat(msg, " PC");
                }
            }
            else {
                if(!(l->flags & LOBBY_FLAG_PCONLY)) {
                    strcat(msg, " V1");

                    if(!(l->flags & LOBBY_FLAG_V1ONLY)) {
                        strcat(msg, " V2");
                    }
                }
                if(!(l->flags & LOBBY_FLAG_DCONLY) &&
                   !(l->flags & LOBBY_FLAG_V1ONLY)) {
                    strcat(msg, " PC");
                }
            }

            if((l->flags & LOBBY_FLAG_GC_ALLOWED) &&
               !(l->flags & LOBBY_FLAG_DCONLY) &&
               !(l->flags & LOBBY_FLAG_PCONLY) &&
               !(l->flags & LOBBY_FLAG_V1ONLY) &&
               !(l->flags & LOBBY_FLAG_NTE)) {
                strcat(msg, " GC");
            }
        }

        if(questing) {
            qelem = quest_lookup(&ship->qmap, l->qid);

            /* Look for the quest in question... */
            if((quest = qelem->qptr[c->version][c->q_lang]) ||
               (quest = qelem->qptr[l->version][c->q_lang])) {
                lang = c->q_lang;
            }
            else if((quest = qelem->qptr[c->version][c->language_code]) ||
                    (quest = qelem->qptr[l->version][c->language_code])) {
                lang = c->language_code;
            }
            else if((quest = qelem->qptr[c->version][CLIENT_LANG_ENGLISH]) ||
                    (quest = qelem->qptr[c->version][CLIENT_LANG_ENGLISH])) {
                lang = CLIENT_LANG_ENGLISH;
            }
            else {
                quest = qelem->qptr[l->version][l->qlang];
                lang = l->qlang;
            }

            /* We definitely should have it now... */
            if(quest) {
                sprintf(msg, "%s\n%s: %s", msg, __(c, "Quest"), quest->name);
                if(lang == CLIENT_LANG_JAPANESE)
                    sprintf(msg, "\tJ%s", msg);
                else
                    sprintf(msg, "\tE%s", msg);
            }
            else {
                sprintf(msg, "%s\n%s", msg, __(c, "Questing"));
            }
        }
        else {
            sprintf(msg, "%s\n%s", msg, __(c, "Free Adventure"));
        }

        /* We might have a third page... */
        if(ship->cfg->limits_count && legit)
            c->last_info_req |= 0x80000000;
        else
            c->last_info_req = 0;
    }
    else if(c->last_info_req == (lobby | 0x80000000)) {
        sprintf(msg, "%s:\n%s",
                __(c, "\tELegit Mode"),
                l->limits_list->name ? l->limits_list->name : "Default");
        c->last_info_req = 0;
    }
    else {
        c->last_info_req = lobby;

        /* Build up the information string */
        for(i = 0; i < l->max_clients; ++i) {
            /* Ignore blank clients */
            if(!l->clients[i]) {
                continue;
            }

            /* Grab the player data and fill in the string */
            pl = l->clients[i]->pl;

            sprintf(msg, "%s%s L%d\n  %s    %s\n", msg, pl->v1.name,
                    pl->v1.level + 1, classes[pl->v1.ch_class],
                    mini_language_codes[pl->v1.inv.language]);
        }
    }

    /* Unlock the lobby */
    pthread_mutex_unlock(&l->mutex);

    /* Send the reply */
    return send_info_reply(c, msg);
}

/* Check if a single player is legit enough for the lobby. */
int lobby_check_player_legit(lobby_t *l, ship_t *s, player_t *pl, uint32_t v) {
    int j, rv = 1, irv = 1;
    sylverant_iitem_t *item;

    /* If we don't have a legit mode set, then everyone's legit! */
    if(!l->limits_list || !(l->flags & LOBBY_FLAG_LEGIT_MODE)) {
        return 1;
    }

    /* Look through each item */
    for(j = 0; j < pl->v1.inv.item_count; ++j) {
        item = (sylverant_iitem_t *)&pl->v1.inv.items[j];
        irv = sylverant_limits_check_item(l->limits_list, item, v);

        if(!irv) {
            debug(DBG_LOG, "Potentially non-legit item in legit mode:\n"
                  "%08x %08x %08x %08x\n", LE32(item->data_l[0]),
                  LE32(item->data_l[1]), LE32(item->data_l[2]),
                  LE32(item->data2_l));
            rv = irv;
        }
    }

    return rv;
}

/* Check if a single client is legit enough for the lobby. */
int lobby_check_client_legit(lobby_t *l, ship_t *s, ship_client_t *c) {
    int rv;
    uint32_t version;

    pthread_mutex_lock(&c->mutex);

    switch(c->version) {
        case CLIENT_VERSION_DCV1:
            version = ITEM_VERSION_V1;
            break;

        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
            version = ITEM_VERSION_V2;
            break;

        case CLIENT_VERSION_GC:
            version = ITEM_VERSION_GC;
            break;

        default:
            return 1;
    }

    rv = lobby_check_player_legit(l, s, c->pl, version);
    pthread_mutex_unlock(&c->mutex);

    return rv;
}

/* Send out any queued packets when we get a done burst signal. You must hold
   the lobby's lock when calling this. */
int lobby_handle_done_burst(lobby_t *l, ship_client_t *c) {
    lobby_pkt_t *i;
    int rv = 0;
    int j;

    /* Go through each packet and handle it */
    while((i = STAILQ_FIRST(&l->pkt_queue))) {
        STAILQ_REMOVE_HEAD(&l->pkt_queue, qentry);

        /* As long as we haven't run into issues yet, continue sending the
           queued packets */
        if(rv == 0) {
            switch(i->pkt->pkt_type) {
                case GAME_COMMAND0_TYPE:
                    if(subcmd_handle_bcast(i->src, (subcmd_pkt_t *)i->pkt)) {
                        rv = -1;
                    }
                    break;

                case GAME_COMMAND2_TYPE:
                case GAME_COMMANDD_TYPE:
                    if(subcmd_handle_one(i->src, (subcmd_pkt_t *)i->pkt)) {
                        rv = -1;
                    }
                    break;

                default:
                    rv = -1;
            }
        }

        free(i->pkt);
        free(i);
    }

    /* Handle any synced regs. */
    if(c && (l->q_flags & LOBBY_QFLAG_SYNC_REGS)) {
        for(j = 0; j < l->num_syncregs; ++j) {
            if(l->regvals[j]) {
                if(send_sync_register(c, l->syncregs[j], l->regvals[j])) {
                    rv = -1;
                    break;
                }
            }
        }
    }

    return rv;
}

int lobby_resend_burst(lobby_t *l, ship_client_t *c) {
    lobby_pkt_t *i;
    int rv = 0;

    /* Go through each packet and handle it */
    while((i = STAILQ_FIRST(&l->burst_queue))) {
        STAILQ_REMOVE_HEAD(&l->burst_queue, qentry);

        /* As long as none of the earlier packets have errored out, continue on
           by re-handling the old packet. */
        if(rv == 0) {
            switch(i->pkt->pkt_type) {
                case GAME_COMMAND2_TYPE:
                case GAME_COMMANDD_TYPE:
                    rv = send_pkt_dc(c, i->pkt);
                    break;

                default:
                    rv = -1;
            }
        }

        free(i->pkt);
        free(i);
    }

    return rv;
}

/* Enqueue a packet for later sending (due to a player bursting) */
static int lobby_enqueue_pkt_ex(lobby_t *l, ship_client_t *c, dc_pkt_hdr_t *p,
                                int q) {
    lobby_pkt_t *pkt;
    int rv = 0;
    uint16_t len = LE16(p->pkt_len);

    pthread_mutex_lock(&l->mutex);

    /* Sanity checks... */
    if(!(l->flags & LOBBY_FLAG_BURSTING)) {
        rv = -1;
        goto out;
    }

    if(p->pkt_type != GAME_COMMAND0_TYPE && p->pkt_type != GAME_COMMAND2_TYPE &&
       p->pkt_type != GAME_COMMANDD_TYPE) {
        rv = -2;
        goto out;
    }

    /* Allocate space */
    pkt = (lobby_pkt_t *)malloc(sizeof(lobby_pkt_t));
    if(!pkt) {
        rv = -3;
        goto out;
    }

    pkt->pkt = (dc_pkt_hdr_t *)malloc(len);
    if(!pkt->pkt) {
        free(pkt);
        rv = -3;
        goto out;
    }

    /* Fill in the struct */
    pkt->src = c;
    memcpy(pkt->pkt, p, len);

    /* Insert into the packet queue */
    if(!q)
        STAILQ_INSERT_TAIL(&l->pkt_queue, pkt, qentry);
    else
        STAILQ_INSERT_TAIL(&l->burst_queue, pkt, qentry);

out:
    pthread_mutex_unlock(&l->mutex);
    return rv;
}

int lobby_enqueue_pkt(lobby_t *l, ship_client_t *c, dc_pkt_hdr_t *p) {
    return lobby_enqueue_pkt_ex(l, c, p, 0);
}

int lobby_enqueue_burst(lobby_t *l, ship_client_t *c, dc_pkt_hdr_t *p) {
    return lobby_enqueue_pkt_ex(l, c, p, 1);
}

/* Add an item to the lobby's inventory. The caller must hold the lobby's mutex
   before calling this. Returns NULL if there is no space in the lobby's
   inventory for the new item. */
item_t *lobby_add_item_locked(lobby_t *l, uint32_t item_data[4]) {
    lobby_item_t *item;

    /* Sanity check... */
    if(l->version != CLIENT_VERSION_BB)
        return NULL;

    if(!(item = (lobby_item_t *)malloc(sizeof(lobby_item_t))))
        return NULL;

    memset(item, 0, sizeof(lobby_item_t));

    /* Copy the item data in. */
    item->d.item_id = LE32(l->item_id);
    item->d.data_l[0] = LE32(item_data[0]);
    item->d.data_l[1] = LE32(item_data[1]);
    item->d.data_l[2] = LE32(item_data[2]);
    item->d.data2_l = LE32(item_data[3]);

    /* Increment the item ID, add it to the queue, and return the new item */
    ++l->item_id;
    TAILQ_INSERT_HEAD(&l->item_queue, item, qentry);
    return &item->d;
}

item_t *lobby_add_item2_locked(lobby_t *l, item_t *it) {
    lobby_item_t *item;

    /* Sanity check... */
    if(l->version != CLIENT_VERSION_BB)
        return NULL;

    if(!(item = (lobby_item_t *)malloc(sizeof(lobby_item_t))))
        return NULL;

    memset(item, 0, sizeof(lobby_item_t));

    /* Copy the item data in. */
    memcpy(&item->d, it, sizeof(item_t));

    /* Add it to the queue, and return the new item */
    TAILQ_INSERT_HEAD(&l->item_queue, item, qentry);
    return &item->d;
}

int lobby_remove_item_locked(lobby_t *l, uint32_t item_id, item_t *rv) {
    lobby_item_t *i, *tmp;

    if(l->version != CLIENT_VERSION_BB)
        return -1;

    memset(rv, 0, sizeof(item_t));
    rv->data_l[0] = LE32(Item_NoSuchItem);

    i = TAILQ_FIRST(&l->item_queue);
    while(i) {
        tmp = TAILQ_NEXT(i, qentry);

        if(i->d.item_id == item_id) {
            memcpy(rv, &i->d, sizeof(item_t));
            TAILQ_REMOVE(&l->item_queue, i, qentry);
            free(i);
            return 0;
        }

        i = tmp;
    }

    return 1;
}

void lobby_send_kill_counts(lobby_t *l) {
    int i;
    ship_client_t *c;

    for(i = 0; i < l->max_clients; ++i) {
        c = l->clients[i];

        /* Send the client's current count and clear out the counters so that we
           don't double count any kills. */
        if(c && (c->flags & CLIENT_FLAG_TRACK_KILLS)) {
            shipgate_send_mkill(&ship->sg, c->guildcard, c->cur_block->b, c, l);
            memset(c->enemy_kills, 0, sizeof(uint32_t) * 0x60);
        }
    }
}

int lobby_setup_quest(lobby_t *l, ship_client_t *c, uint32_t qid, int lang) {
    int rv = 0, flagsset = 0;
    quest_map_elem_t *e;
    sylverant_quest_t *q;

    pthread_rwlock_rdlock(&ship->qlock);
    pthread_mutex_lock(&l->mutex);

    /* Do we have quests configured? */
    if(!TAILQ_EMPTY(&ship->qmap)) {
        e = quest_lookup(&ship->qmap, qid);

        /* We have a bit of extra work on GC/BB quests... */
        if(l->version >= CLIENT_VERSION_GC) {
            /* Find the quest... */
            for(rv = 0; rv < CLIENT_LANG_COUNT; ++rv) {
                if((q = e->qptr[l->version][rv]))
                    break;
            }

            /* This shouldn't happen... */
            if(!q) {
                rv = send_message1(c, "%s", __(c, "\tE\tC4Quest info\n"
                                               "missing.\n\n"
                                               "Report this to\n"
                                               "the ship admin."));
                pthread_mutex_unlock(&l->mutex);
                pthread_rwlock_unlock(&ship->qlock);
                return rv;
            }

            /* Update the lobby's episode, just in case it doesn't
               match up with what's already there. */
            l->episode = q->episode;
        }

        l->flags |= LOBBY_FLAG_QUESTING;
        l->flags &= ~LOBBY_FLAG_QUESTSEL;

        /* Send the clients' kill counts if any of them have kill
           tracking enabled. That way, in case there's an event running
           that doesn't allow quest kills to count, the user will still
           get an updated count if anything was already killed. */
        lobby_send_kill_counts(l);

        l->qid = qid;
        l->qlang = (uint8_t)lang;
        load_quest_enemies(l, qid, l->version);

#ifdef DEBUG
        pthread_mutex_lock(&log_mutex);
        fdebug(logfp, DBG_LOG, "BLOCK%02d: Team with id %" PRIu32 " loading "
               "quest ID %" PRIu32 "\n", l->block->b, l->lobby_id, qid);
        fdebug(logfp, DBG_LOG, "         Enemies Array: %p\n", l->map_enemies);
        fdebug(logfp, DBG_LOG, "         Object Array: %p\n", l->map_objs);
        pthread_mutex_unlock(&log_mutex);
#endif

        /* If the team log is running, note that the quest is loading... */
        if(l->logfp) {
            fdebug(l->logfp, DBG_LOG, "Loading quest ID %" PRIu32 "\n", qid);
            fdebug(l->logfp, DBG_LOG, "Enemies Array: %p\n", l->map_enemies);
            fdebug(l->logfp, DBG_LOG, "Object Array: %p\n", l->map_objs);
        }

        /* Figure out any information we need about the quest for dealing with
           register shenanigans. */
        for(rv = 0; rv < CLIENT_LANG_COUNT; ++rv) {
            if(!(q = e->qptr[l->version][rv]))
                continue;

            if((q->flags & SYLVERANT_QUEST_FLAG16)) {
                l->q_shortflag_reg = q->server_flag16_reg;
                l->q_flags |= LOBBY_QFLAG_SHORT;
                flagsset = 1;
            }

            if((q->flags & SYLVERANT_QUEST_DATAFL)) {
                l->q_data_reg = q->server_data_reg;
                l->q_ctl_reg = q->server_ctl_reg;
                l->q_flags |= LOBBY_QFLAG_DATA;
                flagsset = 1;
            }

            if((q->flags & SYLVERANT_QUEST_JOINABLE)) {
                l->q_flags |= LOBBY_QFLAG_JOIN;

                if((q->flags & SYLVERANT_QUEST_SYNC_REGS)) {
                    l->q_flags |= LOBBY_QFLAG_SYNC_REGS;

                    l->num_syncregs = q->num_sync;
                    if(!(l->syncregs = (uint8_t *)malloc(q->num_sync))) {
                        debug(DBG_ERROR, "Error allocating syncregs!\n");
                        l->q_flags &= ~(LOBBY_QFLAG_JOIN |
                            LOBBY_QFLAG_SYNC_REGS);
                        l->num_syncregs = 0;
                    }
                    else if(!(l->regvals =
                                (uint32_t *)malloc(q->num_sync << 2))) {
                        debug(DBG_ERROR, "Error allocating regvals!\n");
                        l->q_flags &= ~(LOBBY_QFLAG_JOIN |
                            LOBBY_QFLAG_SYNC_REGS);
                        free(l->syncregs);
                        l->syncregs = NULL;
                        l->num_syncregs = 0;
                    }
                    else {
                        memcpy(l->syncregs, q->synced_regs, q->num_sync);
                        memset(l->regvals, 0, q->num_sync << 2);
                    }
                }

                flagsset = 1;
            }

            if(flagsset)
                break;
        }

        rv = send_quest(l, qid, lang);
    }
    else {
        /* This really shouldn't happen... */
        rv = send_message1(c, "%s", __(c, "\tE\tC4Quests not\n"
                                       "configured."));
    }

    pthread_mutex_unlock(&l->mutex);
    pthread_rwlock_unlock(&ship->qlock);

    return rv;
}

#ifdef ENABLE_LUA

static int lobby_id_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, lb->lobby_id);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int lobby_type_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, lb->type);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int lobby_flags_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, lb->flags);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int lobby_numclients_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, lb->num_clients);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int lobby_block_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        lua_pushlightuserdata(l, lb->block);
    }
    else {
        lua_pushnil(l);
    }

    return 1;
}

static int lobby_version_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, lb->version);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int lobby_leaderID_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, lb->leader_id);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int lobby_leader_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        lua_pushlightuserdata(l, lb->clients[lb->leader_id]);
    }
    else {
        lua_pushnil(l);
    }

    return 1;
}

static int lobby_difficulty_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, lb->difficulty);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int lobby_isBattleMode_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        lua_pushboolean(l, lb->battle);
    }
    else {
        lua_pushboolean(l, 0);
    }

    return 1;
}

static int lobby_isChallengeMode_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        lua_pushboolean(l, lb->challenge);
    }
    else {
        lua_pushboolean(l, 0);
    }

    return 1;
}

static int lobby_section_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, lb->section);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int lobby_episode_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, lb->episode);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int lobby_name_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        lua_pushstring(l, lb->name);
    }
    else {
        lua_pushnil(l);
    }

    return 1;
}

static int lobby_questID_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        if((lb->flags & LOBBY_FLAG_QUESTING))
            lua_pushinteger(l, lb->qid);
        else
            lua_pushinteger(l, 0);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int lobby_client_lua(lua_State *l) {
    lobby_t *lb;
    lua_Integer cn;

    if(lua_islightuserdata(l, 1) && lua_isinteger(l, 2)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        cn = lua_tointeger(l, 2);

        if(cn < lb->max_clients && lb->clients[cn])
            lua_pushlightuserdata(l, lb->clients[cn]);
        else
            lua_pushnil(l);
    }
    else {
        lua_pushnil(l);
    }

    return 1;
}

static int lobby_clients_lua(lua_State *l) {
    lobby_t *lb;
    int i;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);

        lua_newtable(l);
        for(i = 0; i < lb->max_clients; ++i) {
            if(lb->clients[i])
                lua_pushlightuserdata(l, lb->clients[i]);
            else
                lua_pushnil(l);

            lua_rawseti(l, -2, i + 1);
        }
    }
    else {
        lua_pushnil(l);
    }

    return 1;
}

static int lobby_sendmsg_lua(lua_State *l) {
    lobby_t *lb;
    const char *s;
    size_t len;
    int i;

    if(lua_islightuserdata(l, 1) && lua_isstring(l, 2)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        s = (const char *)lua_tolstring(l, 2, &len);

        for(i = 0; i < lb->max_clients; ++i) {
            if(lb->clients[i])
                send_txt(lb->clients[i], "\tE\tC7%s", s);
        }
    }

    lua_pushinteger(l, 0);
    return 1;
}

static int lobby_gettable_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        lua_rawgeti(l, LUA_REGISTRYINDEX, lb->script_ref);
    }
    else {
        lua_pushnil(l);
    }

    return 1;
}

static int lobby_randInt_lua(lua_State *l) {
    lobby_t *lb;
    uint32_t rn;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        rn = mt19937_genrand_int32(&lb->block->rng);
        lua_pushinteger(l, (lua_Integer)rn);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int lobby_randFloat_lua(lua_State *l) {
    lobby_t *lb;
    double rn;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        rn = mt19937_genrand_real1(&lb->block->rng);
        lua_pushnumber(l, (lua_Number)rn);
    }
    else {
        lua_pushnumber(l, -1);
    }

    return 1;
}

static int lobby_setSinglePlayer_lua(lua_State *l) {
    lobby_t *lb;

    if(lua_islightuserdata(l, 1)) {
        lb = (lobby_t *)lua_touserdata(l, 1);

        pthread_mutex_lock(&lb->mutex);

        if(lb->num_clients == 1) {
            lb->flags |= LOBBY_FLAG_SINGLEPLAYER;
            lua_pushboolean(l, 1);
        }
        else {
            lua_pushboolean(l, 0);
        }

        pthread_mutex_unlock(&lb->mutex);
    }
    else {
        lua_pushboolean(l, 0);
    }

    return 1;
}

static int lobby_setEventCallback_lua(lua_State *l) {
    lobby_t *lb;
    lua_Integer event;
    int rv;

    if(lua_islightuserdata(l, 1) && lua_isinteger(l, 2) &&
       lua_isfunction(l, 3)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        event = lua_tointeger(l, 2);

        if(event > ScriptActionCount) {
            debug(DBG_WARN, "Script setting unknown lobby event: %d\n",
                  (int)event);
            lua_pushboolean(l, 0);
            return 1;
        }

        pthread_mutex_lock(&lb->mutex);

        /* Push the function to the top of the Lua stack. */
        lua_pushvalue(l, 3);

        /* Attempt to add the callback. */
        rv = script_add_lobby_locked(lb, (script_action_t)event);

        /* Pop the function off of the stack. */
        lua_pop(l, 1);

        /* Push the result of adding the callback to the stack. */
        lua_pushboolean(l, rv);
        pthread_mutex_unlock(&lb->mutex);
    }
    else {
        lua_pushboolean(l, 0);
    }

    return 1;
}

static int lobby_clearEventCallback_lua(lua_State *l) {
    lobby_t *lb;
    lua_Integer event;
    int rv;

    if(lua_islightuserdata(l, 1) && lua_isinteger(l, 2)) {
        lb = (lobby_t *)lua_touserdata(l, 1);
        event = lua_tointeger(l, 2);

        if(event > ScriptActionCount) {
            debug(DBG_WARN, "Script clearing unknown lobby event: %d\n",
                  (int)event);
            lua_pushboolean(l, 0);
            return 1;
        }

        pthread_mutex_lock(&lb->mutex);

        /* Attempt to add the callback. */
        rv = script_remove_lobby_locked(lb, (script_action_t)event);

        /* Push the result of removing the callback to the stack. */
        lua_pushboolean(l, rv);
        pthread_mutex_unlock(&lb->mutex);
    }
    else {
        lua_pushboolean(l, 0);
    }

    return 1;
}

static const luaL_Reg lobbylib[] = {
    { "id", lobby_id_lua },
    { "type", lobby_type_lua },
    { "flags", lobby_flags_lua },
    { "num_clients", lobby_numclients_lua },
    { "numClients", lobby_numclients_lua },
    { "block", lobby_block_lua },
    { "version", lobby_version_lua },
    { "leaderID", lobby_leaderID_lua },
    { "leader", lobby_leader_lua },
    { "difficulty", lobby_difficulty_lua },
    { "isBattleMode", lobby_isBattleMode_lua },
    { "isChallengeMode", lobby_isChallengeMode_lua },
    { "section", lobby_section_lua },
    { "episode", lobby_episode_lua },
    { "name", lobby_name_lua },
    { "questID", lobby_questID_lua },
    { "client", lobby_client_lua },
    { "clients", lobby_clients_lua },
    { "sendMsg", lobby_sendmsg_lua },
    { "getTable", lobby_gettable_lua },
    { "randInt", lobby_randInt_lua },
    { "randFloat", lobby_randFloat_lua },
    { "setSinglePlayer", lobby_setSinglePlayer_lua },
    { "setEventCallback", lobby_setEventCallback_lua },
    { "clearEventCallback", lobby_clearEventCallback_lua },
    { NULL, NULL }
};

int lobby_register_lua(lua_State *l) {
    luaL_newlib(l, lobbylib);
    return 1;
}

#endif /* ENABLE_LUA */
