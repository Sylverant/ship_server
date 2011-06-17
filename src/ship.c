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

static void clean_quests(ship_t *s) {
    int i, j;

    for(i = 0; i < CLIENT_VERSION_COUNT; ++i) {
        for(j = 0; j < CLIENT_LANG_COUNT; ++j) {
            sylverant_quests_destroy(&s->qlist[i][j]);
        }
    }

    quest_cleanup(&s->qmap);
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

    /* Fire up the threads for each block. */
    for(i = 1; i <= s->cfg->blocks; ++i) {
        s->blocks[i - 1] = block_server_start(s, i, s->cfg->base_port +
                                              (i * 4));
    }

    /* While we're still supposed to run... do it. */
    while(s->run) {
        /* Clear the fd_sets so we can use them again. */
        nfds = 0;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        timeout.tv_sec = 9001;
        timeout.tv_usec = 0;
        now = time(NULL);

        /* Break out if we're shutting down now */
        if(s->shutdown_time && s->shutdown_time <= now) {
            s->run = 0;
            break;
        }

        /* If we haven't swept the bans list in the last day, do it now. */
        if((last_ban_sweep + 3600 * 24) <= now) {
            ban_sweep_guildcards(s);
            last_ban_sweep = now = time(NULL);
        }

        /* If the shipgate isn't there, attempt to reconnect */
        if(s->sg.sock == -1 && s->sg.login_attempt < now) {
            if(shipgate_reconnect(&s->sg)) {
                s->sg.login_attempt = now + 60;
            }
            else {
                s->sg.login_attempt = 0;
                timeout.tv_sec = 30;
            }
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
            timeout.tv_sec = 30;
        }

        /* Add the listening sockets to the read fd_set. */
        FD_SET(s->dcsock, &readfds);
        nfds = nfds > s->dcsock ? nfds : s->dcsock;
        FD_SET(s->pcsock, &readfds);
        nfds = nfds > s->pcsock ? nfds : s->pcsock;
        FD_SET(s->gcsock, &readfds);
        nfds = nfds > s->gcsock ? nfds : s->gcsock;
        FD_SET(s->ep3sock, &readfds);
        nfds = nfds > s->ep3sock ? nfds : s->ep3sock;

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
        else {
            timeout.tv_sec = 30;
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

            if(FD_ISSET(s->dcsock, &readfds)) {
                len = sizeof(struct sockaddr_storage);
                if((sock = accept(s->dcsock, addr_p, &len)) < 0) {
                    perror("accept");
                }

                my_ntop(&addr, ipstr);
                debug(DBG_LOG, "%s: Accepted DC ship connection from %s\n",
                      s->cfg->name, ipstr);

                if(!(tmp = client_create_connection(sock, CLIENT_VERSION_DCV1,
                                                    CLIENT_TYPE_SHIP,
                                                    s->clients, s, NULL, addr_p,
                                                    len))) {
                    close(sock);
                }

                if(s->shutdown_time) {
                    send_message_box(tmp, "%s\n\n%s\n%s",
                                     __(tmp, "\tEShip is going down for shut"
                                        "down"),
                                     __(tmp, "Please try another ship."),
                                     __(tmp, "Disconnecting."));
                    it->flags |= CLIENT_FLAG_DISCONNECTED;
                }
            }

            if(FD_ISSET(s->pcsock, &readfds)) {
                len = sizeof(struct sockaddr_storage);
                if((sock = accept(s->pcsock, addr_p, &len)) < 0) {
                    perror("accept");
                }

                my_ntop(&addr, ipstr);
                debug(DBG_LOG, "%s: Accepted PC ship connection from %s\n",
                      s->cfg->name, ipstr);

                if(!(tmp = client_create_connection(sock, CLIENT_VERSION_PC,
                                                    CLIENT_TYPE_SHIP,
                                                    s->clients, s, NULL, addr_p,
                                                    len))) {
                    close(sock);
                }

                if(s->shutdown_time) {
                    send_message_box(tmp, "%s\n\n%s\n%s",
                                     __(tmp, "\tEShip is going down for shut"
                                        "down"),
                                     __(tmp, "Please try another ship."),
                                     __(tmp, "Disconnecting."));
                    it->flags |= CLIENT_FLAG_DISCONNECTED;
                }
            }

            if(FD_ISSET(s->gcsock, &readfds)) {
                len = sizeof(struct sockaddr_storage);
                if((sock = accept(s->gcsock, addr_p, &len)) < 0) {
                    perror("accept");
                }

                my_ntop(&addr, ipstr);
                debug(DBG_LOG, "%s: Accepted GC ship connection from %s\n",
                      s->cfg->name, ipstr);

                if(!(tmp = client_create_connection(sock, CLIENT_VERSION_GC,
                                                    CLIENT_TYPE_SHIP,
                                                    s->clients, s, NULL, addr_p,
                                                    len))) {
                    close(sock);
                }

                if(s->shutdown_time) {
                    send_message_box(tmp, "%s\n\n%s\n%s",
                                     __(tmp, "\tEShip is going down for shut"
                                        "down"),
                                     __(tmp, "Please try another ship."),
                                     __(tmp, "Disconnecting."));
                    it->flags |= CLIENT_FLAG_DISCONNECTED;
                }
            }

            if(FD_ISSET(s->ep3sock, &readfds)) {
                len = sizeof(struct sockaddr_storage);
                if((sock = accept(s->ep3sock, addr_p, &len)) < 0) {
                    perror("accept");
                }

                my_ntop(&addr, ipstr);
                debug(DBG_LOG, "%s: Accepted Episode 3 ship connection from "
                      "%s\n", s->cfg->name, ipstr);

                if(!(tmp = client_create_connection(sock, CLIENT_VERSION_EP3,
                                                    CLIENT_TYPE_SHIP,
                                                    s->clients, s, NULL, addr_p,
                                                    len))) {
                    close(sock);
                }

                if(s->shutdown_time) {
                    send_message_box(tmp, "%s\n\n%s\n%s",
                                     __(tmp, "\tEShip is going down for shut"
                                        "down"),
                                     __(tmp, "Please try another ship."),
                                     __(tmp, "Disconnecting."));
                    it->flags |= CLIENT_FLAG_DISCONNECTED;
                }
            }

            /* Process the shipgate */
            if(s->sg.sock != -1 && FD_ISSET(s->sg.sock, &readfds)) {
                if((rv = shipgate_process_pkt(&s->sg))) {
                    debug(DBG_WARN, "%s: Lost connection with shipgate\n",
                          s->cfg->name);

                    /* Close the connection so we can attempt to reconnect */
                    close(s->sg.sock);
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
                    close(s->sg.sock);
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
    sylverant_free_limits(s->limits);
    shipgate_cleanup(&s->sg);
    free(s->motd);
    free(s->gm_list);
    sylverant_quests_destroy(&s->quests);
    clean_quests(s);
    close(s->pipes[0]);
    close(s->pipes[1]);
    close(s->ep3sock);
    close(s->gcsock);
    close(s->pcsock);
    close(s->dcsock);
    clean_shiplist(s);
    free(s->clients);
    free(s->blocks);
    free(s);
    return NULL;
}

static void ship_read_motd(ship_t *s, const char *fn) {
    FILE *fp;
    size_t len;
    char tmp[512];

    /* Open it up */
    fp = fopen(fn, "rt");
    if(!fp) {
        debug(DBG_WARN, "%s: Couldn't open MOTD file\n", s->cfg->name);
        return;
    }

    /* Read from the file */
    len = fread(tmp, 1, 511, fp);
    if(!len) {
        debug(DBG_WARN, "%s: Couldn't read MOTD file\n", s->cfg->name);
        fclose(fp);
        return;
    }

    fclose(fp);
    tmp[len] = '\0';

    len = strlen(tmp);

    /* If we have a message, allocate space for it, and copy it in. */
    if(len) {
        s->motd = (char *)malloc(len + 1);
        if(!s->motd) {
            perror("malloc");
            return;
        }

        strcpy(s->motd, tmp);
    }
}

ship_t *ship_server_start(sylverant_ship_t *s) {
    ship_t *rv;
    struct sockaddr_in addr;
    int dcsock, pcsock, gcsock, ep3sock;
    int i, j;
    char fn[512];

    debug(DBG_LOG, "Starting server for ship %s...\n", s->name);

    /* Create the sockets for listening for connections. */
    dcsock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(dcsock < 0) {
        perror("socket");
        return NULL;
    }

    /* Bind the socket */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(s->base_port);

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
    
    /* Bind the socket */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(s->base_port + 1);
    
    if(bind(pcsock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
        perror("bind");
        close(dcsock);
        close(pcsock);
        return NULL;
    }
    
    /* Listen on the socket for connections. */
    if(listen(pcsock, 10) < 0) {
        perror("listen");
        close(dcsock);
        close(pcsock);
        return NULL;
    }

    gcsock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if(gcsock < 0) {
        perror("socket");
        close(pcsock);
        close(dcsock);
        return NULL;
    }

    /* Bind the socket */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(s->base_port + 2);
    
    if(bind(gcsock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
        perror("bind");
        close(dcsock);
        close(pcsock);
        close(gcsock);
        return NULL;
    }
    
    /* Listen on the socket for connections. */
    if(listen(gcsock, 10) < 0) {
        perror("listen");
        close(dcsock);
        close(pcsock);
        close(gcsock);
        return NULL;
    }

    ep3sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(ep3sock < 0) {
        perror("socket");
        close(pcsock);
        close(dcsock);
        close(gcsock);
        return NULL;
    }

    /* Bind the socket */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(s->base_port + 3);

    if(bind(ep3sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
        perror("bind");
        close(dcsock);
        close(pcsock);
        close(gcsock);
        close(ep3sock);
        return NULL;
    }

    /* Listen on the socket for connections. */
    if(listen(ep3sock, 10) < 0) {
        perror("listen");
        close(dcsock);
        close(pcsock);
        close(gcsock);
        close(ep3sock);
        return NULL;
    }

    /* Make space for the ship structure. */
    rv = (ship_t *)malloc(sizeof(ship_t));

    if(!rv) {
        debug(DBG_ERROR, "%s: Cannot allocate memory!\n", s->name);
        close(ep3sock);
        close(gcsock);
        close(pcsock);
        close(dcsock);
        return NULL;
    }

    /* Clear it out */
    memset(rv, 0, sizeof(ship_t));
    TAILQ_INIT(&rv->qmap);

    /* Make the pipe */
    if(pipe(rv->pipes) == -1) {
        debug(DBG_ERROR, "%s: Cannot create pipe!\n", s->name);
        free(rv);
        close(ep3sock);
        close(gcsock);
        close(pcsock);
        close(dcsock);
        return NULL;
    }

    /* Make room for the block structures. */
    rv->blocks = (block_t **)malloc(sizeof(block_t *) * s->blocks);

    if(!rv->blocks) {
        debug(DBG_ERROR, "%s: Cannot allocate memory for blocks!\n", s->name);
        close(rv->pipes[0]);
        close(rv->pipes[1]);
        free(rv);
        close(ep3sock);
        close(gcsock);
        close(pcsock);
        close(dcsock);
        return NULL;
    }

    /* Make room for the client list. */
    rv->clients = (struct client_queue *)malloc(sizeof(struct client_queue));

    if(!rv->clients) {
        debug(DBG_ERROR, "%s: Cannot allocate memory for clients!\n", s->name);
        free(rv->blocks);
        close(rv->pipes[0]);
        close(rv->pipes[1]);
        free(rv);
        close(ep3sock);
        close(gcsock);
        close(pcsock);
        close(dcsock);
        return NULL;
    }

    /* Attempt to read the quest list in. */
    if(s->quests_file && s->quests_file[0]) {
        if(sylverant_quests_read(s->quests_file, &rv->quests)) {
            debug(DBG_ERROR, "%s: Couldn't read quests file!\n", s->name);
            free(rv->clients);
            free(rv->blocks);
            close(rv->pipes[0]);
            close(rv->pipes[1]);
            free(rv);
            close(ep3sock);
            close(gcsock);
            close(pcsock);
            close(dcsock);
            return NULL;
        }
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
            sylverant_quests_destroy(&rv->quests);
            clean_quests(rv);
            free(rv->clients);
            free(rv->blocks);
            close(rv->pipes[0]);
            close(rv->pipes[1]);
            free(rv);
            close(ep3sock);
            close(gcsock);
            close(pcsock);
            close(dcsock);
            return NULL;
        }

        debug(DBG_LOG, "%s: Read %d Local GMs\n", s->name, rv->gm_count);
    }

    /* Attempt to read the item limits list in. */
    if(s->limits_file) {
        if(sylverant_read_limits(s->limits_file, &rv->limits)) {
            debug(DBG_ERROR, "%s: Couldn't read limits file!\n", s->name);
            free(rv->gm_list);
            sylverant_quests_destroy(&rv->quests);
            clean_quests(rv);
            free(rv->clients);
            free(rv->blocks);
            close(rv->pipes[0]);
            close(rv->pipes[1]);
            free(rv);
            close(ep3sock);
            close(gcsock);
            close(pcsock);
            close(dcsock);
            return NULL;
        }
    }

    /* Fill in the structure. */
    pthread_rwlock_init(&rv->qlock, NULL);
    pthread_rwlock_init(&rv->banlock, NULL);
    pthread_rwlock_init(&rv->llock, NULL);
    TAILQ_INIT(rv->clients);
    TAILQ_INIT(&rv->ships);
    TAILQ_INIT(&rv->guildcard_bans);
    rv->cfg = s;
    rv->dcsock = dcsock;
    rv->pcsock = pcsock;
    rv->gcsock = gcsock;
    rv->ep3sock = ep3sock;
    rv->run = 1;

    /* Initialize scripting support */
    init_scripts(rv);

    /* Attempt to read the ban list */
    if(s->bans_file) {
        if(ban_list_read(s->bans_file, rv)) {
            debug(DBG_WARN, "%s: Couldn't read bans file!\n", s->name);
        }
    }

    /* Connect to the shipgate. */
    if(shipgate_connect(rv, &rv->sg)) {
        debug(DBG_ERROR, "%s: Couldn't connect to shipgate!\n", s->name);
        ban_list_clear(rv);
        cleanup_scripts(rv);
        pthread_rwlock_destroy(&rv->banlock);
        pthread_rwlock_destroy(&rv->qlock);
        pthread_rwlock_destroy(&rv->llock);
        sylverant_free_limits(rv->limits);
        free(rv->gm_list);
        sylverant_quests_destroy(&rv->quests);
        clean_quests(rv);
        free(rv->clients);
        free(rv->blocks);
        close(rv->pipes[0]);
        close(rv->pipes[1]);
        free(rv);
        close(ep3sock);
        close(pcsock);
        close(dcsock);
        close(gcsock);
        return NULL;
    }

    /* Register with the shipgate. */
    if(shipgate_send_ship_info(&rv->sg, rv)) {
        debug(DBG_ERROR, "%s: Couldn't register with shipgate!\n", s->name);
        ban_list_clear(rv);
        cleanup_scripts(rv);
        pthread_rwlock_destroy(&rv->banlock);
        pthread_rwlock_destroy(&rv->qlock);
        pthread_rwlock_destroy(&rv->llock);
        shipgate_cleanup(&rv->sg);
        sylverant_free_limits(rv->limits);
        free(rv->gm_list);
        sylverant_quests_destroy(&rv->quests);
        clean_quests(rv);
        free(rv->clients);
        free(rv->blocks);
        close(rv->pipes[0]);
        close(rv->pipes[1]);
        free(rv);
        close(ep3sock);
        close(pcsock);
        close(dcsock);
        close(gcsock);
        return NULL;
    }

    /* If we have a message of the day, read it */
    if(s->motd_file) {
        ship_read_motd(rv, s->motd_file);
    }

    /* Start up the thread for this ship. */
    if(pthread_create(&rv->thd, NULL, &ship_thd, rv)) {
        debug(DBG_ERROR, "%s: Cannot start ship thread!\n", s->name);
        ban_list_clear(rv);
        cleanup_scripts(rv);
        pthread_rwlock_destroy(&rv->banlock);
        pthread_rwlock_destroy(&rv->qlock);
        pthread_rwlock_destroy(&rv->llock);
        sylverant_free_limits(rv->limits);
        shipgate_cleanup(&rv->sg);
        free(rv->motd);
        free(rv->gm_list);
        sylverant_quests_destroy(&rv->quests);
        clean_quests(rv);
        free(rv->clients);
        free(rv->blocks);
        close(rv->pipes[0]);
        close(rv->pipes[1]);
        free(rv);
        close(ep3sock);
        close(pcsock);
        close(dcsock);
        close(gcsock);
        return NULL;
    }

    return rv;
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

static int send_ban_msg(ship_client_t *c, time_t until, const char *reason) {
    char string[512];
    struct tm cooked;

    /* Create the ban string. */
    sprintf(string, __(c, "\tEYou have been banned from this ship.\n"
                       "Reason:\n%s\n\nYour ban expires:\n"), reason);

    if(until == (time_t)-1) {
        strcat(string, __(c, "Never"));
    }
    else {
        gmtime_r(&until, &cooked);
        sprintf(string, "%s%02u:%02u UTC %u.%02u.%02u", string, cooked.tm_hour,
                cooked.tm_min, cooked.tm_year + 1900, cooked.tm_mon + 1,
                cooked.tm_mday);
    }

    return send_message_box(c, "%s", string);
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
    }

    c->language_code = pkt->language_code;
    c->guildcard = LE32(pkt->guildcard);

    /* See if the user is banned */
    if(is_guildcard_banned(ship, c->guildcard, &ban_reason, &ban_end)) {
        send_ban_msg(c, ban_end, ban_reason);
        c->flags |= CLIENT_FLAG_DISCONNECTED;
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

static int dc_process_block_sel(ship_client_t *c, dc_select_pkt *pkt) {
    int block = LE32(pkt->item_id);

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
    if(c->version == CLIENT_VERSION_DCV1 ||
       c->version == CLIENT_VERSION_DCV2) {
        return send_redirect(c, ship->cfg->ship_ip,
                             ship->blocks[block - 1]->dc_port);
    }
    else if(c->version == CLIENT_VERSION_PC) {
        return send_redirect(c, ship->cfg->ship_ip,
                             ship->blocks[block - 1]->pc_port);
    }
    else if(c->version == CLIENT_VERSION_GC) {
        return send_redirect(c, ship->cfg->ship_ip,
                             ship->blocks[block - 1]->gc_port);
    }
    else {
        return send_redirect(c, ship->cfg->ship_ip,
                             ship->blocks[block - 1]->ep3_port);
    }
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
            }

            /* Go through all the ships that we know about looking for the one
               that the user has requested. */
            TAILQ_FOREACH(i, &ship->ships, qentry) {
                if(i->ship_id == item_id) {
                    return send_redirect(c, i->ship_addr, i->ship_port + off);
                }
            }

            /* We didn't find it, punt. */
            return send_message1(c, "%s",
                                 __(c, "\tE\tC4That ship is now\noffline."));
        }
    }

    return -1;
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

static int dc_process_pkt(ship_client_t *c, uint8_t *pkt) {
    uint8_t type;
    uint16_t len;
    dc_pkt_hdr_t *dc = (dc_pkt_hdr_t *)pkt;
    pc_pkt_hdr_t *pc = (pc_pkt_hdr_t *)pkt;

    if(c->version == CLIENT_VERSION_DCV1 ||
       c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC ||
       c->version == CLIENT_VERSION_EP3) {
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
#ifdef HAVE_PYTHON
            if(script_execute_pkt(ScriptActionUnknownShipPacket, c, pkt,
                                  len) == 1) {
                return 0;
            }
#endif
            debug(DBG_LOG, "Unknown packet!\n");
            print_packet((unsigned char *)pkt, len);
            return -3;
    }
}

int ship_process_pkt(ship_client_t *c, uint8_t *pkt) {
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            return dc_process_pkt(c, pkt);
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
