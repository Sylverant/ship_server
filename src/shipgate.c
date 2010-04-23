/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010 Lawrence Sebald

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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <openssl/rc4.h>

#include <sylverant/config.h>
#include <sylverant/debug.h>
#include <sylverant/sha4.h>

#include "ship.h"
#include "clients.h"
#include "shipgate.h"
#include "ship_packets.h"

/* Configuration data for the server. */
extern sylverant_shipcfg_t *cfg;
extern in_addr_t local_addr;

/* Forward declaration */
static int send_greply(shipgate_conn_t *c, uint32_t gc1, uint32_t gc2,
                       in_addr_t ip, uint16_t port, char game[], int block,
                       char ship[], uint32_t lobby, char name[], uint32_t sid);

/* Send a raw packet away. */
static int send_raw(shipgate_conn_t *c, int len, uint8_t *sendbuf) {
    ssize_t rv, total = 0;
    void *tmp;

    /* Keep trying until the whole thing's sent. */
    if(!c->sendbuf_cur) {
        while(total < len) {
            rv = send(c->sock, sendbuf + total, len - total, 0);

            if(rv == -1 && errno != EAGAIN) {
                return -1;
            }
            else if(rv == -1) {
                break;
            }

            total += rv;
        }
    }

    rv = len - total;

    if(rv) {
        /* Move out any already transferred data. */
        if(c->sendbuf_start) {
            memmove(c->sendbuf, c->sendbuf + c->sendbuf_start,
                    c->sendbuf_cur - c->sendbuf_start);
            c->sendbuf_cur -= c->sendbuf_start;
            c->sendbuf_start = 0;
        }

        /* See if we need to reallocate the buffer. */
        if(c->sendbuf_cur + rv > c->sendbuf_size) {
            tmp = realloc(c->sendbuf, c->sendbuf_cur + rv);

            /* If we can't allocate the space, bail. */
            if(tmp == NULL) {
                return -1;
            }

            c->sendbuf_size = c->sendbuf_cur + rv;
            c->sendbuf = (unsigned char *)tmp;
        }

        /* Copy what's left of the packet into the output buffer. */
        memcpy(c->sendbuf + c->sendbuf_cur, sendbuf + total, rv);
        c->sendbuf_cur += rv;
    }

    return 0;
}

/* Encrypt a packet, and send it away. */
static int send_crypt(shipgate_conn_t *c, int len, uint8_t *sendbuf) {
    /* Make sure its more than just a header, and encrypt the body. */
    if(len > 8) {
        RC4(&c->ship_key, len - 8, sendbuf + 8, sendbuf + 8);
    }

    return send_raw(c, len, sendbuf);
}

/* Send a ping packet to the server. */
int shipgate_send_ping(shipgate_conn_t *c, int reply) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_hdr_t *pkt = (shipgate_hdr_t *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the header. */
    pkt->pkt_len = htons(sizeof(shipgate_hdr_t));
    pkt->pkt_type = htons(SHDR_TYPE_PING);
    pkt->pkt_unc_len = htons(sizeof(shipgate_hdr_t));

    if(reply) {
        pkt->flags = htons(SHDR_NO_DEFLATE | SHDR_NO_ENCRYPT | SHDR_RESPONSE);
    }
    else {
        pkt->flags = htons(SHDR_NO_DEFLATE | SHDR_NO_ENCRYPT);
    }

    /* Send it away. */
    return send_raw(c, sizeof(shipgate_hdr_t), sendbuf);
}

/* Attempt to connect to the shipgate. Returns < 0 on error, returns the socket
   for communciation on success. */
int shipgate_connect(ship_t *s, shipgate_conn_t *rv) {
    int sock, i;
    struct sockaddr_in addr;
    shipgate_login_pkt pkt;
    FILE *fp;
    uint8_t key[128], hash[64];
    uint32_t key_idx;

    /* Clear it first. */
    memset(rv, 0, sizeof(shipgate_conn_t));

    /* Attempt to read the ship key. */
    debug(DBG_LOG, "Loading shipgate key...\n");

    fp = fopen(s->cfg->key_file, "rb");

    if(!fp) {
        debug(DBG_ERROR, "Couldn't load key!\n");
    }

    /* Read the file data. */
    fread(&key_idx, 1, 4, fp);
    fread(key, 1, 128, fp);
    fclose(fp);

    rv->key_idx = (uint16_t)LE32(key_idx);

    debug(DBG_LOG, "Connecting to shipgate...\n");

    /* Create the socket for the connection. */
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(sock < 0) {
        perror("socket");
        return -1;
    }

    /* Connect the socket to the shipgate. */
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = cfg->shipgate_ip;
    addr.sin_port = htons(cfg->shipgate_port);
    memset(addr.sin_zero, 0, 8);

    if(connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in))) {
        perror("connect");
        close(sock);
        return -2;
    }

    /* Wait for the shipgate to respond back. */
    if(recv(sock, &pkt, SHIPGATE_LOGIN_SIZE, 0) != SHIPGATE_LOGIN_SIZE) {
        debug(DBG_ERROR, "shipgate: Incorrect reply!\n");
        close(sock);
        return -3;
    }

    /* Check the header of the packet. */
    if(ntohs(pkt.hdr.pkt_len) != SHIPGATE_LOGIN_SIZE ||
       ntohs(pkt.hdr.pkt_type) != SHDR_TYPE_LOGIN ||
       ntohs(pkt.hdr.pkt_unc_len) != SHIPGATE_LOGIN_SIZE ||
       ntohs(pkt.hdr.flags) != (SHDR_NO_DEFLATE | SHDR_NO_ENCRYPT)) {
        debug(DBG_ERROR, "shipgate: Bad header!\n");
        close(sock);
        return -4;
    }

    /* Check the copyright message of the packet. */
    if(strcmp(pkt.msg, shipgate_login_msg)) {
        debug(DBG_ERROR, "shipgate: Incorrect message!\n");
        close(sock);
        return -5;
    }

    debug(DBG_LOG, "shipgate: Connected to Shipgate Version %d.%d.%d\n",
          (int)pkt.ver_major, (int)pkt.ver_minor, (int)pkt.ver_micro);

    /* Apply the shipgate's nonce first, then ours. */
    for(i = 0; i < 128; i += 4) {
        key[i + 0] ^= pkt.gate_nonce[0];
        key[i + 1] ^= pkt.gate_nonce[1];
        key[i + 2] ^= pkt.gate_nonce[2];
        key[i + 3] ^= pkt.gate_nonce[3];
    }

    /* Hash the key with SHA-512, and use that as our final key. */
    sha4(key, 128, hash, 0);
    RC4_set_key(&rv->gate_key, 64, hash);

    /* Calculate the final ship key. */
    for(i = 0; i < 128; i += 4) {
        key[i + 0] ^= pkt.ship_nonce[0];
        key[i + 1] ^= pkt.ship_nonce[1];
        key[i + 2] ^= pkt.ship_nonce[2];
        key[i + 3] ^= pkt.ship_nonce[3];
    }

    /* Hash the key with SHA-512, and use that as our final key. */
    sha4(key, 128, hash, 0);
    RC4_set_key(&rv->ship_key, 64, hash);

    /* Save a few other things in the struct */
    rv->sock = sock;
    rv->ship = s;

    return 0;
}

/* Send the shipgate a character data save request. */
int shipgate_send_cdata(shipgate_conn_t *c, uint32_t gc, uint32_t slot,
                        void *cdata) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_char_data_pkt *pkt = (shipgate_char_data_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the header. */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_char_data_pkt));
    pkt->hdr.pkt_type = htons(SHDR_TYPE_CDATA);
    pkt->hdr.pkt_unc_len = htons(sizeof(shipgate_char_data_pkt));
    pkt->hdr.flags = htons(SHDR_NO_DEFLATE);

    /* Fill in the body. */
    pkt->guildcard = htonl(gc);
    pkt->slot = htonl(slot);
    pkt->padding = 0;
    memcpy(pkt->data, cdata, 1052); 

    /* Send it away. */
    return send_crypt(c, sizeof(shipgate_char_data_pkt), sendbuf);
}

/* Send the shipgate a request for character data. */
int shipgate_send_creq(shipgate_conn_t *c, uint32_t gc, uint32_t slot) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_char_req_pkt *pkt = (shipgate_char_req_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the header and the body. */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_char_req_pkt));
    pkt->hdr.pkt_type = htons(SHDR_TYPE_CREQ);
    pkt->hdr.pkt_unc_len = htons(sizeof(shipgate_char_req_pkt));
    pkt->hdr.flags = htons(SHDR_NO_DEFLATE);
    pkt->guildcard = htonl(gc);
    pkt->slot = htonl(slot);

    /* Send it away. */
    return send_crypt(c, sizeof(shipgate_char_req_pkt), sendbuf);
}

static int handle_dc_greply(shipgate_conn_t *conn, dc_guild_reply_pkt *pkt) {
    int i;
    ship_t *s = conn->ship;
    block_t *b;
    ship_client_t *c;
    uint32_t dest = LE32(pkt->gc_search);
    int done = 0, rv = 0;

    for(i = 0; i < s->cfg->blocks && !done; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_mutex_lock(&b->mutex);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest) {
                    /* Forward the packet there. */
                    rv = send_pkt_dc(c, (dc_pkt_hdr_t *)pkt);
                    done = 1;
                }

                pthread_mutex_unlock(&c->mutex);

                if(done) {
                    pthread_mutex_unlock(&b->mutex);
                    break;
                }
            }

            pthread_mutex_unlock(&b->mutex);
        }
    }

    return rv;
}

static int handle_dc_mail(shipgate_conn_t *conn, dc_simple_mail_pkt *pkt) {
    int i;
    ship_t *s = conn->ship;
    block_t *b;
    ship_client_t *c;
    uint32_t dest = LE32(pkt->gc_dest);
    int done = 0, rv = 0;

    for(i = 0; i < s->cfg->blocks && !done; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_mutex_lock(&b->mutex);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest && c->pl) {
                    /* Make sure the user hasn't blacklisted the sender. */
                    if(client_has_blacklisted(c, LE32(pkt->gc_sender))) {
                        done = 1;
                        pthread_mutex_unlock(&c->mutex);
                        break;
                    }

                    /* Check if the user has an autoreply set. */
                    if(c->autoreply) {
                        dc_simple_mail_pkt rep;
                        memset(&rep, 0, sizeof(rep));

                        rep.hdr.pkt_type = SHIP_SIMPLE_MAIL_TYPE;
                        rep.hdr.flags = 0;
                        rep.hdr.pkt_len = LE16(SHIP_DC_SIMPLE_MAIL_LENGTH);

                        rep.tag = LE32(0x00010000);
                        rep.gc_sender = pkt->gc_dest;
                        rep.gc_dest = pkt->gc_sender;

                        strcpy(rep.name, c->pl->v1.name);
                        strcpy(rep.stuff, c->autoreply);
                        shipgate_fw_dc(&c->cur_ship->sg, (dc_pkt_hdr_t *)&rep);
                    }

                    /* Forward the packet there. */
                    rv = send_simple_mail(CLIENT_VERSION_DCV1, c,
                                          (dc_pkt_hdr_t *)pkt);
                    done = 1;
                }

                pthread_mutex_unlock(&c->mutex);

                if(done) {
                    pthread_mutex_unlock(&b->mutex);
                    break;
                }
            }

            pthread_mutex_unlock(&b->mutex);
        }
    }

    return rv;
}

static int handle_pc_mail(shipgate_conn_t *conn, pc_simple_mail_pkt *pkt) {
    int i;
    ship_t *s = conn->ship;
    block_t *b;
    ship_client_t *c;
    uint32_t dest = LE32(pkt->gc_dest);
    int done = 0, rv = 0;

    for(i = 0; i < s->cfg->blocks && !done; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_mutex_lock(&b->mutex);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest && c->pl) {
                    /* Make sure the user hasn't blacklisted the sender. */
                    if(client_has_blacklisted(c, LE32(pkt->gc_sender))) {
                        done = 1;
                        pthread_mutex_unlock(&c->mutex);
                        break;
                    }

                    /* Check if the user has an autoreply set. */
                    if(c->autoreply) {
                        dc_simple_mail_pkt rep;
                        memset(&rep, 0, sizeof(rep));

                        rep.hdr.pkt_type = SHIP_SIMPLE_MAIL_TYPE;
                        rep.hdr.flags = 0;
                        rep.hdr.pkt_len = LE16(SHIP_DC_SIMPLE_MAIL_LENGTH);

                        rep.tag = LE32(0x00010000);
                        rep.gc_sender = pkt->gc_dest;
                        rep.gc_dest = pkt->gc_sender;

                        strcpy(rep.name, c->pl->v1.name);
                        strcpy(rep.stuff, c->autoreply);
                        shipgate_fw_dc(&c->cur_ship->sg, (dc_pkt_hdr_t *)&rep);
                    }

                    /* Forward the packet there. */
                    rv = send_simple_mail(CLIENT_VERSION_PC, c,
                                          (dc_pkt_hdr_t *)pkt);
                    done = 1;
                }

                pthread_mutex_unlock(&c->mutex);

                if(done) {
                    pthread_mutex_unlock(&b->mutex);
                    break;
                }
            }

            pthread_mutex_unlock(&b->mutex);
        }
    }

    return rv;
}

static int handle_dc_gsearch(shipgate_conn_t *conn, dc_guild_search_pkt *pkt,
                             uint32_t sid) {
    int i;
    ship_t *s = conn->ship;
    block_t *b;
    ship_client_t *c;
    uint32_t dest = LE32(pkt->gc_target);
    int done = 0, rv = 0;

    for(i = 0; i < s->cfg->blocks && !done; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_mutex_lock(&b->mutex);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest && c->pl) {
                    /* We've found them, reply */
                    rv = send_greply(conn, pkt->gc_search, pkt->gc_target,
                                     s->cfg->ship_ip, b->dc_port,
                                     c->cur_lobby->name, b->b, s->cfg->name,
                                     c->cur_lobby->lobby_id, c->pl->v1.name,
                                     sid);
                    done = 1;
                }
                else if(c->guildcard) {
                    /* Act like they don't exist for right now (they don't
                       really exist right now) */
                    rv = 0;
                    done = 1;
                }

                pthread_mutex_unlock(&c->mutex);

                if(done) {
                    pthread_mutex_unlock(&b->mutex);
                    break;
                }
            }

            pthread_mutex_unlock(&b->mutex);
        }
    }

    return rv;
}

static int handle_dc(shipgate_conn_t *conn, shipgate_fw_pkt *pkt) {
    dc_pkt_hdr_t *dc = (dc_pkt_hdr_t *)pkt->pkt;
    uint8_t type = dc->pkt_type;

    switch(type) {
        case SHIP_DC_GUILD_REPLY_TYPE:
            return handle_dc_greply(conn, (dc_guild_reply_pkt *)dc);

        case SHIP_GUILD_SEARCH_TYPE:
            return handle_dc_gsearch(conn, (dc_guild_search_pkt *)dc,
                                     pkt->ship_id);

        case SHIP_SIMPLE_MAIL_TYPE:
            return handle_dc_mail(conn, (dc_simple_mail_pkt *)dc);
    }

    return -2;
}

static int handle_pc(shipgate_conn_t *conn, shipgate_fw_pkt *pkt) {
    dc_pkt_hdr_t *dc = (dc_pkt_hdr_t *)pkt->pkt;
    uint8_t type = dc->pkt_type;

    switch(type) {
        case SHIP_SIMPLE_MAIL_TYPE:
            return handle_pc_mail(conn, (pc_simple_mail_pkt *)dc);
    }

    return -2;
}

static int handle_sstatus(shipgate_conn_t *conn, shipgate_ship_status_pkt *p) {
    uint16_t status = ntohs(p->status);
    uint32_t sid = ntohl(p->ship_id);
    ship_t *s = conn->ship;
    int i;
    void *tmp;

    if(!status) {
        /* A ship has gone down */
        for(i = 0; i < s->ship_count; ++i) {
            /* Clear the ship */
            if(sid == s->ships[i].ship_id) {
                s->ships[i].ship_id = 0;
            }
        }
    }
    else {
        /* A ship has come up */
        for(i = 0; i < s->ship_count; ++i) {
            /* See if we have any empty spots first */
            if(s->ships[i].ship_id == 0) {
                goto insert;
            }
        }

        /* No space if we get here. */
        tmp = realloc(s->ships, (s->ship_count + 1) * sizeof(miniship_t));

        if(!tmp) {
            /* Didn't get the space? Punt. */
            return 0;
        }

        s->ships = (miniship_t *)tmp;
        ++s->ship_count;

insert:
        memcpy(s->ships[i].name, p->name, 12);
        s->ships[i].ship_id = sid;
        s->ships[i].ship_addr = p->ship_addr;
        s->ships[i].int_addr = p->int_addr;
        s->ships[i].ship_port = ntohs(p->ship_port);
        s->ships[i].flags = ntohl(p->flags);
    }

    return 0;
}

static int handle_cdata(shipgate_conn_t *conn, shipgate_char_data_pkt *pkt) {
    int i;
    ship_t *s = conn->ship;
    block_t *b;
    ship_client_t *c;
    uint32_t dest = ntohl(pkt->guildcard);
    int done = 0;

    for(i = 0; i < s->cfg->blocks && !done; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_mutex_lock(&b->mutex);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest && c->pl) {
                    /* We've found them, overwrite their data, and send the
                       refresh packet. */
                    memcpy(c->pl, pkt->data, 1052);
                    send_lobby_join(c, c->cur_lobby);
                    done = 1;
                }
                else if(c->guildcard) {
                    /* Act like they don't exist for right now (they don't
                       really exist right now) */
                    done = 1;
                }

                pthread_mutex_unlock(&c->mutex);

                if(done) {
                    pthread_mutex_unlock(&b->mutex);
                    break;
                }
            }

            pthread_mutex_unlock(&b->mutex);
        }
    }

    return 0;
}

static int handle_gmlogin(shipgate_conn_t *conn,
                          shipgate_gmlogin_reply_pkt *pkt) {
    uint16_t flags = ntohs(pkt->hdr.flags);
    uint32_t gc = ntohl(pkt->guildcard);
    uint32_t block = ntohl(pkt->block);
    ship_t *s = conn->ship;
    block_t *b;
    ship_client_t *i;
    int rv = 0;

    /* Check the block number first. */
    if(block > s->cfg->blocks) {
        return 0;
    }

    b = s->blocks[block - 1];
    pthread_mutex_lock(&b->mutex);

    /* Find the requested client. */
    TAILQ_FOREACH(i, b->clients, qentry) {
        if(i->guildcard == gc) {
            if(flags & SHDR_RESPONSE) {
                i->is_gm = 2;
                rv = send_txt(i, "\tE\tC7GM Login Successful");
            }
            else {
                rv = send_txt(i, "\tE\tC7Nice Try!");
            }

            goto out;
        }
    }

out:
    pthread_mutex_unlock(&b->mutex);
    return rv;
}

static int handle_pkt(shipgate_conn_t *conn, shipgate_hdr_t *pkt) {
    uint16_t type = ntohs(pkt->pkt_type);
    uint16_t flags = ntohs(pkt->flags);

    switch(type) {
        case SHDR_TYPE_DC:
            return handle_dc(conn, (shipgate_fw_pkt *)pkt);

        case SHDR_TYPE_PC:
            return handle_pc(conn, (shipgate_fw_pkt *)pkt);

        case SHDR_TYPE_SSTATUS:
            return handle_sstatus(conn, (shipgate_ship_status_pkt *)pkt);

        case SHDR_TYPE_PING:
            /* Ignore responses for now... we don't send these just yet. */
            if(flags & SHDR_RESPONSE) {
                return 0;
            }

            return shipgate_send_ping(conn, 1);

        case SHDR_TYPE_CDATA:
            return handle_cdata(conn, (shipgate_char_data_pkt *)pkt);

        case SHDR_TYPE_GMLOGIN:
            return handle_gmlogin(conn, (shipgate_gmlogin_reply_pkt *)pkt);
    }

    return -1;
}

/* Read data from the shipgate. */
int shipgate_process_pkt(shipgate_conn_t *c) {
    ssize_t sz;
    uint16_t pkt_sz;
    int rv = 0;
    unsigned char *rbp;
    shipgate_hdr_t pkt;
    uint8_t *recvbuf = get_recvbuf();
    void *tmp;

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
    while(sz >= 8 && rv == 0) {
        /* Copy out the packet header so we know what exactly we're looking
           for, in terms of packet length. */
        memcpy(&pkt, rbp, 8);

        /* Read the packet size to see how much we're expecting. */
        pkt_sz = ntohs(pkt.pkt_len);

        /* We'll always need a multiple of 8 bytes. */
        if(pkt_sz & 0x07) {
            pkt_sz = (pkt_sz & 0xFFF8) + 8;
        }

        /* Do we have the whole packet? */
        if(sz >= (ssize_t)pkt_sz) {
            /* Yes, we do, decrypt it. */
            if(!(c->pkt.flags & SHDR_NO_ENCRYPT)) {
                RC4(&c->gate_key, pkt_sz - 8, rbp + 8, rbp + 8);
            }

            memcpy(rbp, &pkt, 8);

            /* Pass it on. */
            if(handle_pkt(c, (shipgate_hdr_t *)rbp)) {
                rv = -1;
                break;
            }

            rbp += pkt_sz;
            sz -= pkt_sz;
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

/* Send any piled up data. */
int shipgate_send_pkts(shipgate_conn_t *c) {
    ssize_t amt;

    /* Send as much as we can. */
    amt = send(c->sock, c->sendbuf, c->sendbuf_cur, 0);

    if(amt == -1) {
        perror("send");
        return -1;
    }
    else if(amt == c->sendbuf_cur) {
        c->sendbuf_cur = 0;
    }
    else {
        memmove(c->sendbuf, c->sendbuf + amt, c->sendbuf_cur - amt);
        c->sendbuf_cur -= amt;
    }

    return 0;
}

/* Packets are below here. */
/* Send a newly opened ship's information to the shipgate. */
int shipgate_send_ship_info(shipgate_conn_t *c, ship_t *ship) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_login_reply_pkt *pkt = (shipgate_login_reply_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Clear the packet first */
    memset(pkt, 0, sizeof(shipgate_login_reply_pkt));

    /* Fill in the header. */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_login_reply_pkt));
    pkt->hdr.pkt_type = htons(SHDR_TYPE_LOGIN);
    pkt->hdr.pkt_unc_len = htons(sizeof(shipgate_login_reply_pkt));
    pkt->hdr.flags = htons(SHDR_NO_DEFLATE | SHDR_NO_ENCRYPT | SHDR_RESPONSE);

    /* Fill in the packet. */
    strcpy(pkt->name, ship->cfg->name);
    pkt->ship_addr = ship->cfg->ship_ip;
    pkt->int_addr = local_addr;
    pkt->ship_port = htons(ship->cfg->base_port);
    pkt->ship_key = htons(c->key_idx);
    pkt->connections = 0;
    pkt->flags = 0;

    /* Send it away */
    return send_raw(c, sizeof(shipgate_login_reply_pkt), sendbuf);
}

/* Send a client count update to the shipgate. */
int shipgate_send_cnt(shipgate_conn_t *c, uint16_t ccnt, uint16_t gcnt) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_cnt_pkt *pkt = (shipgate_cnt_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the header. */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_cnt_pkt));
    pkt->hdr.pkt_type = htons(SHDR_TYPE_COUNT);
    pkt->hdr.pkt_unc_len = htons(sizeof(shipgate_cnt_pkt));
    pkt->hdr.flags = htons(SHDR_NO_DEFLATE);

    /* Fill in the packet. */
    pkt->ccnt = htons(ccnt);
    pkt->gcnt = htons(gcnt);
    pkt->padding = 0;

    /* Send it away */
    return send_crypt(c, sizeof(shipgate_cnt_pkt), sendbuf);
}

/* Forward a Dreamcast packet to the shipgate. */
int shipgate_fw_dc(shipgate_conn_t *c, void *dcp) {
    uint8_t *sendbuf = get_sendbuf();
    dc_pkt_hdr_t *dc = (dc_pkt_hdr_t *)dcp;
    shipgate_fw_pkt *pkt = (shipgate_fw_pkt *)sendbuf;
    int dc_len = LE16(dc->pkt_len);
    int full_len = sizeof(shipgate_fw_pkt) + dc_len;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Copy the packet, unchanged */
    memmove(&pkt->pkt, dc, dc_len);

    /* Round up the packet size, if needed. */
    if(full_len & 0x07)
        full_len = (full_len + 8) & 0xFFF8;

    /* Fill in the shipgate header */
    pkt->hdr.pkt_len = htons(full_len);
    pkt->hdr.pkt_type = htons(SHDR_TYPE_DC);
    pkt->hdr.pkt_unc_len = htons(full_len);
    pkt->hdr.flags = htons(SHDR_NO_DEFLATE);

    /* Send the packet away */
    return send_crypt(c, full_len, sendbuf);
}

/* Forward a PC packet to the shipgate. */
int shipgate_fw_pc(shipgate_conn_t *c, void *pcp) {
    uint8_t *sendbuf = get_sendbuf();
    pc_pkt_hdr_t *pc = (pc_pkt_hdr_t *)pcp;
    shipgate_fw_pkt *pkt = (shipgate_fw_pkt *)sendbuf;
    int pc_len = LE16(pc->pkt_len);
    int full_len = sizeof(shipgate_fw_pkt) + pc_len;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Copy the packet, unchanged */
    memmove(&pkt->pkt, pc, pc_len);

    /* Round up the packet size, if needed. */
    if(full_len & 0x07)
        full_len = (full_len + 8) & 0xFFF8;

    /* Fill in the shipgate header */
    pkt->hdr.pkt_len = htons(full_len);
    pkt->hdr.pkt_type = htons(SHDR_TYPE_PC);
    pkt->hdr.pkt_unc_len = htons(full_len);
    pkt->hdr.flags = htons(SHDR_NO_DEFLATE);

    /* Send the packet away */
    return send_crypt(c, full_len, sendbuf);
}

/* Send a GM login request. */
int shipgate_send_gmlogin(shipgate_conn_t *c, uint32_t gc, uint32_t block,
                          char *username, char *password) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_gmlogin_req_pkt *pkt = (shipgate_gmlogin_req_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the data. */
    memset(pkt, 0, sizeof(shipgate_gmlogin_req_pkt));

    pkt->hdr.pkt_len = htons(sizeof(shipgate_gmlogin_req_pkt));
    pkt->hdr.pkt_type = htons(SHDR_TYPE_GMLOGIN);
    pkt->hdr.pkt_unc_len = pkt->hdr.pkt_len;
    pkt->hdr.flags = htons(SHDR_NO_DEFLATE);

    pkt->guildcard = htonl(gc);
    pkt->block = htonl(block);
    strcpy(pkt->username, username);
    strcpy(pkt->password, password);

    /* Send the packet away */
    return send_crypt(c, sizeof(shipgate_gmlogin_req_pkt), sendbuf);
}

/* Send a ban request. */
int shipgate_send_ban(shipgate_conn_t *c, uint16_t type, uint32_t requester,
                      uint32_t target, uint32_t until, char *msg) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_ban_req_pkt *pkt = (shipgate_ban_req_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Make sure we're requesting something valid. */
    switch(type) {
        case SHDR_TYPE_IPBAN:
        case SHDR_TYPE_GCBAN:
            break;

        default:
            return -1;
    }

    /* Fill in the data. */
    memset(pkt, 0, sizeof(shipgate_ban_req_pkt));

    pkt->hdr.pkt_len = htons(sizeof(shipgate_ban_req_pkt));
    pkt->hdr.pkt_type = htons(type);
    pkt->hdr.pkt_unc_len = pkt->hdr.pkt_len;
    pkt->hdr.flags = htons(SHDR_NO_DEFLATE);

    pkt->req_gc = htonl(requester);
    pkt->target = htonl(target);
    pkt->until = htonl(until);
    strncpy(pkt->message, msg, 255); 

    /* Send the packet away */
    return send_crypt(c, sizeof(shipgate_ban_req_pkt), sendbuf);
}

static int send_greply(shipgate_conn_t *c, uint32_t gc1, uint32_t gc2,
                       in_addr_t ip, uint16_t port, char game[], int block,
                       char ship[], uint32_t lobby, char name[], uint32_t sid) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_fw_pkt *pkt = (shipgate_fw_pkt *)sendbuf;
    dc_guild_reply_pkt *dc = (dc_guild_reply_pkt *)pkt->pkt;
    int full_len = sizeof(shipgate_fw_pkt) + SHIP_DC_GUILD_REPLY_LENGTH;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Round up the packet size, if needed. */
    if(full_len & 0x07)
        full_len = (full_len + 8) & 0xFFF8;

    /* Scrub the buffer */
    memset(pkt, 0, full_len);

    /* Fill in the shipgate header */
    pkt->hdr.pkt_len = htons(full_len);
    pkt->hdr.pkt_type = htons(SHDR_TYPE_DC);
    pkt->hdr.pkt_unc_len = htons(full_len);
    pkt->hdr.flags = htons(SHDR_NO_DEFLATE);
    pkt->ship_id = sid;

    /* Fill in the Dreamcast packet stuff */
    dc->hdr.pkt_type = SHIP_DC_GUILD_REPLY_TYPE;
    dc->hdr.pkt_len = LE16(SHIP_DC_GUILD_REPLY_LENGTH);
    dc->tag = LE32(0x00010000);
    dc->gc_search = gc1;
    dc->gc_target = gc2;
    dc->ip = ip;
    dc->port = LE16(port);
    dc->menu_id = LE32(0xFFFFFFFF);
    dc->item_id = LE32(lobby);
    strcpy(dc->name, name);

    /* Fill in the location string */
    sprintf(dc->location, "%s,BLOCK%02d,%s", game, block, ship);

    /* Send the packet away */
    return send_crypt(c, full_len, sendbuf);
}
