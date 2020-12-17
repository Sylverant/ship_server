/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2012, 2013, 2016, 2018, 2019,
                  2020 Lawrence Sebald

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
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>

#include <sylverant/debug.h>

#include "ship.h"
#include "clients.h"
#include "ship_packets.h"
#include "shipgate.h"
#include "utils.h"
#include "bans.h"
#include "scripts.h"
#include "admin.h"

#ifdef ENABLE_LUA
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#endif

extern int enable_ipv6;
extern uint32_t ship_ip4;
extern uint8_t ship_ip6[16];

miniship_t *ship_find_ship(ship_t *s, uint32_t sid) {
    miniship_t *i;

    TAILQ_FOREACH(i, &s->ships, qentry) {
        if(i->ship_id == sid) {
            return i;
        }
    }

    return NULL;
}

static void clean_shiplist(ship_t *s) {
    miniship_t *i, *tmp;

    i = TAILQ_FIRST(&s->ships);
    while(i) {
        tmp = TAILQ_NEXT(i, qentry);
        free(i);
        i = tmp;
    }

    free(s->menu_codes);
}

static sylverant_event_t *find_current_event(ship_t *s) {
    time_t now;
    int i;
    struct tm tm_now;
    int m, d;

    if(s->cfg->event_count == 1) {
        return s->cfg->events;
    }

    /* If we have more than one event, then its a bit more work... */
    now = time(NULL);
    gmtime_r(&now, &tm_now);
    m = tm_now.tm_mon + 1;
    d = tm_now.tm_mday;

    for(i = 1; i < s->cfg->event_count; ++i) {
        /* Are we completely outside the month(s) of the event? */
        if(s->cfg->events[i].start_month <= s->cfg->events[i].end_month &&
           (m < s->cfg->events[i].start_month ||
            m > s->cfg->events[i].end_month)) {
            continue;
        }
        /* We need a special case for events that span the end of a year... */
        else if(s->cfg->events[i].start_month > s->cfg->events[i].end_month &&
                m > s->cfg->events[i].end_month &&
                m < s->cfg->events[i].start_month) {
            continue;
        }
        /* If we're in the start month, are we before the start day? */
        else if(m == s->cfg->events[i].start_month &&
                d < s->cfg->events[i].start_day) {
            continue;
        }
        /* If we're in the end month, are we after the end day? */
        else if(m == s->cfg->events[i].end_month &&
                d > s->cfg->events[i].end_day) {
            continue;
        }

        /* This is the event we're looking for! */
        return &s->cfg->events[i];
    }

    /* No events matched, so use the default event. */
    return s->cfg->events;
}

static void *ship_thd(void *d) {
    int i, nfds;
    ship_t *s = (ship_t *)d;
    struct timeval timeout;
    fd_set readfds, writefds;
    ship_client_t *it, *tmp;
    socklen_t len;
    struct sockaddr_storage addr;
    struct sockaddr *addr_p = (struct sockaddr *)&addr;
    char ipstr[INET6_ADDRSTRLEN];
    int sock, rv;
    ssize_t sent;
    time_t now;
    time_t last_ban_sweep = time(NULL);
    int numsocks = 1;
    sylverant_event_t *event, *oldevent = s->cfg->events;

#ifdef SYLVERANT_ENABLE_IPV6
    if(enable_ipv6) {
        numsocks = 2;
    }
#endif

    /* Fire up the threads for each block. */
    for(i = 1; i <= s->cfg->blocks; ++i) {
        s->blocks[i - 1] = block_server_start(s, i, s->cfg->base_port +
                                              (i * 6));
    }

    /* We've now started up completely, so run the startup script, if one is
       configured. */
    script_execute(ScriptActionStartup, NULL, SCRIPT_ARG_PTR, s, 0);

    /* While we're still supposed to run... do it. */
    while(s->run) {
        /* Clear the fd_sets so we can use them again. */
        nfds = 0;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        timeout.tv_sec = 30;
        timeout.tv_usec = 0;
        now = time(NULL);

        /* Break out if we're shutting down now */
        if(s->shutdown_time && s->shutdown_time <= now) {
            s->run = 0;
            break;
        }

        /* If we haven't swept the bans list in the last day, do it now. */
        if((last_ban_sweep + 3600 * 24) <= now) {
            ban_sweep(s);
            last_ban_sweep = now = time(NULL);
        }

        /* If the shipgate isn't there, attempt to reconnect */
        if(s->sg.sock == -1 && s->sg.login_attempt < now) {
            if(shipgate_reconnect(&s->sg)) {
                /* Set the next login attempt to ~15 seconds from now... */
                s->sg.login_attempt = now + 14;
                timeout.tv_sec = 15;
            }
            else {
                s->sg.login_attempt = 0;
            }
        }

        /* Check the event to see if its changed on us... */
        event = find_current_event(s);

        if(event != oldevent) {
            debug(DBG_LOG, "Changing event (lobby: %d, game: %d)\n",
                  (int)event->lobby_event, (int)event->game_event);

            if(event->lobby_event != 0xFF) {
                s->lobby_event = event->lobby_event;
                update_lobby_event();
            }

            if(event->game_event != 0xFF) {
                s->game_event = event->game_event;
            }

            oldevent = event;
        }

        /* Fill the sockets into the fd_sets so we can use select below. */
        TAILQ_FOREACH(it, s->clients, qentry) {
            /* If we haven't heard from a client in 2 minutes, its dead.
               Disconnect it. */
            if(now > it->last_message + 120) {
                it->flags |= CLIENT_FLAG_DISCONNECTED;
                continue;
            }
            /* Otherwise, if we haven't heard from them in a minute, ping it. */
            else if(now > it->last_message + 60 && now > it->last_sent + 10) {
                if(send_simple(it, PING_TYPE, 0)) {
                    it->flags |= CLIENT_FLAG_DISCONNECTED;
                    continue;
                }

                it->last_sent = now;
            }

            FD_SET(it->sock, &readfds);

            /* Only add to the write fd set if we have something to send out. */
            if(it->sendbuf_cur) {
                FD_SET(it->sock, &writefds);
            }

            nfds = nfds > it->sock ? nfds : it->sock;
        }

        /* Add the listening sockets to the read fd_set. */
        for(i = 0; i < numsocks; ++i) {
            FD_SET(s->dcsock[i], &readfds);
            nfds = nfds > s->dcsock[i] ? nfds : s->dcsock[i];
            FD_SET(s->pcsock[i], &readfds);
            nfds = nfds > s->pcsock[i] ? nfds : s->pcsock[i];
            FD_SET(s->gcsock[i], &readfds);
            nfds = nfds > s->gcsock[i] ? nfds : s->gcsock[i];
            FD_SET(s->ep3sock[i], &readfds);
            nfds = nfds > s->ep3sock[i] ? nfds : s->ep3sock[i];
            FD_SET(s->bbsock[i], &readfds);
            nfds = nfds > s->bbsock[i] ? nfds : s->bbsock[i];
            FD_SET(s->xbsock[i], &readfds);
            nfds = nfds > s->xbsock[i] ? nfds : s->xbsock[i];
        }

        FD_SET(s->pipes[1], &readfds);
        nfds = nfds > s->pipes[1] ? nfds : s->pipes[1];

        /* Add the shipgate socket to the fd_sets */
        if(s->sg.sock != -1) {
            FD_SET(s->sg.sock, &readfds);

            if(s->sg.sendbuf_cur) {
                FD_SET(s->sg.sock, &writefds);
            }

            nfds = nfds > s->sg.sock ? nfds : s->sg.sock;
        }

        /* If we're supposed to shut down soon, make sure we aren't in the
           middle of a select still when its supposed to happen. */
        if(s->shutdown_time && now + timeout.tv_sec > s->shutdown_time) {
            timeout.tv_sec = s->shutdown_time - now;
        }

        /* Wait for some activity... */
        if(select(nfds + 1, &readfds, &writefds, NULL, &timeout) > 0) {
            /* Clear anything written to the pipe */
            if(FD_ISSET(s->pipes[1], &readfds)) {
                read(s->pipes[1], &len, 1);
            }

            for(i = 0; i < numsocks; ++i) {
                if(FD_ISSET(s->dcsock[i], &readfds)) {
                    len = sizeof(struct sockaddr_storage);
                    if((sock = accept(s->dcsock[i], addr_p, &len)) < 0) {
                        perror("accept");
                    }

                    my_ntop(&addr, ipstr);
                    debug(DBG_LOG, "%s: Accepted DC ship connection from %s\n",
                          s->cfg->name, ipstr);

                    if(!(tmp = client_create_connection(sock,
                                                        CLIENT_VERSION_DCV1,
                                                        CLIENT_TYPE_SHIP,
                                                        s->clients, s, NULL,
                                                        addr_p, len))) {
                        close(sock);
                    }

                    if(s->shutdown_time) {
                        send_message_box(tmp, "%s\n\n%s\n%s",
                                         __(tmp, "\tEShip is going down for "
                                            "shutdown."),
                                         __(tmp, "Please try another ship."),
                                         __(tmp, "Disconnecting."));
                        tmp->flags |= CLIENT_FLAG_DISCONNECTED;
                    }
                }

                if(FD_ISSET(s->pcsock[i], &readfds)) {
                    len = sizeof(struct sockaddr_storage);
                    if((sock = accept(s->pcsock[i], addr_p, &len)) < 0) {
                        perror("accept");
                    }

                    my_ntop(&addr, ipstr);
                    debug(DBG_LOG, "%s: Accepted PC ship connection from %s\n",
                          s->cfg->name, ipstr);

                    if(!(tmp = client_create_connection(sock, CLIENT_VERSION_PC,
                                                        CLIENT_TYPE_SHIP,
                                                        s->clients, s, NULL,
                                                        addr_p, len))) {
                        close(sock);
                    }

                    if(s->shutdown_time) {
                        send_message_box(tmp, "%s\n\n%s\n%s",
                                         __(tmp, "\tEShip is going down for "
                                            "shutdown."),
                                         __(tmp, "Please try another ship."),
                                         __(tmp, "Disconnecting."));
                        tmp->flags |= CLIENT_FLAG_DISCONNECTED;
                    }
                }

                if(FD_ISSET(s->gcsock[i], &readfds)) {
                    len = sizeof(struct sockaddr_storage);
                    if((sock = accept(s->gcsock[i], addr_p, &len)) < 0) {
                        perror("accept");
                    }

                    my_ntop(&addr, ipstr);
                    debug(DBG_LOG, "%s: Accepted GC ship connection from %s\n",
                          s->cfg->name, ipstr);

                    if(!(tmp = client_create_connection(sock, CLIENT_VERSION_GC,
                                                        CLIENT_TYPE_SHIP,
                                                        s->clients, s, NULL,
                                                        addr_p, len))) {
                        close(sock);
                    }

                    if(s->shutdown_time) {
                        send_message_box(tmp, "%s\n\n%s\n%s",
                                         __(tmp, "\tEShip is going down for "
                                            "shutdown."),
                                         __(tmp, "Please try another ship."),
                                         __(tmp, "Disconnecting."));
                        tmp->flags |= CLIENT_FLAG_DISCONNECTED;
                    }
                }

                if(FD_ISSET(s->ep3sock[i], &readfds)) {
                    len = sizeof(struct sockaddr_storage);
                    if((sock = accept(s->ep3sock[i], addr_p, &len)) < 0) {
                        perror("accept");
                    }

                    my_ntop(&addr, ipstr);
                    debug(DBG_LOG, "%s: Accepted Episode 3 ship connection "
                          "from %s\n", s->cfg->name, ipstr);

                    if(!(tmp = client_create_connection(sock,
                                                        CLIENT_VERSION_EP3,
                                                        CLIENT_TYPE_SHIP,
                                                        s->clients, s, NULL,
                                                        addr_p, len))) {
                        close(sock);
                    }

                    if(s->shutdown_time) {
                        send_message_box(tmp, "%s\n\n%s\n%s",
                                         __(tmp, "\tEShip is going down for "
                                            "shutdown."),
                                         __(tmp, "Please try another ship."),
                                         __(tmp, "Disconnecting."));
                        tmp->flags |= CLIENT_FLAG_DISCONNECTED;
                    }
                }

                if(FD_ISSET(s->bbsock[i], &readfds)) {
                    len = sizeof(struct sockaddr_storage);
                    if((sock = accept(s->bbsock[i], addr_p, &len)) < 0) {
                        perror("accept");
                    }

                    my_ntop(&addr, ipstr);
                    debug(DBG_LOG, "%s: Accepted Blue Burst ship connection "
                          "from %s\n", s->cfg->name, ipstr);

                    if(!(tmp = client_create_connection(sock,
                                                        CLIENT_VERSION_BB,
                                                        CLIENT_TYPE_SHIP,
                                                        s->clients, s, NULL,
                                                        addr_p, len))) {
                        close(sock);
                    }

                    if(s->shutdown_time) {
                        send_message_box(tmp, "%s\n\n%s\n%s",
                                         __(tmp, "\tEShip is going down for "
                                            "shutdown."),
                                         __(tmp, "Please try another ship."),
                                         __(tmp, "Disconnecting."));
                        tmp->flags |= CLIENT_FLAG_DISCONNECTED;
                    }
                }

                if(FD_ISSET(s->xbsock[i], &readfds)) {
                    len = sizeof(struct sockaddr_storage);
                    if((sock = accept(s->xbsock[i], addr_p, &len)) < 0) {
                        perror("accept");
                    }

                    my_ntop(&addr, ipstr);
                    debug(DBG_LOG, "%s: Accepted Xbox ship connection "
                          "from %s\n", s->cfg->name, ipstr);

                    if(!(tmp = client_create_connection(sock,
                                                        CLIENT_VERSION_XBOX,
                                                        CLIENT_TYPE_SHIP,
                                                        s->clients, s, NULL,
                                                        addr_p, len))) {
                        close(sock);
                    }

                    if(s->shutdown_time) {
                        send_message_box(tmp, "%s\n\n%s\n%s",
                                         __(tmp, "\tEShip is going down for "
                                            "shutdown."),
                                         __(tmp, "Please try another ship."),
                                         __(tmp, "Disconnecting."));
                        tmp->flags |= CLIENT_FLAG_DISCONNECTED;
                    }
                }
            }

            /* Process the shipgate */
            if(s->sg.sock != -1 && FD_ISSET(s->sg.sock, &readfds)) {
                if((rv = shipgate_process_pkt(&s->sg))) {
                    debug(DBG_WARN, "%s: Lost connection with shipgate\n",
                          s->cfg->name);

                    /* Close the connection so we can attempt to reconnect */
                    gnutls_bye(s->sg.session, GNUTLS_SHUT_RDWR);
                    close(s->sg.sock);
                    gnutls_deinit(s->sg.session);
                    s->sg.sock = -1;

                    if(rv < -1) {
                        debug(DBG_WARN, "%s: Fatal shipgate error, bailing!\n",
                              s->cfg->name);
                        s->run = 0;
                    }
                }
            }

            if(s->sg.sock != -1 && FD_ISSET(s->sg.sock, &writefds)) {
                if(shipgate_send_pkts(&s->sg)) {
                    debug(DBG_WARN, "%s: Lost connection with shipgate\n",
                          s->cfg->name);

                    /* Close the connection so we can attempt to reconnect */
                    gnutls_bye(s->sg.session, GNUTLS_SHUT_RDWR);
                    close(s->sg.sock);
                    gnutls_deinit(s->sg.session);
                    s->sg.sock = -1;
                }
            }

            /* Process client connections. */
            TAILQ_FOREACH(it, s->clients, qentry) {
                /* Check if this connection was trying to send us something. */
                if(FD_ISSET(it->sock, &readfds)) {
                    if(client_process_pkt(it)) {
                        it->flags |= CLIENT_FLAG_DISCONNECTED;
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
            }
        }

        /* Clean up any dead connections (its not safe to do a TAILQ_REMOVE in
           the middle of a TAILQ_FOREACH, and destroy_connection does indeed
           use TAILQ_REMOVE). */
        it = TAILQ_FIRST(s->clients);
        while(it) {
            tmp = TAILQ_NEXT(it, qentry);

            if(it->flags & CLIENT_FLAG_DISCONNECTED) {
                client_destroy_connection(it, s->clients);
            }

            it = tmp;
        }
    }

    debug(DBG_LOG, "%s: Shutting down...\n", s->cfg->name);

    /* Before we shut down, run the shutdown script, if one is configured. */
    script_execute(ScriptActionShutdown, NULL, SCRIPT_ARG_PTR, s, 0);

#ifdef ENABLE_LUA
    /* Remove the table from the registry */
    luaL_unref(s->lstate, LUA_REGISTRYINDEX, s->script_ref);
#endif

    /* Disconnect any clients. */
    it = TAILQ_FIRST(s->clients);
    while(it) {
        tmp = TAILQ_NEXT(it, qentry);
        client_destroy_connection(it, s->clients);
        it = tmp;
    }

    /* Wait for the block threads to die. */
    for(i = 0; i < s->cfg->blocks; ++i) {
        if(s->blocks[i]) {
            block_server_stop(s->blocks[i]);
        }
    }

    /* Free the ship structure. */
    ban_list_clear(s);
    cleanup_scripts(s);
    pthread_rwlock_destroy(&s->banlock);
    pthread_rwlock_destroy(&s->qlock);
    pthread_rwlock_destroy(&s->llock);
    ship_free_limits(s);
    shipgate_cleanup(&s->sg);
    free(s->gm_list);
    clean_quests(s);
    close(s->pipes[0]);
    close(s->pipes[1]);
#ifdef SYLVERANT_ENABLE_IPV6
    if(enable_ipv6) {
        close(s->xbsock[1]);
        close(s->bbsock[1]);
        close(s->ep3sock[1]);
        close(s->gcsock[1]);
        close(s->pcsock[1]);
        close(s->dcsock[1]);
    }
#endif
    close(s->xbsock[0]);
    close(s->bbsock[0]);
    close(s->ep3sock[0]);
    close(s->gcsock[0]);
    close(s->pcsock[0]);
    close(s->dcsock[0]);
    clean_shiplist(s);
    free(s->clients);
    free(s->blocks);
    free(s);
    return NULL;
}

ship_t *ship_server_start(sylverant_ship_t *s) {
    ship_t *rv;
    int dcsock[2] = { -1, -1 }, pcsock[2] = { -1, -1 };
    int gcsock[2] = { -1, -1 }, ep3sock[2] = { -1, -1 };
    int bbsock[2] = { -1, -1 }, xbsock[2] = { -1, -1 };
    int i;
    sylverant_limits_t *l;
    limits_entry_t *ent;

    debug(DBG_LOG, "Starting server for ship %s...\n", s->name);

    /* Create the sockets for listening for connections. */
    dcsock[0] = open_sock(AF_INET, s->base_port);
    if(dcsock[0] < 0) {
        return NULL;
    }

    pcsock[0] = open_sock(AF_INET, s->base_port + 1);
    if(pcsock[0] < 0) {
        goto err_close_dc;
    }

    gcsock[0] = open_sock(AF_INET, s->base_port + 2);
    if(gcsock[0] < 0) {
        goto err_close_pc;
    }

    ep3sock[0] = open_sock(AF_INET, s->base_port + 3);
    if(ep3sock[0] < 0) {
        goto err_close_gc;
    }

    bbsock[0] = open_sock(AF_INET, s->base_port + 4);
    if(bbsock[0] < 0) {
        goto err_close_ep3;
    }

    xbsock[0] = open_sock(AF_INET, s->base_port + 5);
    if(xbsock[0] < 0) {
        goto err_close_bb;
    }

#ifdef SYLVERANT_ENABLE_IPV6
    if(enable_ipv6) {
        dcsock[1] = open_sock(AF_INET6, s->base_port);
        if(dcsock[1] < 0) {
            goto err_close_xb;
        }

        pcsock[1] = open_sock(AF_INET6, s->base_port + 1);
        if(pcsock[1] < 0) {
            goto err_close_dc_6;
        }

        gcsock[1] = open_sock(AF_INET6, s->base_port + 2);
        if(gcsock[1] < 0) {
            goto err_close_pc_6;
        }

        ep3sock[1] = open_sock(AF_INET6, s->base_port + 3);
        if(ep3sock[1] < 0) {
            goto err_close_gc_6;
        }

        bbsock[1] = open_sock(AF_INET6, s->base_port + 4);
        if(bbsock[1] < 0) {
            goto err_close_ep3_6;
        }

        xbsock[1] = open_sock(AF_INET6, s->base_port + 5);
        if(xbsock[1] < 0) {
            goto err_close_bb_6;
        }
    }
#endif

    /* Make space for the ship structure. */
    rv = (ship_t *)malloc(sizeof(ship_t));

    if(!rv) {
        debug(DBG_ERROR, "%s: Cannot allocate memory!\n", s->name);
        goto err_close_all;
    }

    /* Clear it out */
    memset(rv, 0, sizeof(ship_t));

    /* Make the pipe */
    if(pipe(rv->pipes) == -1) {
        debug(DBG_ERROR, "%s: Cannot create pipe!\n", s->name);
        goto err_free;
    }

    /* Make room for the block structures. */
    rv->blocks = (block_t **)malloc(sizeof(block_t *) * s->blocks);

    if(!rv->blocks) {
        debug(DBG_ERROR, "%s: Cannot allocate memory for blocks!\n", s->name);
        goto err_pipes;
    }

    /* Make room for the client list. */
    rv->clients = (struct client_queue *)malloc(sizeof(struct client_queue));

    if(!rv->clients) {
        debug(DBG_ERROR, "%s: Cannot allocate memory for clients!\n", s->name);
        goto err_blocks;
    }

    /* Attempt to read the quest list in. */
    if(s->quests_file && s->quests_file[0]) {
        debug(DBG_WARN, "%s: Ignoring old quests configuration!\n", s->name);
    }

    /* Deal with loading the quest data... */
    pthread_rwlock_init(&rv->qlock, NULL);
    load_quests(rv, s, 1);

    /* Attempt to read the GM list in. */
    if(s->gm_file) {
        debug(DBG_LOG, "%s: Reading Local GM List...\n", s->name);

        if(gm_list_read(s->gm_file, rv)) {
            debug(DBG_ERROR, "%s: Couldn't read GM file!\n", s->name);
            goto err_quests;
        }

        debug(DBG_LOG, "%s: Read %d Local GMs\n", s->name, rv->gm_count);
    }

    /* Read in all limits files. */
    TAILQ_INIT(&rv->all_limits);
    pthread_rwlock_init(&rv->llock, NULL);

    for(i = 0; i < s->limits_count; ++i) {
        debug(DBG_LOG, "%s: Parsing /legit list %d...\n", s->name, i);
        /* Check if they've given us one of the reserved names... */
        if(s->limits[i].name && (!strcmp(s->limits[i].name, "default") ||
                                 !strcmp(s->limits[i].name, "list"))) {
            debug(DBG_ERROR, "%s: Illegal limits list name: %s\n",
                  s->name, s->limits[i].name);
            goto err_limits;
        }

        debug(DBG_LOG, "%s:     Name: %s\n", s->name, s->limits[i].name);

        if(sylverant_read_limits(s->limits[i].filename, &l)) {
            debug(DBG_ERROR, "%s: Couldn't read limits file for %s: %s\n",
                  s->name, s->limits[i].name, s->limits[i].filename);
            goto err_limits;
        }

        debug(DBG_LOG, "%s:    Parsed!\n", s->name);

        if(!(ent = malloc(sizeof(limits_entry_t)))) {
            debug(DBG_ERROR, "%s: %s\n", s->name, strerror(errno));
            sylverant_free_limits(l);
            goto err_limits;
        }

        if(s->limits[i].name) {
            if(!(ent->name = strdup(s->limits[i].name))) {
                debug(DBG_ERROR, "%s: %s\n", s->name, strerror(errno));
                sylverant_free_limits(l);
                free(ent);
                goto err_limits;
            }
        }
        else {
            ent->name = NULL;
        }

        ent->limits = l;
        TAILQ_INSERT_TAIL(&rv->all_limits, ent, qentry);

        if(s->limits_default == i)
            rv->def_limits = l;
    }

    /* Fill in the structure. */
    pthread_rwlock_init(&rv->banlock, NULL);
    TAILQ_INIT(rv->clients);
    TAILQ_INIT(&rv->ships);
    TAILQ_INIT(&rv->guildcard_bans);
    TAILQ_INIT(&rv->ip_bans);
    rv->cfg = s;
    rv->dcsock[0] = dcsock[0];
    rv->pcsock[0] = pcsock[0];
    rv->gcsock[0] = gcsock[0];
    rv->ep3sock[0] = ep3sock[0];
    rv->bbsock[0] = bbsock[0];
    rv->xbsock[0] = xbsock[0];
    rv->dcsock[1] = dcsock[1];
    rv->pcsock[1] = pcsock[1];
    rv->gcsock[1] = gcsock[1];
    rv->ep3sock[1] = ep3sock[1];
    rv->bbsock[1] = bbsock[1];
    rv->xbsock[1] = xbsock[1];
    rv->run = 1;
    rv->lobby_event = s->events[0].lobby_event;
    rv->game_event = s->events[0].game_event;

    /* Initialize scripting support */
    init_scripts(rv);

#ifdef ENABLE_LUA
    /* Initialize the script table */
    lua_newtable(rv->lstate);
    rv->script_ref = luaL_ref(rv->lstate, LUA_REGISTRYINDEX);
#endif

    /* Attempt to read the ban list */
    if(s->bans_file) {
        if(ban_list_read(s->bans_file, rv)) {
            debug(DBG_WARN, "%s: Couldn't read bans file!\n", s->name);
        }
    }

    /* Create the random number generator state */
    mt19937_init(&rv->rng, (uint32_t)time(NULL));

    /* Connect to the shipgate. */
    if(shipgate_connect(rv, &rv->sg)) {
        debug(DBG_ERROR, "%s: Couldn't connect to shipgate!\n", s->name);
        goto err_bans_locks;
    }

    /* Start up the thread for this ship. */
    if(pthread_create(&rv->thd, NULL, &ship_thd, rv)) {
        debug(DBG_ERROR, "%s: Cannot start ship thread!\n", s->name);
        goto err_shipgate;
    }

    return rv;

err_shipgate:
    shipgate_cleanup(&rv->sg);
err_bans_locks:
    pthread_rwlock_destroy(&rv->banlock);
    ban_list_clear(rv);
    cleanup_scripts(rv);
err_limits:
    ship_free_limits(rv);
    pthread_rwlock_destroy(&rv->llock);
    free(rv->gm_list);
err_quests:
    pthread_rwlock_destroy(&rv->qlock);
    clean_quests(rv);
    free(rv->clients);
err_blocks:
    free(rv->blocks);
err_pipes:
    close(rv->pipes[0]);
    close(rv->pipes[1]);
err_free:
    free(rv);
err_close_all:
#ifdef SYLVERANT_ENABLE_IPV6
    if(enable_ipv6) {
        close(xbsock[1]);
err_close_bb_6:
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
err_close_xb:
#endif
    close(xbsock[0]);
err_close_bb:
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

void ship_check_cfg(sylverant_ship_t *s) {
    ship_t *rv;
    int i, j;
    char fn[512];
    sylverant_limits_t *l;
    limits_entry_t *ent;

    debug(DBG_LOG, "Checking config for ship %s...\n", s->name);

    /* Make space for the ship structure. */
    rv = (ship_t *)malloc(sizeof(ship_t));

    if(!rv) {
        debug(DBG_ERROR, "%s: Cannot allocate memory!\n", s->name);
        return;
    }

    /* Clear it out */
    memset(rv, 0, sizeof(ship_t));
    TAILQ_INIT(&rv->qmap);
    TAILQ_INIT(&rv->all_limits);
    pthread_rwlock_init(&rv->llock, NULL);
    rv->cfg = s;

    /* Attempt to read the quest list in. */
    if(s->quests_file && s->quests_file[0]) {
        debug(DBG_WARN, "%s: Ignoring old quest configuration. Please update "
              "your config!\n", s->name);
    }

    if(s->quests_dir && s->quests_dir[0]) {
        for(i = 0; i < CLIENT_VERSION_COUNT; ++i) {
            for(j = 0; j < CLIENT_LANG_COUNT; ++j) {
                sprintf(fn, "%s/%s-%s/quests.xml", s->quests_dir,
                        version_codes[i], language_codes[j]);
                if(!sylverant_quests_read(fn, &rv->qlist[i][j])) {
                    if(!quest_map(&rv->qmap, &rv->qlist[i][j], i, j)) {
                        debug(DBG_LOG, "Read quests for %s-%s\n",
                              version_codes[i], language_codes[j]);
                    }
                    else {
                        debug(DBG_LOG, "Unable to map quests for %s-%s\n",
                              version_codes[i], language_codes[j]);
                        sylverant_quests_destroy(&rv->qlist[i][j]);
                    }
                }
            }
        }
    }

    /* Attempt to read the GM list in. */
    if(s->gm_file) {
        debug(DBG_LOG, "%s: Reading Local GM List...\n", s->name);

        if(gm_list_read(s->gm_file, rv)) {
            debug(DBG_ERROR, "%s: Couldn't read GM file!\n", s->name);
        }

        debug(DBG_LOG, "%s: Read %d Local GMs\n", s->name, rv->gm_count);
    }

    /* Read in all limits files. */
    for(i = 0; i < s->limits_count; ++i) {
        debug(DBG_LOG, "%s: Parsing /legit list %d...\n", s->name, i);

        /* Check if they've given us one of the reserved names... */
        if(s->limits[i].name && (!strcmp(s->limits[i].name, "default") ||
                                 !strcmp(s->limits[i].name, "list"))) {
            debug(DBG_ERROR, "%s: Skipping illegal limits list name: %s\n",
                  s->name, s->limits[i].name);
            continue;
        }

        debug(DBG_LOG, "%s:     Name: %s\n", s->name, s->limits[i].name);

        if(sylverant_read_limits(s->limits[i].filename, &l)) {
            debug(DBG_ERROR, "%s: Couldn't read limits file for %s: %s\n",
                  s->name, s->limits[i].name, s->limits[i].filename);
        }

        debug(DBG_LOG, "%s:     Parsed!\n", s->name);

        if(!(ent = malloc(sizeof(limits_entry_t)))) {
            debug(DBG_ERROR, "%s: %s\n", s->name, strerror(errno));
            sylverant_free_limits(l);
            continue;
        }

        if(s->limits[i].name) {
            if(!(ent->name = strdup(s->limits[i].name))) {
                debug(DBG_ERROR, "%s: %s\n", s->name, strerror(errno));
                free(ent);
                sylverant_free_limits(l);
                continue;
            }

            if(!(l->name = strdup(s->limits[i].name))) {
                debug(DBG_ERROR, "%s: %s\n", s->name, strerror(errno));
                free(ent->name);
                free(ent);
                sylverant_free_limits(l);
                continue;
            }
        }
        else {
            ent->name = NULL;
            l->name = NULL;
        }

        ent->limits = l;
        TAILQ_INSERT_TAIL(&rv->all_limits, ent, qentry);

        if(s->limits_default == i)
            rv->def_limits = l;
    }

    /* Initialize scripting support */
    init_scripts(rv);

    /* Attempt to read the ban list */
    if(s->bans_file) {
        if(ban_list_read(s->bans_file, rv)) {
            debug(DBG_WARN, "%s: Couldn't read bans file!\n", s->name);
        }
    }

    ban_list_clear(rv);
    ship_free_limits(rv);
    pthread_rwlock_destroy(&rv->llock);
    free(rv->gm_list);
    clean_quests(rv);
    free(rv);
}

void ship_server_stop(ship_t *s) {
    /* Set the flag to kill the ship. */
    s->run = 0;

    /* Send a byte to the pipe so that we actually break out of the select. */
    write(s->pipes[0], "\xFF", 1);

    /* Wait for it to die. */
    pthread_join(s->thd, NULL);
}

void ship_server_shutdown(ship_t *s, time_t when) {
    if(when >= time(NULL)) {
        s->shutdown_time = when;

        /* Send a byte to the pipe so that we actually break out of the select
           and put a probably more sane amount in the timeout there */
        write(s->pipes[0], "\xFF", 1);
    }
}

static int dcnte_process_login(ship_client_t *c, dcnte_login_8b_pkt *pkt) {
    char *ban_reason;
    time_t ban_end;

    /* Make sure v1 is allowed on this ship. */
    if((ship->cfg->shipgate_flags & SHIPGATE_FLAG_NODCNTE)) {
        send_message_box(c, "%s", __(c, "\tEPSO NTE is not supported on\n"
                                     "this ship.\n\nDisconnecting."));
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        return 0;
    }

    c->language_code = CLIENT_LANG_JAPANESE;
    c->flags |= CLIENT_FLAG_IS_NTE;
    c->guildcard = LE32(pkt->guildcard);

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

    if(send_dc_security(c, c->guildcard, NULL, 0)) {
        return -1;
    }

    if(send_block_list(c, ship)) {
        return -2;
    }

    return 0;
}

static int dc_process_login(ship_client_t *c, dc_login_93_pkt *pkt) {
    char *ban_reason;
    time_t ban_end;

    /* Make sure v1 is allowed on this ship. */
    if((ship->cfg->shipgate_flags & SHIPGATE_FLAG_NOV1)) {
        send_message_box(c, "%s", __(c, "\tEPSO Version 1 is not supported on\n"
                                     "this ship.\n\nDisconnecting."));
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        return 0;
    }

    c->language_code = pkt->language_code;
    c->guildcard = LE32(pkt->guildcard);

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

    if(send_dc_security(c, c->guildcard, NULL, 0)) {
        return -1;
    }

    if(send_block_list(c, ship)) {
        return -2;
    }

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

/* Just in case I ever use the rest of the stuff... */
static int dcv2_process_login(ship_client_t *c, dcv2_login_9d_pkt *pkt) {
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

    c->language_code = pkt->language_code;
    c->guildcard = LE32(pkt->guildcard);

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

    if(send_dc_security(c, c->guildcard, NULL, 0)) {
        return -1;
    }

    if(send_block_list(c, ship)) {
        return -2;
    }

    return 0;
}

static int gc_process_login(ship_client_t *c, gc_login_9e_pkt *pkt) {
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

    c->language_code = pkt->language_code;
    c->guildcard = LE32(pkt->guildcard);

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

    if(send_dc_security(c, c->guildcard, NULL, 0)) {
        return -1;
    }

    if(send_block_list(c, ship)) {
        return -2;
    }

    return 0;
}

static int bb_process_login(ship_client_t *c, bb_login_93_pkt *pkt) {
    char *ban_reason;
    time_t ban_end;
    uint32_t team_id;

    /* Make sure PSOBB is allowed on this ship. */
    if((ship->cfg->shipgate_flags & LOGIN_FLAG_NOBB)) {
        send_message_box(c, "%s", __(c, "\tEPSO Blue Burst is not "
                                        "supported on\nthis ship.\n\n"
                                        "Disconnecting."));
        c->flags |= CLIENT_FLAG_DISCONNECTED;
        return 0;
    }

    c->guildcard = LE32(pkt->guildcard);
    team_id = LE32(pkt->team_id);

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

    if(send_block_list(c, ship)) {
        return -3;
    }

    return 0;
}

static int dc_process_block_sel(ship_client_t *c, dc_select_pkt *pkt) {
    int block = LE32(pkt->item_id);
    uint16_t port;

    /* See if the block selected is the "Ship Select" block */
    if(block == 0xFFFFFFFF) {
        return send_ship_list(c, ship, ship->cfg->menu_code);
    }

    /* Make sure the block selected is in range. */
    if(block > ship->cfg->blocks) {
        return -1;
    }

    /* Make sure that block is up and running. */
    if(ship->blocks[block - 1] == NULL  || ship->blocks[block - 1]->run == 0) {
        return -2;
    }

    /* Redirect the client where we want them to go. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            port = ship->blocks[block - 1]->dc_port;
            break;

        case CLIENT_VERSION_PC:
            port = ship->blocks[block - 1]->pc_port;
            break;

        case CLIENT_VERSION_GC:
            port = ship->blocks[block - 1]->gc_port;
            break;

        case CLIENT_VERSION_EP3:
            port = ship->blocks[block - 1]->ep3_port;
            break;

        case CLIENT_VERSION_BB:
            port = ship->blocks[block - 1]->bb_port;
            break;

        case CLIENT_VERSION_XBOX:
            port = ship->blocks[block - 1]->xb_port;
            break;

        default:
            return -3;
    }

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

static int dc_process_menu(ship_client_t *c, dc_select_pkt *pkt) {
    uint32_t menu_id = LE32(pkt->menu_id);
    uint32_t item_id = LE32(pkt->item_id);

    /* Figure out what the client is selecting. */
    switch(menu_id & 0xFF) {
        /* Blocks */
        case MENU_ID_BLOCK:
            return dc_process_block_sel(c, pkt);

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

                case CLIENT_VERSION_XBOX:
                    off = 5;
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
    }

    return -1;
}

static int bb_process_menu(ship_client_t *c, bb_select_pkt *pkt) {
    dc_select_pkt pkt2;

    /* Do this the lazy way... */
    pkt2.menu_id = pkt->menu_id;
    pkt2.item_id = pkt->item_id;

    return dc_process_menu(c, &pkt2);
}

static int dc_process_info_req(ship_client_t *c, dc_select_pkt *pkt) {
    uint32_t menu_id = LE32(pkt->menu_id);
    uint32_t item_id = LE32(pkt->item_id);

    /* What kind of information do they want? */
    switch(menu_id & 0xFF) {
        /* Block */
        case MENU_ID_BLOCK:
            return block_info_reply(c, item_id);

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

            return send_info_reply(c,
                                   __(c, "\tE\tC4That ship is now\noffline."));
        }

        default:
            return -1;
    }
}

static int bb_process_info_req(ship_client_t *c, bb_select_pkt *pkt) {
    dc_select_pkt pkt2;

    /* Do this the lazy way... */
    pkt2.menu_id = pkt->menu_id;
    pkt2.item_id = pkt->item_id;

    return dc_process_info_req(c, &pkt2);
}

static int dc_process_pkt(ship_client_t *c, uint8_t *pkt) {
    uint8_t type;
    uint16_t len;
    dc_pkt_hdr_t *dc = (dc_pkt_hdr_t *)pkt;
    pc_pkt_hdr_t *pc = (pc_pkt_hdr_t *)pkt;

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3 ||
       c->version == CLIENT_VERSION_XBOX) {
        type = dc->pkt_type;
        len = LE16(dc->pkt_len);
    }
    else {
        type = pc->pkt_type;
        len = LE16(pc->pkt_len);
    }

    switch(type) {
        case PING_TYPE:
            /* Ignore these. */
            return 0;

        case LOGIN_8B_TYPE:
            return dcnte_process_login(c, (dcnte_login_8b_pkt *)pkt);

        case LOGIN_93_TYPE:
            return dc_process_login(c, (dc_login_93_pkt *)pkt);

        case MENU_SELECT_TYPE:
            return dc_process_menu(c, (dc_select_pkt *)pkt);

        case INFO_REQUEST_TYPE:
            return dc_process_info_req(c, (dc_select_pkt *)pkt);

        case LOGIN_9D_TYPE:
            return dcv2_process_login(c, (dcv2_login_9d_pkt *)pkt);

        case LOGIN_9E_TYPE:
            return gc_process_login(c, (gc_login_9e_pkt *)pkt);

        case GC_MSG_BOX_CLOSED_TYPE:
            return send_block_list(c, ship);

        case GAME_COMMAND0_TYPE:
            /* Ignore these, since taking screenshots on PSOPC generates them
               for some reason. */
            return 0;

        default:
            if(!script_execute_pkt(ScriptActionUnknownShipPacket, c, pkt,
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

    switch(type) {
        case PING_TYPE:
            /* Ignore these. */
            return 0;

        case LOGIN_93_TYPE:
            return bb_process_login(c, (bb_login_93_pkt *)pkt);

        case MENU_SELECT_TYPE:
            return bb_process_menu(c, (bb_select_pkt *)pkt);

        case INFO_REQUEST_TYPE:
            return bb_process_info_req(c, (bb_select_pkt *)pkt);

        default:
            if(!script_execute_pkt(ScriptActionUnknownShipPacket, c, pkt,
                                   len)) {
                debug(DBG_LOG, "Unknown packet!\n");
                print_packet((unsigned char *)pkt, len);
                return -3;
            }
            return 0;
    }
}

int ship_process_pkt(ship_client_t *c, uint8_t *pkt) {
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
        case CLIENT_VERSION_XBOX:
            return dc_process_pkt(c, pkt);

        case CLIENT_VERSION_BB:
            return bb_process_pkt(c, pkt);
    }

    return -1;
}

void ship_inc_clients(ship_t *s) {
    ++s->num_clients;
    shipgate_send_cnt(&s->sg, s->num_clients, s->num_games);
}

void ship_dec_clients(ship_t *s) {
    --s->num_clients;
    shipgate_send_cnt(&s->sg, s->num_clients, s->num_games);
}

void ship_inc_games(ship_t *s) {
    ++s->num_games;
    shipgate_send_cnt(&s->sg, s->num_clients, s->num_games);
}

void ship_dec_games(ship_t *s) {
    --s->num_games;
    shipgate_send_cnt(&s->sg, s->num_clients, s->num_games);
}

void ship_free_limits(ship_t *s) {
    pthread_rwlock_wrlock(&s->llock);
    ship_free_limits_ex(&s->all_limits);
    pthread_rwlock_unlock(&s->llock);
}

void ship_free_limits_ex(struct limits_queue *l) {
    limits_entry_t *it, *tmp;

    it = TAILQ_FIRST(l);

    while(it) {
        tmp = TAILQ_NEXT(it, qentry);
        sylverant_free_limits(it->limits);
        free(it->name);
        it = tmp;
    }

    TAILQ_INIT(l);
}

sylverant_limits_t *ship_lookup_limits(const char *name) {
    limits_entry_t *it;

    /* Not really efficient, but simple. */
    TAILQ_FOREACH(it, &ship->all_limits, qentry) {
        if(!strcmp(name, it->name))
            return it->limits;
    }

    return NULL;
}

#ifdef ENABLE_LUA

static int ship_name_lua(lua_State *l) {
    ship_t *sl;

    if(lua_islightuserdata(l, 1)) {
        sl = (ship_t *)lua_touserdata(l, 1);
        lua_pushstring(l, sl->cfg->name);
    }
    else {
        lua_pushstring(l, "");
    }

    return 1;
}

static int ship_getTable_lua(lua_State *l) {
    ship_t *sl;

    if(lua_islightuserdata(l, 1)) {
        sl = (ship_t *)lua_touserdata(l, 1);
        lua_rawgeti(l, LUA_REGISTRYINDEX, sl->script_ref);
    }
    else {
        lua_pushnil(l);
    }

    return 1;
}

static int ship_writeLog_lua(lua_State *l) {
    const char *s;

    if(lua_isstring(l, 1)) {
        s = (const char *)lua_tostring(l, 1);
        debug(DBG_LOG, "%s\n", s);
    }

    return 0;
}

static const luaL_Reg shiplib[] = {
    { "name", ship_name_lua },
    { "getTable", ship_getTable_lua },
    { "writeLog", ship_writeLog_lua },
    { NULL, NULL }
};

int ship_register_lua(lua_State *l) {
    luaL_newlib(l, shiplib);
    return 1;
}

#endif /* ENABLE_LUA */
