/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010 Lawrence Sebald

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
#include <sys/socket.h>
#include <iconv.h>

#include <sylverant/encryption.h>
#include <sylverant/mtwist.h>
#include <sylverant/debug.h>

#include "ship.h"
#include "utils.h"
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
    rv->last_message = time(NULL);

    switch(version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
            /* Generate the encryption keys for the client and server. */
            client_seed_dc = genrand_int32();
            server_seed_dc = genrand_int32();

            CRYPT_CreateKeys(&rv->skey, &server_seed_dc, CRYPT_PC);
            CRYPT_CreateKeys(&rv->ckey, &client_seed_dc, CRYPT_PC);
            rv->hdr_size = 4;

            /* Send the client the welcome packet, or die trying. */
            if(send_dc_welcome(rv, server_seed_dc, client_seed_dc)) {
                close(sock);

                if(type == CLIENT_TYPE_BLOCK) {
                    free(rv->pl);
                }

                free(rv);
                return NULL;
            }

            break;

        case CLIENT_VERSION_GC:
            /* Generate the encryption keys for the client and server. */
            client_seed_dc = genrand_int32();
            server_seed_dc = genrand_int32();

            CRYPT_CreateKeys(&rv->skey, &server_seed_dc, CRYPT_GAMECUBE);
            CRYPT_CreateKeys(&rv->ckey, &client_seed_dc, CRYPT_GAMECUBE);
            rv->hdr_size = 4;

            /* Send the client the welcome packet, or die trying. */
            if(send_dc_welcome(rv, server_seed_dc, client_seed_dc)) {
                close(sock);

                if(type == CLIENT_TYPE_BLOCK) {
                    free(rv->pl);
                }

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
    time_t now;
    char tstr[26];

    TAILQ_REMOVE(clients, c, qentry);

    /* If the user was on a block, notify the shipgate */
    if(c->type == CLIENT_TYPE_BLOCK && c->pl->v1.name[0]) {
        shipgate_send_block_login(&c->cur_ship->sg, 0, c->guildcard,
                                  c->cur_block->b, c->pl->v1.name);
    }

    pthread_mutex_destroy(&c->mutex);
    ship_dec_clients(c->cur_ship);

    /* If the client has a lobby sitting around that was created but not added
       to the list of lobbies, destroy it */
    if(c->create_lobby) {
        lobby_destroy_noremove(c->create_lobby);
    }

    /* If we were logging the user, close the file */
    if(c->logfile) {
        now = time(NULL);
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

    if(c->pl) {
        free(c->pl);
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
        if(!c->hdr_read) {
            memcpy(&c->pkt, rbp, hsz);
            CRYPT_CryptData(&c->ckey, &c->pkt, hsz, 0);
            c->hdr_read = 1;
        }

        /* Read the packet size to see how much we're expecting. */
        switch(c->version) {
            case CLIENT_VERSION_DCV1:
            case CLIENT_VERSION_DCV2:
            case CLIENT_VERSION_GC:
                pkt_sz = LE16(c->pkt.dc.pkt_len);
                break;

            case CLIENT_VERSION_PC:
                pkt_sz = LE16(c->pkt.pc.pkt_len);
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

/* Set up a simple mail autoreply. */
int client_set_autoreply(ship_client_t *c, dc_pkt_hdr_t *p) {
    char *tmp;
    autoreply_set_pkt *pkt = (autoreply_set_pkt *)p;

    if(c->version == CLIENT_VERSION_PC) {
        int len = LE16(pkt->hdr.dc.pkt_len) - 4;
        char str[len];
        iconv_t ic;
        size_t in, out;
        ICONV_CONST char *inptr;
        char *outptr;

        /* Set up for converting the string. */
        if(pkt->msg[2] == 'J') {
            ic = iconv_open("SHIFT_JIS", "UTF-16LE");
        }
        else {
            ic = iconv_open("ISO-8859-1", "UTF-16LE");
        }

        if(!ic) {
            perror("iconv_open");
            return -1;
        }

        /* Convert to the appropriate encoding. */
        out = in = len;
        inptr = pkt->msg;
        outptr = str;
        iconv(ic, &inptr, &in, &outptr, &out);
        iconv_close(ic);

        /* Allocate space for the new string. */
        tmp = (char *)malloc(strlen(pkt->msg) + 1);

        if(!tmp) {
            perror("malloc");
            return -1;
        }

        strcpy(tmp, str);
    }
    else {
        /* Allocate space for the new string. */
        tmp = (char *)malloc(strlen(pkt->msg) + 1);

        if(!tmp) {
            perror("malloc");
            return -1;
        }

        strcpy(tmp, pkt->msg);
    }

    /* Clean up any old autoreply we might have. */
    if(c->autoreply) {
        free(c->autoreply);
    }

    /* Finish up by setting the new autoreply string. */
    c->autoreply = tmp;

    return 0;
}

/* Clear the simple mail autoreply from a client (if set). */
int client_clear_autoreply(ship_client_t *c) {
    if(c->autoreply) {
        free(c->autoreply);
        c->autoreply = NULL;
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

/* Send a message to a client telling them that a friend has logged on/off */
void client_send_friendmsg(ship_client_t *c, int on, const char *fname,
                           const char *ship, uint32_t block) {
    send_txt(c, "%s %s\n%s BLOCK%02d", fname,
             on ? __(c, "online") : __(c, "offline"), ship, (int)block);
}
