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
#include "utils.h"
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
    /* Make sure its at least a header. */
    if(len < 8) {
        return -1;
    }

    RC4(&c->ship_key, len, sendbuf, sendbuf);

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
        pkt->flags = htons(SHDR_NO_DEFLATE | SHDR_RESPONSE);
    }
    else {
        pkt->flags = htons(SHDR_NO_DEFLATE);
    }

    /* Send it away. */
    return send_crypt(c, sizeof(shipgate_hdr_t), sendbuf);
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
    debug(DBG_LOG, "%s: Loading shipgate key...\n", s->cfg->name);

    fp = fopen(s->cfg->key_file, "rb");

    if(!fp) {
        debug(DBG_ERROR, "%s: Couldn't load key!\n", s->cfg->name);
        return -6;
    }

    /* Read the file data. */
    fread(&key_idx, 1, 4, fp);
    fread(key, 1, 128, fp);
    fclose(fp);

    rv->key_idx = (uint16_t)LE32(key_idx);

    debug(DBG_LOG, "%s: Connecting to shipgate...\n", s->cfg->name);

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
        debug(DBG_ERROR, "%s: Incorrect shipgate reply!\n", s->cfg->name);
        close(sock);
        return -3;
    }

    /* Check the header of the packet. */
    if(ntohs(pkt.hdr.pkt_len) != SHIPGATE_LOGIN_SIZE ||
       ntohs(pkt.hdr.pkt_type) != SHDR_TYPE_LOGIN ||
       ntohs(pkt.hdr.pkt_unc_len) != SHIPGATE_LOGIN_SIZE ||
       ntohs(pkt.hdr.flags) != (SHDR_NO_DEFLATE)) {
        debug(DBG_ERROR, "%s: Bad shipgate header!\n", s->cfg->name);
        close(sock);
        return -4;
    }

    /* Check the copyright message of the packet. */
    if(strcmp(pkt.msg, shipgate_login_msg)) {
        debug(DBG_ERROR, "%s: Incorrect shipgate message!\n", s->cfg->name);
        close(sock);
        return -5;
    }

    debug(DBG_LOG, "%s: Connected to Shipgate Version %d.%d.%d\n", s->cfg->name,
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

/* Reconnect to the shipgate if we are disconnected for some reason. */
int shipgate_reconnect(shipgate_conn_t *conn) {
    int sock;
    struct sockaddr_in addr;
    ship_t *s = conn->ship;
    miniship_t *i, *tmp;

    /* Clear all ships so we don't keep around stale stuff */
    i = TAILQ_FIRST(&s->ships);
    while(i) {
        tmp = TAILQ_NEXT(i, qentry);
        TAILQ_REMOVE(&s->ships, i, qentry);
        free(i);
        i = tmp;
    }

    conn->has_key = 0;

    debug(DBG_LOG, "%s: Reconnecting to shipgate...\n", conn->ship->cfg->name);

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

    conn->sock = sock;

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
                    send_guild_reply_sg(c, pkt);
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

                        rep.hdr.pkt_type = SIMPLE_MAIL_TYPE;
                        rep.hdr.flags = 0;
                        rep.hdr.pkt_len = LE16(DC_SIMPLE_MAIL_LENGTH);

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

                        rep.hdr.pkt_type = SIMPLE_MAIL_TYPE;
                        rep.hdr.flags = 0;
                        rep.hdr.pkt_len = LE16(DC_SIMPLE_MAIL_LENGTH);

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
                else if(c->guildcard == dest) {
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
        case GUILD_REPLY_TYPE:
            return handle_dc_greply(conn, (dc_guild_reply_pkt *)dc);

        case GUILD_SEARCH_TYPE:
            return handle_dc_gsearch(conn, (dc_guild_search_pkt *)dc,
                                     pkt->ship_id);

        case SIMPLE_MAIL_TYPE:
            return handle_dc_mail(conn, (dc_simple_mail_pkt *)dc);
    }

    return -2;
}

static int handle_pc(shipgate_conn_t *conn, shipgate_fw_pkt *pkt) {
    dc_pkt_hdr_t *dc = (dc_pkt_hdr_t *)pkt->pkt;
    uint8_t type = dc->pkt_type;

    switch(type) {
        case SIMPLE_MAIL_TYPE:
            return handle_pc_mail(conn, (pc_simple_mail_pkt *)dc);
    }

    return -2;
}

static void menu_code_sort(uint16_t *codes, int count) {
    int i, j;
    uint16_t tmp;

    /* This list shouldn't ever get too big, and will be mostly sorted to start
       with, so insertion sort should do well here. */
    for(j = 1; j < count; ++j) {
        tmp = codes[j];
        i = j - 1;

        while(i >= 0 && codes[i] > tmp) {
            codes[i + 1] = codes[i];
            --i;
        }

        codes[i + 1] = tmp;
    }
}

static int menu_code_exists(uint16_t *codes, int count, uint16_t target) {
    int i = 0, j = count, k;

    /* Simple binary search */
    while(i < j) {
        k = i + (j - i) / 2;

        if(codes[k] < target) {
            i = k + 1;
        }
        else {
            j = k;
        }
    }

    /* If we've found the value we're looking for, return success */
    if(i < count && codes[i] == target) {
        return 1;
    }

    return 0;
}

static int handle_sstatus(shipgate_conn_t *conn, shipgate_ship_status_pkt *p) {
    uint16_t status = ntohs(p->status);
    uint32_t sid = ntohl(p->ship_id);
    ship_t *s = conn->ship;
    miniship_t *i, *j;
    uint16_t code = 0;
    void *tmp;

    /* Did a ship go down or come up? */
    if(!status) {
        /* A ship has gone down */
        TAILQ_FOREACH(i, &s->ships, qentry) {
            /* Clear the ship from the list, if we've found the right one */
            if(sid == i->ship_id) {
                TAILQ_REMOVE(&s->ships, i, qentry);                
                break;
            }
        }

        /* Figure out if the menu code is still in use */
        TAILQ_FOREACH(j, &s->ships, qentry) {
            if(i->menu_code == j->menu_code) {
                code = 1;
                break;
            }
        }

        /* If the code is not in use, get rid of it */
        if(!code) {
            /* Move all higher menu codes down the list */
            for(code = 0; code < s->mccount - 1; ++code) {
                if(s->menu_codes[code] >= i->menu_code) {
                    s->menu_codes[code] = s->menu_codes[code + 1];
                }
            }

            /* Chop off the last element (its now a duplicated entry or the one
               we want to get rid of) */
            tmp = realloc(s->menu_codes, (s->mccount - 1) * sizeof(uint16_t));

            if(!tmp) {
                perror("realloc");
                s->menu_codes[s->mccount - 1] = 0xFFFF;
            }
            else {
                s->menu_codes = (uint16_t *)tmp;
                --s->mccount;
            }
        }

        /* Clean up the miniship */
        free(i);
    }
    else {
        /* Allocate space, and punt if we can't */
        i = (miniship_t *)malloc(sizeof(miniship_t));

        if(!i) {
            return 0;
        }

        /* See if we need to deal with the menu code or not here */
        code = ntohs(p->menu_code);

        if(!menu_code_exists(s->menu_codes, s->mccount, code)) {
            tmp = realloc(s->menu_codes, (s->mccount + 1) * sizeof(uint16_t));

            /* Can't make space, punt */
            if(!tmp) {
                perror("realloc");
                free(i);
                return 0;
            }

            /* Put the new code in, and sort the list */
            s->menu_codes = (uint16_t *)tmp;
            s->menu_codes[s->mccount++] = code;
            menu_code_sort(s->menu_codes, s->mccount);
        }

        memset(i, 0, sizeof(miniship_t));

        /* Add the new ship, and copy its data */
        TAILQ_INSERT_TAIL(&s->ships, i, qentry);

        memcpy(i->name, p->name, 12);
        i->ship_id = sid;
        i->ship_addr = p->ship_addr;
        i->int_addr = p->int_addr;
        i->ship_port = ntohs(p->ship_port);
        i->clients = ntohs(p->clients);
        i->games = ntohs(p->games);
        i->menu_code = code;
        i->flags = ntohl(p->flags);
    }

    return 0;
}

static int handle_creq(shipgate_conn_t *conn, shipgate_char_data_pkt *pkt) {
    int i;
    ship_t *s = conn->ship;
    block_t *b;
    ship_client_t *c;
    uint32_t dest = ntohl(pkt->guildcard);
    int done = 0;
    uint16_t flags = ntohs(pkt->hdr.flags);

    /* Make sure the packet looks sane */
    if(!(flags & SHDR_RESPONSE)) {
        return 0;
    }

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
                else if(c->guildcard == dest) {
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

    /* Make sure the packet looks sane */
    if(!(flags & SHDR_RESPONSE)) {
        return 0;
    }

    /* Check the block number first. */
    if(block > s->cfg->blocks) {
        return 0;
    }

    b = s->blocks[block - 1];
    pthread_mutex_lock(&b->mutex);

    /* Find the requested client. */
    TAILQ_FOREACH(i, b->clients, qentry) {
        if(i->guildcard == gc) {
            i->privilege |= pkt->priv;
            send_txt(i, "%s", __(i, "\tE\tC7Login Successful"));

            goto out;
        }
    }

out:
    pthread_mutex_unlock(&b->mutex);
    return rv;
}

static int handle_login(shipgate_conn_t *conn, shipgate_login_pkt *pkt) {
    int i;
    FILE *fp;
    uint8_t key[128], hash[64];
    uint32_t key_idx;

    /* Attempt to read the ship key. */
    debug(DBG_LOG, "%s: Loading shipgate key...\n", conn->ship->cfg->name);

    fp = fopen(conn->ship->cfg->key_file, "rb");

    if(!fp) {
        debug(DBG_ERROR, "%s: Couldn't load key!\n", conn->ship->cfg->name);
        return -1;
    }

    /* Read the file data. */
    fread(&key_idx, 1, 4, fp);
    fread(key, 1, 128, fp);
    fclose(fp);

    conn->key_idx = (uint16_t)LE32(key_idx);

    /* Check the header of the packet. */
    if(ntohs(pkt->hdr.pkt_len) != SHIPGATE_LOGIN_SIZE ||
       ntohs(pkt->hdr.pkt_type) != SHDR_TYPE_LOGIN ||
       ntohs(pkt->hdr.pkt_unc_len) != SHIPGATE_LOGIN_SIZE ||
       ntohs(pkt->hdr.flags) != (SHDR_NO_DEFLATE)) {
        return -2;
    }

    /* Check the copyright message of the packet. */
    if(strcmp(pkt->msg, shipgate_login_msg)) {
        return -3;
    }

    debug(DBG_LOG, "%s: Connected to Shipgate Version %d.%d.%d\n",
          conn->ship->cfg->name, (int)pkt->ver_major, (int)pkt->ver_minor,
          (int)pkt->ver_micro);

    /* Apply the shipgate's nonce first, then ours. */
    for(i = 0; i < 128; i += 4) {
        key[i + 0] ^= pkt->gate_nonce[0];
        key[i + 1] ^= pkt->gate_nonce[1];
        key[i + 2] ^= pkt->gate_nonce[2];
        key[i + 3] ^= pkt->gate_nonce[3];
    }

    /* Hash the key with SHA-512, and use that as our final key. */
    sha4(key, 128, hash, 0);
    RC4_set_key(&conn->gate_key, 64, hash);

    /* Calculate the final ship key. */
    for(i = 0; i < 128; i += 4) {
        key[i + 0] ^= pkt->ship_nonce[0];
        key[i + 1] ^= pkt->ship_nonce[1];
        key[i + 2] ^= pkt->ship_nonce[2];
        key[i + 3] ^= pkt->ship_nonce[3];
    }

    /* Hash the key with SHA-512, and use that as our final key. */
    sha4(key, 128, hash, 0);
    RC4_set_key(&conn->ship_key, 64, hash);

    /* Send our info to the shipgate so it can have things set up right. */
    return shipgate_send_ship_info(conn, conn->ship);
}

static int handle_count(shipgate_conn_t *conn, shipgate_cnt_pkt *pkt) {
    uint32_t id = ntohl(pkt->ship_id);
    miniship_t *i;
    ship_t *s = conn->ship;

    TAILQ_FOREACH(i, &s->ships, qentry) {
        /* Find the requested ship and update its counts */
        if(i->ship_id == id) {
            i->clients = ntohs(pkt->clients);
            i->games = ntohs(pkt->games);
            return 0;
        }
    }

    /* We didn't find it... this shouldn't ever happen */
    return -1;
}

static int handle_cdata(shipgate_conn_t *conn, shipgate_cdata_err_pkt *pkt) {
    int i;
    ship_t *s = conn->ship;
    block_t *b;
    ship_client_t *c;
    uint32_t dest = ntohl(pkt->guildcard);
    int done = 0;
    uint16_t flags = ntohs(pkt->base.hdr.flags);

    /* Make sure the packet looks sane */
    if(!(flags & SHDR_RESPONSE)) {
        return 0;
    }

    for(i = 0; i < s->cfg->blocks && !done; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_mutex_lock(&b->mutex);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest && c->pl) {
                    /* We've found them, figure out what to tell them. */
                    if(flags & SHDR_FAILURE) {
                        send_txt(c, "%s", __(c, "\tE\tC7Couldn't save "
                                                    "character data"));
                    }
                    else {
                        send_txt(c, "%s", __(c, "\tE\tC7Saved character data"));
                    }

                    done = 1;
                }
                else if(c->guildcard == dest) {
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

static int handle_ban(shipgate_conn_t *conn, shipgate_ban_err_pkt *pkt) {
    int i;
    ship_t *s = conn->ship;
    block_t *b;
    ship_client_t *c;
    uint32_t dest = ntohl(pkt->req_gc);
    int done = 0;
    uint16_t flags = ntohs(pkt->base.hdr.flags);

    /* Make sure the packet looks sane */
    if(!(flags & SHDR_RESPONSE) && !(flags & SHDR_FAILURE)) {
        return 0;
    }

    for(i = 0; i < s->cfg->blocks && !done; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_mutex_lock(&b->mutex);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest && c->pl) {
                    /* We've found them, figure out what to tell them. */
                    if(flags & SHDR_FAILURE) {
                        /* If the not gm flag is set, disconnect the user. */
                        if(ntohl(pkt->base.error_code) == ERR_BAN_NOT_GM) {
                            c->flags |= CLIENT_FLAG_DISCONNECTED;
                        }

                        send_txt(c, "%s", __(c, "\tE\tC7Error setting ban!"));

                    }
                    else {
                        send_txt(c, "%s", __(c, "\tE\tC7User banned"));
                    }

                    done = 1;
                }
                else if(c->guildcard == dest) {
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

static int handle_creq_err(shipgate_conn_t *conn, shipgate_cdata_err_pkt *pkt) {
    int i;
    ship_t *s = conn->ship;
    block_t *b;
    ship_client_t *c;
    uint32_t dest = ntohl(pkt->guildcard);
    int done = 0;
    uint16_t flags = ntohs(pkt->base.hdr.flags);
    uint32_t err = ntohl(pkt->base.error_code);

    /* Make sure the packet looks sane */
    if(!(flags & SHDR_FAILURE) || !(flags & SHDR_RESPONSE)) {
        return 0;
    }

    for(i = 0; i < s->cfg->blocks && !done; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_mutex_lock(&b->mutex);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest && c->pl) {
                    /* We've found them, figure out what to tell them. */
                    if(err == ERR_CREQ_NO_DATA) {
                        send_txt(c, "%s", __(c, "\tE\tC7No character data "
                                             "found"));
                    }
                    else {
                        send_txt(c, "%s", __(c, "\tE\tC7Couldn't request "
                                             "character data"));
                    }

                    done = 1;
                }
                else if(c->guildcard == dest) {
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

static int handle_gmlogin_err(shipgate_conn_t *conn, shipgate_gm_err_pkt *pkt) {
    uint16_t flags = ntohs(pkt->base.hdr.flags);
    uint32_t gc = ntohl(pkt->guildcard);
    uint32_t block = ntohl(pkt->block);
    ship_t *s = conn->ship;
    block_t *b;
    ship_client_t *i;
    int rv = 0;

    /* Make sure the packet looks sane */
    if(!(flags & SHDR_RESPONSE)) {
        return 0;
    }

    /* Check the block number first. */
    if(block > s->cfg->blocks) {
        return 0;
    }

    b = s->blocks[block - 1];
    pthread_mutex_lock(&b->mutex);

    /* Find the requested client. */
    TAILQ_FOREACH(i, b->clients, qentry) {
        if(i->guildcard == gc) {
            /* XXXX: Maybe send specific error messages sometime later */
            send_txt(i, "%s", __(i, "\tE\tC7Login failed"));

            goto out;
        }
    }

out:
    pthread_mutex_unlock(&b->mutex);
    return rv;
}

static int handle_blogin_err(shipgate_conn_t *c, shipgate_blogin_err_pkt *pkt) {
    uint32_t gc = ntohl(pkt->guildcard);
    uint32_t block = ntohl(pkt->blocknum);
    ship_t *s = c->ship;
    block_t *b;
    ship_client_t *i;

    /* Grab the block first */
    if(block > s->cfg->blocks || !(b = s->blocks[block - 1])) {
        return 0;
    }

    pthread_mutex_lock(&b->mutex);

    /* Find the requested client and boot them off (regardless of the error type
       for now) */
    TAILQ_FOREACH(i, b->clients, qentry) {
        if(i->guildcard == gc) {
            i->flags |= CLIENT_FLAG_DISCONNECTED;
        }
    }

    pthread_mutex_unlock(&b->mutex);

    return 0;
}

static int handle_login_reply(shipgate_conn_t *conn, shipgate_error_pkt *pkt) {
    uint32_t err = ntohl(pkt->error_code);
    uint16_t flags = ntohs(pkt->hdr.flags);
    ship_t *s = conn->ship;

    /* Make sure the packet looks sane */
    if(!(flags & SHDR_RESPONSE)) {
        return -1;
    }

    /* Is this an error or success? */
    if(flags & SHDR_FAILURE) {
        switch(err) {
            case ERR_LOGIN_BAD_PROTO:
                debug(DBG_LOG, "%s: Unsupported shipgate protocol version!\n",
                      s->cfg->name);
                break;

            case ERR_BAD_ERROR:
                debug(DBG_LOG, "%s: Shipgate having issues, try again later.\n",
                      s->cfg->name);
                break;

            case ERR_LOGIN_BAD_KEY:
                debug(DBG_LOG, "%s: Invalid key!\n", s->cfg->name);
                break;

            case ERR_LOGIN_BAD_MENU:
                debug(DBG_LOG, "%s: Invalid menu code!\n", s->cfg->name);
                break;

            case ERR_LOGIN_INVAL_MENU:
                debug(DBG_LOG, "%s: Select a valid menu code in the config!\n",
                      s->cfg->name);
                break;
        }

        /* Whatever the problem, we're hosed here. */
        return -9001;
    }
    else {
        /* We have a response. Set the has key flag. */
        conn->has_key = 1;
        debug(DBG_LOG, "%s: Shipgate connection established\n", s->cfg->name);
    }

    /* Send the burst of client data if we have any to send */
    return shipgate_send_clients(conn);
}

static int handle_friend(shipgate_conn_t *c, shipgate_friend_login_pkt *pkt) {
    uint16_t type = ntohs(pkt->hdr.pkt_type);
    uint32_t ugc, ubl, fgc, fsh, fbl;
    miniship_t *ms;
    ship_t *s = c->ship;
    block_t *b;
    ship_client_t *cl;
    int on = type == SHDR_TYPE_FRLOGIN;

    ugc = ntohl(pkt->dest_guildcard);
    ubl = ntohl(pkt->dest_block);
    fgc = ntohl(pkt->friend_guildcard);
    fsh = ntohl(pkt->friend_ship);
    fbl = ntohl(pkt->friend_block);

    /* Grab the block structure where the user is */
    if(ubl > s->cfg->blocks || !(b = s->blocks[ubl - 1])) {
        return 0;
    }

    /* Find the ship in question */
    TAILQ_FOREACH(ms, &s->ships, qentry) {
        if(ms->ship_id == fsh) {
            break;
        }
    }

    /* If we can't find the ship, give up */
    if(!ms) {
        return 0;
    }

    /* Find the user in question */
    TAILQ_FOREACH(cl, b->clients, qentry) {
        if(cl->guildcard == ugc) {
            break;
        }
    }

    /* If we can't find the user, give up */
    if(!cl) {
        return 0;
    }

    /* The rest is easy */
    client_send_friendmsg(cl, on, pkt->friend_name, ms->name, fbl);
    return 0;
}

static int handle_addfriend(shipgate_conn_t *c, shipgate_friend_err_pkt *pkt) {
    int i;
    ship_t *s = c->ship;
    block_t *b;
    ship_client_t *cl;
    uint32_t dest = ntohl(pkt->user_gc);
    int done = 0;
    uint16_t flags = ntohs(pkt->base.hdr.flags);
    uint32_t err = ntohl(pkt->base.error_code);

    /* Make sure the packet looks sane */
    if(!(flags & SHDR_FAILURE) && !(flags & SHDR_RESPONSE)) {
        return 0;
    }

    for(i = 0; i < s->cfg->blocks && !done; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_mutex_lock(&b->mutex);

            TAILQ_FOREACH(cl, b->clients, qentry) {
                pthread_mutex_lock(&cl->mutex);

                if(cl->guildcard == dest && cl->pl) {
                    /* We've found them, figure out what to tell them. */
                    if(err == ERR_NO_ERROR) {
                        send_txt(cl, "%s", __(cl, "\tE\tC7Friend added"));
                    }
                    else {
                        send_txt(cl, "%s", __(cl, "\tE\tC7Couldn't add "
                                              "friend"));
                    }

                    done = 1;
                }
                else if(cl->guildcard == dest) {
                    /* Act like they don't exist for right now (they don't
                       really exist right now) */
                    done = 1;
                }

                pthread_mutex_unlock(&cl->mutex);

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

static int handle_delfriend(shipgate_conn_t *c, shipgate_friend_err_pkt *pkt) {
    int i;
    ship_t *s = c->ship;
    block_t *b;
    ship_client_t *cl;
    uint32_t dest = ntohl(pkt->user_gc);
    int done = 0;
    uint16_t flags = ntohs(pkt->base.hdr.flags);
    uint32_t err = ntohl(pkt->base.error_code);

    /* Make sure the packet looks sane */
    if(!(flags & SHDR_FAILURE) && !(flags & SHDR_RESPONSE)) {
        return 0;
    }

    for(i = 0; i < s->cfg->blocks && !done; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_mutex_lock(&b->mutex);

            TAILQ_FOREACH(cl, b->clients, qentry) {
                pthread_mutex_lock(&cl->mutex);

                if(cl->guildcard == dest && cl->pl) {
                    /* We've found them, figure out what to tell them. */
                    if(err == ERR_NO_ERROR) {
                        send_txt(cl, "%s", __(cl, "\tE\tC7Friend removed"));
                    }
                    else {
                        send_txt(cl, "%s", __(cl, "\tE\tC7Couldn't remove "
                                              "friend"));
                    }

                    done = 1;
                }
                else if(cl->guildcard == dest) {
                    /* Act like they don't exist for right now (they don't
                       really exist right now) */
                    done = 1;
                }

                pthread_mutex_unlock(&cl->mutex);

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

static int handle_kick(shipgate_conn_t *conn, shipgate_kick_pkt *pkt) {
    uint32_t gc = ntohl(pkt->guildcard);
    uint32_t block = ntohl(pkt->block);
    ship_t *s = conn->ship;
    block_t *b;
    ship_client_t *i;

    /* Check the block number first. */
    if(block > s->cfg->blocks) {
        return 0;
    }

    b = s->blocks[block - 1];
    pthread_mutex_lock(&b->mutex);

    /* Find the requested client. */
    TAILQ_FOREACH(i, b->clients, qentry) {
        if(i->guildcard == gc) {
            /* Found them, send the message and disconnect the client */
            if(strlen(pkt->reason) > 0) {
                send_message_box(i, "%s\n\n%s\n%s",
                                 __(i, "\tEYou have been kicked by a GM."),
                                 __(i, "Reason:"), pkt->reason);
            }
            else {
                send_message_box(i, "%s",
                                 __(i, "\tEYou have been kicked by a GM."));
            }

            i->flags |= CLIENT_FLAG_DISCONNECTED;
            goto out;
        }
    }

out:
    pthread_mutex_unlock(&b->mutex);
    return 0;
}

static int handle_pkt(shipgate_conn_t *conn, shipgate_hdr_t *pkt) {
    uint16_t type = ntohs(pkt->pkt_type);
    uint16_t flags = ntohs(pkt->flags);

    if(!conn->has_key) {
        /* Silently ignore non-login packets when we're without a key. */
        if(type != SHDR_TYPE_LOGIN) {
            return 0;
        }

        if(!(flags & SHDR_RESPONSE)) {
            return handle_login(conn, (shipgate_login_pkt *)pkt);
        }
        else {
            return handle_login_reply(conn, (shipgate_error_pkt *)pkt);
        }
    }

    /* See if this is an error packet */
    if(flags & SHDR_FAILURE) {
        switch(type) {
            case SHDR_TYPE_DC:
            case SHDR_TYPE_PC:
                /* Ignore these for now, we shouldn't get them. */
                return 0;

            case SHDR_TYPE_CDATA:
                return handle_cdata(conn, (shipgate_cdata_err_pkt *)pkt);

            case SHDR_TYPE_CREQ:
                return handle_creq_err(conn, (shipgate_cdata_err_pkt *)pkt);

            case SHDR_TYPE_GMLOGIN:
                return handle_gmlogin_err(conn, (shipgate_gm_err_pkt *)pkt);

            case SHDR_TYPE_IPBAN:
            case SHDR_TYPE_GCBAN:
                return handle_ban(conn, (shipgate_ban_err_pkt *)pkt);

            case SHDR_TYPE_BLKLOGIN:
                return handle_blogin_err(conn, (shipgate_blogin_err_pkt *)pkt);

            case SHDR_TYPE_ADDFRIEND:
                return handle_addfriend(conn, (shipgate_friend_err_pkt *)pkt);
                
            case SHDR_TYPE_DELFRIEND:
                return handle_delfriend(conn, (shipgate_friend_err_pkt *)pkt);

            default:
                debug(DBG_WARN, "%s: Shipgate sent unknown error!",
                      conn->ship->cfg->name);
                return 0;
        }
    }
    else {
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

            case SHDR_TYPE_CREQ:
                return handle_creq(conn, (shipgate_char_data_pkt *)pkt);

            case SHDR_TYPE_GMLOGIN:
                return handle_gmlogin(conn, (shipgate_gmlogin_reply_pkt *)pkt);

            case SHDR_TYPE_COUNT:
                return handle_count(conn, (shipgate_cnt_pkt *)pkt);

            case SHDR_TYPE_CDATA:
                return handle_cdata(conn, (shipgate_cdata_err_pkt *)pkt);

            case SHDR_TYPE_IPBAN:
            case SHDR_TYPE_GCBAN:
                return handle_ban(conn, (shipgate_ban_err_pkt *)pkt);

            case SHDR_TYPE_FRLOGIN:
            case SHDR_TYPE_FRLOGOUT:
                return handle_friend(conn, (shipgate_friend_login_pkt *)pkt);

            case SHDR_TYPE_ADDFRIEND:
                return handle_addfriend(conn, (shipgate_friend_err_pkt *)pkt);

            case SHDR_TYPE_DELFRIEND:
                return handle_delfriend(conn, (shipgate_friend_err_pkt *)pkt);

            case SHDR_TYPE_KICK:
                return handle_kick(conn, (shipgate_kick_pkt *)pkt);
        }
    }

    return -1;
}

/* Read data from the shipgate. */
int shipgate_process_pkt(shipgate_conn_t *c) {
    ssize_t sz;
    uint16_t pkt_sz;
    int rv = 0;
    unsigned char *rbp;
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
        if(!c->hdr_read && c->has_key) {
            RC4(&c->gate_key, 8, rbp, (unsigned char *)&c->pkt);
            c->hdr_read = 1;
        }
        else if(!c->hdr_read) {
            memcpy(&c->pkt, rbp, 8);
            c->hdr_read = 1;
        }

        /* Read the packet size to see how much we're expecting. */
        pkt_sz = ntohs(c->pkt.pkt_len);

        /* We'll always need a multiple of 8 bytes. */
        if(pkt_sz & 0x07) {
            pkt_sz = (pkt_sz & 0xFFF8) + 8;
        }

        /* Do we have the whole packet? */
        if(sz >= (ssize_t)pkt_sz) {
            /* Yes, we do, decrypt it. */
            if(c->has_key) {
                RC4(&c->gate_key, pkt_sz - 8, rbp + 8, rbp + 8);
                memcpy(rbp, &c->pkt, 8);
            }
            else {
                memcpy(rbp, &c->pkt, 8);
            }

            /* Pass it on. */
            if((rv = handle_pkt(c, (shipgate_hdr_t *)rbp))) {
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
    pkt->hdr.flags = htons(SHDR_NO_DEFLATE | SHDR_RESPONSE);

    /* Fill in the packet. */
    strcpy(pkt->name, ship->cfg->name);
    pkt->ship_addr = ship->cfg->ship_ip;
    pkt->int_addr = local_addr;
    pkt->ship_port = htons(ship->cfg->base_port);
    pkt->ship_key = htons(c->key_idx);
    pkt->clients = htons(ship->num_clients);
    pkt->games = htons(ship->num_games);
    pkt->menu_code = htons(ship->cfg->menu_code);
    pkt->flags = 0;                     /* XXXX */
    pkt->proto_ver = htonl(SHIPGATE_PROTO_VER);

    /* Send it away */
    return send_raw(c, sizeof(shipgate_login_reply_pkt), sendbuf);
}

/* Send a client count update to the shipgate. */
int shipgate_send_cnt(shipgate_conn_t *c, uint16_t clients, uint16_t games) {
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
    pkt->clients = htons(clients);
    pkt->games = htons(games);
    pkt->ship_id = 0;                   /* Ignored on ship->gate packets. */

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
    memmove(pkt->pkt, dc, dc_len);

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
    dc_pkt_hdr_t *pc = (dc_pkt_hdr_t *)pcp;
    shipgate_fw_pkt *pkt = (shipgate_fw_pkt *)sendbuf;
    int pc_len = LE16(pc->pkt_len);
    int full_len = sizeof(shipgate_fw_pkt) + pc_len;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Copy the packet, unchanged */
    memmove(pkt->pkt, pc, pc_len);

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
    int full_len = sizeof(shipgate_fw_pkt) + DC_GUILD_REPLY_LENGTH;

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
    dc->hdr.pkt_type = GUILD_REPLY_TYPE;
    dc->hdr.pkt_len = LE16(DC_GUILD_REPLY_LENGTH);
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

/* Send a friendlist update */
int shipgate_send_friend_update(shipgate_conn_t *c, int add, uint32_t user,
                                uint32_t friend_gc) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_friend_upd_pkt *pkt = (shipgate_friend_upd_pkt *)sendbuf;
    uint16_t type = add ? SHDR_TYPE_ADDFRIEND : SHDR_TYPE_DELFRIEND;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Scrub the buffer */
    memset(pkt, 0, sizeof(shipgate_friend_upd_pkt));

    /* Fill in the packet */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_friend_upd_pkt));
    pkt->hdr.pkt_type = htons(type);
    pkt->hdr.pkt_unc_len = pkt->hdr.pkt_len;
    pkt->hdr.flags = htons(SHDR_NO_DEFLATE);

    pkt->user_guildcard = htonl(user);
    pkt->friend_guildcard = htonl(friend_gc);

    /* Send the packet away */
    return send_crypt(c, sizeof(shipgate_friend_upd_pkt), sendbuf);
}

/* Send a block login/logout */
int shipgate_send_block_login(shipgate_conn_t *c, int on, uint32_t user,
                              uint32_t block, const char *name) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_block_login_pkt *pkt = (shipgate_block_login_pkt *)sendbuf;
    uint16_t type = on ? SHDR_TYPE_BLKLOGIN : SHDR_TYPE_BLKLOGOUT;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Scrub the buffer */
    memset(pkt, 0, sizeof(shipgate_block_login_pkt));

    /* Fill in the packet */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_block_login_pkt));
    pkt->hdr.pkt_type = htons(type);
    pkt->hdr.pkt_unc_len = pkt->hdr.pkt_len;
    pkt->hdr.flags = htons(SHDR_NO_DEFLATE);

    pkt->guildcard = htonl(user);
    pkt->blocknum = htonl(block);
    strncpy(pkt->ch_name, name, 32);

    /* Send the packet away */
    return send_crypt(c, sizeof(shipgate_block_login_pkt), sendbuf);
}

/* Send a lobby change packet */
int shipgate_send_lobby_chg(shipgate_conn_t *c, uint32_t user, uint32_t lobby,
                            const char *lobby_name) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_lobby_change_pkt *pkt = (shipgate_lobby_change_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Scrub the buffer */
    memset(pkt, 0, sizeof(shipgate_lobby_change_pkt));

    /* Fill in the packet */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_lobby_change_pkt));
    pkt->hdr.pkt_type = htons(SHDR_TYPE_LOBBYCHG);
    pkt->hdr.pkt_unc_len = pkt->hdr.pkt_len;
    pkt->hdr.flags = htons(SHDR_NO_DEFLATE);

    pkt->guildcard = htonl(user);
    pkt->lobby_id = htonl(lobby);
    strncpy(pkt->lobby_name, lobby_name, 32);

    /* Send the packet away */
    return send_crypt(c, sizeof(shipgate_lobby_change_pkt), sendbuf);
}

/* Send a full client list */
int shipgate_send_clients(shipgate_conn_t *c) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_block_clients_pkt *pkt = (shipgate_block_clients_pkt *)sendbuf;
    uint32_t count;
    uint16_t size;
    ship_t *s = c->ship;
    int i;
    block_t *b;
    lobby_t *l;
    ship_client_t *cl;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Loop through all the blocks looking for clients, sending one packet per
       block */
    for(i = 0; i < s->cfg->blocks; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_mutex_lock(&b->mutex);

            /* Set up this pass */
            pkt->block = htonl(b->b);
            size = 16;
            count = 0;

            TAILQ_FOREACH(cl, b->clients, qentry) {
                pthread_mutex_lock(&cl->mutex);

                /* Only do this if we have enough info to actually have sent
                   the block login before */
                if(cl->pl->v1.name[0]) {
                    l = cl->cur_lobby;

                    /* Fill in what we have */
                    pkt->entries[count].guildcard = htonl(cl->guildcard);
                    strncpy(pkt->entries[count].ch_name, cl->pl->v1.name, 32);

                    if(l) {
                        pkt->entries[count].lobby = htonl(l->lobby_id);
                        strncpy(pkt->entries[count].lobby_name, l->name, 32);
                    }
                    else {
                        pkt->entries[count].lobby = htonl(0);
                        memset(pkt->entries[count].lobby_name, 0, 32);
                    }

                    /* Increment the counter/size */
                    ++count;
                    size += 72;
                }

                pthread_mutex_unlock(&cl->mutex);
            }

            pthread_mutex_unlock(&b->mutex);

            if(count) {
                /* Fill in the header */
                pkt->hdr.pkt_len = htons(size);
                pkt->hdr.pkt_type = htons(SHDR_TYPE_BCLIENTS);
                pkt->hdr.pkt_unc_len = pkt->hdr.pkt_len;
                pkt->hdr.flags = htons(SHDR_NO_DEFLATE);
                pkt->count = htonl(count);

                /* Send the packet away */
                send_crypt(c, size, sendbuf);
            }
        }
    }

    return 0;
}

/* Send a kick packet */
int shipgate_send_kick(shipgate_conn_t *c, uint32_t requester, uint32_t user,
                       const char *reason) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_kick_pkt *pkt = (shipgate_kick_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Scrub the buffer */
    memset(pkt, 0, sizeof(shipgate_kick_pkt));

    /* Fill in the packet */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_kick_pkt));
    pkt->hdr.pkt_type = htons(SHDR_TYPE_KICK);
    pkt->hdr.pkt_unc_len = pkt->hdr.pkt_len;
    pkt->hdr.flags = htons(SHDR_NO_DEFLATE);

    pkt->requester = htonl(requester);
    pkt->guildcard = htonl(user);

    if(reason) {
        strncpy(pkt->reason, reason, 64);
    }

    /* Send the packet away */
    return send_crypt(c, sizeof(shipgate_kick_pkt), sendbuf);
}
