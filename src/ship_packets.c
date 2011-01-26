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
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <iconv.h>
#include <limits.h>
#include <stdarg.h>
#include <ctype.h>

#include <sylverant/encryption.h>
#include <sylverant/database.h>
#include <sylverant/debug.h>

#include "ship_packets.h"
#include "utils.h"
#include "subcmd.h"
#include "quests.h"

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

/* The list of type codes for the quest directories. */
static const char type_codes[][3] = {
    "v1", "v2", "pc", "gc"
};

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

    return 0;
}

/* Encrypt and send a packet away. */
static int crypt_send(ship_client_t *c, int len, uint8_t *sendbuf) {
    /* Expand it to be a multiple of 8/4 bytes long */
    while(len & (c->hdr_size - 1)) {
        sendbuf[len++] = 0;
    }

    /* If we're logging the client, write into the log */
    if(c->logfile) {
        fprint_packet(c->logfile, sendbuf, len, 0);
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
    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        pkt->hdr.dc.pkt_len = LE16(DC_WELCOME_LENGTH);
        pkt->hdr.dc.pkt_type = WELCOME_TYPE;
    }
    else {
        pkt->hdr.pc.pkt_len = LE16(DC_WELCOME_LENGTH);
        pkt->hdr.pc.pkt_type = WELCOME_TYPE;
    }

    /* Fill in the required message */
    memcpy(pkt->copyright, dc_welcome_copyright, 56);

    /* Fill in the encryption vectors */
    pkt->svect = LE32(svect);
    pkt->cvect = LE32(cvect);

    /* Send the packet away */
    return send_raw(c, DC_WELCOME_LENGTH, sendbuf);
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
    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        pkt->hdr.dc.pkt_type = SECURITY_TYPE;
        pkt->hdr.dc.pkt_len = LE16((0x0C + data_len));
    }
    else {
        pkt->hdr.pc.pkt_type = SECURITY_TYPE;
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
    memset(pkt, 0, DC_REDIRECT_LENGTH);

    /* Fill in the header */
    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        pkt->hdr.dc.pkt_type = REDIRECT_TYPE;
        pkt->hdr.dc.pkt_len = LE16(DC_REDIRECT_LENGTH);
    }
    else {
        pkt->hdr.pc.pkt_type = REDIRECT_TYPE;
        pkt->hdr.pc.pkt_len = LE16(DC_REDIRECT_LENGTH);
    }

    /* Fill in the IP and port */
    pkt->ip_addr = ip;
    pkt->port = LE16(port);

    /* Send the packet away */
    return crypt_send(c, DC_REDIRECT_LENGTH, sendbuf);
}

int send_redirect(ship_client_t *c, in_addr_t ip, uint16_t port) {
    /* Call the appropriate function */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
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
    memset(pkt, 0, DC_TIMESTAMP_LENGTH);

    /* Fill in the header */
    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        pkt->hdr.dc.pkt_type = TIMESTAMP_TYPE;
        pkt->hdr.dc.pkt_len = LE16(DC_TIMESTAMP_LENGTH);
    }
    else {
        pkt->hdr.pc.pkt_type = TIMESTAMP_TYPE;
        pkt->hdr.pc.pkt_len = LE16(DC_TIMESTAMP_LENGTH);
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
    return crypt_send(c, DC_TIMESTAMP_LENGTH, sendbuf);
}

int send_timestamp(ship_client_t *c) {
    /* Call the appropriate function */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            return send_dc_timestamp(c);
    }
    
    return -1;
}

/* Send the list of blocks to the client. */
static int send_dc_block_list(ship_client_t *c, ship_t *s) {
    uint8_t *sendbuf = get_sendbuf();
    dc_block_list_pkt *pkt = (dc_block_list_pkt *)sendbuf;
    char tmp[18];
    int i, len = 0x20, entries = 1;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Clear the base packet */
    memset(pkt, 0, sizeof(dc_block_list_pkt));

    /* Fill in some basic stuff */
    pkt->hdr.pkt_type = BLOCK_LIST_TYPE;

    /* Fill in the ship name entry */
    memset(&pkt->entries[0], 0, 0x1C);
    pkt->entries[0].menu_id = LE32(0x00040000);
    pkt->entries[0].item_id = 0;
    pkt->entries[0].flags = 0;

    /* Copy the ship's name to the packet. The ship names are forced to be
       ASCII, and I believe this part is in ISO-8859-1 (which is the same thing
       for all ASCII characters (< 0x80)). */
    strncpy(pkt->entries[0].name, s->cfg->name, 0x10);

    /* Add what's needed at the end */
    pkt->entries[0].name[0x0F] = 0x00;
    pkt->entries[0].name[0x10] = 0x08;
    pkt->entries[0].name[0x11] = 0x00;

    /* Add each block to the list. */
    for(i = 1; i <= s->cfg->blocks; ++i) {
        if(s->blocks[i - 1] && s->blocks[i - 1]->run) {
            /* Clear out the block information */
            memset(&pkt->entries[entries], 0, 0x1C);

            /* Fill in what we have */
            pkt->entries[entries].menu_id = LE32(0x00000001);
            pkt->entries[entries].item_id = LE32(i);
            pkt->entries[entries].flags = LE16(0x0000);

            /* Create the name string */
            sprintf(tmp, "BLOCK%02d", i);
            strncpy(pkt->entries[entries].name, tmp, 0x11);
            pkt->entries[entries].name[0x11] = 0;

            len += 0x1C;
            ++entries;
        }
    }

    /* Add the "Ship Select" entry */
    memset(&pkt->entries[entries], 0, 0x1C);

    /* Fill in what we have */
    pkt->entries[entries].menu_id = LE32(0x00000001);
    pkt->entries[entries].item_id = LE32(0xFFFFFFFF);
    pkt->entries[entries].flags = LE16(0x0000);

    /* Create the name string */
    strncpy(pkt->entries[entries].name, "Ship Select", 0x11);
    pkt->entries[entries].name[0x11] = 0;

    len += 0x1C;

    /* Fill in the rest of the header */
    pkt->hdr.pkt_len = LE16(len);
    pkt->hdr.flags = (uint8_t)(entries);

    /* Send the packet away */
    return crypt_send(c, len, sendbuf);
}

static int send_pc_block_list(ship_client_t *c, ship_t *s) {
    uint8_t *sendbuf = get_sendbuf();
    pc_block_list_pkt *pkt = (pc_block_list_pkt *)sendbuf;
    char tmp[18];
    int i, j, len = 0x30, entries = 1;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Clear the base packet */
    memset(pkt, 0, sizeof(pc_block_list_pkt));

    /* Fill in some basic stuff */
    pkt->hdr.pkt_type = BLOCK_LIST_TYPE;

    /* Fill in the ship name entry */
    memset(&pkt->entries[0], 0, 0x2C);
    pkt->entries[0].menu_id = LE32(0x00040000);
    pkt->entries[0].item_id = 0;
    pkt->entries[0].flags = 0;

    /* Copy the ship's name to the packet. The ship name is always ASCII, so
       this stupid conversion to UTF-16LE works.*/
    for(i = 0; i < 0x10 && s->cfg->name[i]; ++i) {
        pkt->entries[0].name[i] = LE16(s->cfg->name[i]);
    }

    /* Add each block to the list. */
    for(i = 1; i <= s->cfg->blocks; ++i) {
        if(s->blocks[i - 1] && s->blocks[i - 1]->run) {
            /* Clear out the block information */
            memset(&pkt->entries[entries], 0, 0x2C);

            /* Fill in what we have */
            pkt->entries[entries].menu_id = LE32(0x00000001);
            pkt->entries[entries].item_id = LE32(i);
            pkt->entries[entries].flags = LE16(0x0000);

            /* Create the name string */
            sprintf(tmp, "BLOCK%02d", i);

            /* This works here since the block name is always ASCII. */
            for(j = 0; j < 0x10 && tmp[j]; ++j) {
                pkt->entries[entries].name[j] = LE16(tmp[j]);
            }

            len += 0x2C;
            ++entries;
        }
    }

    /* Add the "Ship Select" entry */
    memset(&pkt->entries[entries], 0, 0x2C);

    /* Fill in what we have */
    pkt->entries[entries].menu_id = LE32(0x00000001);
    pkt->entries[entries].item_id = LE32(0xFFFFFFFF);
    pkt->entries[entries].flags = LE16(0x0000);

    /* Create the name string */
    sprintf(tmp, "Ship Select");

    /* This works here since its ASCII */
    for(j = 0; j < 0x10 && tmp[j]; ++j) {
        pkt->entries[entries].name[j] = LE16(tmp[j]);
    }

    len += 0x2C;

    /* Fill in the rest of the header */
    pkt->hdr.pkt_len = LE16(len);
    pkt->hdr.flags = (uint8_t)(entries);

    /* Send the packet away */
    return crypt_send(c, len, sendbuf);
}

int send_block_list(ship_client_t *c, ship_t *s) {
    /* Call the appropriate function */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            return send_dc_block_list(c, s);

        case CLIENT_VERSION_PC:
            return send_pc_block_list(c, s);
    }

    return -1;
}

/* Send a block/ship information reply packet to the client. */
static int send_dc_info_reply(ship_client_t *c, const char *msg) {
    uint8_t *sendbuf = get_sendbuf();
    dc_info_reply_pkt *pkt = (dc_info_reply_pkt *)sendbuf;
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        if(msg[1] == 'J') {
            ic = iconv_open("SHIFT_JIS", "UTF-8");
        }
        else {
            ic = iconv_open("ISO-8859-1", "UTF-8");
        }
    }
    else {
        ic = iconv_open("UTF-16LE", "UTF-8");
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
    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        pkt->hdr.dc.pkt_type = INFO_REPLY_TYPE;
        pkt->hdr.dc.flags = 0;
        pkt->hdr.dc.pkt_len = LE16(out);
    }
    else {
        pkt->hdr.pc.pkt_type = INFO_REPLY_TYPE;
        pkt->hdr.pc.flags = 0;
        pkt->hdr.pc.pkt_len = LE16(out);
    }

    /* Send the packet away */
    return crypt_send(c, out, sendbuf);
}

int send_info_reply(ship_client_t *c, const char *msg) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
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
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
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
    uint32_t i, max = 15;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the header */
    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC) {
        pkt->hdr.dc.pkt_type = LOBBY_LIST_TYPE;
        pkt->hdr.dc.flags = 0x0F;                               /* 15 lobbies */
        pkt->hdr.dc.pkt_len = LE16(DC_LOBBY_LIST_LENGTH);
    }
    else if(c->version == CLIENT_VERSION_EP3) {
        pkt->hdr.dc.pkt_type = LOBBY_LIST_TYPE;
        pkt->hdr.dc.flags = 0x14;                               /* 20 lobbies */
        pkt->hdr.dc.pkt_len = LE16(EP3_LOBBY_LIST_LENGTH);
        max = 20;
    }
    else {
        pkt->hdr.pc.pkt_type = LOBBY_LIST_TYPE;
        pkt->hdr.pc.flags = 0x0F;                               /* 15 lobbies */
        pkt->hdr.pc.pkt_len = LE16(DC_LOBBY_LIST_LENGTH);
    }

    /* Fill in the lobbies. */
    for(i = 0; i < max; ++i) {
        pkt->entries[i].menu_id = 0xFFFFFFFF;
        pkt->entries[i].item_id = LE32((i + 1));
        pkt->entries[i].padding = 0;
    }

    /* There's padding at the end -- enough for one more lobby. */
    pkt->entries[max].menu_id = 0;
    pkt->entries[max].item_id = 0;
    pkt->entries[max].padding = 0;

    if(c->version != CLIENT_VERSION_EP3) {
        return crypt_send(c, DC_LOBBY_LIST_LENGTH, sendbuf);
    }
    else {
        return crypt_send(c, EP3_LOBBY_LIST_LENGTH, sendbuf);
    }
}

int send_lobby_list(ship_client_t *c) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
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
    uint8_t event = l->event;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Clear the packet's header. */
    memset(pkt, 0, sizeof(dc_lobby_join_pkt));

    /* Don't send invalid event codes to the Dreamcast version. */
    if(c->version < CLIENT_VERSION_GC && event > 6) {
        event = 0;
    }

    /* Fill in the basics. */
    pkt->hdr.pkt_type = LOBBY_JOIN_TYPE;
    pkt->leader_id = l->leader_id;
    pkt->one = 1;
    pkt->lobby_num = l->lobby_id - 1;
    pkt->block_num = LE16(l->block->b);
    pkt->event = LE16(event);

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
        pkt->entries[pls].hdr.ip_addr = l->clients[i]->addr;
        pkt->entries[pls].hdr.client_id = LE32(i);

        /* No need to iconv... the encoding is already right */
        memcpy(pkt->entries[pls].hdr.name, l->clients[i]->pl->v1.name, 16);
        memcpy(&pkt->entries[pls].data, &l->clients[i]->pl->v1,
               sizeof(v1_player_t));

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
    ICONV_CONST char *inptr;
    char *outptr;
    uint8_t event = l->event;
    
    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Names are always ISO-8859-1 for all versions of PSO that are supported */
    ic = iconv_open("UTF-16LE", "ISO-8859-1");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Don't send invalid event codes to the PC version. */
    if(event > 6) {
        event = 0;
    }

    /* Clear the packet's header. */
    memset(pkt, 0, sizeof(pc_lobby_join_pkt));

    /* Fill in the basics. */
    pkt->hdr.pkt_type = LOBBY_JOIN_TYPE;
    pkt->leader_id = l->leader_id;
    pkt->one = 1;
    pkt->lobby_num = l->lobby_id - 1;
    pkt->block_num = LE16(l->block->b);
    pkt->event = LE16(l->event);

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
        pkt->entries[pls].hdr.ip_addr = l->clients[i]->addr;
        pkt->entries[pls].hdr.client_id = LE32(i);

        /* Convert the name to UTF-16. */
        in = strlen(l->clients[i]->pl->v1.name) + 1;
        out = 32;
        inptr = l->clients[i]->pl->v1.name;
        outptr = (char *)pkt->entries[pls].hdr.name;
        iconv(ic, &inptr, &in, &outptr, &out);

        memcpy(&pkt->entries[pls].data, &l->clients[i]->pl->v1,
               sizeof(v1_player_t));

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
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            if(send_dc_lobby_join(c, l)) {
                return -1;
            }

            if(l->type == LOBBY_TYPE_DEFAULT && send_lobby_c_rank(c, l)) {
                return -1;
            }

            return send_dc_lobby_arrows(l, c);

        case CLIENT_VERSION_PC:
            if(send_pc_lobby_join(c, l)) {
                return -1;
            }

            if(l->type == LOBBY_TYPE_DEFAULT && send_lobby_c_rank(c, l)) {
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
    pkt->hdr.pkt_type = (l->type == LOBBY_TYPE_DEFAULT) ? 
        LOBBY_ADD_PLAYER_TYPE : GAME_ADD_PLAYER_TYPE;
    pkt->hdr.flags = 1;
    pkt->hdr.pkt_len = LE16(0x044C);
    pkt->client_id = c->client_id;
    pkt->leader_id = l->leader_id;
    pkt->one = 1;
    pkt->lobby_num = (l->type == LOBBY_TYPE_DEFAULT) ? l->lobby_id - 1 : 0xFF;

    if(l->type == LOBBY_TYPE_DEFAULT) {
        pkt->block_num = LE16(l->block->b);
    }
    else {
        pkt->block_num = LE16(0x0001);
        pkt->event = LE16(0x0001);
    }

    /* Copy the player's data into the packet. */
    pkt->entries[0].hdr.tag = LE32(0x00010000);
    pkt->entries[0].hdr.guildcard = LE32(nc->guildcard);
    pkt->entries[0].hdr.ip_addr = nc->addr;
    pkt->entries[0].hdr.client_id = LE32(nc->client_id);

    /* No need to iconv, the encoding is already right */
    memcpy(pkt->entries[0].hdr.name, nc->pl->v1.name, 16);
    memcpy(&pkt->entries[0].data, &nc->pl->v1, sizeof(v1_player_t));

    /* Send it away */
    return crypt_send(c, 0x044C, sendbuf);
}

static int send_pc_lobby_add_player(lobby_t *l, ship_client_t *c,
                                    ship_client_t *nc) {
    uint8_t *sendbuf = get_sendbuf();
    pc_lobby_join_pkt *pkt = (pc_lobby_join_pkt *)sendbuf;
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;
    
    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Names stored in character data are ISO-8859-1 */
    ic = iconv_open("UTF-16LE", "ISO-8859-1");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear the packet's header. */
    memset(pkt, 0, sizeof(pc_lobby_join_pkt));

    /* Fill in the basics. */
    pkt->hdr.pkt_type = (l->type == LOBBY_TYPE_DEFAULT) ? 
        LOBBY_ADD_PLAYER_TYPE : GAME_ADD_PLAYER_TYPE;
    pkt->hdr.flags = 1;
    pkt->hdr.pkt_len = LE16(0x045C);
    pkt->client_id = c->client_id;
    pkt->leader_id = l->leader_id;
    pkt->one = 1;
    pkt->lobby_num = (l->type == LOBBY_TYPE_DEFAULT) ? l->lobby_id - 1 : 0xFF;

    if(l->type == LOBBY_TYPE_DEFAULT) {
        pkt->block_num = LE16(l->block->b);
    }
    else {
        pkt->block_num = LE16(0x0001);
        pkt->event = LE16(0x0001);
    }

    /* Copy the player's data into the packet. */
    pkt->entries[0].hdr.tag = LE32(0x00010000);
    pkt->entries[0].hdr.guildcard = LE32(nc->guildcard);
    pkt->entries[0].hdr.ip_addr = nc->addr;
    pkt->entries[0].hdr.client_id = LE32(nc->client_id);

    /* Convert the name to UTF-16. */
    in = strlen(nc->pl->v1.name) + 1;
    out = 32;
    inptr = nc->pl->v1.name;
    outptr = (char *)pkt->entries[0].hdr.name;
    iconv(ic, &inptr, &in, &outptr, &out);

    memcpy(&pkt->entries[0].data, &nc->pl->v1, sizeof(v1_player_t));

    /* Send it away */
    return crypt_send(c, 0x045C, sendbuf);
}

/* Send a packet to all clients in the lobby when a new player joins. */
int send_lobby_add_player(lobby_t *l, ship_client_t *c) {
    int i;

    /* Send the C-Rank of this new character. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        send_c_rank_update(c, l);
    }

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL && l->clients[i] != c) {
            pthread_mutex_lock(&l->clients[i]->mutex);

            /* Call the appropriate function. */
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_EP3:
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
    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        pkt->hdr.dc.pkt_type = (l->type == LOBBY_TYPE_DEFAULT) ?
            LOBBY_LEAVE_TYPE : GAME_LEAVE_TYPE;
        pkt->hdr.dc.flags = client_id;
        pkt->hdr.dc.pkt_len = LE16(DC_LOBBY_LEAVE_LENGTH);
    }
    else {
        pkt->hdr.pc.pkt_type = (l->type == LOBBY_TYPE_DEFAULT) ?
            LOBBY_LEAVE_TYPE : GAME_LEAVE_TYPE;
        pkt->hdr.pc.flags = client_id;
        pkt->hdr.pc.pkt_len = LE16(DC_LOBBY_LEAVE_LENGTH);
    }

    pkt->client_id = client_id;
    pkt->leader_id = l->leader_id;
    pkt->padding = LE16(0x0001);    /* ????: Not padding, apparently? */

    /* Send it away */
    return crypt_send(c, DC_LOBBY_LEAVE_LENGTH, sendbuf);
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
                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_EP3:
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
    ICONV_CONST char *inptr;
    char *outptr;

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        /* Yes, these are both dummy transformations */
        if(msg[1] == 'J') {
            ic = iconv_open("SHIFT_JIS", "SHIFT_JIS");
        }
        else {
            ic = iconv_open("ISO-8859-1", "ISO-8859-1");
        }
    }
    else {
        if(msg[1] == 'J') {
            ic = iconv_open("UTF-16LE", "SHIFT_JIS");
        }
        else {
            ic = iconv_open("UTF-16LE", "ISO-8859-1");
        }
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
    in = sprintf(tm, "%s\t%s", s->pl->v1.name, msg) + 1;

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

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        pkt->hdr.dc.pkt_type = CHAT_TYPE;
        pkt->hdr.dc.flags = 0;
        pkt->hdr.dc.pkt_len = LE16(len);
    }
    else {
        pkt->hdr.pc.pkt_type = CHAT_TYPE;
        pkt->hdr.pc.flags = 0;
        pkt->hdr.pc.pkt_len = LE16(len);
    }

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

/* Send a talk packet to the specified lobby. */
int send_lobby_chat(lobby_t *l, ship_client_t *sender, char msg[]) {
    int i;

    if((sender->flags & CLIENT_FLAG_STFU)) {
        return send_dc_lobby_chat(l, sender, sender, msg);
    }

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL) {
            pthread_mutex_lock(&l->clients[i]->mutex);

            /* Call the appropriate function. */
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                case CLIENT_VERSION_PC:
                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_EP3:
                    /* Only send if they're not being /ignore'd */
                    if(!client_has_ignored(l->clients[i], sender->guildcard)) {
                        send_dc_lobby_chat(l, l->clients[i], sender, msg);
                    }
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
    char tmp[32];
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Create everything we need for converting stuff. */
    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        if(LE16(msg[1]) == ((uint16_t)'J')) {
            ic = iconv_open("SHIFT_JIS", "UTF-16LE");
        }
        else {
            ic = iconv_open("ISO-8859-1", "UTF-16LE");
        }

        if(ic == (iconv_t)-1) {
            perror("iconv_open");
            return -1;
        }

        ic2 = iconv_open("ISO-8859-1", "ISO-8859-1");
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

        ic2 = iconv_open("UTF-16LE", "ISO-8859-1");
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
    in = sprintf(tmp, "%s\t", s->pl->v1.name);
    out = 65520;
    inptr = tmp;
    outptr = pkt->msg;
    iconv(ic2, &inptr, &in, &outptr, &out);
    iconv_close(ic2);

    /* Fill in the message */
    in = len;
    inptr = (char *)msg;

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

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        pkt->hdr.dc.pkt_type = CHAT_TYPE;
        pkt->hdr.dc.flags = 0;
        pkt->hdr.dc.pkt_len = LE16(len);
    }
    else {
        pkt->hdr.pc.pkt_type = CHAT_TYPE;
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

    if((sender->flags & CLIENT_FLAG_STFU)) {
        return send_dc_lobby_wchat(l, sender, sender, msg, len);
    }    

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL) {
            pthread_mutex_lock(&l->clients[i]->mutex);

            /* Call the appropriate function. */
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                case CLIENT_VERSION_PC:
                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_EP3:
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
    memset(pkt, 0, DC_GUILD_REPLY_LENGTH);

    /* Adjust the port properly... */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            break;

        case CLIENT_VERSION_PC:
            ++port;
            break;

        case CLIENT_VERSION_GC:
            port += 2;
            break;

        case CLIENT_VERSION_EP3:
            port += 3;
            break;
    }

    /* Fill in the simple stuff */
    pkt->hdr.pkt_type = GUILD_REPLY_TYPE;
    pkt->hdr.pkt_len = LE16(DC_GUILD_REPLY_LENGTH);
    pkt->tag = LE32(0x00010000);
    pkt->gc_search = LE32(c->guildcard);
    pkt->gc_target = LE32(gc);
    pkt->ip = ip;
    pkt->port = LE16(port);
    pkt->menu_id = LE32(0xFFFFFFFF);
    pkt->item_id = LE32(lobby);

    /* No need to iconv, we're not doing anything fancy here */
    strcpy(pkt->name, name);

    /* Fill in the location string. Everything here is ASCII, so this is safe */
    sprintf(pkt->location, "%s,BLOCK%02d,%s", game, block, ship);

    /* Send it away */
    return crypt_send(c, DC_GUILD_REPLY_LENGTH, sendbuf);
}

static int send_pc_guild_reply(ship_client_t *c, uint32_t gc, in_addr_t ip,
                               uint16_t port, char game[], int block,
                               char ship[], uint32_t lobby, char name[]) {
    uint8_t *sendbuf = get_sendbuf();
    pc_guild_reply_pkt *pkt = (pc_guild_reply_pkt *)sendbuf;
    char tmp[0x44];
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* We'll be converting stuff from ISO-8859-1 to UTF-16. */
    ic = iconv_open("UTF-16LE", "ISO-8859-1");

    if(ic == (iconv_t)-1) {
        return -1;
    }

    /* Adjust the port properly... */
    ++port;

    /* Clear it out first */
    memset(pkt, 0, PC_GUILD_REPLY_LENGTH);

    /* Fill in the simple stuff */
    pkt->hdr.pkt_type = GUILD_REPLY_TYPE;
    pkt->hdr.pkt_len = LE16(PC_GUILD_REPLY_LENGTH);
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
    return crypt_send(c, PC_GUILD_REPLY_LENGTH, sendbuf);
}

int send_guild_reply(ship_client_t *c, uint32_t gc, in_addr_t ip, uint16_t port,
                     char game[], int block, char ship[], uint32_t lobby,
                     char name[]) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            return send_dc_guild_reply(c, gc, ip, port, game, block, ship,
                                       lobby, name);

        case CLIENT_VERSION_PC:
            return send_pc_guild_reply(c, gc, ip, port, game, block, ship,
                                       lobby, name);
    }
    
    return -1;
}

/* Send a premade guild card search reply to the specified client. */
static int send_dc_guild_reply_sg(ship_client_t *c, dc_guild_reply_pkt *pkt) {
    uint16_t port = LE16(pkt->port);

    /* Adjust the port properly... */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            break;

        case CLIENT_VERSION_GC:
            pkt->port = LE16(port + 2);
            break;

        case CLIENT_VERSION_EP3:
            pkt->port += LE16(port + 3);
            break;
    }

    /* Send it away */
    return crypt_send(c, DC_GUILD_REPLY_LENGTH, (uint8_t *)pkt);
}

static int send_pc_guild_reply_sg(ship_client_t *c, dc_guild_reply_pkt *dc) {
    uint8_t *sendbuf = get_sendbuf();
    pc_guild_reply_pkt *pkt = (pc_guild_reply_pkt *)sendbuf;
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;
    uint16_t port = LE16(dc->port);

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* We'll be converting stuff from ISO-8859-1 to UTF-16. */
    ic = iconv_open("UTF-16LE", "ISO-8859-1");

    if(ic == (iconv_t)-1) {
        return -1;
    }

    /* Adjust the port properly... */
    ++port;

    /* Clear it out first */
    memset(pkt, 0, PC_GUILD_REPLY_LENGTH);

    /* Fill in the simple stuff */
    pkt->hdr.pkt_type = GUILD_REPLY_TYPE;
    pkt->hdr.pkt_len = LE16(PC_GUILD_REPLY_LENGTH);
    pkt->tag = LE32(0x00010000);
    pkt->gc_search = dc->gc_search;
    pkt->gc_target = dc->gc_target;
    pkt->ip = dc->ip;
    pkt->port = LE16(port);
    pkt->menu_id = dc->menu_id;
    pkt->item_id = dc->item_id;

    /* Fill in the location string... */
    in = strlen(dc->location) + 1;
    out = 0x88;
    inptr = dc->location;
    outptr = (char *)pkt->location;
    iconv(ic, &inptr, &in, &outptr, &out);

    /* ...and the name. */
    in = strlen(dc->name) + 1;
    out = 0x40;
    inptr = dc->name;
    outptr = (char *)pkt->name;
    iconv(ic, &inptr, &in, &outptr, &out);

    iconv_close(ic);

    /* Send it away */
    return crypt_send(c, PC_GUILD_REPLY_LENGTH, sendbuf);
}

int send_guild_reply_sg(ship_client_t *c, dc_guild_reply_pkt *pkt) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            return send_dc_guild_reply_sg(c, pkt);

        case CLIENT_VERSION_PC:
            return send_pc_guild_reply_sg(c, pkt);
    }

    return -1;
}

static int send_dc_message(ship_client_t *c, uint16_t type, const char *fmt,
                           va_list args) {
    uint8_t *sendbuf = get_sendbuf();
    dc_chat_pkt *pkt = (dc_chat_pkt *)sendbuf;
    int len;
    iconv_t ic;
    char tm[512];
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Clear the packet header */
    memset(pkt, 0, sizeof(dc_chat_pkt));

    /* Do the formatting */
    vsnprintf(tm, 512, fmt, args);
    tm[511] = '\0';
    in = strlen(tm) + 1;

    /* Make sure we have a language code tag */
    if(tm[0] != '\t' || (tm[1] != 'E' && tm[1] != 'J')) {
        /* Assume Non-Japanese if we don't have a marker. */
        memmove(&tm[2], &tm[0], in);
        tm[0] = '\t';
        tm[1] = 'E';
        in += 2;
    }

    /* Set up to convert between encodings */
    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        if(tm[1] != 'J') {
            ic = iconv_open("ISO-8859-1", "UTF-8");
        }
        else {
            ic = iconv_open("SHIFT_JIS", "UTF-8");
        }
    }
    else {
        ic = iconv_open("UTF-16LE", "UTF-8");
    }

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    in = strlen(tm) + 1;

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

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
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
int send_message1(ship_client_t *c, const char *fmt, ...) {
    va_list args;
    int rv = -1;

    va_start(args, fmt);

    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            rv = send_dc_message(c, MSG1_TYPE, fmt, args);
    }

    va_end(args);
    
    return rv;
}

/* Send a text message to the client (i.e, for stuff related to commands). */
int send_txt(ship_client_t *c, const char *fmt, ...) {
    va_list args;
    int rv = -1;

    va_start(args, fmt);

    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            rv = send_dc_message(c, TEXT_MSG_TYPE, fmt, args);
    }

    va_end(args);

    return rv;
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
    memset(pkt, 0, DC_GAME_JOIN_LENGTH);

    /* Fill in the basics. */
    pkt->hdr.pkt_type = GAME_JOIN_TYPE;
    pkt->hdr.pkt_len = LE16(DC_GAME_JOIN_LENGTH);
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

            /* No need to iconv, the name is fine as is */
            memcpy(pkt->players[i].name, l->clients[i]->pl->v1.name, 16);
            ++clients;
        }
    }

    /* Copy the client count over. */
    pkt->hdr.flags = (uint8_t)clients;

    /* Send it away */
    return crypt_send(c, DC_GAME_JOIN_LENGTH, sendbuf);
}

static int send_pc_game_join(ship_client_t *c, lobby_t *l) {
    uint8_t *sendbuf = get_sendbuf();
    pc_game_join_pkt *pkt = (pc_game_join_pkt *)sendbuf;
    int clients = 0, i;
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Names are sent in the player packets in ISO-8859-1, so that's what we
       have sitting around */
    ic = iconv_open("UTF-16LE", "ISO-8859-1");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear it out first. */
    memset(pkt, 0, sizeof(pc_game_join_pkt));

    /* Fill in the basics. */
    pkt->hdr.pkt_type = GAME_JOIN_TYPE;
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
            in = strlen(l->clients[i]->pl->v1.name) + 1;
            out = 32;
            inptr = l->clients[i]->pl->v1.name;
            outptr = (char *)pkt->players[i].name;
            iconv(ic, &inptr, &in, &outptr, &out);
            ++clients;
        }
    }

    iconv_close(ic);

    /* Copy the client count over. */
    pkt->hdr.flags = (uint8_t)clients;

    /* Send it away */
    return crypt_send(c, sizeof(pc_game_join_pkt), sendbuf);
}

static int send_gc_game_join(ship_client_t *c, lobby_t *l) {
    uint8_t *sendbuf = get_sendbuf();
    gc_game_join_pkt *pkt = (gc_game_join_pkt *)sendbuf;
    int clients = 0, i;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Clear it out first. */
    memset(pkt, 0, GC_GAME_JOIN_LENGTH);

    /* Fill in the basics. */
    pkt->hdr.pkt_type = GAME_JOIN_TYPE;
    pkt->hdr.pkt_len = LE16(GC_GAME_JOIN_LENGTH);
    pkt->client_id = c->client_id;
    pkt->leader_id = l->leader_id;
    pkt->one = 1;
    pkt->difficulty = l->difficulty;
    pkt->battle = l->battle;
    pkt->event = l->event;
    pkt->section = l->section;
    pkt->challenge = l->challenge;
    pkt->rand_seed = LE32(l->rand_seed);
    pkt->episode = l->episode;
    pkt->one2 = 1;

    /* Fill in the variations array. */
    memcpy(pkt->maps, l->maps, 0x20 * 4);

    for(i = 0; i < 4; ++i) {
        if(l->clients[i]) {
            /* Copy the player's data into the packet. */
            pkt->players[i].tag = LE32(0x00010000);
            pkt->players[i].guildcard = LE32(l->clients[i]->guildcard);
            pkt->players[i].ip_addr = 0;
            pkt->players[i].client_id = LE32(i);

            /* No need to iconv the names, they'll be good as is */
            memcpy(pkt->players[i].name, l->clients[i]->pl->v1.name, 16);
            ++clients;
        }
    }

    /* Copy the client count over. */
    pkt->hdr.flags = (uint8_t)clients;

    /* Send it away */
    return crypt_send(c, GC_GAME_JOIN_LENGTH, sendbuf);
}

static int send_ep3_game_join(ship_client_t *c, lobby_t *l) {
    uint8_t *sendbuf = get_sendbuf();
    ep3_game_join_pkt *pkt = (ep3_game_join_pkt *)sendbuf;
    int clients = 0, i;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Clear it out first. */
    memset(pkt, 0, EP3_GAME_JOIN_LENGTH);

    /* Fill in the basics. */
    pkt->hdr.pkt_type = GAME_JOIN_TYPE;
    pkt->hdr.pkt_len = LE16(EP3_GAME_JOIN_LENGTH);
    pkt->client_id = c->client_id;
    pkt->leader_id = l->leader_id;
    pkt->one = 1;
    pkt->difficulty = 0;
    pkt->battle = l->battle;
    pkt->event = l->event;
    pkt->section = l->section;
    pkt->challenge = 0;
    pkt->rand_seed = LE32(l->rand_seed);
    pkt->episode = 1;
    pkt->one2 = 0;

    /* Fill in the variations array? */
    //memcpy(pkt->maps, l->maps, 0x20 * 4);

    for(i = 0; i < 4; ++i) {
        if(l->clients[i]) {
            /* Copy the player's data into the packet. */
            pkt->players[i].tag = LE32(0x00010000);
            pkt->players[i].guildcard = LE32(l->clients[i]->guildcard);
            pkt->players[i].ip_addr = l->clients[i]->addr;
            pkt->players[i].client_id = LE32(i);
            
            /* No need to iconv the names, they'll be good as is */
            memcpy(pkt->players[i].name, l->clients[i]->pl->v1.name, 16);

            /* Copy the player data to that part of the packet. */
            memcpy(&pkt->player_data[i], &l->clients[i]->pl->v1,
                   sizeof(v1_player_t));

            ++clients;
        }
    }

    /* Copy the client count over. */
    pkt->hdr.flags = (uint8_t)clients;

    /* Send it away */
    return crypt_send(c, EP3_GAME_JOIN_LENGTH, sendbuf);
}

int send_game_join(ship_client_t *c, lobby_t *l) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_game_join(c, l);

        case CLIENT_VERSION_PC:
            return send_pc_game_join(c, l);

        case CLIENT_VERSION_GC:
            return send_gc_game_join(c, l);

        case CLIENT_VERSION_EP3:
            return send_ep3_game_join(c, l);
    }

    return -1;
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
    pkt->hdr.pkt_type = GAME_LIST_TYPE;

    /* Fill in the first entry */
    pkt->entries[0].menu_id = 0xFFFFFFFF;
    pkt->entries[0].item_id = 0xFFFFFFFF;
    pkt->entries[0].flags = 0x04;
    strcpy(pkt->entries[0].name, b->ship->cfg->name);

    TAILQ_FOREACH(l, &b->lobbies, qentry) {
        /* Ignore default lobbies and Gamecube games */
        if(l->type != LOBBY_TYPE_GAME || l->episode) {
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
        pkt->entries[entries].v2 = l->v2;
        pkt->entries[entries].flags = (l->challenge ? 0x20 : 0x00) |
            (l->battle ? 0x10 : 0x00) | (l->passwd[0] ? 2 : 0) |
            (l->v2 ? 0x40 : 0x00);

        /* Copy the name. The names are either in Shift-JIS or ISO-8859-1, and
           should be prefixed with the appropriate language tag already */
        strncpy(pkt->entries[entries].name, l->name, 16);

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
    iconv_t ic, ic2;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    ic = iconv_open("UTF-16LE", "SHIFT_JIS");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    ic2 = iconv_open("UTF-16LE", "ISO-8859-1");

    if(ic2 == (iconv_t)-1) {
        perror("iconv_open");
        iconv_close(ic);
        return -1;
    }

    /* Clear out the packet and the first entry */
    memset(pkt, 0, 0x30);

    /* Fill in the header */
    pkt->hdr.pkt_type = GAME_LIST_TYPE;

    /* Fill in the first entry */
    pkt->entries[0].menu_id = 0xFFFFFFFF;
    pkt->entries[0].item_id = 0xFFFFFFFF;
    pkt->entries[0].flags = 0x04;

    in = strlen(b->ship->cfg->name) + 1;
    out = 0x20;
    inptr = b->ship->cfg->name;
    outptr = (char *)pkt->entries[0].name;
    iconv(ic2, &inptr, &in, &outptr, &out);

    TAILQ_FOREACH(l, &b->lobbies, qentry) {
        /* Ignore default lobbies and Gamecube games */
        if(l->type != LOBBY_TYPE_GAME || l->episode) {
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
        pkt->entries[entries].v2 = l->v2;
        pkt->entries[entries].flags = (l->challenge ? 0x20 : 0x00) |
            (l->battle ? 0x10 : 0x00) | (l->passwd[0] ? 2 : 0) |
            (l->v2 ? 0x40 : 0x00);

        /* Copy the name */
        in = strlen(l->name);
        out = 0x20;
        inptr = l->name;
        outptr = (char *)pkt->entries[entries].name;

        if(l->name[1] == 'J') {
            iconv(ic, &inptr, &in, &outptr, &out);
        }
        else {
            iconv(ic2, &inptr, &in, &outptr, &out);
        }

        /* Unlock the lobby */
        pthread_mutex_unlock(&l->mutex);

        /* Update the counters */
        ++entries;
        len += 0x2C;
    }

    iconv_close(ic);
    iconv_close(ic2);

    /* Fill in the rest of the header */
    pkt->hdr.flags = entries - 1;
    pkt->hdr.pkt_len = LE16(len);

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

static int send_gc_game_list(ship_client_t *c, block_t *b) {
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
    pkt->hdr.pkt_type = GAME_LIST_TYPE;

    /* Fill in the first entry */
    pkt->entries[0].menu_id = 0xFFFFFFFF;
    pkt->entries[0].item_id = 0xFFFFFFFF;
    pkt->entries[0].flags = 0x04;
    strcpy(pkt->entries[0].name, b->ship->cfg->name);

    TAILQ_FOREACH(l, &b->lobbies, qentry) {
        /* Ignore default lobbies */
        if(l->type != LOBBY_TYPE_GAME) {
            continue;
        }

        /* Ignore DC/PC games if the user hasn't set the flag to show them or
           the lobby doesn't have the right flag set */
        if(!l->episode && (!(c->flags & CLIENT_FLAG_SHOW_DCPC_ON_GC) ||
                           !(l->flags & LOBBY_FLAG_GC_ALLOWED))) {
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
        pkt->entries[entries].flags = ((l->episode <= 1) ? 0x40 : 0x80) |
            (l->challenge ? 0x20 : 0x00) | (l->battle ? 0x10 : 0x00) |
            (l->passwd[0] ? 0x02 : 0x00);

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

static int send_ep3_game_list(ship_client_t *c, block_t *b) {
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
    pkt->hdr.pkt_type = GAME_LIST_TYPE;

    /* Fill in the first entry */
    pkt->entries[0].menu_id = 0xFFFFFFFF;
    pkt->entries[0].item_id = 0xFFFFFFFF;
    pkt->entries[0].flags = 0x04;
    strcpy(pkt->entries[0].name, b->ship->cfg->name);

    TAILQ_FOREACH(l, &b->lobbies, qentry) {
        /* Ignore non-Episode 3 and default lobbies */
        if(l->type != LOBBY_TYPE_EP3_GAME) {
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
        pkt->entries[entries].flags = ((l->episode <= 1) ? 0x40 : 0x80) |
            (l->challenge ? 0x20 : 0x00) | (l->battle ? 0x10 : 0x00) |
            (l->passwd[0] ? 0x02 : 0x00);

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

        case CLIENT_VERSION_PC:
            return send_pc_game_list(c, b);

        case CLIENT_VERSION_GC:
            return send_gc_game_list(c, b);

        case CLIENT_VERSION_EP3:
            return send_ep3_game_list(c, b);
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
    pkt->hdr.pkt_type = LOBBY_INFO_TYPE;

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

        /* These are always ASCII, so this is fine */
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
    ICONV_CONST char *inptr;
    char *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    ic = iconv_open("UTF-16LE", "ISO-8859-1");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear the base packet */
    memset(pkt, 0, 0x30);

    /* Fill in some basic stuff */
    pkt->hdr.pkt_type = LOBBY_INFO_TYPE;

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
        memset(&pkt->entries[i], 0, 0x2C);

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
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            return send_dc_info_list(c, s);

        case CLIENT_VERSION_PC:
            return send_pc_info_list(c, s);
    }

    return -1;
}

/* This is a special case of the information select menu for PSOPC. This allows
   the user to pick to make a V1 compatible game or not. */
int send_pc_game_type_sel(ship_client_t *c) {
    uint8_t *sendbuf = get_sendbuf();
    pc_block_list_pkt *pkt = (pc_block_list_pkt *)sendbuf;
    ship_t *s = c->cur_ship;
    const char str1[16] = "Allow PSOv1";
    const char str2[16] = "PSOv2 Only";
    const char str3[16] = "PSOPC Only";
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    ic = iconv_open("UTF-16LE", "ASCII");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear the packet */
    memset(pkt, 0, 0xB4);

    /* Fill in the first entry (which isn't shown) */
    pkt->entries[0].menu_id = LE32(0x00040000);
    pkt->entries[0].item_id = 0;
    pkt->entries[0].flags = 0;

    in = strlen(s->cfg->name) + 1;
    out = 0x20;
    inptr = s->cfg->name;
    outptr = (char *)pkt->entries[0].name;
    iconv(ic, &inptr, &in, &outptr, &out);

    /* Add the "Allow PSOv1" entry */
    pkt->entries[1].menu_id = LE32(0x00000006);
    pkt->entries[1].item_id = LE32(0);
    pkt->entries[1].flags = 0;

    in = strlen(str1) + 1;
    out = 0x20;
    inptr = (char *)str1;
    outptr = (char *)pkt->entries[1].name;
    iconv(ic, &inptr, &in, &outptr, &out);

    /* Add the "PSOv2 Only" entry */
    pkt->entries[2].menu_id = LE32(0x00000006);
    pkt->entries[2].item_id = LE32(1);
    pkt->entries[2].flags = 0;

    in = strlen(str2) + 1;
    out = 0x20;
    inptr = (char *)str2;
    outptr = (char *)pkt->entries[2].name;
    iconv(ic, &inptr, &in, &outptr, &out);

    /* Add the "PSOPC Only" entry */
    pkt->entries[3].menu_id = LE32(0x00000006);
    pkt->entries[3].item_id = LE32(2);
    pkt->entries[3].flags = 0;

    in = strlen(str3) + 1;
    out = 0x20;
    inptr = (char *)str3;
    outptr = (char *)pkt->entries[3].name;
    iconv(ic, &inptr, &in, &outptr, &out);

    iconv_close(ic);

    /* Fill in some basic stuff */
    pkt->hdr.pkt_type = LOBBY_INFO_TYPE;
    pkt->hdr.pkt_len = LE16(0xB4);
    pkt->hdr.flags = 3;

    /* Send the packet away */
    return crypt_send(c, 0xB4, sendbuf);
}

/* Send a message to the client. */
static int send_dc_message_box(ship_client_t *c, const char *fmt,
                               va_list args) {
    uint8_t *sendbuf = get_sendbuf();
    dc_msg_box_pkt *pkt = (dc_msg_box_pkt *)sendbuf;
    int len;
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;
    char tm[514];

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Don't send these to GC players, its very likely they'll crash if they're
       on a US GC (apparently). */
    if((c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) &&
       !(c->flags & CLIENT_FLAG_TYPE_SHIP)) {
        debug(DBG_LOG, "Silently (to the user) dropping message box for GC\n");
        return 0;
    }

    /* Do the formatting */
    vsnprintf(tm, 512, fmt, args);
    tm[511] = '\0';
    in = strlen(tm) + 1;

    /* Make sure we have a language code tag */
    if(tm[0] != '\t' || (tm[1] != 'E' && tm[1] != 'J')) {
        /* Assume Non-Japanese if we don't have a marker. */
        memmove(&tm[2], &tm[0], in);
        tm[0] = '\t';
        tm[1] = 'E';
        in += 2;
    }

    /* Set up to convert between encodings */
    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        if(tm[1] == 'J') {
            ic = iconv_open("SHIFT_JIS", "UTF-8");
        }
        else {
            ic = iconv_open("ISO-8859-1", "UTF-8");
        }
    }
    else {
        ic = iconv_open("UTF-16LE", "UTF-8");
    }

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Convert the message to the appropriate encoding. */
    out = 65500;
    inptr = tm;
    outptr = (char *)pkt->msg;
    iconv(ic, &inptr, &in, &outptr, &out);
    len = 65500 - out;
    iconv_close(ic);

    /* Add any padding needed */
    while(len & 0x03) {
        pkt->msg[len++] = 0;
    }

    /* Fill in the header */
    len += 0x04;

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        pkt->hdr.dc.pkt_type = MSG_BOX_TYPE;
        pkt->hdr.dc.flags = 0;
        pkt->hdr.dc.pkt_len = LE16(len);
    }
    else {
        pkt->hdr.pc.pkt_type = MSG_BOX_TYPE;
        pkt->hdr.pc.flags = 0;
        pkt->hdr.pc.pkt_len = LE16(len);
    }

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

int send_message_box(ship_client_t *c, const char *fmt, ...) {
    va_list args;
    int rv = -1;

    va_start(args, fmt);

    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            rv = send_dc_message_box(c, fmt, args);
    }

    va_end(args);

    return rv;
}

/* Send the list of quest categories to the client. */
static int send_dc_quest_categories(ship_client_t *c,
                                    sylverant_quest_list_t *l) {
    uint8_t *sendbuf = get_sendbuf();
    dc_quest_list_pkt *pkt = (dc_quest_list_pkt *)sendbuf;
    int i, len = 0x04, entries = 0;
    uint32_t type = SYLVERANT_QUEST_NORMAL;
    iconv_t ic, ic2;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Quest stuff is stored internally as UTF-8, set up for converting to the
       right encoding */
    ic = iconv_open("ISO-8859-1", "UTF-8");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    ic2 = iconv_open("SHIFT_JIS", "UTF-8");

    if(ic2 == (iconv_t)-1) {
        perror("iconv_open");
        iconv_close(ic);
        return -1;
    }

    if(c->cur_lobby->battle) {
        type = SYLVERANT_QUEST_BATTLE;
    }
    else if(c->cur_lobby->challenge) {
        type = SYLVERANT_QUEST_CHALLENGE;
    }

    /* Clear out the header */
    memset(pkt, 0, 0x04);

    /* Fill in the header */
    pkt->hdr.pkt_type = QUEST_LIST_TYPE;

    for(i = 0; i < l->cat_count; ++i) {
        /* Skip quests not of the right type. */
        if(l->cats[i].type != type) {
            continue;
        }

        /* Clear the entry */
        memset(pkt->entries + entries, 0, 0x98);

        /* Copy the category's information over to the packet */
        pkt->entries[entries].menu_id = LE32(0x00000003);
        pkt->entries[entries].item_id = LE32(i);

        /* Convert the name and the description to the appropriate encoding
           XXXX: Handle Japanese */
        in = 32;
        out = 32;
        inptr = l->cats[i].name;
        outptr = (char *)pkt->entries[entries].name;
        iconv(ic, &inptr, &in, &outptr, &out);

        in = 112;
        out = 112;
        inptr = l->cats[i].desc;
        outptr = (char *)pkt->entries[entries].desc;
        iconv(ic, &inptr, &in, &outptr, &out);

        ++entries;
        len += 0x98;
    }

    iconv_close(ic2);
    iconv_close(ic);

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
    ICONV_CONST char *inptr;
    char *outptr;
    uint32_t type = SYLVERANT_QUEST_NORMAL;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Quest names are stored internally as UTF-8, convert to UTF-16. */
    ic = iconv_open("UTF-16LE", "UTF-8");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    if(c->cur_lobby->battle) {
        type = SYLVERANT_QUEST_BATTLE;
    }
    else if(c->cur_lobby->challenge) {
        type = SYLVERANT_QUEST_CHALLENGE;
    }

    /* Clear out the header */
    memset(pkt, 0, 0x04);

    /* Fill in the header */
    pkt->hdr.pkt_type = QUEST_LIST_TYPE;

    for(i = 0; i < l->cat_count; ++i) {
        /* Skip quests not of the right type. */
        if(l->cats[i].type != type) {
            continue;
        }

        /* Clear the entry */
        memset(pkt->entries + i, 0, 0x128);

        /* Copy the category's information over to the packet */
        pkt->entries[entries].menu_id = LE32(0x00000003);
        pkt->entries[entries].item_id = LE32(i);

        /* Convert the name and the description to UTF-16. */
        in = 32;
        out = 64;
        inptr = l->cats[i].name;
        outptr = (char *)pkt->entries[entries].name;
        iconv(ic, &inptr, &in, &outptr, &out);

        in = 112;
        out = 224;
        inptr = l->cats[i].desc;
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

int send_quest_categories(ship_client_t *c, sylverant_quest_list_t *l) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3: /* XXXX? */
            return send_dc_quest_categories(c, l);

        case CLIENT_VERSION_PC:
            return send_pc_quest_categories(c, l);
    }

    return -1;
}

/* Send the list of quest categories to the client. */
static int send_dc_quest_categories_new(ship_client_t *c) {
    uint8_t *sendbuf = get_sendbuf();
    dc_quest_list_pkt *pkt = (dc_quest_list_pkt *)sendbuf;
    int i, len = 0x04, entries = 0;
    uint32_t type = SYLVERANT_QUEST_NORMAL;
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;
    sylverant_quest_list_t *qlist;
    lobby_t *l = c->cur_lobby;

    if(l->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        qlist = &c->cur_ship->qlist[CLIENT_VERSION_GC][c->language_code];
    }
    else if(!l->v2) {
        qlist = &c->cur_ship->qlist[CLIENT_VERSION_DCV1][c->language_code];
    }
    else {
        qlist = &c->cur_ship->qlist[CLIENT_VERSION_DCV2][c->language_code];
    }

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Quest stuff is stored internally as UTF-8, set up for converting to the
       right encoding */
    if(c->language_code != CLIENT_LANG_JAPANESE) {
        ic = iconv_open("ISO-8859-1", "UTF-8");

        if(ic == (iconv_t)-1) {
            perror("iconv_open");
            return -1;
        }
    }
    else {
        ic = iconv_open("SHIFT_JIS", "UTF-8");

        if(ic == (iconv_t)-1) {
            perror("iconv_open");
            return -1;
        }
    }

    if(c->cur_lobby->battle) {
        type = SYLVERANT_QUEST_BATTLE;
    }
    else if(c->cur_lobby->challenge) {
        type = SYLVERANT_QUEST_CHALLENGE;
    }

    /* Clear out the header */
    memset(pkt, 0, 0x04);

    /* Fill in the header */
    pkt->hdr.pkt_type = QUEST_LIST_TYPE;

    for(i = 0; i < qlist->cat_count; ++i) {
        /* Skip quests not of the right type. */
        if(qlist->cats[i].type != type) {
            continue;
        }

        /* Clear the entry */
        memset(pkt->entries + entries, 0, 0x98);

        /* Copy the category's information over to the packet */
        pkt->entries[entries].menu_id = LE32(0x00000003);
        pkt->entries[entries].item_id = LE32(i);

        /* Convert the name and the description to the appropriate encoding */
        in = 32;
        out = 32;
        inptr = qlist->cats[i].name;
        outptr = (char *)pkt->entries[entries].name;
        iconv(ic, &inptr, &in, &outptr, &out);

        in = 112;
        out = 112;
        inptr = qlist->cats[i].desc;
        outptr = (char *)pkt->entries[entries].desc;
        iconv(ic, &inptr, &in, &outptr, &out);

        ++entries;
        len += 0x98;
    }

    iconv_close(ic);

    /* Fill in the rest of the header */
    pkt->hdr.flags = entries;
    pkt->hdr.pkt_len = LE16(len);

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

static int send_pc_quest_categories_new(ship_client_t *c) {
    uint8_t *sendbuf = get_sendbuf();
    pc_quest_list_pkt *pkt = (pc_quest_list_pkt *)sendbuf;
    int i, len = 0x04, entries = 0;
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;
    uint32_t type = SYLVERANT_QUEST_NORMAL;
    sylverant_quest_list_t *qlist;
    lobby_t *l = c->cur_lobby;

    if(!l->v2) {
        qlist = &c->cur_ship->qlist[CLIENT_VERSION_DCV1][c->language_code];
    }
    else {
        qlist = &c->cur_ship->qlist[CLIENT_VERSION_PC][c->language_code];
    }

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Quest names are stored internally as UTF-8, convert to UTF-16. */
    ic = iconv_open("UTF-16LE", "UTF-8");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    if(c->cur_lobby->battle) {
        type = SYLVERANT_QUEST_BATTLE;
    }
    else if(c->cur_lobby->challenge) {
        type = SYLVERANT_QUEST_CHALLENGE;
    }

    /* Clear out the header */
    memset(pkt, 0, 0x04);

    /* Fill in the header */
    pkt->hdr.pkt_type = QUEST_LIST_TYPE;

    for(i = 0; i < qlist->cat_count; ++i) {
        /* Skip quests not of the right type. */
        if(qlist->cats[i].type != type) {
            continue;
        }

        /* Clear the entry */
        memset(pkt->entries + i, 0, 0x128);

        /* Copy the category's information over to the packet */
        pkt->entries[entries].menu_id = LE32(0x00000003);
        pkt->entries[entries].item_id = LE32(i);

        /* Convert the name and the description to UTF-16. */
        in = 32;
        out = 64;
        inptr = qlist->cats[i].name;
        outptr = (char *)pkt->entries[entries].name;
        iconv(ic, &inptr, &in, &outptr, &out);

        in = 112;
        out = 224;
        inptr = qlist->cats[i].desc;
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

int send_quest_categories_new(ship_client_t *c) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3: /* XXXX? */
            return send_dc_quest_categories_new(c);

        case CLIENT_VERSION_PC:
            return send_pc_quest_categories_new(c);
    }

    return -1;
}

/* Send the list of quests in a category to the client. */
static int send_dc_quest_list(ship_client_t *c, int cat,
                              sylverant_quest_category_t *l, uint32_t ver) {
    uint8_t *sendbuf = get_sendbuf();
    dc_quest_list_pkt *pkt = (dc_quest_list_pkt *)sendbuf;
    int i, len = 0x04, entries = 0, max = INT_MAX;
    iconv_t ic, ic2;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Quest stuff is stored internally as UTF-8, set up for converting to the
       right encoding */
    ic = iconv_open("ISO-8859-1", "UTF-8");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    ic2 = iconv_open("SHIFT_JIS", "UTF-8");

    if(ic2 == (iconv_t)-1) {
        perror("iconv_open");
        iconv_close(ic);
        return -1;
    }

    /* Clear out the header */
    memset(pkt, 0, 0x04);

    /* If this is for challenge mode, figure out our limit. */
    if(c->cur_lobby->challenge) {
        max = c->cur_lobby->max_chal;
    }

    /* Fill in the header */
    pkt->hdr.pkt_type = QUEST_LIST_TYPE;

    for(i = 0; i < l->quest_count && i < max; ++i) {
        if(!(l->quests[i].versions & ver)) {
            continue;
        }

        if(l->quests[i].event != -1 &&
           l->quests[i].event != c->cur_lobby->event) {
            continue;
        }

        /* Clear the entry */
        memset(pkt->entries + entries, 0, 0x98);

        /* Copy the category's information over to the packet */
        pkt->entries[entries].menu_id = LE32(((0x00000004) | (cat << 8)));
        pkt->entries[entries].item_id = LE32(i);

        /* Convert the name and the description to the appropriate encoding
           XXXX: Handle Japanese */
        in = 32;
        out = 32;
        inptr = l->quests[i].name;
        outptr = (char *)pkt->entries[entries].name;
        iconv(ic, &inptr, &in, &outptr, &out);

        in = 112;
        out = 112;
        inptr = l->quests[i].desc;
        outptr = (char *)pkt->entries[entries].desc;
        iconv(ic, &inptr, &in, &outptr, &out);

        ++entries;
        len += 0x98;
    }

    iconv_close(ic2);
    iconv_close(ic);

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
    int i, len = 0x04, entries = 0, max = INT_MAX;
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Quest names are stored internally as UTF-8, convert to UTF-16. */
    ic = iconv_open("UTF-16LE", "UTF-8");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear out the header */
    memset(pkt, 0, 0x04);

    /* If this is for challenge mode, figure out our limit. */
    if(c->cur_lobby->challenge) {
        max = c->cur_lobby->max_chal;
    }

    /* Fill in the header */
    pkt->hdr.pkt_type = QUEST_LIST_TYPE;

    for(i = 0; i < l->quest_count && i < max; ++i) {
        if(!(l->quests[i].versions & ver)) {
            continue;
        }

        if(l->quests[i].event != -1 &&
           l->quests[i].event != c->cur_lobby->event) {
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

static int send_gc_quest_list(ship_client_t *c, int cat,
                              sylverant_quest_category_t *l) {
    uint8_t *sendbuf = get_sendbuf();
    dc_quest_list_pkt *pkt = (dc_quest_list_pkt *)sendbuf;
    int i, len = 0x04, entries = 0, max = INT_MAX;
    iconv_t ic, ic2;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Quest stuff is stored internally as UTF-8, set up for converting to the
       right encoding */
    ic = iconv_open("ISO-8859-1", "UTF-8");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    ic2 = iconv_open("SHIFT_JIS", "UTF-8");

    if(ic2 == (iconv_t)-1) {
        perror("iconv_open");
        iconv_close(ic);
        return -1;
    }

    /* Clear out the header */
    memset(pkt, 0, 0x04);

    /* If this is for challenge mode, figure out our limit. */
    if(c->cur_lobby->challenge) {
        max = c->cur_lobby->max_chal;
    }

    /* Fill in the header */
    pkt->hdr.pkt_type = QUEST_LIST_TYPE;

    for(i = 0; i < l->quest_count && i < max; ++i) {
        if(!(l->quests[i].versions & SYLVERANT_QUEST_GC) ||
           l->quests[i].episode != c->cur_lobby->episode) {
            continue;
        }

        if(l->quests[i].event != -1 &&
           l->quests[i].event != c->cur_lobby->event) {
            continue;
        }

        /* Clear the entry */
        memset(pkt->entries + entries, 0, 0x98);

        /* Copy the category's information over to the packet */
        pkt->entries[entries].menu_id = LE32(((0x00000004) | (cat << 8)));
        pkt->entries[entries].item_id = LE32(i);

        /* Convert the name and the description to the appropriate encoding
           XXXX: Handle Japanese */
        in = 32;
        out = 32;
        inptr = l->quests[i].name;
        outptr = (char *)pkt->entries[entries].name;
        iconv(ic, &inptr, &in, &outptr, &out);

        in = 112;
        out = 112;
        inptr = l->quests[i].desc;
        outptr = (char *)pkt->entries[entries].desc;
        iconv(ic, &inptr, &in, &outptr, &out);

        ++entries;
        len += 0x98;
    }

    iconv_close(ic2);
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

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3: /* XXXX? */
            return send_gc_quest_list(c, cat, l);
    }

    return -1;
}

/* Send the list of quests in a category to the client. */
static int send_dc_quest_list_new(ship_client_t *c, int cn) {
    uint8_t *sendbuf = get_sendbuf();
    dc_quest_list_pkt *pkt = (dc_quest_list_pkt *)sendbuf;
    int i, len = 0x04, entries = 0, max = INT_MAX, j;
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;
    sylverant_quest_list_t *qlist;
    lobby_t *l = c->cur_lobby;
    sylverant_quest_category_t *cat;
    sylverant_quest_t *quest;
    quest_map_elem_t *elem;
    ship_client_t *tmp;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }
    
    if(!l->v2) {
        qlist = &c->cur_ship->qlist[CLIENT_VERSION_DCV1][c->language_code];
    }
    else {
        qlist = &c->cur_ship->qlist[CLIENT_VERSION_DCV2][c->language_code];
    }

    /* Check the category for sanity */
    if(qlist->cat_count <= cn) {
        return -1;
    }

    cat = &qlist->cats[cn];

    /* Quest stuff is stored internally as UTF-8, set up for converting to the
       right encoding */
    if(c->language_code != CLIENT_LANG_JAPANESE) {
        ic = iconv_open("ISO-8859-1", "UTF-8");

        if(ic == (iconv_t)-1) {
            perror("iconv_open");
            return -1;
        }
    }
    else {
        ic = iconv_open("SHIFT_JIS", "UTF-8");

        if(ic == (iconv_t)-1) {
            perror("iconv_open");
            return -1;
        }
    }

    /* Clear out the header */
    memset(pkt, 0, 0x04);

    /* If this is for challenge mode, figure out our limit. */
    if(c->cur_lobby->challenge) {
        max = c->cur_lobby->max_chal;
    }

    /* Fill in the header */
    pkt->hdr.pkt_type = QUEST_LIST_TYPE;

    for(i = 0; i < cat->quest_count && i < max; ++i) {
        quest = &cat->quests[i];
        elem = (quest_map_elem_t *)quest->user_data;

        /* Skip quests that aren't for the current event */
        if(quest->event != -1 && quest->event != c->cur_lobby->event) {
            continue;
        }

        /* Look through to make sure that all clients in the lobby can play the
           quest */
        for(j = 0; j < l->max_clients; ++j) {
            if(!(tmp = l->clients[j])) {
                continue;
            }

            if(!elem->qptr[tmp->version][tmp->language_code] &&
               !elem->qptr[tmp->version][CLIENT_LANG_ENGLISH]) {
                break;
            }
        }

        /* Skip quests where we can't play them due to restrictions by users'
           versions or language codes */
        if(j != l->max_clients) {
            continue;
        }

        /* Clear the entry */
        memset(pkt->entries + entries, 0, 0x98);

        /* Copy the category's information over to the packet */
        pkt->entries[entries].menu_id = LE32(((0x00000004) | (cn << 8)));
        pkt->entries[entries].item_id = LE32(quest->qid);

        /* Convert the name and the description to the appropriate encoding */
        in = 32;
        out = 32;
        inptr = quest->name;
        outptr = (char *)pkt->entries[entries].name;
        iconv(ic, &inptr, &in, &outptr, &out);

        in = 112;
        out = 112;
        inptr = quest->desc;
        outptr = (char *)pkt->entries[entries].desc;
        iconv(ic, &inptr, &in, &outptr, &out);

        ++entries;
        len += 0x98;
    }

    iconv_close(ic);

    /* Fill in the rest of the header */
    pkt->hdr.flags = entries;
    pkt->hdr.pkt_len = LE16(len);

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

static int send_pc_quest_list_new(ship_client_t *c, int cn) {
    uint8_t *sendbuf = get_sendbuf();
    pc_quest_list_pkt *pkt = (pc_quest_list_pkt *)sendbuf;
    int i, len = 0x04, entries = 0, max = INT_MAX, j;
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;
    sylverant_quest_list_t *qlist;
    lobby_t *l = c->cur_lobby;
    sylverant_quest_category_t *cat;
    sylverant_quest_t *quest;
    quest_map_elem_t *elem;
    ship_client_t *tmp;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    if(!l->v2) {
        qlist = &c->cur_ship->qlist[CLIENT_VERSION_DCV1][c->language_code];
    }
    else {
        qlist = &c->cur_ship->qlist[CLIENT_VERSION_PC][c->language_code];
    }

    /* Check the category for sanity */
    if(qlist->cat_count <= cn) {
        return -1;
    }
    
    cat = &qlist->cats[cn];

    /* Quest names are stored internally as UTF-8, convert to UTF-16. */
    ic = iconv_open("UTF-16LE", "UTF-8");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear out the header */
    memset(pkt, 0, 0x04);

    /* If this is for challenge mode, figure out our limit. */
    if(c->cur_lobby->challenge) {
        max = c->cur_lobby->max_chal;
    }

    /* Fill in the header */
    pkt->hdr.pkt_type = QUEST_LIST_TYPE;

    for(i = 0; i < cat->quest_count && i < max; ++i) {
        quest = &cat->quests[i];
        elem = (quest_map_elem_t *)quest->user_data;

        /* Skip quests that aren't for the current event */
        if(quest->event != -1 && quest->event != c->cur_lobby->event) {
            continue;
        }

        /* Look through to make sure that all clients in the lobby can play the
           quest */
        for(j = 0; j < l->max_clients; ++j) {
            if(!(tmp = l->clients[j])) {
                continue;
            }

            if(!elem->qptr[tmp->version][tmp->language_code] &&
               !elem->qptr[tmp->version][CLIENT_LANG_ENGLISH]) {
                break;
            }
        }

        /* Skip quests where we can't play them due to restrictions by users'
           versions or language codes */
        if(j != l->max_clients) {
            continue;
        }

        /* Clear the entry */
        memset(pkt->entries + entries, 0, 0x98);

        /* Copy the category's information over to the packet */
        pkt->entries[entries].menu_id = LE32(((0x00000004) | (cn << 8)));
        pkt->entries[entries].item_id = LE32(quest->qid);

        /* Convert the name and the description to UTF-16. */
        in = 32;
        out = 64;
        inptr = quest->name;
        outptr = (char *)pkt->entries[entries].name;
        iconv(ic, &inptr, &in, &outptr, &out);

        in = 112;
        out = 224;
        inptr = quest->desc;
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

static int send_gc_quest_list_new(ship_client_t *c, int cn) {
    uint8_t *sendbuf = get_sendbuf();
    dc_quest_list_pkt *pkt = (dc_quest_list_pkt *)sendbuf;
    int i, len = 0x04, entries = 0, max = INT_MAX, j;
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;
    sylverant_quest_list_t *qlist;
    lobby_t *l = c->cur_lobby;
    sylverant_quest_category_t *cat;
    sylverant_quest_t *quest;
    quest_map_elem_t *elem;
    ship_client_t *tmp;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    if(l->version == CLIENT_VERSION_GC) {
        qlist = &c->cur_ship->qlist[CLIENT_VERSION_GC][c->language_code];
    }
    else if(!l->v2) {
        qlist = &c->cur_ship->qlist[CLIENT_VERSION_DCV1][c->language_code];
    }
    else {
        qlist = &c->cur_ship->qlist[CLIENT_VERSION_DCV2][c->language_code];
    }

    /* Check the category for sanity */
    if(qlist->cat_count <= cn) {
        return -1;
    }

    cat = &qlist->cats[cn];

    /* Quest stuff is stored internally as UTF-8, set up for converting to the
       right encoding */
    if(c->language_code != CLIENT_LANG_JAPANESE) {
        ic = iconv_open("ISO-8859-1", "UTF-8");

        if(ic == (iconv_t)-1) {
            perror("iconv_open");
            return -1;
        }
    }
    else {
        ic = iconv_open("SHIFT_JIS", "UTF-8");

        if(ic == (iconv_t)-1) {
            perror("iconv_open");
            return -1;
        }
    }

    /* Clear out the header */
    memset(pkt, 0, 0x04);

    /* If this is for challenge mode, figure out our limit. */
    if(c->cur_lobby->challenge) {
        max = c->cur_lobby->max_chal;
    }

    /* Fill in the header */
    pkt->hdr.pkt_type = QUEST_LIST_TYPE;

    for(i = 0; i < cat->quest_count && i < max; ++i) {
        quest = &cat->quests[i];
        elem = (quest_map_elem_t *)quest->user_data;

        /* Skip quests that aren't for the current event */
        if(quest->event != -1 && quest->event != c->cur_lobby->event) {
            continue;
        }

        /* Look through to make sure that all clients in the lobby can play the
           quest */
        for(j = 0; j < l->max_clients; ++j) {
            if(!(tmp = l->clients[j])) {
                continue;
            }

            if(!elem->qptr[tmp->version][tmp->language_code] &&
               !elem->qptr[tmp->version][CLIENT_LANG_ENGLISH]) {
                break;
            }
        }

        /* Skip quests where we can't play them due to restrictions by users'
           versions or language codes */
        if(j != l->max_clients) {
            continue;
        }

        /* Make sure the episode matches up */
        if(quest->episode != c->cur_lobby->episode) {
            continue;
        }

        /* Clear the entry */
        memset(pkt->entries + entries, 0, 0x98);

        /* Copy the category's information over to the packet */
        pkt->entries[entries].menu_id = LE32(((0x00000004) | (cn << 8)));
        pkt->entries[entries].item_id = LE32(quest->qid);

        /* Convert the name and the description to the appropriate encoding */
        in = 32;
        out = 32;
        inptr = quest->name;
        outptr = (char *)pkt->entries[entries].name;
        iconv(ic, &inptr, &in, &outptr, &out);

        in = 112;
        out = 112;
        inptr = quest->desc;
        outptr = (char *)pkt->entries[entries].desc;
        iconv(ic, &inptr, &in, &outptr, &out);

        ++entries;
        len += 0x98;
    }

    iconv_close(ic);

    /* Fill in the rest of the header */
    pkt->hdr.flags = entries;
    pkt->hdr.pkt_len = LE16(len);

    /* Send it away */
    return crypt_send(c, len, sendbuf);
}

int send_quest_list_new(ship_client_t *c, int cat) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_dc_quest_list_new(c, cat);

        case CLIENT_VERSION_PC:
            return send_pc_quest_list_new(c, cat);

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3: /* XXXX? */
            return send_gc_quest_list_new(c, cat);
    }

    return -1;
}

/* Send information about a quest to the lobby. */
static int send_dc_quest_info(ship_client_t *c, sylverant_quest_t *q) {
    uint8_t *sendbuf = get_sendbuf();
    dc_msg_box_pkt *pkt = (dc_msg_box_pkt *)sendbuf;
    iconv_t ic;
    size_t in, out, outt;
    ICONV_CONST char *inptr;
    char *outptr;
    int len;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        if(c->language_code != CLIENT_LANG_JAPANESE) {
            ic = iconv_open("ISO-8859-1", "UTF-8");
        }
        else {
            ic = iconv_open("SHIFT_JIS", "UTF-8");
        }
    }
    else {
        ic = iconv_open("UTF-16LE", "UTF-8");
    }

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Clear the packet header */
    memset(pkt, 0, DC_QUEST_INFO_LENGTH);

    /* Fill in the basics */
    if(c->version == CLIENT_VERSION_DCV1 || c->version == CLIENT_VERSION_DCV2 ||
       c->version == CLIENT_VERSION_GC || c->version == CLIENT_VERSION_EP3) {
        pkt->hdr.dc.pkt_type = QUEST_INFO_TYPE;
        pkt->hdr.dc.flags = 0;
        pkt->hdr.dc.pkt_len = LE16(DC_QUEST_INFO_LENGTH);
        len = DC_QUEST_INFO_LENGTH;
        outt = 0x124;
    }
    else {
        pkt->hdr.pc.pkt_type = QUEST_INFO_TYPE;
        pkt->hdr.pc.flags = 0;
        pkt->hdr.pc.pkt_len = LE16(PC_QUEST_INFO_LENGTH);
        len = PC_QUEST_INFO_LENGTH;
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

int send_quest_info(lobby_t *l, sylverant_quest_t *q) {
    ship_client_t *c;
    int i;

    for(i = 0; i < l->max_clients; ++i) {
        c = l->clients[i];

        if(c) {
            /* Call the appropriate function. */
            switch(c->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                case CLIENT_VERSION_PC:
                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_EP3:
                    send_dc_quest_info(c, q);
                    break;
            }
        }
    }

    return 0;
}

int send_quest_info_new(lobby_t *l, uint32_t qid) {
    ship_client_t *c;
    int i;
    quest_map_elem_t *elem;
    sylverant_quest_t *q;

    /* Grab the mapped entry */
    c = l->clients[l->leader_id];
    elem = quest_lookup(&c->cur_ship->qmap, qid);

    /* Make sure we get the quest we're looking for */
    if(!elem) {
        return -1;
    }

    for(i = 0; i < l->max_clients; ++i) {
        if((c = l->clients[i])) {
            q = elem->qptr[c->version][c->language_code];

            /* If we didn't find it on the normal language code, try the
               fallback one (which is always English, for now). */
            if(!q) {
                q = elem->qptr[c->version][CLIENT_LANG_ENGLISH];

                /* If we still didn't find it, we've got trouble elsewhere... */
                if(!q) {
                    debug(DBG_WARN, "Couldn't find quest to send info!\n"
                          "ID: %d, Ver: %d, Language: %d, Fallback: %d\n", qid,
                          c->version, c->language_code, CLIENT_LANG_ENGLISH);
                    continue;
                }
            }

            /* Call the appropriate function. */
            switch(c->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                case CLIENT_VERSION_PC:
                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_EP3:
                    send_dc_quest_info(c, q);
                    break;
            }
        }
    }

    return 0;
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

    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x02; /* ??? */
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%sv1.dat", q->prefix);
    file->length = LE32(datlen);

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        return -2;
    }

    /* Now the .bin file. */
    memset(file, 0, sizeof(dc_quest_file_pkt));

    sprintf(file->name, "PSO/%s", q->name);

    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x02; /* ??? */
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%sv1.bin", q->prefix);
    file->length = LE32(binlen);

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        return -2;
    }

    /* Now send the chunks of the file, interleaved. */
    while(!bindone || !datdone) {
        /* Start with the dat file if we've got any more to send from it */
        if(!datdone) {
            /* Clear the packet */
            memset(chunk, 0, sizeof(dc_quest_chunk_pkt));

            /* Fill in the header */
            chunk->hdr.dc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%sv1.dat", q->prefix);
            amt = fread(chunk->data, 1, 0x400, dat);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
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
            chunk->hdr.dc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%sv1.bin", q->prefix);
            amt = fread(chunk->data, 1, 0x400, bin);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
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

    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x02; /* ??? */
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%sv2.dat", q->prefix);
    file->length = LE32(datlen);

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        return -2;
    }

    /* Now the .bin file. */
    memset(file, 0, sizeof(dc_quest_file_pkt));

    sprintf(file->name, "PSO/%s", q->name);
    
    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x02; /* ??? */
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%sv2.bin", q->prefix);
    file->length = LE32(binlen);

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        return -2;
    }

    /* Now send the chunks of the file, interleaved. */
    while(!bindone || !datdone) {
        /* Start with the dat file if we've got any more to send from it */
        if(!datdone) {
            /* Clear the packet */
            memset(chunk, 0, sizeof(dc_quest_chunk_pkt));

            /* Fill in the header */
            chunk->hdr.dc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%sv2.dat", q->prefix);
            amt = fread(chunk->data, 1, 0x400, dat);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
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
            chunk->hdr.dc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%sv2.bin", q->prefix);
            amt = fread(chunk->data, 1, 0x400, bin);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
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

    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x00;
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%spc.dat", q->prefix);
    file->length = LE32(datlen);
    file->flags = 0x0002;

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        return -2;
    }

    /* Now the .bin file. */
    memset(file, 0, sizeof(pc_quest_file_pkt));

    sprintf(file->name, "PSO/%s", q->name);

    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x00;
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%spc.bin", q->prefix);
    file->length = LE32(binlen);
    file->flags = 0x0002;

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        return -2;
    }

    /* Now send the chunks of the file, interleaved. */
    while(!bindone || !datdone) {
        /* Start with the dat file if we've got any more to send from it */
        if(!datdone) {
            /* Clear the packet */
            memset(chunk, 0, sizeof(dc_quest_chunk_pkt));

            /* Fill in the header */
            chunk->hdr.pc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.pc.flags = (uint8_t)chunknum;
            chunk->hdr.pc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%spc.dat", q->prefix);
            amt = fread(chunk->data, 1, 0x400, dat);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
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
            chunk->hdr.pc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.pc.flags = (uint8_t)chunknum;
            chunk->hdr.pc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%spc.bin", q->prefix);
            amt = fread(chunk->data, 1, 0x400, bin);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
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

static int send_gc_quest(ship_client_t *c, sylverant_quest_t *q) {
    uint8_t *sendbuf = get_sendbuf();
    gc_quest_file_pkt *file = (gc_quest_file_pkt *)sendbuf;
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
    sprintf(filename, "quests/%sgc.bin", q->prefix);
    bin = fopen(filename, "rb");

    sprintf(filename, "quests/%sgc.dat", q->prefix);
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

    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x00;
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%sgc.dat", q->prefix);
    file->length = LE32(datlen);
    file->flags = 0x0002;

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        return -2;
    }

    /* Now the .bin file. */
    memset(file, 0, sizeof(pc_quest_file_pkt));

    sprintf(file->name, "PSO/%s", q->name);

    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x00;
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%sgc.bin", q->prefix);
    file->length = LE32(binlen);
    file->flags = 0x0002;

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        return -2;
    }

    /* Now send the chunks of the file, interleaved. */
    while(!bindone || !datdone) {
        /* Start with the dat file if we've got any more to send from it */
        if(!datdone) {
            /* Clear the packet */
            memset(chunk, 0, sizeof(dc_quest_chunk_pkt));

            /* Fill in the header */
            chunk->hdr.dc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%sgc.dat", q->prefix);
            amt = fread(chunk->data, 1, 0x400, dat);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
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
            chunk->hdr.dc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%sgc.bin", q->prefix);
            amt = fread(chunk->data, 1, 0x400, bin);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
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

static int send_qst_quest(ship_client_t *c, sylverant_quest_t *q, int v1) {
    char filename[256];
    FILE *fp;
    long len;
    size_t read;
    uint8_t *sendbuf = get_sendbuf();

    /* Figure out what file we're going to send. */
    if(!v1) {
        sprintf(filename, "quests/%s%s.qst", q->prefix, type_codes[c->version]);
    }
    else {
        switch(c->version) {
            case CLIENT_VERSION_DCV1:
            case CLIENT_VERSION_DCV2:
                sprintf(filename, "quests/%sv1.qst", q->prefix);
                break;

            case CLIENT_VERSION_PC:
                sprintf(filename, "quests/%spcv1.qst", q->prefix);
                break;

            case CLIENT_VERSION_GC:
                sprintf(filename, "quests/%sgcv1.qst", q->prefix);
                break;

            default:
                return -1;
        }
    }

    fp = fopen(filename, "rb");

    if(!fp) {
        perror("fopen");
        return -1;
    }

    /* Figure out how long the file is. */
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* Copy the file (in chunks if necessary) to the sendbuf to actually send
       away. */
    while(len) {
        read = fread(sendbuf, 1, 65536, fp);

        /* If we can't read from the file, bail. */
        if(!read) {
            fclose(fp);
            return -2;
        }

        /* Make sure we read up to a header-size boundary. */
        if((read & (c->hdr_size - 1)) && !feof(fp)) {
            long amt = (read & (c->hdr_size - 1));

            fseek(fp, -amt, SEEK_CUR);
            read -= amt;
        }

        /* Send this chunk away. */
        if(crypt_send(c, read, sendbuf)) {
            fclose(fp);
            return -3;
        }

        len -= read;
    }

    /* We're finished. */
    fclose(fp);
    return 0;
}

int send_quest(lobby_t *l, sylverant_quest_t *q) {
    int i;
    int v1 = 0;

    /* What type of quest file are we sending? */
    if(q->format == SYLVERANT_QUEST_BINDAT) {
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

                    case CLIENT_VERSION_GC:
                        send_gc_quest(l->clients[i], q);
                        break;

                    default:
                        return -1;
                }
            }
        }
    }
    else if(q->format == SYLVERANT_QUEST_QST) {
        if(!l->v2 && l->version != CLIENT_VERSION_GC) {
            v1 = 1;
        }

        for(i = 0; i < l->max_clients; ++i) {
            if(l->clients[i] != NULL) {
                send_qst_quest(l->clients[i], q, v1);
            }
        }
    }
    else {
        return -1;
    }

    return 0;
}

/* Send a quest to everyone in a lobby. */
static int send_dcv1_quest_new(ship_client_t *c, quest_map_elem_t *qm, int v1,
                               int lang) {
    uint8_t *sendbuf = get_sendbuf();
    dc_quest_file_pkt *file = (dc_quest_file_pkt *)sendbuf;
    dc_quest_chunk_pkt *chunk = (dc_quest_chunk_pkt *)sendbuf;
    FILE *bin, *dat;
    uint32_t binlen, datlen;
    int bindone = 0, datdone = 0, chunknum = 0;
    char fn_base[256], filename[256];
    size_t amt;
    ship_t *s = c->cur_ship;
    sylverant_quest_t *q = qm->qptr[c->version][lang];

    /* Verify we got the sendbuf. */
    if(!sendbuf || !q) {
        return -1;
    }

    /* Each quest has two files: a .dat file and a .bin file, send a file packet
       for each of them. */
    sprintf(fn_base, "%s/%s-%s/%s", s->cfg->quests_dir,
            version_codes[c->version], language_codes[lang], q->prefix);

    sprintf(filename, "%s.bin", fn_base);
    bin = fopen(filename, "rb");

    if(!bin) {
        return -1;
    }

    sprintf(filename, "%s.dat", fn_base);
    dat = fopen(filename, "rb");

    if(!dat) {
        fclose(bin);
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

    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x02; /* ??? */
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%s.dat", q->prefix);
    file->length = LE32(datlen);

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        fclose(bin);
        fclose(dat);
        return -2;
    }

    /* Now the .bin file. */
    memset(file, 0, sizeof(dc_quest_file_pkt));

    sprintf(file->name, "PSO/%s", q->name);

    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x02; /* ??? */
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%s.bin", q->prefix);
    file->length = LE32(binlen);

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        fclose(bin);
        fclose(dat);
        return -2;
    }

    /* Now send the chunks of the file, interleaved. */
    while(!bindone || !datdone) {
        /* Start with the dat file if we've got any more to send from it */
        if(!datdone) {
            /* Clear the packet */
            memset(chunk, 0, sizeof(dc_quest_chunk_pkt));

            /* Fill in the header */
            chunk->hdr.dc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%s.dat", q->prefix);
            amt = fread(chunk->data, 1, 0x400, dat);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
                fclose(bin);
                fclose(dat);
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
            chunk->hdr.dc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%s.bin", q->prefix);
            amt = fread(chunk->data, 1, 0x400, bin);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
                fclose(bin);
                fclose(dat);
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

static int send_dcv2_quest_new(ship_client_t *c, quest_map_elem_t *qm, int v1,
                               int lang) {
    uint8_t *sendbuf = get_sendbuf();
    dc_quest_file_pkt *file = (dc_quest_file_pkt *)sendbuf;
    dc_quest_chunk_pkt *chunk = (dc_quest_chunk_pkt *)sendbuf;
    FILE *bin, *dat;
    uint32_t binlen, datlen;
    int bindone = 0, datdone = 0, chunknum = 0;
    char fn_base[256], filename[256];
    size_t amt;
    ship_t *s = c->cur_ship;
    sylverant_quest_t *q = qm->qptr[c->version][lang];

    /* Verify we got the sendbuf. */
    if(!sendbuf || !q) {
        return -1;
    }

    /* Each quest has two files: a .dat file and a .bin file, send a file packet
       for each of them. */
    if(!v1 || (q->versions & SYLVERANT_QUEST_V1)) {
        sprintf(fn_base, "%s/%s-%s/%s", s->cfg->quests_dir,
                version_codes[c->version], language_codes[lang], q->prefix);
    }
    else {
        sprintf(fn_base, "%s/%s-%s/%s", s->cfg->quests_dir,
                version_codes[CLIENT_VERSION_DCV1], language_codes[lang],
                q->prefix);
    }

    sprintf(filename, "%s.bin", fn_base);
    bin = fopen(filename, "rb");

    if(!bin) {
        return -1;
    }

    sprintf(filename, "%s.dat", fn_base);
    dat = fopen(filename, "rb");

    if(!dat) {
        fclose(bin);
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

    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x02; /* ??? */
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%s.dat", q->prefix);
    file->length = LE32(datlen);

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        fclose(bin);
        fclose(dat);
        return -2;
    }

    /* Now the .bin file. */
    memset(file, 0, sizeof(dc_quest_file_pkt));

    sprintf(file->name, "PSO/%s", q->name);

    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x02; /* ??? */
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%s.bin", q->prefix);
    file->length = LE32(binlen);

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        fclose(bin);
        fclose(dat);
        return -2;
    }

    /* Now send the chunks of the file, interleaved. */
    while(!bindone || !datdone) {
        /* Start with the dat file if we've got any more to send from it */
        if(!datdone) {
            /* Clear the packet */
            memset(chunk, 0, sizeof(dc_quest_chunk_pkt));

            /* Fill in the header */
            chunk->hdr.dc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%s.dat", q->prefix);
            amt = fread(chunk->data, 1, 0x400, dat);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
                fclose(bin);
                fclose(dat);
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
            chunk->hdr.dc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%s.bin", q->prefix);
            amt = fread(chunk->data, 1, 0x400, bin);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
                fclose(bin);
                fclose(dat);
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

static int send_pc_quest_new(ship_client_t *c, quest_map_elem_t *qm, int v1,
                             int lang) {
    uint8_t *sendbuf = get_sendbuf();
    pc_quest_file_pkt *file = (pc_quest_file_pkt *)sendbuf;
    dc_quest_chunk_pkt *chunk = (dc_quest_chunk_pkt *)sendbuf;
    FILE *bin, *dat;
    uint32_t binlen, datlen;
    int bindone = 0, datdone = 0, chunknum = 0;
    char fn_base[256], filename[256];
    size_t amt;
    ship_t *s = c->cur_ship;
    sylverant_quest_t *q = qm->qptr[c->version][lang];

    /* Verify we got the sendbuf. */
    if(!sendbuf || !q) {
        return -1;
    }

    /* Each quest has two files: a .dat file and a .bin file, send a file packet
       for each of them. */
    if(!v1 || (q->versions & SYLVERANT_QUEST_V1)) {
        sprintf(fn_base, "%s/%s-%s/%s", s->cfg->quests_dir,
                version_codes[c->version], language_codes[lang], q->prefix);
    }
    else {
        sprintf(fn_base, "%s/%s-%s/%sv1", s->cfg->quests_dir,
                version_codes[c->version], language_codes[lang], q->prefix);
    }

    sprintf(filename, "%s.bin", fn_base);
    bin = fopen(filename, "rb");

    if(!bin) {
        return -1;
    }

    sprintf(filename, "%s.dat", fn_base);
    dat = fopen(filename, "rb");

    if(!dat) {
        fclose(bin);
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

    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x00;
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%s.dat", q->prefix);
    file->length = LE32(datlen);
    file->flags = 0x0002;

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        fclose(bin);
        fclose(dat);
        return -2;
    }

    /* Now the .bin file. */
    memset(file, 0, sizeof(pc_quest_file_pkt));

    sprintf(file->name, "PSO/%s", q->name);

    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x00;
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%s.bin", q->prefix);
    file->length = LE32(binlen);
    file->flags = 0x0002;

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        fclose(bin);
        fclose(dat);
        return -2;
    }

    /* Now send the chunks of the file, interleaved. */
    while(!bindone || !datdone) {
        /* Start with the dat file if we've got any more to send from it */
        if(!datdone) {
            /* Clear the packet */
            memset(chunk, 0, sizeof(dc_quest_chunk_pkt));

            /* Fill in the header */
            chunk->hdr.pc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.pc.flags = (uint8_t)chunknum;
            chunk->hdr.pc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%s.dat", q->prefix);
            amt = fread(chunk->data, 1, 0x400, dat);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
                fclose(bin);
                fclose(dat);
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
            chunk->hdr.pc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.pc.flags = (uint8_t)chunknum;
            chunk->hdr.pc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%s.bin", q->prefix);
            amt = fread(chunk->data, 1, 0x400, bin);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
                fclose(bin);
                fclose(dat);
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

static int send_gc_quest_new(ship_client_t *c, quest_map_elem_t *qm, int v1,
                             int lang) {
    uint8_t *sendbuf = get_sendbuf();
    gc_quest_file_pkt *file = (gc_quest_file_pkt *)sendbuf;
    dc_quest_chunk_pkt *chunk = (dc_quest_chunk_pkt *)sendbuf;
    FILE *bin, *dat;
    uint32_t binlen, datlen;
    int bindone = 0, datdone = 0, chunknum = 0;
    char fn_base[256], filename[256];
    size_t amt;
    ship_t *s = c->cur_ship;
    sylverant_quest_t *q = qm->qptr[c->version][lang];

    /* Verify we got the sendbuf. */
    if(!sendbuf || !q) {
        return -1;
    }

    /* Each quest has two files: a .dat file and a .bin file, send a file packet
       for each of them. */
    if(!v1 || (q->versions & SYLVERANT_QUEST_V1)) {
        sprintf(fn_base, "%s/%s-%s/%s", s->cfg->quests_dir,
                version_codes[c->version], language_codes[lang], q->prefix);
    }
    else {
        sprintf(fn_base, "%s/%s-%s/%sv1", s->cfg->quests_dir,
                version_codes[c->version], language_codes[lang], q->prefix);
    }

    sprintf(filename, "%s.bin", fn_base);
    bin = fopen(filename, "rb");

    if(!bin) {
        return -1;
    }

    sprintf(filename, "%s.dat", fn_base);
    dat = fopen(filename, "rb");

    if(!dat) {
        fclose(bin);
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
    memset(file, 0, sizeof(gc_quest_file_pkt));

    sprintf(file->name, "PSO/%s", q->name);

    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x00;
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%s.dat", q->prefix);
    file->length = LE32(datlen);
    file->flags = 0x0002;

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        fclose(bin);
        fclose(dat);
        return -2;
    }

    /* Now the .bin file. */
    memset(file, 0, sizeof(gc_quest_file_pkt));

    sprintf(file->name, "PSO/%s", q->name);

    file->hdr.pkt_type = QUEST_FILE_TYPE;
    file->hdr.flags = 0x00;
    file->hdr.pkt_len = LE16(DC_QUEST_FILE_LENGTH);
    sprintf(file->filename, "%s.bin", q->prefix);
    file->length = LE32(binlen);
    file->flags = 0x0002;

    if(crypt_send(c, DC_QUEST_FILE_LENGTH, sendbuf)) {
        fclose(bin);
        fclose(dat);
        return -2;
    }

    /* Now send the chunks of the file, interleaved. */
    while(!bindone || !datdone) {
        /* Start with the dat file if we've got any more to send from it */
        if(!datdone) {
            /* Clear the packet */
            memset(chunk, 0, sizeof(dc_quest_chunk_pkt));

            /* Fill in the header */
            chunk->hdr.dc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%s.dat", q->prefix);
            amt = fread(chunk->data, 1, 0x400, dat);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
                fclose(bin);
                fclose(dat);
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
            chunk->hdr.dc.pkt_type = QUEST_CHUNK_TYPE;
            chunk->hdr.dc.flags = (uint8_t)chunknum;
            chunk->hdr.dc.pkt_len = LE16(DC_QUEST_CHUNK_LENGTH);

            /* Fill in the rest */
            sprintf(chunk->filename, "%s.bin", q->prefix);
            amt = fread(chunk->data, 1, 0x400, bin);
            chunk->length = LE32(((uint32_t)amt));

            /* Send it away */
            if(crypt_send(c, DC_QUEST_CHUNK_LENGTH, sendbuf)) {
                fclose(bin);
                fclose(dat);
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

static int send_qst_quest_new(ship_client_t *c, quest_map_elem_t *qm, int v1,
                              int lang) {
    char filename[256];
    FILE *fp;
    long len;
    size_t read;
    uint8_t *sendbuf = get_sendbuf();
    ship_t *s = c->cur_ship;
    sylverant_quest_t *q = qm->qptr[c->version][lang];

    /* Make sure we got the sendbuf and the quest */
    if(!sendbuf || !q) {
        return -1;
    }

    /* Figure out what file we're going to send. */
    if(!v1 || (q->versions & SYLVERANT_QUEST_V1)) {
        sprintf(filename, "%s/%s-%s/%s.qst", s->cfg->quests_dir,
                version_codes[c->version], language_codes[lang], q->prefix);
    }
    else {
        switch(c->version) {
            case CLIENT_VERSION_DCV1:
            case CLIENT_VERSION_DCV2:
                sprintf(filename, "%s/%s-%s/%s.qst", s->cfg->quests_dir,
                        version_codes[CLIENT_VERSION_DCV1],
                        language_codes[lang], q->prefix);
                break;

            case CLIENT_VERSION_PC:
            case CLIENT_VERSION_GC:
                sprintf(filename, "%s/%s-%s/%sv1.qst", s->cfg->quests_dir,
                        version_codes[c->version], language_codes[lang],
                        q->prefix);
                break;
        }
    }

    fp = fopen(filename, "rb");

    if(!fp) {
        perror("fopen");
        return -1;
    }

    /* Figure out how long the file is. */
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* Copy the file (in chunks if necessary) to the sendbuf to actually send
       away. */
    while(len) {
        read = fread(sendbuf, 1, 65536, fp);

        /* If we can't read from the file, bail. */
        if(!read) {
            fclose(fp);
            return -2;
        }

        /* Make sure we read up to a header-size boundary. */
        if((read & (c->hdr_size - 1)) && !feof(fp)) {
            long amt = (read & (c->hdr_size - 1));

            fseek(fp, -amt, SEEK_CUR);
            read -= amt;
        }

        /* Send this chunk away. */
        if(crypt_send(c, read, sendbuf)) {
            fclose(fp);
            return -3;
        }

        len -= read;
    }

    /* We're finished. */
    fclose(fp);
    return 0;
}

int send_quest_new(lobby_t *l, uint32_t qid) {
    int i;
    int v1 = 0;
    ship_t *s = l->clients[l->leader_id]->cur_ship;
    quest_map_elem_t *elem = quest_lookup(&s->qmap, qid);
    sylverant_quest_t *q;
    ship_client_t *c;
    int lang;

    /* Make sure we get the quest */
    if(!elem) {
        return -1;
    }

    /* See if we're looking for a v1-compat quest */
    if(!l->v2 && l->version != CLIENT_VERSION_GC) {
        v1 = 1;
    }

    /* What type of quest file are we sending? */
    for(i = 0; i < l->max_clients; ++i) {
        if((c = l->clients[i])) {
            q = elem->qptr[c->version][c->language_code];
            lang = c->language_code;

            /* If we didn't find it on the normal language code, try the
               fallback one (which is always English, for now). */
            if(!q) {
                q = elem->qptr[c->version][CLIENT_LANG_ENGLISH];
                lang = CLIENT_LANG_ENGLISH;
                
                /* If we still didn't find it, we've got trouble elsewhere... */
                if(!q) {
                    debug(DBG_WARN, "Couldn't find quest to send!\n"
                          "ID: %d, Ver: %d, Language: %d, Fallback: %d\n", qid,
                          c->version, c->language_code, CLIENT_LANG_ENGLISH);

                    /* Unfortunately, we're going to have to disconnect the user
                       if this happens, since we really have no recourse. */
                    c->flags |= CLIENT_FLAG_DISCONNECTED;
                    continue;
                }
            }

            if(q->format == SYLVERANT_QUEST_BINDAT) {
                /* Call the appropriate function. */
                switch(l->clients[i]->version) {
                    case CLIENT_VERSION_DCV1:
                        send_dcv1_quest_new(c, elem, v1, lang);
                        break;

                    case CLIENT_VERSION_DCV2:
                        send_dcv2_quest_new(c, elem, v1, lang);
                        break;

                    case CLIENT_VERSION_PC:
                        send_pc_quest_new(c, elem, v1, lang);
                        break;

                    case CLIENT_VERSION_GC:
                        send_gc_quest_new(c, elem, v1, lang);
                        break;

                    case CLIENT_VERSION_EP3:
                        return -1;
                }
            }
            else if(q->format == SYLVERANT_QUEST_QST) {
                send_qst_quest_new(c, elem, v1, lang);
            }
            else {
                return -1;
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
    pkt->hdr.dc.pkt_type = LOBBY_NAME_TYPE;
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
    ICONV_CONST char *inptr;
    char *outptr;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Lobby names are stored internally as Shift-JIS or ISO-8859-1, convert to
       UTF-16. */
    if(l->name[1] == 'J') {
        ic = iconv_open("UTF-16LE", "SHIFT_JIS");
    }
    else {
        ic = iconv_open("UTF-16LE", "ISO-8859-1");
    }

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

    /* Figure out how long the new string is. */
    len = 65532 - out;

    /* Fill in the basics */
    pkt->hdr.pc.pkt_type = LOBBY_NAME_TYPE;
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
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
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
            pkt->entries[clients].tag = LE32(0x00010000);
            pkt->entries[clients].guildcard = LE32(l->clients[i]->guildcard);
            pkt->entries[clients].arrow = LE32(l->clients[i]->arrow);

            ++clients;
            len += 0x0C;
        }
    }

    /* Fill in the rest of it */
    if(c->version == CLIENT_VERSION_DCV2 || c->version == CLIENT_VERSION_GC ||
       c->version == CLIENT_VERSION_EP3) {
        pkt->hdr.dc.pkt_type = LOBBY_ARROW_LIST_TYPE;
        pkt->hdr.dc.flags = (uint8_t)clients;
        pkt->hdr.dc.pkt_len = LE16(((uint16_t)len));
    }
    else {
        pkt->hdr.pc.pkt_type = LOBBY_ARROW_LIST_TYPE;
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
                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_EP3:
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
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            return send_dc_lobby_arrows(l, c);
    }

    return -1;
}

/* Send a ship list packet to the client. */
static int send_dc_ship_list(ship_client_t *c, ship_t *s, uint16_t menu_code) {
    uint8_t *sendbuf = get_sendbuf();
    dc_ship_list_pkt *pkt = (dc_ship_list_pkt *)sendbuf;
    int len = 0x20, entries = 0, j;
    miniship_t *i;
    char tmp[3];

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Clear the packet's header. */
    memset(pkt, 0, 0x20);

    /* Fill in the basics. */
    pkt->hdr.pkt_type = SHIP_LIST_TYPE;

    /* Fill in the "DATABASE/JP" entry */
    memset(&pkt->entries[0], 0, 0x1C);
    pkt->entries[0].menu_id = LE32(0x00000005);
    pkt->entries[0].item_id = 0;
    pkt->entries[0].flags = LE16(0x0004);
    strcpy(pkt->entries[0].name, "DATABASE/JP");
    pkt->entries[0].name[0x11] = 0x08;
    entries = 1;

    TAILQ_FOREACH(i, &s->ships, qentry) {
        if(i->ship_id && i->menu_code == menu_code) {
            if((i->flags & LOGIN_FLAG_GMONLY) &&
               !(c->privilege & CLIENT_PRIV_GLOBAL_GM)) {
                continue;
            }

            if((i->flags & (LOGIN_FLAG_NOV1 << c->version))) {
                continue;
            }

            /* Clear the new entry */
            memset(&pkt->entries[entries], 0, 0x1C);

            /* Copy the ship's information to the packet. */
            pkt->entries[entries].menu_id = LE32(0x00000005);
            pkt->entries[entries].item_id = LE32(i->ship_id);
            pkt->entries[entries].flags = 0;
            strcpy(pkt->entries[entries].name, i->name);

            ++entries;
            len += 0x1C;
        }
    }

    /* Fill in the menu codes */
    for(j = 0; j < s->mccount; ++j) {
        if(s->menu_codes[j] != menu_code) {
            tmp[0] = (char)(s->menu_codes[j]);
            tmp[1] = (char)(s->menu_codes[j] >> 8);
            tmp[2] = '\0';

            /* Make sure the values are in-bounds */
            if((tmp[0] || tmp[1]) && (!isalpha(tmp[0]) || !isalpha(tmp[1]))) {
                continue;
            }

            /* Clear out the ship information */
            memset(&pkt->entries[entries], 0, 0x1C);

            /* Fill in what we have */
            pkt->entries[entries].menu_id = LE32((0x00000005 |
                                                  (s->menu_codes[j] << 8)));
            pkt->entries[entries].item_id = LE32(0x00000000);
            pkt->entries[entries].flags = LE16(0x0000);

            /* Create the name string */
            if(tmp[0] && tmp[1]) {
                sprintf(pkt->entries[entries].name, "\tC6%s Ship List", tmp);
            }
            else {
                strcpy(pkt->entries[entries].name, "\tC6Main Ships");
            }

            /* We're done with this ship, increment the counter */
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

static int send_pc_ship_list(ship_client_t *c, ship_t *s, uint16_t menu_code) {
    uint8_t *sendbuf = get_sendbuf();
    pc_ship_list_pkt *pkt = (pc_ship_list_pkt *)sendbuf;
    int len = 0x30, entries = 0, j;
    iconv_t ic = iconv_open("UTF-16LE", "ASCII");
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;
    miniship_t *i;
    char tmp[18], tmp2[3];

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
    pkt->hdr.pkt_type = SHIP_LIST_TYPE;

    /* Fill in the "DATABASE/JP" entry */
    memset(&pkt->entries[0], 0, 0x1C);
    pkt->entries[0].menu_id = LE32(0x00000005);
    pkt->entries[0].item_id = 0;
    pkt->entries[0].flags = LE16(0x0004);
    memcpy(pkt->entries[0].name, "D\0A\0T\0A\0B\0A\0S\0E\0/\0J\0P\0", 22);
    entries = 1;

    TAILQ_FOREACH(i, &s->ships, qentry) {
        if(i->ship_id && i->menu_code == menu_code) {
            if((i->flags & LOGIN_FLAG_GMONLY) &&
               !(c->privilege & CLIENT_PRIV_GLOBAL_GM)) {
                continue;
            }

            if((i->flags & (LOGIN_FLAG_NOV1 << c->version))) {
                continue;
            }

            /* Clear the new entry */
            memset(&pkt->entries[entries], 0, 0x2C);

            /* Copy the ship's information to the packet. */
            pkt->entries[entries].menu_id = LE32(0x00000005);
            pkt->entries[entries].item_id = LE32(i->ship_id);
            pkt->entries[entries].flags = 0;

            /* Convert the name to UTF-16 */
            in = strlen(i->name) + 1;
            out = 0x22;
            inptr = i->name;
            outptr = (char *)pkt->entries[entries].name;
            iconv(ic, &inptr, &in, &outptr, &out);

            ++entries;
            len += 0x2C;
        }
    }

    /* Fill in the menu codes */
    for(j = 0; j < s->mccount; ++j) {
        if(s->menu_codes[j] != menu_code) {
            tmp2[0] = (char)(s->menu_codes[j]);
            tmp2[1] = (char)(s->menu_codes[j] >> 8);
            tmp2[2] = '\0';

            /* Make sure the values are in-bounds */
            if((tmp2[0] || tmp2[1]) &&
               (!isalpha(tmp2[0]) || !isalpha(tmp2[1]))) {
                continue;
            }

            /* Clear out the ship information */
            memset(&pkt->entries[entries], 0, 0x2C);

            /* Fill in what we have */
            pkt->entries[entries].menu_id = LE32((0x00000005 |
                                                  (s->menu_codes[j] << 8)));
            pkt->entries[entries].item_id = LE32(0x00000000);
            pkt->entries[entries].flags = LE16(0x0000);

            /* Create the name string (UTF-8) */
            if(tmp2[0] && tmp2[1]) {
                sprintf(tmp, "\tC6%s Ship List", tmp2);
            }
            else {
                strcpy(tmp, "\tC6Main Ships");
            }

            /* And convert to UTF-16 */
            in = strlen(tmp);
            out = 0x22;
            inptr = tmp;
            outptr = (char *)pkt->entries[entries].name;
            iconv(ic, &inptr, &in, &outptr, &out);

            /* We're done with this ship, increment the counter */
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

int send_ship_list(ship_client_t *c, ship_t *s, uint16_t menu_code) {
    /* Call the appropriate function. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            return send_dc_ship_list(c, s, menu_code);

        case CLIENT_VERSION_PC:
            return send_pc_ship_list(c, s, menu_code);
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
    pkt->pkt_type = GAME_COMMAND2_TYPE;
    pkt->flags = c->client_id;
    pkt->pkt_len = LE16(0x000C);

    /* Fill in the stuff that will make us warp. */
    sendbuf[4] = SUBCMD_WARP;
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
    pkt->pkt_type = GAME_COMMAND2_TYPE;
    pkt->flags = c->client_id;
    pkt->pkt_len = LE16(0x000C);

    /* Fill in the stuff that will make us warp. */
    sendbuf[4] = SUBCMD_WARP;
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
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            return send_dc_warp(c, area);

        case CLIENT_VERSION_PC:
            return send_pc_warp(c, area);
    }

    return -1;
}

int send_lobby_warp(lobby_t *l, uint8_t area) {
    int i;

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL) {
            pthread_mutex_lock(&l->clients[i]->mutex);

            /* Call the appropriate function. */
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV2:
                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_EP3:
                    send_dc_warp(l->clients[i], area);
                    break;

                case CLIENT_VERSION_PC:
                    send_pc_warp(l->clients[i], area);
                    break;
            }

            pthread_mutex_unlock(&l->clients[i]->mutex);
        }
    }

    return 0;
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
    pkt->hdr.pkt_type = CHOICE_OPTION_TYPE;
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
    ICONV_CONST char *inptr;
    char *outptr;

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
    pkt->hdr.pkt_type = CHOICE_OPTION_TYPE;
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
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            return send_dc_choice_search(c);

        case CLIENT_VERSION_PC:
            return send_pc_choice_search(c);
    }

    return -1;
}

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

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Search any local ships. */
    for(j = 0; j < cfg->ship_count; ++j) {
        s = ships[j];
        for(i = 0; i < s->cfg->blocks; ++i) {
            b = s->blocks[i];

            if(b && b->run) {
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
                    else if(it->pl->v1.level < minlvl ||
                            it->pl->v1.level > maxlvl) {
                        continue;
                    }
                    else if(cl != 0 && it->pl->v1.ch_class != cl - 1) {
                        continue;
                    }
                    else if(it == c) {
                        continue;
                    }
                    else if(it->version > CLIENT_VERSION_PC) {
                        continue;
                    }

                    /* If we get here, they match the search. Fill in the
                       entry. */
                    memset(&pkt->entries[entries], 0, 0xD4);
                    pkt->entries[entries].guildcard = LE32(it->guildcard);

                    /* Everything here is ISO-8859-1 already... so no need to
                       iconv anything in here */
                    strcpy(pkt->entries[entries].name, it->pl->v1.name);
                    sprintf(pkt->entries[entries].cl_lvl, "%s Lvl %d\n",
                            classes[it->pl->v1.ch_class], it->pl->v1.level + 1);
                    sprintf(pkt->entries[entries].location, "%s,BLOCK%02d,%s",
                            it->cur_lobby->name, it->cur_block->b,
                            s->cfg->name);

                    pkt->entries[entries].ip = a;
                    pkt->entries[entries].port = LE16(b->dc_port);
                    pkt->entries[entries].menu_id = LE32(0xFFFFFFFF);
                    pkt->entries[entries].item_id =
                        LE32(it->cur_lobby->lobby_id);

                    len += 0xD4;
                    ++entries;
                }

                pthread_mutex_unlock(&b->mutex);
            }
        }
    }

    /* Put in a blank entry at the end... */
    memset(&pkt->entries[entries], 0, 0xD4);
    len += 0xD4;

    /* Fill in the header. */
    pkt->hdr.pkt_type = CHOICE_REPLY_TYPE;
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
    ICONV_CONST char *inptr;
    char *outptr;
    ship_t *s;
    block_t *b;
    ship_client_t *it;
    char tmp[64];

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Set up the converting stuff. */
    ic = iconv_open("UTF-16LE", "ISO-8859-1");

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Search any local ships. */
    for(j = 0; j < cfg->ship_count; ++j) {
        s = ships[j];
        for(i = 0; i < s->cfg->blocks; ++i) {
            b = s->blocks[i];

            if(b && b->run) {
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
                    else if(it->pl->v1.level < minlvl ||
                            it->pl->v1.level > maxlvl) {
                        continue;
                    }
                    else if(cl != 0 && it->pl->v1.ch_class != cl - 1) {
                        continue;
                    }
                    else if(it == c) {
                        continue;
                    }
                    else if(it->version > CLIENT_VERSION_PC) {
                        continue;
                    }

                    /* If we get here, they match the search. Fill in the
                       entry. */
                    memset(&pkt->entries[entries], 0, 0x154);
                    pkt->entries[entries].guildcard = LE32(it->guildcard);

                    in = strlen(it->pl->v1.name) + 1;
                    out = 0x20;
                    inptr = it->pl->v1.name;
                    outptr = (char *)pkt->entries[entries].name;
                    iconv(ic, &inptr, &in, &outptr, &out);

                    in = sprintf(tmp, "%s Lvl %d\n",
                                 classes[it->pl->v1.ch_class],
                                 it->pl->v1.level + 1) + 1;
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
                    pkt->entries[entries].item_id =
                        LE32(it->cur_lobby->lobby_id);

                    len += 0x154;
                    ++entries;
                }

                pthread_mutex_unlock(&b->mutex);
            }
        }
    }

    /* Put in a blank entry at the end... */
    memset(&pkt->entries[entries], 0, 0x154);
    len += 0x154;

    iconv_close(ic);

    /* Fill in the header. */
    pkt->hdr.pkt_type = CHOICE_REPLY_TYPE;
    pkt->hdr.pkt_len = LE16(len);
    pkt->hdr.flags = entries;

    return crypt_send(c, len, sendbuf);
}

static int send_gc_choice_reply(ship_client_t *c, dc_choice_set_t *search,
                                int minlvl, int maxlvl, int cl, in_addr_t a) {
    uint8_t *sendbuf = get_sendbuf();
    dc_choice_reply_t *pkt = (dc_choice_reply_t *)sendbuf;
    uint16_t len = 4;
    uint8_t entries = 0;
    int i, j;
    ship_t *s;
    block_t *b;
    ship_client_t *it;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Search any local ships. */
    for(j = 0; j < cfg->ship_count; ++j) {
        s = ships[j];
        for(i = 0; i < s->cfg->blocks; ++i) {
            b = s->blocks[i];

            if(b && b->run) {
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
                    else if(it->pl->v1.level < minlvl ||
                            it->pl->v1.level > maxlvl) {
                        continue;
                    }
                    else if(cl != 0 && it->pl->v1.ch_class != cl - 1) {
                        continue;
                    }
                    else if(it == c) {
                        continue;
                    }
                    else if(c->version != CLIENT_VERSION_GC) {
                        continue;
                    }

                    /* If we get here, they match the search. Fill in the
                       entry. */
                    memset(&pkt->entries[entries], 0, 0xD4);
                    pkt->entries[entries].guildcard = LE32(it->guildcard);

                    /* Everything here is ISO-8859-1 already, so no need to
                       iconv */
                    strcpy(pkt->entries[entries].name, it->pl->v1.name);
                    sprintf(pkt->entries[entries].cl_lvl, "%s Lvl %d\n",
                            classes[it->pl->v1.ch_class], it->pl->v1.level + 1);
                    sprintf(pkt->entries[entries].location, "%s,BLOCK%02d,%s",
                            it->cur_lobby->name, it->cur_block->b,
                            s->cfg->name);

                    pkt->entries[entries].ip = a;
                    pkt->entries[entries].port = LE16(b->gc_port);
                    pkt->entries[entries].menu_id = LE32(0xFFFFFFFF);
                    pkt->entries[entries].item_id =
                        LE32(it->cur_lobby->lobby_id);

                    len += 0xD4;
                    ++entries;
                }

                pthread_mutex_unlock(&b->mutex);
            }
        }
    }

    /* Put in a blank entry at the end... */
    memset(&pkt->entries[entries], 0, 0xD4);
    len += 0xD4;

    /* Fill in the header. */
    pkt->hdr.pkt_type = CHOICE_REPLY_TYPE;
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
            minlvl = c->pl->v1.level - 5;
            maxlvl = c->pl->v1.level + 5;
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

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            return send_gc_choice_reply(c, search, minlvl, maxlvl, cl, addr);
    }

    return -1;
}

static int send_pc_simple_mail_dc(ship_client_t *c, dc_simple_mail_pkt *p) {
    uint8_t *sendbuf = get_sendbuf();
    pc_simple_mail_pkt *pkt = (pc_simple_mail_pkt *)sendbuf;
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;
    int i;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Set up the converting stuff. */
    if(p->stuff[1] == 'J') {
        ic = iconv_open("UTF-16LE", "SHIFT_JIS");
    }
    else {
        ic = iconv_open("UTF-16LE", "ISO-8859-1");
    }

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Scrub the buffer. */
    memset(pkt, 0, PC_SIMPLE_MAIL_LENGTH);

    /* Fill in the header. */
    pkt->hdr.pkt_type = SIMPLE_MAIL_TYPE;
    pkt->hdr.flags = 0;
    pkt->hdr.pkt_len = LE16(PC_SIMPLE_MAIL_LENGTH);

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
    return crypt_send(c, PC_SIMPLE_MAIL_LENGTH, sendbuf);
}

static int send_dc_simple_mail_pc(ship_client_t *c, pc_simple_mail_pkt *p) {
    uint8_t *sendbuf = get_sendbuf();
    dc_simple_mail_pkt *pkt = (dc_simple_mail_pkt *)sendbuf;
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;
    int i;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Set up the converting stuff. */
    if(p->stuff[2] == 'J') {
        ic = iconv_open("SHIFT_JIS", "UTF-16LE");
    }
    else {
        ic = iconv_open("ISO-8859-1", "UTF-16LE");
    }

    if(ic == (iconv_t)-1) {
        perror("iconv_open");
        return -1;
    }

    /* Scrub the buffer. */
    memset(pkt, 0, DC_SIMPLE_MAIL_LENGTH);

    /* Fill in the header. */
    pkt->hdr.pkt_type = SIMPLE_MAIL_TYPE;
    pkt->hdr.flags = 0;
    pkt->hdr.pkt_len = LE16(DC_SIMPLE_MAIL_LENGTH);

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
    return crypt_send(c, DC_SIMPLE_MAIL_LENGTH, sendbuf);
}

/* Send a simple mail packet, doing any needed transformations. */
int send_simple_mail(int version, ship_client_t *c, dc_pkt_hdr_t *pkt) {
    switch(version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
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

/* Send the lobby's info board to the client. */
static int send_gc_infoboard(ship_client_t *c, lobby_t *l) {
    int i;
    uint8_t *sendbuf = get_sendbuf();
    gc_read_info_pkt *pkt = (gc_read_info_pkt *)sendbuf;
    int entries = 0, size = 4;
    ship_client_t *c2;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL && l->clients[i] != c) {
            c2 = l->clients[i];
            pthread_mutex_lock(&c2->mutex);

            memset(&pkt->entries[entries], 0, 0xBC);
            strcpy(pkt->entries[entries].name, c2->pl->v1.name);

            if(c2->infoboard) {
                strcpy(pkt->entries[entries].msg, c2->infoboard);
            }
            else if(c2->version == CLIENT_VERSION_DCV1) {
                strcpy(pkt->entries[entries].msg, "\tEPSO DCv1 Client");
            }
            else if(c2->version == CLIENT_VERSION_DCV2) {
                strcpy(pkt->entries[entries].msg, "\tEPSO DCv2 Client");
            }
            else if(c2->version == CLIENT_VERSION_PC) {
                strcpy(pkt->entries[entries].msg, "\tEPSOPC Client");
            }
            else if(c2->version == CLIENT_VERSION_GC) {
                strcpy(pkt->entries[entries].msg, "\tEPSOGC Client");
            }
            else if(c2->version == CLIENT_VERSION_EP3) {
                strcpy(pkt->entries[entries].msg, "\tEPSO Ep3 Client");
            }

            ++entries;
            size += 0xBC;

            pthread_mutex_unlock(&c2->mutex);
        }
    }

    /* Fill in the header. */
    pkt->hdr.pkt_type = GC_INFOBOARD_REQ_TYPE;
    pkt->hdr.flags = entries;
    pkt->hdr.pkt_len = LE16(size);

    return crypt_send(c, size, sendbuf);
}

int send_infoboard(ship_client_t *c, lobby_t *l) {
    switch(c->version) {
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            return send_gc_infoboard(c, l);
    }

    return -1;
}

/* Utilities for C-Rank stuff... */
static void copy_c_rank_gc(gc_c_rank_update_pkt *pkt, int entry,
                           ship_client_t *s) {
    int j;

    switch(s->version) {
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            pkt->entries[entry].client_id = LE32(s->client_id);
            memcpy(pkt->entries[entry].c_rank, s->c_rank, 0x0118);
            break;

        case CLIENT_VERSION_DCV2:
            pkt->entries[entry].client_id = LE32(s->client_id);

            memset(pkt->entries[entry].c_rank, 0, 0x0118);

            pkt->entries[entry].unk1 = (s->pl->v2.c_rank.part.unk1 >> 16) |
                (s->pl->v2.c_rank.part.unk1 << 16);

            /* Copy the rank over. */
            memcpy(pkt->entries[entry].string, s->pl->v2.c_rank.part.string,
                   0x0C);

            /* Copy the times for the levels and battle stuff over... */
            memcpy(pkt->entries[entry].times,
                   s->pl->v2.c_rank.part.times, 9 * sizeof(uint32_t));
            memcpy(pkt->entries[entry].battle,
                   s->pl->v2.c_rank.part.battle, 7 * sizeof(uint32_t));
            break;

        case CLIENT_VERSION_PC:
            pkt->entries[entry].client_id = LE32(s->client_id);

            memset(pkt->entries[entry].c_rank, 0, 0x0118);

            pkt->entries[entry].unk1 = s->pl->pc.c_rank.part.unk1;

            /* Copy the rank over. */
            for(j = 0; j < 0x0C; ++j) {
                pkt->entries[entry].string[j] =
                    (char)LE16(s->pl->pc.c_rank.part.string[j]);
            }

            /* Copy the times for the levels and battle stuff over... */
            memcpy(pkt->entries[entry].times, s->pl->pc.c_rank.part.times,
                   9 * sizeof(uint32_t));
            memcpy(pkt->entries[entry].battle, s->pl->pc.c_rank.part.battle,
                   7 * sizeof(uint32_t));
            break;

        default:
            memset(pkt->entries[entry].c_rank, 0, 0x0118);
    }
}
static void copy_c_rank_dc(dc_c_rank_update_pkt *pkt, int entry,
                           ship_client_t *s) {
    int j;

    switch(s->version) {
        case CLIENT_VERSION_DCV2:
            pkt->entries[entry].client_id = LE32(s->client_id);
            memcpy(pkt->entries[entry].c_rank, s->c_rank, 0xB8);
            break;

        case CLIENT_VERSION_PC:
            pkt->entries[entry].client_id = LE32(s->client_id);

            memset(pkt->entries[entry].c_rank, 0, 0xB8);

            /* This is a bit hackish.... */
            pkt->entries[entry].unk1 = s->pl->pc.c_rank.part.unk1;

            /* Copy the rank over. */
            for(j = 0; j < 0x0C; ++j) {
                pkt->entries[entry].string[j] =
                    (char)LE16(s->pl->pc.c_rank.part.string[j]);
            }

            /* Copy the times for the levels and battle stuff over... */
            memcpy(pkt->entries[entry].times, s->pl->pc.c_rank.part.times,
                   9 * sizeof(uint32_t));
            memcpy(pkt->entries[entry].battle, s->pl->pc.c_rank.part.battle,
                   7 * sizeof(uint32_t));
            break;

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            pkt->entries[entry].client_id = LE32(s->client_id);

            memset(pkt->entries[entry].c_rank, 0, 0xB8);

            pkt->entries[entry].unk1 = (s->pl->v3.c_rank.part.unk1 >> 16) |
                (s->pl->v3.c_rank.part.unk1 << 16);

            /* Copy the rank over. */
            memcpy(pkt->entries[entry].string, s->pl->v3.c_rank.part.string,
                   0x0C);

            /* Copy the times for the levels and battle stuff over... */
            memcpy(pkt->entries[entry].times,
                   s->pl->v3.c_rank.part.times, 9 * sizeof(uint32_t));
            memcpy(pkt->entries[entry].battle,
                   s->pl->v3.c_rank.part.battle, 7 * sizeof(uint32_t));
            break;

        default:
            memset(pkt->entries[entry].c_rank, 0, 0xB8);
    }
}

static void copy_c_rank_pc(pc_c_rank_update_pkt *pkt, int entry,
                           ship_client_t *s) {
    int j;

    switch(s->version) {
        case CLIENT_VERSION_PC:
            pkt->entries[entry].client_id = LE32(s->client_id);
            memcpy(pkt->entries[entry].c_rank, s->c_rank, 0xF0);
            break;

        case CLIENT_VERSION_DCV2:
            pkt->entries[entry].client_id = LE32(s->client_id);

            memset(pkt->entries[entry].c_rank, 0, 0xF0);

            pkt->entries[entry].unk1 = s->pl->v2.c_rank.part.unk1;

            /* Copy the rank over. */
            for(j = 0; j < 0x0C; ++j) {
                pkt->entries[entry].string[j] =
                    LE16(s->pl->v2.c_rank.part.string[j]);
            }

            /* Copy the times for the levels and battle stuff over... */
            memcpy(pkt->entries[entry].times, s->pl->v2.c_rank.part.times,
                   9 * sizeof(uint32_t));
            memcpy(pkt->entries[entry].battle, s->pl->v2.c_rank.part.battle,
                   7 * sizeof(uint32_t));
            break;

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            pkt->entries[entry].client_id = LE32(s->client_id);

            memset(pkt->entries[entry].c_rank, 0, 0xF0);

            pkt->entries[entry].unk1 = (s->pl->v3.c_rank.part.unk1 >> 16) |
                (s->pl->v3.c_rank.part.unk1 << 16);

            /* Copy the rank over. */
            for(j = 0; j < 0x0C; ++j) {
                pkt->entries[entry].string[j] =
                    LE16(s->pl->v3.c_rank.part.string[j]);
            }

            /* Copy the times for the levels and battle stuff over... */
            memcpy(pkt->entries[entry].times,
                   s->pl->v3.c_rank.part.times, 9 * sizeof(uint32_t));
            memcpy(pkt->entries[entry].battle,
                   s->pl->v3.c_rank.part.battle, 7 * sizeof(uint32_t));
            break;

        default:
            memset(pkt->entries[entry].c_rank, 0, 0xF0);
    }
}

/* Send the lobby's C-Rank data to the client. */
static int send_gc_lobby_c_rank(ship_client_t *c, lobby_t *l) {
    int i;
    uint8_t *sendbuf = get_sendbuf();
    gc_c_rank_update_pkt *pkt = (gc_c_rank_update_pkt *)sendbuf;
    int entries = 0, size = 4;
    ship_client_t *c2;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL) {
            c2 = l->clients[i];
            pthread_mutex_lock(&c2->mutex);

            /* Copy over this character's data... */
            if(c2->c_rank) {
                copy_c_rank_gc(pkt, entries, c2);
                ++entries;
                size += 0x011C;
            }

            pthread_mutex_unlock(&c2->mutex);
        }
    }

    /* Fill in the header. */
    pkt->hdr.pkt_type = C_RANK_TYPE;
    pkt->hdr.flags = entries;
    pkt->hdr.pkt_len = LE16(size);

    return crypt_send(c, size, sendbuf);
}

static int send_dc_lobby_c_rank(ship_client_t *c, lobby_t *l) {
    int i;
    uint8_t *sendbuf = get_sendbuf();
    dc_c_rank_update_pkt *pkt = (dc_c_rank_update_pkt *)sendbuf;
    int entries = 0, size = 4;
    ship_client_t *c2;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL) {
            c2 = l->clients[i];
            pthread_mutex_lock(&c2->mutex);

            /* Copy this character's data... */
            if(c2->c_rank) {
                copy_c_rank_dc(pkt, entries, c2);
                ++entries;
                size += 0xBC;
            }

            pthread_mutex_unlock(&c2->mutex);
        }
    }

    /* Fill in the header. */
    pkt->hdr.pkt_type = C_RANK_TYPE;
    pkt->hdr.flags = entries;
    pkt->hdr.pkt_len = LE16(size);

    return crypt_send(c, size, sendbuf);
}

static int send_pc_lobby_c_rank(ship_client_t *c, lobby_t *l) {
    int i;
    uint8_t *sendbuf = get_sendbuf();
    pc_c_rank_update_pkt *pkt = (pc_c_rank_update_pkt *)sendbuf;
    int entries = 0, size = 4;
    ship_client_t *c2;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL) {
            c2 = l->clients[i];
            pthread_mutex_lock(&c2->mutex);

            /* Copy over this character's data... */
            if(c2->c_rank) {
                copy_c_rank_pc(pkt, entries, c2);
                ++entries;
                size += 0xF4;
            }

            pthread_mutex_unlock(&c2->mutex);
        }
    }

    /* Fill in the header. */
    pkt->hdr.pkt_type = C_RANK_TYPE;
    pkt->hdr.flags = entries;
    pkt->hdr.pkt_len = LE16(size);

    return crypt_send(c, size, sendbuf);
}

int send_lobby_c_rank(ship_client_t *c, lobby_t *l) {
    switch(c->version) {
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            return send_gc_lobby_c_rank(c, l);

        case CLIENT_VERSION_DCV2:
            return send_dc_lobby_c_rank(c, l);

        case CLIENT_VERSION_PC:
            return send_pc_lobby_c_rank(c, l);
    }

    /* Don't send to unsupporting clients. */
    return 0;
}

/* Send a C-Rank update for a single client to the whole lobby. */
static int send_gc_c_rank_update(ship_client_t *d, ship_client_t *s) {
    uint8_t *sendbuf = get_sendbuf();
    gc_c_rank_update_pkt *pkt = (gc_c_rank_update_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Copy the data. */
    copy_c_rank_gc(pkt, 0, s);

    /* Fill in the header. */
    pkt->hdr.pkt_type = C_RANK_TYPE;
    pkt->hdr.flags = 1;
    pkt->hdr.pkt_len = LE16(0x0120);

    return crypt_send(d, 0x0120, sendbuf);
}

static int send_dc_c_rank_update(ship_client_t *d, ship_client_t *s) {
    uint8_t *sendbuf = get_sendbuf();
    dc_c_rank_update_pkt *pkt = (dc_c_rank_update_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Copy the data. */
    copy_c_rank_dc(pkt, 0, s);

    /* Fill in the header. */
    pkt->hdr.pkt_type = C_RANK_TYPE;
    pkt->hdr.flags = 1;
    pkt->hdr.pkt_len = LE16(0xC0);

    return crypt_send(d, 0xC0, sendbuf);
}

static int send_pc_c_rank_update(ship_client_t *d, ship_client_t *s) {
    uint8_t *sendbuf = get_sendbuf();
    pc_c_rank_update_pkt *pkt = (pc_c_rank_update_pkt *)sendbuf;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Copy the data. */
    copy_c_rank_pc(pkt, 0, s);

    /* Fill in the header. */
    pkt->hdr.pkt_type = C_RANK_TYPE;
    pkt->hdr.flags = 1;
    pkt->hdr.pkt_len = LE16(0xF8);

    return crypt_send(d, 0xF8, sendbuf);
}

int send_c_rank_update(ship_client_t *c, lobby_t *l) {
    int i;

    /* Don't even bother if we don't have something to send. */
    if(!c->c_rank) {
        return 0;
    }

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] != NULL && l->clients[i] != c) {
            pthread_mutex_lock(&l->clients[i]->mutex);

            /* Call the appropriate function. */
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_EP3:
                    send_gc_c_rank_update(l->clients[i], c);
                    break;

                case CLIENT_VERSION_DCV2:
                    send_dc_c_rank_update(l->clients[i], c);
                    break;

                case CLIENT_VERSION_PC:
                    send_pc_c_rank_update(l->clients[i], c);
                    break;
            }

            pthread_mutex_unlock(&l->clients[i]->mutex);
        }
    }

    return 0;
}

/* Send a statistics mod packet to a client. */
static int send_dc_mod_stat(ship_client_t *d, ship_client_t *s, int stat,
                            int amt) {
    uint8_t *sendbuf = get_sendbuf();
    subcmd_pkt_t *pkt = (subcmd_pkt_t *)sendbuf;
    int len = 4;

    /* Verify we got the sendbuf. */
    if(!sendbuf) {
        return -1;
    }

    /* Fill in the main part of the packet */
    while(amt > 0) {
        sendbuf[len++] = SUBCMD_CHANGE_STAT;
        sendbuf[len++] = 2;
        sendbuf[len++] = s->client_id;
        sendbuf[len++] = 0;
        sendbuf[len++] = 0;
        sendbuf[len++] = 0;
        sendbuf[len++] = stat;
        sendbuf[len++] = (amt > 0xFF) ? 0xFF : amt;
        amt -= 0xFF;
    }

    /* Fill in the header */
    if(d->version == CLIENT_VERSION_DCV1 || d->version == CLIENT_VERSION_DCV2 ||
       d->version == CLIENT_VERSION_GC || d->version == CLIENT_VERSION_EP3) {
        pkt->hdr.dc.pkt_type = GAME_COMMAND0_TYPE;
        pkt->hdr.dc.flags = 0;
        pkt->hdr.dc.pkt_len = LE16(len);
    }
    else {
        pkt->hdr.pc.pkt_type = GAME_COMMAND0_TYPE;
        pkt->hdr.dc.flags = 0;
        pkt->hdr.pc.pkt_len = LE16(len);
    }

    /* Send the packet away */
    return crypt_send(d, len, sendbuf);
}

int send_lobby_mod_stat(lobby_t *l, ship_client_t *c, int stat, int amt) {
    int i;

    /* Don't send these to default lobbies, ever */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return 0;
    }

    /* Make sure the request is sane */
    if(stat < SUBCMD_STAT_HPDOWN || stat > SUBCMD_STAT_TPUP || amt < 1 ||
       amt > 2040) {
        return 0;
    }

    pthread_mutex_lock(&l->mutex);

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i]) {
            pthread_mutex_lock(&l->clients[i]->mutex);

            /* Call the appropriate function. */
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                case CLIENT_VERSION_PC:
                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_EP3:
                    send_dc_mod_stat(l->clients[i], c, stat, amt);
                    break;
            }

            pthread_mutex_unlock(&l->clients[i]->mutex);
        }
    }

    pthread_mutex_unlock(&l->mutex);

    return 0;
}

/* Send an Episode 3 Jukebox music change packet to the lobby. */
int send_lobby_ep3_jukebox(lobby_t *l, uint16_t music) {
    ep3_jukebox_pkt pkt;
    int i;

    /* Fill in the packet first... */
    pkt.hdr.pkt_type = EP3_COMMAND_TYPE;
    pkt.hdr.flags = EP3_COMMAND_JUKEBOX_SET;
    pkt.hdr.pkt_len = LE16(0x0010);
    pkt.unk1 = LE32(0x0000012C);
    pkt.unk2 = LE32(0x000008E8);
    pkt.unk3 = LE16(0x0000);
    pkt.music = LE16(music);

    pthread_mutex_lock(&l->mutex);

    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i]) {
            pthread_mutex_lock(&l->clients[i]->mutex);

            /* Send to the client if they're on Episode 3. */
            if(l->clients[i]->version == CLIENT_VERSION_EP3) {
                send_pkt_dc(l->clients[i], (dc_pkt_hdr_t *)&pkt);
            }

            pthread_mutex_unlock(&l->clients[i]->mutex);
        }
    }

    pthread_mutex_unlock(&l->mutex);

    return 0;
}
