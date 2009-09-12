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
#include <unistd.h>
#include <errno.h>
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

static void *ship_thd(void *d) {
    int i, nfds;
    ship_t *s = (ship_t *)d;
    struct timeval timeout;
    fd_set readfds, writefds, exceptfds;
    ship_client_t *it, *tmp;
    socklen_t len;
    struct sockaddr_in addr;
    int sock;
    ssize_t sent;

    /* Fire up the threads for each block. */
    for(i = 1; i <= s->cfg->blocks; ++i) {
        s->blocks[i - 1] = block_server_start(s, i, s->cfg->base_port + i);
    }

    /* While we're still supposed to run... do it. */
    while(s->run) {
        /* Clear the fd_sets so we can use them again. */
        nfds = 0;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 5000;

        /* Fill the sockets into the fd_sets so we can use select below. */
        TAILQ_FOREACH(it, s->clients, qentry) {
            FD_SET(it->sock, &readfds);
            FD_SET(it->sock, &exceptfds);

            /* Only add to the write fd set if we have something to send out. */
            if(it->sendbuf_cur) {
                FD_SET(it->sock, &writefds);
            }

            nfds = nfds > it->sock ? nfds : it->sock;
        }

        /* Add the listening socket to the read fd_set. */
        FD_SET(s->sock, &readfds);
        nfds = nfds > s->sock ? nfds : s->sock;

        /* Add the shipgate socket to the fd_sets */
        FD_SET(s->sg.sock, &readfds);

        if(s->sg.sendbuf_cur) {
            FD_SET(s->sg.sock, &writefds);
        }
        nfds = nfds > s->sg.sock ? nfds : s->sg.sock;

        /* Wait for some activity... */
        if(select(nfds + 1, &readfds, &writefds, &exceptfds, &timeout) > 0) {
            if(FD_ISSET(s->sock, &readfds)) {
                len = sizeof(struct sockaddr_in);
                if((sock = accept(s->sock, (struct sockaddr *)&addr,
                                  &len)) < 0) {
                    perror("accept");
                }

                debug(DBG_LOG, "%s: Accepted ship connection from %s\n",
                      s->cfg->name, inet_ntoa(addr.sin_addr));

                if(!client_create_connection(sock, CLIENT_VERSION_DCV1,
                                             CLIENT_TYPE_SHIP, s->clients, s,
                                             NULL, addr.sin_addr.s_addr)) {
                    close(sock);
                }
            }

            /* Process the shipgate */
            if(FD_ISSET(s->sg.sock, &readfds)) {
                if(shipgate_process_pkt(&s->sg)) {
                    debug(DBG_WARN, "%s: Lost connection with shipgate\n",
                          s->cfg->name);
                    break;
                }
            }

            if(FD_ISSET(s->sg.sock, &writefds)) {
                if(shipgate_send_pkts(&s->sg)) {
                    debug(DBG_WARN, "%s: Lost connection with shipgate\n",
                          s->cfg->name);
                    break;
                }
            }

            /* Process client connections. */
            TAILQ_FOREACH(it, s->clients, qentry) {
                /* Make sure there wasn't some kind of error with this
                   connection. */
                if(FD_ISSET(it->sock, &exceptfds)) {
                    it->disconnected = 1;
                    continue;
                }

                /* Check if this connection was trying to send us something. */
                if(FD_ISSET(it->sock, &readfds)) {
                    if(client_process_pkt(it)) {
                        it->disconnected = 1;
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
            
            if(it->disconnected) {
                client_destroy_connection(it, s->clients);
            }
            
            it = tmp;
        }
    }

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
    free(s->gm_list);
    sylverant_quests_destroy(&s->quests);
    close(s->sock);
    free(s->ships);
    free(s->clients);
    free(s->blocks);
    free(s);
    return NULL;
}

ship_t *ship_server_start(sylverant_ship_t *s) {
    ship_t *rv;
    struct sockaddr_in addr;
    int sock;

    debug(DBG_LOG, "Starting server for ship %s...\n", s->name);

    /* Create the sockets for listening for connections. */
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(sock < 0) {
        perror("socket");
        return NULL;
    }

    /* Bind the socket */
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(s->base_port);
    memset(addr.sin_zero, 0, 8);

    if(bind(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
        perror("bind");
        close(sock);
        return NULL;
    }

    /* Listen on the socket for connections. */
    if(listen(sock, 10) < 0) {
        perror("listen");
        close(sock);
        return NULL;
    }

    /* Make space for the ship structure. */
    rv = (ship_t *)malloc(sizeof(ship_t));

    if(!rv) {
        debug(DBG_ERROR, "%s: Cannot allocate memory!\n", s->name);
        return NULL;
    }

    /* Clear it out */
    memset(rv, 0, sizeof(ship_t));

    /* Make room for the block structures. */
    rv->blocks = (block_t **)malloc(sizeof(block_t *) * s->blocks);

    if(!rv->blocks) {
        debug(DBG_ERROR, "%s: Cannot allocate memory for blocks!\n", s->name);
        free(rv);
        close(sock);
        return NULL;
    }

    /* Make room for the client list. */
    rv->clients = (struct client_queue *)malloc(sizeof(struct client_queue));

    if(!rv->clients) {
        debug(DBG_ERROR, "%s: Cannot allocate memory for clients!\n", s->name);
        free(rv->blocks);
        free(rv);
        close(sock);
        return NULL;
    }

    /* Attempt to read the quest list in. */
    if(s->quests_file[0]) {
        if(sylverant_quests_read(s->quests_file, &rv->quests)) {
            debug(DBG_ERROR, "%s: Couldn't read quests file!\n", s->name);
            free(rv->clients);
            free(rv->blocks);
            free(rv);
            close(sock);
            return NULL;
        }
    }

    /* Attempt to read the GM list in. */
    if(s->gm_file[0]) {
        if(gm_list_read(s->gm_file, rv)) {
            debug(DBG_ERROR, "%s: Couldn't read GM file!\n", s->name);
            sylverant_quests_destroy(&rv->quests);
            free(rv->clients);
            free(rv->blocks);
            free(rv);
            close(sock);
            return NULL;
        }
    }

    /* Fill in the structure. */
    TAILQ_INIT(rv->clients);
    rv->cfg = s;
    rv->sock = sock;
    rv->run = 1;

    /* Connect to the shipgate. */
    if(shipgate_connect(rv, &rv->sg)) {
        debug(DBG_ERROR, "%s: Couldn't connect to shipgate!\n", s->name);
        free(rv->gm_list);
        sylverant_quests_destroy(&rv->quests);
        free(rv->clients);
        free(rv->blocks);
        free(rv);
        close(sock);
        return NULL;
    }

    /* Register with the shipgate. */
    if(shipgate_send_ship_info(&rv->sg, rv)) {
        debug(DBG_ERROR, "%s: Couldn't register with shipgate!\n", s->name);
        free(rv->gm_list);
        sylverant_quests_destroy(&rv->quests);
        free(rv->clients);
        free(rv->blocks);
        free(rv);
        close(sock);
        return NULL;
    }

    /* Start up the thread for this ship. */
    if(pthread_create(&rv->thd, NULL, &ship_thd, rv)) {
        debug(DBG_ERROR, "%s: Cannot start ship thread!\n", s->name);
        free(rv->gm_list);
        sylverant_quests_destroy(&rv->quests);
        free(rv->clients);
        free(rv->blocks);
        free(rv);
        close(sock);
        return NULL;
    }

    return rv;
}

void ship_server_stop(ship_t *s) {
    /* Set the flag to kill the ship. */
    s->run = 0;

    /* Wait for it to die. */
    pthread_join(s->thd, NULL);
}

static int dc_process_login(ship_client_t *c, dc_login_pkt *pkt) {
    if(send_dc_security(c, pkt->guildcard, NULL, 0)) {
        return -1;
    }

    if(send_block_list(c, c->cur_ship)) {
        return -2;
    }

    return 0;
}

/* Just in case I ever use the rest of the stuff... */
static int dcv2_process_login(ship_client_t *c, dcv2_login_pkt *pkt) {
    if(send_dc_security(c, pkt->guildcard, NULL, 0)) {
        return -1;
    }

    if(send_block_list(c, c->cur_ship)) {
        return -2;
    }

    return 0;
}

static int dc_process_block_sel(ship_client_t *c, dc_select_pkt *pkt) {
    int block = LE32(pkt->item_id);
    ship_t *s = c->cur_ship;
    in_addr_t addr;

    /* Make sure the block selected is in range. */
    if(block > s->cfg->blocks) {
        return -1;
    }

    /* Make sure that block is up and running. */
    if(s->blocks[block - 1] == NULL  || s->blocks[block - 1]->run == 0) {
        return -2;
    }

    /* Figure out what address to send the client. */
    if((c->addr & netmask) == (local_addr & netmask)) {
        addr = local_addr;
    }
    else {
        addr = s->cfg->ship_ip;
    }

    /* Redirect the client where we want them to go. */
    return send_redirect(c, addr, s->blocks[block - 1]->dc_port);
}

static int dc_process_info_req(ship_client_t *c, dc_select_pkt *pkt) {
    uint32_t menu_id = LE32(pkt->menu_id);
    uint32_t item_id = LE32(pkt->item_id);

    /* What kind of information do they want? */
    switch(menu_id) {
        /* Block */
        case 0x00000001:
            return block_info_reply(c, (int)item_id);

        default:
            debug(DBG_WARN, "Unknown info request menu_id: 0x%08X\n", menu_id);
            return -1;
    }
}

static int dc_process_pkt(ship_client_t *c, dc_pkt_hdr_t *pkt) {
    uint8_t type = pkt->pkt_type;

    debug(DBG_LOG, "%s: Received type 0x%02X\n", c->cur_ship->cfg->name, type);

    switch(type) {
        case SHIP_LOGIN_TYPE:
            return dc_process_login(c, (dc_login_pkt *)pkt);

        case SHIP_MENU_SELECT_TYPE:
            return dc_process_block_sel(c, (dc_select_pkt *)pkt);

        case SHIP_INFO_REQUEST_TYPE:
            return dc_process_info_req(c, (dc_select_pkt *)pkt);

        case SHIP_DCV2_LOGIN_TYPE:
            return dcv2_process_login(c, (dcv2_login_pkt *)pkt);

        default:
            printf("Unknown packet!\n");
            print_packet((unsigned char *)pkt, pkt->pkt_len);
            return -3;
    }
}

int ship_process_pkt(ship_client_t *c, uint8_t *pkt) {
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return dc_process_pkt(c, (dc_pkt_hdr_t *)pkt);
    }

    return -1;
}

void ship_inc_clients(ship_t *s) {
    ++s->num_clients;
    shipgate_send_cnt(&s->sg, s->num_clients, 0);
}

void ship_dec_clients(ship_t *s) {
    --s->num_clients;
    shipgate_send_cnt(&s->sg, s->num_clients, 0);
}
