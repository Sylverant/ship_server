/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2012 Lawrence Sebald

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

#include <sylverant/config.h>
#include <sylverant/debug.h>
#include <sylverant/sha4.h>

#include "ship.h"
#include "utils.h"
#include "clients.h"
#include "shipgate.h"
#include "ship_packets.h"

/* TLS stuff -- from ship_server.c */
extern gnutls_certificate_credentials_t tls_cred;
extern gnutls_priority_t tls_prio;

static inline ssize_t sg_recv(shipgate_conn_t *c, void *buffer, size_t len) {
    return gnutls_record_recv(c->session, buffer, len);
}

static inline ssize_t sg_send(shipgate_conn_t *c, void *buffer, size_t len) {
    return gnutls_record_send(c->session, buffer, len);
}

/* Send a raw packet away. */
static int send_raw(shipgate_conn_t *c, int len, uint8_t *sendbuf, int crypt) {
    ssize_t rv, total = 0;
    void *tmp;

    /* Keep trying until the whole thing's sent. */
    if((!crypt || c->has_key) && c->sock >= 0 && !c->sendbuf_cur) {
        while(total < len) {
            rv = sg_send(c, sendbuf + total, len - total);

            if(rv == GNUTLS_E_AGAIN || rv == GNUTLS_E_INTERRUPTED) {
                /* Try again. */
                continue;
            }
            else if(rv <= 0) {
                return -1;
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

    return send_raw(c, len, sendbuf, 1);
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
    pkt->version = pkt->reserved = 0;

    if(reply) {
        pkt->flags = htons(SHDR_RESPONSE);
    }

    /* Send it away. */
    return send_crypt(c, sizeof(shipgate_hdr_t), sendbuf);
}

/* Attempt to connect to the shipgate. Returns < 0 on error, returns the socket
   for communciation on success. */
static int shipgate_conn(ship_t *s, shipgate_conn_t *rv, int reconn) {
    int sock, irv;
    struct sockaddr_in addr;
    struct sockaddr_in6 addr6;
    unsigned int peer_status;
    miniship_t *i, *tmp;

    if(reconn) {
        /* Clear all ships so we don't keep around stale stuff */
        i = TAILQ_FIRST(&s->ships);
        while(i) {
            tmp = TAILQ_NEXT(i, qentry);
            TAILQ_REMOVE(&s->ships, i, qentry);
            free(i);
            i = tmp;
        }

        rv->has_key = 0;
    }
    else {
        /* Clear it first. */
        memset(rv, 0, sizeof(shipgate_conn_t));
    }

    debug(DBG_LOG, "%s: Connecting to shipgate...\n", s->cfg->name);

    /* Create the socket for the connection. */
    if(s->cfg->shipgate_ip) {
        sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

        if(sock < 0) {
            perror("socket");
            return -1;
        }

        /* Connect the socket to the shipgate. */
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = s->cfg->shipgate_ip;
        addr.sin_port = htons(s->cfg->shipgate_port);

        if(connect(sock, (struct sockaddr *)&addr,
                   sizeof(struct sockaddr_in))) {
            perror("connect");
            close(sock);
            return -2;
        }
    }
    else {
        sock = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP);

        if(sock < 0) {
            perror("socket");
            return -1;
        }

        /* Connect the socket to the shipgate. */
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        memcpy(&addr6.sin6_addr, s->cfg->shipgate_ip6, 16);
        addr6.sin6_port = htons(s->cfg->shipgate_port);

        if(connect(sock, (struct sockaddr *)&addr6,
                   sizeof(struct sockaddr_in6))) {
            perror("connect");
            close(sock);
            return -2;
        }
    }

    /* Set up the TLS session */
    gnutls_init(&rv->session, GNUTLS_CLIENT);
    gnutls_priority_set(rv->session, tls_prio);
    irv = gnutls_credentials_set(rv->session, GNUTLS_CRD_CERTIFICATE, tls_cred);

    if(irv < 0) {
        debug(DBG_ERROR, "TLS credentials problem: %s\n", gnutls_strerror(irv));
        close(sock);
        gnutls_deinit(rv->session);
        return -3;
    }

#if (SIZEOF_INT != SIZEOF_VOIDP) && (SIZEOF_LONG_INT == SIZEOF_VOIDP)
    gnutls_transport_set_ptr(rv->session, (gnutls_transport_ptr_t)((long)sock));
#else
    gnutls_transport_set_ptr(rv->session, (gnutls_transport_ptr_t)sock);
#endif

    /* Do the TLS handshake */
    irv = gnutls_handshake(rv->session);

    if(irv < 0) {
        debug(DBG_ERROR, "TLS Handshake failed: %s\n", gnutls_strerror(irv));
        close(sock);
        gnutls_deinit(rv->session);
        return -3;
    }

    /* Verify that the peer has a valid certificate */
    irv = gnutls_certificate_verify_peers2(rv->session, &peer_status);

    if(irv < 0) {
        debug(DBG_WARN, "Error validating peer: %s\n", gnutls_strerror(irv));
        gnutls_bye(rv->session, GNUTLS_SHUT_RDWR);
        close(sock);
        gnutls_deinit(rv->session);
        return -4;
    }

    /* Check whether or not the peer is trusted... */
    if(peer_status & GNUTLS_CERT_INVALID) {
        debug(DBG_WARN, "Untrusted peer connection, reason below:\n");

        if(peer_status & GNUTLS_CERT_SIGNER_NOT_FOUND)
            debug(DBG_WARN, "No issuer found\n");
        if(peer_status & GNUTLS_CERT_SIGNER_NOT_CA)
            debug(DBG_WARN, "Issuer is not a CA\n");
        if(peer_status & GNUTLS_CERT_NOT_ACTIVATED)
            debug(DBG_WARN, "Certificate not yet activated\n");
        if(peer_status & GNUTLS_CERT_EXPIRED)
            debug(DBG_WARN, "Certificate Expired\n");
        if(peer_status & GNUTLS_CERT_REVOKED)
            debug(DBG_WARN, "Certificate Revoked\n");
        if(peer_status & GNUTLS_CERT_INSECURE_ALGORITHM)
            debug(DBG_WARN, "Insecure certificate signature\n");

        gnutls_bye(rv->session, GNUTLS_SHUT_RDWR);
        close(sock);
        gnutls_deinit(rv->session);
        return -5;
    }

    /* Save a few other things in the struct */
    rv->sock = sock;
    rv->ship = s;

    return 0;
}

int shipgate_connect(ship_t *s, shipgate_conn_t *rv) {
    return shipgate_conn(s, rv, 0);
}

/* Reconnect to the shipgate if we are disconnected for some reason. */
int shipgate_reconnect(shipgate_conn_t *conn) {
    return shipgate_conn(conn->ship, conn, 1);
}

/* Clean up a shipgate connection. */
void shipgate_cleanup(shipgate_conn_t *c) {
    if(c->sock > 0) {
        gnutls_bye(c->session, GNUTLS_SHUT_RDWR);
        close(c->sock);
        gnutls_deinit(c->session);
    }

    free(c->recvbuf);
    free(c->sendbuf);
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
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest) {
#ifdef SYLVERANT_ENABLE_IPV6
                    if(pkt->hdr.flags != 6) {
                        send_guild_reply_sg(c, pkt);
                    }
                    else {
                        send_guild_reply6_sg(c, (dc_guild_reply6_pkt *)pkt);
                    }
#else
                    send_guild_reply_sg(c, pkt);
#endif

                    done = 1;
                }

                pthread_mutex_unlock(&c->mutex);

                if(done) {
                    break;
                }
            }

            pthread_rwlock_unlock(&b->lock);
        }
    }

    return rv;
}

static int handle_bb_greply(shipgate_conn_t *conn, bb_guild_reply_pkt *pkt,
                            uint32_t block) {
    block_t *b;
    ship_client_t *c;
    uint32_t dest = LE32(pkt->gc_search);

    /* Make sure the block given is sane */
    if(block > ship->cfg->blocks || !ship->blocks[block - 1]) {
        return 0;
    }

    b = ship->blocks[block - 1];
    pthread_rwlock_rdlock(&b->lock);

    /* Look for the client */
    TAILQ_FOREACH(c, b->clients, qentry) {
        pthread_mutex_lock(&c->mutex);

        if(c->guildcard == dest) {
            send_pkt_bb(c, (bb_pkt_hdr_t *)pkt);
            pthread_mutex_unlock(&c->mutex);
            pthread_rwlock_unlock(&b->lock);
            return 0;
        }

        pthread_mutex_unlock(&c->mutex);
    }

    pthread_rwlock_unlock(&b->lock);

    return 0;
}

static void handle_mail_autoreply(shipgate_conn_t *c, ship_client_t *s,
                                  uint32_t dest) {
    int i;

    switch(s->version) {
        case CLIENT_VERSION_PC:
        {
            pc_simple_mail_pkt p;
            dc_pkt_hdr_t *dc = (dc_pkt_hdr_t *)&p;

            /* Build the packet up */
            memset(&p, 0, sizeof(pc_simple_mail_pkt));
            dc->pkt_type = SIMPLE_MAIL_TYPE;
            dc->pkt_len = LE16(PC_SIMPLE_MAIL_LENGTH);
            p.tag = LE32(0x00010000);
            p.gc_sender = LE32(s->guildcard);
            p.gc_dest = LE32(dest);

            /* Copy the name */
            for(i = 0; i < 16; ++i) {
                p.name[i] = LE16(s->pl->v1.name[i]);
            }

            /* Copy the message */
            memcpy(p.stuff, s->autoreply, s->autoreply_len);

            /* Send it */
            shipgate_fw_pc(c, dc, 0, s);
            break;
        }

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
        {
            dc_simple_mail_pkt p;
            dc_pkt_hdr_t *dc = (dc_pkt_hdr_t *)&p;

            /* Build the packet up */
            memset(&p, 0, sizeof(dc_simple_mail_pkt));
            p.hdr.pkt_type = SIMPLE_MAIL_TYPE;
            p.hdr.pkt_len = LE16(DC_SIMPLE_MAIL_LENGTH);
            p.tag = LE32(0x00010000);
            p.gc_sender = LE32(s->guildcard);
            p.gc_dest = LE32(dest);

            /* Copy the name and message */
            memcpy(p.name, s->pl->v1.name, 16);
            memcpy(p.stuff, s->autoreply, s->autoreply_len);

            /* Send it */
            shipgate_fw_dc(c, dc, 0, s);
            break;
        }

        case CLIENT_VERSION_BB:
        {
            bb_simple_mail_pkt p;

            /* Build the packet up */
            memset(&p, 0, sizeof(bb_simple_mail_pkt));
            p.hdr.pkt_type = LE16(SIMPLE_MAIL_TYPE);
            p.hdr.pkt_len = LE16(BB_SIMPLE_MAIL_LENGTH);
            p.tag = LE32(0x00010000);
            p.gc_sender = LE32(s->guildcard);
            p.gc_dest = LE32(dest);

            /* Copy the name, date/time, and message */
            memcpy(p.name, s->pl->bb.character.name, 32);
            memcpy(p.message, s->autoreply, s->autoreply_len);

            /* Send it */
            shipgate_fw_bb(c, (bb_pkt_hdr_t *)&p, 0, s);
            break;
        }
    }
}

static int handle_dc_mail(shipgate_conn_t *conn, dc_simple_mail_pkt *pkt) {
    int i;
    ship_t *s = conn->ship;
    block_t *b;
    ship_client_t *c;
    uint32_t dest = LE32(pkt->gc_dest);
    uint32_t sender = LE32(pkt->gc_sender);
    int done = 0, rv = 0;

    for(i = 0; i < s->cfg->blocks && !done; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest && c->pl) {
                    /* Make sure the user hasn't blacklisted the sender. */
                    if(client_has_blacklisted(c, sender) ||
                       client_has_ignored(c, sender)) {
                        done = 1;
                        pthread_mutex_unlock(&c->mutex);
                        rv = 0;
                        break;
                    }

                    /* Check if the user has an autoreply set. */
                    if(c->autoreply_on) {
                        handle_mail_autoreply(conn, c, sender);
                    }

                    /* Forward the packet there. */
                    rv = send_simple_mail(CLIENT_VERSION_DCV1, c,
                                          (dc_pkt_hdr_t *)pkt);
                    done = 1;
                }

                pthread_mutex_unlock(&c->mutex);

                if(done) {
                    break;
                }
            }

            pthread_rwlock_unlock(&b->lock);
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
    uint32_t sender = LE32(pkt->gc_sender);
    int done = 0, rv = 0;

    for(i = 0; i < s->cfg->blocks && !done; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest && c->pl) {
                    /* Make sure the user hasn't blacklisted the sender. */
                    if(client_has_blacklisted(c, sender) ||
                       client_has_ignored(c, sender)) {
                        done = 1;
                        pthread_mutex_unlock(&c->mutex);
                        rv = 0;
                        break;
                    }

                    /* Check if the user has an autoreply set. */
                    if(c->autoreply) {
                        handle_mail_autoreply(conn, c, sender);
                    }

                    /* Forward the packet there. */
                    rv = send_simple_mail(CLIENT_VERSION_PC, c,
                                          (dc_pkt_hdr_t *)pkt);
                    done = 1;
                }

                pthread_mutex_unlock(&c->mutex);

                if(done) {
                    break;
                }
            }

            pthread_rwlock_unlock(&b->lock);
        }
    }

    return rv;
}

static int handle_bb_mail(shipgate_conn_t *conn, bb_simple_mail_pkt *pkt) {
    int i;
    ship_t *s = conn->ship;
    block_t *b;
    ship_client_t *c;
    uint32_t dest = LE32(pkt->gc_dest);
    uint32_t sender = LE32(pkt->gc_sender);
    int done = 0, rv = 0;

    for(i = 0; i < s->cfg->blocks && !done; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest && c->pl) {
                    /* Make sure the user hasn't blacklisted the sender. */
                    if(client_has_blacklisted(c, sender) ||
                       client_has_ignored(c, sender)) {
                        done = 1;
                        pthread_mutex_unlock(&c->mutex);
                        rv = 0;
                        break;
                    }

                    /* Check if the user has an autoreply set. */
                    if(c->autoreply) {
                        handle_mail_autoreply(conn, c, sender);
                    }

                    /* Forward the packet there. */
                    rv = send_bb_simple_mail(c, pkt);
                    done = 1;
                }

                pthread_mutex_unlock(&c->mutex);

                if(done) {
                    break;
                }
            }

            pthread_rwlock_unlock(&b->lock);
        }
    }

    return rv;
}

static int handle_dc(shipgate_conn_t *conn, shipgate_fw_9_pkt *pkt) {
    dc_pkt_hdr_t *dc = (dc_pkt_hdr_t *)pkt->pkt;
    uint8_t type = dc->pkt_type;

    switch(type) {
        case GUILD_REPLY_TYPE:
            return handle_dc_greply(conn, (dc_guild_reply_pkt *)dc);

        case GUILD_SEARCH_TYPE:
            /* We should never get these... Ignore them, but log a warning. */
            debug(DBG_WARN, "Shipgate sent guild search?!\n");
            return 0;

        case SIMPLE_MAIL_TYPE:
            return handle_dc_mail(conn, (dc_simple_mail_pkt *)dc);
    }

    return -2;
}

static int handle_pc(shipgate_conn_t *conn, shipgate_fw_9_pkt *pkt) {
    dc_pkt_hdr_t *dc = (dc_pkt_hdr_t *)pkt->pkt;
    uint8_t type = dc->pkt_type;

    switch(type) {
        case SIMPLE_MAIL_TYPE:
            return handle_pc_mail(conn, (pc_simple_mail_pkt *)dc);
    }

    return -2;
}

static int handle_bb(shipgate_conn_t *conn, shipgate_fw_9_pkt *pkt) {
    bb_pkt_hdr_t *bb = (bb_pkt_hdr_t *)pkt->pkt;
    uint8_t type = LE16(bb->pkt_type);
    uint32_t block = ntohl(pkt->block);

    switch(type) {
        case SIMPLE_MAIL_TYPE:
            return handle_bb_mail(conn, (bb_simple_mail_pkt *)bb);

        case GUILD_REPLY_TYPE:
            return handle_bb_greply(conn, (bb_guild_reply_pkt *)bb, block);
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
    miniship_t *i, *j, *k;
    uint16_t code = 0;
    int ship_found = 0;
    void *tmp;

    /* Did a ship go down or come up? */
    if(!status) {
        /* A ship has gone down */
        TAILQ_FOREACH(i, &s->ships, qentry) {
            /* Clear the ship from the list, if we've found the right one */
            if(sid == i->ship_id) {
                TAILQ_REMOVE(&s->ships, i, qentry);  
                ship_found = 1;
                break;
            }
        }

        if(!ship_found) {
            return 0;
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
        /* Look for the ship first of all, just in case. */
        TAILQ_FOREACH(i, &s->ships, qentry) {
            if(sid == i->ship_id) {
                ship_found = 1;

                /* Remove it from the list, it'll get re-added later (in the
                   correct position). This also saves us some trouble, in case
                   anything goes wrong... */
                TAILQ_REMOVE(&s->ships, i, qentry);

                break;
            }
        }

        /* If we didn't find the ship (in most cases we won't), allocate space
           for it. */
        if(!ship_found) {
            /* Allocate space, and punt if we can't */
            i = (miniship_t *)malloc(sizeof(miniship_t));

            if(!i) {
                return 0;
            }
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

        /* Copy the ship data */
        memset(i, 0, sizeof(miniship_t));
        memcpy(i->name, p->name, 12);
        memcpy(i->ship_addr6, p->ship_addr6, 16);
        i->ship_id = sid;
        i->ship_addr = p->ship_addr4;
        i->ship_port = ntohs(p->ship_port);
        i->clients = ntohs(p->clients);
        i->games = ntohs(p->games);
        i->menu_code = code;
        i->flags = ntohl(p->flags);
        i->ship_number = p->ship_number;

        /* Add the new ship to the list */
        j = TAILQ_FIRST(&s->ships);
        if(j && j->ship_number < i->ship_number) {
            /* Figure out where this entry is going. */
            while(j && i->ship_number > j->ship_number) {
                k = j;
                j = TAILQ_NEXT(j, qentry);
            }

            /* We've got the spot to put it at, so add it in. */
            TAILQ_INSERT_AFTER(&s->ships, k, i, qentry);
        }
        else {
            /* Nothing here (or the first entry goes after this one), add us to
               the front of the list. */
            TAILQ_INSERT_HEAD(&s->ships, i, qentry);
        }
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
    uint16_t plen = ntohs(pkt->hdr.pkt_len);
    int clen = plen - sizeof(shipgate_char_data_pkt);

    /* Make sure the packet looks sane */
    if(!(flags & SHDR_RESPONSE)) {
        return 0;
    }

    for(i = 0; i < s->cfg->blocks && !done; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest) {
                    if(!c->bb_pl && c->pl) {
                        /* We've found them, overwrite their data, and send the
                           refresh packet. */
                        memcpy(c->pl, pkt->data, clen);
                        send_lobby_join(c, c->cur_lobby);
                    }
                    else if(c->bb_pl) {
                        memcpy(c->bb_pl, pkt->data, clen);

                        /* Clear the item ids from the inventory. */
                        for(i = 0; i < 30; ++i) {
                            c->bb_pl->inv.items[i].item_id = 0xFFFFFFFF;
                        }
                    }

                    done = 1;
                }

                pthread_mutex_unlock(&c->mutex);

                if(done) {
                    break;
                }
            }

            pthread_rwlock_unlock(&b->lock);
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

    /* Make sure the packet looks sane */
    if(!(flags & SHDR_RESPONSE)) {
        return 0;
    }

    /* Check the block number first. */
    if(block > s->cfg->blocks) {
        return 0;
    }

    b = s->blocks[block - 1];
    pthread_rwlock_rdlock(&b->lock);

    /* Find the requested client. */
    TAILQ_FOREACH(i, b->clients, qentry) {
        if(i->guildcard == gc) {
            i->privilege |= pkt->priv;
            i->flags |= CLIENT_FLAG_LOGGED_IN;
            send_txt(i, "%s", __(i, "\tE\tC7Login Successful."));
            break;
        }
    }

    pthread_rwlock_unlock(&b->lock);
    return 0;
}

static int handle_login(shipgate_conn_t *conn, shipgate_login_pkt *pkt) {
    /* Check the header of the packet. */
    if(ntohs(pkt->hdr.pkt_len) < SHIPGATE_LOGINV0_SIZE) {
        return -2;
    }

    /* Check the copyright message of the packet. */
    if(strcmp(pkt->msg, shipgate_login_msg)) {
        return -3;
    }

    debug(DBG_LOG, "%s: Connected to Shipgate Version %d.%d.%d\n",
          conn->ship->cfg->name, (int)pkt->ver_major, (int)pkt->ver_minor,
          (int)pkt->ver_micro);

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
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest && c->pl) {
                    /* We've found them, figure out what to tell them. */
                    if(flags & SHDR_FAILURE) {
                        send_txt(c, "%s", __(c, "\tE\tC7Couldn't save "
                                                "character data."));
                    }
                    else {
                        send_txt(c, "%s", __(c, "\tE\tC7Saved character "
                                             "data."));
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
                    break;
                }
            }

            pthread_rwlock_unlock(&b->lock);
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
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest && c->pl) {
                    /* We've found them, figure out what to tell them. */
                    if(flags & SHDR_FAILURE) {
                        /* If the not gm flag is set, disconnect the user. */
                        if(ntohl(pkt->base.error_code) == ERR_BAN_NOT_GM) {
                            c->flags |= CLIENT_FLAG_DISCONNECTED;
                        }

                        send_txt(c, "%s", __(c, "\tE\tC7Error setting ban."));

                    }
                    else {
                        send_txt(c, "%s", __(c, "\tE\tC7User banned."));
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
                    break;
                }
            }

            pthread_rwlock_unlock(&b->lock);
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
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(c, b->clients, qentry) {
                pthread_mutex_lock(&c->mutex);

                if(c->guildcard == dest && c->pl) {
                    /* We've found them, figure out what to tell them. */
                    if(err == ERR_CREQ_NO_DATA) {
                        send_txt(c, "%s", __(c, "\tE\tC7No character data "
                                             "found."));
                    }
                    else {
                        send_txt(c, "%s", __(c, "\tE\tC7Couldn't request "
                                             "character data."));
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
                    break;
                }
            }

            pthread_rwlock_unlock(&b->lock);
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
    pthread_rwlock_rdlock(&b->lock);

    /* Find the requested client. */
    TAILQ_FOREACH(i, b->clients, qentry) {
        if(i->guildcard == gc) {
            /* XXXX: Maybe send specific error messages sometime later */
            send_txt(i, "%s", __(i, "\tE\tC7Login failed."));
            break;
        }
    }

    pthread_rwlock_unlock(&b->lock);
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

    pthread_rwlock_rdlock(&b->lock);

    /* Find the requested client and boot them off (regardless of the error type
       for now) */
    TAILQ_FOREACH(i, b->clients, qentry) {
        if(i->guildcard == gc) {
            i->flags |= CLIENT_FLAG_DISCONNECTED;
        }
    }

    pthread_rwlock_unlock(&b->lock);

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
    pthread_rwlock_rdlock(&b->lock);

    TAILQ_FOREACH(cl, b->clients, qentry) {
        if(cl->guildcard == ugc) {
            /* The rest is easy */
            client_send_friendmsg(cl, on, pkt->friend_name, ms->name, fbl,
                                  pkt->friend_nick);
            break;
        }
    }

    pthread_rwlock_unlock(&b->lock);

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
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(cl, b->clients, qentry) {
                pthread_mutex_lock(&cl->mutex);

                if(cl->guildcard == dest && cl->pl) {
                    /* We've found them, figure out what to tell them. */
                    if(err == ERR_NO_ERROR) {
                        send_txt(cl, "%s", __(cl, "\tE\tC7Friend added."));
                    }
                    else {
                        send_txt(cl, "%s", __(cl, "\tE\tC7Couldn't add "
                                              "friend."));
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
                    break;
                }
            }

            pthread_rwlock_unlock(&b->lock);
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
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(cl, b->clients, qentry) {
                pthread_mutex_lock(&cl->mutex);

                if(cl->guildcard == dest && cl->pl) {
                    /* We've found them, figure out what to tell them. */
                    if(err == ERR_NO_ERROR) {
                        send_txt(cl, "%s", __(cl, "\tE\tC7Friend removed."));
                    }
                    else {
                        send_txt(cl, "%s", __(cl, "\tE\tC7Couldn't remove "
                                              "friend."));
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
                    break;
                }
            }

            pthread_rwlock_unlock(&b->lock);
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
    pthread_rwlock_rdlock(&b->lock);

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
            break;
        }
    }

    pthread_rwlock_unlock(&b->lock);
    return 0;
}

static int handle_frlist(shipgate_conn_t *c, shipgate_friend_list_pkt *pkt) {
    ship_t *s = c->ship;
    block_t *b;
    ship_client_t *i;
    uint32_t gc = ntohl(pkt->requester), gc2;
    uint32_t block = ntohl(pkt->block), bl2, ship;
    int j, total;
    char msg[1024];
    miniship_t *ms;

    /* Check the block number first. */
    if(block > s->cfg->blocks) {
        return 0;
    }

    b = s->blocks[block - 1];
    total = ntohs(pkt->hdr.pkt_len) - sizeof(shipgate_friend_list_pkt);
    msg[0] = '\0';
    pthread_rwlock_rdlock(&b->lock);

    /* Find the requested client. */
    TAILQ_FOREACH(i, b->clients, qentry) {
        if(i->guildcard == gc) {
            pthread_mutex_lock(&i->mutex);

            if(!total) {
                strcpy(msg, __(i, "\tENo friends at that offset."));
            }

            for(j = 0; total; ++j, total -= 48) {
                ship = ntohl(pkt->entries[j].ship);
                bl2 = ntohl(pkt->entries[j].block);
                gc2 = ntohl(pkt->entries[j].guildcard);

                if(ship && block) {
                    /* Grab the ship the user is on */
                    ms = ship_find_ship(s, ship);

                    if(!ms) {
                        continue;
                    }

                    /* Fill in the message */
                    if(ms->menu_code) {
                        sprintf(msg, "%s\tC2%s (%d)\n\tC7%02x:%c%c/%s "
                                "BLOCK%02d\n", msg, pkt->entries[j].name, gc2,
                                ms->ship_number, (char)(ms->menu_code),
                                (char)(ms->menu_code >> 8), ms->name, bl2);
                    }
                    else {
                        sprintf(msg, "%s\tC2%s (%d)\n\tC7%02x:%s BLOCK%02d\n",
                                msg, pkt->entries[j].name, gc2, ms->ship_number,
                                ms->name, bl2);
                    }
                }
                else {
                    /* Not online? Much easier to deal with! */
                    sprintf(msg, "%s\tC4%s (%d)\n", msg, pkt->entries[j].name,
                            gc2);
                }
            }

            /* Send the message to the user */
            if(send_message_box(i, "%s", msg)) {
                i->flags |= CLIENT_FLAG_DISCONNECTED;
            }

            pthread_mutex_unlock(&i->mutex);

            break;
        }
    }

    pthread_rwlock_unlock(&b->lock);
    return 0;
}

static int handle_globalmsg(shipgate_conn_t *c, shipgate_global_msg_pkt *pkt) {
    uint16_t text_len;
    ship_t *s = c->ship;
    int i;
    block_t *b;
    ship_client_t *i2;

    /* Make sure the message looks sane */
    text_len = ntohs(pkt->hdr.pkt_len) - sizeof(shipgate_global_msg_pkt);

    /* Make sure the string is NUL terminated */
    if(pkt->text[text_len - 1]) {
        debug(DBG_WARN, "Ignoring Non-terminated global msg\n");
        return 0;
    }

    /* Go through each block and send the message to anyone that is alive. */
    for(i = 0; i < s->cfg->blocks; ++i) {
        b = s->blocks[i];

        if(b && b->run) {
            pthread_rwlock_rdlock(&b->lock);

            /* Send the message to each player. */
            TAILQ_FOREACH(i2, b->clients, qentry) {
                pthread_mutex_lock(&i2->mutex);

                if(i2->pl) {
                    send_txt(i2, "%s\n%s", __(i2, "\tE\tC7Global Message:"),
                             pkt->text);
                }

                pthread_mutex_unlock(&i2->mutex);
            }

            pthread_rwlock_unlock(&b->lock);
        }
    }

    return 0;
}

static int  handle_useropt(shipgate_conn_t *c, shipgate_user_opt_pkt *pkt) {
    ship_t *s = c->ship;
    block_t *b;
    ship_client_t *i;
    uint32_t gc = ntohl(pkt->guildcard), block = ntohl(pkt->block);
    uint8_t *optptr = (uint8_t *)pkt->options;
    uint8_t *endptr = ((uint8_t *)pkt) + ntohs(pkt->hdr.pkt_len);
    shipgate_user_opt_t *opt = (shipgate_user_opt_t *)optptr;
    uint32_t option, length;

    /* Check the block number first. */
    if(block > s->cfg->blocks) {
        return 0;
    }

    b = s->blocks[block - 1];
    pthread_rwlock_rdlock(&b->lock);

    /* Find the requested client. */
    TAILQ_FOREACH(i, b->clients, qentry) {
        if(i->guildcard == gc) {
            pthread_mutex_lock(&i->mutex);

            /* Deal with the options */
            while(optptr < endptr && opt->option != 0) {
                option = ntohl(opt->option);
                length = ntohl(opt->length);

                switch(ntohl(opt->option)) {
                    case USER_OPT_QUEST_LANG:
                        /* Make sure the length is right */
                        if(length != 16) {
                            break;
                        }

                        /* Only byte of the data that's used is the first one.
                           It has the language code in it. */
                        i->q_lang = opt->data[0];
                        break;

                    case USER_OPT_ENABLE_BACKUP:
                        /* Make sure the length is right */
                        if(length != 16) {
                            break;
                        }

                        /* Only byte of the data that's used is the first one.
                           It is a boolean saying whether or not to enable the
                           auto backup feature. */
                        if(opt->data[0]) {
                            i->flags |= CLIENT_FLAG_AUTO_BACKUP;
                        }
                        break;
                }

                /* Adjust the pointers to the next option */
                optptr = optptr + ntohl(opt->length);
                opt = (shipgate_user_opt_t *)optptr;
            }

            pthread_mutex_unlock(&i->mutex);
            break;
        }
    }

    pthread_rwlock_unlock(&b->lock);
    return 0;
}

static int handle_bbopts(shipgate_conn_t *c, shipgate_bb_opts_pkt *pkt) {
    ship_t *s = c->ship;
    block_t *b;
    ship_client_t *i;
    uint32_t gc = ntohl(pkt->guildcard), block = ntohl(pkt->block);

    /* Check the block number first. */
    if(block > s->cfg->blocks) {
        return 0;
    }

    b = s->blocks[block - 1];
    pthread_rwlock_rdlock(&b->lock);

    /* Find the requested client. */
    TAILQ_FOREACH(i, b->clients, qentry) {
        if(i->guildcard == gc) {
            pthread_mutex_lock(&i->mutex);

            /* Copy the user's options */
            memcpy(i->bb_opts, &pkt->opts, sizeof(sylverant_bb_db_opts_t));

            /* Move the user on now that we have everything... */
            send_lobby_list(i);
            send_bb_full_char(i);
            send_simple(i, CHAR_DATA_REQUEST_TYPE, 0);

            pthread_mutex_unlock(&i->mutex);
            break;
        }
    }

    pthread_rwlock_unlock(&b->lock);
    return 0;
}

static int handle_pkt(shipgate_conn_t *conn, shipgate_hdr_t *pkt) {
    uint16_t type = ntohs(pkt->pkt_type);
    uint16_t flags = ntohs(pkt->flags);

    if(!conn->has_key) {
        /* Silently ignore non-login packets when we're without a key. */
        if(type != SHDR_TYPE_LOGIN && type != SHDR_TYPE_LOGIN6) {
            return 0;
        }

        if(type == SHDR_TYPE_LOGIN && !(flags & SHDR_RESPONSE)) {
            return handle_login(conn, (shipgate_login_pkt *)pkt);
        }
        else if(type == SHDR_TYPE_LOGIN6 && (flags & SHDR_RESPONSE)) {
            return handle_login_reply(conn, (shipgate_error_pkt *)pkt);
        }
        else {
            return 0;
        }
    }

    /* See if this is an error packet */
    if(flags & SHDR_FAILURE) {
        switch(type) {
            case SHDR_TYPE_DC:
            case SHDR_TYPE_PC:
            case SHDR_TYPE_BB:
                /* Ignore these for now, we shouldn't get them. */
                return 0;

            case SHDR_TYPE_CDATA:
                return handle_cdata(conn, (shipgate_cdata_err_pkt *)pkt);

            case SHDR_TYPE_CREQ:
            case SHDR_TYPE_CBKUP:
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
                return handle_dc(conn, (shipgate_fw_9_pkt *)pkt);

            case SHDR_TYPE_PC:
                return handle_pc(conn, (shipgate_fw_9_pkt *)pkt);

            case SHDR_TYPE_BB:
                return handle_bb(conn, (shipgate_fw_9_pkt *)pkt);

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

            case SHDR_TYPE_FRLIST:
                return handle_frlist(conn, (shipgate_friend_list_pkt *)pkt);

            case SHDR_TYPE_GLOBALMSG:
                return handle_globalmsg(conn, (shipgate_global_msg_pkt *)pkt);

            case SHDR_TYPE_USEROPT:
                /* We really should notify the user either way... but for now,
                   punt. */
                if(flags & (SHDR_RESPONSE | SHDR_FAILURE)) {
                    return 0;
                }

                return handle_useropt(conn, (shipgate_user_opt_pkt *)pkt);

            case SHDR_TYPE_BBOPTS:
                return handle_bbopts(conn, (shipgate_bb_opts_pkt *)pkt);

            case SHDR_TYPE_CBKUP:
                if(!(flags & SHDR_RESPONSE)) {
                    /* We should never get a non-response version of this. */
                    return -1;
                }

                /* No need really to notify the user. */
                return 0;
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
    if((sz = sg_recv(c, recvbuf + c->recvbuf_cur,
                     65536 - c->recvbuf_cur)) <= 0) {
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
        if(!c->hdr_read) {
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
            /* Yes, we do, copy it out. */
            memcpy(rbp, &c->pkt, 8);

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

    /* Don't even try if there's not a connection. */
    if(!c->has_key || c->sock < 0) {
        return 0;
    }

    /* Send as much as we can. */
    amt = sg_send(c, c->sendbuf, c->sendbuf_cur);

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
/* Send the shipgate a character data save request. */
int shipgate_send_cdata(shipgate_conn_t *c, uint32_t gc, uint32_t slot,
                        const void *cdata, int len, uint32_t block) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_char_data_pkt *pkt = (shipgate_char_data_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the header. */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_char_data_pkt) + len);
    pkt->hdr.pkt_type = htons(SHDR_TYPE_CDATA);
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

    /* Fill in the body. */
    pkt->guildcard = htonl(gc);
    pkt->slot = htonl(slot);
    pkt->block = htonl(block);
    memcpy(pkt->data, cdata, len); 

    /* Send it away. */
    return send_crypt(c, sizeof(shipgate_char_data_pkt) + len, sendbuf);
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
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;
    pkt->guildcard = htonl(gc);
    pkt->slot = htonl(slot);

    /* Send it away. */
    return send_crypt(c, sizeof(shipgate_char_req_pkt), sendbuf);
}

/* Send a newly opened ship's information to the shipgate. */
int shipgate_send_ship_info(shipgate_conn_t *c, ship_t *ship) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_login6_reply_pkt *pkt = (shipgate_login6_reply_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Clear the packet first */
    memset(pkt, 0, sizeof(shipgate_login6_reply_pkt));

    /* Fill in the header. */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_login6_reply_pkt));
    pkt->hdr.pkt_type = htons(SHDR_TYPE_LOGIN6);
    pkt->hdr.flags = htons(SHDR_RESPONSE);

    /* Fill in the packet. */
    pkt->proto_ver = htonl(SHIPGATE_PROTO_VER);
    pkt->flags = htonl(ship->cfg->shipgate_flags);
    memcpy(pkt->name, ship->cfg->name, 12);
    pkt->ship_addr4 = ship->cfg->ship_ip;
    memcpy(pkt->ship_addr6, ship->cfg->ship_ip6, 16);
    pkt->ship_port = htons(ship->cfg->base_port);
    pkt->ship_key = htons(c->key_idx);
    pkt->clients = htons(ship->num_clients);
    pkt->games = htons(ship->num_games);
    pkt->menu_code = htons(ship->cfg->menu_code);

    /* Send it away */
    return send_raw(c, sizeof(shipgate_login6_reply_pkt), sendbuf, 0);
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
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

    /* Fill in the packet. */
    pkt->clients = htons(clients);
    pkt->games = htons(games);
    pkt->ship_id = 0;                   /* Ignored on ship->gate packets. */

    /* Send it away */
    return send_crypt(c, sizeof(shipgate_cnt_pkt), sendbuf);
}

/* Forward a Dreamcast packet to the shipgate. */
int shipgate_fw_dc(shipgate_conn_t *c, const void *dcp, uint32_t flags,
                   ship_client_t *req) {
    uint8_t *sendbuf = get_sendbuf();
    const dc_pkt_hdr_t *dc = (const dc_pkt_hdr_t *)dcp;
    shipgate_fw_9_pkt *pkt = (shipgate_fw_9_pkt *)sendbuf;
    int dc_len = LE16(dc->pkt_len);
    int full_len = sizeof(shipgate_fw_9_pkt) + dc_len;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Copy the packet, unchanged */
    memmove(pkt->pkt, dc, dc_len);

    /* Round up the packet size, if needed. */
    while(full_len & 0x07) {
        sendbuf[full_len++] = 0;
    }

    /* Fill in the shipgate header */
    pkt->hdr.pkt_len = htons(full_len);
    pkt->hdr.pkt_type = htons(SHDR_TYPE_DC);
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;
    pkt->fw_flags = htonl(flags);
    pkt->guildcard = htonl(req->guildcard);
    pkt->block = htonl(req->cur_block->b);

    /* Send the packet away */
    return send_crypt(c, full_len, sendbuf);
}

/* Forward a PC packet to the shipgate. */
int shipgate_fw_pc(shipgate_conn_t *c, const void *pcp, uint32_t flags,
                   ship_client_t *req) {
    uint8_t *sendbuf = get_sendbuf();
    const dc_pkt_hdr_t *pc = (const dc_pkt_hdr_t *)pcp;
    shipgate_fw_9_pkt *pkt = (shipgate_fw_9_pkt *)sendbuf;
    int pc_len = LE16(pc->pkt_len);
    int full_len = sizeof(shipgate_fw_9_pkt) + pc_len;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Copy the packet, unchanged */
    memmove(pkt->pkt, pc, pc_len);

    /* Round up the packet size, if needed. */
    while(full_len & 0x07) {
        sendbuf[full_len++] = 0;
    }

    /* Fill in the shipgate header */
    pkt->hdr.pkt_len = htons(full_len);
    pkt->hdr.pkt_type = htons(SHDR_TYPE_PC);
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;
    pkt->fw_flags = flags;
    pkt->guildcard = htonl(req->guildcard);
    pkt->block = htonl(req->cur_block->b);

    /* Send the packet away */
    return send_crypt(c, full_len, sendbuf);
}

/* Forward a Blue Burst packet to the shipgate. */
int shipgate_fw_bb(shipgate_conn_t *c, const void *bbp, uint32_t flags,
                   ship_client_t *req) {
    uint8_t *sendbuf = get_sendbuf();
    const bb_pkt_hdr_t *bb = (const bb_pkt_hdr_t *)bbp;
    shipgate_fw_9_pkt *pkt = (shipgate_fw_9_pkt *)sendbuf;
    int bb_len = LE16(bb->pkt_len);
    int full_len = sizeof(shipgate_fw_9_pkt) + bb_len;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Copy the packet, unchanged */
    memmove(pkt->pkt, bb, bb_len);

    /* Round up the packet size, if needed. */
    while(full_len & 0x07) {
        sendbuf[full_len++] = 0;
    }

    /* Fill in the shipgate header */
    pkt->hdr.pkt_len = htons(full_len);
    pkt->hdr.pkt_type = htons(SHDR_TYPE_BB);
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;
    pkt->fw_flags = flags;
    pkt->guildcard = htonl(req->guildcard);
    pkt->block = htonl(req->cur_block->b);

    /* Send the packet away */
    return send_crypt(c, full_len, sendbuf);
}

/* Send a GM login request. */
int shipgate_send_gmlogin(shipgate_conn_t *c, uint32_t gc, uint32_t block,
                          const char *username, const char *password) {
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
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

    pkt->guildcard = htonl(gc);
    pkt->block = htonl(block);
    strcpy(pkt->username, username);
    strcpy(pkt->password, password);

    /* Send the packet away */
    return send_crypt(c, sizeof(shipgate_gmlogin_req_pkt), sendbuf);
}

/* Send a ban request. */
int shipgate_send_ban(shipgate_conn_t *c, uint16_t type, uint32_t requester,
                      uint32_t target, uint32_t until, const char *msg) {
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
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

    pkt->req_gc = htonl(requester);
    pkt->target = htonl(target);
    pkt->until = htonl(until);
    strncpy(pkt->message, msg, 255); 

    /* Send the packet away */
    return send_crypt(c, sizeof(shipgate_ban_req_pkt), sendbuf);
}

/* Send a friendlist update */
int shipgate_send_friend_del(shipgate_conn_t *c, uint32_t user,
                             uint32_t friend_gc) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_friend_upd_pkt *pkt = (shipgate_friend_upd_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Scrub the buffer */
    memset(pkt, 0, sizeof(shipgate_friend_upd_pkt));

    /* Fill in the packet */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_friend_upd_pkt));
    pkt->hdr.pkt_type = htons(SHDR_TYPE_DELFRIEND);
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

    pkt->user_guildcard = htonl(user);
    pkt->friend_guildcard = htonl(friend_gc);

    /* Send the packet away */
    return send_crypt(c, sizeof(shipgate_friend_upd_pkt), sendbuf);
}

int shipgate_send_friend_add(shipgate_conn_t *c, uint32_t user,
                             uint32_t friend_gc, const char *nick) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_friend_add_pkt *pkt = (shipgate_friend_add_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Scrub the buffer */
    memset(pkt, 0, sizeof(shipgate_friend_add_pkt));

    /* Fill in the packet */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_friend_add_pkt));
    pkt->hdr.pkt_type = htons(SHDR_TYPE_ADDFRIEND);
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

    pkt->user_guildcard = htonl(user);
    pkt->friend_guildcard = htonl(friend_gc);
    strncpy(pkt->friend_nick, nick, 32);
    pkt->friend_nick[31] = 0;

    /* Send the packet away */
    return send_crypt(c, sizeof(shipgate_friend_add_pkt), sendbuf);
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
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

    pkt->guildcard = htonl(user);
    pkt->blocknum = htonl(block);
    strncpy(pkt->ch_name, name, 32);

    /* Send the packet away */
    return send_crypt(c, sizeof(shipgate_block_login_pkt), sendbuf);
}

int shipgate_send_block_login_bb(shipgate_conn_t *c, int on, uint32_t user,
                                 uint32_t block, const uint16_t *name) {
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
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

    pkt->guildcard = htonl(user);
    pkt->blocknum = htonl(block);
    memcpy(pkt->ch_name, name, 32);

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
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

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
            pthread_rwlock_rdlock(&b->lock);

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

                    if(cl->version != CLIENT_VERSION_BB) {
                        strncpy(pkt->entries[count].ch_name, cl->pl->v1.name,
                                32);
                    }
                    else {
                        memcpy(pkt->entries[count].ch_name,
                               cl->bb_pl->character.name, 32);
                    }

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

            pthread_rwlock_unlock(&b->lock);

            if(count) {
                /* Fill in the header */
                pkt->hdr.pkt_len = htons(size);
                pkt->hdr.pkt_type = htons(SHDR_TYPE_BCLIENTS);
                pkt->hdr.version = pkt->hdr.reserved = 0;
                pkt->hdr.flags = 0;
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
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

    pkt->requester = htonl(requester);
    pkt->guildcard = htonl(user);

    if(reason) {
        strncpy(pkt->reason, reason, 64);
    }

    /* Send the packet away */
    return send_crypt(c, sizeof(shipgate_kick_pkt), sendbuf);
}

/* Send a friend list request packet */
int shipgate_send_frlist_req(shipgate_conn_t *c, uint32_t gc, uint32_t block,
                             uint32_t start) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_friend_list_req *pkt = (shipgate_friend_list_req *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the packet */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_friend_list_req));
    pkt->hdr.pkt_type = htons(SHDR_TYPE_FRLIST);
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

    pkt->requester = htonl(gc);
    pkt->block = htonl(block);
    pkt->start = htonl(start);

    /* Send the packet away */
    return send_crypt(c, sizeof(shipgate_friend_list_req), sendbuf);
}

/* Send a global message packet */
int shipgate_send_global_msg(shipgate_conn_t *c, uint32_t gc,
                             const char *text) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_global_msg_pkt *pkt = (shipgate_global_msg_pkt *)sendbuf;
    uint16_t text_len = strlen(text) + 1, tl2 = (text_len + 7) & 0xFFF8;
    uint16_t len = tl2 + sizeof(shipgate_global_msg_pkt);

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Make sure its sane */
    if(text_len > 0x100 || !text) {
        return -1;
    }

    /* Fill in the packet */
    pkt->hdr.pkt_len = htons(len);
    pkt->hdr.pkt_type = htons(SHDR_TYPE_GLOBALMSG);
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

    pkt->requester = htonl(gc);
    memcpy(pkt->text, text, text_len);
    memset(pkt->text + text_len, 0, tl2 - text_len);

    /* Send the packet away */
    return send_crypt(c, len, sendbuf);
}

/* Send a user option update packet */
int shipgate_send_user_opt(shipgate_conn_t *c, uint32_t gc, uint32_t block,
                           uint32_t opt, uint32_t len, const uint8_t *data) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_user_opt_pkt *pkt = (shipgate_user_opt_pkt *)sendbuf;
    int padding = 8 - (len & 7);
    uint16_t pkt_len = len + sizeof(shipgate_user_opt_pkt) + 8 + padding;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Make sure its sane */
    if(!len || len >= 0x100 || !data) {
        return -1;
    }

    /* Fill in the option data first */
    pkt->options[0].option = htonl(opt);
    memcpy(pkt->options[0].data, data, len);

    /* Options must be a multiple of 8 bytes in length */
    while(padding--) {
        pkt->options[0].data[len++] = 0;
    }

    pkt->options[0].length = htonl(len + 8);

    /* Fill in the packet */
    pkt->hdr.pkt_len = htons(pkt_len);
    pkt->hdr.pkt_type = htons(SHDR_TYPE_USEROPT);
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

    pkt->guildcard = htonl(gc);
    pkt->block = htonl(block);
    pkt->count = htonl(1);
    pkt->reserved = 0;

    /* Send the packet away */
    return send_crypt(c, pkt_len, sendbuf);
}

int shipgate_send_bb_opt_req(shipgate_conn_t *c, uint32_t gc, uint32_t block) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_bb_opts_req_pkt *pkt = (shipgate_bb_opts_req_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the packet */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_bb_opts_req_pkt));
    pkt->hdr.pkt_type = htons(SHDR_TYPE_BBOPT_REQ);
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

    pkt->guildcard = htonl(gc);
    pkt->block = htonl(block);

    /* Send the packet away */
    return send_crypt(c, sizeof(shipgate_bb_opts_req_pkt), sendbuf);
}

/* Send the user's Blue Burst options to be stored */
int shipgate_send_bb_opts(shipgate_conn_t *c, ship_client_t *cl) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_bb_opts_pkt *pkt = (shipgate_bb_opts_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the packet */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_bb_opts_pkt));
    pkt->hdr.pkt_type = htons(SHDR_TYPE_BBOPTS);
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

    pkt->guildcard = htonl(cl->guildcard);
    pkt->block = htonl(cl->cur_block->b);
    memcpy(&pkt->opts, cl->bb_opts, sizeof(sylverant_bb_db_opts_t));

    /* Send the packet away */
    return send_crypt(c, sizeof(shipgate_bb_opts_pkt), sendbuf);
}

/* Send the shipgate a character data backup request. */
int shipgate_send_cbkup(shipgate_conn_t *c, uint32_t gc, uint32_t block,
                        const char *name, const void *cdata, int len) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_char_bkup_pkt *pkt = (shipgate_char_bkup_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the header. */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_char_bkup_pkt) + len);
    pkt->hdr.pkt_type = htons(SHDR_TYPE_CBKUP);
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;

    /* Fill in the body. */
    pkt->guildcard = htonl(gc);
    pkt->block = htonl(block);
    strncpy((char *)pkt->name, name, 32);
    pkt->name[31] = 0;
    memcpy(pkt->data, cdata, len); 

    /* Send it away. */
    return send_crypt(c, sizeof(shipgate_char_bkup_pkt) + len, sendbuf);
}

/* Send the shipgate a request for character backup data. */
int shipgate_send_cbkup_req(shipgate_conn_t *c, uint32_t gc, uint32_t block,
                            const char *name) {
    uint8_t *sendbuf = get_sendbuf();
    shipgate_char_bkup_pkt *pkt = (shipgate_char_bkup_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the header and the body. */
    pkt->hdr.pkt_len = htons(sizeof(shipgate_char_bkup_pkt));
    pkt->hdr.pkt_type = htons(SHDR_TYPE_CBKUP);
    pkt->hdr.version = pkt->hdr.reserved = 0;
    pkt->hdr.flags = 0;
    pkt->guildcard = htonl(gc);
    pkt->block = htonl(block);
    strncpy((char *)pkt->name, name, 32);
    pkt->name[31] = 0;

    /* Send it away. */
    return send_crypt(c, sizeof(shipgate_char_bkup_pkt), sendbuf);
}
