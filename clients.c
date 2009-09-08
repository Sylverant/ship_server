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
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>

#include <sylverant/encryption.h>
#include <sylverant/mtwist.h>
#include <sylverant/debug.h>

#include "ship.h"
#include "clients.h"
#include "ship_packets.h"

/* The key for accessing our thread-specific receive buffer. */
static pthread_key_t recvbuf_key;

/* The key for accessing our thread-specific send buffer. */
pthread_key_t sendbuf_key;

/* Destructor for the thread-specific receive buffer */
static void buf_dtor(void *rb) {
    free(rb);
}

/* Initialize the clients system, allocating any thread specific keys */
int client_init() {
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
void client_shutdown() {
    pthread_key_delete(recvbuf_key);
    pthread_key_delete(sendbuf_key);
}

/* Create a new connection, storing it in the list of clients. */
ship_client_t *client_create_connection(int sock, int version, int type,
                                        struct client_queue *clients,
                                        ship_t *ship, block_t *block,
                                        in_addr_t addr) {
    ship_client_t *rv = (ship_client_t *)malloc(sizeof(ship_client_t));
    uint32_t client_seed_dc, server_seed_dc;
    pthread_mutexattr_t attr;

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
            return NULL;
        }

        memset(rv->pl, 0, sizeof(player_t));
    }

    /* Store basic parameters in the client structure. */
    rv->sock = sock;
    rv->type = type;
    rv->version = version;
    rv->cur_ship = ship;
    rv->cur_block = block;
    rv->addr = addr;
    rv->arrow = 1;

    switch(version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            /* Generate the encryption keys for the client and server. */
            client_seed_dc = genrand_int32();
            server_seed_dc = genrand_int32();

            CRYPT_CreateKeys(&rv->skey, &server_seed_dc, CRYPT_PC);
            CRYPT_CreateKeys(&rv->ckey, &client_seed_dc, CRYPT_PC);
            rv->hdr_size = 4;

            /* Send the client the welcome packet, or die trying. */
            if(send_dc_welcome(rv, server_seed_dc, client_seed_dc)) {
                close(sock);
                free(rv);
                return NULL;
            }

            break;
    }

    /* Create the mutex */
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&rv->mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    /* Insert it at the end of our list, and we're done. */
    TAILQ_INSERT_TAIL(clients, rv, qentry);
    ship_inc_clients(ship);

    return rv;
}

/* Destroy a connection, closing the socket and removing it from the list. */
void client_destroy_connection(ship_client_t *c, struct client_queue *clients) {
    TAILQ_REMOVE(clients, c, qentry);

    pthread_mutex_destroy(&c->mutex);
    ship_dec_clients(c->cur_ship);

    if(c->sock >= 0) {
        close(c->sock);
    }

    if(c->recvbuf) {
        free(c->recvbuf);
    }

    if(c->sendbuf) {
        free(c->sendbuf);
    }

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

    debug(DBG_LOG, "Read %d from %d\n", (int)sz, c->guildcard);

    sz += c->recvbuf_cur;
    c->recvbuf_cur = 0;
    rbp = recvbuf;

    /* As long as what we have is long enough, decrypt it. */
    if(sz >= c->hdr_size) {
        while(sz >= c->hdr_size && rv == 0) {
            /* Decrypt the packet header so we know what exactly we're looking
               for, in terms of packet length. */
            if(!c->hdr_read) {
                memcpy(&c->pkt, rbp, c->hdr_size);
                CRYPT_CryptData(&c->ckey, &c->pkt, c->hdr_size, 0);
                c->hdr_read = 1;
            }

            /* Read the packet size to see how much we're expecting. */
            switch(c->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                    pkt_sz = LE16(c->pkt.dc.pkt_len);
                    break;
                default:
                    return -1;
            }

            /* We'll always need a multiple of 8 or 4 (depending on the type of
               the client) bytes. */
            if(pkt_sz & (c->hdr_size - 1)) {
                pkt_sz = (pkt_sz & (0x10000 - c->hdr_size)) + c->hdr_size;
            }

            /* Do we have the whole packet? */
            if(sz >= (ssize_t)pkt_sz) {
                /* Yes, we do, decrypt it. */
                CRYPT_CryptData(&c->ckey, rbp + c->hdr_size,
                                pkt_sz - c->hdr_size, 0);
                memcpy(rbp, &c->pkt, c->hdr_size);

                /* Pass it onto the correct handler. */
                switch(c->type) {
                    case CLIENT_TYPE_SHIP:
                        rv = ship_process_pkt(c, rbp);
                        break;

                    case CLIENT_TYPE_BLOCK:
                        rv = block_process_pkt(c, rbp);
                        break;
                }

                rbp += pkt_sz;
                sz -= pkt_sz;

                c->hdr_read = 0;
            }
            else {
                /* Nope, we're missing part, break out of the loop, and buffer
                   the remaining data. */
                break;
            }
        }
    }

    /* If we've still got something left here, buffer it for the next pass. */
    if(sz && !rv) {
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
uint8_t *get_recvbuf() {
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
