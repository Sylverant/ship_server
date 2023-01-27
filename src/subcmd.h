/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2012, 2013, 2015, 2018, 2019,
                  2021, 2023 Lawrence Sebald

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

#include <sylverant/characters.h>

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

typedef struct bb_subcmd_pkt {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t data[0];
} PACKED bb_subcmd_pkt_t;

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

/* Guild card send packet (Xbox). */
typedef struct subcmd_xb_gcsend {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unk;                       /* 0x0D 0xFB */
    uint32_t tag;
    uint32_t guildcard;
    uint64_t xbl_userid;
    char name[24];
    char text[512];                     /* Why so long? */
    uint8_t one;
    uint8_t language;
    uint8_t section;
    uint8_t char_class;
} PACKED subcmd_xb_gcsend_t;

/* Guild card send packet (Blue Burst) */
typedef struct subcmd_bb_gc_send {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unused;
    uint32_t guildcard;
    uint16_t name[24];
    uint16_t team_name[16];
    uint16_t text[88];
    uint8_t one;
    uint8_t language;
    uint8_t section;
    uint8_t char_class;
} PACKED subcmd_bb_gcsend_t;

/* Request drop from enemy (DC/PC/GC) or box (DC/PC) */
typedef struct subcmd_itemreq {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unused;
    uint8_t area;
    uint8_t pt_index;
    uint16_t req;
    float x;
    float y;
    uint32_t unk2[2];
} PACKED subcmd_itemreq_t;

/* Request drop from enemy (Blue Burst) */
typedef struct subcmd_bb_itemreq {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unused;
    uint8_t area;
    uint8_t pt_index;
    uint16_t req;
    float x;
    float y;
    uint32_t unk2[2];
} PACKED subcmd_bb_itemreq_t;

/* Request drop from box (GC) */
typedef struct subcmd_bitemreq {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unk1;                      /* 0x80 0x3F?*/
    uint8_t area;
    uint8_t pt_index;                   /* Always 0x30 */
    uint16_t req;
    float x;
    float y;
    uint32_t unk2[2];
    uint16_t unk3;
    uint16_t unk4;                      /* 0x80 0x3F? */
    uint32_t unused[3];                 /* All zeroes? */
} PACKED subcmd_bitemreq_t;

/* Request drop from box (Blue Burst) */
typedef struct subcmd_bb_bitemreq {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unk1;                      /* 0x80 0x3F?*/
    uint8_t area;
    uint8_t pt_index;                   /* Always 0x30 */
    uint16_t req;
    float x;
    float y;
    uint32_t unk2[2];
    uint16_t unk3;
    uint16_t unk4;                      /* 0x80 0x3F? */
    uint32_t unused[3];                 /* All zeroes? */
} PACKED subcmd_bb_bitemreq_t;

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
    uint32_t item_id;
    uint32_t item2[2];
} PACKED subcmd_itemgen_t;

typedef struct subcmd_bb_itemgen {
    bb_pkt_hdr_t hdr;
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
    uint32_t item_id;
    uint32_t item2;
} PACKED subcmd_bb_itemgen_t;

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

typedef struct subcmd_bb_take_damage {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint16_t unk1;
    uint16_t hp_rem;
    uint32_t unk2[2];
} PACKED subcmd_bb_take_damage_t;

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

typedef struct subcmd_bb_used_tech {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint8_t tech;
    uint8_t unused2;
    uint8_t level;
    uint8_t unused3;
} PACKED subcmd_bb_used_tech_t;

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

typedef struct subcmd_bb_drop_item {
    bb_pkt_hdr_t hdr;
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
} PACKED subcmd_bb_drop_item_t;

/* Packet used to destroy an item on the map or to remove an item from a
   client's inventory */
typedef struct subcmd_destroy_item {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint32_t item_id;
    uint32_t amount;
} PACKED subcmd_destroy_item_t;

typedef struct subcmd_bb_destroy_item {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint32_t item_id;
    uint32_t amount;
} PACKED subcmd_bb_destroy_item_t;

typedef struct subcmd_pick_up {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint32_t item_id;
    uint8_t area;
    uint8_t unused2[3];
} PACKED subcmd_pick_up_t;

typedef struct subcmd_bb_pick_up {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint32_t item_id;
    uint8_t area;
    uint8_t unused2[3];
} PACKED subcmd_bb_pick_up_t;

typedef struct subcmd_bb_destroy_map_item {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint8_t client_id2;
    uint8_t unused2;
    uint8_t area;
    uint8_t unused3;
    uint32_t item_id;
} PACKED subcmd_bb_destroy_map_item_t;

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

typedef struct subcmd_bb_drop_stack {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint32_t area;
    float x;
    float z;
    uint32_t item[3];
    uint32_t item_id;
    uint32_t item2;
} PACKED subcmd_bb_drop_stack_t;

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

typedef struct subcmd_bb_set_area {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint8_t area;
    uint8_t unused2[3];
} PACKED subcmd_bb_set_area_t;

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

typedef struct subcmd_bb_set_pos {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint32_t unk;
    float w;
    float x;
    float y;
    float z;
} PACKED subcmd_bb_set_pos_t;

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

typedef struct subcmd_bb_move {
    bb_pkt_hdr_t hdr;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    float x;
    float z;
    uint32_t unused2;   /* Not present in 0x42 */
} PACKED subcmd_bb_move_t;

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

/* Packet used when buying an item from the shop */
typedef struct subcmd_buy {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint32_t item[3];
    uint32_t item_id;
    uint32_t meseta;
} PACKED subcmd_buy_t;

/* Packet used when an item has been used */
typedef struct subcmd_use_item {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint32_t item_id;
} PACKED subcmd_use_item_t;

/* Packet used for word select */
typedef struct subcmd_word_select {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t client_id_gc;
    uint8_t num_words;
    uint8_t unused1;
    uint8_t ws_type;
    uint8_t unused2;
    uint16_t words[12];
} PACKED subcmd_word_select_t;

typedef struct subcmd_bb_word_select {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t client_id_gc;
    uint8_t num_words;
    uint8_t unused1;
    uint8_t ws_type;
    uint8_t unused2;
    uint16_t words[12];
} PACKED subcmd_bb_word_select_t;

/* Packet used for grave data in C-Mode (Dreamcast) */
typedef struct subcmd_dc_grave {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t unused1;                    /* Unused? */
    uint8_t unused2;
    uint8_t client_id;
    uint8_t unused3;
    uint16_t unk0;
    uint32_t unk1;                      /* Matches with C-Data unk1 */
    char string[0x0C];                  /* Challenge Rank string */
    uint8_t unk2[0x24];                 /* Always blank? */
    uint16_t grave_unk4;
    uint16_t deaths;
    uint32_t coords_time[5];
    char team[20];
    char message[24];
    uint32_t times[9];
    uint32_t unk;
} PACKED subcmd_dc_grave_t;

/* Packet used for grave data in C-Mode (PC) */
typedef struct subcmd_pc_grave {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t unused1;                    /* Unused? */
    uint8_t unused2;
    uint8_t client_id;
    uint8_t unused3;
    uint16_t unk0;
    uint32_t unk1;                      /* Matches with C-Data unk1 */
    uint16_t string[0x0C];              /* Challenge Rank string */
    uint8_t unk2[0x24];                 /* Always blank? */
    uint16_t grave_unk4;
    uint16_t deaths;
    uint32_t coords_time[5];
    uint16_t team[20];
    uint16_t message[24];
    uint32_t times[9];
    uint32_t unk;
} PACKED subcmd_pc_grave_t;

/* Packet used for requesting a shop's inventory. (Blue Burst) */
typedef struct subcmd_bb_shop_req {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unk;                       /* Always 0xFFFF? */
    uint8_t shop_type;
    uint8_t padding;
} PACKED subcmd_bb_shop_req_t;

/* Packet used for telling a client a shop's inventory. (Blue Burst) */
typedef struct subcmd_bb_shop_inv {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unused1;                   /* Set to 0 */
    uint8_t shop_type;
    uint8_t num_items;
    uint16_t unused2;                   /* Set to 0 */
    struct {
        uint32_t item_data[3];
        uint32_t reserved;              /* Set to 0xFFFFFFFF */
        uint32_t cost;
    } items[0x18];
    uint32_t unused3[4];                /* Set all to 0 */
} PACKED subcmd_bb_shop_inv_t;

/* Packet sent by the client to buy an item from the shop. (Blue Burst) */
typedef struct subcmd_bb_shop_buy {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unused1;                   /* 0xFFFF (unused?) */
    uint32_t item_id;
    uint8_t unused2;
    uint8_t shop_index;
    uint8_t num_bought;
    uint8_t unused3;
} PACKED subcmd_bb_shop_buy_t;

/* Packet sent by the client to notify that they're dropping part of a stack of
   items and tell where they're dropping them. (Blue Burst) */
typedef struct subcmd_bb_drop_pos {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint32_t area;
    float x;
    float z;
    uint32_t item_id;
    uint32_t amount;
} PACKED subcmd_bb_drop_pos_t;

/* Packet sent to clients to let them know that an item got picked up off of
   the ground. (Blue Burst) */
typedef struct subcmd_bb_create_item {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint32_t item[3];
    uint32_t item_id;
    uint32_t item2;
    uint32_t unused2;
} PACKED subcmd_bb_create_item_t;

/* Packet sent by clients to open the bank menu. (Blue Burst) */
typedef struct subcmd_bb_bank_open {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unused;                    /* 0xFFFF */
    uint32_t unk;                       /* Maybe a checksum or somesuch? */
} PACKED subcmd_bb_bank_open_t;

/* Packet sent to clients to tell them what's in their bank. (Blue Burst) */
typedef struct subcmd_bb_bank_inv {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t unused[3];                  /* No size here, apparently. */
    uint32_t size;
    uint32_t checksum;
    uint32_t item_count;
    uint32_t meseta;
    sylverant_bitem_t items[];
} PACKED subcmd_bb_bank_inv_t;

/* Packet sent by clients to do something at the bank. (Blue Burst) */
typedef struct subcmd_bb_bank_act {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unused;                    /* 0xFFFF */
    uint32_t item_id;
    uint32_t meseta_amount;
    uint8_t action;
    uint8_t item_amount;
    uint16_t unused2;                   /* 0xFFFF */
} PACKED subcmd_bb_bank_act_t;

/* Packet sent by clients to equip/unequip an item. */
typedef struct subcmd_bb_equip {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint32_t item_id;
    uint32_t unk;
} PACKED subcmd_bb_equip_t;

/* Packet sent by clients to sort their inventory. (Blue Burst) */
typedef struct subcmd_bb_sort_inv {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unused;                    /* 0xFFFF */
    uint32_t item_ids[30];
} PACKED subcmd_bb_sort_inv_t;

/* Packet sent to clients to give them experience. (Blue Burst) */
typedef struct subcmd_bb_exp {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint32_t exp;
} PACKED subcmd_bb_exp_t;

/* Packet sent to clients regarding a level up. */
typedef struct subcmd_bb_level {
    bb_pkt_hdr_t hdr;
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
} PACKED subcmd_bb_level_t;

/* Packet sent by Blue Burst clients to request experience after killing an
   enemy. */
typedef struct subcmd_bb_req_exp {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t enemy_id2;
    uint16_t enemy_id;
    uint8_t client_id;
    uint8_t unused;
    uint8_t last_hitter;
    uint8_t unused2[3];
} PACKED subcmd_bb_req_exp_pkt_t;

/* Packet sent by clients to say that a monster has been hit. */
typedef struct subcmd_mhit {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t enemy_id2;
    uint16_t enemy_id;
    uint16_t damage;
    uint32_t flags;
} PACKED subcmd_mhit_pkt_t;

typedef struct subcmd_bb_mhit {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t enemy_id2;
    uint16_t enemy_id;
    uint16_t damage;
    uint32_t flags;
} PACKED subcmd_bb_mhit_pkt_t;

/* Packet sent by clients to say that a box has been hit. */
typedef struct subcmd_bhit {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t box_id2;
    uint32_t unk;
    uint16_t box_id;
    uint16_t unk2;
} PACKED subcmd_bhit_pkt_t;

typedef struct subcmd_bb_bhit {
    bb_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t box_id2;
    uint32_t unk;
    uint16_t box_id;
    uint16_t unk2;
} PACKED subcmd_bb_bhit_pkt_t;

/* Packet sent by clients to say that they killed a monster. Unfortunately, this
   doesn't always get sent (for instance for the Darvants during a Falz fight),
   thus its not actually used in Sylverant for anything. */
typedef struct subcmd_mkill {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t enemy_id;
    uint8_t client_id;
    uint8_t unused;
    uint16_t unk;
} PACKED subcmd_mkill_pkt_t;

/* Packet sent to create a pipe. Most of this, I haven't bothered looking too
   much at... */
typedef struct subcmd_pipe {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unk1;
    uint8_t client_id;
    uint8_t unused;
    uint8_t area_id;
    uint8_t unused2;
    uint32_t unk[5];                    /* Location is in here. */
} PACKED subcmd_pipe_pkt_t;

/* Packet sent when an object is hit by a technique. */
typedef struct subcmd_objhit_tech {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint8_t tech;
    uint8_t unused2;
    uint8_t level;
    uint8_t hit_count;
    struct {
        uint16_t obj_id;    /* 0xtiii -> i: id, t: type (4 object, 1 monster) */
        uint16_t unused;
    } objects[];
} PACKED subcmd_objhit_tech_t;

/* Packet sent when an object is hit by a physical attack. */
typedef struct subcmd_objhit_phys {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint8_t hit_count;
    uint8_t unused2[3];
    struct {
        uint16_t obj_id;    /* 0xtiii -> i: id, t: type (4 object, 1 monster) */
        uint16_t unused;
    } objects[];
} PACKED subcmd_objhit_phys_t;

/* Packet sent in response to a quest register sync (sync_register, sync_let,
   or sync_leti in qedit terminology). */
typedef struct subcmd_sync_reg {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint16_t unk1;          /* Probably unused junk. */
    uint8_t reg_num;
    uint8_t unused;
    uint16_t unk2;          /* Probably unused junk again. */
    uint32_t value;
} PACKED subcmd_sync_reg_t;

/* Packet sent when talking to an NPC on Pioneer 2 (and other purposes). */
typedef struct subcmd_talk_npc {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t size;
    uint8_t client_id;
    uint8_t unused;
    uint16_t unk;           /* Always 0xFFFF for NPCs? */
    uint16_t zero;          /* Always 0? */
    float x;
    float z;
    uint32_t unused2;       /* Always zero? */
} PACKED subcmd_talk_npc_t;

/* Packet used to communicate current state of players in-game while a new
   player is bursting. */
typedef struct subcmd_burst_pldata {
    dc_pkt_hdr_t hdr;
    uint8_t type;
    uint8_t unused1;
    uint16_t unused2;
    uint32_t size_minus_4;  /* ??? */
    uint32_t unk1[2];
    float x;
    float y;
    float z;
    uint8_t unk2[0x54];
    uint32_t tag;
    uint32_t guildcard;
    uint8_t unk3[0x44];
    uint8_t techs[20];
    char name[16];
    uint32_t c_unk1[2];
    uint32_t name_color;
    uint8_t model;
    uint8_t c_unused[15];
    uint32_t name_color_checksum;
    uint8_t section;
    uint8_t ch_class;
    uint8_t v2flags;
    uint8_t version;
    uint32_t v1flags;
    uint16_t costume;
    uint16_t skin;
    uint16_t face;
    uint16_t head;
    uint16_t hair;
    uint16_t hair_r;
    uint16_t hair_g;
    uint16_t hair_b;
    float prop_x;
    float prop_y;
    uint16_t atp;
    uint16_t mst;
    uint16_t evp;
    uint16_t hp;
    uint16_t dfp;
    uint16_t ata;
    uint16_t lck;
    uint16_t c_unk2;
    uint32_t c_unk3[2];
    uint32_t level;
    uint32_t exp;
    uint32_t meseta;
    sylverant_inventory_t inv;
    uint32_t zero;          /* Unused? */
    /* Xbox has a little bit more at the end than other versions... Probably
       safe to just ignore it. */
} PACKED subcmd_burst_pldata_t;

#undef PACKED

/* Subcommand types we care about (0x62/0x6D). */
#define SUBCMD_GUILDCARD    0x06
#define SUBCMD_PICK_UP      0x5A    /* Sent to leader when picking up item */
#define SUBCMD_ITEMREQ      0x60
#define SUBCMD_BITEMREQ     0xA2    /* BB/GC - Request item drop from box */
#define SUBCMD_SHOPREQ      0xB5    /* Blue Burst - Request shop inventory */
#define SUBCMD_SHOPBUY      0xB7    /* Blue Burst - Buy an item from the shop */
#define SUBCMD_OPEN_BANK    0xBB    /* Blue Burst - open the bank menu */
#define SUBCMD_BANK_ACTION  0xBD    /* Blue Burst - do something at the bank */

/* Subcommand types we might care about (0x60/0x6C). */
#define SUBCMD_SYMBOL_CHAT  0x07
#define SUBCMD_HIT_MONSTER  0x0A
#define SUBCMD_HIT_OBJ      0x0B
#define SUBCMD_TELEPORT     0x17
#define SUBCMD_SET_AREA     0x1F
#define SUBCMD_SET_AREA_21  0x21    /* Seems to match 0x1F */
#define SUBCMD_LOAD_22      0x22    /* Related to 0x21 and 0x23... */
#define SUBCMD_FINISH_LOAD  0x23    /* Finished loading to a map, maybe? */
#define SUBCMD_SET_POS_24   0x24    /* Used when starting a quest. */
#define SUBCMD_EQUIP        0x25
#define SUBCMD_REMOVE_EQUIP 0x26
#define SUBCMD_USE_ITEM     0x27
#define SUBCMD_DELETE_ITEM  0x29    /* Selling, deposit in bank, etc */
#define SUBCMD_DROP_ITEM    0x2A    /* Drop full stack or non-stack item */
#define SUBCMD_TAKE_ITEM    0x2B
#define SUBCMD_TALK_NPC     0x2C    /* Maybe this is talking to an NPC? */
#define SUBCMD_DONE_NPC     0x2D    /* Shows up when you're done with an NPC */
#define SUBCMD_LEVELUP      0x30
#define SUBCMD_LOAD_3B      0x3B    /* Something with loading to a map... */
#define SUBCMD_SET_POS_3E   0x3E
#define SUBCMD_SET_POS_3F   0x3F
#define SUBCMD_MOVE_SLOW    0x40
#define SUBCMD_MOVE_FAST    0x42
#define SUBCMD_OBJHIT_PHYS  0x46
#define SUBCMD_OBJHIT_TECH  0x47
#define SUBCMD_USED_TECH    0x48
#define SUBCMD_TAKE_DAMAGE1 0x4B
#define SUBCMD_TAKE_DAMAGE2 0x4C
#define SUBCMD_TALK_SHOP    0x52    /* Talking to someone at a shop */
#define SUBCMD_WARP_55      0x55    /* Something with the principal's warp? */
#define SUBCMD_LOBBY_ACTION 0x58
#define SUBCMD_DEL_MAP_ITEM 0x59    /* Sent by leader when item picked up */
#define SUBCMD_DROP_STACK   0x5D
#define SUBCMD_BUY          0x5E
#define SUBCMD_ITEMDROP     0x5F
#define SUBCMD_DESTROY_ITEM 0x63    /* Sent when game inventory is full */
#define SUBCMD_CREATE_PIPE  0x68
#define SUBCMD_SPAWN_NPC    0x69
#define SUBCMD_BURST_DONE   0x72
#define SUBCMD_WORD_SELECT  0x74
#define SUBCMD_KILL_MONSTER 0x76    /* A monster was killed. */
#define SUBCMD_SYNC_REG     0x77    /* Sent when register is synced in quest */
#define SUBCMD_GOGO_BALL    0x79
#define SUBCMD_CMODE_GRAVE  0x7C
#define SUBCMD_WARP         0x94
#define SUBCMD_CHANGE_STAT  0x9A
#define SUBCMD_LOBBY_CHAIR  0xAB
#define SUBCMD_CHAIR_DIR    0xAF
#define SUBCMD_CHAIR_MOVE   0xB0
#define SUBCMD_SHOPINV      0xB6    /* Blue Burst - shop inventory */
#define SUBCMD_BANK_INV     0xBC    /* Blue Burst - bank inventory */
#define SUBCMD_CREATE_ITEM  0xBE    /* Blue Burst - create new inventory item */
#define SUBCMD_JUKEBOX      0xBF    /* Episode III - Change background music */
#define SUBCMD_GIVE_EXP     0xBF    /* Blue Burst - give experience points */
#define SUBCMD_DROP_POS     0xC3    /* Blue Burst - Drop part of stack coords */
#define SUBCMD_SORT_INV     0xC4    /* Blue Burst - Sort inventory */
#define SUBCMD_MEDIC        0xC5    /* Blue Burst - Use the medical center */
#define SUBCMD_REQ_EXP      0xC8    /* Blue Burst - Request Experience */

/* Subcommands that we might care about in the Dreamcast NTE (0x60) */
#define SUBCMD_DCNTE_SET_AREA       0x1D    /* 0x21 */
#define SUBCMD_DCNTE_FINISH_LOAD    0x1F    /* 0x23 */
#define SUBCMD_DCNTE_SET_POS        0x36    /* 0x3F */
#define SUBCMD_DCNTE_MOVE_SLOW      0x37    /* 0x40 */
#define SUBCMD_DCNTE_MOVE_FAST      0x39    /* 0x42 */
#define SUBCMD_DCNTE_TALK_SHOP      0x46    /* 0x52 */

/* The commands OK to send during bursting (0x62/0x6D). These are named for the
   order in which they're sent, hence why the names are out of order... */
#define SUBCMD_BURST2       0x6B
#define SUBCMD_BURST3       0x6C
#define SUBCMD_BURST1       0x6D
#define SUBCMD_BURST4       0x6E
#define SUBCMD_BURST5       0x6F
#define SUBCMD_BURST_PLDATA 0x70    /* Was SUBCMD_BURST7 */
#define SUBCMD_BURST6       0x71

/* The commands OK to send during bursting (0x60) */
/* 0x3B */
#define SUBCMD_UNK_7C       0x7C

/* Stats to use with Subcommand 0x9A (0x60) */
#define SUBCMD_STAT_HPDOWN  0
#define SUBCMD_STAT_TPDOWN  1
#define SUBCMD_STAT_MESDOWN 2
#define SUBCMD_STAT_HPUP    3
#define SUBCMD_STAT_TPUP    4

/* Actions that can be performed at the bank with Subcommand 0xBD (0x62) */
#define SUBCMD_BANK_ACT_DEPOSIT 0
#define SUBCMD_BANK_ACT_TAKE    1
#define SUBCMD_BANK_ACT_DONE    2
#define SUBCMD_BANK_ACT_CLOSE   3

/* Handle a 0x62/0x6D packet. */
int subcmd_handle_one(ship_client_t *c, subcmd_pkt_t *pkt);
int subcmd_bb_handle_one(ship_client_t *c, bb_subcmd_pkt_t *pkt);

/* Handle a 0x60 packet. */
int subcmd_handle_bcast(ship_client_t *c, subcmd_pkt_t *pkt);
int subcmd_bb_handle_bcast(ship_client_t *c, bb_subcmd_pkt_t *pkt);
int subcmd_dcnte_handle_bcast(ship_client_t *c, subcmd_pkt_t *pkt);

/* Handle an 0xC9/0xCB packet from Episode 3. */
int subcmd_handle_ep3_bcast(ship_client_t *c, subcmd_pkt_t *pkt);

int subcmd_send_lobby_item(lobby_t *l, subcmd_itemreq_t *req,
                           const uint32_t item[4]);
int subcmd_send_bb_lobby_item(lobby_t *l, subcmd_bb_itemreq_t *req,
                              const item_t *item);

int subcmd_send_bb_exp(ship_client_t *c, uint32_t exp);
int subcmd_send_bb_level(ship_client_t *c);

int subcmd_send_pos(ship_client_t *dst, ship_client_t *src);

/* Send a broadcast subcommand to the whole lobby. */
int subcmd_send_lobby_dc(lobby_t *l, ship_client_t *c, subcmd_pkt_t *pkt,
                         int igcheck);
int subcmd_send_lobby_bb(lobby_t *l, ship_client_t *c, bb_subcmd_pkt_t *pkt,
                         int igcheck);
int subcmd_send_lobby_dcnte(lobby_t *l, ship_client_t *c, subcmd_pkt_t *pkt,
                            int igcheck);

/* Stuff dealing with the Dreamcast Network Trial edition */
int subcmd_translate_dc_to_nte(ship_client_t *c, subcmd_pkt_t *pkt);
int subcmd_translate_nte_to_dc(ship_client_t *c, subcmd_pkt_t *pkt);
int subcmd_translate_bb_to_nte(ship_client_t *c, bb_subcmd_pkt_t *pkt);

#endif /* !SUBCMD_H */
