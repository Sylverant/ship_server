/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2017, 2018,
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
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>

#include <sylverant/debug.h>
#include <sylverant/config.h>
#include <sylverant/memory.h>

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
#include "scripts.h"
#include "admin.h"
#include "smutdata.h"

extern int enable_ipv6;
extern uint32_t ship_ip4;
extern uint8_t ship_ip6[16];

static void *block_thd(void *d) {
    block_t *b = (block_t *)d;
    ship_t *s = b->ship;
    int nfds, i;
    struct timeval timeout;
    fd_set readfds, writefds;
    ship_client_t *it, *tmp;
    socklen_t len;
    struct sockaddr_storage addr;
    struct sockaddr *addr_p = (struct sockaddr *)&addr;
    char ipstr[INET6_ADDRSTRLEN];
    char nm[64];
    int sock;
    ssize_t sent;
    time_t now;
    int numsocks = 1;

#ifdef SYLVERANT_ENABLE_IPV6
    if(enable_ipv6) {
        numsocks = 2;
    }
#endif

    debug(DBG_LOG, "%s(%d): Up and running\n", s->cfg->name, b->b);

    /* While we're still supposed to run... do it. */
    while(b->run) {
        /* Clear the fd_sets so we can use them again. */
        nfds = 0;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        timeout.tv_sec = 15;
        timeout.tv_usec = 0;
        now = time(NULL);

        /* Fill the sockets into the fd_sets so we can use select below. */
        pthread_rwlock_rdlock(&b->lock);

        TAILQ_FOREACH(it, b->clients, qentry) {
            /* If we haven't heard from a client in a minute and a half, it is
               probably dead. Disconnect it. */
            if(now > it->last_message + 90) {
                if(it->bb_pl) {
                    istrncpy16(ic_utf16_to_utf8, nm,
                               &it->pl->bb.character.name[2], 64);
                    debug(DBG_LOG, "Ping Timeout: %s(%d)\n", nm, it->guildcard);
                }
                else if(it->pl) {
                    debug(DBG_LOG, "Ping Timeout: %s(%d)\n", it->pl->v1.name,
                          it->guildcard);
                }

                it->flags |= CLIENT_FLAG_DISCONNECTED;

                /* Make sure that we disconnect the client ASAP! */
                timeout.tv_sec = 0;

                continue;
            }
            /* Otherwise, if we haven't heard from them in half of a minute,
               ping them. */
            else if(now > it->last_message + 30 && now > it->last_sent + 10) {
                if(send_simple(it, PING_TYPE, 0)) {
                    it->flags |= CLIENT_FLAG_DISCONNECTED;
                    timeout.tv_sec = 0;
                    continue;
                }

                it->last_sent = now;
            }

            /* Check if their timeout expired to login after getting a
               protection message. */
            if((it->flags & CLIENT_FLAG_GC_PROTECT) &&
               it->join_time + 60 < now) {
                it->flags |= CLIENT_FLAG_DISCONNECTED;
                timeout.tv_sec = 0;
                continue;
            }

            FD_SET(it->sock, &readfds);

            /* Only add to the write fd set if we have something to send out. */
            if(it->sendbuf_cur) {
                FD_SET(it->sock, &writefds);
            }

            nfds = nfds > it->sock ? nfds : it->sock;
        }

        pthread_rwlock_unlock(&b->lock);

        /* Add the listening sockets to the read fd_set. */
        for(i = 0; i < numsocks; ++i) {
            FD_SET(b->dcsock[i], &readfds);
            nfds = nfds > b->dcsock[i] ? nfds : b->dcsock[i];
            FD_SET(b->pcsock[i], &readfds);
            nfds = nfds > b->pcsock[i] ? nfds : b->pcsock[i];
            FD_SET(b->gcsock[i], &readfds);
            nfds = nfds > b->gcsock[i] ? nfds : b->gcsock[i];
            FD_SET(b->ep3sock[i], &readfds);
            nfds = nfds > b->ep3sock[i] ? nfds : b->ep3sock[i];
            FD_SET(b->bbsock[i], &readfds);
            nfds = nfds > b->bbsock[i] ? nfds : b->bbsock[i];
        }

        FD_SET(b->pipes[1], &readfds);
        nfds = nfds > b->pipes[1] ? nfds : b->pipes[1];

        /* Wait for some activity... */
        if(select(nfds + 1, &readfds, &writefds, NULL, &timeout) > 0) {
            if(FD_ISSET(b->pipes[1], &readfds)) {
                read(b->pipes[1], &len, 1);
            }

            for(i = 0; i < numsocks; ++i) {
                if(FD_ISSET(b->dcsock[i], &readfds)) {
                    len = sizeof(struct sockaddr_storage);
                    if((sock = accept(b->dcsock[i], addr_p, &len)) < 0) {
                        perror("accept");
                    }

                    my_ntop(&addr, ipstr);
                    debug(DBG_LOG, "%s(%d): Accepted DC block connection from "
                          "%s\n", s->cfg->name, b->b, ipstr);

                    if(!client_create_connection(sock, CLIENT_VERSION_DCV1,
                                                 CLIENT_TYPE_BLOCK, b->clients,
                                                 s, b, addr_p, len)) {
                        close(sock);
                    }
                }

                if(FD_ISSET(b->pcsock[i], &readfds)) {
                    len = sizeof(struct sockaddr_storage);
                    if((sock = accept(b->pcsock[i], addr_p, &len)) < 0) {
                        perror("accept");
                    }

                    my_ntop(&addr, ipstr);
                    debug(DBG_LOG, "%s(%d): Accepted PC block connection from "
                          "%s\n", s->cfg->name, b->b, ipstr);

                    if(!client_create_connection(sock, CLIENT_VERSION_PC,
                                                 CLIENT_TYPE_BLOCK, b->clients,
                                                 s, b, addr_p, len)) {
                        close(sock);
                    }
                }

                if(FD_ISSET(b->gcsock[i], &readfds)) {
                    len = sizeof(struct sockaddr_storage);
                    if((sock = accept(b->gcsock[i], addr_p, &len)) < 0) {
                        perror("accept");
                    }

                    my_ntop(&addr, ipstr);
                    debug(DBG_LOG, "%s(%d): Accepted GC block connection from "
                          "%s\n", s->cfg->name, b->b, ipstr);

                    if(!client_create_connection(sock, CLIENT_VERSION_GC,
                                                 CLIENT_TYPE_BLOCK, b->clients,
                                                 s, b, addr_p, len)) {
                        close(sock);
                    }
                }

                if(FD_ISSET(b->ep3sock[i], &readfds)) {
                    len = sizeof(struct sockaddr_storage);
                    if((sock = accept(b->ep3sock[i], addr_p, &len)) < 0) {
                        perror("accept");
                    }

                    my_ntop(&addr, ipstr);
                    debug(DBG_LOG, "%s(%d): Accepted Episode 3 block "
                          "connection from %s\n", s->cfg->name, b->b, ipstr);

                    if(!client_create_connection(sock, CLIENT_VERSION_EP3,
                                                 CLIENT_TYPE_BLOCK, b->clients,
                                                 s, b, addr_p, len)) {
                        close(sock);
                    }
                }

                if(FD_ISSET(b->bbsock[i], &readfds)) {
                    len = sizeof(struct sockaddr_storage);
                    if((sock = accept(b->bbsock[i], addr_p, &len)) < 0) {
                        perror("accept");
                    }

                    my_ntop(&addr, ipstr);
                    debug(DBG_LOG, "%s(%d): Accepted Blue Burst block "
                          "connection from %s\n", s->cfg->name, b->b, ipstr);

                    if(!client_create_connection(sock, CLIENT_VERSION_BB,
                                                 CLIENT_TYPE_BLOCK, b->clients,
                                                 s, b, addr_p, len)) {
                        close(sock);
                    }
                }
            }

            pthread_rwlock_rdlock(&b->lock);

            /* Process client connections. */
            TAILQ_FOREACH(it, b->clients, qentry) {
                pthread_mutex_lock(&it->mutex);

                /* Check if this connection was trying to send us something. */
                if(FD_ISSET(it->sock, &readfds)) {
                    if(client_process_pkt(it)) {
                        it->flags |= CLIENT_FLAG_DISCONNECTED;
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
                                it->flags |= CLIENT_FLAG_DISCONNECTED;
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

            pthread_rwlock_unlock(&b->lock);
        }

        /* Clean up any dead connections (its not safe to do a TAILQ_REMOVE
           in the middle of a TAILQ_FOREACH, and client_destroy_connection
           does indeed use TAILQ_REMOVE). */
        pthread_rwlock_wrlock(&b->lock);
        it = TAILQ_FIRST(b->clients);
        while(it) {
            tmp = TAILQ_NEXT(it, qentry);

            if(it->flags & CLIENT_FLAG_DISCONNECTED) {
                if(it->bb_pl) {
                    istrncpy16(ic_utf16_to_utf8, nm,
                               &it->pl->bb.character.name[2], 64);
                    debug(DBG_LOG, "Disconnecting %s(%d)\n", nm, it->guildcard);
                }
                else if(it->pl) {
                    debug(DBG_LOG, "Disconnecting %s(%d)\n", it->pl->v1.name,
                          it->guildcard);
                }
                else {
                    my_ntop(&it->ip_addr, ipstr);
                    debug(DBG_LOG, "Disconnecting something (IP: %s).\n",
                          ipstr);
                }

                /* Remove the player from the lobby before disconnecting
                   them, or else bad things might happen. */
                lobby_remove_player(it);
                client_destroy_connection(it, b->clients);
                --b->num_clients;
            }

            it = tmp;
        }

        pthread_rwlock_unlock(&b->lock);
    }

    pthread_exit(NULL);
}

block_t *block_server_start(ship_t *s, int b, uint16_t port) {
    block_t *rv;
    int dcsock[2] = { -1, -1 }, pcsock[2] = { -1, -1 };
    int gcsock[2] = { -1, -1 }, ep3sock[2] = { -1, -1 };
    int bbsock[2] = { -1, -1 }, i;
    lobby_t *l, *l2;
    uint32_t rng_seed;

    debug(DBG_LOG, "%s: Starting server for block %d...\n", s->cfg->name, b);

    /* Create the sockets for listening for connections. */
    dcsock[0] = open_sock(AF_INET, port);
    if(dcsock[0] < 0) {
        return NULL;
    }

    pcsock[0] = open_sock(AF_INET, port + 1);
    if(pcsock[0] < 0) {
        goto err_close_dc;
    }

    gcsock[0] = open_sock(AF_INET, port + 2);
    if(gcsock[0] < 0) {
        goto err_close_pc;
    }

    ep3sock[0] = open_sock(AF_INET, port + 3);
    if(ep3sock[0] < 0) {
        goto err_close_gc;
    }

    bbsock[0] = open_sock(AF_INET, port + 4);
    if(bbsock[0] < 0) {
        goto err_close_ep3;
    }

#ifdef SYLVERANT_ENABLE_IPV6
    if(enable_ipv6) {
        dcsock[1] = open_sock(AF_INET6, port);
        if(dcsock[1] < 0) {
            goto err_close_bb;
        }

        pcsock[1] = open_sock(AF_INET6, port + 1);
        if(pcsock[1] < 0) {
            goto err_close_dc_6;
        }

        gcsock[1] = open_sock(AF_INET6, port + 2);
        if(gcsock[1] < 0) {
            goto err_close_pc_6;
        }

        ep3sock[1] = open_sock(AF_INET6, port + 3);
        if(ep3sock[1] < 0) {
            goto err_close_gc_6;
        }

        bbsock[1] = open_sock(AF_INET6, port + 4);
        if(bbsock[1] < 0) {
            goto err_close_ep3_6;
        }
    }
#endif

    /* Make space for the block structure. */
    rv = (block_t *)malloc(sizeof(block_t));

    if(!rv) {
        debug(DBG_ERROR, "%s(%d): Cannot allocate memory!\n", s->cfg->name, b);
        goto err_close_all;
    }

    memset(rv, 0, sizeof(block_t));

    /* Make our pipe */
    if(pipe(rv->pipes) == -1) {
        debug(DBG_ERROR, "%s(%d): Cannot create pipe!\n", s->cfg->name, b);
        goto err_free;
    }

    /* Make room for the client list. */
    rv->clients = (struct client_queue *)malloc(sizeof(struct client_queue));

    if(!rv->clients) {
        debug(DBG_ERROR, "%s(%d): Cannot allocate memory for clients!\n",
              s->cfg->name, b);
        goto err_pipes;
    }

    /* Fill in the structure. */
    TAILQ_INIT(rv->clients);
    rv->ship = s;
    rv->b = b;
    rv->dc_port = port;
    rv->pc_port = port + 1;
    rv->gc_port = port + 2;
    rv->ep3_port = port + 3;
    rv->bb_port = port + 4;
    rv->dcsock[0] = dcsock[0];
    rv->pcsock[0] = pcsock[0];
    rv->gcsock[0] = gcsock[0];
    rv->ep3sock[0] = ep3sock[0];
    rv->bbsock[0] = bbsock[0];
    rv->dcsock[1] = dcsock[1];
    rv->pcsock[1] = pcsock[1];
    rv->gcsock[1] = gcsock[1];
    rv->ep3sock[1] = ep3sock[1];
    rv->bbsock[1] = bbsock[1];
    rv->run = 1;

    TAILQ_INIT(&rv->lobbies);

    /* Create the first 20 lobbies (the default ones) */
    for(i = 1; i <= 20; ++i) {
        /* Grab a new lobby. XXXX: Check the return value. */
        l = lobby_create_default(rv, i, s->lobby_event);

        /* Add it into our list of lobbies */
        TAILQ_INSERT_TAIL(&rv->lobbies, l, qentry);
    }

    /* Create the reader-writer locks */
    pthread_rwlock_init(&rv->lock, NULL);
    pthread_rwlock_init(&rv->lobby_lock, NULL);

    /* Initialize the random number generator. The seed value is the current
       UNIX time, xored with the port (so that each block will use a different
       seed even though they'll probably get the same timestamp). */
    rng_seed = (uint32_t)(time(NULL) ^ port);
    mt19937_init(&rv->rng, rng_seed);

    /* Start up the thread for this block. */
    if(pthread_create(&rv->thd, NULL, &block_thd, rv)) {
        debug(DBG_ERROR, "%s(%d): Cannot start block thread!\n",
              s->cfg->name, b);
        goto err_lobbies;
    }

    return rv;

err_lobbies:
    l2 = TAILQ_FIRST(&rv->lobbies);
    while(l2) {
        l = TAILQ_NEXT(l2, qentry);
        lobby_destroy(l2);
        l2 = l;
    }

    pthread_rwlock_destroy(&rv->lock);
    pthread_rwlock_destroy(&rv->lobby_lock);
    free(rv->clients);
err_pipes:
    close(rv->pipes[0]);
    close(rv->pipes[1]);
err_free:
    free(rv);
err_close_all:
#ifdef SYLVERANT_ENABLE_IPV6
    if(enable_ipv6) {
        close(bbsock[1]);
err_close_ep3_6:
        close(ep3sock[1]);
err_close_gc_6:
        close(gcsock[1]);
err_close_pc_6:
        close(pcsock[1]);
err_close_dc_6:
        close(dcsock[1]);
    }
err_close_bb:
#endif
    close(bbsock[0]);
err_close_ep3:
    close(ep3sock[0]);
err_close_gc:
    close(gcsock[0]);
err_close_pc:
    close(pcsock[0]);
err_close_dc:
    close(dcsock[0]);

    return NULL;
}

void block_server_stop(block_t *b) {
    lobby_t *it2, *tmp2;
    ship_client_t *it, *tmp;

    /* Set the flag to kill the block. */
    b->run = 0;

    /* Send a byte to the pipe so that we actually break out of the select. */
    write(b->pipes[0], "\xFF", 1);

    /* Wait for it to die. */
    pthread_join(b->thd, NULL);

    /* Close all the sockets so nobody can connect... */
    close(b->pipes[0]);
    close(b->pipes[1]);
    close(b->dcsock[0]);
    close(b->pcsock[0]);
    close(b->gcsock[0]);
    close(b->ep3sock[0]);
    close(b->bbsock[0]);
#ifdef SYLVERANT_ENABLE_IPV6
    if(enable_ipv6) {
        close(b->dcsock[1]);
        close(b->pcsock[1]);
        close(b->gcsock[1]);
        close(b->ep3sock[1]);
        close(b->bbsock[1]);
    }
#endif

    /* Disconnect any clients. */
    pthread_rwlock_wrlock(&b->lock);

    it = TAILQ_FIRST(b->clients);
    while(it) {
        tmp = TAILQ_NEXT(it, qentry);
        client_destroy_connection(it, b->clients);
        it = tmp;
    }

    pthread_rwlock_unlock(&b->lock);

    /* Destroy the lobbies that exist. */
    pthread_rwlock_wrlock(&b->lobby_lock);

    it2 = TAILQ_FIRST(&b->lobbies);
    while(it2) {
        tmp2 = TAILQ_NEXT(it2, qentry);
        lobby_destroy(it2);
        it2 = tmp2;
    }

    pthread_rwlock_unlock(&b->lobby_lock);

    /* Finish with our cleanup... */
    pthread_rwlock_destroy(&b->lobby_lock);
    pthread_rwlock_destroy(&b->lock);

    free(b->clients);
    free(b);
}

int block_info_reply(ship_client_t *c, uint32_t block) {
    block_t *b;
    char string[256];
    int players, games;

    /* Make sure the block selected is in range. */
    if(block > ship->cfg->blocks) {
        return 0;
    }

    /* Make sure that block is up and running. */
    if(ship->blocks[block - 1] == NULL || ship->blocks[block - 1]->run == 0) {
        return 0;
    }

    /* Grab the block in question */
    b = ship->blocks[block - 1];

    /* Grab the stats from the block structure */
    pthread_rwlock_rdlock(&b->lobby_lock);
    games = b->num_games;
    pthread_rwlock_unlock(&b->lobby_lock);

    pthread_rwlock_rdlock(&b->lock);
    players = b->num_clients;
    pthread_rwlock_unlock(&b->lock);

    /* Fill in the string. */
    snprintf(string, 256, "BLOCK%02d\n%d %s\n%d %s", b->b, players,
             __(c, "Users"), games, __(c, "Teams"));

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

static int join_game(ship_client_t *c, lobby_t *l) {
    int rv;
    int i;
    uint32_t id;

    /* Make sure they don't have the protection flag on */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7You must login\nbefore joining a\nteam."));
        return -1;
    }

    /* See if they can change lobbies... */
    rv = lobby_change_lobby(c, l);
    if(rv == -15) {
        /* HUcaseal, FOmar, or RAmarl trying to join a v1 game */
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7Your class is\nnot allowed in a\n"
                         "PSOv1 game."));
    }
    if(rv == -14) {
        /* Single player mode */
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7The game is\nin single player\nmode."));
    }
    else if(rv == -13) {
        /* PC only */
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7The game is\nfor PSOPC only."));
    }
    else if(rv == -12) {
        /* V1 only */
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7The game is\nfor PSOv1 only."));
    }
    else if(rv == -11) {
        /* DC only */
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7The game is\nfor PSODC only."));
    }
    else if(rv == -10) {
        /* Temporarily unavailable */
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7The game is\ntemporarily\nunavailable."));
    }
    else if(rv == -9) {
        /* Legit check failed */
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7Game mode is set\nto legit and you\n"
                         "failed the legit\ncheck!"));
    }
    else if(rv == -8) {
        /* Quest selection in progress */
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7Quest selection\nis in progress"));
    }
    else if(rv == -7) {
        /* Questing in progress */
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7A quest is in\nprogress."));
    }
    else if(rv == -6) {
        /* V1 client attempting to join a V2 only game */
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7This game is for\nVersion 2 only."));
    }
    else if(rv == -5) {
        /* Level is too high */
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7Your level is\ntoo high."));
    }
    else if(rv == -4) {
        /* Level is too high */
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7Your level is\ntoo low."));
    }
    else if(rv == -3) {
        /* A client is bursting. */
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7A Player is\nbursting."));
    }
    else if(rv == -2) {
        /* The lobby has disappeared. */
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7This game is\nnon-existant."));
    }
    else if(rv == -1) {
        /* The lobby is full. */
        send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                      __(c, "\tC7This game is\nfull."));
    }
    else {
        /* Clear their legit mode flag... */
        if((c->flags & CLIENT_FLAG_LEGIT)) {
            c->flags &= ~CLIENT_FLAG_LEGIT;
            if(c->limits)
                release(c->limits);
        }

        if(c->version == CLIENT_VERSION_BB) {
            /* Fix up the inventory for their new lobby */
            id = 0x00010000 | (c->client_id << 21) |
                (l->highest_item[c->client_id]);

            for(i = 0; i < c->bb_pl->inv.item_count; ++i, ++id) {
                c->bb_pl->inv.items[i].item_id = LE32(id);
            }

            --id;
            l->highest_item[c->client_id] = id;
        }
        else {
            /* Fix up the inventory for their new lobby */
            id = 0x00010000 | (c->client_id << 21) |
                (l->highest_item[c->client_id]);

            for(i = 0; i < c->item_count; ++i, ++id) {
                c->items[i].item_id = LE32(id);
            }

            --id;
            l->highest_item[c->client_id] = id;
        }
    }

    /* Try to backup their character data */
    if(c->version != CLIENT_VERSION_BB &&
       (c->flags & CLIENT_FLAG_AUTO_BACKUP)) {
        if(shipgate_send_cbkup(&ship->sg, c->guildcard, c->cur_block->b,
                               c->pl->v1.name, &c->pl->v1, 1052)) {
            /* XXXX: Should probably notify them... */
            return rv;
        }
    }

    return rv;
}

/* Process a login packet, sending security data, a lobby list, and a character
   data request. */
static int dcnte_process_login(ship_client_t *c, dcnte_login_8b_pkt *pkt) {
    char ipstr[INET6_ADDRSTRLEN];
    char *ban_reason;
    time_t ban_end;

    /* Make sure v1 is allowed on this ship. */
    if((ship->cfg->shipgate_flags & SHIPGATE_FLAG_NODCNTE)) {
        send_message_box(c, "%s", __(c, "\tEPSO NTE is not supported on\n"
                                     "this ship.\n\nDisconnecting."));
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        return 0;
    }

    /* See if the user is banned */
    if(is_guildcard_banned(ship, c->guildcard, &ban_reason, &ban_end)) {
        send_ban_msg(c, ban_end, ban_reason);
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        free(ban_reason);
        return 0;
    }
    else if(is_ip_banned(ship, &c->ip_addr, &ban_reason, &ban_end)) {
        send_ban_msg(c, ban_end, ban_reason);
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        free(ban_reason);
        return 0;
    }

    /* Save what we care about in here. */
    c->guildcard = LE32(pkt->guildcard);
    c->language_code = CLIENT_LANG_JAPANESE;
    c->q_lang = CLIENT_LANG_JAPANESE;
    c->flags |= CLIENT_FLAG_IS_NTE;

    /* See if this person is a GM. */
    c->privilege = is_gm(c->guildcard, ship);

    if(send_dc_security(c, c->guildcard, NULL, 0)) {
        return -1;
    }

    if(send_lobby_list(c)) {
        return -2;
    }

    if(send_simple(c, DCNTE_CHAR_DATA_REQ_TYPE, 0)) {
        return -3;
    }

    /* Log the connection. */
    my_ntop(&c->ip_addr, ipstr);
    debug(DBG_LOG, "%s(%d): DC NTE Guild Card %d connected with IP %s\n",
          ship->cfg->name, c->cur_block->b, c->guildcard, ipstr);

    return 0;
}

/* Process a login packet, sending security data, a lobby list, and a character
   data request. */
static int dc_process_login(ship_client_t *c, dc_login_93_pkt *pkt) {
    char ipstr[INET6_ADDRSTRLEN];
    char *ban_reason;
    time_t ban_end;

    /* Make sure v1 is allowed on this ship. */
    if((ship->cfg->shipgate_flags & SHIPGATE_FLAG_NOV1)) {
        send_message_box(c, "%s", __(c, "\tEPSO Version 1 is not supported on\n"
                                     "this ship.\n\nDisconnecting."));
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        return 0;
    }

    /* See if the user is banned */
    if(is_guildcard_banned(ship, c->guildcard, &ban_reason, &ban_end)) {
        send_ban_msg(c, ban_end, ban_reason);
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        free(ban_reason);
        return 0;
    }
    else if(is_ip_banned(ship, &c->ip_addr, &ban_reason, &ban_end)) {
        send_ban_msg(c, ban_end, ban_reason);
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        free(ban_reason);
        return 0;
    }

    /* Save what we care about in here. */
    c->guildcard = LE32(pkt->guildcard);
    c->language_code = pkt->language_code;
    c->q_lang = pkt->language_code;

    /* See if this person is a GM. */
    c->privilege = is_gm(c->guildcard, ship);

    if(send_dc_security(c, c->guildcard, NULL, 0)) {
        return -1;
    }

    if(send_lobby_list(c)) {
        return -2;
    }

    if(send_simple(c, CHAR_DATA_REQUEST_TYPE, 0)) {
        return -3;
    }

    /* Log the connection. */
    my_ntop(&c->ip_addr, ipstr);
    debug(DBG_LOG, "%s(%d): DCv1 Guild Card %d connected with IP %s\n",
          ship->cfg->name, c->cur_block->b, c->guildcard, ipstr);

    return 0;
}

static int is_pctrial(dcv2_login_9d_pkt *pkt) {
    int i = 0;

    for(i = 0; i < 8; ++i) {
        if(pkt->serial[i] || pkt->access_key[i])
            return 0;
    }

    return 1;
}

/* Process a v2 login packet, sending security data, a lobby list, and a
   character data request. */
static int dcv2_process_login(ship_client_t *c, dcv2_login_9d_pkt *pkt) {
    char ipstr[INET6_ADDRSTRLEN];
    char *ban_reason;
    time_t ban_end;

    /* Make sure the client's version is allowed on this ship. */
    if(c->version != CLIENT_VERSION_PC) {
        if((ship->cfg->shipgate_flags & SHIPGATE_FLAG_NOV2)) {
            send_message_box(c, "%s", __(c, "\tEPSO Version 2 is not supported "
                                         "on\nthis ship.\n\nDisconnecting."));
            c->flags |= CLIENT_FLAG_DISCONNECTED;
            return 0;
        }
    }
    else {
        if((ship->cfg->shipgate_flags & SHIPGATE_FLAG_NOPC)) {
            send_message_box(c, "%s", __(c, "\tEPSO for PC is not supported "
                                         "on\nthis ship.\n\nDisconnecting."));
            c->flags |= CLIENT_FLAG_DISCONNECTED;
            return 0;
        }

        /* Mark trial users as trial users. */
        if(is_pctrial(pkt)) {
            c->flags |= CLIENT_FLAG_IS_NTE;

            if((ship->cfg->shipgate_flags & SHIPGATE_FLAG_NOPCNTE)) {
                send_message_box(c, "%s", __(c, "\tEPSO for PC Network Trial "
                                             "Edition\nis not supported on "
                                             "this ship.\n\nDisconnecting."));
                c->flags |= CLIENT_FLAG_DISCONNECTED;
                return 0;
            }
        }
    }

    /* See if the user is banned */
    if(is_guildcard_banned(ship, c->guildcard, &ban_reason, &ban_end)) {
        send_ban_msg(c, ban_end, ban_reason);
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        free(ban_reason);
        return 0;
    }
    else if(is_ip_banned(ship, &c->ip_addr, &ban_reason, &ban_end)) {
        send_ban_msg(c, ban_end, ban_reason);
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        free(ban_reason);
        return 0;
    }

    /* Save what we care about in here. */
    c->guildcard = LE32(pkt->guildcard);
    c->language_code = pkt->language_code;
    c->q_lang = pkt->language_code;

    if(c->version != CLIENT_VERSION_PC)
        c->version = CLIENT_VERSION_DCV2;

    /* See if this person is a GM. */
    c->privilege = is_gm(c->guildcard, ship);

    if(send_dc_security(c, c->guildcard, NULL, 0)) {
        return -1;
    }

    if(send_lobby_list(c)) {
        return -2;
    }

    if(send_simple(c, CHAR_DATA_REQUEST_TYPE, 0)) {
        return -3;
    }

    /* Log the connection. */
    my_ntop(&c->ip_addr, ipstr);
    if(c->version == CLIENT_VERSION_DCV2)
        debug(DBG_LOG, "%s(%d): DCv2 Guild Card %d connected with IP %s\n",
              ship->cfg->name, c->cur_block->b, c->guildcard, ipstr);
    else if(!(c->flags & CLIENT_FLAG_IS_NTE))
        debug(DBG_LOG, "%s(%d): PC Guild Card %d connected with IP %s\n",
              ship->cfg->name, c->cur_block->b, c->guildcard, ipstr);
    else
        debug(DBG_LOG, "%s(%d): PC NTE Guild Card %d connected with IP %s\n",
              ship->cfg->name, c->cur_block->b, c->guildcard, ipstr);

    return 0;
}

/* Process a GC login packet, sending security data, a lobby list, and a
   character data request. */
static int gc_process_login(ship_client_t *c, gc_login_9e_pkt *pkt) {
    char ipstr[INET6_ADDRSTRLEN];
    char *ban_reason;
    time_t ban_end;

    /* Make sure PSOGC is allowed on this ship. */
    if(c->version == CLIENT_VERSION_GC) {
        if((ship->cfg->shipgate_flags & SHIPGATE_FLAG_NOEP12)) {
            send_message_box(c, "%s", __(c, "\tEPSO Episode 1 & 2 is not "
                                         "supported on\nthis ship.\n\n"
                                         "Disconnecting."));
            c->flags |= CLIENT_FLAG_DISCONNECTED;
            return 0;
        }
    }
    else {
        if((ship->cfg->shipgate_flags & SHIPGATE_FLAG_NOEP3)) {
            send_message_box(c, "%s", __(c, "\tEPSO Episode 3 is not "
                                         "supported on\nthis ship.\n\n"
                                         "Disconnecting."));
            c->flags |= CLIENT_FLAG_DISCONNECTED;
            return 0;
        }
    }

    /* See if the user is banned */
    if(is_guildcard_banned(ship, c->guildcard, &ban_reason, &ban_end)) {
        send_ban_msg(c, ban_end, ban_reason);
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        free(ban_reason);
        return 0;
    }
    else if(is_ip_banned(ship, &c->ip_addr, &ban_reason, &ban_end)) {
        send_ban_msg(c, ban_end, ban_reason);
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        free(ban_reason);
        return 0;
    }

    /* Save what we care about in here. */
    c->guildcard = LE32(pkt->guildcard);
    c->language_code = pkt->language_code;
    c->q_lang = pkt->language_code;

    /* See if this user can get message boxes properly... */
    switch(pkt->version) {
        case 0x32: /* Episode 1 & 2 (Europe, 50hz) */
        case 0x33: /* Episode 1 & 2 (Europe, 60hz) */
        case 0x34: /* Episode 1 & 2 (Japan, v1.03) */
        case 0x35: /* Episode 1 & 2 (Japan, v1.04) */
        case 0x36: /* Episode 1 & 2 Plus (US) */
        case 0x39: /* Episode 1 & 2 Plus (Japan) */
            c->flags |= CLIENT_FLAG_GC_MSG_BOXES;
            break;
    }

    /* See if this person is a GM. */
    c->privilege = is_gm(c->guildcard, ship);

    if(send_dc_security(c, c->guildcard, NULL, 0)) {
        return -1;
    }

    if(send_lobby_list(c)) {
        return -2;
    }

    if(send_simple(c, CHAR_DATA_REQUEST_TYPE, 0)) {
        return -3;
    }

    /* Log the connection. */
    my_ntop(&c->ip_addr, ipstr);
    debug(DBG_LOG, "%s(%d): GC Guild Card %d connected with IP %s\n",
          ship->cfg->name, c->cur_block->b, c->guildcard, ipstr);

    return 0;
}

static int bb_process_login(ship_client_t *c, bb_login_93_pkt *pkt) {
    uint32_t team_id;
    char ipstr[INET6_ADDRSTRLEN];
    char *ban_reason;
    time_t ban_end;

    /* Make sure PSOBB is allowed on this ship. */
    if((ship->cfg->shipgate_flags & LOGIN_FLAG_NOBB)) {
        send_message_box(c, "%s", __(c, "\tEPSO Blue Burst is not "
                                        "supported on\nthis ship.\n\n"
                                        "Disconnecting."));
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        return 0;
    }

    /* See if the user is banned */
    if(is_guildcard_banned(ship, c->guildcard, &ban_reason, &ban_end)) {
        send_ban_msg(c, ban_end, ban_reason);
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        free(ban_reason);
        return 0;
    }
    else if(is_ip_banned(ship, &c->ip_addr, &ban_reason, &ban_end)) {
        send_ban_msg(c, ban_end, ban_reason);
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        free(ban_reason);
        return 0;
    }

    c->guildcard = LE32(pkt->guildcard);
    team_id = LE32(pkt->team_id);

    /* See if this person is a GM. */
    c->privilege = is_gm(c->guildcard, ship);

    /* Copy in the security data */
    memcpy(&c->sec_data, pkt->security_data, sizeof(bb_security_data_t));

    if(c->sec_data.magic != LE32(0xDEADBEEF)) {
        send_bb_security(c, 0, LOGIN_93BB_FORCED_DISCONNECT, 0, NULL, 0);
        return -1;
    }

    /* Send the security data packet */
    if(send_bb_security(c, c->guildcard, LOGIN_93BB_OK, team_id,
                        &c->sec_data, sizeof(bb_security_data_t))) {
        return -2;
    }

    /* Request the character data from the shipgate */
    if(shipgate_send_creq(&ship->sg, c->guildcard, c->sec_data.slot)) {
        return -3;
    }

    /* Request the user options from the shipgate */
    if(shipgate_send_bb_opt_req(&ship->sg, c->guildcard, c->cur_block->b)) {
        return -4;
    }

    /* Log the connection. */
    my_ntop(&c->ip_addr, ipstr);
    debug(DBG_LOG, "%s(%d): BB Guild Card %d connected with IP %s\n",
          ship->cfg->name, c->cur_block->b, c->guildcard, ipstr);

    return 0;
}

/* Process incoming character data, and add to a lobby, if the character isn't
   currently in a lobby. */
static int dc_process_char(ship_client_t *c, dc_char_data_pkt *pkt) {
    uint8_t type = pkt->hdr.dc.pkt_type;
    uint16_t len = LE16(pkt->hdr.dc.pkt_len);
    uint8_t version = pkt->hdr.dc.flags;
    uint32_t v;
    int i;

    pthread_mutex_lock(&c->mutex);

    /* If they already had character data, then check if it's still sane. */
    if(c->pl->v1.name[0]) {
        i = client_check_character(c, &pkt->data, version);
        if(i) {
            debug(DBG_LOG, "%s(%d): Character check failed for GC %" PRIu32
                 " with error code %d\n", ship->cfg->name, c->cur_block->b,
                 c->guildcard, i);
            if(c->cur_lobby) {
                debug(DBG_LOG, "        Lobby name: %s (type: %d,%d,%d,%d)\n",
                      c->cur_lobby->name, c->cur_lobby->difficulty,
                      c->cur_lobby->battle, c->cur_lobby->challenge,
                      c->cur_lobby->v2);
            }
        }
    }

    /* Do some more sanity checking...
       XXXX: This should probably be more thorough and done as part of the
       client_check_character() function. */
    v = LE32(pkt->data.v1.level);
    if(v > 199) {
        send_message_box(c, __(c, "\tEHacked characters are not allowed\n"
                                  "on this server.\n\n"
                                  "This will be reported to the server\n"
                                  "administration."));
        debug(DBG_WARN, "%s(%d): Character with invalid level detected!\n"
                        "        GC %" PRIu32 ", Level: %" PRIu32 "\n",
              c->guildcard, v + 1);
        return -1;
    }

    /* Copy out the player data, and set up pointers. */
    if(version == 1) {
        memcpy(c->pl, &pkt->data, sizeof(v1_player_t));
        c->infoboard = NULL;
        c->c_rank = NULL;
        c->blacklist = NULL;
    }
    else if(version == 2 && c->version == CLIENT_VERSION_DCV2) {
        memcpy(c->pl, &pkt->data, sizeof(v2_player_t));
        c->infoboard = NULL;
        c->c_rank = c->pl->v2.c_rank.all;
        c->blacklist = NULL;
    }
    else if(version == 2 && c->version == CLIENT_VERSION_PC) {
        if(pkt->data.pc.autoreply[0]) {
            /* Copy in the autoreply */
            client_set_autoreply(c, pkt->data.pc.autoreply,
                                 len - 4 - sizeof(pc_player_t));
        }

        memcpy(c->pl, &pkt->data, sizeof(pc_player_t));
        c->infoboard = NULL;
        c->c_rank = c->pl->pc.c_rank.all;
        c->blacklist = c->pl->pc.blacklist;
    }
    else if(version == 3) {
        if(pkt->data.v3.autoreply[0]) {
            /* Copy in the autoreply */
            client_set_autoreply(c, pkt->data.v3.autoreply,
                                 len - 4 - sizeof(v3_player_t));
        }

        memcpy(c->pl, &pkt->data, sizeof(v3_player_t));
        c->infoboard = c->pl->v3.infoboard;
        c->c_rank = c->pl->v3.c_rank.all;
        c->blacklist = c->pl->v3.blacklist;
    }
    else if(version == 4) {
        /* XXXX: Not right, but work with it for now. */
        memcpy(c->pl, &pkt->data, sizeof(v3_player_t));
        c->infoboard = c->pl->v3.infoboard;
        c->c_rank = c->pl->v3.c_rank.all;
        c->blacklist = c->pl->v3.blacklist;
    }

    /* Copy out the inventory data */
    memcpy(c->items, c->pl->v1.inv.items, sizeof(item_t) * 30);
    c->item_count = (int)c->pl->v1.inv.item_count;

    /* Renumber the inventory data so we know what's going on later */
    for(i = 0; i < c->item_count; ++i) {
        v = 0x00210000 | i;
        c->items[i].item_id = LE32(v);
    }

    /* If this packet is coming after the client has left a game, then don't
       do anything else here, they'll take care of it by sending an 0x84. */
    if(type == LEAVE_GAME_PL_DATA_TYPE) {
        c->flags &= ~(CLIENT_FLAG_TRACK_INVENTORY | CLIENT_FLAG_LEGIT);
        pthread_mutex_unlock(&c->mutex);

        /* Remove the client from the lobby they're in, which will force the
           0x84 sent later to act like we're adding them to any lobby. */
        return lobby_remove_player(c);
    }

    /* If the client isn't in a lobby/team already, then add them to the first
       available lobby. */
    if(!c->cur_lobby) {
        if(lobby_add_to_any(c, NULL)) {
            pthread_mutex_unlock(&c->mutex);
            return -1;
        }

        if(send_lobby_join(c, c->cur_lobby)) {
            pthread_mutex_unlock(&c->mutex);
            return -2;
        }

        if(send_lobby_add_player(c->cur_lobby, c)) {
            pthread_mutex_unlock(&c->mutex);
            return -3;
        }

        /* Do a few things that should only be done once per session... */
        if(!(c->flags & CLIENT_FLAG_SENT_MOTD)) {
            script_execute(ScriptActionClientBlockLogin, c, SCRIPT_ARG_PTR,
                           c, SCRIPT_ARG_END);

            /* Notify the shipgate */
            shipgate_send_block_login(&ship->sg, 1, c->guildcard,
                                      c->cur_block->b, c->pl->v1.name);
            shipgate_send_lobby_chg(&ship->sg, c->guildcard,
                                    c->cur_lobby->lobby_id, c->cur_lobby->name);

            /* Set up to send the Message of the Day if we have one and the
               client hasn't already gotten it this session.
               Disabled for some Gamecube versions, due to bugginess (of the
               game) and for the DC NTE. */
            if((c->version == CLIENT_VERSION_DCV1 &&
                (c->flags & CLIENT_FLAG_IS_NTE)) ||
               ((c->version == CLIENT_VERSION_GC ||
                 c->version == CLIENT_VERSION_EP3) &&
                 !(c->flags & CLIENT_FLAG_GC_MSG_BOXES))) {
                /* Just say that we sent the MOTD, even though we won't ever do
                   it at all. */
                c->flags |= CLIENT_FLAG_SENT_MOTD;
            }
        }
        else {
            shipgate_send_lobby_chg(&ship->sg, c->guildcard,
                                    c->cur_lobby->lobby_id, c->cur_lobby->name);
        }

        /* Send a ping so we know when they're done loading in. This is useful
           for sending the MOTD as well as enforcing always-legit mode. */
        send_simple(c, PING_TYPE, 0);
    }

    pthread_mutex_unlock(&c->mutex);

    return 0;
}

static int bb_process_char(ship_client_t *c, bb_char_data_pkt *pkt) {
    uint16_t type = LE32(pkt->hdr.pkt_type);
    uint32_t v;
    int i;

    pthread_mutex_lock(&c->mutex);

    /* Copy out the player data, and set up pointers. */
    memcpy(c->pl, &pkt->data, sizeof(sylverant_bb_player_t));
    c->infoboard = (char *)c->pl->bb.infoboard;
    c->c_rank = c->pl->bb.c_rank;
    c->blacklist = c->pl->bb.blacklist;

    /* Copy out the inventory data */
    memcpy(c->items, c->pl->bb.inv.items, sizeof(item_t) * 30);
    c->item_count = (int)c->pl->bb.inv.item_count;

    /* Renumber the inventory data so we know what's going on later */
    for(i = 0; i < c->item_count; ++i) {
        v = 0x00210000 | i;
        c->items[i].item_id = LE32(v);
    }

    /* If this packet is coming after the client has left a game, then don't
       do anything else here, they'll take care of it by sending an 0x84. */
    if(type == LEAVE_GAME_PL_DATA_TYPE) {
        /* Remove the client from the lobby they're in, which will force the
           0x84 sent later to act like we're adding them to any lobby. */
        pthread_mutex_unlock(&c->mutex);
        return lobby_remove_player(c);
    }

    /* If the client isn't in a lobby already, then add them to the first
       available default lobby. */
    if(!c->cur_lobby) {
        if(lobby_add_to_any(c, NULL)) {
            pthread_mutex_unlock(&c->mutex);
            return -1;
        }

        if(send_lobby_join(c, c->cur_lobby)) {
            pthread_mutex_unlock(&c->mutex);
            return -2;
        }

        if(send_lobby_add_player(c->cur_lobby, c)) {
            pthread_mutex_unlock(&c->mutex);
            return -3;
        }

        /* Do a few things that should only be done once per session... */
        if(!(c->flags & CLIENT_FLAG_SENT_MOTD)) {
            /* Notify the shipgate */
            shipgate_send_block_login_bb(&ship->sg, 1, c->guildcard,
                                         c->cur_block->b,
                                         c->bb_pl->character.name);
            shipgate_send_lobby_chg(&ship->sg, c->guildcard,
                                    c->cur_lobby->lobby_id, c->cur_lobby->name);

            c->flags |= CLIENT_FLAG_SENT_MOTD;
        }
        else {
            shipgate_send_lobby_chg(&ship->sg, c->guildcard,
                                    c->cur_lobby->lobby_id, c->cur_lobby->name);
        }
    }

    pthread_mutex_unlock(&c->mutex);

    return 0;
}

/* Process a change lobby packet. */
static int process_change_lobby(ship_client_t *c, uint32_t item_id) {
    lobby_t *i, *req = NULL;
    int rv;

    /* Make sure they don't have the protection flag on */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't change lobby!"),
                             __(c, "\tC7You must login\nbefore changing\n"
                                "lobbies."));
    }

    pthread_rwlock_rdlock(&c->cur_block->lobby_lock);

    TAILQ_FOREACH(i, &c->cur_block->lobbies, qentry) {
        if(i->lobby_id == item_id) {
            req = i;
            break;
        }
    }

    /* The requested lobby is non-existant? What to do... */
    if(req == NULL) {
        pthread_rwlock_unlock(&c->cur_block->lobby_lock);
        return send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't change lobby!"),
                             __(c, "\tC7The lobby is non-\nexistant."));
    }

    rv = lobby_change_lobby(c, req);

    pthread_rwlock_unlock(&c->cur_block->lobby_lock);

    if(rv == -1) {
        return send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't change lobby!"),
                             __(c, "\tC7The lobby is full."));
    }
    else if(rv < 0) {
        return send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't change lobby!"),
                             __(c, "\tC7Unknown error occurred."));
    }
    else {
        /* Send a ping so we know when they're done loading in. This is useful
           for enforcing always-legit mode. */
        send_simple(c, PING_TYPE, 0);
        return rv;
    }
}

static int dc_process_change_lobby(ship_client_t *c, dc_select_pkt *pkt) {
    uint32_t item_id = LE32(pkt->item_id);

    return process_change_lobby(c, item_id);
}

static int bb_process_change_lobby(ship_client_t *c, bb_select_pkt *pkt) {
    uint32_t item_id = LE32(pkt->item_id);

    return process_change_lobby(c, item_id);
}

/* Process a chat packet. */
static int dc_process_chat(ship_client_t *c, dc_chat_pkt *pkt) {
    lobby_t *l = c->cur_lobby;
    int i;
    char *u8msg, *cmsg;
    size_t len;

    /* Sanity check... this shouldn't happen. */
    if(!l) {
        return -1;
    }

    len = strlen(pkt->msg);

    /* Fill in escapes for the color chat stuff */
    if(c->cc_char) {
        for(i = 0; i < len; ++i) {
            /* Only accept it if it has a C right after, since that means we
               should have a color code... Also, make sure there's at least one
               character after the C, or we get junk... */
            if(pkt->msg[i] == c->cc_char && pkt->msg[i + 1] == 'C' &&
               pkt->msg[i + 2] != '\0') {
                pkt->msg[i] = '\t';
            }
        }
    }

    /* Convert it to UTF-8. */
    if(!(u8msg = (char *)malloc((len + 1) << 2)))
        return -1;

    if((pkt->msg[0] == '\t' && pkt->msg[1] == 'J') ||
       (c->flags & CLIENT_FLAG_IS_NTE)) {
        istrncpy(ic_sjis_to_utf8, u8msg, pkt->msg, (len + 1) << 2);
    }
    else {
        istrncpy(ic_8859_to_utf8, u8msg, pkt->msg, (len + 1) << 2);
    }

    /* Check for commands. */
    if(pkt->msg[2] == '/') {
        return command_parse(c, pkt);
    }

    /* Don't send the message if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can chat."));
    }

    /* Create a censored version. */
    cmsg = smutdata_censor_string(u8msg, SMUTDATA_BOTH);

    /* Send the message to the lobby. */
    i = send_lobby_chat(l, c, u8msg, cmsg);

    /* Clean up. */
    free(cmsg);
    free(u8msg);
    return i;
}

/* Process a chat packet from a PC client. */
static int pc_process_chat(ship_client_t *c, dc_chat_pkt *pkt) {
    lobby_t *l = c->cur_lobby;
    size_t len = LE16(pkt->hdr.dc.pkt_len) - 12;
    int i;
    char *u8msg, *cmsg;

    /* Sanity check... this shouldn't happen. */
    if(!l) {
        return -1;
    }

    /* Fill in escapes for the color chat stuff */
    if(c->cc_char) {
        for(i = 0; i < len; i += 2) {
            /* Only accept it if it has a C right after, since that means we
               should have a color code... Also, make sure there's at least one
               character after the C, or we get junk... */
            if(pkt->msg[i] == c->cc_char && pkt->msg[i + 1] == '\0' &&
               pkt->msg[i + 2] == 'C' && pkt->msg[i + 3] == '\0' &&
               pkt->msg[i + 4] != '\0') {
                pkt->msg[i] = '\t';
            }
        }
    }

    /* Convert it to UTF-8. */
    if(!(u8msg = (char *)malloc((len + 1) << 1)))
        return -1;

    istrncpy16(ic_utf16_to_utf8, u8msg, (uint16_t *)pkt->msg, (len + 1) << 1);

    /* Check for commands. */
    if(pkt->msg[4] == '/') {
        return wcommand_parse(c, pkt);
    }

    /* Don't send the message if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can chat."));
    }

    cmsg = smutdata_censor_string(u8msg, SMUTDATA_BOTH);

    /* Send the message to the lobby. */
    i = send_lobby_chat(l, c, u8msg, cmsg);

    /* Clean up. */
    free(cmsg);
    free(u8msg);
    return i;
}

/* Process a chat packet from a Blue Burst client. */
static int bb_process_chat(ship_client_t *c, bb_chat_pkt *pkt) {
    lobby_t *l = c->cur_lobby;
    size_t len = LE16(pkt->hdr.pkt_len) - 16;
    int i;

    /* Sanity check... this shouldn't happen. */
    if(!l) {
        return -1;
    }

    /* Fill in escapes for the color chat stuff */
    if(c->cc_char) {
        for(i = 0; i < len; i += 2) {
            /* Only accept it if it has a C right after, since that means we
               should have a color code... Also, make sure there's at least one
               character after the C, or we get junk... */
            if(pkt->msg[i] == LE16(c->cc_char) &&
               pkt->msg[i + 2] == LE16('C')) {
                pkt->msg[i] = '\t';
            }
        }
    }

#ifndef DISABLE_CHAT_COMMANDS
    /* Check for commands. */
    if(pkt->msg[2] == LE16('/')) {
        return bbcommand_parse(c, pkt);
    }
#endif

    /* Don't send the message if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can chat."));
    }

    /* Send the message to the lobby. */
    return send_lobby_bbchat(l, c, (uint16_t *)pkt->msg, len);
}

/* Process a Guild Search request. */
static int dc_process_guild_search(ship_client_t *c, dc_guild_search_pkt *pkt) {
    int i;
    ship_client_t *it;
    uint32_t gc = LE32(pkt->gc_target);
    int done = 0, rv = -1;
    uint32_t flags = 0;

    /* Don't allow this if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can do that."));
    }

    /* Don't allow guild searches for any reserved guild card numbers. */
    if(gc < 1000)
        return 0;

    /* Search the local ship first. */
    for(i = 0; i < ship->cfg->blocks && !done; ++i) {
        if(!ship->blocks[i] || !ship->blocks[i]->run) {
            continue;
        }

        pthread_rwlock_rdlock(&ship->blocks[i]->lock);

        /* Look through all clients on that block. */
        TAILQ_FOREACH(it, ship->blocks[i]->clients, qentry) {
            /* Check if this is the target and the target has player
               data. */
            if(it->guildcard == gc && it->pl) {
                pthread_mutex_lock(&it->mutex);
#ifdef SYLVERANT_ENABLE_IPV6
                if((c->flags & CLIENT_FLAG_IPV6)) {
                    rv = send_guild_reply6(c, it);
                }
                else {
                    rv = send_guild_reply(c, it);
                }
#else
                rv = send_guild_reply(c, it);
#endif
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

        pthread_rwlock_unlock(&ship->blocks[i]->lock);
    }

    /* If we get here, we didn't find it locally. Send to the shipgate to
       continue searching. */
    if(!done) {
#ifdef SYLVERANT_ENABLE_IPV6
        if((c->flags & CLIENT_FLAG_IPV6)) {
            flags |= FW_FLAG_PREFER_IPV6;
        }
#endif

        return shipgate_fw_dc(&ship->sg, pkt, flags, c);
    }

    return rv;
}

static int bb_process_guild_search(ship_client_t *c, bb_guild_search_pkt *pkt) {
    int i;
    ship_client_t *it;
    uint32_t gc = LE32(pkt->gc_target);
    int done = 0, rv = -1;
    uint32_t flags = 0;

    /* Don't allow this if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can do that."));
    }

    /* Don't allow guild searches for any reserved guild card numbers. */
    if(gc < 1000)
        return 0;

    /* Search the local ship first. */
    for(i = 0; i < ship->cfg->blocks && !done; ++i) {
        if(!ship->blocks[i] || !ship->blocks[i]->run) {
            continue;
        }

        pthread_rwlock_rdlock(&ship->blocks[i]->lock);

        /* Look through all clients on that block. */
        TAILQ_FOREACH(it, ship->blocks[i]->clients, qentry) {
            /* Check if this is the target and the target has player
               data. */
            if(it->guildcard == gc && it->pl) {
                pthread_mutex_lock(&it->mutex);
#ifdef SYLVERANT_ENABLE_IPV6
                if((c->flags & CLIENT_FLAG_IPV6)) {
                    rv = send_guild_reply6(c, it);
                }
                else {
                    rv = send_guild_reply(c, it);
                }
#else
                rv = send_guild_reply(c, it);
#endif
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

        pthread_rwlock_unlock(&ship->blocks[i]->lock);
    }

    /* If we get here, we didn't find it locally. Send to the shipgate to
       continue searching. */
    if(!done) {
#ifdef SYLVERANT_ENABLE_IPV6
        if((c->flags & CLIENT_FLAG_IPV6)) {
            flags |= FW_FLAG_PREFER_IPV6;
        }
#endif

        return shipgate_fw_bb(&ship->sg, pkt, flags, c);
    }

    return rv;
}

static int dc_process_mail(ship_client_t *c, dc_simple_mail_pkt *pkt) {
    int i;
    ship_client_t *it;
    uint32_t gc = LE32(pkt->gc_dest);
    int done = 0, rv = -1;

    /* Don't send mail if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can send mail."));
    }

    /* Don't send mail for a STFUed client. */
    if((c->flags & CLIENT_FLAG_STFU)) {
        return 0;
    }

    /* First check if this is to the bug report "character". */
    if(gc == BUG_REPORT_GC) {
        dc_bug_report(c, pkt);
        return 0;
    }

    /* Don't allow mails for any reserved guild card numbers other than the bug
       report one. */
    if(gc < 1000)
        return 0;

    /* Search the local ship first. */
    for(i = 0; i < ship->cfg->blocks && !done; ++i) {
        if(!ship->blocks[i] || !ship->blocks[i]->run) {
            continue;
        }

        pthread_rwlock_rdlock(&ship->blocks[i]->lock);

        /* Look through all clients on that block. */
        TAILQ_FOREACH(it, ship->blocks[i]->clients, qentry) {
            /* Check if this is the target and the target has player
               data. */
            if(it->guildcard == gc && it->pl) {
                pthread_mutex_lock(&it->mutex);

                /* Make sure the user hasn't blacklisted the sender. */
                if(client_has_blacklisted(it, c->guildcard) ||
                   client_has_ignored(it, c->guildcard)) {
                    done = 1;
                    pthread_mutex_unlock(&it->mutex);
                    rv = 0;
                    break;
                }

                /* Check if the user has an autoreply set. */
                if(it->autoreply_on) {
                    send_mail_autoreply(c, it);
                }

                /* Send the mail. */
                rv = send_simple_mail(c->version, it, (dc_pkt_hdr_t *)pkt);
                pthread_mutex_unlock(&it->mutex);
                done = 1;
                break;
            }
            else if(it->guildcard == gc) {
                /* If they're on but don't have data, we're not going to
                   find them anywhere else, return success. */
                rv = 0;
                done = 1;
                break;
            }
        }

        pthread_rwlock_unlock(&ship->blocks[i]->lock);
    }

    if(!done) {
        /* If we get here, we didn't find it locally. Send to the shipgate to
           continue searching. */
        return shipgate_fw_dc(&ship->sg, pkt, 0, c);
    }

    return rv;
}

static int pc_process_mail(ship_client_t *c, pc_simple_mail_pkt *pkt) {
    int i;
    ship_client_t *it;
    uint32_t gc = LE32(pkt->gc_dest);
    int done = 0, rv = -1;

    /* Don't send mail if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can send mail."));
    }

    /* Don't send mail for a STFUed client. */
    if((c->flags & CLIENT_FLAG_STFU)) {
        return 0;
    }

    /* First check if this is to the bug report "character". */
    if(gc == BUG_REPORT_GC) {
        pc_bug_report(c, pkt);
        return 0;
    }

    /* Don't allow mails for any reserved guild card numbers other than the bug
       report one. */
    if(gc < 1000)
        return 0;

    /* Search the local ship first. */
    for(i = 0; i < ship->cfg->blocks && !done; ++i) {
        if(!ship->blocks[i] || !ship->blocks[i]->run) {
            continue;
        }

        pthread_rwlock_rdlock(&ship->blocks[i]->lock);

        /* Look through all clients on that block. */
        TAILQ_FOREACH(it, ship->blocks[i]->clients, qentry) {
            /* Check if this is the target and the target has player
               data. */
            if(it->guildcard == gc && it->pl) {
                pthread_mutex_lock(&it->mutex);

                /* Make sure the user hasn't blacklisted the sender. */
                if(client_has_blacklisted(it, c->guildcard) ||
                   client_has_ignored(it, c->guildcard)) {
                    done = 1;
                    pthread_mutex_unlock(&it->mutex);
                    rv = 0;
                    break;
                }

                /* Check if the user has an autoreply set. */
                if(it->autoreply_on) {
                    send_mail_autoreply(c, it);
                }

                rv = send_simple_mail(c->version, it, (dc_pkt_hdr_t *)pkt);
                pthread_mutex_unlock(&it->mutex);
                done = 1;
                break;
            }
            else if(it->guildcard == gc) {
                /* If they're on but don't have data, we're not going to
                   find them anywhere else, return success. */
                rv = 0;
                done = 1;
                break;
            }
        }

        pthread_rwlock_unlock(&ship->blocks[i]->lock);
    }

    if(!done) {
        /* If we get here, we didn't find it locally. Send to the shipgate to
           continue searching. */
        return shipgate_fw_pc(&ship->sg, pkt, 0, c);
    }

    return rv;
}

static int bb_process_mail(ship_client_t *c, bb_simple_mail_pkt *pkt) {
    int i;
    ship_client_t *it;
    uint32_t gc = LE32(pkt->gc_dest);
    int done = 0, rv = -1;

    /* Don't send mail if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can send mail."));
    }

    /* Don't send mail for a STFUed client. */
    if((c->flags & CLIENT_FLAG_STFU)) {
        return 0;
    }

    /* First check if this is to the bug report "character". */
    if(gc == BUG_REPORT_GC) {
        bb_bug_report(c, pkt);
        return 0;
    }

    /* Don't allow mails for any reserved guild card numbers other than the bug
       report one. */
    if(gc < 1000)
        return 0;

    /* Search the local ship first. */
    for(i = 0; i < ship->cfg->blocks && !done; ++i) {
        if(!ship->blocks[i] || !ship->blocks[i]->run) {
            continue;
        }

        pthread_rwlock_rdlock(&ship->blocks[i]->lock);

        /* Look through all clients on that block. */
        TAILQ_FOREACH(it, ship->blocks[i]->clients, qentry) {
            /* Check if this is the target and the target has player
               data. */
            if(it->guildcard == gc && it->pl) {
                pthread_mutex_lock(&it->mutex);

                /* Make sure the user hasn't blacklisted the sender. */
                if(client_has_blacklisted(it, c->guildcard) ||
                   client_has_ignored(it, c->guildcard)) {
                    done = 1;
                    pthread_mutex_unlock(&it->mutex);
                    rv = 0;
                    break;
                }

                /* Check if the user has an autoreply set. */
                if(it->autoreply_on) {
                    send_mail_autoreply(c, it);
                }

                rv = send_bb_simple_mail(it, pkt);
                pthread_mutex_unlock(&it->mutex);
                done = 1;
                break;
            }
            else if(it->guildcard == gc) {
                /* If they're on but don't have data, we're not going to
                   find them anywhere else, return success. */
                rv = 0;
                done = 1;
                break;
            }
        }

        pthread_rwlock_unlock(&ship->blocks[i]->lock);
    }

    if(!done) {
        /* If we get here, we didn't find it locally. Send to the shipgate to
           continue searching. */
        return shipgate_fw_bb(&ship->sg, pkt, 0, c);
    }

    return rv;
}

static int dcnte_process_game_create(ship_client_t *c,
                                     dcnte_game_create_pkt *pkt) {
    lobby_t *l;
    uint8_t event = ship->game_event;
    char name[32], tmp[19];

    /* Convert the team name to UTF-8 */
    tmp[0] = '\t';
    tmp[1] = 'J';
    memcpy(tmp, pkt->name + 2, 16);
    tmp[18] = 0;

    istrncpy(ic_sjis_to_utf8, name, tmp, 32);

    /* Create the lobby structure. */
    l = lobby_create_game(c->cur_block, name, pkt->password,
                          0, 0, 0, 0, c->version, c->pl->v1.section,
                          event, 0, c, 0);

    /* If we don't have a game, something went wrong... tell the user. */
    if(!l) {
        return send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't create game!"),
                             __(c, "\tC7Try again later."));
    }

    /* We've got a new game, but nobody's in it yet... Lets put the requester
       in the game. */
    if(join_game(c, l)) {
        /* Something broke, destroy the created lobby before anyone tries to
           join it. */
        pthread_rwlock_wrlock(&c->cur_block->lobby_lock);
        lobby_destroy(l);
        pthread_rwlock_unlock(&c->cur_block->lobby_lock);
    }

    /* All is good in the world. */
    return 0;
}

static int dc_process_game_create(ship_client_t *c, dc_game_create_pkt *pkt) {
    lobby_t *l;
    uint8_t event = ship->game_event;
    char name[32], tmp[17];

    /* Check the user's ability to create a game of that difficulty. */
    if(!(c->flags & CLIENT_FLAG_OVERRIDE_GAME)) {
        if((LE32(c->pl->v1.level) + 1) < game_required_level[pkt->difficulty]) {
            return send_message1(c, "%s\n\n%s",
                                 __(c, "\tE\tC4Can't create game!"),
                                 __(c, "\tC7Your level is too\nlow for that\n"
                                    "difficulty."));
        }
    }

    /* Convert the team name to UTF-8 */
    memcpy(tmp, pkt->name, 16);
    tmp[16] = 0;

    if(pkt->name[1] == 'J') {
        istrncpy(ic_sjis_to_utf8, name, tmp, 32);
    }
    else {
        istrncpy(ic_8859_to_utf8, name, tmp, 32);
    }

    /* Create the lobby structure. */
    l = lobby_create_game(c->cur_block, name, pkt->password,
                          pkt->difficulty, pkt->battle, pkt->challenge,
                          pkt->version, c->version, c->pl->v1.section,
                          event, 0, c, 0);

    /* If we don't have a game, something went wrong... tell the user. */
    if(!l) {
        return send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't create game!"),
                             __(c, "\tC7Try again later."));
    }

    /* We've got a new game, but nobody's in it yet... Lets put the requester
       in the game. */
    if(join_game(c, l)) {
        /* Something broke, destroy the created lobby before anyone tries to
           join it. */
        pthread_rwlock_wrlock(&c->cur_block->lobby_lock);
        lobby_destroy(l);
        pthread_rwlock_unlock(&c->cur_block->lobby_lock);
    }

    /* All is good in the world. */
    return 0;
}

static int pc_process_game_create(ship_client_t *c, pc_game_create_pkt *pkt) {
    lobby_t *l = NULL;
    uint8_t event = ship->game_event;
    char name[32], password[16];

    /* Convert the name/password to the appropriate encoding. */
    istrncpy16(ic_utf16_to_utf8, name, pkt->name, 32);
    istrncpy16(ic_utf16_to_ascii, password, pkt->password, 16);

    /* Check the user's ability to create a game of that difficulty. */
    if(!(c->flags & CLIENT_FLAG_OVERRIDE_GAME)) {
        if((LE32(c->pl->v1.level) + 1) < game_required_level[pkt->difficulty]) {
            return send_message1(c, "%s\n\n%s",
                                 __(c, "\tE\tC4Can't create game!"),
                                 __(c, "\tC7Your level is too\nlow for that\n"
                                    "difficulty."));
        }
    }

    /* Create the lobby structure. */
    l = lobby_create_game(c->cur_block, name, password, pkt->difficulty,
                          pkt->battle, pkt->challenge, 1, c->version,
                          c->pl->v1.section, event, 0, c, 0);

    /* If we don't have a game, something went wrong... tell the user. */
    if(!l) {
        return send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't create game!"),
                             __(c, "\tC7Try again later."));
    }

    /* If its a non-challenge, non-battle, non-ultimate game, ask the user if
       they want v1 compatibility or not. */
    if(!pkt->battle && !pkt->challenge && pkt->difficulty != 3 &&
       !(c->flags & CLIENT_FLAG_IS_NTE)) {
        c->create_lobby = l;
        return send_pc_game_type_sel(c);
    }

    /* We've got a new game, but nobody's in it yet... Lets put the requester
       in the game (as long as we're still here). */
    if(join_game(c, l)) {
        /* Something broke, destroy the created lobby before anyone tries to
           join it. */
        pthread_rwlock_wrlock(&c->cur_block->lobby_lock);
        lobby_destroy(l);
        pthread_rwlock_unlock(&c->cur_block->lobby_lock);
    }

    /* All is good in the world. */
    return 0;
}

static int gc_process_game_create(ship_client_t *c, gc_game_create_pkt *pkt) {
    lobby_t *l;
    uint8_t event = ship->game_event;
    char name[32], tmp[17];

    /* Check the user's ability to create a game of that difficulty. */
    if(!(c->flags & CLIENT_FLAG_OVERRIDE_GAME)) {
        if((LE32(c->pl->v1.level) + 1) < game_required_level[pkt->difficulty]) {
            return send_message1(c, "%s\n\n%s",
                                 __(c, "\tE\tC4Can't create game!"),
                                 __(c, "\tC7Your level is too\nlow for that\n"
                                    "difficulty."));
        }
    }

    /* Convert the team name to UTF-8 */
    memcpy(tmp, pkt->name, 16);
    tmp[16] = 0;

    if(pkt->name[1] == 'J') {
        istrncpy(ic_sjis_to_utf8, name, tmp, 32);
    }
    else {
        istrncpy(ic_8859_to_utf8, name, tmp, 32);
    }

    /* Create the lobby structure. */
    l = lobby_create_game(c->cur_block, name, pkt->password,
                          pkt->difficulty, pkt->battle, pkt->challenge,
                          0, c->version, c->pl->v1.section, event,
                          pkt->episode, c, 0);

    /* If we don't have a game, something went wrong... tell the user. */
    if(!l) {
        return send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't create game!"),
                             __(c, "\tC7Try again later."));
    }

    /* We've got a new game, but nobody's in it yet... Lets put the requester
       in the game. */
    if(join_game(c, l)) {
        /* Something broke, destroy the created lobby before anyone tries to
           join it. */
        pthread_rwlock_wrlock(&c->cur_block->lobby_lock);
        lobby_destroy(l);
        pthread_rwlock_unlock(&c->cur_block->lobby_lock);
    }

    /* All is good in the world. */
    return 0;
}

static int ep3_process_game_create(ship_client_t *c, ep3_game_create_pkt *pkt) {
    lobby_t *l;
    char name[32], tmp[17];

    /* Convert the team name to UTF-8 */
    memcpy(tmp, pkt->name, 16);
    tmp[16] = 0;

    if(pkt->name[1] == 'J') {
        istrncpy(ic_sjis_to_utf8, name, tmp, 32);
    }
    else {
        istrncpy(ic_8859_to_utf8, name, tmp, 32);
    }

    /* Create the lobby structure. */
    l = lobby_create_ep3_game(c->cur_block, name, pkt->password,
                              pkt->view_battle, c->pl->v1.section, c);

    /* If we don't have a game, something went wrong... tell the user. */
    if(!l) {
        return send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't create game!"),
                             __(c, "\tC7Try again later."));
    }

    /* We've got a new game, but nobody's in it yet... Lets put the requester
       in the game. */
    if(join_game(c, l)) {
        /* Something broke, destroy the created lobby before anyone tries to
           join it. */
        pthread_rwlock_wrlock(&c->cur_block->lobby_lock);
        lobby_destroy(l);
        pthread_rwlock_unlock(&c->cur_block->lobby_lock);
    }

    /* All is good in the world. */
    return 0;
}

static int bb_process_game_create(ship_client_t *c, bb_game_create_pkt *pkt) {
    lobby_t *l;
    uint8_t event = ship->game_event;
    char name[65], passwd[65];

    if(pkt->battle || pkt->challenge) {
        return send_message1(c, "%s\n%s",
                             __(c, "\tE\tC4Can't create game!"),
                             __(c, "\tC7Battle and Challenge\nmodes are not\n"
                                "currently supported on\nBlue Burst!"));
    }

    /* Check the user's ability to create a game of that difficulty. */
    if(!(c->flags & CLIENT_FLAG_OVERRIDE_GAME)) {
        if((LE32(c->pl->v1.level) + 1) < game_required_level[pkt->difficulty]) {
            return send_message1(c, "%s\n\n%s",
                                 __(c, "\tE\tC4Can't create game!"),
                                 __(c, "\tC7Your level is too\nlow for that\n"
                                    "difficulty."));
        }
    }

    /* Convert the team name/password to UTF-8 */
    istrncpy16(ic_utf16_to_utf8, name, pkt->name, 64);
    istrncpy16(ic_utf16_to_utf8, passwd, pkt->password, 64);

    /* Create the lobby structure. */
    l = lobby_create_game(c->cur_block, name, passwd, pkt->difficulty,
                          pkt->battle, pkt->challenge, 0, c->version,
                          c->pl->bb.character.section, event, pkt->episode, c,
                          pkt->single_player);

    /* If we don't have a game, something went wrong... tell the user. */
    if(!l) {
        return send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't create game!"),
                             __(c, "\tC7Try again later."));
    }

    /* We've got a new game, but nobody's in it yet... Lets put the requester
       in the game. */
    if(join_game(c, l)) {
        /* Something broke, destroy the created lobby before anyone tries to
           join it. */
        pthread_rwlock_wrlock(&c->cur_block->lobby_lock);
        lobby_destroy(l);
        pthread_rwlock_unlock(&c->cur_block->lobby_lock);
    }

    /* All is good in the world. */
    return 0;
}

/* Process a client's done bursting signal. */
static int dc_process_done_burst(ship_client_t *c) {
    lobby_t *l = c->cur_lobby;
    int rv;

    /* Sanity check... Is the client in a game lobby? */
    if(!l || l->type == LOBBY_TYPE_DEFAULT) {
        return -1;
    }

    /* Lock the lobby, clear its bursting flag, send the resume game packet to
       the rest of the lobby, and continue on. */
    pthread_mutex_lock(&l->mutex);

    /* Handle the end of burst stuff with the lobby */
    if(!(l->flags & LOBBY_FLAG_QUESTING)) {
        l->flags &= ~LOBBY_FLAG_BURSTING;
        c->flags &= ~CLIENT_FLAG_BURSTING;

        if(l->version == CLIENT_VERSION_BB) {
            send_lobby_end_burst(l);
        }

        rv = send_simple(c, PING_TYPE, 0) | lobby_handle_done_burst(l, c);
    }
    else {
        rv = send_quest_one(l, c, l->qid, l->qlang);
        c->flags |= CLIENT_FLAG_WAIT_QPING;
        rv |= send_simple(c, PING_TYPE, 0);
    }

    pthread_mutex_unlock(&l->mutex);

    return rv;
}

static int process_gm_menu(ship_client_t *c, uint32_t menu_id,
                           uint32_t item_id) {
    if(!LOCAL_GM(c)) {
        return send_message1(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    switch(menu_id) {
        case MENU_ID_GM:
            switch(item_id) {
                case ITEM_ID_GM_REF_QUESTS:
                    return refresh_quests(c, send_message1);

                case ITEM_ID_GM_REF_GMS:
                    return refresh_gms(c, send_message1);

                case ITEM_ID_GM_REF_LIMITS:
                    return refresh_limits(c, send_message1);

                case ITEM_ID_GM_RESTART:
                case ITEM_ID_GM_SHUTDOWN:
                case ITEM_ID_GM_GAME_EVENT:
                case ITEM_ID_GM_LOBBY_EVENT:
                    return send_gm_menu(c, MENU_ID_GM | (item_id << 8));
            }

            break;

        case MENU_ID_GM_SHUTDOWN:
        case MENU_ID_GM_RESTART:
            return schedule_shutdown(c, item_id, menu_id == MENU_ID_GM_RESTART,
                                     send_message1);

        case MENU_ID_GM_GAME_EVENT:
            if(item_id < 7) {
                ship->game_event = item_id;
                return send_message1(c, "%s", __(c, "\tE\tC7Game Event set."));
            }

            break;

        case MENU_ID_GM_LOBBY_EVENT:
            if(item_id < 15) {
                ship->lobby_event = item_id;
                update_lobby_event();
                return send_message1(c, "%s", __(c, "\tE\tC7Event set."));
            }

            break;
    }

    return send_message1(c, "%s", __(c, "\tE\tC4Huh?"));
}

static int process_menu(ship_client_t *c, uint32_t menu_id, uint32_t item_id,
                        const uint8_t *passwd, uint16_t passwd_len) {
    /* Figure out what the client is selecting. */
    switch(menu_id & 0xFF) {
        /* Lobby Information Desk */
        case MENU_ID_INFODESK:
        {
            FILE *fp;
            char buf[1024];
            long len;

            /* The item_id should be the information the client wants. */
            if(item_id >= ship->cfg->info_file_count) {
                send_message1(c, "%s\n\n%s",
                              __(c, "\tE\tC4That information is\nclassified!"),
                              __(c, "\tC7Nah, it just doesn't\nexist, sorry."));
                return 0;
            }

            /* Attempt to open the file */
            fp = fopen(ship->cfg->info_files[item_id].filename, "r");

            if(!fp) {
                send_message1(c, "%s\n\n%s",
                              __(c, "\tE\tC4That information is\nclassified!"),
                              __(c, "\tC7Nah, it just doesn't\nexist, sorry."));
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
            return send_message_box(c, "%s", buf);
        }

        /* Blocks */
        case MENU_ID_BLOCK:
        {
            uint16_t port;

            /* See if it's the "Ship Select" entry */
            if(item_id == 0xFFFFFFFF) {
                return send_ship_list(c, ship, ship->cfg->menu_code);
            }

            /* Make sure the block selected is in range. */
            if(item_id > ship->cfg->blocks) {
                return -1;
            }

            /* Make sure that block is up and running. */
            if(ship->blocks[item_id - 1] == NULL  ||
               ship->blocks[item_id - 1]->run == 0) {
                return -2;
            }

            switch(c->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                    port = ship->blocks[item_id - 1]->dc_port;
                    break;

                case CLIENT_VERSION_PC:
                    port = ship->blocks[item_id - 1]->pc_port;
                    break;

                case CLIENT_VERSION_GC:
                    port = ship->blocks[item_id - 1]->gc_port;
                    break;

                case CLIENT_VERSION_EP3:
                    port = ship->blocks[item_id - 1]->ep3_port;
                    break;

                case CLIENT_VERSION_BB:
                    port = ship->blocks[item_id - 1]->bb_port;
                    break;

                default:
                    return -1;
            }

            /* Redirect the client where we want them to go. */
#ifdef SYLVERANT_ENABLE_IPV6
            if(c->flags & CLIENT_FLAG_IPV6) {
                return send_redirect6(c, ship_ip6, port);
            }
            else {
                return send_redirect(c, ship_ip4, port);
            }
#else
            return send_redirect(c, ship_ip4, port);
#endif
        }

        /* Game Selection */
        case MENU_ID_GAME:
        {
            char passwd_cmp[17];
            lobby_t *l;
            int override = c->flags & CLIENT_FLAG_OVERRIDE_GAME;

            memset(passwd_cmp, 0, 17);

            /* Read the password, if the client provided one. */
            if(c->version == CLIENT_VERSION_PC ||
               c->version == CLIENT_VERSION_BB) {
                char tmp[32];

                if(passwd_len > 0x20) {
                    return -1;
                }

                memset(tmp, 0, 32);
                memcpy(tmp, passwd, passwd_len);
                istrncpy16(ic_utf16_to_ascii, passwd_cmp, (uint16_t *)tmp, 16);
            }
            else {
                if(passwd_len > 0x10) {
                    return -1;
                }

                memcpy(passwd_cmp, passwd, passwd_len);
            }

            /* The client is selecting a game to join. */
            l = block_get_lobby(c->cur_block, item_id);

            if(!l) {
                /* The lobby has disappeared. */
                send_message1(c, "%s\n\n%s", __(c, "\tE\tC4Can't join game!"),
                              __(c, "\tC7This game is\nnon-existant."));
                return 0;
            }

            /* Check the provided password (if any). */
            if(!override) {
                if(l->passwd[0] && strcmp(passwd_cmp, l->passwd)) {
                    send_message1(c, "%s\n\n%s",
                                  __(c, "\tE\tC4Can't join game!"),
                                  __(c, "\tC7Wrong Password."));
                    return 0;
                }
            }

            /* Attempt to change the player's lobby. */
            join_game(c, l);

            return 0;
        }

        /* Quest category */
        case MENU_ID_QCATEGORY:
        {
            int rv;
            int lang;

            pthread_rwlock_rdlock(&ship->qlock);

            /* Do we have quests configured? */
            if(!TAILQ_EMPTY(&ship->qmap)) {
                lang = (menu_id >> 24) & 0xFF;
                rv = send_quest_list(c, (int)item_id, lang);
            }
            else {
                rv = send_message1(c, "%s", __(c, "\tE\tC4Quests not\n"
                                               "configured."));
            }

            pthread_rwlock_unlock(&ship->qlock);
            return rv;
        }

        /* Quest */
        case MENU_ID_QUEST:
        {
            int lang = (menu_id >> 24) & 0xFF;
            lobby_t *l = c->cur_lobby;

            if(l->flags & LOBBY_FLAG_BURSTING) {
                return send_message1(c, "%s",
                                     __(c, "\tE\tC4Please wait a moment."));
            }

            return lobby_setup_quest(l, c, item_id, lang);
        }

        /* Ship */
        case MENU_ID_SHIP:
        {
            miniship_t *i;
            int off = 0;

            /* See if the user picked a Ship List item */
            if(item_id == 0) {
                return send_ship_list(c, ship, (uint16_t)(menu_id >> 8));
            }

            switch(c->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                    off = 0;
                    break;

                case CLIENT_VERSION_PC:
                    off = 1;
                    break;

                case CLIENT_VERSION_GC:
                    off = 2;
                    break;

                case CLIENT_VERSION_EP3:
                    off = 3;
                    break;

                case CLIENT_VERSION_BB:
                    off = 4;
                    break;
            }

            /* Go through all the ships that we know about looking for the one
               that the user has requested. */
            TAILQ_FOREACH(i, &ship->ships, qentry) {
                if(i->ship_id == item_id) {
#ifdef SYLVERANT_ENABLE_IPV6
                    if(c->flags & CLIENT_FLAG_IPV6 && i->ship_addr6[0]) {
                        return send_redirect6(c, i->ship_addr6,
                                              i->ship_port + off);
                    }
                    else {
                        return send_redirect(c, i->ship_addr,
                                             i->ship_port + off);
                    }
#else
                    return send_redirect(c, i->ship_addr, i->ship_port + off);
#endif
                }
            }

            /* We didn't find it, punt. */
            return send_message1(c, "%s",
                                 __(c, "\tE\tC4That ship is now\noffline."));
        }

        /* Game type (PSOPC only) */
        case MENU_ID_GAME_TYPE:
        {
            lobby_t *l = c->create_lobby;

            if(l) {
                if(item_id == 0) {
                    l->v2 = 0;
                    l->version = CLIENT_VERSION_DCV1;
                }
                else if(item_id == 2) {
                    l->flags |= LOBBY_FLAG_PCONLY;
                }

                /* Add the lobby to the list of lobbies on the block. */
                pthread_rwlock_wrlock(&c->cur_block->lobby_lock);
                TAILQ_INSERT_TAIL(&c->cur_block->lobbies, l, qentry);
                ship_inc_games(ship);
                ++c->cur_block->num_games;
                pthread_rwlock_unlock(&c->cur_block->lobby_lock);
                c->create_lobby = NULL;

                /* Add the user to the lobby... */
                if(join_game(c, l)) {
                    /* Something broke, destroy the created lobby before anyone
                       tries to join it. */
                    pthread_rwlock_wrlock(&c->cur_block->lobby_lock);
                    lobby_destroy(l);
                    pthread_rwlock_unlock(&c->cur_block->lobby_lock);
                }

                /* All's well in the world if we get here. */
                return 0;
            }

            return send_message1(c, "%s", __(c, "\tE\tC4Huh?"));
        }

        /* GM Menu */
        case MENU_ID_GM:
            return process_gm_menu(c, menu_id, item_id);

        default:
            if(script_execute(ScriptActionUnknownMenu, c, SCRIPT_ARG_PTR, c,
                              SCRIPT_ARG_UINT32, menu_id, SCRIPT_ARG_UINT32,
                              item_id, SCRIPT_ARG_END) > 0)
                return 0;
    }

    return -1;
}

static int dc_process_menu(ship_client_t *c, dc_select_pkt *pkt) {
    uint32_t menu_id = LE32(pkt->menu_id);
    uint32_t item_id = LE32(pkt->item_id);
    uint8_t *bp = (uint8_t *)pkt;
    uint16_t len = LE16(pkt->hdr.dc.pkt_len) - 0x0C;

    return process_menu(c, menu_id, item_id, bp + 0x0C, len);
}

static int bb_process_menu(ship_client_t *c, bb_select_pkt *pkt) {
    uint32_t menu_id = LE32(pkt->menu_id);
    uint32_t item_id = LE32(pkt->item_id);
    uint8_t *bp = (uint8_t *)pkt;
    uint16_t len = LE16(pkt->hdr.pkt_len) - 0x10;

    return process_menu(c, menu_id, item_id, bp + 0x10, len);
}

static int process_info_req(ship_client_t *c, uint32_t menu_id,
                            uint32_t item_id) {
    /* What kind of information do they want? */
    switch(menu_id & 0xFF) {
        /* Block */
        case MENU_ID_BLOCK:
            return block_info_reply(c, item_id);

        /* Game List */
        case MENU_ID_GAME:
            return lobby_info_reply(c, item_id);

        /* Quest */
        case MENU_ID_QUEST:
        {
            int lang = (menu_id >> 24) & 0xFF;
            int rv;

            pthread_rwlock_rdlock(&ship->qlock);

            /* Do we have quests configured? */
            if(!TAILQ_EMPTY(&ship->qmap)) {
                rv = send_quest_info(c->cur_lobby, item_id, lang);
            }
            else {
                rv = send_message1(c, "%s", __(c, "\tE\tC4Quests not\n"
                                               "configured."));
            }

            pthread_rwlock_unlock(&ship->qlock);
            return rv;
        }

        /* Ship */
        case MENU_ID_SHIP:
        {
            miniship_t *i;

            /* Find the ship if its still online */
            TAILQ_FOREACH(i, &ship->ships, qentry) {
                if(i->ship_id == item_id) {
                    char string[256];
                    char tmp[3] = { (char)i->menu_code,
                        (char)(i->menu_code >> 8), 0 };

                    sprintf(string, "%02x:%s%s%s\n%d %s\n%d %s", i->ship_number,
                            tmp, tmp[0] ? "/" : "", i->name, i->clients,
                            __(c, "Users"), i->games, __(c, "Teams"));
                    return send_info_reply(c, string);
                }
            }

            return 0;
        }

        default:
            return -1;
    }
}

static int dc_process_info_req(ship_client_t *c, dc_select_pkt *pkt) {
    uint32_t menu_id = LE32(pkt->menu_id);
    uint32_t item_id = LE32(pkt->item_id);

    return process_info_req(c, menu_id, item_id);
}

static int bb_process_info_req(ship_client_t *c, bb_select_pkt *pkt) {
    uint32_t menu_id = LE32(pkt->menu_id);
    uint32_t item_id = LE32(pkt->item_id);

    return process_info_req(c, menu_id, item_id);
}

/* Process a client's arrow update request. */
static int dc_process_arrow(ship_client_t *c, uint8_t flag) {
    c->arrow = flag;
    return send_lobby_arrows(c->cur_lobby);
}

/* Process a client's trade request. */
static int process_trade(ship_client_t *c, gc_trade_pkt *pkt) {
    lobby_t *l = c->cur_lobby;
    ship_client_t *dest;

    /* Find the destination. */
    dest = l->clients[pkt->who];

    send_simple(dest, TRADE_1_TYPE, 0);
    pkt->hdr.pkt_type = TRADE_3_TYPE;
    send_pkt_dc(dest, (dc_pkt_hdr_t *)pkt);
    return send_simple(dest, TRADE_4_TYPE, 1);
}

/* Process a blacklist update packet. */
static int gc_process_blacklist(ship_client_t *c,
                                gc_blacklist_update_pkt *pkt) {
    memcpy(c->blacklist, pkt->list, 30 * sizeof(uint32_t));
    return send_txt(c, "%s", __(c, "\tE\tC7Updated blacklist."));
}

static int bb_process_blacklist(ship_client_t *c,
                                bb_blacklist_update_pkt *pkt) {
    memcpy(c->blacklist, pkt->list, 28 * sizeof(uint32_t));
    memcpy(c->bb_opts->blocked, pkt->list, 28 * sizeof(uint32_t));
    return send_txt(c, "%s", __(c, "\tE\tC7Updated blacklist."));
}

/* Process an infoboard update packet. */
static int process_infoboard(ship_client_t *c, gc_write_info_pkt *pkt) {
    uint16_t len = LE16(pkt->hdr.dc.pkt_len) - 4;

    if(!c->infoboard || len > 0xAC) {
        return -1;
    }

    memcpy(c->infoboard, pkt->msg, len);
    memset(c->infoboard + len, 0, 0xAC - len);
    return 0;
}

static int process_bb_infoboard(ship_client_t *c, bb_write_info_pkt *pkt) {
    uint16_t len = LE16(pkt->hdr.pkt_len) - 8;

    if(!c->infoboard || len > 0x158) {
        return -1;
    }

    /* BB has this in two places for now... */
    memcpy(c->infoboard, pkt->msg, len);
    memset(c->infoboard + len, 0, 0x158 - len);

    memcpy(c->bb_pl->infoboard, pkt->msg, len);
    memset(((uint8_t *)c->bb_pl->infoboard) + len, 0, 0x158 - len);

    return 0;
}

/* Process a 0xBA packet. */
static int process_ep3_command(ship_client_t *c, const uint8_t *pkt) {
    dc_pkt_hdr_t *hdr = (dc_pkt_hdr_t *)pkt;
    uint16_t len = LE16(hdr->pkt_len);
    uint16_t tmp;

    switch(hdr->flags) {
        case EP3_COMMAND_LEAVE_TEAM:
            /* Make sure the size looks ok... */
            if(len != 0x10)
                return -1;

            tmp = pkt[0x0E] | (pkt[0x0F] << 8);
            return send_ep3_ba01(c, tmp);

        case EP3_COMMAND_JUKEBOX_REQUEST:
            /* Make sure the size looks ok... */
            if(len != 0x10)
                return -1;

            tmp = pkt[0x0E] | (pkt[0x0F] << 8);
            return send_ep3_jukebox_reply(c, tmp);

        default:
            if(!script_execute_pkt(ScriptActionUnknownEp3Packet, c, pkt, len)) {
                debug(DBG_LOG, "Unknown Episode 3 Command: %02x\n", hdr->flags);
                print_packet(pkt, len);
                return -1;
            }
            return 0;
    }
}

/* Process a Blue Burst options update */
static int bb_process_opt_flags(ship_client_t *c, bb_options_update_pkt *pkt) {
    uint16_t len = LE16(pkt->hdr.pkt_len);

    if(len != sizeof(bb_options_update_pkt) + 4) {
        debug(DBG_LOG, "Invalid sized BB options flag update (%d)\n", len);
        return -1;
    }

    memcpy(&c->bb_opts->option_flags, pkt->data, 4);
    return 0;
}

static int bb_process_symbols(ship_client_t *c, bb_options_update_pkt *pkt) {
    uint16_t len = LE16(pkt->hdr.pkt_len);

    if(len != sizeof(bb_options_update_pkt) + 0x04E0) {
        debug(DBG_LOG, "Invalid sized BB symbol chat update (%d)\n", len);
        return -1;
    }

    memcpy(c->bb_opts->symbol_chats, pkt->data, 0x04E0);
    return 0;
}

static int bb_process_shortcuts(ship_client_t *c, bb_options_update_pkt *pkt) {
    uint16_t len = LE16(pkt->hdr.pkt_len);

    if(len != sizeof(bb_options_update_pkt) + 0x0A40) {
        debug(DBG_LOG, "Invalid sized BB chat shortcut update (%d)\n", len);
        return -1;
    }

    memcpy(c->bb_opts->shortcuts, pkt->data, 0x0A40);
    return 0;
}

static int bb_process_keys(ship_client_t *c, bb_options_update_pkt *pkt) {
    uint16_t len = LE16(pkt->hdr.pkt_len);

    if(len != sizeof(bb_options_update_pkt) + 0x016C) {
        debug(DBG_LOG, "Invalid sized BB key config update (%d)\n", len);
        return -1;
    }

    memcpy(c->bb_opts->key_config, pkt->data, 0x016C);
    return 0;
}

static int bb_process_pad(ship_client_t *c, bb_options_update_pkt *pkt) {
    uint16_t len = LE16(pkt->hdr.pkt_len);

    if(len != sizeof(bb_options_update_pkt) + 0x0038) {
        debug(DBG_LOG, "Invalid sized BB pad config update (%d)\n", len);
        return -1;
    }

    memcpy(c->bb_opts->joystick_config, pkt->data, 0x0038);
    return 0;
}

static int bb_process_techs(ship_client_t *c, bb_options_update_pkt *pkt) {
    uint16_t len = LE16(pkt->hdr.pkt_len);

    if(len != sizeof(bb_options_update_pkt) + 0x0028) {
        debug(DBG_LOG, "Invalid sized BB tech menu update (%d)\n", len);
        return -1;
    }

    memcpy(c->bb_pl->tech_menu, pkt->data, 0x0028);
    return 0;
}

static int bb_set_guild_text(ship_client_t *c, bb_guildcard_set_txt_pkt *pkt) {
    uint16_t len = LE16(pkt->hdr.pkt_len);

    if(len != sizeof(bb_guildcard_set_txt_pkt)) {
        debug(DBG_LOG, "Invalid sized BB guildcard text update (%d)\n", len);
        return -1;
    }

    memcpy(c->bb_pl->guildcard_desc, pkt->text, 0x0110);
    return 0;
}

static int bb_process_config(ship_client_t *c, bb_options_update_pkt *pkt) {
    uint16_t len = LE16(pkt->hdr.pkt_len);

    if(len != sizeof(bb_options_update_pkt) + 0x00E8) {
        debug(DBG_LOG, "Invalid sized BB configuration update (%d)\n", len);
        return -1;
    }

    memcpy(c->bb_pl->character.config, pkt->data, 0x00E8);
    return 0;
}

static int process_qload_done(ship_client_t *c) {
    lobby_t *l = c->cur_lobby;
    ship_client_t *c2;
    int i;

    c->flags |= CLIENT_FLAG_QLOAD_DONE;

    /* See if everyone's done now. */
    for(i = 0; i < l->max_clients; ++i) {
        if((c2 = l->clients[i])) {
            /* This client isn't done, so we can end the search now. */
            if(!(c2->flags & CLIENT_FLAG_QLOAD_DONE) &&
               c2->version >= CLIENT_VERSION_GC)
                return 0;
        }
    }

    /* If we get here, everyone's done. Send out the packet to everyone now. */
    for(i = 0; i < l->max_clients; ++i) {
        if((c2 = l->clients[i]) && c2->version >= CLIENT_VERSION_GC) {
            if(send_simple(c2, QUEST_LOAD_DONE_TYPE, 0))
                c2->flags |= CLIENT_FLAG_DISCONNECTED;
        }
    }

    return 0;
}

int send_motd(ship_client_t *c) {
    FILE *fp;
    char buf[1024];
    long len;
    uint32_t lang = (1 << c->q_lang), ver;
    int i, found = 0;
    sylverant_info_file_t *f;

    switch(c->version) {
        case CLIENT_VERSION_DCV1:
            ver = SYLVERANT_INFO_V1;
            break;

        case CLIENT_VERSION_DCV2:
            ver = SYLVERANT_INFO_V2;
            break;

        case CLIENT_VERSION_PC:
            ver = SYLVERANT_INFO_PC;
            break;

        case CLIENT_VERSION_GC:
            ver = SYLVERANT_INFO_GC;
            break;

        default:
            return 0;
    }

    for(i = 0; i < ship->cfg->info_file_count && !found; ++i) {
        f = &ship->cfg->info_files[i];

        if(!f->desc && (f->versions & ver) && (f->languages & lang)) {
            found = 1;
        }
    }

    /* No MOTD found for the given version/language combination. */
    if(!found) {
        return 0;
    }

    /* Attempt to open the file */
    fp = fopen(f->filename, "r");

    /* Can't find the file? Punt. */
    if(!fp) {
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
    return send_message_box(c, "%s", buf);
}

ship_client_t *block_find_client(block_t *b, uint32_t gc) {
    ship_client_t *it;

    pthread_rwlock_rdlock(&b->lock);

    TAILQ_FOREACH(it, b->clients, qentry) {
        if(it->guildcard == gc) {
            pthread_rwlock_unlock(&b->lock);
            return it;
        }
    }

    pthread_rwlock_unlock(&b->lock);
    return NULL;
}

/* Process block commands for a Dreamcast client. */
static int dc_process_pkt(ship_client_t *c, uint8_t *pkt) {
    uint8_t type;
    uint8_t flags;
    uint16_t len;
    dc_pkt_hdr_t *dc = (dc_pkt_hdr_t *)pkt;
    pc_pkt_hdr_t *pc = (pc_pkt_hdr_t *)pkt;
    int rv;

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
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

    switch(type) {
        case LOGIN_8B_TYPE:
            return dcnte_process_login(c, (dcnte_login_8b_pkt *)pkt);

        case LOGIN_93_TYPE:
            return dc_process_login(c, (dc_login_93_pkt *)pkt);

        case CHAR_DATA_TYPE:
            return dc_process_char(c, (dc_char_data_pkt *)pkt);

        case GAME_COMMAND0_TYPE:
            return subcmd_handle_bcast(c, (subcmd_pkt_t *)pkt);

        case GAME_COMMAND2_TYPE:
        case GAME_COMMANDD_TYPE:
            return subcmd_handle_one(c, (subcmd_pkt_t *)pkt);

        case LOBBY_CHANGE_TYPE:
            return dc_process_change_lobby(c, (dc_select_pkt *)pkt);

        case PING_TYPE:
            if(!(c->flags & CLIENT_FLAG_SENT_MOTD)) {
                send_motd(c);
                c->flags |= CLIENT_FLAG_SENT_MOTD;
            }

            /* If they've got the always legit flag set, but we haven't run the
               legit check yet, then do so now. The reason we wait until now is
               so that if they fail the legit check, we can actually inform them
               of that. */
            if((c->flags & CLIENT_FLAG_ALWAYS_LEGIT) &&
               !(c->flags & CLIENT_FLAG_LEGIT)) {
                sylverant_limits_t *limits;

                pthread_rwlock_rdlock(&ship->llock);
                if(!ship->def_limits) {
                    pthread_rwlock_unlock(&ship->llock);
                    c->flags &= ~CLIENT_FLAG_ALWAYS_LEGIT;
                    send_txt(c, "%s", __(c, "\tE\tC7Legit mode not\n"
                                            "available on this\n"
                                            "ship."));
                    return 0;
                }

                limits = ship->def_limits;

                if(client_legit_check(c, limits)) {
                    c->flags &= ~CLIENT_FLAG_ALWAYS_LEGIT;
                    send_txt(c, "%s", __(c, "\tE\tC7You failed the legit "
                                            "check."));
                }
                else {
                    /* Set the flag and retain the limits list on the client. */
                    c->flags |= CLIENT_FLAG_LEGIT;
                    c->limits = retain(limits);
                }

                pthread_rwlock_unlock(&ship->llock);
            }

            if(c->flags & CLIENT_FLAG_WAIT_QPING) {
                lobby_t *l = c->cur_lobby;

                pthread_mutex_lock(&l->mutex);
                l->flags &= ~LOBBY_FLAG_BURSTING;
                c->flags &= ~(CLIENT_FLAG_BURSTING | CLIENT_FLAG_WAIT_QPING);

                rv = lobby_resend_burst(l, c);
                rv = send_simple(c, PING_TYPE, 0) |
                    lobby_handle_done_burst(l, c);
                pthread_mutex_unlock(&l->mutex);
                return rv;
            }

            return 0;

        case TYPE_05:
            /* If we've already gotten one of these, disconnect the client. */
            if(c->flags & CLIENT_FLAG_GOT_05) {
                c->flags |= CLIENT_FLAG_DISCONNECTED;
            }

            c->flags |= CLIENT_FLAG_GOT_05;
            return 0;

        case CHAT_TYPE:
            if(c->version != CLIENT_VERSION_PC) {
                return dc_process_chat(c, (dc_chat_pkt *)pkt);
            }
            else {
                return pc_process_chat(c, (dc_chat_pkt *)pkt);
            }

        case GUILD_SEARCH_TYPE:
            return dc_process_guild_search(c, (dc_guild_search_pkt *)pkt);

        case SIMPLE_MAIL_TYPE:
            if(c->version != CLIENT_VERSION_PC) {
                return dc_process_mail(c, (dc_simple_mail_pkt *)pkt);
            }
            else {
                return pc_process_mail(c, (pc_simple_mail_pkt *)pkt);
            }

        case DC_GAME_CREATE_TYPE:
        case GAME_CREATE_TYPE:
            if(c->version == CLIENT_VERSION_DCV1 &&
               (c->flags & CLIENT_FLAG_IS_NTE)) {
                return dcnte_process_game_create(c,
                                                 (dcnte_game_create_pkt *)pkt);
            }
            else if(c->version != CLIENT_VERSION_PC &&
               c->version != CLIENT_VERSION_GC) {
                return dc_process_game_create(c, (dc_game_create_pkt *)pkt);
            }
            else if(c->version == CLIENT_VERSION_PC) {
                return pc_process_game_create(c, (pc_game_create_pkt *)pkt);
            }
            else {
                return gc_process_game_create(c, (gc_game_create_pkt *)pkt);
            }

        case DONE_BURSTING_TYPE:
            return dc_process_done_burst(c);

        case GAME_LIST_TYPE:
            return send_game_list(c, c->cur_block);

        case MENU_SELECT_TYPE:
            return dc_process_menu(c, (dc_select_pkt *)pkt);

        case LEAVE_GAME_PL_DATA_TYPE:
            return dc_process_char(c, (dc_char_data_pkt *)pkt);

        case LOBBY_INFO_TYPE:
            return send_info_list(c, ship);

        case BLOCK_LIST_REQ_TYPE:
        case DCNTE_BLOCK_LIST_REQ_TYPE:
            return send_block_list(c, ship);

        case INFO_REQUEST_TYPE:
            return dc_process_info_req(c, (dc_select_pkt *)pkt);

        case QUEST_LIST_TYPE:
            pthread_rwlock_rdlock(&ship->qlock);
            pthread_mutex_lock(&c->cur_lobby->mutex);

            /* Do we have quests configured? */
            if(!TAILQ_EMPTY(&ship->qmap)) {
                c->cur_lobby->flags |= LOBBY_FLAG_QUESTSEL;
                rv = send_quest_categories(c, c->q_lang);
            }
            else {
                rv = send_message1(c, "%s", __(c, "\tE\tC4Quests not\n"
                                               "configured."));
            }

            pthread_mutex_unlock(&c->cur_lobby->mutex);
            pthread_rwlock_unlock(&ship->qlock);
            return rv;

        case QUEST_END_LIST_TYPE:
            pthread_mutex_lock(&c->cur_lobby->mutex);
            c->cur_lobby->flags &= ~LOBBY_FLAG_QUESTSEL;
            pthread_mutex_unlock(&c->cur_lobby->mutex);
            return 0;

        case LOGIN_9D_TYPE:
            return dcv2_process_login(c, (dcv2_login_9d_pkt *)pkt);

        case LOBBY_NAME_TYPE:
            return send_lobby_name(c, c->cur_lobby);

        case LOBBY_ARROW_CHANGE_TYPE:
            return dc_process_arrow(c, flags);

        case SHIP_LIST_TYPE:
        case DCNTE_SHIP_LIST_TYPE:
            return send_ship_list(c, ship, ship->cfg->menu_code);

        case CHOICE_OPTION_TYPE:
            return send_choice_search(c);

        case CHOICE_SETTING_TYPE:
            /* Ignore these for now. */
            return 0;

        case CHOICE_SEARCH_TYPE:
            return send_choice_reply(c, (dc_choice_set_pkt *)pkt);

        case LOGIN_9E_TYPE:
            return gc_process_login(c, (gc_login_9e_pkt *)pkt);

        case QUEST_CHUNK_TYPE:
        case QUEST_FILE_TYPE:
            /* Uhh... Ignore these for now, we've already sent it by the time we
               get this packet from the client. */
            return 0;

        case QUEST_LOAD_DONE_TYPE:
            return process_qload_done(c);

        case INFOBOARD_WRITE_TYPE:
            return process_infoboard(c, (gc_write_info_pkt *)pkt);

        case INFOBOARD_TYPE:
            return send_infoboard(c, c->cur_lobby);

        case TRADE_0_TYPE:
            return process_trade(c, (gc_trade_pkt *)pkt);

        case TRADE_2_TYPE:
            /* Ignore. */
            return 0;

        case GC_MSG_BOX_CLOSED_TYPE:
            /* Ignore. */
            return 0;

        case BLACKLIST_TYPE:
            return gc_process_blacklist(c, (gc_blacklist_update_pkt *)pkt);

        case AUTOREPLY_SET_TYPE:
            return client_set_autoreply(c, ((autoreply_set_pkt *)dc)->msg,
                                        len - 4);

        case AUTOREPLY_CLEAR_TYPE:
            return client_disable_autoreply(c);

        case GAME_COMMAND_C9_TYPE:
        case GAME_COMMAND_CB_TYPE:
            return subcmd_handle_ep3_bcast(c, (subcmd_pkt_t *)pkt);

        case EP3_COMMAND_TYPE:
            return process_ep3_command(c, pkt);

        case EP3_SERVER_DATA_TYPE:
            debug(DBG_LOG, "Ep3 Server Data from %s (%d)\n", c->pl->v1.name,
                  c->guildcard);
            print_packet((unsigned char *)pkt, len);
            return 0;

        case EP3_MENU_CHANGE_TYPE:
            if(dc->flags != 0) {
                return send_simple(c, EP3_MENU_CHANGE_TYPE, 0);
            }
            return 0;

        case EP3_GAME_CREATE_TYPE:
            return ep3_process_game_create(c, (ep3_game_create_pkt *)pkt);

        case QUEST_STATS_TYPE:
            debug(DBG_LOG, "Received quest stats packet from %s (%d)\n",
                  c->pl->v1.name, c->guildcard);
            print_packet((unsigned char *)pkt, len);
            return 0;

        default:
            if(!script_execute_pkt(ScriptActionUnknownBlockPacket, c, pkt,
                                   len)) {
                debug(DBG_LOG, "Unknown packet!\n");
                print_packet((unsigned char *)pkt, len);
                return -3;
            }
            return 0;
    }
}

static int bb_process_pkt(ship_client_t *c, uint8_t *pkt) {
    bb_pkt_hdr_t *hdr = (bb_pkt_hdr_t *)pkt;
    uint16_t type = LE16(hdr->pkt_type);
    uint16_t len = LE16(hdr->pkt_len);
    uint32_t flags = LE32(hdr->flags);
    int rv;

    switch(type) {
        case PING_TYPE:
            return 0;

        case TYPE_05:
            c->flags |= CLIENT_FLAG_DISCONNECTED;
            return 0;

        case LOGIN_93_TYPE:
            return bb_process_login(c, (bb_login_93_pkt *)pkt);

        case CHAR_DATA_TYPE:
        case LEAVE_GAME_PL_DATA_TYPE:
            return bb_process_char(c, (bb_char_data_pkt *)pkt);

        case GAME_COMMAND0_TYPE:
            return subcmd_bb_handle_bcast(c, (bb_subcmd_pkt_t *)pkt);

        case GAME_COMMAND2_TYPE:
        case GAME_COMMANDD_TYPE:
            return subcmd_bb_handle_one(c, (bb_subcmd_pkt_t *)pkt);

        case CHAT_TYPE:
            return bb_process_chat(c, (bb_chat_pkt *)pkt);

        case BLOCK_LIST_REQ_TYPE:
            return send_block_list(c, ship);

        case INFO_REQUEST_TYPE:
            return bb_process_info_req(c, (bb_select_pkt *)pkt);

        case MENU_SELECT_TYPE:
            return bb_process_menu(c, (bb_select_pkt *)pkt);

        case SHIP_LIST_TYPE:
            return send_ship_list(c, ship, ship->cfg->menu_code);

        case LOBBY_CHANGE_TYPE:
            return bb_process_change_lobby(c, (bb_select_pkt *)pkt);

        case BB_UPDATE_OPTION_FLAGS:
            return bb_process_opt_flags(c, (bb_options_update_pkt *)pkt);

        case BB_UPDATE_SYMBOL_CHAT:
            return bb_process_symbols(c, (bb_options_update_pkt *)pkt);

        case BB_UPDATE_SHORTCUTS:
            return bb_process_shortcuts(c, (bb_options_update_pkt *)pkt);

        case BB_UPDATE_KEY_CONFIG:
            return bb_process_keys(c, (bb_options_update_pkt *)pkt);

        case BB_UPDATE_PAD_CONFIG:
            return bb_process_pad(c, (bb_options_update_pkt *)pkt);

        case BB_UPDATE_TECH_MENU:
            return bb_process_techs(c, (bb_options_update_pkt *)pkt);

        case BB_UPDATE_CONFIG:
            return bb_process_config(c, (bb_options_update_pkt *)pkt);

        case LOBBY_ARROW_CHANGE_TYPE:
            return dc_process_arrow(c, (uint8_t)flags);

        case BB_ADD_GUILDCARD_TYPE:
        case BB_DEL_GUILDCARD_TYPE:
        case BB_SORT_GUILDCARD_TYPE:
        case BB_ADD_BLOCKED_USER_TYPE:
        case BB_DEL_BLOCKED_USER_TYPE:
        case BB_SET_GUILDCARD_COMMENT_TYPE:
            /* Let the shipgate deal with these... */
            shipgate_fw_bb(&ship->sg, pkt, 0, c);
            return 0;

        case BLACKLIST_TYPE:
            return bb_process_blacklist(c, (bb_blacklist_update_pkt *)pkt);

        case SIMPLE_MAIL_TYPE:
            return bb_process_mail(c, (bb_simple_mail_pkt *)pkt);

        case AUTOREPLY_SET_TYPE:
            return client_set_autoreply(c, ((bb_autoreply_set_pkt *)pkt)->msg,
                                        len - 8);

        case AUTOREPLY_CLEAR_TYPE:
            return client_disable_autoreply(c);

        case INFOBOARD_WRITE_TYPE:
            return process_bb_infoboard(c, (bb_write_info_pkt *)pkt);

        case INFOBOARD_TYPE:
            return send_infoboard(c, c->cur_lobby);

        case GUILD_SEARCH_TYPE:
            return bb_process_guild_search(c, (bb_guild_search_pkt *)pkt);

        case BB_SET_GUILDCARD_TEXT_TYPE:
            return bb_set_guild_text(c, (bb_guildcard_set_txt_pkt *)pkt);

        case BB_FULL_CHARACTER_TYPE:
            /* Ignore for now... */
            return 0;

        case GAME_LIST_TYPE:
            return send_game_list(c, c->cur_block);

        case DONE_BURSTING_TYPE:
            return dc_process_done_burst(c);

        case GAME_CREATE_TYPE:
            return bb_process_game_create(c, (bb_game_create_pkt *)pkt);

        case LOBBY_NAME_TYPE:
            return send_lobby_name(c, c->cur_lobby);

        case QUEST_LIST_TYPE:
            pthread_rwlock_rdlock(&ship->qlock);
            pthread_mutex_lock(&c->cur_lobby->mutex);

            /* Do we have quests configured? */
            if(!TAILQ_EMPTY(&ship->qmap)) {
                c->cur_lobby->flags |= LOBBY_FLAG_QUESTSEL;
                rv = send_quest_categories(c, c->q_lang);
            }
            else {
                rv = send_message1(c, "%s", __(c, "\tE\tC4Quests not\n"
                                               "configured."));
            }

            pthread_mutex_unlock(&c->cur_lobby->mutex);
            pthread_rwlock_unlock(&ship->qlock);
            return rv;

        case QUEST_END_LIST_TYPE:
            pthread_mutex_lock(&c->cur_lobby->mutex);
            c->cur_lobby->flags &= ~LOBBY_FLAG_QUESTSEL;
            pthread_mutex_unlock(&c->cur_lobby->mutex);
            return 0;

        case QUEST_CHUNK_TYPE:
        case QUEST_FILE_TYPE:
            /* Uhh... Ignore these for now, we've already sent it by the time we
               get this packet from the client. */
            return 0;

        case QUEST_LOAD_DONE_TYPE:
            /* XXXX: This isn't right... we need to synchronize this. */
            return send_simple(c, QUEST_LOAD_DONE_TYPE, 0);

        case DONE_BURSTING_TYPE | 0x0100:
            return 0;

        default:
            if(!script_execute_pkt(ScriptActionUnknownBlockPacket, c, pkt,
                                   len)) {
                debug(DBG_LOG, "Unknown packet!\n");
                print_packet((unsigned char *)pkt, len);
                return -3;
            }
            return 0;
    }
}

/* Process any packet that comes into a block. */
int block_process_pkt(ship_client_t *c, uint8_t *pkt) {
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            return dc_process_pkt(c, pkt);

        case CLIENT_VERSION_BB:
            return bb_process_pkt(c, pkt);
    }

    return -1;
}
