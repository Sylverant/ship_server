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

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sylverant/mtwist.h>
#include <sylverant/debug.h>
#include <sylverant/checksum.h>

#include "lobby.h"
#include "utils.h"
#include "block.h"
#include "clients.h"
#include "ship_packets.h"
#include "subcmd.h"
#include "ship.h"
#include "shipgate.h"
#include "items.h"

static int td(lobby_t *l, void *req);

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

    /* Initialize the lobby mutex. */
    pthread_mutex_init(&l->mutex, NULL);

    return l;
}

static void lobby_setup_drops(lobby_t *l, uint32_t rs) {
    if(rs == 0x9C350DD4) {
        l->dropfunc = td;
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

lobby_t *lobby_create_game(block_t *block, char *name, char *passwd,
                           uint8_t difficulty, uint8_t battle, uint8_t chal,
                           uint8_t v2, int version, uint8_t section,
                           uint8_t event, uint8_t episode, ship_client_t *c,
                           uint8_t single_player) {
    lobby_t *l = (lobby_t *)malloc(sizeof(lobby_t));
    uint32_t id = 0x20;
    int i;

    /* If we don't have a lobby, bail. */
    if(!l) {
        debug(DBG_WARN, "Couldn't allocate space for game lobby!\n");
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

    if(single_player)
        l->flags |= LOBBY_FLAG_SINGLEPLAYER;

    /* Copy the game name and password. */
    strncpy(l->name, name, 64);
    strncpy(l->passwd, passwd, 64);
    l->name[64] = 0;
    l->passwd[64] = 0;

    /* Initialize the packet queue */
    STAILQ_INIT(&l->pkt_queue);
    TAILQ_INIT(&l->item_queue);

    /* Initialize the lobby mutex. */
    pthread_mutex_init(&l->mutex, NULL);

    /* We need episode to be either 1 or 2 for the below map selection code to
       work. On PSODC and PSOPC, it'll be 0 at this point, so make it 1 (as it
       would be expected to be). */
    if(version < CLIENT_VERSION_GC) {
        episode = 1;
    }

    /* Generate the random maps we'll be using for this game, assuming the
       client hasn't set a maps string. */
    if(!c->next_maps) {
        if(!single_player) {
            for(i = 0; i < 0x20; ++i) {
                if(maps[episode - 1][i] != 1) {
                    l->maps[i] = genrand_int32() % maps[episode - 1][i];
                }
            }
        }
        else {
            for(i = 0; i < 0x20; ++i) {
                if(sp_maps[episode - 1][i] != 1) {
                    l->maps[i] = genrand_int32() % sp_maps[episode - 1][i];
                }
            }
        }
    }
    else {
        if(!single_player) {
            for(i = 0; i < 0x20; ++i) {
                if(c->next_maps[i] < maps[episode - 1][i]) {
                    l->maps[i] = c->next_maps[i];
                }
                else {
                    l->maps[i] = genrand_int32() % maps[episode - 1][i];
                }
            }
        }
        else {
            for(i = 0; i < 0x20; ++i) {
                if(c->next_maps[i] < sp_maps[episode - 1][i]) {
                    l->maps[i] = c->next_maps[i];
                }
                else {
                    l->maps[i] = genrand_int32() % sp_maps[episode - 1][i];
                }
            }
        }

        free(c->next_maps);
        c->next_maps = NULL;
    }

    /* Add it to the list of lobbies, and increment the game count. */
    if(version != CLIENT_VERSION_PC || battle || chal || difficulty == 3) {
        pthread_rwlock_wrlock(&block->lobby_lock);
        TAILQ_INSERT_TAIL(&block->lobbies, l, qentry);
        ++block->num_games;
        pthread_rwlock_unlock(&block->lobby_lock);

        ship_inc_games(block->ship);
    }

    l->rand_seed = genrand_int32();

    lobby_setup_drops(l, sylverant_crc32((uint8_t *)l->name, 16));

    return l;
}

lobby_t *lobby_create_ep3_game(block_t *block, char *name, char *passwd,
                               uint8_t view_battle, uint8_t section) {
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
    l->rand_seed = genrand_int32();
    l->create_time = time(NULL);
    l->flags |= LOBBY_FLAG_EP3;

    /* Copy the game name and password. */
    strncpy(l->name, name, 32);
    strncpy(l->passwd, passwd, 16);
    l->name[33] = 0;
    l->passwd[16] = 0;

    /* Initialize the packet queue */
    STAILQ_INIT(&l->pkt_queue);

    /* Initialize the lobby mutex. */
    pthread_mutex_init(&l->mutex, NULL);

    /* Add it to the list of lobbies, and increment the game count. */
    pthread_rwlock_wrlock(&block->lobby_lock);
    TAILQ_INSERT_TAIL(&block->lobbies, l, qentry);
    ++block->num_games;
    pthread_rwlock_unlock(&block->lobby_lock);
    ship_inc_games(block->ship);

    return l;
}

static void lobby_empty_pkt_queue(lobby_t *l) {
    lobby_pkt_t *i;

    while((i = STAILQ_FIRST(&l->pkt_queue))) {
        STAILQ_REMOVE_HEAD(&l->pkt_queue, qentry);
        free(i->pkt);
        free(i);
    }
}

static void lobby_destroy_locked(lobby_t *l, int remove) {
    pthread_mutex_t m = l->mutex;
    lobby_item_t *i, *tmp;

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

    /* Free up any items left in the lobby for Blue Burst. */
    i = TAILQ_FIRST(&l->item_queue);
    while(i) {
        tmp = TAILQ_NEXT(i, qentry);
        free(i);
        i = tmp;
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
    int min_lev = 255, i, j;
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
                    for(i = 0; i < 9; ++i) {
                        if(c->pl->v2.c_rank.part.times[i] == 0) {
                            break;
                        }
                    }
                    
                    break;

                case CLIENT_VERSION_PC:
                    for(i = 0; i < 9; ++i) {
                        if(c->pl->pc.c_rank.part.times[i] == 0) {
                            break;
                        }
                    }
                    
                    break;

                case CLIENT_VERSION_GC:
                    /* XXXX: Handle ep2 stuff too. */
                    for(i = 0; i < 9; ++i) {
                        if(c->pl->v3.c_rank.part.times[i] == 0) {
                            break;
                        }
                    }

                    break;

                default:
                    /* We shouldn't get here... */
                    return -1;
            }

            if(i < min_lev) {
                min_lev = i;
            }
        }
    }

    return (uint8_t)(min_lev + 1);
}

static int td(lobby_t *l, void *req) {
    uint32_t r = genrand_int32();
    uint32_t i[4] = { 4, 0, 0, 0 };

    if((r & 3) == 2) {
        return 0;
    }

    r = genrand_int32();

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
    uint8_t clev = l->max_chal;

    /* Sanity check: Do we have space? */
    if(l->num_clients >= l->max_clients) {
        return -1;
    }

    /* If this is a challenge lobby, check to see what the max level of
       challenge mode the party can now access is. */
    if(l->challenge) {
        switch(c->version) {
            case CLIENT_VERSION_DCV2:
                for(i = 0; i < 9; ++i) {
                    if(c->pl->v2.c_rank.part.times[i] == 0) {
                        break;
                    }
                }

                break;

            case CLIENT_VERSION_PC:
                for(i = 0; i < 9; ++i) {
                    if(c->pl->pc.c_rank.part.times[i] == 0) {
                        break;
                    }
                }

                break;

            case CLIENT_VERSION_GC:
                /* XXXX: Handle ep2 stuff too. */
                for(i = 0; i < 9; ++i) {
                    if(c->pl->v3.c_rank.part.times[i] == 0) {
                        break;
                    }
                }

                break;

            default:
                /* We shouldn't get here... */
                return -1;
        }

        clev = (uint8_t)i + 1;
    }

    /* First client goes in slot 1, not 0 on DC/PC. Why Sega did this, who
       knows? */
    if(!l->num_clients && l->version < CLIENT_VERSION_GC &&
       l->type == LOBBY_TYPE_GAME) {
        l->clients[1] = c;
        c->cur_lobby = l;
        c->client_id = 1;
        c->arrow = 0;
        c->join_time = time(NULL);
        ++l->num_clients;

        /* Obviously if this is the only person in the lobby, the challenge
           level is that which they've unlocked.  */
        if(l->challenge) {
            l->max_chal = clev;
        }

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

            /* If this player is at a lower challenge level than the rest of the
               lobby, fix the maximum challenge level down to their level. */
            if(l->challenge && l->max_chal > clev) {
                l->max_chal = clev;
            }

            if(l->num_clients == 1) {
                l->leader_id = c->client_id;
            }

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
    if(l->challenge) {
        l->max_chal = lobby_find_max_challenge(l);
    }

    /* If this is the player's current lobby, fix that. */
    if(c->cur_lobby == l) {
        c->cur_lobby = NULL;
        c->client_id = 0;
    }

    return l->type == LOBBY_TYPE_DEFAULT ? 0 : !l->num_clients;
}

/* Add the client to any available lobby on the current block. */
int lobby_add_to_any(ship_client_t *c) {
    block_t *b = c->cur_block;
    lobby_t *l;
    int added = 0;

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
        if(lobby_add_to_any(c)) {
            return -11;
        }

        l = c->cur_lobby;

        if(send_lobby_join(c, l)) {
            return -11;
        }

        if(send_lobby_add_player(l, c)) {
            return -11;
        }

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
    if((req->flags & LOBBY_FLAG_QUESTING)) {
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
    }
    else {
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
        lobby_handle_done_burst(l);
    }

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
    int i;
    player_t *pl;
    time_t t;
    int h, m, s;
    int legit, questing;

    if(!l) {
        return send_info_reply(c, __(c, "\tEThis game is no\nlonger active."));
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

        sprintf(msg, "%s: %d:%02d:%02d\n"   /* Game time */
                "%s: %s\n"                  /* Legit/normal mode */
                "%s\n"                      /* Questing/Free adventure */
                "%s: %d-%d\n"               /* Levels allowed */
                "%s:\n",                    /* Versions allowed */
                __(c, "\tETime"), h, m, s,
                __(c, "Mode"), legit ? __(c, "Legit") : __(c, "Normal"),
                questing ? __(c, "Questing") : __(c, "Free Adventure"),
                __(c, "Level Range"), l->min_level, l->max_level,
                __(c, "Versions Allowed"));

        /* Figure out what versions are allowed. */
        if(l->version == CLIENT_VERSION_BB) {
            /* Blue Burst is easy... */
            sprintf(msg, "%s Blue Burst", msg);
        }
        else if(l->version == CLIENT_VERSION_GC) {
            /* Easy one here, GC games can only have GC chars */
            sprintf(msg, "%s GC", msg);
        }
        else if(l->version == CLIENT_VERSION_EP3) {
            /* Also easy, since Episode 3 games are completely different */
            sprintf(msg, "%s Episode 3", msg);
        }
        else {
            /* Slightly more interesting here... */
            if(l->v2) {
                if(!(l->flags & LOBBY_FLAG_PCONLY)) {
                    sprintf(msg, "%s DCv2", msg);
                }
                if(!(l->flags & LOBBY_FLAG_DCONLY)) {
                    sprintf(msg, "%s PC", msg);
                }
            }
            else {
                if(!(l->flags & LOBBY_FLAG_PCONLY)) {
                    sprintf(msg, "%s DCv1", msg);

                    if(!(l->flags & LOBBY_FLAG_V1ONLY)) {
                        sprintf(msg, "%s DCv2", msg);
                    }
                }
                if(!(l->flags & LOBBY_FLAG_DCONLY) &&
                   !(l->flags & LOBBY_FLAG_V1ONLY)) {
                    sprintf(msg, "%s PC", msg);
                }
            }

            if((l->flags & LOBBY_FLAG_GC_ALLOWED) &&
               !(l->flags & LOBBY_FLAG_DCONLY) &&
               !(l->flags & LOBBY_FLAG_PCONLY) &&
               !(l->flags & LOBBY_FLAG_V1ONLY)) {
                sprintf(msg, "%s GC", msg);
            }
        }

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

    pthread_rwlock_rdlock(&s->llock);

    /* If we don't have a legit mode set, then everyone's legit! */
    if(!s->limits || (!(l->flags & LOBBY_FLAG_LEGIT_MODE) &&
                      !(l->flags & LOBBY_FLAG_LEGIT_CHECK))) {
        pthread_rwlock_unlock(&s->llock);
        return 1;
    }

    /* Look through each item */
    for(j = 0; j < pl->v1.inv.item_count; ++j) {
        item = (sylverant_iitem_t *)&pl->v1.inv.items[j];
        irv = sylverant_limits_check_item(s->limits, item, v);

        if(!irv) {
            debug(DBG_LOG, "Potentially non-legit item in legit mode:\n"
                  "%08x %08x %08x %08x\n", LE32(item->data_l[0]), 
                  LE32(item->data_l[1]), LE32(item->data_l[2]),
                  LE32(item->data2_l));
            rv = irv;
        }
    }

    pthread_rwlock_unlock(&s->llock);

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

/* Finish with a legit check. */
void lobby_legit_check_finish_locked(lobby_t *l) {
    int i;

    /* If everyone passed, the game is now in legit mode. */
    if(l->legit_check_passed == l->num_clients) {
        l->flags |= LOBBY_FLAG_LEGIT_MODE;

        for(i = 0; i < l->max_clients; ++i) {
            if(l->clients[i]) {
                send_txt(l->clients[i], "%s",
                         __(l->clients[i], "\tE\tC7Legit mode active."));
            }
        }
    }
    else {
        send_txt(l->clients[l->leader_id], "%s",
                 __(l->clients[l->leader_id],
                    "\tE\tC7Team legit check failed."));
    }

    /* Since the legit check is done, clear the flag for that and the
       temporarily unavailable flag. */
    l->flags &= ~(LOBBY_FLAG_LEGIT_CHECK | LOBBY_FLAG_TEMP_UNAVAIL);
}

/* Send out any queued packets when we get a done burst signal. */
int lobby_handle_done_burst(lobby_t *l) {
    lobby_pkt_t *i;
    int rv = 0;

    pthread_mutex_lock(&l->mutex);

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

    pthread_mutex_unlock(&l->mutex);
    return rv;
}

/* Enqueue a packet for later sending (due to a player bursting) */
int lobby_enqueue_pkt(lobby_t *l, ship_client_t *c, dc_pkt_hdr_t *p) {
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
    STAILQ_INSERT_TAIL(&l->pkt_queue, pkt, qentry);

out:
    pthread_mutex_unlock(&l->mutex);
    return rv;
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
