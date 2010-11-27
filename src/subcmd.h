/*
    Sylverant Ship Server
    Copyright (C) 2009 Lawrence Sebald

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

#ifndef SUBCMD_H
#define SUBCMD_H

#include "clients.h"

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

/* General format of a subcommand packet. */
typedef struct subcmd_pkt {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
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
    uint8_t one;
    uint8_t language;
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
    uint8_t one;
    uint8_t language;
    uint8_t section;
    uint8_t char_class;
} PACKED subcmd_pc_gcsend_t;

/* Guild card send packet (Gamecube). */
typedef struct subcmd_gc_gcsend {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unused;
    uint32_t tag;
    uint32_t guildcard;
    char name[24];
    char text[104];
    uint32_t padding;
    uint8_t one;
    uint8_t language;
    uint8_t section;
    uint8_t char_class;
} PACKED subcmd_gc_gcsend_t;

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
} PACKED subcmd_levelup_t;

/* Packet used to take an item from the bank. */
typedef struct subcmd_take_item {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint32_t data_l[3];
    uint32_t item_id;
    uint32_t data2_l;
    uint32_t unk;
} PACKED subcmd_take_item_t;

/* Packet used when a client takes damage. */
typedef struct subcmd_take_damage {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint16_t unk1;
    uint16_t hp_rem;
    uint32_t unk2[2];
} PACKED subcmd_take_damage_t;

/* Packet used after a client uses a tech. */
typedef struct subcmd_used_tech {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint8_t tech;
    uint8_t unused2;
    uint8_t level;
    uint8_t unused3;
} PACKED subcmd_used_tech_t;

/* Packet used when a client drops an item from their inventory */
typedef struct subcmd_drop_item {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint16_t unk;
    uint16_t area;
    uint32_t item_id;
    float x;
    float y;
    float z;
} PACKED subcmd_drop_item_t;

/* Packet used to destroy an item on the map */
typedef struct subcmd_destroy_item {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint32_t item_id;
    uint32_t amount;
} PACKED subcmd_destroy_item_t;

/* Packet used when dropping part of a stack of items */
typedef struct subcmd_drop_stack {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint16_t area;
    uint16_t unk;
    float x;
    float z;
    uint32_t item[3];
    uint32_t item_id;
    uint32_t item2;
    uint32_t two;
} PACKED subcmd_drop_stack_t;

/* Packet used to update other people when a player warps to another area */
typedef struct subcmd_set_area {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint8_t area;
    uint8_t unused2[3];
} PACKED subcmd_set_area_t;

/* Packets used to set a user's position */
typedef struct subcmd_set_pos {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint32_t unk;
    float w;
    float x;
    float y;
    float z;
} PACKED subcmd_set_pos_t;

/* Packet used for moving around */
typedef struct subcmd_move {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    float x;
    float z;
    uint32_t unused2;   /* Not present in 0x42 */
} PACKED subcmd_move_t;

/* Packet used to teleport to a specified position */
typedef struct subcmd_teleport {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    float x;
    float y;
    float z;
    float w;
} PACKED subcmd_teleport_t;

#undef PACKED

/* Subcommand types we care about (0x62/0x6D). */
#define SUBCMD_GUILDCARD    0x06
#define SUBCMD_PICK_UP      0x5A    /* Sent to leader when picking up item */
#define SUBCMD_ITEMREQ      0x60

/* Subcommand types we might care about (0x60). */
#define SUBCMD_TELEPORT     0x17
#define SUBCMD_SET_AREA     0x1F
#define SUBCMD_EQUIP        0x25
#define SUBCMD_REMOVE_EQUIP 0x26
#define SUBCMD_USE_ITEM     0x27
#define SUBCMD_DELETE_ITEM  0x29    /* Selling, deposit in bank, etc */
#define SUBCMD_DROP_ITEM    0x2A
#define SUBCMD_TAKE_ITEM    0x2B
#define SUBCMD_LEVELUP      0x30
#define SUBCMD_SET_POS_3E   0x3E
#define SUBCMD_SET_POS_3F   0x3F
#define SUBCMD_MOVE_SLOW    0x40
#define SUBCMD_MOVE_FAST    0x42
#define SUBCMD_USED_TECH    0x48
#define SUBCMD_TAKE_DAMAGE1 0x4B
#define SUBCMD_TAKE_DAMAGE2 0x4C
#define SUBCMD_DEL_MAP_ITEM 0x59    /* Sent by leader when item picked up */
#define SUBCMD_DROP_STACK   0x5D
#define SUBCMD_BUY          0x5E
#define SUBCMD_ITEMDROP     0x5F
#define SUBCMD_DESTROY_ITEM 0x63
#define SUBCMD_BURST_DONE   0x72
#define SUBCMD_WARP         0x94
#define SUBCMD_CHANGE_STAT  0x9A

/* The commands OK to send during bursting (0x62/0x6D). These are named for the
   order in which they're sent, hence why the names are out of order... */
#define SUBCMD_BURST2       0x6B
#define SUBCMD_BURST3       0x6C
#define SUBCMD_BURST1       0x6D
#define SUBCMD_BURST4       0x6E
#define SUBCMD_BURST5       0x6F
#define SUBCMD_BURST7       0x70
#define SUBCMD_BURST6       0x71

/* The commands OK to send during bursting (0x60) */
#define SUBCMD_UNK_3B       0x3B
#define SUBCMD_UNK_7C       0x7C

/* Stats to use with Subcommand 0x9A (0x60) */
#define SUBCMD_STAT_HPDOWN  0
#define SUBCMD_STAT_TPDOWN  1
#define SUBCMD_STAT_MESDOWN 2
#define SUBCMD_STAT_HPUP    3
#define SUBCMD_STAT_TPUP    4

/* Handle a 0x62/0x6D packet. */
int subcmd_handle_one(ship_client_t *c, subcmd_pkt_t *pkt);

/* Handle a 0x60 packet. */
int subcmd_handle_bcast(ship_client_t *c, subcmd_pkt_t *pkt);

#endif /* !SUBCMD_H */
