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

#ifndef PLAYER_H
#define PLAYER_H

#include <inttypes.h>

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

/* The header attached before the player data when sending to a lobby client. */
typedef struct dc_player_hdr {
    uint32_t tag;
    uint32_t guildcard;
    uint32_t ip_addr;
    uint32_t client_id;
    char name[16];
} PACKED dc_player_hdr_t;

typedef struct pc_player_hdr {
    uint32_t tag;
    uint32_t guildcard;
    uint32_t ip_addr;
    uint32_t client_id;
    uint16_t name[16];
} PACKED pc_player_hdr_t;

/* These structures heavily based on those in newserv. */
typedef struct item {
    uint16_t equipped;
    uint16_t tech;
    uint32_t flags;

    union {
        uint8_t data_b[12];
        uint16_t data_w[6];
        uint32_t data_l[3];
    };

    uint32_t item_id;

    union {
        uint8_t data2_b[4];
        uint16_t data2_w[2];
        uint32_t data2_l;
    };
} PACKED item_t;

typedef struct inventory {
    uint8_t item_count;
    uint8_t hpmats_used;
    uint8_t tpmats_used;
    uint8_t language;
    item_t items[30];
} PACKED inventory_t;

typedef struct v1_player {
    inventory_t inv;
    uint16_t atp;
    uint16_t mst;
    uint16_t evp;
    uint16_t hp;
    uint16_t dfp;
    uint16_t ata;
    uint16_t lck;
    uint16_t unk1;
    uint32_t unk2[2];
    uint32_t level;
    uint32_t exp;
    uint32_t meseta;
    char name[16];
    uint32_t unk3[2];
    uint32_t name_color;
    uint8_t model;
    uint8_t unused[15];
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
    uint8_t config[0x48];
    uint8_t techniques[0x14];
} PACKED v1_player_t;

typedef struct v2_player {
    inventory_t inv;
    uint16_t atp;
    uint16_t mst;
    uint16_t evp;
    uint16_t hp;
    uint16_t dfp;
    uint16_t ata;
    uint16_t lck;
    uint16_t unk1;
    uint32_t unk2[2];
    uint32_t level;
    uint32_t exp;
    uint32_t meseta;
    char name[16];
    uint32_t unk3[2];
    uint32_t name_color;
    uint8_t model;
    uint8_t unused[15];
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
    uint8_t config[0x48];
    uint8_t techniques[0x14];
    uint32_t padding;
    union {
        uint8_t all[0xB8];
        struct {
            uint32_t unk1;
            char string[0x0C];
            uint8_t unk2[0x68];
            uint32_t times[9];
            uint32_t battle[7];
        } part;
    } c_rank;
    uint32_t unk4[6];
} PACKED v2_player_t;

typedef struct pc_player {
    inventory_t inv;
    uint16_t atp;
    uint16_t mst;
    uint16_t evp;
    uint16_t hp;
    uint16_t dfp;
    uint16_t ata;
    uint16_t lck;
    uint16_t unk1;
    uint32_t unk2[2];
    uint32_t level;
    uint32_t exp;
    uint32_t meseta;
    char name[16];
    uint32_t unk3[2];
    uint32_t name_color;
    uint8_t model;
    uint8_t unused[15];
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
    uint8_t config[0x48];
    uint8_t techniques[0x14];
    uint32_t padding;
    union {
        uint8_t all[0xF0];
        struct {
            uint32_t unk1;
            uint16_t string[0x0C];
            uint8_t unk2[0x94];
            uint32_t times[9];
            uint32_t battle[7];
        } part;
    } c_rank;
    uint8_t unk4[0x98];
} PACKED pc_player_t;

typedef struct v3_player {
    inventory_t inv;
    uint16_t atp;
    uint16_t mst;
    uint16_t evp;
    uint16_t hp;
    uint16_t dfp;
    uint16_t ata;
    uint16_t lck;
    uint16_t unk1;
    uint32_t unk2[2];
    uint32_t level;
    uint32_t exp;
    uint32_t meseta;
    char name[16];
    uint32_t unk3[2];
    uint32_t name_color;
    uint8_t model;
    uint8_t unused[15];
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
    uint8_t config[0x48];
    uint8_t techniques[0x14];
    uint32_t padding;
    union {
        uint8_t all[0x0118];
        struct {
            uint32_t unk1;              /* Flip the words for dc/pc! */
            uint32_t times[9];
            uint8_t unk2[0xB0];
            char string[0x0C];
            uint8_t unk3[0x18];
            uint32_t battle[7];
        } part;
    } c_rank;
    uint32_t unk4[6];
    char infoboard[0x012C];     /* Probably shorter, but this fills it out. */
} PACKED v3_player_t;

#undef PACKED

typedef union {
    v1_player_t v1;
    v2_player_t v2;
    pc_player_t pc;
    v3_player_t v3;
} player_t;

#endif /* !PLAYER_H */
