/*
    Sylverant Ship Server
    Copyright (C) 2012 Lawrence Sebald

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

#ifndef PTDATA_H
#define PTDATA_H

#include <stdint.h>

#include "lobby.h"

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

#define BOX_TYPE_WEAPON     0
#define BOX_TYPE_ARMOR      1
#define BOX_TYPE_SHIELD     2
#define BOX_TYPE_UNIT       3
#define BOX_TYPE_TOOL       4
#define BOX_TYPE_MESETA     5
#define BOX_TYPE_NOTHING    6

/* Entry in one of the ItemPT files. Mostly adapted from Tethealla... In the
   file itself, each of these fields is stored in big-endian byter order.
   Some of this data also comes from a post by Lee on the PSOBB Eden forums:
   http://edenserv.net/forum/viewtopic.php?p=19305#p19305 */
typedef struct pt_v3_entry {
    uint8_t weapon_ratio[12];               /* 0x0000 */
    int8_t weapon_minrank[12];              /* 0x000C */
    uint8_t weapon_maxfloor[12];            /* 0x0018 */
    uint8_t power_pattern[9][4];            /* 0x0024 */
    uint16_t percent_pattern[23][6];        /* 0x0048 */
    uint8_t area_pattern[30];               /* 0x015C */
    uint8_t percent_attachment[6][10];      /* 0x017A */
    uint8_t element_ranking[10];            /* 0x01B6 */
    uint8_t element_probability[10];        /* 0x01C0 */
    uint8_t armor_ranking[5];               /* 0x01CA */
    uint8_t slot_ranking[5];                /* 0x01CF */
    uint8_t unit_level[10];                 /* 0x01D4 */
    uint16_t tool_frequency[28][10];        /* 0x01DE */
    uint8_t tech_frequency[19][10];         /* 0x040E */
    int8_t tech_levels[19][20];             /* 0x04CC */
    uint8_t enemy_dar[100];                 /* 0x0648 */
    uint16_t enemy_meseta[100][2];          /* 0x06AC */
    int8_t enemy_drop[100];                 /* 0x083C */
    uint16_t box_meseta[10][2];             /* 0x08A0 */
    uint8_t box_drop[7][10];                /* 0x08C8 */
    uint16_t padding;                       /* 0x090E */
    uint32_t pointers[18];                  /* 0x0910 */
    uint32_t armor_level;                   /* 0x0958 */
    /* There is a bit more data here... Dunno what it is. No reason to store it
       if I don't know how to use it. */
} pt_v3_entry_t;

/* Entry in one of the ItemPT files. This version corresponds to the files that
   were used in PSOv2. The names of the fields were taken from the above
   structure. In the file itself, each of these fields is stored in
   little-endian byte order. */
typedef struct pt_v2_entry {
    uint8_t weapon_ratio[12];               /* 0x0000 */
    int8_t weapon_minrank[12];              /* 0x000C */
    uint8_t weapon_maxfloor[12];            /* 0x0018 */
    uint8_t power_pattern[9][4];            /* 0x0024 */
    uint8_t percent_pattern[23][5];         /* 0x0048 */
    uint8_t area_pattern[30];               /* 0x00BB */
    uint8_t percent_attachment[6][10];      /* 0x00D9 */
    uint8_t element_ranking[10];            /* 0x0115 */
    uint8_t element_probability[10];        /* 0x011F */
    uint8_t armor_ranking[5];               /* 0x0129 */
    uint8_t slot_ranking[5];                /* 0x012E */
    uint8_t unit_level[10];                 /* 0x0133 */
    uint8_t padding;                        /* 0x013D */
    uint16_t tool_frequency[28][10];        /* 0x013E */
    uint8_t tech_frequency[19][10];         /* 0x036E */
    int8_t tech_levels[19][20];             /* 0x042C */
    uint8_t enemy_dar[100];                 /* 0x05A8 */
    uint16_t enemy_meseta[100][2];          /* 0x060C */
    int8_t enemy_drop[100];                 /* 0x079C */
    uint16_t box_meseta[10][2];             /* 0x0800 */
    uint8_t box_drop[7][10];                /* 0x0828 */
    uint16_t padding2;                      /* 0x086E */
    uint32_t pointers[18];                  /* 0x0870 */
    uint32_t armor_level;                   /* 0x08B8 */
    /* There is a bit more data here... Dunno what it is. No reason to store it
       if I don't know how to use it. */
} pt_v2_entry_t;

/* Read the ItemPT data from a v2-style (ItemPT.afs) file. */
int pt_read_v2(const char *fn);

/* Read the ItemPT data from a v3-style (ItemPT.gsl) file. */
int pt_read_v3(const char *fn);

/* Did we read in a v2 ItemPT? */
int pt_v2_enabled(void);

/* Did we read in a v3 ItemPT? */
int pt_v3_enabled(void);

/* Generate an item drop from the PT data. This version uses the v2 PT data set,
   and thus is appropriate for any version before PSOGC. */
int pt_generate_v2_drop(ship_client_t *c, lobby_t *l, void *r);
int pt_generate_v2_boxdrop(ship_client_t *c, lobby_t *l, void *r);

/* Generate an item drop from the PT data. This version uses the v3 PT data set.
   This function only works for PSOGC. */
int pt_generate_v3_drop(ship_client_t *c, lobby_t *l, void *r);

/* Generate an item drop from the PT data. This version uses the v3 PT data set.
   This function only works for PSOBB. */
int pt_generate_bb_drop(ship_client_t *c, lobby_t *l, void *r);

#endif /* !PTDATA_H */
