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
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <iconv.h>
#include <sys/time.h>
#include <sys/socket.h>

#include <sylverant/encryption.h>
#include <sylverant/database.h>

#include "ship_packets.h"
#include "utils.h"

/* Forward declaration. */
static int send_dc_lobby_arrows(lobby_t *l, ship_client_t *c);

/* Send a raw packet away. */
static int send_raw(ship_client_t *c, int len, uint8_t *sendbuf) {
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

/* Encrypt and send a packet away. */
static int crypt_send(ship_client_t *c, int len, uint8_t *sendbuf) {
    /* Expand it to be a multiple of 8/4 bytes long */
    while(len & (c->hdr_size - 1)) {
        sendbuf[len++] = 0;
    }

    printf("Sending %d bytes to %d\n", len, c->guildcard);

    /* Encrypt the packet */
    CRYPT_CryptData(&c->skey, sendbuf, len, 1);

    return send_raw(c, len, sendbuf);
}

/* Retrieve the thread-specific sendbuf for the current thread. */
uint8_t *get_sendbuf() {
    uint8_t *sendbuf = (uint8_t *)pthread_getspecific(sendbuf_key);

    /* If we haven't initialized the sendbuf pointer yet for this thread, then
       we need to do that now. */
    if(!sendbuf) {
        sendbuf = (uint8_t *)malloc(65536);

        if(!sendbuf) {
            perror("malloc");
            return NULL;
        }

        if(pthread_setspecific(sendbuf_key, sendbuf)) {
            perror("pthread_setspecific");
            free(sendbuf);
            return NULL;
        }
    }

    return sendbuf;
}

/* Send a Dreamcast welcome packet to the given client. */
int send_dc_welcome(ship_client_t *c, uint32_t svect, uint32_t cvect) {
    uint8_t *sendbuf = get_sendbuf();
    dc_welcome_pkt *pkt = (dc_welcome_pkt *)sendbuf;

    /* Scrub the buffer */
    memset(pkt, 0, sizeof(dc_welcome_pkt));

    /* Fill in the header */
    pkt->hdr.pkt_len = LE16(SHIP_DC_WELCOME_LENGTH);
    pkt->hdr.pkt_type = SHIP_DC_WELCOME_TYPE;

    /* Fill in the required message */
    memcpy(pkt->copyright, dc_welcome_copyright, 56);

    /* Fill in the encryption vectors */
    pkt->svect = LE32(svect);
    pkt->cvect = LE32(cvect);

    /* Send the packet away */
    return send_raw(c, SHIP_DC_WELCOME_LENGTH, sendbuf);
}

/* Send the Dreamcast security packet to the given client. */
int send_dc_security(ship_client_t *c, uint32_t gc, uint8_t *data,
                     int data_len) {
    uint8_t *sendbuf = get_sendbuf();
    dc_security_pkt *pkt = (dc_security_pkt *)sendbuf;

    /* Wipe the packet */
    memset(pkt, 0, sizeof(dc_security_pkt));

    /* Fill in the header */
    pkt->hdr.pkt_type = SHIP_DC_SECURITY_TYPE;
    pkt->hdr.pkt_len = LE16((0x0C + data_len));

    /* Fill in the guildcard/tag */
    pkt->tag = LE32(0x00010000);
    pkt->guildcard = LE32(gc);

    /* Copy over any security data */
    if(data_len)
        memcpy(pkt->security_data, data, data_len);

    /* Send the packet away */
    return crypt_send(c, 0x0C + data_len, sendbuf);
}

/* Send a redirect packet to the given client. */
static int send_dc_redirect(ship_client_t *c, in_addr_t ip, uint16_t port) {
    uint8_t *sendbuf = get_sendbuf();
    dc_redirect_pkt *pkt = (dc_redirect_pkt *)sendbuf;

    /* Wipe the packet */
    memset(pkt, 0, SHIP_DC_REDIRECT_LENGTH);

    /* Fill in the header */
    pkt->hdr.pkt_type = SHIP_REDIRECT_TYPE;
    pkt->hdr.pkt_len = LE16(SHIP_DC_REDIRECT_LENGTH);

    /* Fill in the IP and port */
    pkt->ip_addr = ip;
    pkt->port = LE16(port);

    /* Send the packet away */
    return crypt_send(c, SHIP_DC_REDIRECT_LENGTH, sendbuf);
}

int send_redirect(ship_client_t *c, in_addr_t ip, uint16_t port) {
    /* Call the appropriate function */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_redirect(c, ip, port);
    }
    
    return -1;
}

/* Send a timestamp packet to the given client. */
static int send_dc_timestamp(ship_client_t *c) {
    uint8_t *sendbuf = get_sendbuf();
    dc_timestamp_pkt *pkt = (dc_timestamp_pkt *)sendbuf;
    struct timeval rawtime;
    struct tm cooked;

    /* Wipe the packet */
    memset(pkt, 0, SHIP_DC_TIMESTAMP_LENGTH);

    /* Fill in the header */
    pkt->hdr.pkt_type = SHIP_TIMESTAMP_TYPE;
    pkt->hdr.pkt_len = LE16(SHIP_DC_TIMESTAMP_LENGTH);

    /* Get the timestamp */
    gettimeofday(&rawtime, NULL);

    /* Get UTC */
    gmtime_r(&rawtime.tv_sec, &cooked);

    /* Fill in the timestamp */
    sprintf(pkt->timestamp, "%u:%02u:%02u: %02u:%02u:%02u.%03u",
            cooked.tm_year + 1900, cooked.tm_mon + 1, cooked.tm_mday,
            cooked.tm_hour, cooked.tm_min, cooked.tm_sec,
            (unsigned int)(rawtime.tv_usec / 1000));

    /* Send the packet away */
    return crypt_send(c, SHIP_DC_TIMESTAMP_LENGTH, sendbuf);
}

int send_timestamp(ship_client_t *c) {
    /* Call the appropriate function */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_timestamp(c);
    }
    
    return -1;
}

/* Send the list of blocks to the client. */
static int send_dc_block_list(ship_client_t *c, ship_t *s) {
    uint8_t *sendbuf = get_sendbuf();
    dc_block_list_pkt *pkt = (dc_block_list_pkt *)sendbuf;
    char tmp[18];
    int i, len = 0x20;
    iconv_t ic = iconv_open("SHIFT_JIS", "ASCII");
    size_t in, out;
    char *inptr, *outptr;

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear the base packet */
    memset(pkt, 0, sizeof(dc_block_list_pkt));

    /* Fill in some basic stuff */
    pkt->hdr.pkt_type = SHIP_DC_BLOCK_LIST_TYPE;

    /* Fill in the ship name entry */
    memset(&pkt->entries[0], 0, 0x1C);
    pkt->entries[0].menu_id = LE32(0x00040000);
    pkt->entries[0].item_id = 0;
    pkt->entries[0].flags = 0;

    /* Convert to Shift-JIS */
    in = strlen(s->cfg->name);
    out = 0x10;
    inptr = s->cfg->name;
    outptr = pkt->entries[0].name;
    iconv(ic, &inptr, &in, &outptr, &out);

    /* Add what's needed at the end */
    pkt->entries[0].name[0x0F] = 0x00;
    pkt->entries[0].name[0x10] = 0x08;
    pkt->entries[0].name[0x11] = 0x00;

    /* Add each block to the list. */
    for(i = 1; i <= s->cfg->blocks; ++i) {
        /* Clear out the ship information */
        memset(&pkt->entries[i], 0, 0x1C);

        /* Fill in what we have */
        pkt->entries[i].menu_id = LE32(0x00000001);
        pkt->entries[i].item_id = LE32(i);
        pkt->entries[i].flags = LE16(0x0000);

        /* Create the name string (ASCII) */
        sprintf(tmp, "BLOCK%02d", i);

        /* And convert to Shift-JIS */
        in = strlen(tmp);
        out = 0x12;
        inptr = tmp;
        outptr = pkt->entries[i].name;
        iconv(ic, &inptr, &in, &outptr, &out);

        len += 0x1C;
    }

    /* Fill in the rest of the header */
    pkt->hdr.pkt_len = LE16(len);
    pkt->hdr.flags = (uint8_t)(s->cfg->blocks);

    iconv_close(ic);

    /* Send the packet away */
    return crypt_send(c, len, sendbuf);
}

int send_block_list(ship_client_t *c, ship_t *s) {
    /* Call the appropriate function */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_block_list(c, s);
    }

    return -1;
}

/* Send a block/ship information reply packet to the client. */
static int send_dc_info_reply(ship_client_t *c, char msg[]) {
    uint8_t *sendbuf = get_sendbuf();
    dc_info_reply_pkt *pkt = (dc_info_reply_pkt *)sendbuf;
    iconv_t ic = iconv_open("SHIFT_JIS", "ASCII");
    size_t in, out;
    char *inptr, *outptr;

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Convert the message to Shift-JIS */
    in = strlen(msg);
    out = 65524;
    inptr = msg;
    outptr = pkt->msg;
    iconv(ic, &inptr, &in, &outptr, &out);
    iconv_close(ic);

    /* Figure out how long the new string is. */
    out = 65524 - out + 12;

    /* Fill in the oddities of the packet. */
    pkt->odd[0] = LE32(0x00200000);
    pkt->odd[1] = LE32(0x00200020);

    /* Pad to a length that's at divisible by 4. */
    while(out & 0x03) {
        sendbuf[out++] = 0;
    }

    /* Fill in the header */
    pkt->hdr.pkt_type = SHIP_INFO_REPLY_TYPE;
    pkt->hdr.flags = 0;
    pkt->hdr.pkt_len = LE16(out);

    /* Send the packet away */
    return crypt_send(c, out, sendbuf);
}

int send_info_reply(ship_client_t *c, char msg[]) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_info_reply(c, msg);
    }

    return -1;
}

/* Send a simple (header-only) packet to the client */
static int send_dc_simple(ship_client_t *c, int type, int flags) {
    uint8_t *sendbuf = get_sendbuf();
    dc_pkt_hdr_t *pkt = (dc_pkt_hdr_t *)sendbuf;

    /* Fill in the header */
    pkt->pkt_type = (uint8_t)type;
    pkt->flags = (uint8_t)flags;
    pkt->pkt_len = LE16(4);

    /* Send the packet away */
    return crypt_send(c, 4, sendbuf);
}

int send_simple(ship_client_t *c, int type, int flags) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_simple(c, type, flags);
    }

    return -1;
}

/* Send the lobby list packet to the client. */
static int send_dc_lobby_list(ship_client_t *c) {
    uint8_t *sendbuf = get_sendbuf();
    dc_lobby_list_pkt *pkt = (dc_lobby_list_pkt *)sendbuf;
    uint32_t i;

    /* Fill in the header */
    pkt->hdr.pkt_type = SHIP_LOBBY_LIST_TYPE;
    pkt->hdr.flags = 0x0F;                                  /* 15 lobbies */
    pkt->hdr.pkt_len = LE16(SHIP_DC_LOBBY_LIST_LENGTH);

    /* Fill in the lobbies. */
    for(i = 0; i < 15; ++i) {
        pkt->entries[i].menu_id = 0xFFFFFFFF;
        pkt->entries[i].item_id = LE32((i + 1));
        pkt->entries[i].padding = 0;
    }

    /* There's padding at the end -- enough for one more lobby. */
    pkt->entries[15].menu_id = 0;
    pkt->entries[15].item_id = 0;
    pkt->entries[15].padding = 0;

    return crypt_send(c, SHIP_DC_LOBBY_LIST_LENGTH, sendbuf);
}

int send_lobby_list(ship_client_t *c) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_lobby_list(c);
    }

    return -1;
}

/* Send the packet to join a lobby to the client. */
static int send_dc_lobby_join(ship_client_t *c, lobby_t *l) {
    uint8_t *sendbuf = get_sendbuf();
    dc_lobby_join_pkt *pkt = (dc_lobby_join_pkt *)sendbuf;
    int i, pls = 0;
    uint16_t pkt_size = 0x10;

    /* Clear the packet's header. */
    memset(pkt, 0, sizeof(dc_lobby_join_pkt));

    /* Fill in the basics. */
    pkt->hdr.pkt_type = SHIP_LOBBY_JOIN_TYPE;
    pkt->leader_id = l->leader_id;
    pkt->one = 1;
    pkt->lobby_num = l->lobby_id - 1;
    pkt->block_num = LE16(l->block->b);

    for(i = 0; i < l->max_clients; ++i) {
        /* Skip blank clients. */
        if(l->clients[i] == NULL) {
            continue;
        }
        /* If this is the client we're sending to, mark their client id. */
        else if(l->clients[i] == c) {
            pkt->client_id = (uint8_t)i;
        }

        /* Copy the player's data into the packet. */
        pkt->entries[pls].hdr.tag = LE32(0x00010000);
        pkt->entries[pls].hdr.guildcard = LE32(l->clients[i]->guildcard);
        pkt->entries[pls].hdr.ip_addr = 0;
        pkt->entries[pls].hdr.client_id = LE32(i);

        memcpy(pkt->entries[pls].hdr.name, l->clients[i]->pl->name, 16);
        memcpy(&pkt->entries[pls].data, l->clients[i]->pl, sizeof(player_t));

        ++pls;
        pkt_size += 1084;
    }

    /* Fill in the rest of it. */
    pkt->hdr.flags = (uint8_t)pls;
    pkt->hdr.pkt_len = LE16(pkt_size);

    /* Send it away */
    return crypt_send(c, pkt_size, sendbuf);
}

int send_lobby_join(ship_client_t *c, lobby_t *l) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
            return send_dc_lobby_join(c, l);

        case CLIENT_VERSION_DCV2:
            if(send_dc_lobby_join(c, l)) {
                return -1;
            }

            return send_dc_lobby_arrows(l, c);

    }

    return -1;
}

/* Send a prepared packet to the given client. */
int send_pkt_dc(ship_client_t *c, dc_pkt_hdr_t *pkt) {
    uint8_t *sendbuf = get_sendbuf();
    int len = (int)LE16(pkt->pkt_len);

    /* Copy the packet to the send buffer */
    memcpy(sendbuf, pkt, len);

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

static int send_dc_lobby_add_player(lobby_t *l, ship_client_t *c,
                                    ship_client_t *nc) {
    uint8_t *sendbuf = get_sendbuf();
    dc_lobby_join_pkt *pkt = (dc_lobby_join_pkt *)sendbuf;

    /* Clear the packet's header. */
    memset(pkt, 0, sizeof(dc_lobby_join_pkt));

    /* Fill in the basics. */
    pkt->hdr.pkt_type = l->type & LOBBY_TYPE_DEFAULT ? 
        SHIP_LOBBY_ADD_PLAYER_TYPE : SHIP_GAME_ADD_PLAYER_TYPE;
    pkt->hdr.flags = 1;
    pkt->hdr.pkt_len = LE16(0x044C);
    pkt->leader_id = l->leader_id;
    pkt->one = 1;
    pkt->lobby_num = l->lobby_id - 1;
    pkt->block_num = l->block->b;

    /* Copy the player's data into the packet. */
    pkt->entries[0].hdr.tag = LE32(0x00010000);
    pkt->entries[0].hdr.guildcard = LE32(nc->guildcard);
    pkt->entries[0].hdr.ip_addr = 0;
    pkt->entries[0].hdr.client_id = LE32(nc->client_id);

    memcpy(pkt->entries[0].hdr.name, nc->pl->name, 16);
    memcpy(&pkt->entries[0].data, nc->pl, sizeof(player_t));

    /* Send it away */
    return crypt_send(c, 0x044C, sendbuf);
}

/* Send a packet to all clients in the lobby when a new player joins. */
int send_lobby_add_player(lobby_t *l, ship_client_t *c) {
    int i;

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL && l->clients[i] != c) {
            pthread_mutex_lock(&l->clients[i]->mutex);

            /* Call the appropriate function. */
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                    send_dc_lobby_add_player(l, l->clients[i], c);
                    break;
            }

            pthread_mutex_unlock(&l->clients[i]->mutex);
        }
    }

    return 0;
}

/* Send a packet to a client that leaves a lobby. */
static int send_dc_lobby_leave(lobby_t *l, ship_client_t *c, int client_id) {
    uint8_t *sendbuf = get_sendbuf();
    dc_lobby_leave_pkt *pkt = (dc_lobby_leave_pkt *)sendbuf;

    /* Fill in the header */
    pkt->hdr.pkt_type = l->type & LOBBY_TYPE_DEFAULT ?
        SHIP_LOBBY_LEAVE_TYPE : SHIP_GAME_LEAVE_TYPE;
    pkt->hdr.flags = client_id;
    pkt->hdr.pkt_len = LE16(SHIP_DC_LOBBY_LEAVE_LENGTH);

    pkt->client_id = client_id;
    pkt->leader_id = l->leader_id;
    pkt->padding = 0;

    /* Send it away */
    return crypt_send(c, SHIP_DC_LOBBY_LEAVE_LENGTH, sendbuf);
}

int send_lobby_leave(lobby_t *l, ship_client_t *c, int client_id) {
    int i;

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL) {
            pthread_mutex_lock(&l->clients[i]->mutex);

            /* Call the appropriate function. */
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                    send_dc_lobby_leave(l, l->clients[i], client_id);
                    break;
            }

            pthread_mutex_unlock(&l->clients[i]->mutex);
        }
    }

    return 0;
}

static int send_dc_lobby_chat(lobby_t *l, ship_client_t *c, ship_client_t *s,
                              char msg[]) {
    uint8_t *sendbuf = get_sendbuf();
    dc_chat_pkt *pkt = (dc_chat_pkt *)sendbuf;
    int len;

    /* Clear the packet header */
    memset(pkt, 0, sizeof(dc_chat_pkt));

    /* Fill in the basics */
    pkt->hdr.pkt_type = SHIP_CHAT_TYPE;
    pkt->hdr.flags = 0;
    pkt->guildcard = LE32(s->guildcard);

    /* Fill in the message */
    len = sprintf(pkt->msg, "%s\t\tE%s", s->pl->name, msg) + 1;

    /* Add any padding needed */
    while(len & 0x03) {
        pkt->msg[len++] = 0;
    }

    /* Fill in the length */
    len += 0x0C;
    pkt->hdr.pkt_len = LE16(len);

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

/* Send a talk packet to the specified lobby. */
int send_lobby_chat(lobby_t *l, ship_client_t *sender, char msg[]) {
    int i;

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL) {
            pthread_mutex_lock(&l->clients[i]->mutex);

            /* Call the appropriate function. */
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                    send_dc_lobby_chat(l, l->clients[i], sender, msg);
                    break;
            }

            pthread_mutex_unlock(&l->clients[i]->mutex);
        }
    }

    return 0;
}

/* Send a guild card search reply to the specified client. */
static int send_dc_guild_reply(ship_client_t *c, uint32_t gc, in_addr_t ip,
                               uint16_t port, char game[], int block,
                               char ship[], uint32_t lobby, char name[]) {
    uint8_t *sendbuf = get_sendbuf();
    dc_guild_reply_pkt *pkt = (dc_guild_reply_pkt *)sendbuf;

    /* Clear it out first */
    memset(pkt, 0, SHIP_DC_GUILD_REPLY_LENGTH);

    /* Fill in the simple stuff */
    pkt->hdr.pkt_type = SHIP_DC_GUILD_REPLY_TYPE;
    pkt->hdr.pkt_len = LE16(SHIP_DC_GUILD_REPLY_LENGTH);
    pkt->tag = LE32(0x00010000);
    pkt->gc_search = LE32(c->guildcard);
    pkt->gc_target = LE32(gc);
    pkt->ip = ip;
    pkt->port = LE16(port);
    pkt->menu_id = LE32(0xFFFFFFFF);
    pkt->item_id = LE32(lobby);
    strcpy(pkt->name, name);

    /* Fill in the location string */
    sprintf(pkt->location, "%s,BLOCK%02d,%s", game, block, ship);

    /* Send it away */
    return crypt_send(c, SHIP_DC_GUILD_REPLY_LENGTH, sendbuf);
}

int send_guild_reply(ship_client_t *c, uint32_t gc, in_addr_t ip, uint16_t port,
                     char game[], int block, char ship[], uint32_t lobby,
                     char name[]) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_guild_reply(c, gc, ip, port, game, block, ship,
                                       lobby, name);
    }
    
    return -1;
}

static int send_dc_message(ship_client_t *c, char msg[], uint16_t type) {
    uint8_t *sendbuf = get_sendbuf();
    dc_chat_pkt *pkt = (dc_chat_pkt *)sendbuf;
    int len;

    /* Clear the packet header */
    memset(pkt, 0, sizeof(dc_chat_pkt));

    /* Fill in the basics */
    pkt->hdr.pkt_type = type;
    pkt->hdr.flags = 0;

    /* Fill in the message */
    len = sprintf(pkt->msg, "\tE%s", msg) + 1;

    /* Add any padding needed */
    while(len & 0x03) {
        pkt->msg[len++] = 0;
    }

    /* Fill in the length */
    len += 0x0C;
    pkt->hdr.pkt_len = LE16(len);

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

/* Send a message to the client. */
int send_message1(ship_client_t *c, char msg[]) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_message(c, msg, SHIP_MSG1_TYPE);
    }
    
    return -1;
}

/* Send a text message to the client (i.e, for stuff related to commands). */
int send_txt(ship_client_t *c, char msg[]) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_message(c, msg, SHIP_TEXT_MSG_TYPE);
    }

    return -1;
}

/* Send a packet to the client indicating information about the game they're
   joining. */
static int send_dc_game_join(ship_client_t *c, lobby_t *l) {
    uint8_t *sendbuf = get_sendbuf();
    dc_game_join_pkt *pkt = (dc_game_join_pkt *)sendbuf;
    int clients = 0, i;

    /* Clear it out first. */
    memset(pkt, 0, SHIP_DC_GAME_JOIN_LENGTH);

    /* Fill in the basics. */
    pkt->hdr.pkt_type = SHIP_GAME_JOIN_TYPE;
    pkt->hdr.pkt_len = LE16(SHIP_DC_GAME_JOIN_LENGTH);
    pkt->client_id = c->client_id;
    pkt->leader_id = l->leader_id;
    pkt->one = 1;
    pkt->difficulty = l->difficulty;
    pkt->battle = l->battle;
    pkt->event = l->event;
    pkt->section = l->section;
    pkt->challenge = l->challenge;
    pkt->game_id = LE32(l->lobby_id);

    /* Fill in the variations array. */
    memcpy(pkt->maps, l->maps, 0x20 * 4);

    for(i = 0; i < 4; ++i) {
        if(l->clients[i]) {
            /* Copy the player's data into the packet. */
            pkt->players[i].tag = LE32(0x00010000);
            pkt->players[i].guildcard = LE32(l->clients[i]->guildcard);
            pkt->players[i].ip_addr = 0;
            pkt->players[i].client_id = LE32(i);

            memcpy(pkt->players[i].name, l->clients[i]->pl->name, 16);
            ++clients;
        }
    }

    /* Copy the client count over. */
    pkt->hdr.flags = (uint8_t) clients;

    /* Send it away */
    return crypt_send(c, SHIP_DC_GAME_JOIN_LENGTH, sendbuf);
}

int send_game_join(ship_client_t *c, lobby_t *l) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_game_join(c, l);
    }

    return -1;
}

static int send_dc_lobby_done_burst(lobby_t *l, ship_client_t *c,
                                    ship_client_t *nc) {
    uint8_t *sendbuf = get_sendbuf();
    dc_pkt_hdr_t *pkt = (dc_pkt_hdr_t *)sendbuf;

    /* Clear the packet's header. */
    memset(pkt, 0, 8);

    /* Fill in the basics. */
    pkt->pkt_type = SHIP_GAME_COMMAND0_TYPE;
    pkt->flags = 0;
    pkt->pkt_len = LE16(0x0008);

    /* No idea what this does, but its needed, apparently. */
    sendbuf[4] = 0x72;
    sendbuf[5] = 0x03;
    sendbuf[6] = 0x1C;
    sendbuf[7] = 0x08;

    /* Send it away */
    return crypt_send(c, 0x08, sendbuf);
}

/* Send a packet to all clients in the lobby letting them know the new player
   has finished bursting. */
int send_lobby_done_burst(lobby_t *l, ship_client_t *c) {
    int i;

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL && l->clients[i] != c) {
            pthread_mutex_lock(&l->clients[i]->mutex);

            /* Call the appropriate function. */
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                    send_dc_lobby_done_burst(l, l->clients[i], c);
                    break;
            }

            pthread_mutex_unlock(&l->clients[i]->mutex);
        }
    }

    return 0;
}

/* Send a packet to a client giving them the list of games on the block. */
static int send_dc_game_list(ship_client_t *c, block_t *b) {
    uint8_t *sendbuf = get_sendbuf();
    dc_game_list_pkt *pkt = (dc_game_list_pkt *)sendbuf;
    int entries = 1, len = 0x20;
    lobby_t *l;

    /* Clear out the packet and the first entry */
    memset(pkt, 0, 0x20);

    /* Fill in the header */
    pkt->hdr.pkt_type = SHIP_GAME_LIST_TYPE;

    /* Fill in the first entry */
    pkt->entries[0].menu_id = 0xFFFFFFFF;
    pkt->entries[0].item_id = 0xFFFFFFFF;
    pkt->entries[0].flags = 0x04;
    strcpy(pkt->entries[0].name, b->ship->cfg->name);

    TAILQ_FOREACH(l, &b->lobbies, qentry) {
        /* Ignore default lobbies */
        if(l->type & LOBBY_TYPE_DEFAULT) {
            continue;
        }

        /* Lock the lobby */
        pthread_mutex_lock(&l->mutex);

        /* Clear the entry */
        memset(pkt->entries + entries, 0, 0x1C);

        /* Copy the lobby's data to the packet */
        pkt->entries[entries].menu_id = LE32(0x00000002);
        pkt->entries[entries].item_id = LE32(l->lobby_id);
        pkt->entries[entries].difficulty = 0x22 + l->difficulty;
        pkt->entries[entries].players = l->num_clients;
        pkt->entries[entries].v2 = l->version;
        pkt->entries[entries].flags = (l->challenge ? 0x20 : 0x00) |
            (l->battle ? 0x10 : 0x00) | l->passwd[0] ? 2 : 0;

        /* Disable v2 games for v1 players */
        if(l->v2 && c->version == CLIENT_VERSION_DCV1) {
            pkt->entries[entries].flags |= 0x04;
        }

        /* Copy the name */
        strcpy(pkt->entries[entries].name, l->name);

        /* Unlock the lobby */
        pthread_mutex_unlock(&l->mutex);

        /* Update the counters */
        ++entries;
        len += 0x1C;
    }

    /* Fill in the rest of the header */
    pkt->hdr.flags = entries - 1;
    pkt->hdr.pkt_len = LE16(len);

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

int send_game_list(ship_client_t *c, block_t *b) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_game_list(c, b);
    }

    return -1;
}

/* Send the list of lobby info items to the client. */
static int send_dc_info_list(ship_client_t *c, ship_t *s) {
    uint8_t *sendbuf = get_sendbuf();
    dc_block_list_pkt *pkt = (dc_block_list_pkt *)sendbuf;
    int i, len = 0x20;
    iconv_t ic = iconv_open("SHIFT_JIS", "ASCII");
    size_t in, out;
    char *inptr, *outptr;

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear the base packet */
    memset(pkt, 0, sizeof(dc_block_list_pkt));

    /* Fill in some basic stuff */
    pkt->hdr.pkt_type = SHIP_LOBBY_INFO_TYPE;

    /* Fill in the ship name entry */
    memset(&pkt->entries[0], 0, 0x1C);
    pkt->entries[0].menu_id = LE32(0x00040000);
    pkt->entries[0].item_id = 0;
    pkt->entries[0].flags = 0;

    /* Convert to Shift-JIS */
    in = strlen(s->cfg->name);
    out = 0x10;
    inptr = s->cfg->name;
    outptr = pkt->entries[0].name;
    iconv(ic, &inptr, &in, &outptr, &out);

    /* Add what's needed at the end */
    pkt->entries[0].name[0x0F] = 0x00;
    pkt->entries[0].name[0x10] = 0x08;
    pkt->entries[0].name[0x11] = 0x00;

    /* Add each info item to the list. */
    for(i = 1; i <= s->cfg->info_file_count; ++i) {
        /* Clear out the ship information */
        memset(&pkt->entries[i], 0, 0x1C);

        /* Fill in what we have */
        pkt->entries[i].menu_id = LE32(0x00000000);
        pkt->entries[i].item_id = LE32((i - 1));
        pkt->entries[i].flags = LE16(0x0000);

        /* Convert the description to Shift-JIS */
        in = strlen(s->cfg->info_files_desc[i - 1]);
        out = 0x12;
        inptr = s->cfg->info_files_desc[i - 1];
        outptr = pkt->entries[i].name;
        iconv(ic, &inptr, &in, &outptr, &out);

        len += 0x1C;
    }

    /* Fill in the rest of the header */
    pkt->hdr.pkt_len = LE16(len);
    pkt->hdr.flags = (uint8_t)(s->cfg->info_file_count);

    iconv_close(ic);

    /* Send the packet away */
    return crypt_send(c, len, sendbuf);
}

int send_info_list(ship_client_t *c, ship_t *s) {
    /* Call the appropriate function */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_info_list(c, s);
    }

    return -1;
}

/* Send a message to the client. */
static int send_dc_message_box(ship_client_t *c, char msg[]) {
    uint8_t *sendbuf = get_sendbuf();
    dc_msg_box_pkt *pkt = (dc_msg_box_pkt *)sendbuf;
    int len;

    /* Fill in the basics */
    pkt->hdr.pkt_type = SHIP_MSG_BOX_TYPE;
    pkt->hdr.flags = 0;

    /* Fill in the message */
    len = sprintf(pkt->msg, "%s", msg) + 1;

    /* Add any padding needed */
    while(len & 0x03) {
        pkt->msg[len++] = 0;
    }

    /* Fill in the length */
    len += 0x04;
    pkt->hdr.pkt_len = LE16(len);

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

int send_message_box(ship_client_t *c, char msg[]) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_message_box(c, msg);
    }

    return -1;
}

/* Send the list of quest categories to the client. */
static int send_dc_quest_categories(ship_client_t *c,
                                    sylverant_quest_list_t *l) {
    uint8_t *sendbuf = get_sendbuf();
    dc_quest_list_pkt *pkt = (dc_quest_list_pkt *)sendbuf;
    int i, len = 0x04, entries = 0;

    /* Clear out the header */
    memset(pkt, 0, 0x04);

    /* Fill in the header */
    pkt->hdr.pkt_type = SHIP_QUEST_LIST_TYPE;

    for(i = 0; i < l->cat_count; ++i) {
        /* Clear the entry */
        memset(pkt->entries + i, 0, 0x98);

        /* Copy the category's information over to the packet */
        pkt->entries[i].menu_id = LE32(0x00000003);
        pkt->entries[i].item_id = LE32(i);

        memcpy(pkt->entries[i].name, l->cats[i].name, 32);
        memcpy(pkt->entries[i].desc, l->cats[i].desc, 112);

        ++entries;
        len += 0x98;
    }

    /* Fill in the rest of the header */
    pkt->hdr.flags = entries;
    pkt->hdr.pkt_len = LE16(len);

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

int send_quest_categories(ship_client_t *c, sylverant_quest_list_t *l) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_quest_categories(c, l);
    }

    return -1;
}

/* Send the list of quests in a category to the client. */
static int send_dc_quest_list(ship_client_t *c, int cat,
                              sylverant_quest_category_t *l) {
    uint8_t *sendbuf = get_sendbuf();
    dc_quest_list_pkt *pkt = (dc_quest_list_pkt *)sendbuf;
    int i, len = 0x04, entries = 0;

    /* Clear out the header */
    memset(pkt, 0, 0x04);

    /* Fill in the header */
    pkt->hdr.pkt_type = SHIP_QUEST_LIST_TYPE;

    for(i = 0; i < l->quest_count; ++i) {
        if(!(l->quests[i].versions & SYLVERANT_QUEST_V1)) {
            continue;
        }

        /* Clear the entry */
        memset(pkt->entries + entries, 0, 0x98);

        /* Copy the category's information over to the packet */
        pkt->entries[entries].menu_id = LE32(((0x00000004) | (cat << 8)));
        pkt->entries[entries].item_id = LE32(i);

        memcpy(pkt->entries[entries].name, l->quests[i].name, 32);
        memcpy(pkt->entries[entries].desc, l->quests[i].desc, 112);
        
        ++entries;
        len += 0x98;
    }

    /* Fill in the rest of the header */
    pkt->hdr.flags = entries;
    pkt->hdr.pkt_len = LE16(len);

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

int send_quest_list(ship_client_t *c, int cat, sylverant_quest_category_t *l) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_quest_list(c, cat, l);
    }

    return -1;
}

/* Send information about a quest to the client. */
static int send_dc_quest_info(ship_client_t *c, sylverant_quest_t *q) {
    uint8_t *sendbuf = get_sendbuf();
    dc_msg_box_pkt *pkt = (dc_msg_box_pkt *)sendbuf;

    /* Clear the packet header */
    memset(pkt, 0, SHIP_DC_QUEST_INFO_LENGTH);

    /* Fill in the basics */
    pkt->hdr.pkt_type = SHIP_QUEST_INFO_TYPE;
    pkt->hdr.flags = 0;
    pkt->hdr.pkt_len = LE16(SHIP_DC_QUEST_INFO_LENGTH);

    /* Fill in the message */
    strncpy(pkt->msg, q->long_desc, 0x123);
    pkt->msg[0x123] = '\0';

    /* Send it away */
    return crypt_send(c, SHIP_DC_QUEST_INFO_LENGTH, sendbuf);
}

int send_quest_info(ship_client_t *c, sylverant_quest_t *q) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_quest_info(c, q);
    }

    return -1;
}

/* Send a quest to everyone in a lobby. */
static int send_dcv1_quest(ship_client_t *c, sylverant_quest_t *q) {
    uint8_t *sendbuf = get_sendbuf();
    dc_quest_file_pkt *file = (dc_quest_file_pkt *)sendbuf;
    dc_quest_chunk_pkt *chunk = (dc_quest_chunk_pkt *)sendbuf;
    FILE *bin, *dat;
    uint32_t binlen, datlen;
    int bindone = 0, datdone = 0, chunknum = 0;
    char filename[256];
    size_t amt;

    /* Each quest has two files: a .dat file and a .bin file, send a file packet
       for each of them. The files are v6 here for drop-in compatibility with
       the quests from newserv/Aeon */
    sprintf(filename, "quests/%sv6.bin", q->prefix);
    bin = fopen(filename, "rb");

    sprintf(filename, "quests/%sv6.dat", q->prefix);
    dat = fopen(filename, "rb");

    if(!bin || !dat) {
        return -1;
    }

    /* Figure out how long each of the files are */
    fseek(bin, 0, SEEK_END);
    binlen = (uint32_t)ftell(bin);
    fseek(bin, 0, SEEK_SET);

    fseek(dat, 0, SEEK_END);
    datlen = (uint32_t)ftell(dat);
    fseek(dat, 0, SEEK_SET);

    /* Send the file info packets */
    /* Start with the .dat file. */
    memset(file, 0, sizeof(dc_quest_file_pkt));
    file->hdr.pkt_type = SHIP_QUEST_FILE_TYPE;
    file->hdr.flags = 0x02; /* ??? */
    file->hdr.pkt_len = LE16(SHIP_DC_QUEST_FILE_LENGTH);
    sprintf(file->name, "PSO/%s", q->name);
    sprintf(file->filename, "%sv6.dat", q->prefix);
    file->length = LE32(datlen);

    if(crypt_send(c, SHIP_DC_QUEST_FILE_LENGTH, sendbuf)) {
        return -2;
    }

    /* Now the .bin file. */
    memset(file, 0, sizeof(dc_quest_file_pkt));
    file->hdr.pkt_type = SHIP_QUEST_FILE_TYPE;
    file->hdr.flags = 0x02; /* ??? */
    file->hdr.pkt_len = LE16(SHIP_DC_QUEST_FILE_LENGTH);
    sprintf(file->name, "PSO/%s", q->name);
    sprintf(file->filename, "%sv6.bin", q->prefix);
    file->length = LE32(binlen);

    if(crypt_send(c, SHIP_DC_QUEST_FILE_LENGTH, sendbuf)) {
        return -2;
    }

    /* Now send the chunks of the file, interleaved. */
    while(!bindone || !datdone) {
        /* Start with the dat file if we've got any more to send from it */
        if(!datdone) {
            /* Clear the packet */
            memset(chunk, 0, sizeof(dc_quest_chunk_pkt));

            /* Fill in the header */
            chunk->hdr.pkt_type = SHIP_QUEST_CHUNK_TYPE;
            chunk->hdr.flags = (uint8_t)chunknum;
            chunk->hdr.pkt_len = LE16(SHIP_DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%sv6.dat", q->prefix);
            amt = fread(chunk->data, 1, 0x400, dat);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, SHIP_DC_QUEST_CHUNK_LENGTH, sendbuf)) {
                return -3;
            }

            /* Are we done with this file? */
            if(amt != 0x400) {
                datdone = 1;
            }
        }

        /* Then the bin file if we've got any more to send from it */
        if(!bindone) {
            /* Clear the packet */
            memset(chunk, 0, sizeof(dc_quest_chunk_pkt));

            /* Fill in the header */
            chunk->hdr.pkt_type = SHIP_QUEST_CHUNK_TYPE;
            chunk->hdr.flags = (uint8_t)chunknum;
            chunk->hdr.pkt_len = LE16(SHIP_DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%sv6.bin", q->prefix);
            amt = fread(chunk->data, 1, 0x400, bin);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, SHIP_DC_QUEST_CHUNK_LENGTH, sendbuf)) {
                return -3;
            }

            /* Are we done with this file? */
            if(amt != 0x400) {
                bindone = 1;
            }
        }

        ++chunknum;
    }

    /* We're done with the files, close them */
    fclose(bin);
    fclose(dat);

    return 0;
}

int send_quest(lobby_t *l, sylverant_quest_t *q) {
    int i;

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL) {
            /* Call the appropriate function. */
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                    send_dcv1_quest(l->clients[i], q);
                    break;
            }
        }
    }

    return 0;
}

/* Send the lobby name to the client. */
static int send_dcv2_lobby_name(ship_client_t *c, lobby_t *l) {
    uint8_t *sendbuf = get_sendbuf();
    dc_msg_box_pkt *pkt = (dc_msg_box_pkt *)sendbuf;
    uint16_t len;

    /* Clear the packet header */
    memset(pkt, 0, SHIP_DC_QUEST_INFO_LENGTH);

    /* Fill in the basics */
    pkt->hdr.pkt_type = SHIP_LOBBY_NAME_TYPE;
    pkt->hdr.flags = 0;
    pkt->hdr.pkt_len = LE16(SHIP_DC_QUEST_INFO_LENGTH);

    /* Fill in the message */
    len = (uint16_t)sprintf(pkt->msg, "%s", l->name) + 1;

    /* Add any padding needed */
    while(len & 0x03) {
        pkt->msg[len++] = 0;
    }

    /* Fill in the length */
    len += 0x04;
    pkt->hdr.pkt_len = LE16(len);

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

int send_lobby_name(ship_client_t *c, lobby_t *l) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV2: /* Should be V2... V1 doesn't do this */
            return send_dcv2_lobby_name(c, l);
    }

    return -1;
}

/* Send a packet to all clients in the lobby letting them know about a change to
   the arrows displayed. */
static int send_dc_lobby_arrows(lobby_t *l, ship_client_t *c) {
    uint8_t *sendbuf = get_sendbuf();
    dc_arrow_list_pkt *pkt = (dc_arrow_list_pkt *)sendbuf;
    int clients = 0, len = 0x04, i;

    /* Clear the packet's header. */
    memset(pkt, 0, sizeof(dc_arrow_list_pkt));

    /* Fill in the basics. */
    pkt->hdr.pkt_type = SHIP_LOBBY_ARROW_LIST_TYPE;

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i]) {
            /* Copy the player's data into the packet. */
            pkt->entries[i].tag = LE32(0x00010000);
            pkt->entries[i].guildcard = LE32(l->clients[i]->guildcard);
            pkt->entries[i].arrow = LE32(l->clients[i]->arrow);

            ++clients;
            len += 0x0C;
        }
    }

    /* Fill in the rest of it */
    pkt->hdr.flags = (uint8_t)clients;
    pkt->hdr.pkt_len = LE16(((uint16_t)len));

    /* Don't send anything if we have no clients. */
    if(!clients) {
        return 0;
    }

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

int send_lobby_arrows(lobby_t *l) {
    int i;

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL) {
            pthread_mutex_lock(&l->clients[i]->mutex);

            /* Call the appropriate function. */
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV1:
                    /* V1 doesn't support this and will disconnect if it gets
                       this packet. */
                    break;

                case CLIENT_VERSION_DCV2:
                    send_dc_lobby_arrows(l, l->clients[i]);
                    break;
            }

            pthread_mutex_unlock(&l->clients[i]->mutex);
        }
    }

    return 0;
}

/* Send a packet to ONE client letting them know about the arrow colors in the
   given lobby. */
int send_arrows(ship_client_t *c, lobby_t *l) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
            /* V1 doesn't support this and will disconnect if it gets
               this packet. */
            break;

        case CLIENT_VERSION_DCV2:
            return send_dc_lobby_arrows(l, c);
    }

    return -1;
}

/* Send a ship list packet to the client. */
static int send_dc_ship_list(ship_client_t *c, miniship_t *l, int ships) {
    uint8_t *sendbuf = get_sendbuf();
    dc_ship_list_pkt *pkt = (dc_ship_list_pkt *)sendbuf;
    int len = 0x20, i, entries = 0;

    /* Clear the packet's header. */
    memset(pkt, 0, 0x20);

    /* Fill in the basics. */
    pkt->hdr.pkt_type = SHIP_SHIP_LIST_TYPE;

    /* Fill in the "DATABASE/JP" entry */
    memset(&pkt->entries[0], 0, 0x1C);
    pkt->entries[0].menu_id = LE32(0x00000005);
    pkt->entries[0].item_id = 0;
    pkt->entries[0].flags = LE16(0x0004);
    strcpy(pkt->entries[0].name, "DATABASE/JP");
    pkt->entries[0].name[0x11] = 0x08;
    entries = 1;

    for(i = 0; i < ships; ++i) {
        if(l[i].ship_id) {
            /* Clear the new entry */
            memset(&pkt->entries[entries], 0, 0x1C);

            /* Copy the ship's information to the packet. */
            pkt->entries[entries].menu_id = LE32(0x00000005);
            pkt->entries[entries].item_id = LE32(l[i].ship_id);
            pkt->entries[entries].flags = 0;
            strcpy(pkt->entries[entries].name, l[i].name);

            ++entries;
            len += 0x1C;
        }
    }

    /* We'll definitely have at least one ship (ourselves), so just fill in the
       rest of it */
    pkt->hdr.flags = (uint8_t)(entries - 1);
    pkt->hdr.pkt_len = LE16(((uint16_t)len));

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

int send_ship_list(ship_client_t *c, miniship_t *l, int ships) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_ship_list(c, l, ships);
    }

    return -1;
}

/* Send a warp command to the client. */
static int send_dc_warp(ship_client_t *c, uint8_t area) {
    uint8_t *sendbuf = get_sendbuf();
    dc_pkt_hdr_t *pkt = (dc_pkt_hdr_t *)sendbuf;

    /* Fill in the basics. */
    pkt->pkt_type = SHIP_GAME_COMMAND2_TYPE;
    pkt->flags = c->client_id;
    pkt->pkt_len = LE16(0x000C);

    /* Fill in the stuff that will make us warp. */
    sendbuf[4] = 0x94;
    sendbuf[5] = 0x02;
    sendbuf[6] = c->client_id;
    sendbuf[7] = 0x00;
    sendbuf[8] = area;
    sendbuf[9] = 0x00;
    sendbuf[10] = 0x00;
    sendbuf[11] = 0x00;

    return crypt_send(c, 12, sendbuf);
}

int send_warp(ship_client_t *c, uint8_t area) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_warp(c, area);
    }

    return -1;
}
