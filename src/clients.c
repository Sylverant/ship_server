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

#ifdef HAVE_PYTHON
#include <Python.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>

#include <sylverant/encryption.h>
#include <sylverant/mtwist.h>
#include <sylverant/debug.h>
#include <sylverant/prs.h>

#include "ship.h"
#include "utils.h"
#include "clients.h"
#include "ship_packets.h"
#include "scripts.h"
#include "subcmd.h"
#include "mapdata.h"

#ifdef UNUSED
#undef UNUSED
#endif

#define UNUSED __attribute__((unused))

#ifdef HAVE_PYTHON
/* Forward declarations */
static PyObject *client_pyobj_create(ship_client_t *c);
static void client_pyobj_invalidate(ship_client_t *c);
#endif

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

#ifdef HAVE_PYTHON
    rv->pyobj = client_pyobj_create(rv);
    
    if(!rv->pyobj) {
        goto err;
    }

    if(type == CLIENT_TYPE_SHIP) {
        script_execute(ScriptActionClientShipLogin, rv->pyobj, NULL);
    }
    else {
        script_execute(ScriptActionClientBlockLogin, rv->pyobj, NULL);
    }
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
    close(sock);

    if(type == CLIENT_TYPE_BLOCK) {
        free(rv->enemy_kills);
        free(rv->pl);
    }

#ifdef HAVE_PYTHON
    client_pyobj_invalidate(rv);
    Py_XDECREF(rv->pyobj);
#endif

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

#ifdef HAVE_PYTHON
    if(c->flags & CLIENT_FLAG_TYPE_SHIP) {
        script_execute(ScriptActionClientShipLogout, c->pyobj, NULL);
    }
    else {
        script_execute(ScriptActionClientBlockLogout, c->pyobj, NULL);
    }
#endif

    /* If the user was on a block, notify the shipgate */
    if(c->version != CLIENT_VERSION_BB && c->pl && c->pl->v1.name[0]) {
        shipgate_send_block_login(&ship->sg, 0, c->guildcard,
                                  c->cur_block->b, c->pl->v1.name);
    }
    else if(c->version == CLIENT_VERSION_BB && c->bb_pl) {
        shipgate_send_block_login_bb(&ship->sg, 0, c->guildcard,
                                     c->cur_block->b,
                                     c->bb_pl->character.name);
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

#ifdef HAVE_PYTHON
    client_pyobj_invalidate(c);
    Py_XDECREF(c->pyobj);
#endif

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
    if(!c->blacklist) {
        return 0;
    }

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

#ifdef HAVE_PYTHON

/* Basic client class structure */
typedef struct client_pyobj {
    PyObject_HEAD
    ship_client_t *client;
} ClientObject;

/* Destroy a Client object */
static void Client_dealloc(ClientObject *self) {
    self->ob_type->tp_free((PyObject *)self);
}

/* Get the user's guildcard number */
static PyObject *Client_guildcard(ClientObject *self, PyObject *args UNUSED) {
    if(!self->client) {
        return NULL;
    }

    return Py_BuildValue("k", (unsigned long)self->client->guildcard);
}

/* Is the client a block client? */
static PyObject *Client_isOnBlock(ClientObject *self, PyObject *args UNUSED) {
    if(!self->client) {
        return NULL;
    }

    if(!(self->client->flags & CLIENT_FLAG_TYPE_SHIP)) {
        Py_RETURN_TRUE;
    }

    Py_RETURN_FALSE;
}

/* Disconnect the client */
static PyObject *Client_disconnect(ClientObject *self, PyObject *args UNUSED) {
    if(!self->client) {
        return NULL;
    }

    self->client->flags |= CLIENT_FLAG_DISCONNECTED;
    Py_RETURN_NONE;
}

/* Get the user's IPv4 address */
static PyObject *Client_addr(ClientObject *self, PyObject *args UNUSED) {
    struct sockaddr_in *addr = (struct sockaddr_in *)&self->client->ip_addr;

    if(!self->client) {
        return NULL;
    }

    if(addr->sin_family != AF_INET) {
        Py_RETURN_NONE;
    }

    return PyString_FromStringAndSize((const char *)&addr->sin_addr.s_addr, 4);
}

/* Get the user's version */
static PyObject *Client_version(ClientObject *self, PyObject *args UNUSED) {
    if(!self->client) {
        return NULL;
    }

    return Py_BuildValue("i", self->client->version);
}

/* Get the user's client ID */
static PyObject *Client_clientID(ClientObject *self, PyObject *args UNUSED) {
    if(!self->client) {
        return NULL;
    }

    return Py_BuildValue("i", self->client->client_id);
}

/* Get the user's privilege level */
static PyObject *Client_privilege(ClientObject *self, PyObject *args UNUSED) {
    if(!self->client) {
        return NULL;
    }
    
    return Py_BuildValue("B", self->client->privilege);
}

/* Send a packet to the client */
static PyObject *Client_send(ClientObject *self, PyObject *args) {
    unsigned char *pkt = NULL;
    int pkt_len = 0, pkt_len2;

    if(!self->client || self->client->skey.type == 0xFF) {
        return NULL;
    }

    /* Grab the argument list */
    if(!PyArg_ParseTuple(args, "s#:send", (char *)&pkt, &pkt_len)) {
        return NULL;
    }

    /* Check the packet for sanity */
    if(pkt_len < 4 || (pkt_len & 0x03)) {
        return NULL;
    }

    pkt_len2 = pkt[2] | (pkt[3] << 8);
    if(pkt_len2 != pkt_len) {
        return NULL;
    }

    /* Send it away */
    if(send_pkt_dc(self->client, (dc_pkt_hdr_t *)pkt)) {
        return NULL;
    }

    Py_RETURN_NONE;
}

/* List of methods available to the Client class */
static PyMethodDef Client_methods[] = {
    { "guildcard", (PyCFunction)Client_guildcard, METH_NOARGS,
        "Return the guildcard number" },
    { "isOnBlock", (PyCFunction)Client_isOnBlock, METH_NOARGS,
        "Returns True if the client is on a block" },
    { "disconnect", (PyCFunction)Client_disconnect, METH_NOARGS,
        "Disconnect the client" },
    { "addr", (PyCFunction)Client_addr, METH_NOARGS,
        "Get the IPv4 address of the client" },
    { "version", (PyCFunction)Client_version, METH_NOARGS,
        "Get the version of PSO the user is playing" },
    { "clientID", (PyCFunction)Client_clientID, METH_NOARGS,
        "Get the user's client ID" },
    { "privilege", (PyCFunction)Client_privilege, METH_NOARGS,
        "Get the user's privilege level" },
    { "send", (PyCFunction)Client_send, METH_VARARGS,
        "Send a packet to the user" },
    { NULL }
};

static PyTypeObject sylverant_ClientType = {
    PyObject_HEAD_INIT(NULL)
    0,                              /* ob_size */
    "sylverant.Client",             /* tp_name */
    sizeof(ClientObject),           /* tp_basicsize */
    0,                              /* tp_itemsize */
    (destructor)Client_dealloc,     /* tp_dealloc */
    0,                              /* tp_print */
    0,                              /* tp_getattr */
    0,                              /* tp_setattr */
    0,                              /* tp_compare */
    0,                              /* tp_repr */
    0,                              /* tp_as_number */
    0,                              /* tp_as_sequence */
    0,                              /* tp_as_mapping */
    0,                              /* tp_hash */
    0,                              /* tp_call */
    0,                              /* tp_str */
    0,                              /* tp_getattro */
    0,                              /* tp_setattro */
    0,                              /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,             /* tp_flags */
    "Ship Server Clients",          /* tp_doc */
    0,                              /* tp_traverse */
    0,                              /* tp_clear */
    0,                              /* tp_richcompare */
    0,                              /* tp_weaklistoffset */
    0,                              /* tp_iter */
    0,                              /* tp_iternext */
    Client_methods,                 /* tp_methods */
    0,                              /* tp_members */
    0,                              /* tp_getset */
    0,                              /* tp_base */
    0,                              /* tp_dict */
    0,                              /* tp_descr_get */
    0,                              /* tp_descr_set */
    0,                              /* tp_dictoffset */
    0,                              /* tp_init */
    0,                              /* tp_alloc */
    PyType_GenericNew,              /* tp_new */
};

void client_init_scripting(PyObject *m) {
    if(PyType_Ready(&sylverant_ClientType) < 0)
        return;

    Py_INCREF(&sylverant_ClientType);

    PyModule_AddObject(m, "Client", (PyObject *)&sylverant_ClientType);
}

/* Get a Client object for Python */
static PyObject *client_pyobj_create(ship_client_t *c) {
    ClientObject *rv;

    rv = PyObject_New(ClientObject, &sylverant_ClientType);

    if(rv) {
        rv->client = c;
    }

    return (PyObject *)rv;
}

static void client_pyobj_invalidate(ship_client_t *c) {
    ClientObject *o = (ClientObject *)c->pyobj;

    o->client = NULL;
}

#endif /* HAVE_PYTHON */
