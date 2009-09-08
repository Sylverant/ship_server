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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sylverant/mtwist.h>

#include "lobby.h"
#include "block.h"
#include "clients.h"
#include "ship_packets.h"

lobby_t *lobby_create_default(block_t *block, uint32_t lobby_id) {
    lobby_t *l = (lobby_t *)malloc(sizeof(lobby_t));

    /* If we don't have a lobby, bail. */
    if(!l) {
        perror("malloc");
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

    /* Fill in the name of the lobby. */
    sprintf(l->name, "BLOCK%02d-%02d", block->b, lobby_id);

    /* Initialize the lobby mutex. */
    pthread_mutex_init(&l->mutex, NULL);

    return l;
}

lobby_t *lobby_create_game(block_t *block, char name[16], char passwd[16],
                           uint8_t difficulty, uint8_t battle, uint8_t chal,
                           uint8_t v2, int version, uint8_t section,
                           uint8_t event) {
    lobby_t *l = (lobby_t *)malloc(sizeof(lobby_t));
    uint32_t id = 0x11;

    /* If we don't have a lobby, bail. */
    if(!l) {
        perror("malloc");
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
    l->max_clients = 4;
    l->block = block;

    l->difficulty = difficulty;
    l->battle = battle;
    l->challenge = chal;
    l->v2 = v2;
    l->version = (version == CLIENT_VERSION_DCV2 && !v2) ?
        CLIENT_VERSION_DCV1 : version;
    l->section = section;
    l->event = event;
    l->min_level = game_required_level[difficulty];
    l->max_level = 9001;                /* Its OVER 9000! */

    /* Copy the game name and password. */
    if(v2) {
        sprintf(l->name, "\tC6%s", name);
    }
    else {
        strcpy(l->name, name);
    }

    strcpy(l->passwd, passwd);

    /* Initialize the lobby mutex. */
    pthread_mutex_init(&l->mutex, NULL);

    /* XXXX: Fill in the map number array with appropriate random maps. I still
       need to work out exactly what those are on PSO... */

    /* Add it to the list of lobbies. */
    TAILQ_INSERT_TAIL(&block->lobbies, l, qentry);

    return l;
}

static void lobby_destroy_locked(lobby_t *l) {
    pthread_mutex_t m = l->mutex;

    TAILQ_REMOVE(&l->block->lobbies, l, qentry);
    free(l);

    pthread_mutex_unlock(&m);
    pthread_mutex_destroy(&m);
}

void lobby_destroy(lobby_t *l) {
    pthread_mutex_lock(&l->mutex);
    lobby_destroy_locked(l);
}

static int lobby_add_client_locked(ship_client_t *c, lobby_t *l) {
    int i;

    /* Sanity check: Do we have space? */
    if(l->num_clients >= l->max_clients) {
        return -1;
    }

    /* Find a place to put the client. */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] == NULL) {
            l->clients[i] = c;
            c->cur_lobby = l;
            c->client_id = i;
            c->arrow = 0;
            ++l->num_clients;
            return 0;
        }
    }

    /* If we get here, something went terribly wrong... */
    return -1;
}

static int lobby_elect_leader_locked(lobby_t *l) {
    int i;

    /* Go through and look for a new leader. */
    for(i = 0; i < l->max_clients; ++i) {
        /* We obviously can't give it to the old leader, they're gone now. */
        if(i == l->leader_id) {
            continue;
        }
        else if(l->clients[i]) {
            /* This person will do fine. */
            return i;
        }
    }

    return -1;
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
        new_leader = lobby_elect_leader_locked(l);

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

    /* If this is the player current lobby, fix that. */
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

        if(l->type & LOBBY_TYPE_DEFAULT && l->num_clients < l->max_clients) {
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

    /* Swap the data out on the server end before we do anything rash. */
    pthread_mutex_lock(&l->mutex);

    if(l != req) {
        pthread_mutex_lock(&req->mutex);
    }

    /* There is currently a client bursting */
    if((l->flags & LOBBY_FLAG_BURSTING)) {
        rv = -3;
        goto out;
    }

    /* Make sure the character is in the correct level range. */
    if(l->min_level > LE32(c->pl->level)) {
        /* Too low. */
        rv = -4;
        goto out;
    }

    if(l->max_level < LE32(c->pl->level)) {
        /* Too high. */
        rv = -5;
        goto out;
    }

    /* Make sure a V1 client isn't trying to join a V2 only lobby. */
    if(c->version == CLIENT_VERSION_DCV1 && req->v2) {
        rv = -6;
        goto out;
    }

    /* Attempt to add the client to the new lobby first. */
    if(lobby_add_client_locked(c, req)) {
        /* Nope... we can't do that, the lobby's probably full. */
        rv = -1;
        goto out;
    }

    /* The client is in the new lobby so we still need to remove them from the
       old lobby. */
    delete_lobby = lobby_remove_client_locked(c, old_cid, l);

    if(delete_lobby < 0) {
        /* Uhh... what do we do about this... */
        rv = -2;
        goto out;
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
    }

    /* ...and let his/her new lobby know that he/she has arrived. */
    send_lobby_add_player(c->cur_lobby, c);

    /* If the old lobby is empty (and not a default lobby), remove it. */
    if(delete_lobby) {
        lobby_destroy_locked(l);
    }

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
        lobby_destroy_locked(l);
    }

out:
    /* We're done, clean up. */
    if(delete_lobby < 1) {
        pthread_mutex_unlock(&l->mutex);
    }

    return rv;
}

int lobby_send_pkt_dc(lobby_t *l, ship_client_t *c, void *h) {
    dc_pkt_hdr_t *hdr = (dc_pkt_hdr_t *)h;
    int i;

    /* Send the packet to every connected client. */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] && l->clients[i] != c) {
            send_pkt_dc(l->clients[i], hdr);
        }
    }

    return 0;
}

static const char *classes[12] = {
    "HUmar", "HUnewearl", "HUcast",
    "RAmar", "RAcast", "RAcaseal",
    "FOmarl", "FOnewm", "FOnewearl",
    "HUcaseal", "FOmar", "RAmarl"
};

static const char language_codes[] = { 'J', 'E', 'G', 'F', 'S' };

/* Send an information reply packet with information about the lobby. */
int lobby_info_reply(ship_client_t *c, uint32_t lobby) {
    char msg[512] = { 0 };
    lobby_t *l = block_get_lobby(c->cur_block, lobby);
    int i;
    player_t *pl;

    if(!l) {
        return send_info_reply(c, "This game is no\nlonger active.");
    }

    /* Lock the lobby */
    pthread_mutex_lock(&l->mutex);

    /* Build up the information string */
    for(i = 0; i < l->max_clients; ++i) {
        /* Ignore blank clients */
        if(!l->clients[i]) {
            continue;
        }

        /* Grab the player data and fill in the string */
        pl = l->clients[i]->pl;

        sprintf(msg, "%s%s L%d\n  %s    %c\n", msg, pl->name, pl->level + 1,
                classes[pl->ch_class], language_codes[pl->inv.language]);
    }

    /* Unlock the lobby */
    pthread_mutex_unlock(&l->mutex);

    /* Send the reply */
    return send_info_reply(c, msg);
}
