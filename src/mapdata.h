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

#ifndef MAPDATA_H
#define MAPDATA_H

#include <stdint.h>

#include <sylverant/config.h>

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

/* Battle parameter entry (enemy type, basically) used by the server for Blue
   Burst. This is basically the same structure as newserv's BATTLE_PARAM or
   Tethealla's BATTLEPARAM, which makes sense considering that all of us are
   using basically the same data files (directly from the game itself). */
typedef struct bb_battle_param {
    uint16_t atp;
    uint16_t psv;
    uint16_t evp;
    uint16_t hp;
    uint16_t dfp;
    uint16_t ata;
    uint16_t lck;
    uint16_t esp;
    uint8_t unk[12];
    uint32_t exp;
    uint32_t diff;
} PACKED bb_battle_param_t;

/* A single entry in the level table. */
typedef struct bb_level_entry {
    uint8_t atp;
    uint8_t mst;
    uint8_t evp;
    uint8_t hp;
    uint8_t dfp;
    uint8_t ata;
    uint8_t unk[2];
    uint32_t exp;
} PACKED bb_level_entry_t;

/* Level-up information table from PlyLevelTbl.prs */
typedef struct bb_level_table {
    struct {
        uint16_t atp;
        uint16_t mst;
        uint16_t evp;
        uint16_t hp;
        uint16_t dfp;
        uint16_t ata;
        uint16_t lck;
    } start_stats[12];
    uint32_t unk[12];
    bb_level_entry_t levels[12][200];
} PACKED bb_level_table_t;

/* Enemy data in the map files. This the same as the ENEMY_ENTRY struct from
   newserv. */
typedef struct bb_map_enemy {
    uint32_t base;
    uint16_t reserved0;
    uint16_t num_clones;
    uint32_t reserved[11];
    uint32_t reserved12;
    uint32_t reserved13;
    uint32_t reserved14;
    uint32_t skin;
    uint32_t reserved15;
} PACKED bb_map_enemy_t;

/* Enemy data as used in the game. */
typedef struct bb_game_enemy {
    uint32_t bp_entry;
    uint16_t rt_index;
    uint8_t clients_hit;
    uint8_t last_client;
} bb_game_enemy_t;

typedef struct bb_game_enemies {
    uint32_t count;
    bb_game_enemy_t *enemies;
} bb_game_enemies_t;

typedef struct bb_parsed_map {
    uint32_t map_count;
    uint32_t variation_count;
    bb_game_enemies_t *data;
} bb_parsed_map_t;

#undef PACKED

#ifndef LOBBY_DEFINED
#define LOBBY_DEFINED
struct lobby;
typedef struct lobby lobby_t;
#endif

/* Player levelup data */
extern bb_level_table_t char_stats;

int bb_read_params(sylverant_ship_t *cfg);
void bb_free_params(void);

int bb_load_game_enemies(lobby_t *l);
void bb_free_game_enemies(lobby_t *l);

#endif /* !MAPDATA_H */
