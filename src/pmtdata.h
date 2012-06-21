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

#ifndef PMTDATA_H
#define PMTDATA_H

#include <stdint.h>

#include <sylverant/mtwist.h>

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

typedef struct pmt_weapon_v2 {
    uint16_t index;
    uint16_t unused1;
    uint8_t classes;
    uint8_t unused2;
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
    uint8_t unused3[3];
} PACKED pmt_weapon_v2_t;

typedef struct pmt_guard_v2 {
    uint16_t index;
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
} PACKED pmt_guard_v2_t;

typedef struct pmt_unit_v2 {
    uint16_t index;
    uint16_t unused1;
    uint16_t stat;
    uint16_t amount;
    uint8_t pm_range;
    uint8_t unused2[3];
} PACKED pmt_unit_v2_t;

typedef struct pmt_mag_v2 {
    uint16_t index;
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
} PACKED pmt_mag_v2_t;

#undef PACKED

int pmt_read_v2(const char *fn, int norestrict);
int pmt_v2_enabled(void);

void pmt_cleanup(void);

int pmt_lookup_weapon_v2(uint32_t code, pmt_weapon_v2_t *rv);
int pmt_lookup_guard_v2(uint32_t code, pmt_guard_v2_t *rv);
int pmt_lookup_unit_v2(uint32_t code, pmt_unit_v2_t *rv);

uint8_t pmt_lookup_stars_v2(uint32_t code);
int pmt_random_unit_v2(uint8_t max, uint32_t item[4],
                       struct mt19937_state *rng);

#endif /* !PMTDATA_H */
