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

#ifndef SUBCMD_H
#define SUBCMD_H

#include "clients.h"

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

/* General format of a subcommand packet. */
typedef struct subcmd_pkt {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t data[0];
} PACKED subcmd_pkt_t;

/* Guild card send packet (Dreamcast). */
typedef struct subcmd_dc_gcsend {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unused;
    uint32_t tag;
    uint32_t guildcard;
    char name[24];
    char text[88];
    uint8_t unused2;
    uint8_t one[2];
    uint8_t section;
    uint8_t char_class;
    uint8_t padding[3];
} PACKED subcmd_dc_gcsend_t;

/* Guild card send packet (PC). */
typedef struct subcmd_pc_gcsend {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unused;
    uint32_t tag;
    uint32_t guildcard;
    uint16_t name[24];
    uint16_t text[88];
    uint32_t padding;
    uint8_t one[2];
    uint8_t section;
    uint8_t char_class;
} PACKED subcmd_pc_gcsend_t;

typedef struct subcmd_itemreq {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unused;
    uint8_t area;
    uint8_t unk1;
    uint16_t req;
    float x;
    float y;
    uint32_t unk2[2];
} PACKED subcmd_itemreq_t;

typedef struct subcmd_itemgen {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unused;
    uint8_t area;
    uint8_t what;
    uint16_t req;
    float x;
    float y;
    uint32_t unk1;
    uint32_t item[3];
    uint32_t unk2;
    uint32_t item2[2];
} PACKED subcmd_itemgen_t;

typedef struct subcmd_levelup {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint16_t atp;
    uint16_t mst;
    uint16_t evp;
    uint16_t hp;
    uint16_t dfp;
    uint16_t ata;
    uint32_t level;
} subcmd_levelup_t;

#undef PACKED

/* Subcommand types we care about (0x62/0x6D). */
#define SUBCMD_GUILDCARD    0x06
#define SUBCMD_ITEMREQ      0x60

/* Subcommand types we might care about (0x60). */
#define SUBCMD_LEVELUP      0x30
#define SUBCMD_ITEMDROP     0x5F

/* Handle a 0x62/0x6D packet. */
int subcmd_handle_one(ship_client_t *c, subcmd_pkt_t *pkt);

/* Handle a 0x60 packet. */
int subcmd_handle_bcast(ship_client_t *c, subcmd_pkt_t *pkt);

#endif /* !SUBCMD_H */
