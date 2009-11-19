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
#include <sys/time.h>
#include <sys/socket.h>
#include <iconv.h>

#include <sylverant/encryption.h>
#include <sylverant/database.h>

#include "ship_packets.h"
#include "utils.h"

/* Options for choice search. */
typedef struct cs_opt {
    uint16_t menu_id;
    uint16_t item_id;
    char text[0x1C];
} cs_opt_t;

cs_opt_t cs_options[] = {
    { 0x0000, 0x0001, "Hunter's Level" },
    { 0x0001, 0x0000, "Any" },
    { 0x0001, 0x0001, "Own Level +/- 5" },
    { 0x0001, 0x0002, "Level 1-10" },
    { 0x0001, 0x0003, "Level 11-20" },
    { 0x0001, 0x0004, "Level 21-40" },
    { 0x0001, 0x0005, "Level 41-60" },
    { 0x0001, 0x0006, "Level 61-80" },
    { 0x0001, 0x0007, "Level 81-100" },
    { 0x0001, 0x0008, "Level 101-120" },
    { 0x0001, 0x0009, "Level 121-160" },
    { 0x0001, 0x000A, "Level 161-200" },
    { 0x0000, 0x0002, "Hunter's Class" },
    { 0x0002, 0x0000, "Any" },
    { 0x0002, 0x0001, "HUmar" },
    { 0x0002, 0x0002, "HUnewearl" },
    { 0x0002, 0x0003, "HUcast" },
    { 0x0002, 0x0004, "RAmar" },
    { 0x0002, 0x0005, "RAcast" },
    { 0x0002, 0x0006, "RAcaseal" },
    { 0x0002, 0x0007, "FOmar" },
    { 0x0002, 0x0008, "FOnewm" },
    { 0x0002, 0x0009, "FOnewearl" }
};

#define CS_OPTIONS_COUNT 23

extern sylverant_shipcfg_t *cfg;
extern ship_t **ships;

extern in_addr_t local_addr;
extern in_addr_t netmask;

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

    c->last_sent = time(NULL);

    return 0;
}

/* Encrypt and send a packet away. */
static int crypt_send(ship_client_t *c, int len, uint8_t *sendbuf) {
    /* Expand it to be a multiple of 8/4 bytes long */
    while(len & (c->hdr_size - 1)) {
        sendbuf[len++] = 0;
    }

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

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Scrub the buffer */
    memset(pkt, 0, sizeof(dc_welcome_pkt));

    /* Fill in the header */
    if(c->version == CLIENT_VERSION_DCV1 ||
       c->version == CLIENT_VERSION_DCV2) {
        pkt->hdr.dc.pkt_len = LE16(SHIP_DC_WELCOME_LENGTH);
        pkt->hdr.dc.pkt_type = SHIP_DC_WELCOME_TYPE;
    }
    else {
        pkt->hdr.pc.pkt_len = LE16(SHIP_DC_WELCOME_LENGTH);
        pkt->hdr.pc.pkt_type = SHIP_DC_WELCOME_TYPE;
    }

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

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Wipe the packet */
    memset(pkt, 0, sizeof(dc_security_pkt));

    /* Fill in the header */
    if(c->version == CLIENT_VERSION_DCV1 ||
       c->version == CLIENT_VERSION_DCV2) {
        pkt->hdr.dc.pkt_type = SHIP_DC_SECURITY_TYPE;
        pkt->hdr.dc.pkt_len = LE16((0x0C + data_len));
    }
    else {
        pkt->hdr.pc.pkt_type = SHIP_DC_SECURITY_TYPE;
        pkt->hdr.pc.pkt_len = LE16((0x0C + data_len));
    }

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

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Wipe the packet */
    memset(pkt, 0, SHIP_DC_REDIRECT_LENGTH);

    /* Fill in the header */
    if(c->version == CLIENT_VERSION_DCV1 ||
       c->version == CLIENT_VERSION_DCV2) {
        pkt->hdr.dc.pkt_type = SHIP_REDIRECT_TYPE;
        pkt->hdr.dc.pkt_len = LE16(SHIP_DC_REDIRECT_LENGTH);
    }
    else {
        pkt->hdr.pc.pkt_type = SHIP_REDIRECT_TYPE;
        pkt->hdr.pc.pkt_len = LE16(SHIP_DC_REDIRECT_LENGTH);
    }

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
        case CLIENT_VERSION_PC:
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

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Wipe the packet */
    memset(pkt, 0, SHIP_DC_TIMESTAMP_LENGTH);

    /* Fill in the header */
    if(c->version == CLIENT_VERSION_DCV1 ||
       c->version == CLIENT_VERSION_DCV2) {
        pkt->hdr.dc.pkt_type = SHIP_TIMESTAMP_TYPE;
        pkt->hdr.dc.pkt_len = LE16(SHIP_DC_TIMESTAMP_LENGTH);
    }
    else {
        pkt->hdr.pc.pkt_type = SHIP_TIMESTAMP_TYPE;
        pkt->hdr.pc.pkt_len = LE16(SHIP_DC_TIMESTAMP_LENGTH);
    }

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
        case CLIENT_VERSION_PC:
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

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
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

    /* Copy the ship's name to the packet. */
    strncpy(pkt->entries[0].name, s->cfg->name, 0x10);

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

        /* Create the name string */
        sprintf(tmp, "BLOCK%02d", i);
        strncpy(pkt->entries[i].name, tmp, 0x11);
        pkt->entries[i].name[0x11] = 0;

        len += 0x1C;
    }

    /* Fill in the rest of the header */
    pkt->hdr.pkt_len = LE16(len);
    pkt->hdr.flags = (uint8_t)(s->cfg->blocks);

    /* Send the packet away */
    return crypt_send(c, len, sendbuf);
}

static int send_pc_block_list(ship_client_t *c, ship_t *s) {
    uint8_t *sendbuf = get_sendbuf();
    pc_block_list_pkt *pkt = (pc_block_list_pkt *)sendbuf;
    char tmp[18];
    int i, j, len = 0x30;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Clear the base packet */
    memset(pkt, 0, sizeof(pc_block_list_pkt));

    /* Fill in some basic stuff */
    pkt->hdr.pkt_type = SHIP_DC_BLOCK_LIST_TYPE;

    /* Fill in the ship name entry */
    memset(&pkt->entries[0], 0, 0x2C);
    pkt->entries[0].menu_id = LE32(0x00040000);
    pkt->entries[0].item_id = 0;
    pkt->entries[0].flags = 0;

    /* Copy the ship's name to the packet. */
    for(i = 0; i < 0x10 && s->cfg->name[i]; ++i) {
        pkt->entries[0].name[i] = LE16(s->cfg->name[i]);
    }

    /* Add each block to the list. */
    for(i = 1; i <= s->cfg->blocks; ++i) {
        /* Clear out the ship information */
        memset(&pkt->entries[i], 0, 0x2C);

        /* Fill in what we have */
        pkt->entries[i].menu_id = LE32(0x00000001);
        pkt->entries[i].item_id = LE32(i);
        pkt->entries[i].flags = LE16(0x0000);

        /* Create the name string */
        sprintf(tmp, "BLOCK%02d", i);

        /* This works here since the block name is always ASCII. */
        for(j = 0; j < 0x10 && tmp[j]; ++j) {
            pkt->entries[i].name[j] = LE16(tmp[j]);
        }

        len += 0x2C;
    }

    /* Fill in the rest of the header */
    pkt->hdr.pkt_len = LE16(len);
    pkt->hdr.flags = (uint8_t)(s->cfg->blocks);

    /* Send the packet away */
    return crypt_send(c, len, sendbuf);
}

int send_block_list(ship_client_t *c, ship_t *s) {
    /* Call the appropriate function */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_block_list(c, s);

        case CLIENT_VERSION_PC:
            return send_pc_block_list(c, s);
    }

    return -1;
}

/* Send a block/ship information reply packet to the client. */
static int send_dc_info_reply(ship_client_t *c, char msg[]) {
    uint8_t *sendbuf = get_sendbuf();
    dc_info_reply_pkt *pkt = (dc_info_reply_pkt *)sendbuf;
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2) {
        ic = iconv_open("SHIFT_JIS", "SHIFT_JIS");
    }
    else {
        ic = iconv_open("UTF-16LE", "SHIFT_JIS");
    }

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Convert the message to the appropriate encoding. */
    in = strlen(msg) + 1;
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
    if(c->version == CLIENT_VERSION_DCV1 ||
       c->version == CLIENT_VERSION_DCV2) {
        pkt->hdr.dc.pkt_type = SHIP_INFO_REPLY_TYPE;
        pkt->hdr.dc.flags = 0;
        pkt->hdr.dc.pkt_len = LE16(out);
    }
    else {
        pkt->hdr.pc.pkt_type = SHIP_INFO_REPLY_TYPE;
        pkt->hdr.pc.flags = 0;
        pkt->hdr.pc.pkt_len = LE16(out);
    }

    /* Send the packet away */
    return crypt_send(c, out, sendbuf);
}

int send_info_reply(ship_client_t *c, char msg[]) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
            return send_dc_info_reply(c, msg);
    }

    return -1;
}

/* Send a simple (header-only) packet to the client */
static int send_dc_simple(ship_client_t *c, int type, int flags) {
    uint8_t *sendbuf = get_sendbuf();
    dc_pkt_hdr_t *pkt = (dc_pkt_hdr_t *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the header */
    pkt->pkt_type = (uint8_t)type;
    pkt->flags = (uint8_t)flags;
    pkt->pkt_len = LE16(4);

    /* Send the packet away */
    return crypt_send(c, 4, sendbuf);
}

static int send_pc_simple(ship_client_t *c, int type, int flags) {
    uint8_t *sendbuf = get_sendbuf();
    pc_pkt_hdr_t *pkt = (pc_pkt_hdr_t *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

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

        case CLIENT_VERSION_PC:
            return send_pc_simple(c, type, flags);
    }

    return -1;
}

/* Send the lobby list packet to the client. */
static int send_dc_lobby_list(ship_client_t *c) {
    uint8_t *sendbuf = get_sendbuf();
    dc_lobby_list_pkt *pkt = (dc_lobby_list_pkt *)sendbuf;
    uint32_t i;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the header */
    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2) {
        pkt->hdr.dc.pkt_type = SHIP_LOBBY_LIST_TYPE;
        pkt->hdr.dc.flags = 0x0F;                               /* 15 lobbies */
        pkt->hdr.dc.pkt_len = LE16(SHIP_DC_LOBBY_LIST_LENGTH);
    }
    else {
        pkt->hdr.pc.pkt_type = SHIP_LOBBY_LIST_TYPE;
        pkt->hdr.pc.flags = 0x0F;                               /* 15 lobbies */
        pkt->hdr.pc.pkt_len = LE16(SHIP_DC_LOBBY_LIST_LENGTH);
    }

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
        case CLIENT_VERSION_PC:
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

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

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

static int send_pc_lobby_join(ship_client_t *c, lobby_t *l) {
    uint8_t *sendbuf = get_sendbuf();
    pc_lobby_join_pkt *pkt = (pc_lobby_join_pkt *)sendbuf;
    int i, pls = 0;
    uint16_t pkt_size = 0x10;
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;
    
    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }
    
    ic = iconv_open("UTF-16LE", "SHIFT_JIS");
    
    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear the packet's header. */
    memset(pkt, 0, sizeof(pc_lobby_join_pkt));

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

        /* Convert the name to UTF-16. */
        in = strlen(l->clients[i]->pl->name) + 1;
        out = 32;
        inptr = l->clients[i]->pl->name;
        outptr = (char *)pkt->entries[pls].hdr.name;
        iconv(ic, &inptr, &in, &outptr, &out);

        memcpy(&pkt->entries[pls].data, l->clients[i]->pl, sizeof(player_t));

        ++pls;
        pkt_size += 1100;
    }

    iconv_close(ic);

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

        case CLIENT_VERSION_PC:
            if(send_pc_lobby_join(c, l)) {
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

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Copy the packet to the send buffer */
    memcpy(sendbuf, pkt, len);

    /* Move stuff around for the PC version's odd header. */
    if(c->version == CLIENT_VERSION_PC) {
        pc_pkt_hdr_t *hdr = (pc_pkt_hdr_t *)sendbuf;

        hdr->pkt_len = pkt->pkt_len;
        hdr->flags = pkt->flags;
        hdr->pkt_type = pkt->pkt_type;
    }

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

static int send_dc_lobby_add_player(lobby_t *l, ship_client_t *c,
                                    ship_client_t *nc) {
    uint8_t *sendbuf = get_sendbuf();
    dc_lobby_join_pkt *pkt = (dc_lobby_join_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

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

static int send_pc_lobby_add_player(lobby_t *l, ship_client_t *c,
                                    ship_client_t *nc) {
    uint8_t *sendbuf = get_sendbuf();
    pc_lobby_join_pkt *pkt = (pc_lobby_join_pkt *)sendbuf;
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;
    
    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }
    
    ic = iconv_open("UTF-16LE", "SHIFT_JIS");
    
    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear the packet's header. */
    memset(pkt, 0, sizeof(pc_lobby_join_pkt));

    /* Fill in the basics. */
    pkt->hdr.pkt_type = l->type & LOBBY_TYPE_DEFAULT ? 
        SHIP_LOBBY_ADD_PLAYER_TYPE : SHIP_GAME_ADD_PLAYER_TYPE;
    pkt->hdr.flags = 1;
    pkt->hdr.pkt_len = LE16(0x045C);
    pkt->leader_id = l->leader_id;
    pkt->one = 1;
    pkt->lobby_num = l->lobby_id - 1;
    pkt->block_num = l->block->b;

    /* Copy the player's data into the packet. */
    pkt->entries[0].hdr.tag = LE32(0x00010000);
    pkt->entries[0].hdr.guildcard = LE32(nc->guildcard);
    pkt->entries[0].hdr.ip_addr = 0;
    pkt->entries[0].hdr.client_id = LE32(nc->client_id);

    /* Convert the name to UTF-16. */
    in = strlen(nc->pl->name) + 1;
    out = 32;
    inptr = nc->pl->name;
    outptr = (char *)pkt->entries[0].hdr.name;
    iconv(ic, &inptr, &in, &outptr, &out);

    memcpy(&pkt->entries[0].data, nc->pl, sizeof(player_t));

    /* Send it away */
    return crypt_send(c, 0x045C, sendbuf);
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

                case CLIENT_VERSION_PC:
                    send_pc_lobby_add_player(l, l->clients[i], c);
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

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the header */
    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2) {
        pkt->hdr.dc.pkt_type = l->type & LOBBY_TYPE_DEFAULT ?
            SHIP_LOBBY_LEAVE_TYPE : SHIP_GAME_LEAVE_TYPE;
        pkt->hdr.dc.flags = client_id;
        pkt->hdr.dc.pkt_len = LE16(SHIP_DC_LOBBY_LEAVE_LENGTH);
    }
    else {
        pkt->hdr.pc.pkt_type = l->type & LOBBY_TYPE_DEFAULT ?
            SHIP_LOBBY_LEAVE_TYPE : SHIP_GAME_LEAVE_TYPE;
        pkt->hdr.pc.flags = client_id;
        pkt->hdr.pc.pkt_len = LE16(SHIP_DC_LOBBY_LEAVE_LENGTH);
    }

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
                case CLIENT_VERSION_PC:
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
    iconv_t ic;
    char tm[strlen(msg) + 32];
    size_t in, out, len;
    char *inptr, *outptr;
    char *tmp = msg;

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2) {
        ic = iconv_open("SHIFT_JIS", "SHIFT_JIS");
    }
    else {
        ic = iconv_open("UTF-16LE", "SHIFT_JIS");
    }

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Clear the packet header */
    memset(pkt, 0, sizeof(dc_chat_pkt));

    /* Fill in the basics */
    pkt->guildcard = LE32(s->guildcard);

    /* Fill in the message */
    if(msg[0] == '\t') {
        tmp += 2;
    }
    in = sprintf(tm, "%s\t%s", s->pl->name, tmp) + 1;

    /* Convert the message to the appropriate encoding. */
    out = 65520;
    inptr = tm;
    outptr = pkt->msg;
    iconv(ic, &inptr, &in, &outptr, &out);
    iconv_close(ic);

    /* Figure out how long the new string is. */
    len = 65520 - out;

    /* Add any padding needed */
    while(len & 0x03) {
        pkt->msg[len++] = 0;
    }

    /* Fill in the length */
    len += 0x0C;

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2) {
        pkt->hdr.dc.pkt_type = SHIP_CHAT_TYPE;
        pkt->hdr.dc.flags = 0;
        pkt->hdr.dc.pkt_len = LE16(len);
    }
    else {
        pkt->hdr.pc.pkt_type = SHIP_CHAT_TYPE;
        pkt->hdr.pc.flags = 0;
        pkt->hdr.pc.pkt_len = LE16(len);
    }

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
                case CLIENT_VERSION_PC:
                    send_dc_lobby_chat(l, l->clients[i], sender, msg);
                    break;
            }

            pthread_mutex_unlock(&l->clients[i]->mutex);
        }
    }

    return 0;
}

static int send_dc_lobby_wchat(lobby_t *l, ship_client_t *c, ship_client_t *s,
                               uint16_t *msg, size_t len) {
    uint8_t *sendbuf = get_sendbuf();
    dc_chat_pkt *pkt = (dc_chat_pkt *)sendbuf;
    iconv_t ic, ic2;
    uint16_t tm[len];
    char tmp[32];
    size_t in, out;
    char *inptr, *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Create everything we need for converting stuff. */
    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2) {
        ic = iconv_open("SHIFT_JIS", "UTF-16LE");
        if(ic == (iconv_t)-1) {
            perror("iconv_open");
            return -1;
        }

        ic2 = iconv_open("SHIFT_JIS", "SHIFT_JIS");
        if(ic2 == (iconv_t)-1) {
            perror("iconv_open");
            iconv_close(ic);
            return -1;
        }
    }
    else {
        ic = iconv_open("UTF-16LE", "UTF-16LE");
        if(ic == (iconv_t)-1) {
            perror("iconv_open");
            return -1;
        }

        ic2 = iconv_open("UTF-16LE", "SHIFT_JIS");
        if(ic2 == (iconv_t)-1) {
            perror("iconv_open");
            iconv_close(ic);
            return -1;
        }
    }

    /* Clear the packet header */
    memset(pkt, 0, sizeof(dc_chat_pkt));

    /* Fill in the basics */
    pkt->guildcard = LE32(s->guildcard);

    /* Convert the name string first. */
    in = sprintf(tmp, "%s\t", s->pl->name);
    out = 65520;
    inptr = tmp;
    outptr = pkt->msg;
    iconv(ic2, &inptr, &in, &outptr, &out);
    iconv_close(ic2);

    /* Fill in the message */
    if(LE16(msg[0]) != (uint16_t)'\t') {
        in = len;
        inptr = (char *)msg;
    }
    else {
        in = len - 4;
        inptr = (char *)(msg + 2);
    }

    /* Convert the message to the appropriate encoding. */
    iconv(ic, &inptr, &in, &outptr, &out);
    iconv_close(ic);

    /* Figure out how long the new string is. */
    len = 65520 - out;

    /* Add any padding needed */
    while(len & 0x03) {
        pkt->msg[len++] = 0;
    }

    /* Fill in the length */
    len += 0x0C;

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2) {
        pkt->hdr.dc.pkt_type = SHIP_CHAT_TYPE;
        pkt->hdr.dc.flags = 0;
        pkt->hdr.dc.pkt_len = LE16(len);
    }
    else {
        pkt->hdr.pc.pkt_type = SHIP_CHAT_TYPE;
        pkt->hdr.pc.flags = 0;
        pkt->hdr.pc.pkt_len = LE16(len);
    }

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

/* Send a talk packet to the specified lobby (UTF-16). */
int send_lobby_wchat(lobby_t *l, ship_client_t *sender, uint16_t *msg,
                     size_t len) {
    int i;

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL) {
            pthread_mutex_lock(&l->clients[i]->mutex);

            /* Call the appropriate function. */
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                case CLIENT_VERSION_PC:
                    send_dc_lobby_wchat(l, l->clients[i], sender, msg, len);
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

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

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

static int send_pc_guild_reply(ship_client_t *c, uint32_t gc, in_addr_t ip,
                               uint16_t port, char game[], int block,
                               char ship[], uint32_t lobby, char name[]) {
    uint8_t *sendbuf = get_sendbuf();
    pc_guild_reply_pkt *pkt = (pc_guild_reply_pkt *)sendbuf;
    char tmp[0x44];
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* We'll be converting stuff from Shift-JIS to UTF-16. */
    ic = iconv_open("UTF-16LE", "SHIFT_JIS");

    if(ic == (iconv_t)-1) {
        return -1;
    }

    /* Clear it out first */
    memset(pkt, 0, SHIP_PC_GUILD_REPLY_LENGTH);

    /* Fill in the simple stuff */
    pkt->hdr.pkt_type = SHIP_DC_GUILD_REPLY_TYPE;
    pkt->hdr.pkt_len = LE16(SHIP_PC_GUILD_REPLY_LENGTH);
    pkt->tag = LE32(0x00010000);
    pkt->gc_search = LE32(c->guildcard);
    pkt->gc_target = LE32(gc);
    pkt->ip = ip;
    pkt->port = LE16(port);
    pkt->menu_id = LE32(0xFFFFFFFF);
    pkt->item_id = LE32(lobby);

    /* Fill in the location string... */
    in = sprintf(tmp, "%s,BLOCK%02d,%s", game, block, ship) + 1;
    out = 0x88;
    inptr = tmp;
    outptr = (char *)pkt->location;
    iconv(ic, &inptr, &in, &outptr, &out);

    /* ...and the name. */
    in = strlen(name) + 1;
    out = 0x40;
    inptr = name;
    outptr = (char *)pkt->name;
    iconv(ic, &inptr, &in, &outptr, &out);

    iconv_close(ic);

    /* Send it away */
    return crypt_send(c, SHIP_PC_GUILD_REPLY_LENGTH, sendbuf);
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

        case CLIENT_VERSION_PC:
            return send_pc_guild_reply(c, gc, ip, port, game, block, ship,
                                       lobby, name);
    }
    
    return -1;
}

static int send_dc_message(ship_client_t *c, char msg[], uint16_t type) {
    uint8_t *sendbuf = get_sendbuf();
    dc_chat_pkt *pkt = (dc_chat_pkt *)sendbuf;
    int len;
    iconv_t ic;
    char tm[strlen(msg) + 3];
    size_t in, out;
    char *inptr, *outptr;

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2) {
        ic = iconv_open("SHIFT_JIS", "SHIFT_JIS");
    }
    else {
        ic = iconv_open("UTF-16LE", "SHIFT_JIS");
    }
    
    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Clear the packet header */
    memset(pkt, 0, sizeof(dc_chat_pkt));

    /* Fill in the message */
    in = sprintf(tm, "\tE%s", msg) + 1;

    /* Convert the message to the appropriate encoding. */
    out = 65520;
    inptr = tm;
    outptr = pkt->msg;
    iconv(ic, &inptr, &in, &outptr, &out);
    iconv_close(ic);
    
    /* Figure out how long the new string is. */
    len = 65520 - out;

    /* Add any padding needed */
    while(len & 0x03) {
        pkt->msg[len++] = 0;
    }

    /* Fill in the length */
    len += 0x0C;

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2) {
        pkt->hdr.dc.pkt_type = type;
        pkt->hdr.dc.flags = 0;
        pkt->hdr.dc.pkt_len = LE16(len);
    }
    else {
        pkt->hdr.pc.pkt_type = type;
        pkt->hdr.pc.flags = 0;
        pkt->hdr.pc.pkt_len = LE16(len);
    }

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

/* Send a message to the client. */
int send_message1(ship_client_t *c, char msg[]) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
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
        case CLIENT_VERSION_PC:
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

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

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
    pkt->rand_seed = LE32(l->rand_seed);

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

static int send_pc_game_join(ship_client_t *c, lobby_t *l) {
    uint8_t *sendbuf = get_sendbuf();
    pc_game_join_pkt *pkt = (pc_game_join_pkt *)sendbuf;
    int clients = 0, i;
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    ic = iconv_open("UTF-16LE", "SHIFT_JIS");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear it out first. */
    memset(pkt, 0, sizeof(pc_game_join_pkt));

    /* Fill in the basics. */
    pkt->hdr.pkt_type = SHIP_GAME_JOIN_TYPE;
    pkt->hdr.pkt_len = LE16(sizeof(pc_game_join_pkt));
    pkt->client_id = c->client_id;
    pkt->leader_id = l->leader_id;
    pkt->one = 1;
    pkt->difficulty = l->difficulty;
    pkt->battle = l->battle;
    pkt->event = l->event;
    pkt->section = l->section;
    pkt->challenge = l->challenge;
    pkt->rand_seed = LE32(l->rand_seed);

    /* Fill in the variations array. */
    memcpy(pkt->maps, l->maps, 0x20 * 4);

    for(i = 0; i < 4; ++i) {
        if(l->clients[i]) {
            /* Copy the player's data into the packet. */
            pkt->players[i].tag = LE32(0x00010000);
            pkt->players[i].guildcard = LE32(l->clients[i]->guildcard);
            pkt->players[i].ip_addr = 0;
            pkt->players[i].client_id = LE32(i);

            /* Convert the name to UTF-16. */
            in = strlen(l->clients[i]->pl->name) + 1;
            out = 32;
            inptr = l->clients[i]->pl->name;
            outptr = (char *)pkt->players[i].name;
            iconv(ic, &inptr, &in, &outptr, &out);
            ++clients;
        }
    }

    iconv_close(ic);

    /* Copy the client count over. */
    pkt->hdr.flags = (uint8_t) clients;

    /* Send it away */
    return crypt_send(c, sizeof(pc_game_join_pkt), sendbuf);
}

int send_game_join(ship_client_t *c, lobby_t *l) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_game_join(c, l);

        case CLIENT_VERSION_PC:
            return send_pc_game_join(c, l);
    }

    return -1;
}

static int send_dc_lobby_done_burst(lobby_t *l, ship_client_t *c,
                                    ship_client_t *nc) {
    uint8_t *sendbuf = get_sendbuf();
    dc_pkt_hdr_t *pkt = (dc_pkt_hdr_t *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

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

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

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

static int send_pc_game_list(ship_client_t *c, block_t *b) {
    uint8_t *sendbuf = get_sendbuf();
    pc_game_list_pkt *pkt = (pc_game_list_pkt *)sendbuf;
    int entries = 1, len = 0x30;
    lobby_t *l;
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    ic = iconv_open("UTF-16LE", "SHIFT_JIS");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear out the packet and the first entry */
    memset(pkt, 0, 0x30);

    /* Fill in the header */
    pkt->hdr.pkt_type = SHIP_GAME_LIST_TYPE;

    /* Fill in the first entry */
    pkt->entries[0].menu_id = 0xFFFFFFFF;
    pkt->entries[0].item_id = 0xFFFFFFFF;
    pkt->entries[0].flags = 0x04;

    in = strlen(b->ship->cfg->name) + 1;
    out = 0x20;
    inptr = b->ship->cfg->name;
    outptr = (char *)pkt->entries[0].name;
    iconv(ic, &inptr, &in, &outptr, &out);

    TAILQ_FOREACH(l, &b->lobbies, qentry) {
        /* Ignore default lobbies */
        if(l->type & LOBBY_TYPE_DEFAULT) {
            continue;
        }

        /* Lock the lobby */
        pthread_mutex_lock(&l->mutex);

        /* Clear the entry */
        memset(pkt->entries + entries, 0, 0x2C);

        /* Copy the lobby's data to the packet */
        pkt->entries[entries].menu_id = LE32(0x00000002);
        pkt->entries[entries].item_id = LE32(l->lobby_id);
        pkt->entries[entries].difficulty = 0x22 + l->difficulty;
        pkt->entries[entries].players = l->num_clients;
        pkt->entries[entries].v2 = l->version;
        pkt->entries[entries].flags = (l->challenge ? 0x20 : 0x00) |
        (l->battle ? 0x10 : 0x00) | l->passwd[0] ? 2 : 0;

        /* Copy the name */
        in = 0x10;
        out = 0x20;
        inptr = l->name;
        outptr = (char *)pkt->entries[entries].name;
        iconv(ic, &inptr, &in, &outptr, &out);

        /* Unlock the lobby */
        pthread_mutex_unlock(&l->mutex);

        /* Update the counters */
        ++entries;
        len += 0x2C;
    }

    iconv_close(ic);

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

        case CLIENT_VERSION_PC:
            return send_pc_game_list(c, b);
    }

    return -1;
}

/* Send the list of lobby info items to the client. */
static int send_dc_info_list(ship_client_t *c, ship_t *s) {
    uint8_t *sendbuf = get_sendbuf();
    dc_block_list_pkt *pkt = (dc_block_list_pkt *)sendbuf;
    int i, len = 0x20;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
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
    strncpy(pkt->entries[0].name, s->cfg->name, 0x10);

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

        strncpy(pkt->entries[i].name, s->cfg->info_files_desc[i - 1], 0x11);
        pkt->entries[i].name[0x11] = 0;

        len += 0x1C;
    }

    /* Fill in the rest of the header */
    pkt->hdr.pkt_len = LE16(len);
    pkt->hdr.flags = (uint8_t)(s->cfg->info_file_count);

    /* Send the packet away */
    return crypt_send(c, len, sendbuf);
}

static int send_pc_info_list(ship_client_t *c, ship_t *s) {
    uint8_t *sendbuf = get_sendbuf();
    pc_block_list_pkt *pkt = (pc_block_list_pkt *)sendbuf;
    int i, len = 0x30;
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    ic = iconv_open("UTF-16LE", "SHIFT_JIS");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear the base packet */
    memset(pkt, 0, 0x30);

    /* Fill in some basic stuff */
    pkt->hdr.pkt_type = SHIP_LOBBY_INFO_TYPE;

    /* Fill in the ship name entry */
    memset(&pkt->entries[0], 0, 0x1C);
    pkt->entries[0].menu_id = LE32(0x00040000);
    pkt->entries[0].item_id = 0;
    pkt->entries[0].flags = 0;

    in = strlen(s->cfg->name) + 1;
    out = 0x20;
    inptr = s->cfg->name;
    outptr = (char *)pkt->entries[0].name;
    iconv(ic, &inptr, &in, &outptr, &out);

    /* Add each info item to the list. */
    for(i = 1; i <= s->cfg->info_file_count; ++i) {
        /* Clear out the ship information */
        memset(&pkt->entries[i], 0, 0x1C);

        /* Fill in what we have */
        pkt->entries[i].menu_id = LE32(0x00000000);
        pkt->entries[i].item_id = LE32((i - 1));
        pkt->entries[i].flags = LE16(0x0000);

        in = strlen(s->cfg->info_files_desc[i - 1]) + 1;
        out = 0x20;
        inptr = s->cfg->info_files_desc[i - 1];
        outptr = (char *)pkt->entries[i].name;
        iconv(ic, &inptr, &in, &outptr, &out);

        len += 0x2C;
    }

    iconv_close(ic);

    /* Fill in the rest of the header */
    pkt->hdr.pkt_len = LE16(len);
    pkt->hdr.flags = (uint8_t)(s->cfg->info_file_count);

    /* Send the packet away */
    return crypt_send(c, len, sendbuf);
}

int send_info_list(ship_client_t *c, ship_t *s) {
    /* Call the appropriate function */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_info_list(c, s);

        case CLIENT_VERSION_PC:
            return send_pc_info_list(c, s);
    }

    return -1;
}

/* Send a message to the client. */
static int send_dc_message_box(ship_client_t *c, char msg[]) {
    uint8_t *sendbuf = get_sendbuf();
    dc_msg_box_pkt *pkt = (dc_msg_box_pkt *)sendbuf;
    int len;
    iconv_t ic;
    size_t in, out, outt;
    char *inptr, *outptr;
    
    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2) {
        ic = iconv_open("SHIFT_JIS", "SHIFT_JIS");
    }
    else {
        ic = iconv_open("UTF-16LE", "SHIFT_JIS");
    }

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the message */
    in = strlen(msg) + 1;
    out = 65500;
    inptr = msg;
    outptr = (char *)pkt->msg;
    iconv(ic, &inptr, &in, &outptr, &out);
    len = 65500 - out;

    /* Add any padding needed */
    while(len & 0x03) {
        pkt->msg[len++] = 0;
    }

    /* Fill in the header */
    len += 0x04;

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV1) {
        pkt->hdr.dc.pkt_type = SHIP_MSG_BOX_TYPE;
        pkt->hdr.dc.flags = 0;
        pkt->hdr.dc.pkt_len = LE16(len);
    }
    else {
        pkt->hdr.pc.pkt_type = SHIP_MSG_BOX_TYPE;
        pkt->hdr.pc.flags = 0;
        pkt->hdr.pc.pkt_len = LE16(len);
    }

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

int send_message_box(ship_client_t *c, char msg[]) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
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

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

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

static int send_pc_quest_categories(ship_client_t *c,
                                    sylverant_quest_list_t *l) {
    uint8_t *sendbuf = get_sendbuf();
    pc_quest_list_pkt *pkt = (pc_quest_list_pkt *)sendbuf;
    int i, len = 0x04, entries = 0;
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Quest names are stored internally as Shift-JIS, convert to UTF-16. */
    ic = iconv_open("UTF-16LE", "SHIFT_JIS");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear out the header */
    memset(pkt, 0, 0x04);

    /* Fill in the header */
    pkt->hdr.pkt_type = SHIP_QUEST_LIST_TYPE;

    for(i = 0; i < l->cat_count; ++i) {
        /* Clear the entry */
        memset(pkt->entries + i, 0, 0x128);

        /* Copy the category's information over to the packet */
        pkt->entries[i].menu_id = LE32(0x00000003);
        pkt->entries[i].item_id = LE32(i);

        /* Convert the name and the description to UTF-16. */
        in = 32;
        out = 64;
        inptr = l->cats[i].name;
        outptr = (char *)pkt->entries[i].name;
        iconv(ic, &inptr, &in, &outptr, &out);

        in = 112;
        out = 224;
        inptr = l->cats[i].desc;
        outptr = (char *)pkt->entries[i].desc;
        iconv(ic, &inptr, &in, &outptr, &out);

        ++entries;
        len += 0x128;
    }

    iconv_close(ic);

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

        case CLIENT_VERSION_PC:
            return send_pc_quest_categories(c, l);
    }

    return -1;
}

/* Send the list of quests in a category to the client. */
static int send_dc_quest_list(ship_client_t *c, int cat,
                              sylverant_quest_category_t *l, uint32_t ver) {
    uint8_t *sendbuf = get_sendbuf();
    dc_quest_list_pkt *pkt = (dc_quest_list_pkt *)sendbuf;
    int i, len = 0x04, entries = 0;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Clear out the header */
    memset(pkt, 0, 0x04);

    /* Fill in the header */
    pkt->hdr.pkt_type = SHIP_QUEST_LIST_TYPE;

    for(i = 0; i < l->quest_count; ++i) {
        if(!(l->quests[i].versions & ver)) {
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

static int send_pc_quest_list(ship_client_t *c, int cat,
                              sylverant_quest_category_t *l, uint32_t ver) {
    uint8_t *sendbuf = get_sendbuf();
    pc_quest_list_pkt *pkt = (pc_quest_list_pkt *)sendbuf;
    int i, len = 0x04, entries = 0;
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Quest names are stored internally as Shift-JIS, convert to UTF-16. */
    ic = iconv_open("UTF-16LE", "SHIFT_JIS");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear out the header */
    memset(pkt, 0, 0x04);

    /* Fill in the header */
    pkt->hdr.pkt_type = SHIP_QUEST_LIST_TYPE;

    for(i = 0; i < l->quest_count; ++i) {
        if(!(l->quests[i].versions & ver)) {
            continue;
        }

        /* Clear the entry */
        memset(pkt->entries + entries, 0, 0x98);

        /* Copy the category's information over to the packet */
        pkt->entries[entries].menu_id = LE32(((0x00000004) | (cat << 8)));
        pkt->entries[entries].item_id = LE32(i);

        /* Convert the name and the description to UTF-16. */
        in = 32;
        out = 64;
        inptr = l->quests[i].name;
        outptr = (char *)pkt->entries[entries].name;
        iconv(ic, &inptr, &in, &outptr, &out);

        in = 112;
        out = 224;
        inptr = l->quests[i].desc;
        outptr = (char *)pkt->entries[entries].desc;
        iconv(ic, &inptr, &in, &outptr, &out);

        ++entries;
        len += 0x128;
    }

    iconv_close(ic);

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
            if(c->cur_lobby->v2) {
                return send_dc_quest_list(c, cat, l, SYLVERANT_QUEST_V2);
            }
            else {
                return send_dc_quest_list(c, cat, l, SYLVERANT_QUEST_V1);
            }

        case CLIENT_VERSION_PC:
            if(c->cur_lobby->v2) {
                return send_pc_quest_list(c, cat, l, SYLVERANT_QUEST_V2);
            }
            else {
                return send_pc_quest_list(c, cat, l, SYLVERANT_QUEST_V1);
            }
    }

    return -1;
}

/* Send information about a quest to the client. */
static int send_dc_quest_info(ship_client_t *c, sylverant_quest_t *q) {
    uint8_t *sendbuf = get_sendbuf();
    dc_msg_box_pkt *pkt = (dc_msg_box_pkt *)sendbuf;
    iconv_t ic;
    size_t in, out, outt;
    char *inptr, *outptr;
    int len;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2) {
        ic = iconv_open("SHIFT_JIS", "SHIFT_JIS");
    }
    else {
        ic = iconv_open("UTF-16LE", "SHIFT_JIS");
    }

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear the packet header */
    memset(pkt, 0, SHIP_DC_QUEST_INFO_LENGTH);

    /* Fill in the basics */
    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2) {
        pkt->hdr.dc.pkt_type = SHIP_QUEST_INFO_TYPE;
        pkt->hdr.dc.flags = 0;
        pkt->hdr.dc.pkt_len = LE16(SHIP_DC_QUEST_INFO_LENGTH);
        len = SHIP_DC_QUEST_INFO_LENGTH;
        outt = 0x124;
    }
    else {
        pkt->hdr.pc.pkt_type = SHIP_QUEST_INFO_TYPE;
        pkt->hdr.pc.flags = 0;
        pkt->hdr.pc.pkt_len = LE16(SHIP_PC_QUEST_INFO_LENGTH);
        len = SHIP_PC_QUEST_INFO_LENGTH;
        outt = 0x248;
    }

    in = 0x124;
    out = outt;
    inptr = q->long_desc;
    outptr = pkt->msg;
    iconv(ic, &inptr, &in, &outptr, &out);
    iconv_close(ic);

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

int send_quest_info(ship_client_t *c, sylverant_quest_t *q) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
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

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Each quest has two files: a .dat file and a .bin file, send a file packet
       for each of them. */
    sprintf(filename, "quests/%sv1.bin", q->prefix);
    bin = fopen(filename, "rb");

    sprintf(filename, "quests/%sv1.dat", q->prefix);
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

    sprintf(file->name, "PSO/%s", q->name);

    file->hdr.pkt_type = SHIP_QUEST_FILE_TYPE;
    file->hdr.flags = 0x02; /* ??? */
    file->hdr.pkt_len = LE16(SHIP_DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%sv1.dat", q->prefix);
    file->length = LE32(datlen);

    if(crypt_send(c, SHIP_DC_QUEST_FILE_LENGTH, sendbuf)) {
        return -2;
    }

    /* Now the .bin file. */
    memset(file, 0, sizeof(dc_quest_file_pkt));

    sprintf(file->name, "PSO/%s", q->name);

    file->hdr.pkt_type = SHIP_QUEST_FILE_TYPE;
    file->hdr.flags = 0x02; /* ??? */
    file->hdr.pkt_len = LE16(SHIP_DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%sv1.bin", q->prefix);
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
            chunk->hdr.dc.pkt_type = SHIP_QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(SHIP_DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%sv1.dat", q->prefix);
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
            chunk->hdr.dc.pkt_type = SHIP_QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(SHIP_DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%sv1.bin", q->prefix);
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

static int send_dcv2_quest(ship_client_t *c, sylverant_quest_t *q) {
    uint8_t *sendbuf = get_sendbuf();
    dc_quest_file_pkt *file = (dc_quest_file_pkt *)sendbuf;
    dc_quest_chunk_pkt *chunk = (dc_quest_chunk_pkt *)sendbuf;
    FILE *bin, *dat;
    uint32_t binlen, datlen;
    int bindone = 0, datdone = 0, chunknum = 0;
    char filename[256];
    size_t amt;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Each quest has two files: a .dat file and a .bin file, send a file packet
       for each of them. */
    sprintf(filename, "quests/%sv2.bin", q->prefix);
    bin = fopen(filename, "rb");

    sprintf(filename, "quests/%sv2.dat", q->prefix);
    dat = fopen(filename, "rb");

    if(!bin || !dat) {
        return -1;
    }

    printf("sending %s\n", filename);

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

    sprintf(file->name, "PSO/%s", q->name);

    file->hdr.pkt_type = SHIP_QUEST_FILE_TYPE;
    file->hdr.flags = 0x02; /* ??? */
    file->hdr.pkt_len = LE16(SHIP_DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%sv2.dat", q->prefix);
    file->length = LE32(datlen);

    if(crypt_send(c, SHIP_DC_QUEST_FILE_LENGTH, sendbuf)) {
        return -2;
    }

    /* Now the .bin file. */
    memset(file, 0, sizeof(dc_quest_file_pkt));

    sprintf(file->name, "PSO/%s", q->name);
    
    file->hdr.pkt_type = SHIP_QUEST_FILE_TYPE;
    file->hdr.flags = 0x02; /* ??? */
    file->hdr.pkt_len = LE16(SHIP_DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%sv2.bin", q->prefix);
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
            chunk->hdr.dc.pkt_type = SHIP_QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(SHIP_DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%sv2.dat", q->prefix);
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
            chunk->hdr.dc.pkt_type = SHIP_QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(SHIP_DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%sv2.bin", q->prefix);
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

static int send_pc_quest(ship_client_t *c, sylverant_quest_t *q) {
    uint8_t *sendbuf = get_sendbuf();
    pc_quest_file_pkt *file = (pc_quest_file_pkt *)sendbuf;
    dc_quest_chunk_pkt *chunk = (dc_quest_chunk_pkt *)sendbuf;
    FILE *bin, *dat;
    uint32_t binlen, datlen;
    int bindone = 0, datdone = 0, chunknum = 0;
    char filename[256];
    size_t amt;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Each quest has two files: a .dat file and a .bin file, send a file packet
       for each of them. */
    sprintf(filename, "quests/%spc.bin", q->prefix);
    bin = fopen(filename, "rb");

    sprintf(filename, "quests/%spc.dat", q->prefix);
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
    memset(file, 0, sizeof(pc_quest_file_pkt));

    sprintf(file->name, "PSO/%s", q->name);

    file->hdr.pkt_type = SHIP_QUEST_FILE_TYPE;
    file->hdr.flags = 0x00;
    file->hdr.pkt_len = LE16(SHIP_DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%spc.dat", q->prefix);
    file->length = LE32(datlen);
    file->flags = 0x0002;

    if(crypt_send(c, SHIP_DC_QUEST_FILE_LENGTH, sendbuf)) {
        return -2;
    }

    /* Now the .bin file. */
    memset(file, 0, sizeof(pc_quest_file_pkt));

    sprintf(file->name, "PSO/%s", q->name);

    file->hdr.pkt_type = SHIP_QUEST_FILE_TYPE;
    file->hdr.flags = 0x00;
    file->hdr.pkt_len = LE16(SHIP_DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%spc.bin", q->prefix);
    file->length = LE32(binlen);
    file->flags = 0x0002;

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
            chunk->hdr.pc.pkt_type = SHIP_QUEST_CHUNK_TYPE;
            chunk->hdr.pc.flags = (uint8_t)chunknum;
            chunk->hdr.pc.pkt_len = LE16(SHIP_DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%spc.dat", q->prefix);
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
            chunk->hdr.pc.pkt_type = SHIP_QUEST_CHUNK_TYPE;
            chunk->hdr.pc.flags = (uint8_t)chunknum;
            chunk->hdr.pc.pkt_len = LE16(SHIP_DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%spc.bin", q->prefix);
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
                    if(l->v2) {
                        send_dcv2_quest(l->clients[i], q);
                    }
                    else {
                        send_dcv1_quest(l->clients[i], q);
                    }
                    break;

                case CLIENT_VERSION_PC:
                    send_pc_quest(l->clients[i], q);
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

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the basics */
    pkt->hdr.dc.pkt_type = SHIP_LOBBY_NAME_TYPE;
    pkt->hdr.dc.flags = 0;

    /* Fill in the message */
    len = (uint16_t)sprintf(pkt->msg, "%s", l->name) + 1;

    /* Add any padding needed */
    while(len & 0x03) {
        pkt->msg[len++] = 0;
    }

    /* Fill in the length */
    len += 0x04;
    pkt->hdr.dc.pkt_len = LE16(len);

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

static int send_pc_lobby_name(ship_client_t *c, lobby_t *l) {
    uint8_t *sendbuf = get_sendbuf();
    dc_msg_box_pkt *pkt = (dc_msg_box_pkt *)sendbuf;
    uint16_t len;
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Lobby names are stored internally as Shift-JIS, convert to UTF-16. */
    ic = iconv_open("UTF-16LE", "SHIFT_JIS");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Convert the message to the appropriate encoding. */
    in = strlen(l->name) + 1;
    out = 65532;
    inptr = l->name;
    outptr = pkt->msg;
    iconv(ic, &inptr, &in, &outptr, &out);
    iconv_close(ic);

    /* Fill in the basics */
    pkt->hdr.pc.pkt_type = SHIP_LOBBY_NAME_TYPE;
    pkt->hdr.pc.flags = 0;

    /* Add any padding needed */
    while(len & 0x03) {
        pkt->msg[len++] = 0;
    }

    /* Fill in the length */
    len += 0x04;
    pkt->hdr.pc.pkt_len = LE16(len);

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

int send_lobby_name(ship_client_t *c, lobby_t *l) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV2:
            return send_dcv2_lobby_name(c, l);

        case CLIENT_VERSION_PC:
            return send_pc_lobby_name(c, l);
    }

    return -1;
}

/* Send a packet to all clients in the lobby letting them know about a change to
   the arrows displayed. */
static int send_dc_lobby_arrows(lobby_t *l, ship_client_t *c) {
    uint8_t *sendbuf = get_sendbuf();
    dc_arrow_list_pkt *pkt = (dc_arrow_list_pkt *)sendbuf;
    int clients = 0, len = 0x04, i;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Clear the packet's header. */
    memset(pkt, 0, sizeof(dc_arrow_list_pkt));

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
    if(c->version == CLIENT_VERSION_DCV2) {
        pkt->hdr.dc.pkt_type = SHIP_LOBBY_ARROW_LIST_TYPE;
        pkt->hdr.dc.flags = (uint8_t)clients;
        pkt->hdr.dc.pkt_len = LE16(((uint16_t)len));
    }
    else {
        pkt->hdr.pc.pkt_type = SHIP_LOBBY_ARROW_LIST_TYPE;
        pkt->hdr.pc.flags = (uint8_t)clients;
        pkt->hdr.pc.pkt_len = LE16(((uint16_t)len));
    }

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
                case CLIENT_VERSION_PC:
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
        case CLIENT_VERSION_PC:
            return send_dc_lobby_arrows(l, c);
    }

    return -1;
}

/* Send a ship list packet to the client. */
static int send_dc_ship_list(ship_client_t *c, miniship_t *l, int ships) {
    uint8_t *sendbuf = get_sendbuf();
    dc_ship_list_pkt *pkt = (dc_ship_list_pkt *)sendbuf;
    int len = 0x20, i, entries = 0;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

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

static int send_pc_ship_list(ship_client_t *c, miniship_t *l, int ships) {
    uint8_t *sendbuf = get_sendbuf();
    pc_ship_list_pkt *pkt = (pc_ship_list_pkt *)sendbuf;
    int len = 0x30, i, entries = 0;
    iconv_t ic = iconv_open("UTF-16LE", "SHIFT_JIS");
    size_t in, out;
    char *inptr, *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear the packet's header. */
    memset(pkt, 0, 0x30);

    /* Fill in the basics. */
    pkt->hdr.pkt_type = SHIP_SHIP_LIST_TYPE;

    /* Fill in the "DATABASE/JP" entry */
    memset(&pkt->entries[0], 0, 0x1C);
    pkt->entries[0].menu_id = LE32(0x00000005);
    pkt->entries[0].item_id = 0;
    pkt->entries[0].flags = LE16(0x0004);
    memcpy(pkt->entries[0].name, "D\0A\0T\0A\0B\0A\0S\0E\0/\0J\0P\0", 22);
    entries = 1;

    for(i = 0; i < ships; ++i) {
        if(l[i].ship_id) {
            /* Clear the new entry */
            memset(&pkt->entries[entries], 0, 0x2C);

            /* Copy the ship's information to the packet. */
            pkt->entries[entries].menu_id = LE32(0x00000005);
            pkt->entries[entries].item_id = LE32(l[i].ship_id);
            pkt->entries[entries].flags = 0;

            /* Convert the name to UTF-16 */
            in = strlen(l[i].name) + 1;
            out = 0x22;
            inptr = l[i].name;
            outptr = (char *)pkt->entries[entries].name;
            iconv(ic, &inptr, &in, &outptr, &out);

            ++entries;
            len += 0x2C;
        }
    }

    iconv_close(ic);

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

        case CLIENT_VERSION_PC:
            return send_pc_ship_list(c, l, ships);
    }

    return -1;
}

/* Send a warp command to the client. */
static int send_dc_warp(ship_client_t *c, uint8_t area) {
    uint8_t *sendbuf = get_sendbuf();
    dc_pkt_hdr_t *pkt = (dc_pkt_hdr_t *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

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

static int send_pc_warp(ship_client_t *c, uint8_t area) {
    uint8_t *sendbuf = get_sendbuf();
    pc_pkt_hdr_t *pkt = (pc_pkt_hdr_t *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

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

        case CLIENT_VERSION_PC:
            return send_pc_warp(c, area);
    }

    return -1;
}

/* Send the choice search option list to the client. */
static int send_dc_choice_search(ship_client_t *c) {
    uint8_t *sendbuf = get_sendbuf();
    dc_choice_search_pkt *pkt = (dc_choice_search_pkt *)sendbuf;
    uint16_t len = 4 + 0x20 * CS_OPTIONS_COUNT;
    int i;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the basics. */
    pkt->hdr.pkt_type = SHIP_CHOICE_OPTION_TYPE;
    pkt->hdr.pkt_len = LE16(len);
    pkt->hdr.flags = CS_OPTIONS_COUNT;

    /* Copy the options over. */
    for(i = 0; i < CS_OPTIONS_COUNT; ++i) {
        memset(&pkt->entries[i], 0, 0x20);
        pkt->entries[i].menu_id = LE16(cs_options[i].menu_id);
        pkt->entries[i].item_id = LE16(cs_options[i].item_id);
        strcpy(pkt->entries[i].text, cs_options[i].text);
    }

    return crypt_send(c, len, sendbuf);
}

static int send_pc_choice_search(ship_client_t *c) {
    uint8_t *sendbuf = get_sendbuf();
    pc_choice_search_pkt *pkt = (pc_choice_search_pkt *)sendbuf;
    uint16_t len = 4 + 0x3C * CS_OPTIONS_COUNT;
    uint16_t i;
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Set up the converting stuff. */
    ic = iconv_open("UTF-16LE", "ASCII");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Fill in the basics. */
    pkt->hdr.pkt_type = SHIP_CHOICE_OPTION_TYPE;
    pkt->hdr.pkt_len = LE16(len);
    pkt->hdr.flags = CS_OPTIONS_COUNT;

    /* Copy the options over. */
    for(i = 0; i < CS_OPTIONS_COUNT; ++i) {
        pkt->entries[i].menu_id = LE16(cs_options[i].menu_id);
        pkt->entries[i].item_id = LE16(cs_options[i].item_id);

        /* Convert the text to UTF-16 */
        in = strlen(cs_options[i].text) + 1;
        out = 0x38;
        inptr = cs_options[i].text;
        outptr = (char *)pkt->entries[i].text;
        iconv(ic, &inptr, &in, &outptr, &out);
    }

    iconv_close(ic);

    return crypt_send(c, len, sendbuf);
}

int send_choice_search(ship_client_t *c) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_choice_search(c);

        case CLIENT_VERSION_PC:
            return send_pc_choice_search(c);
    }

    return -1;
}

static const char *classes[12] = {
    "HUmar", "HUnewearl", "HUcast",
    "RAmar", "RAcast", "RAcaseal",
    "FOmarl", "FOnewm", "FOnewearl",
    "HUcaseal", "FOmar", "RAmarl"
};

/* Send a reply to a choice search to the client. */
static int send_dc_choice_reply(ship_client_t *c, dc_choice_set_t *search,
                                int minlvl, int maxlvl, int cl, in_addr_t a) {
    uint8_t *sendbuf = get_sendbuf();
    dc_choice_reply_t *pkt = (dc_choice_reply_t *)sendbuf;
    uint16_t len = 4;
    uint8_t entries = 0;
    int i, j;
    ship_t *s;
    block_t *b;
    ship_client_t *it;
    char tmp[64];

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Search any local ships. */
    for(j = 0; j < cfg->ship_count; ++j) {
        s = ships[j];
        for(i = 0; i < s->cfg->blocks; ++i) {
            b = s->blocks[i];
            pthread_mutex_lock(&b->mutex);

            /* Look through all clients on that block. */
            TAILQ_FOREACH(it, b->clients, qentry) {
                /* Look to see if they match the search. */
                if(!it->pl) {
                    continue;
                }
                else if(!it->cur_lobby) {
                    continue;
                }
                else if(it->pl->level < minlvl || it->pl->level > maxlvl) {
                    continue;
                }
                else if(cl != 0 && it->pl->ch_class != cl - 1) {
                    continue;
                }
                else if(it == c) {
                    continue;
                }

                /* If we get here, they match the search. Fill in the entry. */
                memset(&pkt->entries[entries], 0, 0xD4);
                pkt->entries[entries].guildcard = LE32(it->guildcard);

                strcpy(pkt->entries[entries].name, it->pl->name);
                sprintf(pkt->entries[entries].cl_lvl, "%s Lvl %d\n",
                        classes[it->pl->ch_class], it->pl->level + 1);
                sprintf(pkt->entries[entries].location, "%s,BLOCK%02d,%s",
                        it->cur_lobby->name, it->cur_block->b, s->cfg->name);

                pkt->entries[entries].ip = a;
                pkt->entries[entries].port = LE16(b->dc_port);
                pkt->entries[entries].menu_id = LE32(0xFFFFFFFF);
                pkt->entries[entries].item_id = LE32(it->cur_lobby->lobby_id);

                len += 0xD4;
                ++entries;
            }

            pthread_mutex_unlock(&b->mutex);
        }
    }

    /* Put in a blank entry if nothing's there, otherwise PSO will crash. */
    if(entries == 0) {
        memset(&pkt->entries[0], 0, 0xD4);
        len += 0xD4;
    }

    /* Fill in the header. */
    pkt->hdr.pkt_type = SHIP_CHOICE_REPLY_TYPE;
    pkt->hdr.pkt_len = LE16(len);
    pkt->hdr.flags = entries;

    return crypt_send(c, len, sendbuf);
}

static int send_pc_choice_reply(ship_client_t *c, dc_choice_set_t *search,
                                int minlvl, int maxlvl, int cl, in_addr_t a) {
    uint8_t *sendbuf = get_sendbuf();
    pc_choice_reply_t *pkt = (pc_choice_reply_t *)sendbuf;
    uint16_t len = 4;
    uint8_t entries = 0;
    int i, j;
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;
    ship_t *s;
    block_t *b;
    ship_client_t *it;
    char tmp[64];

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Set up the converting stuff. */
    ic = iconv_open("UTF-16LE", "SHIFT_JIS");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Search any local ships. */
    for(j = 0; j < cfg->ship_count; ++j) {
        s = ships[j];
        for(i = 0; i < s->cfg->blocks; ++i) {
            b = s->blocks[i];
            pthread_mutex_lock(&b->mutex);

            /* Look through all clients on that block. */
            TAILQ_FOREACH(it, b->clients, qentry) {
                /* Look to see if they match the search. */
                if(!it->pl) {
                    continue;
                }
                else if(!it->cur_lobby) {
                    continue;
                }
                else if(it->pl->level < minlvl || it->pl->level > maxlvl) {
                    continue;
                }
                else if(cl != 0 && it->pl->ch_class != cl - 1) {
                    continue;
                }
                else if(it == c) {
                    continue;
                }

                /* If we get here, they match the search. Fill in the entry. */
                memset(&pkt->entries[entries], 0, 0x154);
                pkt->entries[entries].guildcard = LE32(it->guildcard);

                in = strlen(it->pl->name) + 1;
                out = 0x20;
                inptr = it->pl->name;
                outptr = (char *)pkt->entries[entries].name;
                iconv(ic, &inptr, &in, &outptr, &out);

                in = sprintf(tmp, "%s Lvl %d\n", classes[it->pl->ch_class],
                             it->pl->level + 1) + 1;
                out = 0x40;
                inptr = tmp;
                outptr = (char *)pkt->entries[entries].cl_lvl;
                iconv(ic, &inptr, &in, &outptr, &out);

                in = sprintf(tmp, "%s,BLOCK%02d,%s", it->cur_lobby->name,
                             it->cur_block->b, s->cfg->name) + 1;
                out = 0x60;
                inptr = tmp;
                outptr = (char *)pkt->entries[entries].location;
                iconv(ic, &inptr, &in, &outptr, &out);

                pkt->entries[entries].ip = a;
                pkt->entries[entries].port = LE16(b->pc_port);
                pkt->entries[entries].menu_id = LE32(0xFFFFFFFF);
                pkt->entries[entries].item_id = LE32(it->cur_lobby->lobby_id);

                len += 0x154;
                ++entries;
            }

            pthread_mutex_unlock(&b->mutex);
        }
    }

    /* Put in a blank entry if nothing's there, otherwise PSO will crash. */
    if(entries == 0) {
        memset(&pkt->entries[0], 0, 0x154);
        len += 0x154;
    }

    iconv_close(ic);

    /* Fill in the header. */
    pkt->hdr.pkt_type = SHIP_CHOICE_REPLY_TYPE;
    pkt->hdr.pkt_len = LE16(len);
    pkt->hdr.flags = entries;

    return crypt_send(c, len, sendbuf);
}

int send_choice_reply(ship_client_t *c, dc_choice_set_t *search) {
    int minlvl = 0, maxlvl = 199, cl;
    in_addr_t addr;

    /* Figure out the IP address to send. */
    if(netmask && (c->addr & netmask) == (local_addr & netmask)) {
        addr = local_addr;
    }
    else {
        addr = c->cur_ship->cfg->ship_ip;
    }

    /* Parse the packet for the minimum and maximum levels. */
    switch(LE16(search->entries[0].item_id)) {
        case 0x0001:
            minlvl = c->pl->level - 5;
            maxlvl = c->pl->level + 5;
            break;

        case 0x0002:
            minlvl = 0;
            maxlvl = 9;
            break;

        case 0x0003:
            minlvl = 10;
            maxlvl = 19;
            break;

        case 0x0004:
            minlvl = 20;
            maxlvl = 39;
            break;

        case 0x0005:
            minlvl = 40;
            maxlvl = 59;
            break;

        case 0x0006:
            minlvl = 60;
            maxlvl = 79;
            break;

        case 0x0007:
            minlvl = 80;
            maxlvl = 99;
            break;

        case 0x0008:
            minlvl = 100;
            maxlvl = 119;
            break;

        case 0x0009:
            minlvl = 120;
            maxlvl = 159;
            break;

        case 0x000A:
            minlvl = 160;
            maxlvl = 199;
            break;
    }

    cl = LE16(search->entries[1].item_id);

    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_choice_reply(c, search, minlvl, maxlvl, cl, addr);

        case CLIENT_VERSION_PC:
            return send_pc_choice_reply(c, search, minlvl, maxlvl, cl, addr);
    }

    return -1;
}

static int send_pc_simple_mail_dc(ship_client_t *c, dc_simple_mail_pkt *p) {
    uint8_t *sendbuf = get_sendbuf();
    pc_simple_mail_pkt *pkt = (pc_simple_mail_pkt *)sendbuf;
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;
    int i;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Set up the converting stuff. */
    ic = iconv_open("UTF-16LE", "SHIFT_JIS");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Scrub the buffer. */
    memset(pkt, 0, SHIP_PC_SIMPLE_MAIL_LENGTH);

    /* Fill in the header. */
    pkt->hdr.pkt_type = SHIP_SIMPLE_MAIL_TYPE;
    pkt->hdr.flags = 0;
    pkt->hdr.pkt_len = LE16(SHIP_PC_SIMPLE_MAIL_LENGTH);

    /* Copy everything that doesn't need to be converted. */
    pkt->tag = p->tag;
    pkt->gc_sender = p->gc_sender;
    pkt->gc_dest = p->gc_dest;

    /* Convert the name. */
    in = 0x10;
    out = 0x20;
    inptr = p->name;
    outptr = (char *)pkt->name;
    iconv(ic, &inptr, &in, &outptr, &out);

    /* Convert the first instance of text. */
    in = 0x90;
    out = 0x120;
    inptr = p->stuff;
    outptr = pkt->stuff;
    iconv(ic, &inptr, &in, &outptr, &out);

    /* This is a BIT hackish (just a bit...). */
    for(i = 0; i < 0x150; ++i) {
        pkt->stuff[(i << 1) + 0x150] = p->stuff[i + 0xB0];
        pkt->stuff[(i << 1) + 0x151] = 0;
    }

    iconv_close(ic);

    /* Send it away. */
    return crypt_send(c, SHIP_PC_SIMPLE_MAIL_LENGTH, sendbuf);
}

static int send_dc_simple_mail_pc(ship_client_t *c, pc_simple_mail_pkt *p) {
    uint8_t *sendbuf = get_sendbuf();
    dc_simple_mail_pkt *pkt = (dc_simple_mail_pkt *)sendbuf;
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;
    int i;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Set up the converting stuff. */
    ic = iconv_open("SHIFT_JIS", "UTF-16LE");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Scrub the buffer. */
    memset(pkt, 0, SHIP_DC_SIMPLE_MAIL_LENGTH);

    /* Fill in the header. */
    pkt->hdr.pkt_type = SHIP_SIMPLE_MAIL_TYPE;
    pkt->hdr.flags = 0;
    pkt->hdr.pkt_len = LE16(SHIP_DC_SIMPLE_MAIL_LENGTH);

    /* Copy everything that doesn't need to be converted. */
    pkt->tag = p->tag;
    pkt->gc_sender = p->gc_sender;
    pkt->gc_dest = p->gc_dest;

    /* Convert the name. */
    in = 0x20;
    out = 0x10;
    inptr = (char *)p->name;
    outptr = pkt->name;
    iconv(ic, &inptr, &in, &outptr, &out);

    /* Convert the first instance of text. */
    in = 0x120;
    out = 0x90;
    inptr = p->stuff;
    outptr = pkt->stuff;
    iconv(ic, &inptr, &in, &outptr, &out);

    /* This is a BIT hackish (just a bit...). */
    for(i = 0; i < 0x150; ++i) {
        pkt->stuff[i + 0xB0] = p->stuff[(i << 1) + 0x150];
    }

    iconv_close(ic);

    /* Send it away. */
    return crypt_send(c, SHIP_DC_SIMPLE_MAIL_LENGTH, sendbuf);
}

/* Send a simple mail packet, doing any needed transformations. */
int send_simple_mail(int version, ship_client_t *c, dc_pkt_hdr_t *pkt) {
    switch(version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            if(c->version == CLIENT_VERSION_PC) {
                return send_pc_simple_mail_dc(c, (dc_simple_mail_pkt *)pkt);
            }
            else {
                return send_pkt_dc(c, pkt);
            }
            break;

        case CLIENT_VERSION_PC:
            if(c->version == CLIENT_VERSION_PC) {
                return send_pkt_dc(c, pkt);
            }
            else {
                return send_dc_simple_mail_pc(c, (pc_simple_mail_pkt *)pkt);
            }
            break;
    }

    return -1;
}
