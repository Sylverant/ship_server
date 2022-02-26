/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2012, 2016, 2017, 2018, 2019, 2020, 2021
                  2022 Lawrence Sebald

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
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <math.h>
#include <sys/socket.h>

#include <sylverant/encryption.h>
#include <sylverant/mtwist.h>
#include <sylverant/debug.h>
#include <sylverant/memory.h>

#include "ship.h"
#include "utils.h"
#include "clients.h"
#include "ship_packets.h"
#include "scripts.h"
#include "subcmd.h"
#include "mapdata.h"
#include "items.h"

#ifdef ENABLE_LUA
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#endif

#ifdef UNUSED
#undef UNUSED
#endif

#define UNUSED __attribute__((unused))

/* The key for accessing our thread-specific receive buffer. */
pthread_key_t recvbuf_key;

/* The key for accessing our thread-specific send buffer. */
pthread_key_t sendbuf_key;

/* Destructor for the thread-specific receive buffer */
static void buf_dtor(void *rb) {
    free(rb);
}

/* Initialize the clients system, allocating any thread specific keys */
int client_init(sylverant_ship_t *cfg) {
    if(pthread_key_create(&recvbuf_key, &buf_dtor)) {
        perror("pthread_key_create");
        return -1;
    }

    if(pthread_key_create(&sendbuf_key, &buf_dtor)) {
        perror("pthread_key_create");
        return -1;
    }

    return 0;
}

/* Clean up the clients system. */
void client_shutdown(void) {
    pthread_key_delete(recvbuf_key);
    pthread_key_delete(sendbuf_key);
}

/* Create a new connection, storing it in the list of clients. */
ship_client_t *client_create_connection(int sock, int version, int type,
                                        struct client_queue *clients,
                                        ship_t *ship, block_t *block,
                                        struct sockaddr *ip, socklen_t size) {
    ship_client_t *rv = (ship_client_t *)malloc(sizeof(ship_client_t));
    uint32_t client_seed_dc, server_seed_dc;
    uint8_t client_seed_bb[48], server_seed_bb[48];
    int i;
    pthread_mutexattr_t attr;
    struct mt19937_state *rng;

    if(!rv) {
        perror("malloc");
        return NULL;
    }

    memset(rv, 0, sizeof(ship_client_t));

    if(type == CLIENT_TYPE_BLOCK) {
        rv->pl = (player_t *)malloc(sizeof(player_t));

        if(!rv->pl) {
            perror("malloc");
            free(rv);
            close(sock);
            return NULL;
        }

        memset(rv->pl, 0, sizeof(player_t));

        if(!(rv->enemy_kills = (uint32_t *)malloc(sizeof(uint32_t) * 0x60))) {
            perror("malloc");
            free(rv->pl);
            free(rv);
            close(sock);
            return NULL;
        }

        if(version == CLIENT_VERSION_BB) {
            rv->bb_pl =
                (sylverant_bb_db_char_t *)malloc(sizeof(sylverant_bb_db_char_t));

            if(!rv->bb_pl) {
                perror("malloc");
                free(rv->pl);
                free(rv);
                close(sock);
                return NULL;
            }

            rv->bb_opts =
                (sylverant_bb_db_opts_t *)malloc(sizeof(sylverant_bb_db_opts_t));

            if(!rv->bb_opts) {
                perror("malloc");
                free(rv->bb_pl);
                free(rv->pl);
                free(rv);
                close(sock);
                return NULL;
            }

            memset(rv->bb_pl, 0, sizeof(sylverant_bb_db_char_t));
            memset(rv->bb_opts, 0, sizeof(sylverant_bb_db_opts_t));
        }
    }

    /* Store basic parameters in the client structure. */
    rv->sock = sock;
    rv->version = version;
    rv->cur_block = block;
    rv->arrow = 1;
    rv->last_message = rv->login_time = time(NULL);
    rv->hdr_size = 4;

    /* Create the mutex */
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&rv->mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    memcpy(&rv->ip_addr, ip, size);

    if(ip->sa_family == AF_INET6) {
        rv->flags |= CLIENT_FLAG_IPV6;
    }

    /* Make sure any packets sent early bail... */
    rv->ckey.type = 0xFF;
    rv->skey.type = 0xFF;

    if(type == CLIENT_TYPE_SHIP) {
        rv->flags |= CLIENT_FLAG_TYPE_SHIP;
        rng = &ship->rng;
    }
    else {
        rng = &block->rng;
    }

#ifdef ENABLE_LUA
    /* Initialize the script table */
    lua_newtable(ship->lstate);
    rv->script_ref = luaL_ref(ship->lstate, LUA_REGISTRYINDEX);
#endif

    switch(version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
            /* Generate the encryption keys for the client and server. */
            client_seed_dc = mt19937_genrand_int32(rng);
            server_seed_dc = mt19937_genrand_int32(rng);

            CRYPT_CreateKeys(&rv->skey, &server_seed_dc, CRYPT_PC);
            CRYPT_CreateKeys(&rv->ckey, &client_seed_dc, CRYPT_PC);

            /* Send the client the welcome packet, or die trying. */
            if(send_dc_welcome(rv, server_seed_dc, client_seed_dc)) {
                goto err;
            }

            break;

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
        case CLIENT_VERSION_XBOX:
            /* Generate the encryption keys for the client and server. */
            client_seed_dc = mt19937_genrand_int32(rng);
            server_seed_dc = mt19937_genrand_int32(rng);

            CRYPT_CreateKeys(&rv->skey, &server_seed_dc, CRYPT_GAMECUBE);
            CRYPT_CreateKeys(&rv->ckey, &client_seed_dc, CRYPT_GAMECUBE);

            /* Send the client the welcome packet, or die trying. */
            if(send_dc_welcome(rv, server_seed_dc, client_seed_dc)) {
                goto err;
            }

            break;

        case CLIENT_VERSION_BB:
            /* Generate the encryption keys for the client and server. */
            for(i = 0; i < 48; i += 4) {
                client_seed_dc = mt19937_genrand_int32(rng);
                server_seed_dc = mt19937_genrand_int32(rng);

                client_seed_bb[i + 0] = (uint8_t)(client_seed_dc >>  0);
                client_seed_bb[i + 1] = (uint8_t)(client_seed_dc >>  8);
                client_seed_bb[i + 2] = (uint8_t)(client_seed_dc >> 16);
                client_seed_bb[i + 3] = (uint8_t)(client_seed_dc >> 24);
                server_seed_bb[i + 0] = (uint8_t)(server_seed_dc >>  0);
                server_seed_bb[i + 1] = (uint8_t)(server_seed_dc >>  8);
                server_seed_bb[i + 2] = (uint8_t)(server_seed_dc >> 16);
                server_seed_bb[i + 3] = (uint8_t)(server_seed_dc >> 24);
            }

            CRYPT_CreateKeys(&rv->skey, server_seed_bb, CRYPT_BLUEBURST);
            CRYPT_CreateKeys(&rv->ckey, client_seed_bb, CRYPT_BLUEBURST);
            rv->hdr_size = 8;

            /* Send the client the welcome packet, or die trying. */
            if(send_bb_welcome(rv, server_seed_bb, client_seed_bb)) {
                goto err;
            }

            break;
    }

    /* Insert it at the end of our list, and we're done. */
    if(type == CLIENT_TYPE_BLOCK) {
        pthread_rwlock_wrlock(&block->lock);
        TAILQ_INSERT_TAIL(clients, rv, qentry);
        ++block->num_clients;
        pthread_rwlock_unlock(&block->lock);
    }
    else {
        TAILQ_INSERT_TAIL(clients, rv, qentry);
    }

    ship_inc_clients(ship);

    return rv;

err:
#ifdef ENABLE_LUA
    /* Remove the table from the registry */
    luaL_unref(ship->lstate, LUA_REGISTRYINDEX, rv->script_ref);
#endif

    close(sock);

    if(type == CLIENT_TYPE_BLOCK) {
        free(rv->enemy_kills);
        free(rv->pl);
    }

    pthread_mutex_destroy(&rv->mutex);

    free(rv);
    return NULL;
}

/* Destroy a connection, closing the socket and removing it from the list. This
   must always be called with the appropriate lock held for the list! */
void client_destroy_connection(ship_client_t *c,
                               struct client_queue *clients) {
    time_t now = time(NULL);
    char tstr[26];
    script_action_t action = ScriptActionClientShipLogout;

    if(!(c->flags & CLIENT_FLAG_TYPE_SHIP))
        action = ScriptActionClientBlockLogout;

    TAILQ_REMOVE(clients, c, qentry);

    /* If the client was on Blue Burst, update their db character */
    if(c->version == CLIENT_VERSION_BB &&
       !(c->flags & CLIENT_FLAG_TYPE_SHIP)) {
        c->bb_pl->character.play_time += now - c->login_time;
        shipgate_send_cdata(&ship->sg, c->guildcard, c->sec_data.slot,
                            c->bb_pl, sizeof(sylverant_bb_db_char_t),
                            c->cur_block->b);
        shipgate_send_bb_opts(&ship->sg, c);
    }

    script_execute(action, c, SCRIPT_ARG_PTR, c, 0);

#ifdef ENABLE_LUA
    /* Remove the table from the registry */
    luaL_unref(ship->lstate, LUA_REGISTRYINDEX, c->script_ref);
#endif

    /* If the user was on a block, notify the shipgate */
    if(c->version != CLIENT_VERSION_BB && c->pl && c->pl->v1.name[0]) {
        shipgate_send_block_login(&ship->sg, 0, c->guildcard,
                                  c->cur_block->b, c->pl->v1.name);
    }
    else if(c->version == CLIENT_VERSION_BB && c->bb_pl) {
        uint16_t bbname[17];

        memcpy(bbname, c->bb_pl->character.name, 16);
        bbname[16] = 0;
        shipgate_send_block_login_bb(&ship->sg, 0, c->guildcard,
                                     c->cur_block->b, bbname);
    }

    ship_dec_clients(ship);

    /* If the client has a lobby sitting around that was created but not added
       to the list of lobbies, destroy it */
    if(c->create_lobby) {
        lobby_destroy_noremove(c->create_lobby);
    }

    /* If we were logging the user, close the file */
    if(c->logfile) {
        ctime_r(&now, tstr);
        tstr[strlen(tstr) - 1] = 0;
        fprintf(c->logfile, "[%s] Connection closed\n", tstr);
        fclose(c->logfile);
    }

    if(c->sock >= 0) {
        close(c->sock);
    }

    if(c->limits)
        release(c->limits);

    if(c->recvbuf) {
        free(c->recvbuf);
    }

    if(c->sendbuf) {
        free(c->sendbuf);
    }

    if(c->autoreply) {
        free(c->autoreply);
    }

    if(c->enemy_kills) {
        free(c->enemy_kills);
    }

    if(c->pl) {
        free(c->pl);
    }

    if(c->bb_pl) {
        free(c->bb_pl);
    }

    if(c->bb_opts) {
        free(c->bb_opts);
    }

    if(c->next_maps) {
        free(c->next_maps);
    }

    pthread_mutex_destroy(&c->mutex);

    free(c);
}

/* Read data from a client that is connected to any port. */
int client_process_pkt(ship_client_t *c) {
    ssize_t sz;
    uint16_t pkt_sz;
    int rv = 0;
    unsigned char *rbp;
    void *tmp;
    uint8_t *recvbuf = get_recvbuf();
    int hsz = c->hdr_size;

    /* Make sure we got the recvbuf, otherwise, bail. */
    if(!recvbuf) {
        return -1;
    }

    /* If we've got anything buffered, copy it out to the main buffer to make
       the rest of this a bit easier. */
    if(c->recvbuf_cur) {
        memcpy(recvbuf, c->recvbuf, c->recvbuf_cur);
    }

    /* Attempt to read, and if we don't get anything, punt. */
    if((sz = recv(c->sock, recvbuf + c->recvbuf_cur, 65536 - c->recvbuf_cur,
                  0)) <= 0) {
        if(sz == -1) {
            perror("recv");
        }

        return -1;
    }

    sz += c->recvbuf_cur;
    c->recvbuf_cur = 0;
    rbp = recvbuf;

    /* As long as what we have is long enough, decrypt it. */
    while(sz >= hsz && rv == 0) {
        /* Decrypt the packet header so we know what exactly we're looking
           for, in terms of packet length. */
        if(!(c->flags & CLIENT_FLAG_HDR_READ)) {
            memcpy(&c->pkt, rbp, hsz);
            CRYPT_CryptData(&c->ckey, &c->pkt, hsz, 0);
            c->flags |= CLIENT_FLAG_HDR_READ;
        }

        /* Read the packet size to see how much we're expecting. */
        switch(c->version) {
            case CLIENT_VERSION_DCV1:
            case CLIENT_VERSION_DCV2:
            case CLIENT_VERSION_GC:
            case CLIENT_VERSION_EP3:
            case CLIENT_VERSION_XBOX:
                pkt_sz = LE16(c->pkt.dc.pkt_len);
                break;

            case CLIENT_VERSION_PC:
                pkt_sz = LE16(c->pkt.pc.pkt_len);
                break;

            case CLIENT_VERSION_BB:
                pkt_sz = LE16(c->pkt.bb.pkt_len);
                break;

            default:
                return -1;
        }

        /* A packet with a size less than that of it's header is obviously bad.
           Quite possibly, malicious. Boot the user and log it. */
        if(pkt_sz < hsz) {
            if(c->guildcard)
                debug(DBG_WARN, "User %" PRIu32 " sent packet with invalid "
                      "length!\n", c->guildcard);
            else
                debug(DBG_WARN, "Got invalid length packet from unknown "
                      "client.\n");

            return -1;
        }

        /* We'll always need a multiple of 8 or 4 (depending on the type of
           the client) bytes. */
        if(pkt_sz & (hsz - 1)) {
            pkt_sz = (pkt_sz & (0x10000 - hsz)) + hsz;
        }

        /* Do we have the whole packet? */
        if(sz >= (ssize_t)pkt_sz) {
            /* Yes, we do, decrypt it. */
            CRYPT_CryptData(&c->ckey, rbp + hsz, pkt_sz - hsz, 0);
            memcpy(rbp, &c->pkt, hsz);
            c->last_message = time(NULL);

            /* If we're logging the client, write into the log */
            if(c->logfile) {
                fprint_packet(c->logfile, rbp, pkt_sz, 1);
            }

            /* Pass it onto the correct handler. */
            if(c->flags & CLIENT_FLAG_TYPE_SHIP) {
                rv = ship_process_pkt(c, rbp);
            }
            else {
                rv = block_process_pkt(c, rbp);
            }

            rbp += pkt_sz;
            sz -= pkt_sz;

            c->flags &= ~CLIENT_FLAG_HDR_READ;
        }
        else {
            /* Nope, we're missing part, break out of the loop, and buffer
               the remaining data. */
            break;
        }
    }

    /* If we've still got something left here, buffer it for the next pass. */
    if(sz && rv == 0) {
        /* Reallocate the recvbuf for the client if its too small. */
        if(c->recvbuf_size < sz) {
            tmp = realloc(c->recvbuf, sz);

            if(!tmp) {
                perror("realloc");
                return -1;
            }

            c->recvbuf = (unsigned char *)tmp;
            c->recvbuf_size = sz;
        }

        memcpy(c->recvbuf, rbp, sz);
        c->recvbuf_cur = sz;
    }
    else if(c->recvbuf) {
        /* Free the buffer, if we've got nothing in it. */
        free(c->recvbuf);
        c->recvbuf = NULL;
        c->recvbuf_size = 0;
    }

    return rv;
}

/* Retrieve the thread-specific recvbuf for the current thread. */
uint8_t *get_recvbuf(void) {
    uint8_t *recvbuf = (uint8_t *)pthread_getspecific(recvbuf_key);

    /* If we haven't initialized the recvbuf pointer yet for this thread, then
       we need to do that now. */
    if(!recvbuf) {
        recvbuf = (uint8_t *)malloc(65536);

        if(!recvbuf) {
            perror("malloc");
            return NULL;
        }

        if(pthread_setspecific(recvbuf_key, recvbuf)) {
            perror("pthread_setspecific");
            free(recvbuf);
            return NULL;
        }
    }

    return recvbuf;
}

/* Set up a simple mail autoreply. */
int client_set_autoreply(ship_client_t *c, void *buf, uint16_t len) {
    char *tmp;

    /* Make space for the new autoreply and copy it in. */
    if(!(tmp = malloc(len))) {
        debug(DBG_ERROR, "Cannot allocate memory for autoreply (%" PRIu32
              "):\n%s\n", c->guildcard, strerror(errno));
        return -1;
    }

    memcpy(tmp, buf, len);

    switch(c->version) {
        case CLIENT_VERSION_PC:
            c->pl->pc.autoreply_enabled = LE32(1);
            c->autoreply_on = 1;
            break;

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
        case CLIENT_VERSION_XBOX:
            c->pl->v3.autoreply_enabled = LE32(1);
            c->autoreply_on = 1;
            break;

        case CLIENT_VERSION_BB:
            c->pl->bb.autoreply_enabled = LE32(1);
            c->autoreply_on = 1;
            memcpy(c->bb_pl->autoreply, buf, len);
            memset(((uint8_t *)c->bb_pl->autoreply) + len, 0, 0x158 - len);
            break;
    }

    /* Clean up and set the new autoreply in place */
    free(c->autoreply);
    c->autoreply = tmp;
    c->autoreply_len = (int)len;

    return 0;
}

/* Disable the user's simple mail autoreply (if set). */
int client_disable_autoreply(ship_client_t *c) {
    switch(c->version) {
        case CLIENT_VERSION_PC:
            c->pl->pc.autoreply_enabled = 0;
            c->autoreply_on = 0;
            break;

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
        case CLIENT_VERSION_XBOX:
            c->pl->v3.autoreply_enabled = 0;
            c->autoreply_on = 0;
            break;

        case CLIENT_VERSION_BB:
            c->pl->bb.autoreply_enabled = 0;
            c->autoreply_on = 0;
            break;
    }

    return 0;
}

/* Check if a client has blacklisted someone. */
int client_has_blacklisted(ship_client_t *c, uint32_t gc) {
    uint32_t rgc = LE32(gc);
    int i;

    /* If the user doesn't have a blacklist, this is easy. */
    if(c->version < CLIENT_VERSION_PC)
        return 0;

    /* Look through each blacklist entry. */
    for(i = 0; i < 30; ++i) {
        if(c->blacklist[i] == rgc) {
            return 1;
        }
    }

    /* If we didn't find anything, then we're done. */
    return 0;
}

/* Check if a client has /ignore'd someone. */
int client_has_ignored(ship_client_t *c, uint32_t gc) {
    int i;

    /* Look through the ignore list... */
    for(i = 0; i < CLIENT_IGNORE_LIST_SIZE; ++i) {
        if(c->ignore_list[i] == gc) {
            return 1;
        }
    }

    /* We didn't find the person, so they're not being /ignore'd. */
    return 0;
}

/* Send a message to a client telling them that a friend has logged on/off */
void client_send_friendmsg(ship_client_t *c, int on, const char *fname,
                           const char *ship, uint32_t block, const char *nick) {
    if(fname[0] != '\t') {
        send_txt(c, "%s%s %s\n%s %s\n%s BLOCK%02d",
                 on ? __(c, "\tE\tC2") : __(c, "\tE\tC4"), nick,
                 on ? __(c, "online") : __(c, "offline"), __(c, "Character:"),
                 fname, ship, (int)block);
    }
    else {
        send_txt(c, "%s%s %s\n%s %s\n%s BLOCK%02d",
                 on ? __(c, "\tE\tC2") : __(c, "\tE\tC4"), nick,
                 on ? __(c, "online") : __(c, "offline"), __(c, "Character:"),
                 fname + 2, ship, (int)block);
    }
}

static void give_stats(ship_client_t *c, bb_level_entry_t *ent) {
    uint16_t tmp;

    tmp = LE16(c->bb_pl->character.atp) + ent->atp;
    c->bb_pl->character.atp = LE16(tmp);

    tmp = LE16(c->bb_pl->character.mst) + ent->mst;
    c->bb_pl->character.mst = LE16(tmp);

    tmp = LE16(c->bb_pl->character.evp) + ent->evp;
    c->bb_pl->character.evp = LE16(tmp);

    tmp = LE16(c->bb_pl->character.hp) + ent->hp;
    c->bb_pl->character.hp = LE16(tmp);

    tmp = LE16(c->bb_pl->character.dfp) + ent->dfp;
    c->bb_pl->character.dfp = LE16(tmp);

    tmp = LE16(c->bb_pl->character.ata) + ent->ata;
    c->bb_pl->character.ata = LE16(tmp);
}

/* Give a Blue Burst client some experience. */
int client_give_exp(ship_client_t *c, uint32_t exp) {
    uint32_t exp_total;
    bb_level_entry_t *ent;
    int need_lvlup = 0;
    int cl;
    uint32_t level;

    if(c->version != CLIENT_VERSION_BB || !c->bb_pl)
        return -1;

    /* No need if they've already maxed out. */
    if(c->bb_pl->character.level >= 199)
        return 0;

    /* Add in the experience to their total so far. */
    exp_total = LE32(c->bb_pl->character.exp);
    exp_total += exp;
    c->bb_pl->character.exp = LE32(exp_total);
    cl = c->bb_pl->character.ch_class;
    level = LE32(c->bb_pl->character.level);

    /* Send the packet telling them they've gotten experience. */
    if(subcmd_send_bb_exp(c, exp))
        return -1;

    /* See if they got any level ups. */
    do {
        ent = &char_stats.levels[cl][level + 1];

        if(exp_total >= ent->exp) {
            need_lvlup = 1;
            give_stats(c, ent);
            ++level;
        }
    } while(exp_total >= ent->exp && level < 199);

    /* If they got any level ups, send out the packet that says so. */
    if(need_lvlup) {
        c->bb_pl->character.level = LE32(level);
        if(subcmd_send_bb_level(c))
            return -1;
    }

    return 0;
}

/* Give a Blue Burst client some free level ups. */
int client_give_level(ship_client_t *c, uint32_t level_req) {
    uint32_t exp_total;
    bb_level_entry_t *ent;
    int cl;
    uint32_t exp_gained;

    if(c->version != CLIENT_VERSION_BB || !c->bb_pl || level_req > 199)
        return -1;

    /* No need if they've already at that level. */
    if(c->bb_pl->character.level >= level_req)
        return 0;

    /* Grab the entry for that level... */
    cl = c->bb_pl->character.ch_class;
    ent = &char_stats.levels[cl][level_req];

    /* Add in the experience to their total so far. */
    exp_total = LE32(c->bb_pl->character.exp);
    exp_gained = ent->exp - exp_total;
    c->bb_pl->character.exp = LE32(ent->exp);

    /* Send the packet telling them they've gotten experience. */
    if(subcmd_send_bb_exp(c, exp_gained))
        return -1;

    /* Send the level-up packet. */
    c->bb_pl->character.level = LE32(level_req);
    if(subcmd_send_bb_level(c))
        return -1;

    return 0;
}

static void give_stats_v2(ship_client_t *c, level_entry_t *ent) {
    uint16_t tmp;

    tmp = LE16(c->pl->v1.atp) + ent->atp;
    c->pl->v1.atp = LE16(tmp);

    tmp = LE16(c->pl->v1.mst) + ent->mst;
    c->pl->v1.mst = LE16(tmp);

    tmp = LE16(c->pl->v1.evp) + ent->evp;
    c->pl->v1.evp = LE16(tmp);

    tmp = LE16(c->pl->v1.hp) + ent->hp;
    c->pl->v1.hp = LE16(tmp);

    tmp = LE16(c->pl->v1.dfp) + ent->dfp;
    c->pl->v1.dfp = LE16(tmp);

    tmp = LE16(c->pl->v1.ata) + ent->ata;
    c->pl->v1.ata = LE16(tmp);

    c->pl->v1.exp = LE32(ent->exp);
}

/* Give a PSOv2 client some free level ups. */
int client_give_level_v2(ship_client_t *c, uint32_t level_req) {
    level_entry_t *ent;
    int cl, i;

    if(c->version != CLIENT_VERSION_DCV2 && c->version != CLIENT_VERSION_PC)
        return -1;

    if(!c->pl || level_req > 199)
        return -1;

    /* No need if they've already at that level. */
    if(c->pl->v1.level >= level_req)
        return 0;

    /* Give all the stat boosts for the intervening levels... */
    cl = c->pl->v1.ch_class;

    for(i = c->pl->v1.level + 1; i <= level_req; ++i) {
        ent = &v2_char_stats.levels[cl][i];
        give_stats_v2(c, ent);
    }

    c->pl->v1.level = LE32(level_req);

    /* Reload them into the lobby. */
    send_lobby_join(c, c->cur_lobby);

    return 0;
}

static int check_char_v1(ship_client_t *c, player_t *pl) {
    bitfloat_t f1, f2;

    /* Check some stuff that shouldn't ever change first... For these ones,
       we don't have to worry about byte ordering. */
    if(c->pl->v1.model != pl->v1.model)
        return -10;

    if(c->pl->v1.section != pl->v1.section)
        return -11;

    if(c->pl->v1.ch_class != pl->v1.ch_class)
        return -12;

    if(c->pl->v1.costume != pl->v1.costume)
        return -13;

    if(c->pl->v1.skin != pl->v1.skin)
        return -14;

    if(c->pl->v1.face != pl->v1.face)
        return -15;

    if(c->pl->v1.head != pl->v1.head)
        return -16;

    if(c->pl->v1.hair != pl->v1.hair)
        return -17;

    if(c->pl->v1.hair_r != pl->v1.hair_r)
        return -18;

    if(c->pl->v1.hair_g != pl->v1.hair_g)
        return -19;

    if(c->pl->v1.hair_b != pl->v1.hair_b)
        return -20;

    /* Floating point stuff... Ugh. Pay careful attention to these, just in case
       they're some special value like NaN or Inf (potentially because of byte
       ordering or whatnot). */
    f1.f = c->pl->v1.prop_x;
    f2.f = pl->v1.prop_x;
    if(f1.b != f2.b)
        return -21;

    f1.f = c->pl->v1.prop_y;
    f2.f = pl->v1.prop_y;
    if(f1.b != f2.b)
        return -22;

    if(memcmp(c->pl->v1.name, pl->v1.name, 16))
        return -23;

    /* Now make sure that nothing has decreased that should never decrease.
       Since these aren't equality comparisons, we have to deal with byte
       ordering here... The hp/tp materials count are 8-bits each, but
       everything else is multi-byte. */
    if(c->pl->v1.inv.hpmats_used > pl->v1.inv.hpmats_used)
        return -24;

    if(c->pl->v1.inv.tpmats_used > pl->v1.inv.tpmats_used)
        return -25;

    if(LE32(c->pl->v1.exp) > LE32(pl->v1.exp))
        return -26;

    /* Why is the level 32-bits?... */
    if(LE32(c->pl->v1.level) > LE32(pl->v1.level))
        return -27;

    /* Other stats omitted for now... */

    /* If we get here, we've passed all the checks... */
    return 0;
}

static int check_char_v2(ship_client_t *c, player_t *pl) {
    return check_char_v1(c, pl);
}

static int check_char_pc(ship_client_t *c, player_t *pl) {
    return check_char_v1(c, pl);
}

static int check_char_gc(ship_client_t *c, player_t *pl) {
    return 0;
}

static int check_char_xbox(ship_client_t *c, player_t *pl) {
    return 0;
}

int client_check_character(ship_client_t *c, player_t *pl, uint8_t ver) {
    switch(ver) {
        case 1:
            if(c->version == CLIENT_VERSION_DCV1) {
                if((c->flags & CLIENT_FLAG_IS_NTE))
                    /* XXXX */
                    return 0;
                else
                    return check_char_v1(c, pl);
            }
            else
                /* This shouldn't happen... */
                return -1;

        case 2:
            if(c->version == CLIENT_VERSION_DCV2)
                return check_char_v2(c, pl);
            else if(c->version == CLIENT_VERSION_PC) {
                if(!(c->flags & CLIENT_FLAG_IS_NTE))
                    return check_char_pc(c, pl);
                else
                    /* XXXX */
                    return 0;
            }
            else
                /* This shouldn't happen... */
                return -1;

        case 3:
            if(c->version == CLIENT_VERSION_GC)
                return check_char_gc(c, pl);
            else if(c->version == CLIENT_VERSION_XBOX)
                return check_char_xbox(c, pl);
            else if(c->version == CLIENT_VERSION_EP3)
                /* XXXX */
                return 0;
            else
                /* This shouldn't happen... */
                return -1;

        case 4:
            if(c->version == CLIENT_VERSION_BB)
                /* XXXX */
                return 0;
            else
                /* This shouldn't happen... */
                return -1;

        default:
            debug(DBG_WARN, "client_check_character: Unknown version %d\n",
                  (int)ver);
    }

    /* XXXX */
    return 0;
}

int client_legit_check(ship_client_t *c, sylverant_limits_t *limits) {
    uint32_t v;
    sylverant_iitem_t *item;
    int j, irv;

    /* Figure out what version they're on. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
            v = ITEM_VERSION_V1;
            break;

        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
            v = ITEM_VERSION_V2;
            break;

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_XBOX: /* XXXX */
            v = ITEM_VERSION_GC;
            break;

        case CLIENT_VERSION_EP3:
        case CLIENT_VERSION_BB:
        default:
            /* XXXX */
            return 0;
    }

    /* Make sure the player qualifies for legit mode... */
    for(j = 0; j < c->pl->v1.inv.item_count; ++j) {
        item = (sylverant_iitem_t *)&c->pl->v1.inv.items[j];
        irv = sylverant_limits_check_item(limits, item, v);

        if(!irv) {
            debug(DBG_LOG, "Potentially non-legit found in inventory (GC: %"
                  PRIu32"):\n%08x %08x %08x %08x\n", c->guildcard,
                  LE32(item->data_l[0]), LE32(item->data_l[1]),
                  LE32(item->data_l[2]), LE32(item->data2_l));
            return -1;
        }
    }

    return 0;
}

#ifdef ENABLE_LUA

static int client_guildcard_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, c->guildcard);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int client_isOnBlock_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        lua_pushboolean(l, !(c->flags & CLIENT_FLAG_TYPE_SHIP));
    }
    else {
        lua_pushboolean(l, 0);
    }

    return 1;
}

static int client_disconnect_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        c->flags |= CLIENT_FLAG_DISCONNECTED;
    }

    return 0;
}

static int client_addr_lua(lua_State *l) {
    ship_client_t *c;
    char str[INET6_ADDRSTRLEN];

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        my_ntop(&c->ip_addr, str);
        lua_pushstring(l, str);
    }
    else {
        lua_pushliteral(l, "");
    }

    return 1;
}

static int client_version_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, c->version);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int client_clientID_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, c->client_id);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int client_privilege_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, c->privilege);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int client_send_lua(lua_State *l) {
    ship_client_t *c;
    const uint8_t *s;
    size_t len;
    uint16_t len2;

    if(lua_islightuserdata(l, 1) && lua_isstring(l, 2)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        s = (const uint8_t *)lua_tolstring(l, 2, &len);

        if(!s) {
            lua_pushinteger(l, -1);
            return 1;
        }

        /* Check it for sanity. */
        if(len < 4 || (len & 0x03)) {
            lua_pushinteger(l, -1);
            return 1;
        }

        len2 = s[2] | (s[3] << 8);
        if(len2 != len) {
            lua_pushinteger(l, -1);
            return 1;
        }

        if(send_pkt_dc(c, (const dc_pkt_hdr_t *)s)) {
            lua_pushinteger(l, -1);
            return 1;
        }

        lua_pushinteger(l, 0);
        return 1;
    }
    else {
        lua_pushinteger(l, -1);
        return 1;
    }
}

static int client_lobby_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);

        if(c->cur_lobby)
            lua_pushlightuserdata(l, c->cur_lobby);
        else
            lua_pushnil(l);
    }
    else {
        lua_pushnil(l);
    }

    return 1;
}

static int client_block_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);

        if(c->cur_block)
            lua_pushlightuserdata(l, c->cur_block);
        else
            lua_pushnil(l);
    }
    else {
        lua_pushnil(l);
    }

    return 1;
}

static int client_sendSData_lua(lua_State *l) {
    ship_client_t *c;
    uint32_t event;
    const uint8_t *s;
    size_t len;
    lua_Integer rv = -1;

    if(lua_islightuserdata(l, 1) && lua_isinteger(l, 2) && lua_isstring(l, 3)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        event = (uint32_t)lua_tointeger(l, 2);
        s = (const uint8_t *)lua_tolstring(l, 3, &len);

        rv = shipgate_send_sdata(&ship->sg, c, event, s, (uint32_t)len);
    }

    lua_pushinteger(l, rv);
    return 1;
}

static int client_sendMsg_lua(lua_State *l) {
    ship_client_t *c;
    const char *s;
    size_t len;

    if(lua_islightuserdata(l, 1) && lua_isstring(l, 2)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        s = (const char *)lua_tolstring(l, 2, &len);

        send_txt(c, "\tE\tC7%s", s);
    }

    lua_pushinteger(l, 0);
    return 1;
}

static int client_getTable_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        lua_rawgeti(l, LUA_REGISTRYINDEX, c->script_ref);
    }
    else {
        lua_pushnil(l);
    }

    return 1;
}

static int client_area_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, c->cur_area);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int client_name_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);

        if(c->pl)
            lua_pushstring(l, c->pl->v1.name);
        else
            lua_pushnil(l);
    }
    else {
        lua_pushnil(l);
    }

    return 1;
}

static int client_flags_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, c->flags);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int client_level_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);

        if(c->pl)
            lua_pushinteger(l, c->pl->v1.level + 1);
        else
            lua_pushinteger(l, -1);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int client_sendMenu_lua(lua_State *l) {
    ship_client_t *c;
    gen_menu_entry_t *ents;
    lua_Integer count, i;
    int tp;
    const char *str;
    uint32_t menu_id;

    if(lua_islightuserdata(l, 1) && lua_isinteger(l, 2) &&
       lua_isinteger(l, 3) && lua_istable(l, 4) && lua_istable(l, 5)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        menu_id = (uint32_t)lua_tointeger(l, 2);
        count = lua_tointeger(l, 3);

        /* Make sure we've got a sane count for the menu. */
        if(count <= 0) {
            lua_pushinteger(l, -1);
            return 1;
        }

        ents = (gen_menu_entry_t *)malloc(sizeof(gen_menu_entry_t) * count);
        if(!ents) {
            lua_pushinteger(l, -1);
            return 1;
        }

        /* Read each element from the tables passed in */
        for(i = 1; i <= count; ++i) {
            tp = lua_rawgeti(l, 4, i);
            if(tp != LUA_TNUMBER) {
                lua_pop(l, 1);
                free(ents);
                lua_pushinteger(l, -1);
                return 1;
            }

            ents[i - 1].item_id = (uint32_t)lua_tointeger(l, -1);
            lua_pop(l, 1);

            tp = lua_rawgeti(l, 5, i);
            if(tp != LUA_TSTRING) {
                lua_pop(l, 1);
                free(ents);
                lua_pushinteger(l, -1);
                return 1;
            }

            str = lua_tostring(l, -1);
            strncpy(ents[i - 1].text, str, 15);
            ents[i - 1].text[15] = 0;
            lua_pop(l, 1);
        }

        send_generic_menu(c, menu_id, (size_t)count, ents);
        lua_pushinteger(l, 0);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int client_dropItem_lua(lua_State *l) {
    ship_client_t *c;
    lobby_t *lb;
    uint32_t item[4] = { 0, 0, 0, 0 };
    subcmd_drop_stack_t p2;

    /* We need at least the client itself and the first dword of the item */
    if(lua_islightuserdata(l, 1) && lua_isinteger(l, 2)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        lb = c->cur_lobby;
        item[0] = (uint32_t)lua_tointeger(l, 2);

        /* Make sure we're in a team, not a regular lobby... */
        if(lb->type != LOBBY_TYPE_GAME) {
            lua_pushinteger(l, -1);
            return 1;
        }

        /* Check all the optional arguments */
        if(lua_isinteger(l, 3)) {
            item[1] = (uint32_t)lua_tointeger(l, 3);

            if(lua_isinteger(l, 4)) {
                item[2] = (uint32_t)lua_tointeger(l, 4);

                if(lua_isinteger(l, 5)) {
                    item[3] = (uint32_t)lua_tointeger(l, 5);
                }
            }
        }

        /* Do some basic checks of the item... */
        if(item_is_stackable(item[0]) && !(item[1] & 0x0000FF00)) {
            /* If the item is stackable and doesn't have a quantity, give one
               of it. */
            item[1] |= (1 << 8);
        }

        /* Generate the packet to drop the item */
        p2.hdr.pkt_type = GAME_COMMAND0_TYPE;
        p2.hdr.pkt_len = sizeof(subcmd_drop_stack_t);
        p2.hdr.flags = 0;
        p2.type = SUBCMD_DROP_STACK;
        p2.size = 0x0A;
        p2.client_id = c->client_id;
        p2.unused = 0;
        p2.area = LE16(c->cur_area);
        p2.unk = LE16(0);
        p2.x = c->x;
        p2.z = c->z;
        p2.item[0] = LE32(item[0]);
        p2.item[1] = LE32(item[1]);
        p2.item[2] = LE32(item[2]);
        p2.item_id = LE32(lb->item_id);
        p2.item2 = LE32(item[3]);
        p2.two = LE32(0x00000002);
        ++lb->item_id;

        lobby_send_pkt_dc(lb, NULL, (dc_pkt_hdr_t *)&p2, 0);
        lua_pushinteger(l, 0);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int client_sendMsgBox_lua(lua_State *l) {
    ship_client_t *c;
    const char *s;
    size_t len;

    if(lua_islightuserdata(l, 1) && lua_isstring(l, 2)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        s = (const char *)lua_tolstring(l, 2, &len);

        send_message1(c, "\tE%s", s);
        lua_pushinteger(l, 0);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int client_numItems_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);

        /* Make sure we have character data first. */
        if(!c->pl) {
            lua_pushinteger(l, -1);
            return 1;
        }

        lua_pushinteger(l, c->pl->v1.inv.item_count);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int client_item_lua(lua_State *l) {
    ship_client_t *c;
    int index;

    if(lua_islightuserdata(l, 1) && lua_isinteger(l, 2)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        index = lua_tointeger(l, 2);

        /* Make sure we have character data first. */
        if(!c->pl) {
            lua_pushnil(l);
            return 1;
        }

        /* Make sure the index is sane */
        if(index < 0 || index >= c->pl->v1.inv.item_count) {
            lua_pushnil(l);
            return 1;
        }

        /* Create a table and put all 4 dwords of item data in it. */
        lua_newtable(l);
        lua_pushinteger(l, 1);
        lua_pushinteger(l, c->pl->v1.inv.items[index].data_l[0]);
        lua_settable(l, -3);
        lua_pushinteger(l, 2);
        lua_pushinteger(l, c->pl->v1.inv.items[index].data_l[1]);
        lua_settable(l, -3);
        lua_pushinteger(l, 2);
        lua_pushinteger(l, c->pl->v1.inv.items[index].data_l[2]);
        lua_settable(l, -3);
        lua_pushinteger(l, 2);
        lua_pushinteger(l, c->pl->v1.inv.items[index].data2_l);
        lua_settable(l, -3);
    }
    else {
        lua_pushnil(l);
    }

    return 1;
}

static int client_find_lua(lua_State *l) {
    ship_client_t *c;
    uint32_t gc;
    int i;

    if(lua_isinteger(l, 1)) {
        gc = (uint32_t)lua_tointeger(l, 1);

        for(i = 0; i < ship->cfg->blocks; ++i) {
            if((c = block_find_client(ship->blocks[i], gc))) {
                lua_pushlightuserdata(l, c);
                return 1;
            }
        }

        lua_pushnil(l);
    }
    else {
        lua_pushnil(l);
    }

    return 1;
}

static int client_syncRegister_lua(lua_State *l) {
    ship_client_t *c;
    lua_Integer reg, value;

    if(lua_islightuserdata(l, 1) && lua_isinteger(l, 2) &&
       lua_isinteger(l, 3)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        reg = lua_tointeger(l, 2);
        value = lua_tointeger(l, 3);

        send_sync_register(c, (uint8_t)reg, (uint32_t)value);
        lua_pushinteger(l, 0);
    }
    else {
        lua_pushinteger(l, -1);
    }

    return 1;
}

static int client_hasItem_lua(lua_State *l) {
    ship_client_t *c;
    lua_Integer ic;
    uint32_t val;
    int i;
    sylverant_iitem_t *item;

    if(lua_islightuserdata(l, 1) && lua_isinteger(l, 2)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        ic = lua_tointeger(l, 2);

        for(i = 0; i < c->pl->v1.inv.item_count; ++i) {
            item = (sylverant_iitem_t *)&c->pl->v1.inv.items[i];
            val = item->data_l[0];

            /* Grab the real item type, if its a v2 item.
               Note: Gamecube uses this byte for wrapping paper design. */
            if(c->version < ITEM_VERSION_GC && item->data_b[5])
                val = (item->data_b[5] << 8);

            if((val & 0x00FFFFFF) == ic) {
                lua_pushboolean(l, 1);
                return 1;
            }
        }
    }

    /* If we get here, the user doesn't have the item requested, so return
       0 for false. */
    lua_pushboolean(l, 0);
    return 1;
}

static int client_legitCheck_lua(lua_State *l) {
    ship_client_t *c;
    int rv;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);

        rv = client_legit_check(c, ship->def_limits);
        lua_pushboolean(l, !rv);
        return 1;
    }

    lua_pushboolean(l, 1);
    return 1;
}

static int client_legitCheckItem_lua(lua_State *l) {
    ship_client_t *c;
    lua_Integer ic1, ic2, ic3, ic4, v;
    sylverant_iitem_t item;
    int rv;

    if(lua_islightuserdata(l, 1) && lua_isinteger(l, 2) &&
       lua_isinteger(l, 3) && lua_isinteger(l, 4) && lua_isinteger(l, 5)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        ic1 = lua_tointeger(l, 2);
        ic2 = lua_tointeger(l, 3);
        ic3 = lua_tointeger(l, 4);
        ic4 = lua_tointeger(l, 5);

        item.data_l[0] = (uint32_t)ic1;
        item.data_l[1] = (uint32_t)ic2;
        item.data_l[2] = (uint32_t)ic3;
        item.data2_l = (uint32_t)ic4;

        switch(c->version) {
            case CLIENT_VERSION_DCV1:
                v = ITEM_VERSION_V1;
                break;

            case CLIENT_VERSION_DCV2:
            case CLIENT_VERSION_PC:
                v = ITEM_VERSION_V2;
                break;

            case CLIENT_VERSION_GC:
            case CLIENT_VERSION_XBOX:
                v = ITEM_VERSION_GC;
                break;

            default:
                lua_pushboolean(l, 1);
                return 1;
        }

        rv = sylverant_limits_check_item(ship->def_limits, &item, v);

        if(!rv) {
            debug(DBG_LOG, "legitCheckItem failed for GC %" PRIu32 " with "
                  "item %08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32
                  "\n", c->guildcard, item.data_l[0], item.data_l[1],
                  item.data_l[2], item.data2_l);
        }

        lua_pushboolean(l, !!rv);
        return 1;
    }

    lua_pushboolean(l, 1);
    return 1;
}

static int client_coords_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        lua_pushnumber(l, c->x);
        lua_pushnumber(l, c->y);
        lua_pushnumber(l, c->z);

        return 3;
    }

    lua_pushnumber(l, 0);
    lua_pushnumber(l, 0);
    lua_pushnumber(l, 0);
    return 3;
}

static int client_distance_lua(lua_State *l) {
    ship_client_t *c, *c2;
    double x, y, z, d;

    if(lua_islightuserdata(l, 1) && lua_islightuserdata(l, 2)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        c2 = (ship_client_t *)lua_touserdata(l, 2);

        if(c->cur_lobby != c2->cur_lobby)
            goto err;

        if(c->cur_area != c2->cur_area)
            goto err;

        /* Calculate euclidian distance. */
        x = c->x - c2->x;
        y = c->y - c2->y;
        z = c->z - c2->z;

        d = sqrt(x * x + y * y + z * z);
        lua_pushnumber(l, d);
        return 1;
    }

err:
    lua_pushnumber(l, -1);
    return 1;
}

static int client_language_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, c->language_code);
        return 1;
    }

    lua_pushinteger(l, -1);
    return 1;
}

static int client_qlang_lua(lua_State *l) {
    ship_client_t *c;

    if(lua_islightuserdata(l, 1)) {
        c = (ship_client_t *)lua_touserdata(l, 1);
        lua_pushinteger(l, c->q_lang);
        return 1;
    }

    lua_pushinteger(l, -1);
    return 1;
}

static const luaL_Reg clientlib[] = {
    { "guildcard", client_guildcard_lua },
    { "isOnBlock", client_isOnBlock_lua },
    { "disconnect", client_disconnect_lua },
    { "addr", client_addr_lua },
    { "version", client_version_lua },
    { "clientID", client_clientID_lua },
    { "privilege", client_privilege_lua },
    { "send", client_send_lua },
    { "lobby", client_lobby_lua },
    { "block", client_block_lua },
    { "sendScriptData", client_sendSData_lua },
    { "sendMsg", client_sendMsg_lua },
    { "getTable", client_getTable_lua },
    { "area", client_area_lua },
    { "name", client_name_lua },
    { "flags", client_flags_lua },
    { "level", client_level_lua },
    { "sendMenu", client_sendMenu_lua },
    { "dropItem", client_dropItem_lua },
    { "sendMsgBox", client_sendMsgBox_lua },
    { "numItems", client_numItems_lua },
    { "item", client_item_lua },
    { "find", client_find_lua },
    { "syncRegister", client_syncRegister_lua },
    { "hasItem", client_hasItem_lua },
    { "legitCheck", client_legitCheck_lua },
    { "legitCheckItem", client_legitCheckItem_lua },
    { "coords", client_coords_lua },
    { "distance", client_distance_lua },
    { "language", client_language_lua },
    { "qlang", client_qlang_lua },
    { NULL, NULL }
};

int client_register_lua(lua_State *l) {
    luaL_newlib(l, clientlib);
    return 1;
}

#endif /* ENABLE_LUA */
