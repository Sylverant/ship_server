/*
    Sylverant Ship Server
    Copyright (C) 2012, 2013 Lawrence Sebald

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

#ifndef PMTDATA_H
#define PMTDATA_H

#include <stdint.h>

#include <sylverant/mtwist.h>

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

typedef struct pmt_weapon_v2 {
    uint32_t index;
    uint8_t classes;
    uint8_t unused1;
    uint16_t atp_min;
    uint16_t atp_max;
    uint16_t atp_req;
    uint16_t mst_req;
    uint16_t ata_req;
    uint8_t max_grind;
    uint8_t photon;
    uint8_t special;
    uint8_t ata;
    uint8_t stat_boost;
    uint8_t unused2[3];
} PACKED pmt_weapon_v2_t;

typedef struct pmt_weapon_gc {
    uint32_t index;
    uint16_t model;
    uint16_t skin;
    uint8_t unused1;
    uint8_t classes;
    uint16_t atp_min;
    uint16_t atp_max;
    uint16_t atp_req;
    uint16_t mst_req;
    uint16_t ata_req;
    uint16_t mst;
    uint8_t max_grind;
    uint8_t photon;
    uint8_t special;
    uint8_t ata;
    uint8_t stat_boost;
    uint8_t projectile;
    uint8_t ptrail_1_x;
    uint8_t ptrail_1_y;
    uint8_t ptrail_2_x;
    uint8_t ptrail_2_y;
    uint8_t ptype;
    uint8_t unk[3];
    uint8_t unused2[2];
    uint8_t tech_boost;
    uint8_t combo_type;
} PACKED pmt_weapon_gc_t;

typedef struct pmt_weapon_bb {
    uint32_t index;
    uint16_t model;
    uint16_t skin;
    uint16_t team_points;
    uint16_t unused1;
    uint8_t classes;
    uint8_t unused2;
    uint16_t atp_min;
    uint16_t atp_max;
    uint16_t atp_req;
    uint16_t mst_req;
    uint16_t ata_req;
    uint16_t mst;
    uint8_t max_grind;
    uint8_t photon;
    uint8_t special;
    uint8_t ata;
    uint8_t stat_boost;
    uint8_t projectile;
    uint8_t ptrail_1_x;
    uint8_t ptrail_1_y;
    uint8_t ptrail_2_x;
    uint8_t ptrail_2_y;
    uint8_t ptype;
    uint8_t unk[3];
    uint8_t unused3[2];
    uint8_t tech_boost;
    uint8_t combo_type;
} PACKED pmt_weapon_bb_t;

typedef struct pmt_guard_v2 {
    uint32_t index;
    uint16_t base_dfp;
    uint16_t base_evp;
    uint16_t unused1;
    uint8_t equip_flag;
    uint8_t unused2;
    uint8_t level_req;
    uint8_t efr;
    uint8_t eth;
    uint8_t eic;
    uint8_t edk;
    uint8_t elt;
    uint8_t dfp_range;
    uint8_t evp_range;
    uint32_t unused3;
} PACKED pmt_guard_v2_t;

typedef struct pmt_guard_gc {
    uint32_t index;
    uint16_t model;
    uint16_t skin;
    uint16_t base_dfp;
    uint16_t base_evp;
    uint16_t unused1;
    uint8_t unused2;
    uint8_t equip_flag;
    uint8_t level_req;
    uint8_t efr;
    uint8_t eth;
    uint8_t eic;
    uint8_t edk;
    uint8_t elt;
    uint8_t dfp_range;
    uint8_t evp_range;
    uint32_t unused3;
} PACKED pmt_guard_gc_t;

typedef struct pmt_guard_bb {
    uint32_t index;
    uint16_t model;
    uint16_t skin;
    uint16_t team_points;
    uint16_t unused1;
    uint16_t base_dfp;
    uint16_t base_evp;
    uint16_t unused2;
    uint8_t equip_flag;
    uint8_t unused3;
    uint8_t level_req;
    uint8_t efr;
    uint8_t eth;
    uint8_t eic;
    uint8_t edk;
    uint8_t elt;
    uint8_t dfp_range;
    uint8_t evp_range;
    uint32_t unused4;
} PACKED pmt_guard_bb_t;

typedef struct pmt_unit_v2 {
    uint32_t index;
    uint16_t stat;
    uint16_t amount;
    uint8_t pm_range;
    uint8_t unused[3];
} PACKED pmt_unit_v2_t;

typedef struct pmt_unit_gc {
    uint32_t index;
    uint16_t model;
    uint16_t skin;
    uint16_t stat;
    uint16_t amount;
    uint8_t pm_range;
    uint8_t unused[3];
} PACKED pmt_unit_gc_t;

typedef struct pmt_unit_bb {
    uint32_t index;
    uint16_t model;
    uint16_t skin;
    uint16_t team_points;
    uint16_t unused1;
    uint16_t stat;
    uint16_t amount;
    uint8_t pm_range;
    uint8_t unused2[3];
} PACKED pmt_unit_bb_t;

typedef struct pmt_mag_v2 {
    uint32_t index;
    uint16_t feed_table;
    uint8_t photon_blast;
    uint8_t activation;
    uint8_t on_pb_full;
    uint8_t on_low_hp;
    uint8_t on_death;
    uint8_t on_boss;
    uint8_t pb_full_flag;
    uint8_t low_hp_flag;
    uint8_t death_flag;
    uint8_t boss_flag;
    uint8_t classes;
    uint8_t unused[3];
} PACKED pmt_mag_v2_t;

typedef struct pmt_mag_gc {
    uint32_t index;
    uint16_t model;
    uint16_t skin;
    uint16_t feed_table;
    uint8_t photon_blast;
    uint8_t activation;
    uint8_t on_pb_full;
    uint8_t on_low_hp;
    uint8_t on_death;
    uint8_t on_boss;
    uint8_t pb_full_flag;
    uint8_t low_hp_flag;
    uint8_t death_flag;
    uint8_t boss_flag;
    uint8_t classes;
    uint8_t unused[3];
} PACKED pmt_mag_gc_t;

typedef struct pmt_mag_bb {
    uint32_t index;
    uint16_t model;
    uint16_t skin;
    uint16_t team_points;
    uint16_t unused1;
    uint16_t feed_table;
    uint8_t photon_blast;
    uint8_t activation;
    uint8_t on_pb_full;
    uint8_t on_low_hp;
    uint8_t on_death;
    uint8_t on_boss;
    uint8_t pb_full_flag;
    uint8_t low_hp_flag;
    uint8_t death_flag;
    uint8_t boss_flag;
    uint8_t classes;
    uint8_t unused2[3];
} PACKED pmt_mag_bb_t;

#undef PACKED

int pmt_read_v2(const char *fn, int norestrict);
int pmt_read_gc(const char *fn, int norestrict);
int pmt_read_bb(const char *fn, int norestrict);
int pmt_v2_enabled(void);
int pmt_gc_enabled(void);
int pmt_bb_enabled(void);

void pmt_cleanup(void);

int pmt_lookup_weapon_v2(uint32_t code, pmt_weapon_v2_t *rv);
int pmt_lookup_guard_v2(uint32_t code, pmt_guard_v2_t *rv);
int pmt_lookup_unit_v2(uint32_t code, pmt_unit_v2_t *rv);

uint8_t pmt_lookup_stars_v2(uint32_t code);
int pmt_random_unit_v2(uint8_t max, uint32_t item[4],
                       struct mt19937_state *rng);

int pmt_lookup_weapon_gc(uint32_t code, pmt_weapon_gc_t *rv);
int pmt_lookup_guard_gc(uint32_t code, pmt_guard_gc_t *rv);
int pmt_lookup_unit_gc(uint32_t code, pmt_unit_gc_t *rv);

uint8_t pmt_lookup_stars_gc(uint32_t code);
int pmt_random_unit_gc(uint8_t max, uint32_t item[4],
                       struct mt19937_state *rng);

int pmt_lookup_weapon_bb(uint32_t code, pmt_weapon_bb_t *rv);
int pmt_lookup_guard_bb(uint32_t code, pmt_guard_bb_t *rv);
int pmt_lookup_unit_bb(uint32_t code, pmt_unit_bb_t *rv);

int pmt_random_unit_bb(uint8_t max, uint32_t item[4],
                       struct mt19937_state *rng);
uint8_t pmt_lookup_stars_bb(uint32_t code);

#endif /* !PMTDATA_H */
