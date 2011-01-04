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

/* Local host configuration. */
extern in_addr_t local_addr;
extern in_addr_t netmask;

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

static void *ship_thd(void *d) {
    int i, nfds;
    ship_t *s = (ship_t *)d;
    struct timeval timeout;
    fd_set readfds, writefds;
    ship_client_t *it, *tmp;
    socklen_t len;
    struct sockaddr_in addr;
    int sock, rv;
    ssize_t sent;
    time_t now;

    /* Fire up the threads for each block. */
    for(i = 1; i <= s->cfg->blocks; ++i) {
        s->blocks[i - 1] = block_server_start(s, i, s->cfg->base_port +
                                              (i * 3));
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
                len = sizeof(struct sockaddr_in);
                if((sock = accept(s->dcsock, (struct sockaddr *)&addr,
                                  &len)) < 0) {
                    perror("accept");
                }

                debug(DBG_LOG, "%s: Accepted DC ship connection from %s\n",
                      s->cfg->name, inet_ntoa(addr.sin_addr));

                if(!(tmp = client_create_connection(sock, CLIENT_VERSION_DCV1,
                                                    CLIENT_TYPE_SHIP,
                                                    s->clients, s, NULL,
                                                    addr.sin_addr.s_addr))) {
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
                len = sizeof(struct sockaddr_in);
                if((sock = accept(s->pcsock, (struct sockaddr *)&addr,
                                  &len)) < 0) {
                    perror("accept");
                }

                debug(DBG_LOG, "%s: Accepted PC ship connection from %s\n",
                      s->cfg->name, inet_ntoa(addr.sin_addr));

                if(!(tmp = client_create_connection(sock, CLIENT_VERSION_PC,
                                                    CLIENT_TYPE_SHIP,
                                                    s->clients, s, NULL,
                                                    addr.sin_addr.s_addr))) {
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
                len = sizeof(struct sockaddr_in);
                if((sock = accept(s->gcsock, (struct sockaddr *)&addr,
                                  &len)) < 0) {
                    perror("accept");
                }

                debug(DBG_LOG, "%s: Accepted GC ship connection from %s\n",
                      s->cfg->name, inet_ntoa(addr.sin_addr));

                if(!(tmp = client_create_connection(sock, CLIENT_VERSION_GC,
                                                    CLIENT_TYPE_SHIP,
                                                    s->clients, s, NULL,
                                                    addr.sin_addr.s_addr))) {
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
    pthread_mutex_destroy(&s->qmutex);
    sylverant_free_limits(s->limits);
    shipgate_cleanup(&s->sg);
    free(s->motd);
    free(s->gm_list);
    sylverant_quests_destroy(&s->quests);
    close(s->pipes[0]);
    close(s->pipes[1]);
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
    int dcsock, pcsock, gcsock;
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

    /* Make space for the ship structure. */
    rv = (ship_t *)malloc(sizeof(ship_t));

    if(!rv) {
        debug(DBG_ERROR, "%s: Cannot allocate memory!\n", s->name);
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
    if(s->gm_file[0]) {
        if(gm_list_read(s->gm_file, rv)) {
            debug(DBG_ERROR, "%s: Couldn't read GM file!\n", s->name);
            sylverant_quests_destroy(&rv->quests);
            free(rv->clients);
            free(rv->blocks);
            close(rv->pipes[0]);
            close(rv->pipes[1]);
            free(rv);
            close(gcsock);
            close(pcsock);
            close(dcsock);
            return NULL;
        }
    }

    /* Attempt to read the item limits list in. */
    if(s->limits_file[0]) {
        if(sylverant_read_limits(s->limits_file, &rv->limits)) {
            debug(DBG_ERROR, "%s: Couldn't read limits file!\n", s->name);
            free(rv->gm_list);
            sylverant_quests_destroy(&rv->quests);
            free(rv->clients);
            free(rv->blocks);
            close(rv->pipes[0]);
            close(rv->pipes[1]);
            free(rv);
            close(gcsock);
            close(pcsock);
            close(dcsock);
            return NULL;
        }
    }

    /* Fill in the structure. */
    pthread_mutex_init(&rv->qmutex, NULL);
    TAILQ_INIT(rv->clients);
    TAILQ_INIT(&rv->ships);
    rv->cfg = s;
    rv->dcsock = dcsock;
    rv->pcsock = pcsock;
    rv->gcsock = gcsock;
    rv->run = 1;

    /* Connect to the shipgate. */
    if(shipgate_connect(rv, &rv->sg)) {
        debug(DBG_ERROR, "%s: Couldn't connect to shipgate!\n", s->name);
        pthread_mutex_destroy(&rv->qmutex);
        sylverant_free_limits(rv->limits);
        free(rv->gm_list);
        sylverant_quests_destroy(&rv->quests);
        free(rv->clients);
        free(rv->blocks);
        close(rv->pipes[0]);
        close(rv->pipes[1]);
        free(rv);
        close(pcsock);
        close(dcsock);
        close(gcsock);
        return NULL;
    }

    /* Register with the shipgate. */
    if(shipgate_send_ship_info(&rv->sg, rv)) {
        debug(DBG_ERROR, "%s: Couldn't register with shipgate!\n", s->name);
        pthread_mutex_destroy(&rv->qmutex);
        shipgate_cleanup(&rv->sg);
        sylverant_free_limits(rv->limits);
        free(rv->gm_list);
        sylverant_quests_destroy(&rv->quests);
        free(rv->clients);
        free(rv->blocks);
        close(rv->pipes[0]);
        close(rv->pipes[1]);
        free(rv);
        close(pcsock);
        close(dcsock);
        close(gcsock);
        return NULL;
    }

    /* If we have a message of the day, read it */
    if(s->motd_file[0]) {
        ship_read_motd(rv, s->motd_file);
    }

    /* Start up the thread for this ship. */
    if(pthread_create(&rv->thd, NULL, &ship_thd, rv)) {
        debug(DBG_ERROR, "%s: Cannot start ship thread!\n", s->name);
        pthread_mutex_destroy(&rv->qmutex);
        sylverant_free_limits(rv->limits);
        shipgate_cleanup(&rv->sg);
        free(rv->motd);
        free(rv->gm_list);
        sylverant_quests_destroy(&rv->quests);
        free(rv->clients);
        free(rv->blocks);
        close(rv->pipes[0]);
        close(rv->pipes[1]);
        free(rv);
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

static int dc_process_login(ship_client_t *c, dc_login_93_pkt *pkt) {
    c->language_code = pkt->language_code;

    if(send_dc_security(c, pkt->guildcard, NULL, 0)) {
        return -1;
    }

    if(send_block_list(c, c->cur_ship)) {
        return -2;
    }

    return 0;
}

/* Just in case I ever use the rest of the stuff... */
static int dcv2_process_login(ship_client_t *c, dcv2_login_9d_pkt *pkt) {
    c->language_code = pkt->language_code;

    if(send_dc_security(c, pkt->guildcard, NULL, 0)) {
        return -1;
    }

    if(send_block_list(c, c->cur_ship)) {
        return -2;
    }

    return 0;
}

static int gc_process_login(ship_client_t *c, gc_login_9e_pkt *pkt) {
    c->language_code = pkt->language_code;

    if(send_dc_security(c, pkt->guildcard, NULL, 0)) {
        return -1;
    }

    if(send_message_box(c, "%s", c->cur_ship->motd)) {
        return -2;
    }

    return 0;
}

static int dc_process_block_sel(ship_client_t *c, dc_select_pkt *pkt) {
    int block = LE32(pkt->item_id);
    ship_t *s = c->cur_ship;
    in_addr_t addr;

    /* See if the block selected is the "Ship Select" block */
    if(block == 0xFFFFFFFF) {
        return send_ship_list(c, s, s->cfg->menu_code);
    }

    /* Make sure the block selected is in range. */
    if(block > s->cfg->blocks) {
        return -1;
    }

    /* Make sure that block is up and running. */
    if(s->blocks[block - 1] == NULL  || s->blocks[block - 1]->run == 0) {
        return -2;
    }

    /* Figure out what address to send the client. */
    if(netmask && (c->addr & netmask) == (local_addr & netmask)) {
        addr = local_addr;
    }
    else {
        addr = s->cfg->ship_ip;
    }

    /* Redirect the client where we want them to go. */
    if(c->version == CLIENT_VERSION_DCV1 ||
       c->version == CLIENT_VERSION_DCV2) {
        return send_redirect(c, addr, s->blocks[block - 1]->dc_port);
    }
    else if(c->version == CLIENT_VERSION_PC) {
        return send_redirect(c, addr, s->blocks[block - 1]->pc_port);
    }
    else {
        return send_redirect(c, addr, s->blocks[block - 1]->gc_port);
    }
}

static int dc_process_menu(ship_client_t *c, dc_select_pkt *pkt) {
    uint32_t menu_id = LE32(pkt->menu_id);
    uint32_t item_id = LE32(pkt->item_id);

    /* Figure out what the client is selecting. */
    switch(menu_id & 0xFF) {
        /* Blocks */
        case 0x01:
            return dc_process_block_sel(c, pkt);

        /* Ship */
        case 0x05:
        {
            miniship_t *i;
            ship_t *s = c->cur_ship;
            in_addr_t addr;
            int off = 0;

            /* See if the user picked a Ship List item */
            if(item_id == 0) {
                return send_ship_list(c, s, (uint16_t)(menu_id >> 8));
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
            }

            /* Go through all the ships that we know about looking for the one
               that the user has requested. */
            TAILQ_FOREACH(i, &s->ships, qentry) {
                if(i->ship_id == item_id) {
                    /* Figure out which address we need to send the client. */
                    if(c->addr == i->ship_addr) {
                        /* The client and the ship are connecting from the same
                           address, this one is obvious. */
                        addr = i->int_addr;
                    }
                    else if(netmask &&
                            i->ship_addr == s->cfg->ship_ip &&
                            (c->addr & netmask) == (local_addr & netmask)) {
                        /* The destination and the source are on the same
                           network, and the client is on the same network as the
                           source, thus the client must be on the same network
                           as the destination, send the internal address. */
                        addr = i->int_addr;
                    }
                    else {
                        /* They should be on different networks if we get here,
                           send the external IP. */
                        addr = i->ship_addr;
                    }

                    return send_redirect(c, addr, i->ship_port + off);
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
        case 0x01:
            return block_info_reply(c, item_id);

        /* Ship */
        case 0x05:
        {
            ship_t *s = c->cur_ship;
            miniship_t *i;

            /* Find the ship if its still online */
            TAILQ_FOREACH(i, &s->ships, qentry) {
                if(i->ship_id == item_id) {
                    char string[256];
                    sprintf(string, "%s\n\n%d %s\n%d %s", i->name, i->clients,
                            __(c, "Player(s)"), i->games, __(c, "Team(s)"));
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
       c->version == CLIENT_VERSION_GC) {
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
            return send_block_list(c, c->cur_ship);

        case GAME_COMMAND0_TYPE:
            /* Ignore these, since taking screenshots on PSOPC generates them
               for some reason. */
            return 0;

        default:
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
