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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <iconv.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>

#include <sylverant/debug.h>
#include <sylverant/config.h>

#include "block.h"
#include "ship.h"
#include "clients.h"
#include "lobby.h"
#include "ship_packets.h"
#include "utils.h"
#include "shipgate.h"
#include "commands.h"
#include "gm.h"
#include "subcmd.h"

extern ship_t **ships;
extern sylverant_shipcfg_t *cfg;

extern in_addr_t local_addr;
extern in_addr_t netmask;

static void *block_thd(void *d) {
    block_t *b = (block_t *)d;
    ship_t *s = b->ship;
    int nfds;
    struct timeval timeout;
    fd_set readfds, writefds;
    ship_client_t *it, *tmp;
    socklen_t len;
    struct sockaddr_in addr;
    int sock;
    ssize_t sent;
    time_t now;

    debug(DBG_LOG, "%s(%d): Up and running\n", s->cfg->name, b->b);

    /* While we're still supposed to run... do it. */
    while(b->run) {
        /* Clear the fd_sets so we can use them again. */
        nfds = 0;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 5000;
        now = time(NULL);

        /* Fill the sockets into the fd_sets so we can use select below. */
        pthread_mutex_lock(&b->mutex);

        TAILQ_FOREACH(it, b->clients, qentry) {
            /* If we haven't heard from a client in 2 minutes, its dead.
               Disconnect it. */
            if(now > it->last_message + 120) {
                if(it->pl) {
                    debug(DBG_LOG, "Ping Timeout: %s(%d)\n", it->pl->name,
                          it->guildcard);
                }

                it->disconnected = 1;
                continue;
            }
            /* Otherwise, if we haven't heard from them in a minute, ping it. */
            else if(now > it->last_message + 60 && now > it->last_sent + 10) {
                if(send_simple(it, SHIP_PING_TYPE, 0)) {
                    it->disconnected = 1;
                    continue;
                }
            }

            FD_SET(it->sock, &readfds);

            /* Only add to the write fd set if we have something to send out. */
            if(it->sendbuf_cur) {
                FD_SET(it->sock, &writefds);
            }

            nfds = nfds > it->sock ? nfds : it->sock;
        }

        /* Add the listening sockets to the read fd_set. */
        FD_SET(b->dcsock, &readfds);
        nfds = nfds > b->dcsock ? nfds : b->dcsock;
        FD_SET(b->pcsock, &readfds);
        nfds = nfds > b->pcsock ? nfds : b->pcsock;

        pthread_mutex_unlock(&b->mutex);
        
        /* Wait for some activity... */
        if(select(nfds + 1, &readfds, &writefds, NULL, &timeout) > 0) {
            pthread_mutex_lock(&b->mutex);

            if(FD_ISSET(b->dcsock, &readfds)) {
                len = sizeof(struct sockaddr_in);
                if((sock = accept(b->dcsock, (struct sockaddr *)&addr,
                                  &len)) < 0) {
                    perror("accept");
                }

                debug(DBG_LOG, "%s(%d): Accepted block connection from %s\n",
                      s->cfg->name, b->b, inet_ntoa(addr.sin_addr));

                if(!client_create_connection(sock, CLIENT_VERSION_DCV1,
                                             CLIENT_TYPE_BLOCK, b->clients, s,
                                             b, addr.sin_addr.s_addr)) {
                    close(sock);
                }
            }

            if(FD_ISSET(b->pcsock, &readfds)) {
                len = sizeof(struct sockaddr_in);
                if((sock = accept(b->pcsock, (struct sockaddr *)&addr,
                                  &len)) < 0) {
                    perror("accept");
                }

                debug(DBG_LOG, "%s(%d): Accepted block connection from %s\n",
                      s->cfg->name, b->b, inet_ntoa(addr.sin_addr));

                if(!client_create_connection(sock, CLIENT_VERSION_PC,
                                             CLIENT_TYPE_BLOCK, b->clients, s,
                                             b, addr.sin_addr.s_addr)) {
                    close(sock);
                }
            }

            /* Process client connections. */
            TAILQ_FOREACH(it, b->clients, qentry) {
                pthread_mutex_lock(&it->mutex);

                /* Check if this connection was trying to send us something. */
                if(FD_ISSET(it->sock, &readfds)) {
                    if(client_process_pkt(it)) {
                        it->disconnected = 1;
                        pthread_mutex_unlock(&it->mutex);
                        continue;
                    }
                }

                /* If we have anything to write, check if we can right now. */
                if(FD_ISSET(it->sock, &writefds)) {
                    if(it->sendbuf_cur) {
                        sent = send(it->sock, it->sendbuf + it->sendbuf_start,
                                    it->sendbuf_cur - it->sendbuf_start, 0);

                        /* If we fail to send, and the error isn't EAGAIN,
                           bail. */
                        if(sent == -1) {
                            if(errno != EAGAIN) {
                                it->disconnected = 1;
                                pthread_mutex_unlock(&it->mutex);
                                continue;
                            }
                        }
                        else {
                            it->sendbuf_start += sent;

                            /* If we've sent everything, free the buffer. */
                            if(it->sendbuf_start == it->sendbuf_cur) {
                                free(it->sendbuf);
                                it->sendbuf = NULL;
                                it->sendbuf_cur = 0;
                                it->sendbuf_size = 0;
                                it->sendbuf_start = 0;
                            }
                        }
                    }
                }

                pthread_mutex_unlock(&it->mutex);
            }
        }

        /* Clean up any dead connections (its not safe to do a TAILQ_REMOVE
           in the middle of a TAILQ_FOREACH, and client_destroy_connection
           does indeed use TAILQ_REMOVE). */
        it = TAILQ_FIRST(b->clients);
        while(it) {
            tmp = TAILQ_NEXT(it, qentry);

            if(it->disconnected) {
                if(it->pl) {
                    debug(DBG_LOG, "Disconnecting %s(%d)\n", it->pl->name,
                          it->guildcard);
                }
                else {
                    debug(DBG_LOG, "Disconnecting something...\n");
                }

                /* Remove the player from the lobby before disconnecting
                   them, or else bad things might happen. */
                lobby_remove_player(it);
                client_destroy_connection(it, b->clients);
            }

            it = tmp;
        }

        pthread_mutex_unlock(&b->mutex);
    }

    return NULL;
}

block_t *block_server_start(ship_t *s, int b, uint16_t port) {
    block_t *rv;
    int dcsock, pcsock, i;
    struct sockaddr_in addr;
    lobby_t *l;
    pthread_mutexattr_t attr;
    int window_size = (32 * 1024) - 1;      /* ~32kb window size */

    debug(DBG_LOG, "%s: Starting server for block %d...\n", s->cfg->name, b);

    /* Create the sockets for listening for connections. */
    dcsock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(dcsock < 0) {
        perror("socket");
        return NULL;
    }

    /* Increase the TCP window size. */
    if(setsockopt(dcsock, SOL_SOCKET, SO_SNDBUF, &window_size, sizeof(int))) {
        perror("setsockopt");
        close(dcsock);
        return NULL;
    }

    if(setsockopt(dcsock, SOL_SOCKET, SO_RCVBUF, &window_size, sizeof(int))) {
        perror("setsockopt");
        close(dcsock);
        return NULL;
    }

    /* Bind the socket. */
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    memset(addr.sin_zero, 0, 8);

    if(bind(dcsock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
        perror("bind");
        close(dcsock);
        return NULL;
    }

    /* Listen on the socket for connections. */
    if(listen(dcsock, 10) < 0) {
        perror("listen");
        close(dcsock);
        return NULL;
    }

    pcsock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(pcsock < 0) {
        perror("socket");
        close(dcsock);
        return NULL;
    }

    /* Increase the TCP window size. */
    if(setsockopt(pcsock, SOL_SOCKET, SO_SNDBUF, &window_size, sizeof(int))) {
        perror("setsockopt");
        close(pcsock);
        close(dcsock);
        return NULL;
    }

    if(setsockopt(pcsock, SOL_SOCKET, SO_RCVBUF, &window_size, sizeof(int))) {
        perror("setsockopt");
        close(pcsock);
        close(dcsock);
        return NULL;
    }

    /* Bind the socket. */
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port + 1);
    memset(addr.sin_zero, 0, 8);

    if(bind(pcsock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
        perror("bind");
        close(pcsock);
        close(pcsock);
        return NULL;
    }

    /* Listen on the socket for connections. */
    if(listen(pcsock, 10) < 0) {
        perror("listen");
        close(pcsock);
        close(dcsock);
        return NULL;
    }    

    /* Make space for the block structure. */
    rv = (block_t *)malloc(sizeof(block_t));

    if(!rv) {
        debug(DBG_ERROR, "%s(%d): Cannot allocate memory!\n", s->cfg->name, b);
        close(pcsock);
        close(dcsock);
        return NULL;
    }

    /* Make room for the client list. */
    rv->clients = (struct client_queue *)malloc(sizeof(struct client_queue));

    if(!rv->clients) {
        debug(DBG_ERROR, "%s(%d): Cannot allocate memory for clients!\n",
              s->cfg->name, b);
        free(rv);
        close(pcsock);
        close(dcsock);
        return NULL;
    }

    /* Fill in the structure. */
    TAILQ_INIT(rv->clients);
    rv->ship = s;
    rv->b = b;
    rv->dc_port = port;
    rv->pc_port = port + 1;
    rv->dcsock = dcsock;
    rv->pcsock = pcsock;
    rv->run = 1;

    TAILQ_INIT(&rv->lobbies);

    /* Create the first 15 lobbies (the default ones) */
    for(i = 1; i <= 15; ++i) {
        /* Grab a new lobby. XXXX: Check the return value. */
        l = lobby_create_default(rv, i);

        /* Add it into our list of lobbies */
        TAILQ_INSERT_TAIL(&rv->lobbies, l, qentry);
    }

    /* Create the mutex */
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&rv->mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    /* Start up the thread for this block. */
    if(pthread_create(&rv->thd, NULL, &block_thd, rv)) {
        debug(DBG_ERROR, "%s(%d): Cannot start block thread!\n",
              s->cfg->name, b);
        close(pcsock);
        close(dcsock);
        /* XXXX: Deal with lobbies. */
        free(rv->clients);
        free(rv);
        return NULL;
    }

    return rv;
}

void block_server_stop(block_t *b) {
    lobby_t *it2, *tmp2;
    ship_client_t *it, *tmp;

    /* Set the flag to kill the block. */
    b->run = 0;

    /* Wait for it to die. */
    pthread_join(b->thd, NULL);

    /* Disconnect any clients. */
    it = TAILQ_FIRST(b->clients);
    while(it) {
        tmp = TAILQ_NEXT(it, qentry);
        client_destroy_connection(it, b->clients);        
        it = tmp;
    }

    /* Destroy the lobbies that exist. */
    it2 = TAILQ_FIRST(&b->lobbies);
    while(it2) {
        tmp2 = TAILQ_NEXT(it2, qentry);
        lobby_destroy(it2);
        it2 = tmp2;
    }

    /* Free the block structure. */
    close(b->dcsock);
    close(b->pcsock);
    free(b->clients);
    free(b);
}

int block_info_reply(ship_client_t *c, int block) {
    ship_t *s = c->cur_ship;
    block_t *b;
    char string[256];
    int games = 0, players = 0;
    lobby_t *i;
    ship_client_t *i2;

    /* Make sure the block selected is in range. */
    if(block > s->cfg->blocks) {
        return -1;
    }

    /* Make sure that block is up and running. */
    if(s->blocks[block - 1] == NULL  || s->blocks[block - 1]->run == 0) {
        return -2;
    }

    /* Grab the block in question */
    b = s->blocks[block - 1];

    /* Determine the number of games currently active. */
    TAILQ_FOREACH(i, &b->lobbies, qentry) {
        if(i->type & LOBBY_TYPE_GAME) {
            ++games;
        }
    }

    /* And the number of players active. */
    TAILQ_FOREACH(i2, b->clients, qentry) {
        if(i2->pl) {
            ++players;
        }
    }

    /* Fill in the string. */
    sprintf(string, "BLOCK%02d\n\n%d Players\n%d Games", b->b, players, games);

    /* Send the information away. */
    return send_info_reply(c, string);
}

lobby_t *block_get_lobby(block_t *b, uint32_t lobby_id) {
    lobby_t *rv = NULL, *l;

    /* Look through all the lobbies in this block. */
    TAILQ_FOREACH(l, &b->lobbies, qentry) {
        pthread_mutex_lock(&l->mutex);

        if(l->lobby_id == lobby_id) {
            rv = l;
        }

        pthread_mutex_unlock(&l->mutex);

        if(rv) {
            break;
        }
    }

    return rv;
}

/* Process a login packet, sending security data, a lobby list, and a character
   data request. */
static int dc_process_login(ship_client_t *c, dc_login_pkt *pkt) {
    /* Save what we care about in here. */
    c->guildcard = pkt->guildcard;

    /* See if this person is a GM. */
    c->is_gm = is_gm(pkt->guildcard, pkt->serial, pkt->access_key, c->cur_ship);

    if(send_dc_security(c, pkt->guildcard, NULL, 0)) {
        return -1;
    }

    if(send_lobby_list(c)) {
        return -2;
    }

    if(send_simple(c, SHIP_CHAR_DATA_REQUEST_TYPE, 0)) {
        return -3;
    }

    return 0;
}

/* Process a v2 login packet, sending security data, a lobby list, and a
   character data request. */
static int dcv2_process_login(ship_client_t *c, dcv2_login_pkt *pkt) {
    /* Save what we care about in here. */
    c->guildcard = LE32(pkt->guildcard);

    if(c->version != CLIENT_VERSION_PC)
        c->version = CLIENT_VERSION_DCV2;

    /* See if this person is a GM. */
    c->is_gm = is_gm(c->guildcard, pkt->serial, pkt->access_key, c->cur_ship);

    if(send_dc_security(c, c->guildcard, NULL, 0)) {
        return -1;
    }

    if(send_lobby_list(c)) {
        return -2;
    }

    if(send_simple(c, SHIP_CHAR_DATA_REQUEST_TYPE, 0)) {
        return -3;
    }

    return 0;
}

/* Process incoming character data, and add to a lobby, if the character isn't
   currently in a lobby. */
static int dc_process_char(ship_client_t *c, dc_char_data_pkt *pkt) {
    uint8_t type = c->version == CLIENT_VERSION_PC ? pkt->hdr.pc.pkt_type :
                                                     pkt->hdr.dc.pkt_type;

    /* Copy the character data to the client's structure. */
    memcpy(c->pl, &pkt->data, sizeof(player_t));

    /* If this packet is coming after the client has left a game, then don't
       do anything else here, they'll take care of it by sending an 0x84. */
    if(type == SHIP_LEAVE_GAME_PL_DATA_TYPE) {
        return 0;
    }

    /* If the client isn't in a lobby already, then add them to the first
       available default lobby. */
    if(!c->cur_lobby) {
        if(lobby_add_to_any(c)) {
            return -1;
        }

        if(send_lobby_join(c, c->cur_lobby)) {
            return -2;
        }

        if(send_lobby_add_player(c->cur_lobby, c)) {
            return -3;
        }
    }

    return 0;
}

/* Process a Packet 0x60 - Game Command 0 packet. For now, just forward to all
   other clients... We'll deal with specifics later. */
static int dc_process_g0(ship_client_t *c, dc_pkt_hdr_t *pkt) {
    return lobby_send_pkt_dc(c->cur_lobby, c, pkt);
}

/* Process a change lobby packet. */
static int dc_process_change_lobby(ship_client_t *c, dc_select_pkt *pkt) {
    lobby_t *i, *req = NULL;
    int rv;

    TAILQ_FOREACH(i, &c->cur_block->lobbies, qentry) {
        if(i->lobby_id == LE32(pkt->item_id)) {
            req = i;
            break;
        }
    }

    /* The requested lobby is non-existant? What to do... */
    if(req == NULL) {
        return send_message1(c, "\tC4Can\'t Change lobby!\n\n"
                                "\tC7The lobby is non-\nexistant.");
    }

    rv = lobby_change_lobby(c, req);

    if(rv == -1) {
        return send_message1(c, "\tC4Can\'t Change lobby!\n\n"
                                "\tC7The lobby is full.");
    }
    else {
        return rv;
    }
}

/* Process a chat packet. */
static int dc_process_chat(ship_client_t *c, dc_chat_pkt *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Sanity check... this shouldn't happen. */
    if(!l) {
        return -1;
    }

#ifndef DISABLE_CHAT_COMMANDS
    /* Check for commands. */
    if(pkt->msg[2] == '/') {
        return command_parse(c, pkt);
    }
#endif

    /* Send the message to the lobby. */
    return send_lobby_chat(l, c, pkt->msg);
}

/* Process a chat packet from a PC client. */
static int pc_process_chat(ship_client_t *c, dc_chat_pkt *pkt) {
    lobby_t *l = c->cur_lobby;
    size_t len = LE16(pkt->hdr.dc.pkt_len) - 12;

    /* Sanity check... this shouldn't happen. */
    if(!l) {
        return -1;
    }

#if 0
#ifndef DISABLE_CHAT_COMMANDS
    /* Check for commands. */
    if(pkt->msg[2] == '/') {
        return command_parse(c, pkt);
    }
#endif
#endif

    /* Send the message to the lobby. */
    return send_lobby_wchat(l, c, (uint16_t *)pkt->msg, len);
}

/* Process a Guild Search request. */
static int dc_process_guild_search(ship_client_t *c, dc_guild_search_pkt *pkt) {
    ship_t *s;
    int i, j;
    ship_client_t *it;
    uint32_t gc = LE32(pkt->gc_target);
    in_addr_t addr;
    int done = 0, rv = -1;

    /* Search any local ships first. */
    for(j = 0; j < cfg->ship_count && !done; ++j) {
        s = ships[j];
        for(i = 0; i < s->cfg->blocks && !done; ++i) {
            pthread_mutex_lock(&s->blocks[i]->mutex);

            /* Look through all clients on that block. */
            TAILQ_FOREACH(it, s->blocks[i]->clients, qentry) {
                /* Check if this is the target and the target has player
                   data. */
                if(it->guildcard == gc && it->pl) {
                    /* Figure out the IP address to send. */
                    if(netmask &&
                       (c->addr & netmask) == (local_addr & netmask)) {
                        addr = local_addr;
                    }
                    else {
                        addr = s->cfg->ship_ip;
                    }

                    pthread_mutex_lock(&it->mutex);
                    rv = send_guild_reply(c, gc, addr, s->blocks[i]->dc_port,
                                          it->cur_lobby->name,
                                          s->blocks[i]->b, s->cfg->name,
                                          it->cur_lobby->lobby_id,
                                          it->pl->name);
                    done = 1;
                    pthread_mutex_unlock(&it->mutex);
                }
                else if(it->guildcard == gc) {
                    /* If they're on but don't have data, we're not going to
                       find them anywhere else, return success. */
                    rv = 0;
                    done = 1;
                }

                if(done)
                    break;
            }

            pthread_mutex_unlock(&s->blocks[i]->mutex);
        }
    }

    /* If we get here, we didn't find it locally. Send to the shipgate to
       continue searching. */
    if(!done) {
        return shipgate_fw_dc(&c->cur_ship->sg, pkt);
    }

    return rv;
}

static int dc_process_mail(ship_client_t *c, dc_simple_mail_pkt *pkt) {
    ship_t *s;
    int i, j;
    ship_client_t *it;
    uint32_t gc = LE32(pkt->gc_dest);
    int done = 0, rv = -1;

    /* Search any local ships first. */
    for(j = 0; j < cfg->ship_count && !done; ++j) {
        s = ships[j];
        for(i = 0; i < s->cfg->blocks && !done; ++i) {
            pthread_mutex_lock(&s->blocks[i]->mutex);

            /* Look through all clients on that block. */
            TAILQ_FOREACH(it, s->blocks[i]->clients, qentry) {
                /* Check if this is the target and the target has player
                   data. */
                if(it->guildcard == gc && it->pl) {
                    pthread_mutex_lock(&it->mutex);
                    rv = send_pkt_dc(it, (dc_pkt_hdr_t *)pkt);
                    pthread_mutex_unlock(&it->mutex);
                    done = 1;
                }
                else if(it->guildcard == gc) {
                    /* If they're on but don't have data, we're not going to
                       find them anywhere else, return success. */
                    rv = 0;
                    done = 1;
                }

                if(done)
                    break;
            }

            pthread_mutex_unlock(&s->blocks[i]->mutex);
        }
    }

    if(!done) {
        /* If we get here, we didn't find it locally. Send to the shipgate to
           continue searching. */
        return shipgate_fw_dc(&c->cur_ship->sg, pkt);
    }

    return rv;
}

static int dc_process_game_create(ship_client_t *c, dc_game_create_pkt *pkt) {
    lobby_t *l;

    /* Check the user's ability to create a game of that difficulty. */
    if(c->pl->level < game_required_level[pkt->difficulty]) {
        return send_message1(c, "\tC4Can\'t create game!\n\n"
                             "\tC7Your level is too\nlow for that\n"
                             "difficulty.");
    }

    /* Create the lobby structure. */
    l = lobby_create_game(c->cur_block, pkt->name, pkt->password,
                          pkt->difficulty, pkt->battle, pkt->challenge,
                          pkt->version, c->version, c->pl->section, 0);

    /* If we don't have a game, something went wrong... tell the user. */
    if(!l) {
        return send_message1(c, "\tC4Can\'t create game!\n\n"
                             "\tC7Try again later.");
    }

    /* We've got a new game, but nobody's in it yet... Lets put the requester
       in the game. */
    if(lobby_change_lobby(c, l)) {
        /* Something broke, destroy the created lobby before anyone tries to
           join it. */
        lobby_destroy(l);
        return -1;
    }

    /* All is good in the world. */
    return 0;
}

static int pc_process_game_create(ship_client_t *c, pc_game_create_pkt *pkt) {
    lobby_t *l = NULL;
    char name[16], password[16];
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;

    /* Convert the name/password to Shift-JIS. */
    ic = iconv_open("SHIFT_JIS", "UTF-16LE");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    in = 32;
    out = 16;
    inptr = (char *)pkt->name;
    outptr = name;
    iconv(ic, &inptr, &in, &outptr, &out);

    in = 32;
    out = 16;
    inptr = (char *)pkt->password;
    outptr = password;
    iconv(ic, &inptr, &in, &outptr, &out);
    iconv_close(ic);

    /* Check the user's ability to create a game of that difficulty. */
    if(c->pl->level < game_required_level[pkt->difficulty]) {
        return send_message1(c, "\tC4Can\'t create game!\n\n"
                             "\tC7Your level is too\nlow for that\n"
                             "difficulty.");
    }

    /* Create the lobby structure. */
    l = lobby_create_game(c->cur_block, name, password, pkt->difficulty,
                          pkt->battle, pkt->challenge, 1, c->version,
                          c->pl->section, 0);

    /* If we don't have a game, something went wrong... tell the user. */
    if(!l) {
        return send_message1(c, "\tC4Can\'t create game!\n\n"
                             "\tC7Try again later.");
    }

    /* We've got a new game, but nobody's in it yet... Lets put the requester
       in the game. */
    if(lobby_change_lobby(c, l)) {
        /* Something broke, destroy the created lobby before anyone tries to
           join it. */
        lobby_destroy(l);
        return -1;
    }

    /* All is good in the world. */
    return 0;
}

/* Process a client's done bursting signal. */
static int dc_process_done_burst(ship_client_t *c) {
    lobby_t *l = c->cur_lobby;

    /* Sanity check... Is the client in a game lobby? */
    if(!l || !(l->type & LOBBY_TYPE_GAME)) {
        return -1;
    }

    /* Lock the lobby, clear its bursting flag, send the resume game packet to
       the rest of the lobby, and continue on. */
    pthread_mutex_lock(&l->mutex);

    l->flags &= ~LOBBY_FLAG_BURSTING;
    send_lobby_done_burst(l, c);

    pthread_mutex_unlock(&l->mutex);

    return 0;
}

static int dc_process_menu(ship_client_t *c, dc_select_pkt *pkt) {
    uint32_t menu_id = LE32(pkt->menu_id);
    uint32_t item_id = LE32(pkt->item_id);

    /* Figure out what the client is selecting. */
    switch(menu_id & 0xFF) {
        /* Lobby Information Desk */
        case 0x00:
        {
            FILE *fp;
            char buf[1024];
            long len;

            /* The item_id should be the information the client wants. */
            if(item_id >= c->cur_ship->cfg->info_file_count) {
                send_message1(c, "\tC4That information is\n"
                              "clasified!\n\n"
                              "\tC7Nah, it just doesn't\n"
                              "exist, sorry.");
                return 0;
            }

            /* Attempt to open the file */
            fp = fopen(c->cur_ship->cfg->info_files[item_id], "r");

            if(!fp) {
                send_message1(c, "\tC4That information is\n"
                              "clasified!\n\n"
                              "\tC7Nah, it just doesn't\n"
                              "exist, sorry.");
                return 0;
            }

            /* Figure out the length of the file. */
            fseek(fp, 0, SEEK_END);
            len = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            /* Truncate to about 1KB */
            if(len > 1023) {
                len = 1023;
            }

            /* Read the file in. */
            fread(buf, 1, len, fp);
            fclose(fp);
            buf[len] = 0;

            /* Send the message to the client. */
            return send_message_box(c, buf);
        }

        /* Blocks */
        case 0x01:
        {
            ship_t *s = c->cur_ship;
            in_addr_t addr;
            uint16_t port;

            /* Make sure the block selected is in range. */
            if(item_id > s->cfg->blocks) {
                return -1;
            }

            /* Make sure that block is up and running. */
            if(s->blocks[item_id - 1] == NULL  ||
               s->blocks[item_id - 1]->run == 0) {
                return -2;
            }

            /* Figure out what address to send the client. */
            if(netmask && (c->addr & netmask) == (local_addr & netmask)) {
                addr = local_addr;
            }
            else {
                addr = s->cfg->ship_ip;
            }

            switch(c->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                    port = s->blocks[item_id - 1]->dc_port;
                    break;
                    
                case CLIENT_VERSION_PC:
                    port = s->blocks[item_id - 1]->pc_port;
                    break;

                default:
                    return -1;
            }

            /* Redirect the client where we want them to go. */
            return send_redirect(c, addr, port);
        }

        /* Game Selection */
        case 0x02:
        {
            char tmp[32];
            char passwd[16];
            lobby_t *l;
            int rv;
            uint16_t len = LE16(pkt->hdr.dc.pkt_len);

            /* Read the password if the client provided one. */
            if(len > 0x0C) {
                memcpy(tmp, ((uint8_t *)pkt) + 0x0C, len - 0x0C);
            }
            else {
                tmp[0] = '\0';
            }

            if(c->version == CLIENT_VERSION_PC) {
                iconv_t ic;
                size_t in, out;
                char *inptr, *outptr;

                ic = iconv_open("SHIFT_JIS", "UTF-16LE");

                if(ic == (iconv_t)-1) {
                    perror("iconv_open");
                    return send_message1(c, "\tC4Internal Server\nError");
                }

                in = 32;
                out = 16;
                inptr = tmp;
                outptr = passwd;
                iconv(ic, &inptr, &in, &outptr, &out);
                iconv_close(ic);
            }
            else {
                strcpy(passwd, tmp);
            }

            /* The client is selecting a game to join. */
            l = block_get_lobby(c->cur_block, item_id);

            if(!l) {
                /* The lobby has disappeared. */
                send_message1(c, "\tC4Can't join game!\n\n"
                              "\tC7This game is\nnon-existant.");
                return 0;
            }

            /* Check the provided password (if any). */
            if(strcmp(passwd, l->passwd)) {
                send_message1(c, "\tC4Can't join game!\n\n"
                              "\tC7Wrong Password.");
                return 0;
            }

            /* Attempt to change the player's lobby. */
            rv = lobby_change_lobby(c, l);

            if(rv == -8) {
                /* Quest selection in progress */
                send_message1(c, "\tC4Can't join game!\n\n"
                              "\tC7Quest selection\nis in progress");
            }
            else if(rv == -7) {
                /* Questing in progress */
                send_message1(c, "\tC4Can't join game!\n\n"
                              "\tC7A quest is in\nprogress.");
            }
            else if(rv == -6) {
                /* V1 client attempting to join a V2 only game */
                send_message1(c, "\tC4Can't join game!\n\n"
                              "\tC7This game is for\nVersion 2 only.");
            }
            else if(rv == -5) {
                /* Level is too high */
                send_message1(c, "\tC4Can't join game!\n\n"
                              "\tC7Your level is\ntoo high.");
            }
            else if(rv == -4) {
                /* Level is too high */
                send_message1(c, "\tC4Can't join game!\n\n"
                              "\tC7Your level is\ntoo low.");
            }
            else if(rv == -3) {
                /* A client is bursting. */
                send_message1(c, "\tC4Can't join game!\n\n"
                              "\tC7A Player is\nbursting.");
            }
            else if(rv == -2) {
                /* The lobby has disappeared. */
                send_message1(c, "\tC4Can't join game!\n\n"
                              "\tC7This game is\nnon-existant.");
            }
            else if(rv == -1) {
                /* The lobby is full. */
                send_message1(c, "\tC4Can't join game!\n\n"
                              "\tC7This game is\nfull.");
            }

            return 0;
        }

        /* Quest category */
        case 0x03:
        {
            int rv;
    
            pthread_mutex_lock(&c->cur_ship->qmutex);

            if(item_id >= c->cur_ship->quests.cat_count) {
                rv = send_message1(c, "\tC4That category is\nnon-existant.");
            }
            else {
                rv = send_quest_list(c, (int)item_id,
                                     c->cur_ship->quests.cats + item_id);
            }

            pthread_mutex_unlock(&c->cur_ship->qmutex);
            return rv;
        }

        /* Quest */
        case 0x04:
        {
            int q = menu_id >> 8;
            int rv;
            sylverant_quest_t *quest;

            pthread_mutex_lock(&c->cur_ship->qmutex);

            if(q >= c->cur_ship->quests.cat_count) {
                rv = send_message1(c, "\tC4That category is\nnon-existant.");
            }
            else if(item_id >= c->cur_ship->quests.cats[q].quest_count) {
                rv = send_message1(c, "\tC4That quest is\nnon-existant.");
            }
            else if(c->cur_lobby->flags & LOBBY_FLAG_BURSTING) {
                rv = send_message1(c, "\tC4Please wait a moment.");
            }
            else {
                c->cur_lobby->flags |= LOBBY_FLAG_QUESTING;
                quest = &c->cur_ship->quests.cats[q].quests[item_id];
                rv = send_quest(c->cur_lobby, quest);
            }

            pthread_mutex_unlock(&c->cur_ship->qmutex);
            return rv;
        }

        /* Ship */
        case 0x05:
        {
            int i;
            ship_t *s = c->cur_ship;
            in_addr_t addr;
            int off = c->version == CLIENT_VERSION_PC ? 1 : 0;

            /* Go through all the ships that we know about looking for the one
               that the user has requested. */
            for(i = 0; i < s->ship_count; ++i) {
                if(s->ships[i].ship_id == item_id) {
                    /* Figure out which address we need to send the client. */
                    if(c->addr == s->ships[i].ship_addr) {
                        /* The client and the ship are connecting from the same
                           address, this one is obvious. */
                        addr = s->ships[i].int_addr;
                    }
                    else if(netmask &&
                            s->ships[i].ship_addr == s->cfg->ship_ip &&
                            (c->addr & netmask) == (local_addr & netmask)) {
                        /* The destination and the source are on the same
                           network, and the client is on the same network as the
                           source, thus the client must be on the same network
                           as the destination, send the internal address. */
                        addr = s->ships[i].int_addr;
                    }
                    else {
                        /* They should be on different networks if we get here,
                           send the external IP. */
                        addr = s->ships[i].ship_addr;
                    }

                    return send_redirect(c, addr, s->ships[i].ship_port + off);
                }
            }

            /* We didn't find it, punt. */
            return send_message1(c, "\tC4That ship is now\noffline.");
        }
    }

    return -1;
}

static int dc_process_lobby_inf(ship_client_t *c) {
    return send_info_list(c, c->cur_ship);
}

static int dc_process_info_req(ship_client_t *c, dc_select_pkt *pkt) {
    uint32_t menu_id = LE32(pkt->menu_id);
    uint32_t item_id = LE32(pkt->item_id);

    /* What kind of information do they want? */
    switch(menu_id & 0xFF) {
        /* Block */
        case 0x01:
            return block_info_reply(c, (int)item_id);

        /* Game List */
        case 0x02:
            return lobby_info_reply(c, item_id);

        /* Quest */
        case 0x04:
        {
            int q = menu_id >> 8;
            int rv;
            sylverant_quest_t *quest;

            pthread_mutex_lock(&c->cur_ship->qmutex);

            if(q >= c->cur_ship->quests.cat_count) {
                rv = send_message1(c, "\tC4That category is\nnon-existant.");
            }
            else if(item_id >= c->cur_ship->quests.cats[q].quest_count) {
                rv = send_message1(c, "\tC4That quest is\nnon-existant.");
            }
            else {
                quest = &c->cur_ship->quests.cats[q].quests[item_id];
                rv = send_quest_info(c, quest);
            }

            pthread_mutex_unlock(&c->cur_ship->qmutex);
            return rv;
        }

        /* Ship */
        case 0x05:
            return send_info_reply(c, "Nothing here.");

        default:
            debug(DBG_WARN, "Unknown info request menu_id: 0x%08X\n", menu_id);
            return -1;
    }
}

/* Process a client's arrow update request. */
static int dc_process_arrow(ship_client_t *c, uint8_t flag) {
    c->arrow = flag;
    return send_lobby_arrows(c->cur_lobby);
}

/* Process block commands for a Dreamcast client. */
static int dc_process_pkt(ship_client_t *c, uint8_t *pkt) {
    uint8_t type;
    uint8_t flags;
    uint16_t len;
    dc_pkt_hdr_t *dc = (dc_pkt_hdr_t *)pkt;
    pc_pkt_hdr_t *pc = (pc_pkt_hdr_t *)pkt;
    int rv;

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2) {
        type = dc->pkt_type;
        len = LE16(dc->pkt_len);
        flags = dc->flags;
    }
    else {
        type = pc->pkt_type;
        len = LE16(pc->pkt_len);
        flags = pc->flags;
        dc->pkt_type = type;
        dc->pkt_len = LE16(len);
        dc->flags = flags;
    }

    debug(DBG_LOG, "%s(%d): Received type 0x%02X\n", c->cur_ship->cfg->name,
          c->cur_block->b, type);

    if(c->guildcard && c->pl->name[0])
        debug(DBG_LOG, "\tFrom %s (%d)\n", c->pl->name, c->guildcard);

    switch(type) {
        case SHIP_LOGIN_TYPE:
            return dc_process_login(c, (dc_login_pkt *)pkt);

        case SHIP_DC_CHAR_DATA_TYPE:
            return dc_process_char(c, (dc_char_data_pkt *)pkt);

        case SHIP_GAME_COMMAND0_TYPE:
            return dc_process_g0(c, dc);

        case SHIP_GAME_COMMAND2_TYPE:
        case SHIP_GAME_COMMANDD_TYPE:
            return subcmd_handle_one(c, (subcmd_pkt_t *)pkt);

        case SHIP_LOBBY_CHANGE_TYPE:
            return dc_process_change_lobby(c, (dc_select_pkt *)pkt);

        case SHIP_PING_TYPE:
            /* Ignore these, they're handled elsewhere. */
            return 0;

        case SHIP_TYPE_05:
            /* Ignore these for now. I dunno what they're supposed to mean. */
            return 0;

        case SHIP_CHAT_TYPE:
            if(c->version != CLIENT_VERSION_PC) {
                return dc_process_chat(c, (dc_chat_pkt *)pkt);
            }
            else {
                return pc_process_chat(c, (dc_chat_pkt *)pkt);
            }

        case SHIP_GUILD_SEARCH_TYPE:
            return dc_process_guild_search(c, (dc_guild_search_pkt *)pkt);

        case SHIP_SIMPLE_MAIL_TYPE:
            return dc_process_mail(c, (dc_simple_mail_pkt *)pkt);

        case SHIP_DC_GAME_CREATE_TYPE:
        case SHIP_GAME_CREATE_TYPE:
            if(c->version != CLIENT_VERSION_PC) {
                return dc_process_game_create(c, (dc_game_create_pkt *)pkt);
            }
            else {
                return pc_process_game_create(c, (pc_game_create_pkt *)pkt);
            }

        case SHIP_DONE_BURSTING_TYPE:
            return dc_process_done_burst(c);

        case SHIP_GAME_LIST_TYPE:
            return send_game_list(c, c->cur_block);

        case SHIP_MENU_SELECT_TYPE:
            return dc_process_menu(c, (dc_select_pkt *)pkt);

        case SHIP_LEAVE_GAME_PL_DATA_TYPE:
            return dc_process_char(c, (dc_char_data_pkt *)pkt);

        case SHIP_LOBBY_INFO_TYPE:
            return dc_process_lobby_inf(c);

        case SHIP_BLOCK_LIST_REQ_TYPE:
            return send_block_list(c, c->cur_ship);

        case SHIP_INFO_REQUEST_TYPE:
            return dc_process_info_req(c, (dc_select_pkt *)pkt);

        case SHIP_QUEST_LIST_TYPE:
            pthread_mutex_lock(&c->cur_ship->qmutex);
            pthread_mutex_lock(&c->cur_lobby->mutex);
            rv = send_quest_categories(c, &c->cur_ship->quests);
            c->cur_lobby->flags |= LOBBY_FLAG_QUESTSEL;
            pthread_mutex_unlock(&c->cur_lobby->mutex);
            pthread_mutex_unlock(&c->cur_ship->qmutex);
            return rv;

        case SHIP_QUEST_END_LIST_TYPE:
            pthread_mutex_lock(&c->cur_lobby->mutex);
            c->cur_lobby->flags &= ~LOBBY_FLAG_QUESTSEL;
            pthread_mutex_unlock(&c->cur_lobby->mutex);
            return 0;

        case SHIP_DCV2_LOGIN_TYPE:
            return dcv2_process_login(c, (dcv2_login_pkt *)pkt);

        case SHIP_LOBBY_NAME_TYPE:
            return send_lobby_name(c, c->cur_lobby);

        case SHIP_LOBBY_ARROW_CHANGE_TYPE:
            return dc_process_arrow(c, flags);

        case SHIP_SHIP_LIST_TYPE:
            return send_ship_list(c, c->cur_ship->ships,
                                  c->cur_ship->ship_count);

        default:
            debug(DBG_LOG, "Unknown packet!\n");
            print_packet((unsigned char *)pkt, len);
            return -3;
    }
}

/* Process any packet that comes into a block. */
int block_process_pkt(ship_client_t *c, uint8_t *pkt) {
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
            return dc_process_pkt(c, pkt);
    }

    return -1;
}
