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

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

typedef struct pmt_guard_v2 {
    uint16_t index;
    uint16_t unused1;
    uint16_t base_dfp;
    uint16_t base_evp;
    uint8_t unused2[2];
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

#undef PACKED

int pmt_read_v2(const char *fn);
int pmt_v2_enabled(void);

void pmt_cleanup(void);

int pmt_lookup_guard_v2(uint32_t code, pmt_guard_v2_t *rv);

#endif /* !PMTDATA_H */
