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

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <arpa/inet.h>

#include <sylverant/prs.h>
#include <sylverant/debug.h>
#include <sylverant/mtwist.h>

#include "pmtdata.h"
#include "utils.h"
#include "packets.h"
#include "items.h"

/* PSOv1/PSOv2 data. */
static pmt_weapon_v2_t **weapons = NULL;
static uint32_t *num_weapons = NULL;
static uint32_t num_weapon_types = 0;
static uint32_t weapon_lowest = 0xFFFFFFFF;

static pmt_guard_v2_t **guards = NULL;
static uint32_t *num_guards = NULL;
static uint32_t num_guard_types = 0;
static uint32_t guard_lowest = 0xFFFFFFFF;

static pmt_unit_v2_t *units = NULL;
static uint32_t num_units = 0;
static uint32_t unit_lowest = 0xFFFFFFFF;

static uint8_t *star_table = NULL;
static uint32_t star_max = 0;

/* These three are used in generating random units... */
static uint64_t *units_by_stars = NULL;
static uint32_t *units_with_stars = NULL;
static uint8_t unit_max_stars = 0;

/* PSOGC data. */
static pmt_weapon_gc_t **weapons_gc = NULL;
static uint32_t *num_weapons_gc = NULL;
static uint32_t num_weapon_types_gc = 0;
static uint32_t weapon_lowest_gc = 0xFFFFFFFF;

static pmt_guard_gc_t **guards_gc = NULL;
static uint32_t *num_guards_gc = NULL;
static uint32_t num_guard_types_gc = 0;
static uint32_t guard_lowest_gc = 0xFFFFFFFF;

static pmt_unit_gc_t *units_gc = NULL;
static uint32_t num_units_gc = 0;
static uint32_t unit_lowest_gc = 0xFFFFFFFF;

static uint8_t *star_table_gc = NULL;
static uint32_t star_max_gc = 0;

static uint64_t *units_by_stars_gc = NULL;
static uint32_t *units_with_stars_gc = NULL;
static uint8_t unit_max_stars_gc = 0;

/* PSOBB data. */
static pmt_weapon_bb_t **weapons_bb = NULL;
static uint32_t *num_weapons_bb = NULL;
static uint32_t num_weapon_types_bb = 0;
static uint32_t weapon_lowest_bb = 0xFFFFFFFF;

static pmt_guard_bb_t **guards_bb = NULL;
static uint32_t *num_guards_bb = NULL;
static uint32_t num_guard_types_bb = 0;
static uint32_t guard_lowest_bb = 0xFFFFFFFF;

static pmt_unit_bb_t *units_bb = NULL;
static uint32_t num_units_bb = 0;
static uint32_t unit_lowest_bb = 0xFFFFFFFF;

static uint8_t *star_table_bb = NULL;
static uint32_t star_max_bb = 0;

static uint64_t *units_by_stars_bb = NULL;
static uint32_t *units_with_stars_bb = NULL;
static uint8_t unit_max_stars_bb = 0;

static int have_v2_pmt = 0;
static int have_gc_pmt = 0;
static int have_bb_pmt = 0;

/* The parsing code in here is based on some code/information from Lee. Thanks
   again! */
static int read_ptr_tbl(const uint8_t *pmt, uint32_t sz, uint32_t ptrs[21]) {
    uint32_t tmp;
#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__)
    int i;
#endif

    memcpy(&tmp, pmt + sz - 16, 4);
    tmp = LE32(tmp);

    if(tmp + sizeof(uint32_t) * 21 > sz - 16) {
        debug(DBG_ERROR, "Invalid pointer table location in PMT!\n");
        return -1;
    }

    memcpy(ptrs, pmt + tmp, sizeof(uint32_t) * 21);

#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__)
    for(i = 0; i < 21; ++i) {
        ptrs[i] = LE32(ptrs[i]);
    }
#endif

    return 0;
}

static int read_gcptr_tbl(const uint8_t *pmt, uint32_t sz, uint32_t ptrs[23]) {
    uint32_t tmp;
#if !defined(WORDS_BIGENDIAN) && !defined(__BIG_ENDIAN__)
    int i;
#endif

    memcpy(&tmp, pmt + sz - 16, 4);
    tmp = ntohl(tmp);

    if(tmp + sizeof(uint32_t) * 23 > sz - 16) {
        debug(DBG_ERROR, "Invalid pointer table location in PMT!\n");
        return -1;
    }

    memcpy(ptrs, pmt + tmp, sizeof(uint32_t) * 23);

#if !defined(WORDS_BIGENDIAN) && !defined(__BIG_ENDIAN__)
    for(i = 0; i < 23; ++i) {
        ptrs[i] = ntohl(ptrs[i]);
    }
#endif

    return 0;
}

static int read_bbptr_tbl(const uint8_t *pmt, uint32_t sz, uint32_t ptrs[23]) {
    uint32_t tmp;
#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__)
    int i;
#endif

    memcpy(&tmp, pmt + sz - 16, 4);
    tmp = LE32(tmp);

    if(tmp + sizeof(uint32_t) * 23 > sz - 16) {
        debug(DBG_ERROR, "Invalid pointer table location in PMT!\n");
        return -1;
    }

    memcpy(ptrs, pmt + tmp, sizeof(uint32_t) * 23);

#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__)
    for(i = 0; i < 23; ++i) {
        ptrs[i] = LE32(ptrs[i]);
    }
#endif

    return 0;
}

static int read_v2_weapons(const uint8_t *pmt, uint32_t sz,
                           const uint32_t ptrs[21]) {
    uint32_t cnt, i, values[2], j;

    /* Make sure the pointers are sane... */
    if(ptrs[1] > sz || ptrs[1] > ptrs[11]) {
        debug(DBG_ERROR, "ItemPMT.prs file for v2 has invalid weapon pointers. "
              "Please check it for validity!\n");
        return -1;
    }

    /* Figure out how many tables we have... */
    num_weapon_types = cnt = (ptrs[11] - ptrs[1]) / 8;

    /* Allocate the stuff we need to allocate... */
    if(!(num_weapons = (uint32_t *)malloc(sizeof(uint32_t) * cnt))) {
        debug(DBG_ERROR, "Cannot allocate space for v2 weapon count: %s\n",
              strerror(errno));
        num_weapon_types = 0;
        return -2;
    }

    if(!(weapons = (pmt_weapon_v2_t **)malloc(sizeof(pmt_weapon_v2_t *) *
                                              cnt))) {
        debug(DBG_ERROR, "Cannot allocate space for v2 weapon list: %s\n",
              strerror(errno));
        free(num_weapons);
        num_weapons = NULL;
        num_weapon_types = 0;
        return -3;
    }

    memset(weapons, 0, sizeof(pmt_weapon_v2_t *) * cnt);

    /* Read in each table... */
    for(i = 0; i < cnt; ++i) {
        /* Read the pointer and the size... */
        memcpy(values, pmt + ptrs[1] + (i << 3), sizeof(uint32_t) * 2);
        values[0] = LE32(values[0]);
        values[1] = LE32(values[1]);

        /* Make sure we have enough file... */
        if(values[1] + sizeof(pmt_weapon_v2_t) * values[0] > sz) {
            debug(DBG_ERROR, "ItemPMT.prs file for v2 has weapon table outside "
                  "of file bounds! Please check the file for validity!\n");
            return -4;
        }

        num_weapons[i] = values[0];
        if(!(weapons[i] = (pmt_weapon_v2_t *)malloc(sizeof(pmt_weapon_v2_t) *
                                                    values[0]))) {
            debug(DBG_ERROR, "Cannot allocate space for v2 weapons: %s\n",
                  strerror(errno));
            return -5;
        }

        memcpy(weapons[i], pmt + values[1],
               sizeof(pmt_weapon_v2_t) * values[0]);

        for(j = 0; j < values[0]; ++j) {
#if defined(__BIG_ENDIAN__) || defined(WORDS_BIGENDIAN)
            weapons[i][j].index = LE32(weapons[i][j].index);
            weapons[i][j].atp_min = LE16(weapons[i][j].atp_min);
            weapons[i][j].atp_max = LE16(weapons[i][j].atp_max);
            weapons[i][j].atp_req = LE16(weapons[i][j].atp_req);
            weapons[i][j].mst_req = LE16(weapons[i][j].mst_req);
            weapons[i][j].ata_req = LE16(weapons[i][j].ata_req);
#endif

            if(weapons[i][j].index < weapon_lowest)
                weapon_lowest = weapons[i][j].index;
        }
    }

    return 0;
}

static int read_gc_weapons(const uint8_t *pmt, uint32_t sz,
                           const uint32_t ptrs[23]) {
    uint32_t cnt, i, values[2], j;

    /* Make sure the pointers are sane... */
    if(ptrs[0] > sz || ptrs[0] > ptrs[17]) {
        debug(DBG_ERROR, "ItemPMT.prs file for GC has invalid weapon pointers. "
              "Please check it for validity!\n");
        return -1;
    }

    /* Figure out how many tables we have... */
    num_weapon_types_gc = cnt = (ptrs[17] - ptrs[0]) / 8;

    /* Allocate the stuff we need to allocate... */
    if(!(num_weapons_gc = (uint32_t *)malloc(sizeof(uint32_t) * cnt))) {
        debug(DBG_ERROR, "Cannot allocate space for GC weapon count: %s\n",
              strerror(errno));
        num_weapon_types_gc = 0;
        return -2;
    }

    if(!(weapons_gc = (pmt_weapon_gc_t **)malloc(sizeof(pmt_weapon_gc_t *) *
                                                 cnt))) {
        debug(DBG_ERROR, "Cannot allocate space for GC weapon list: %s\n",
              strerror(errno));
        free(num_weapons_gc);
        num_weapons_gc = NULL;
        num_weapon_types_gc = 0;
        return -3;
    }

    memset(weapons_gc, 0, sizeof(pmt_weapon_gc_t *) * cnt);

    /* Read in each table... */
    for(i = 0; i < cnt; ++i) {
        /* Read the pointer and the size... */
        memcpy(values, pmt + ptrs[0] + (i << 3), sizeof(uint32_t) * 2);
        values[0] = ntohl(values[0]);
        values[1] = ntohl(values[1]);

        /* Make sure we have enough file... */
        if(values[1] + sizeof(pmt_weapon_gc_t) * values[0] > sz) {
            debug(DBG_ERROR, "ItemPMT.prs file for GC has weapon table outside "
                  "of file bounds! Please check the file for validity!\n");
            return -4;
        }

        num_weapons_gc[i] = values[0];
        if(!(weapons_gc[i] = (pmt_weapon_gc_t *)malloc(sizeof(pmt_weapon_gc_t) *
                                                       values[0]))) {
            debug(DBG_ERROR, "Cannot allocate space for GC weapons: %s\n",
                  strerror(errno));
            return -5;
        }

        memcpy(weapons_gc[i], pmt + values[1],
               sizeof(pmt_weapon_gc_t) * values[0]);

        for(j = 0; j < values[0]; ++j) {
#if !defined(__BIG_ENDIAN__) && !defined(WORDS_BIGENDIAN)
            weapons_gc[i][j].index = ntohl(weapons_gc[i][j].index);
            weapons_gc[i][j].model = ntohs(weapons_gc[i][j].model);
            weapons_gc[i][j].skin = ntohs(weapons_gc[i][j].skin);
            weapons_gc[i][j].atp_min = ntohs(weapons_gc[i][j].atp_min);
            weapons_gc[i][j].atp_max = ntohs(weapons_gc[i][j].atp_max);
            weapons_gc[i][j].atp_req = ntohs(weapons_gc[i][j].atp_req);
            weapons_gc[i][j].mst_req = ntohs(weapons_gc[i][j].mst_req);
            weapons_gc[i][j].ata_req = ntohs(weapons_gc[i][j].ata_req);
            weapons_gc[i][j].mst = ntohs(weapons_gc[i][j].mst);
#endif

            if(weapons_gc[i][j].index < weapon_lowest_gc)
                weapon_lowest_gc = weapons_gc[i][j].index;
        }
    }

    return 0;
}

static int read_bb_weapons(const uint8_t *pmt, uint32_t sz,
                           const uint32_t ptrs[23]) {
    uint32_t cnt, i, values[2], j;

    /* Make sure the pointers are sane... */
    if(ptrs[0] > sz || ptrs[0] > ptrs[17]) {
        debug(DBG_ERROR, "ItemPMT.prs file for BB has invalid weapon pointers. "
              "Please check it for validity!\n");
        return -1;
    }

    /* Figure out how many tables we have... */
    num_weapon_types_bb = cnt = (ptrs[17] - ptrs[0]) / 8;

    /* Allocate the stuff we need to allocate... */
    if(!(num_weapons_bb = (uint32_t *)malloc(sizeof(uint32_t) * cnt))) {
        debug(DBG_ERROR, "Cannot allocate space for BB weapon count: %s\n",
              strerror(errno));
        num_weapon_types_bb = 0;
        return -2;
    }

    if(!(weapons_bb = (pmt_weapon_bb_t **)malloc(sizeof(pmt_weapon_bb_t *) *
                                                 cnt))) {
        debug(DBG_ERROR, "Cannot allocate space for BB weapon list: %s\n",
              strerror(errno));
        free(num_weapons_bb);
        num_weapons_bb = NULL;
        num_weapon_types_bb = 0;
        return -3;
    }

    memset(weapons_bb, 0, sizeof(pmt_weapon_bb_t *) * cnt);

    /* Read in each table... */
    for(i = 0; i < cnt; ++i) {
        /* Read the pointer and the size... */
        memcpy(values, pmt + ptrs[0] + (i << 3), sizeof(uint32_t) * 2);
        values[0] = LE32(values[0]);
        values[1] = LE32(values[1]);

        /* Make sure we have enough file... */
        if(values[1] + sizeof(pmt_weapon_bb_t) * values[0] > sz) {
            debug(DBG_ERROR, "ItemPMT.prs file for BB has weapon table outside "
                  "of file bounds! Please check the file for validity!\n");
            return -4;
        }

        num_weapons_bb[i] = values[0];
        if(!(weapons_bb[i] = (pmt_weapon_bb_t *)malloc(sizeof(pmt_weapon_bb_t) *
                                                       values[0]))) {
            debug(DBG_ERROR, "Cannot allocate space for BB weapons: %s\n",
                  strerror(errno));
            return -5;
        }

        memcpy(weapons_bb[i], pmt + values[1],
               sizeof(pmt_weapon_bb_t) * values[0]);

        for(j = 0; j < values[0]; ++j) {
#if defined(__BIG_ENDIAN__) || defined(WORDS_BIGENDIAN)
            weapons_bb[i][j].index = LE32(weapons_bb[i][j].index);
            weapons_bb[i][j].model = LE16(weapons_bb[i][j].model);
            weapons_bb[i][j].skin = LE16(weapons_bb[i][j].skin);
            weapons_bb[i][j].team_ponts = LE16(weapons_bb[i][j].team_points);
            weapons_bb[i][j].atp_min = LE16(weapons_bb[i][j].atp_min);
            weapons_bb[i][j].atp_max = LE16(weapons_bb[i][j].atp_max);
            weapons_bb[i][j].atp_req = LE16(weapons_bb[i][j].atp_req);
            weapons_bb[i][j].mst_req = LE16(weapons_bb[i][j].mst_req);
            weapons_bb[i][j].ata_req = LE16(weapons_bb[i][j].ata_req);
            weapons_bb[i][j].mst = LE16(weapons_bb[i][j].mst);
#endif

            if(weapons_bb[i][j].index < weapon_lowest_bb)
                weapon_lowest_bb = weapons_bb[i][j].index;
        }
    }

    return 0;
}

static int read_v2_guards(const uint8_t *pmt, uint32_t sz,
                          const uint32_t ptrs[21]) {
    uint32_t cnt, i, values[2], j;

    /* Make sure the pointers are sane... */
    if(ptrs[3] > sz || ptrs[2] > ptrs[3]) {
        debug(DBG_ERROR, "ItemPMT.prs file for v2 has invalid guard pointers. "
              "Please check it for validity!\n");
        return -1;
    }

    /* Figure out how many tables we have... */
    num_guard_types = cnt = (ptrs[3] - ptrs[2]) / 8;

    /* Make sure its sane... Should always be 2. */
    if(cnt != 2) {
        debug(DBG_ERROR, "ItemPMT.prs file for v2 does not have two guard "
              "tables. Please check it for validity!\n");
        num_guard_types = 0;
        return -2;
    }

    /* Allocate the stuff we need to allocate... */
    if(!(num_guards = (uint32_t *)malloc(sizeof(uint32_t) * cnt))) {
        debug(DBG_ERROR, "Cannot allocate space for v2 guard count: %s\n",
              strerror(errno));
        num_guard_types = 0;
        return -3;
    }

    if(!(guards = (pmt_guard_v2_t **)malloc(sizeof(pmt_guard_v2_t *) * cnt))) {
        debug(DBG_ERROR, "Cannot allocate space for v2 guards list: %s\n",
              strerror(errno));
        free(num_guards);
        num_guards = NULL;
        num_guard_types = 0;
        return -4;
    }

    memset(guards, 0, sizeof(pmt_guard_v2_t *) * cnt);

    /* Read in each table... */
    for(i = 0; i < cnt; ++i) {
        /* Read the pointer and the size... */
        memcpy(values, pmt + ptrs[2] + (i << 3), sizeof(uint32_t) * 2);
        values[0] = LE32(values[0]);
        values[1] = LE32(values[1]);

        /* Make sure we have enough file... */
        if(values[1] + sizeof(pmt_guard_v2_t) * values[0] > sz) {
            debug(DBG_ERROR, "ItemPMT.prs file for v2 has guard table outside "
                  "of file bounds! Please check the file for validity!\n");
            return -5;
        }

        num_guards[i] = values[0];
        if(!(guards[i] = (pmt_guard_v2_t *)malloc(sizeof(pmt_guard_v2_t) *
                                                  values[0]))) {
            debug(DBG_ERROR, "Cannot allocate space for v2 guards: %s\n",
                  strerror(errno));
            return -6;
        }

        memcpy(guards[i], pmt + values[1], sizeof(pmt_guard_v2_t) * values[0]);

        for(j = 0; j < values[0]; ++j) {
#if defined(__BIG_ENDIAN__) || defined(WORDS_BIGENDIAN)
            guards[i][j].index = LE32(guards[i][j].index);
            guards[i][j].base_dfp = LE16(guards[i][j].base_dfp);
            guards[i][j].base_evp = LE16(guards[i][j].base_evp);
#endif

            if(guards[i][j].index < guard_lowest)
                guard_lowest = guards[i][j].index;
        }
    }

    return 0;
}

static int read_gc_guards(const uint8_t *pmt, uint32_t sz,
                          const uint32_t ptrs[23]) {
    uint32_t cnt, i, values[2], j;

    /* Make sure the pointers are sane... */
    if(ptrs[2] > sz || ptrs[1] > ptrs[2]) {
        debug(DBG_ERROR, "ItemPMT.prs file for GC has invalid guard pointers. "
              "Please check it for validity!\n");
        return -1;
    }

    /* Figure out how many tables we have... */
    num_guard_types_gc = cnt = (ptrs[2] - ptrs[1]) / 8;

    /* Make sure its sane... Should always be 2. */
    if(cnt != 2) {
        debug(DBG_ERROR, "ItemPMT.prs file for GC does not have two guard "
              "tables. Please check it for validity!\n");
        num_guard_types_gc = 0;
        return -2;
    }

    /* Allocate the stuff we need to allocate... */
    if(!(num_guards_gc = (uint32_t *)malloc(sizeof(uint32_t) * cnt))) {
        debug(DBG_ERROR, "Cannot allocate space for GC guard count: %s\n",
              strerror(errno));
        num_guard_types_gc = 0;
        return -3;
    }

    if(!(guards_gc = (pmt_guard_gc_t **)malloc(sizeof(pmt_guard_gc_t *) *
                                               cnt))) {
        debug(DBG_ERROR, "Cannot allocate space for GC guards list: %s\n",
              strerror(errno));
        free(num_guards_gc);
        num_guards_gc = NULL;
        num_guard_types_gc = 0;
        return -4;
    }

    memset(guards_gc, 0, sizeof(pmt_guard_gc_t *) * cnt);

    /* Read in each table... */
    for(i = 0; i < cnt; ++i) {
        /* Read the pointer and the size... */
        memcpy(values, pmt + ptrs[1] + (i << 3), sizeof(uint32_t) * 2);
        values[0] = ntohl(values[0]);
        values[1] = ntohl(values[1]);

        /* Make sure we have enough file... */
        if(values[1] + sizeof(pmt_guard_gc_t) * values[0] > sz) {
            debug(DBG_ERROR, "ItemPMT.prs file for GC has guard table outside "
                  "of file bounds! Please check the file for validity!\n");
            return -5;
        }

        num_guards_gc[i] = values[0];
        if(!(guards_gc[i] = (pmt_guard_gc_t *)malloc(sizeof(pmt_guard_gc_t) *
                                                     values[0]))) {
            debug(DBG_ERROR, "Cannot allocate space for GC guards: %s\n",
                  strerror(errno));
            return -6;
        }

        memcpy(guards_gc[i], pmt + values[1],
               sizeof(pmt_guard_gc_t) * values[0]);

        for(j = 0; j < values[0]; ++j) {
#if !defined(__BIG_ENDIAN__) && !defined(WORDS_BIGENDIAN)
            guards_gc[i][j].index = ntohl(guards_gc[i][j].index);
            guards_gc[i][j].model = ntohs(guards_gc[i][j].model);
            guards_gc[i][j].skin = ntohs(guards_gc[i][j].skin);
            guards_gc[i][j].base_dfp = ntohs(guards_gc[i][j].base_dfp);
            guards_gc[i][j].base_evp = ntohs(guards_gc[i][j].base_evp);
#endif

            if(guards_gc[i][j].index < guard_lowest_gc)
                guard_lowest_gc = guards_gc[i][j].index;
        }
    }

    return 0;
}

static int read_bb_guards(const uint8_t *pmt, uint32_t sz,
                          const uint32_t ptrs[23]) {
    uint32_t cnt, i, values[2], j;

    /* Make sure the pointers are sane... */
    if(ptrs[2] > sz || ptrs[1] > ptrs[2]) {
        debug(DBG_ERROR, "ItemPMT.prs file for BB has invalid guard pointers. "
              "Please check it for validity!\n");
        return -1;
    }

    /* Figure out how many tables we have... */
    num_guard_types_bb = cnt = (ptrs[2] - ptrs[1]) / 8;

    /* Make sure its sane... Should always be 2. */
    if(cnt != 2) {
        debug(DBG_ERROR, "ItemPMT.prs file for BB does not have two guard "
              "tables. Please check it for validity!\n");
        num_guard_types_bb = 0;
        return -2;
    }

    /* Allocate the stuff we need to allocate... */
    if(!(num_guards_bb = (uint32_t *)malloc(sizeof(uint32_t) * cnt))) {
        debug(DBG_ERROR, "Cannot allocate space for BB guard count: %s\n",
              strerror(errno));
        num_guard_types_bb = 0;
        return -3;
    }

    if(!(guards_bb = (pmt_guard_bb_t **)malloc(sizeof(pmt_guard_bb_t *) *
                                               cnt))) {
        debug(DBG_ERROR, "Cannot allocate space for BB guards list: %s\n",
              strerror(errno));
        free(num_guards_bb);
        num_guards_bb = NULL;
        num_guard_types_bb = 0;
        return -4;
    }

    memset(guards_bb, 0, sizeof(pmt_guard_bb_t *) * cnt);

    /* Read in each table... */
    for(i = 0; i < cnt; ++i) {
        /* Read the pointer and the size... */
        memcpy(values, pmt + ptrs[1] + (i << 3), sizeof(uint32_t) * 2);
        values[0] = LE32(values[0]);
        values[1] = LE32(values[1]);

        /* Make sure we have enough file... */
        if(values[1] + sizeof(pmt_guard_bb_t) * values[0] > sz) {
            debug(DBG_ERROR, "ItemPMT.prs file for BB has guard table outside "
                  "of file bounds! Please check the file for validity!\n");
            return -5;
        }

        num_guards_bb[i] = values[0];
        if(!(guards_bb[i] = (pmt_guard_bb_t *)malloc(sizeof(pmt_guard_bb_t) *
                                                     values[0]))) {
            debug(DBG_ERROR, "Cannot allocate space for BB guards: %s\n",
                  strerror(errno));
            return -6;
        }

        memcpy(guards_bb[i], pmt + values[1],
               sizeof(pmt_guard_bb_t) * values[0]);

        for(j = 0; j < values[0]; ++j) {
#if defined(__BIG_ENDIAN__) || defined(WORDS_BIGENDIAN)
            guards_bb[i][j].index = LE32(guards_bb[i][j].index);
            guards_bb[i][j].model = LE16(guards_bb[i][j].model);
            guards_bb[i][j].skin = LE16(guards_bb[i][j].skin);
            guards_bb[i][j].team_ponts = LE16(guards_bb[i][j].team_points);
            guards_bb[i][j].base_dfp = LE16(guards_bb[i][j].base_dfp);
            guards_bb[i][j].base_evp = LE16(guards_bb[i][j].base_evp);
#endif

            if(guards_bb[i][j].index < guard_lowest_bb)
                guard_lowest_bb = guards_bb[i][j].index;
        }
    }

    return 0;
}

static int read_v2_units(const uint8_t *pmt, uint32_t sz,
                         const uint32_t ptrs[21]) {
    uint32_t values[2], i;

    /* Make sure the pointers are sane... */
    if(ptrs[3] > sz) {
        debug(DBG_ERROR, "ItemPMT.prs file for v2 has invalid unit pointers. "
              "Please check it for validity!\n");
        return -1;
    }

    /* Read the pointer and the size... */
    memcpy(values, pmt + ptrs[3], sizeof(uint32_t) * 2);
    values[0] = LE32(values[0]);
    values[1] = LE32(values[1]);

    /* Make sure we have enough file... */
    if(values[1] + sizeof(pmt_unit_v2_t) * values[0] > sz) {
        debug(DBG_ERROR, "ItemPMT.prs file for v2 has unit table outside "
              "of file bounds! Please check the file for validity!\n");
        return -2;
    }

    num_units = values[0];
    if(!(units = (pmt_unit_v2_t *)malloc(sizeof(pmt_unit_v2_t) * values[0]))) {
        debug(DBG_ERROR, "Cannot allocate space for v2 units: %s\n",
              strerror(errno));
        num_units = 0;
        return -3;
    }

    memcpy(units, pmt + values[1], sizeof(pmt_unit_v2_t) * values[0]);

    for(i = 0; i < values[0]; ++i) {
#if defined(__BIG_ENDIAN__) || defined(WORDS_BIGENDIAN)
        units[i].index = LE32(units[i].index);
        units[i].stat = LE16(units[i].stat);
        units[i].amount = LE16(units[i].amount);
#endif

        if(units[i].index < unit_lowest)
            unit_lowest = units[i].index;
    }

    return 0;
}

static int read_gc_units(const uint8_t *pmt, uint32_t sz,
                         const uint32_t ptrs[23]) {
    uint32_t values[2], i;

    /* Make sure the pointers are sane... */
    if(ptrs[2] > sz) {
        debug(DBG_ERROR, "ItemPMT.prs file for GC has invalid unit pointers. "
              "Please check it for validity!\n");
        return -1;
    }

    /* Read the pointer and the size... */
    memcpy(values, pmt + ptrs[2], sizeof(uint32_t) * 2);
    values[0] = ntohl(values[0]);
    values[1] = ntohl(values[1]);

    /* Make sure we have enough file... */
    if(values[1] + sizeof(pmt_unit_gc_t) * values[0] > sz) {
        debug(DBG_ERROR, "ItemPMT.prs file for GC has unit table outside "
              "of file bounds! Please check the file for validity!\n");
        return -2;
    }

    num_units_gc = values[0];
    if(!(units_gc = (pmt_unit_gc_t *)malloc(sizeof(pmt_unit_gc_t) *
                                            values[0]))) {
        debug(DBG_ERROR, "Cannot allocate space for GC units: %s\n",
              strerror(errno));
        num_units_gc = 0;
        return -3;
    }

    memcpy(units_gc, pmt + values[1], sizeof(pmt_unit_gc_t) * values[0]);

    for(i = 0; i < values[0]; ++i) {
#if !defined(__BIG_ENDIAN__) && !defined(WORDS_BIGENDIAN)
        units_gc[i].index = ntohl(units_gc[i].index);
        units_gc[i].model = ntohs(units_gc[i].model);
        units_gc[i].skin = ntohs(units_gc[i].skin);
        units_gc[i].stat = ntohs(units_gc[i].stat);
        units_gc[i].amount = ntohs(units_gc[i].amount);
#endif

        if(units_gc[i].index < unit_lowest_gc)
            unit_lowest_gc = units_gc[i].index;
    }

    return 0;
}

static int read_bb_units(const uint8_t *pmt, uint32_t sz,
                         const uint32_t ptrs[23]) {
    uint32_t values[2], i;

    /* Make sure the pointers are sane... */
    if(ptrs[2] > sz) {
        debug(DBG_ERROR, "ItemPMT.prs file for BB has invalid unit pointers. "
              "Please check it for validity!\n");
        return -1;
    }

    /* Read the pointer and the size... */
    memcpy(values, pmt + ptrs[2], sizeof(uint32_t) * 2);
    values[0] = LE32(values[0]);
    values[1] = LE32(values[1]);

    /* Make sure we have enough file... */
    if(values[1] + sizeof(pmt_unit_bb_t) * values[0] > sz) {
        debug(DBG_ERROR, "ItemPMT.prs file for BB has unit table outside "
              "of file bounds! Please check the file for validity!\n");
        return -2;
    }

    num_units_bb = values[0];
    if(!(units_bb = (pmt_unit_bb_t *)malloc(sizeof(pmt_unit_bb_t) *
                                            values[0]))) {
        debug(DBG_ERROR, "Cannot allocate space for BB units: %s\n",
              strerror(errno));
        num_units_bb = 0;
        return -3;
    }

    memcpy(units_bb, pmt + values[1], sizeof(pmt_unit_bb_t) * values[0]);

    for(i = 0; i < values[0]; ++i) {
#if defined(__BIG_ENDIAN__) || defined(WORDS_BIGENDIAN)
        units_bb[i].index = LE32(units_bb[i].index);
        units_bb[i].model = LE16(units_bb[i].model);
        units_bb[i].skin = LE16(units_bb[i].skin);
        units_bb[i].team_ponts = LE16(units_bb[i].team_points);
        units_bb[i].stat = LE16(units_bb[i].stat);
        units_bb[i].amount = LE16(units_bb[i].amount);
#endif

        if(units_bb[i].index < unit_lowest_bb)
            unit_lowest_bb = units_bb[i].index;
    }

    return 0;
}

static int read_v2_stars(const uint8_t *pmt, uint32_t sz,
                         const uint32_t ptrs[21]) {
    /* Make sure the pointers are sane... */
    if(ptrs[12] > sz || ptrs[13] > sz || ptrs[13] < ptrs[12]) {
        debug(DBG_ERROR, "ItemPMT.prs file for v2 has invalid star pointers. "
              "Please check it for validity!\n");
        return -1;
    }

    /* Save how big it is, allocate the space, and copy it in */
    star_max = ptrs[13] - ptrs[12];

    if(star_max < unit_lowest + num_units - weapon_lowest) {
        debug(DBG_ERROR, "Star table doesn't have enough entries!\n"
              "Expected at least %u, got %u\n",
              unit_lowest + num_units - weapon_lowest, star_max);
        star_max = 0;
        return -2;
    }

    if(!(star_table = (uint8_t *)malloc(star_max))) {
        debug(DBG_ERROR, "Cannot allocate star table: %s\n", strerror(errno));
        star_max = 0;
        return -3;
    }

    memcpy(star_table, pmt + ptrs[12], star_max);
    return 0;
}

static int read_gc_stars(const uint8_t *pmt, uint32_t sz,
                         const uint32_t ptrs[23]) {
    /* Make sure the pointers are sane... */
    if(ptrs[11] > sz || ptrs[12] > sz || ptrs[12] < ptrs[11]) {
        debug(DBG_ERROR, "ItemPMT.prs file for GC has invalid star pointers. "
              "Please check it for validity!\n");
        return -1;
    }

    /* Save how big it is, allocate the space, and copy it in */
    star_max_gc = ptrs[12] - ptrs[11];

    if(star_max_gc < unit_lowest_gc + num_units - weapon_lowest_gc) {
        debug(DBG_ERROR, "Star table doesn't have enough entries!\n"
              "Expected at least %u, got %u\n",
              unit_lowest_gc + num_units_gc - weapon_lowest_gc, star_max_gc);
        star_max_gc = 0;
        return -2;
    }

    if(!(star_table_gc = (uint8_t *)malloc(star_max_gc))) {
        debug(DBG_ERROR, "Cannot allocate star table: %s\n", strerror(errno));
        star_max_gc = 0;
        return -3;
    }

    memcpy(star_table_gc, pmt + ptrs[11], star_max_gc);
    return 0;
}

static int read_bb_stars(const uint8_t *pmt, uint32_t sz,
                         const uint32_t ptrs[23]) {
    /* Make sure the pointers are sane... */
    if(ptrs[11] > sz || ptrs[12] > sz || ptrs[12] < ptrs[11]) {
        debug(DBG_ERROR, "ItemPMT.prs file for BB has invalid star pointers. "
              "Please check it for validity!\n");
        return -1;
    }

    /* Save how big it is, allocate the space, and copy it in */
    star_max_bb = ptrs[12] - ptrs[11];

    if(star_max_bb < unit_lowest_bb + num_units - weapon_lowest_bb) {
        debug(DBG_ERROR, "Star table doesn't have enough entries!\n"
              "Expected at least %u, got %u\n",
              unit_lowest_bb + num_units_bb - weapon_lowest_bb, star_max_bb);
        star_max_bb = 0;
        return -2;
    }

    if(!(star_table_bb = (uint8_t *)malloc(star_max_bb))) {
        debug(DBG_ERROR, "Cannot allocate star table: %s\n", strerror(errno));
        star_max_bb = 0;
        return -3;
    }

    memcpy(star_table_bb, pmt + ptrs[11], star_max_bb);
    return 0;
}

static int build_v2_units(int norestrict) {
    uint32_t i, j, k;
    uint8_t star;
    pmt_unit_v2_t *unit;
    int16_t pm;
    void *tmp;

    /* Figure out what the max number of stars for a unit is. */
    for(i = 0; i < num_units; ++i) {
        star = star_table[i + unit_lowest - weapon_lowest];
        if(star > unit_max_stars)
            unit_max_stars = star;
    }

    /* Always go one beyond, since we may have to deal with + and ++ on the last
       possible unit. */
    ++unit_max_stars;

    /* For now, punt and allocate space for every theoretically possible unit,
       even though some are actually disabled by the game. */
    if(!(units_by_stars = (uint64_t *)malloc((num_units * 5 + 1) *
                                             sizeof(uint64_t)))) {
        debug(DBG_ERROR, "Cannot allocate unit table: %s\n", strerror(errno));
        return -1;
    }

    /* Allocate space for the "pointer" table */
    if(!(units_with_stars = (uint32_t *)malloc((unit_max_stars + 1) *
                                               sizeof(uint32_t)))) {
        debug(DBG_ERROR, "Cannot allocate unit ptr table: %s\n",
              strerror(errno));
        free(units_by_stars);
        units_by_stars = NULL;
        return -2;
    }

    /* Fill in the game's failsafe of a plain Knight/Power... */
    units_by_stars[0] = (uint64_t)Item_Knight_Power;
    units_with_stars[0] = 1;
    k = 1;

    /* Go through and fill in our tables... */
    for(i = 0; i <= unit_max_stars; ++i) {
        for(j = 0; j < num_units; ++j) {
            unit = units + j;
            star = star_table[j + unit_lowest - weapon_lowest];

            if(star - 1 == i && unit->pm_range &&
               (unit->stat <= 3 || norestrict)) {
                pm = -2 * unit->pm_range;
                units_by_stars[k++] = 0x00000301 | (j << 16) |
                    (((uint64_t)pm) << 48);
                pm = -1 * unit->pm_range;
                units_by_stars[k++] = 0x00000301 | (j << 16) |
                    (((uint64_t)pm) << 48);
            }
            else if(star == i) {
                units_by_stars[k++] = 0x00000301 | (j << 16);
            }
            else if(star + 1 == i && unit->pm_range &&
                    (unit->stat <= 3 || norestrict)) {
                pm = 2 * unit->pm_range;
                units_by_stars[k++] = 0x00000301 | (j << 16) |
                    (((uint64_t)pm) << 48);
                pm = unit->pm_range;
                units_by_stars[k++] = 0x00000301 | (j << 16) |
                    (((uint64_t)pm) << 48);
            }
        }

        units_with_stars[i] = k;
    }

    if((tmp = realloc(units_by_stars, k * sizeof(uint64_t)))) {
        units_by_stars = (uint64_t *)tmp;
    }
    else {
        debug(DBG_WARN, "Cannot resize units_by_stars table: %s\n",
              strerror(errno));
    }

    return 0;
}

static int build_gc_units(int norestrict) {
    uint32_t i, j, k;
    uint8_t star;
    pmt_unit_gc_t *unit;
    int16_t pm;
    void *tmp;

    /* Figure out what the max number of stars for a unit is. */
    for(i = 0; i < num_units_gc; ++i) {
        star = star_table_gc[i + unit_lowest_gc - weapon_lowest_gc];
        if(star > unit_max_stars_gc)
            unit_max_stars_gc = star;
    }

    /* Always go one beyond, since we may have to deal with + and ++ on the last
       possible unit. */
    ++unit_max_stars_gc;

    /* For now, punt and allocate space for every theoretically possible unit,
       even though some are actually disabled by the game. */
    if(!(units_by_stars_gc = (uint64_t *)malloc((num_units_gc * 5 + 1) *
                                                sizeof(uint64_t)))) {
        debug(DBG_ERROR, "Cannot allocate unit table: %s\n", strerror(errno));
        return -1;
    }

    /* Allocate space for the "pointer" table */
    if(!(units_with_stars_gc = (uint32_t *)malloc((unit_max_stars_gc + 1) *
                                                  sizeof(uint32_t)))) {
        debug(DBG_ERROR, "Cannot allocate unit ptr table: %s\n",
              strerror(errno));
        free(units_by_stars_gc);
        units_by_stars_gc = NULL;
        return -2;
    }

    /* Fill in the game's failsafe of a plain Knight/Power... */
    units_by_stars_gc[0] = (uint64_t)Item_Knight_Power;
    units_with_stars_gc[0] = 1;
    k = 1;

    /* Go through and fill in our tables... */
    for(i = 0; i <= unit_max_stars_gc; ++i) {
        for(j = 0; j < num_units_gc; ++j) {
            unit = units_gc + j;
            star = star_table_gc[j + unit_lowest_gc - weapon_lowest_gc];

            if(star - 1 == i && unit->pm_range &&
               (unit->stat <= 3 || norestrict)) {
                pm = -2 * unit->pm_range;
                units_by_stars_gc[k++] = 0x00000301 | (j << 16) |
                    (((uint64_t)pm) << 48);
                pm = -1 * unit->pm_range;
                units_by_stars_gc[k++] = 0x00000301 | (j << 16) |
                    (((uint64_t)pm) << 48);
            }
            else if(star == i) {
                units_by_stars_gc[k++] = 0x00000301 | (j << 16);
            }
            else if(star + 1 == i && unit->pm_range &&
                    (unit->stat <= 3 || norestrict)) {
                pm = 2 * unit->pm_range;
                units_by_stars_gc[k++] = 0x00000301 | (j << 16) |
                    (((uint64_t)pm) << 48);
                pm = unit->pm_range;
                units_by_stars_gc[k++] = 0x00000301 | (j << 16) |
                    (((uint64_t)pm) << 48);
            }
        }

        units_with_stars_gc[i] = k;
    }

    if((tmp = realloc(units_by_stars_gc, k * sizeof(uint64_t)))) {
        units_by_stars_gc = (uint64_t *)tmp;
    }
    else {
        debug(DBG_WARN, "Cannot resize units_by_stars_gc table: %s\n",
              strerror(errno));
    }

    return 0;
}

static int build_bb_units(int norestrict) {
    uint32_t i, j, k;
    uint8_t star;
    pmt_unit_bb_t *unit;
    int16_t pm;
    void *tmp;

    /* Figure out what the max number of stars for a unit is. */
    for(i = 0; i < num_units_bb; ++i) {
        star = star_table_bb[i + unit_lowest_bb - weapon_lowest_bb];
        if(star > unit_max_stars_bb)
            unit_max_stars_bb = star;
    }

    /* Always go one beyond, since we may have to deal with + and ++ on the last
       possible unit. */
    ++unit_max_stars_bb;

    /* For now, punt and allocate space for every theoretically possible unit,
       even though some are actually disabled by the game. */
    if(!(units_by_stars_bb = (uint64_t *)malloc((num_units_bb * 5 + 1) *
                                                sizeof(uint64_t)))) {
        debug(DBG_ERROR, "Cannot allocate unit table: %s\n", strerror(errno));
        return -1;
    }

    /* Allocate space for the "pointer" table */
    if(!(units_with_stars_bb = (uint32_t *)malloc((unit_max_stars_bb + 1) *
                                                  sizeof(uint32_t)))) {
        debug(DBG_ERROR, "Cannot allocate unit ptr table: %s\n",
              strerror(errno));
        free(units_by_stars_bb);
        units_by_stars_bb = NULL;
        return -2;
    }

    /* Fill in the game's failsafe of a plain Knight/Power... */
    units_by_stars_bb[0] = (uint64_t)Item_Knight_Power;
    units_with_stars_bb[0] = 1;
    k = 1;

    /* Go through and fill in our tables... */
    for(i = 0; i <= unit_max_stars_bb; ++i) {
        for(j = 0; j < num_units_bb; ++j) {
            unit = units_bb + j;
            star = star_table_bb[j + unit_lowest_bb - weapon_lowest_bb];

            if(star - 1 == i && unit->pm_range &&
               (unit->stat <= 3 || norestrict)) {
                pm = -2 * unit->pm_range;
                units_by_stars_bb[k++] = 0x00000301 | (j << 16) |
                    (((uint64_t)pm) << 48);
                pm = -1 * unit->pm_range;
                units_by_stars_bb[k++] = 0x00000301 | (j << 16) |
                    (((uint64_t)pm) << 48);
            }
            else if(star == i) {
                units_by_stars_bb[k++] = 0x00000301 | (j << 16);
            }
            else if(star + 1 == i && unit->pm_range &&
                    (unit->stat <= 3 || norestrict)) {
                pm = 2 * unit->pm_range;
                units_by_stars_bb[k++] = 0x00000301 | (j << 16) |
                    (((uint64_t)pm) << 48);
                pm = unit->pm_range;
                units_by_stars_bb[k++] = 0x00000301 | (j << 16) |
                    (((uint64_t)pm) << 48);
            }
        }

        units_with_stars_bb[i] = k;
    }

    if((tmp = realloc(units_by_stars_bb, k * sizeof(uint64_t)))) {
        units_by_stars_bb = (uint64_t *)tmp;
    }
    else {
        debug(DBG_WARN, "Cannot resize units_by_stars_bb table: %s\n",
              strerror(errno));
    }

    return 0;
}

int pmt_read_v2(const char *fn, int norestrict) {
    FILE *fp;
    long len;
    uint32_t ucsz;
    uint8_t *cbuf, *ucbuf;
    uint32_t ptrs[21];

    /* Open up the file */
    if(!(fp = fopen(fn, "rb"))) {
        debug(DBG_ERROR, "Cannot open %s: %s\n", fn, strerror(errno));
        return -1;
    }

    /* Figure out how long the file is. */
    if(fseek(fp, 0, SEEK_END)) {
        debug(DBG_ERROR, "Cannot seek PMT file: %s\n", strerror(errno));
        fclose(fp);
        return -2;
    }

    if((len = ftell(fp)) < 0) {
        debug(DBG_ERROR, "Cannot determine size of compressed PMT: %s\n",
              strerror(errno));
        fclose(fp);
        return -3;
    }

    if(fseek(fp, 0, SEEK_SET)) {
        debug(DBG_ERROR, "Cannot seek PMT file: %s\n", strerror(errno));
        fclose(fp);
        return -4;
    }

    /* Allocate space for the compressed file and read it in */
    if(!(cbuf = (uint8_t *)malloc(len))) {
        debug(DBG_ERROR, "Cannot allocate space for compressed PMT: %s\n",
              strerror(errno));
        fclose(fp);
        return -5;
    }

    if(fread(cbuf, 1, len, fp) != len) {
        debug(DBG_ERROR, "Cannot read compressed PMT: %s\n", strerror(errno));
        free(cbuf);
        fclose(fp);
        return -6;
    }

    /* Figure out how big the uncompressed data is and allocate space for it */
    ucsz = prs_decompress_size(cbuf);

    if(!(ucbuf = (uint8_t *)malloc(ucsz))) {
        debug(DBG_ERROR, "Cannot allocate space for uncompressed PMT: %s\n",
              strerror(errno));
        free(cbuf);
        fclose(fp);
        return -7;
    }

    /* Decompress the file */
    if(prs_decompress(cbuf, ucbuf) != ucsz) {
        debug(DBG_ERROR, "Error uncompressing PMT!\n");
        free(ucbuf);
        free(cbuf);
        fclose(fp);
        return -8;
    }

    /* Clean up the stuff we can clean up now */
    free(cbuf);
    fclose(fp);

    /* Read in the pointers table. */
    if(read_ptr_tbl(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -9;
    }

    /* Let's start with weapons... */
    if(read_v2_weapons(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -10;
    }

    /* Grab the guards... */
    if(read_v2_guards(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -11;
    }

    /* Next, read in the units... */
    if(read_v2_units(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -12;
    }

    /* Read in the star values... */
    if(read_v2_stars(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -13;
    }

    /* We're done with the raw PMT data now, clean it up. */
    free(ucbuf);

    /* Make the tables for generating random units */
    if(build_v2_units(norestrict)) {
        return -14;
    }

    have_v2_pmt = 1;

    return 0;
}

int pmt_read_gc(const char *fn, int norestrict) {
    FILE *fp;
    long len;
    uint32_t ucsz;
    uint8_t *cbuf, *ucbuf;
    uint32_t ptrs[23];

    /* Open up the file */
    if(!(fp = fopen(fn, "rb"))) {
        debug(DBG_ERROR, "Cannot open %s: %s\n", fn, strerror(errno));
        return -1;
    }

    /* Figure out how long the file is. */
    if(fseek(fp, 0, SEEK_END)) {
        debug(DBG_ERROR, "Cannot seek PMT file: %s\n", strerror(errno));
        fclose(fp);
        return -2;
    }

    if((len = ftell(fp)) < 0) {
        debug(DBG_ERROR, "Cannot determine size of compressed PMT: %s\n",
              strerror(errno));
        fclose(fp);
        return -3;
    }

    if(fseek(fp, 0, SEEK_SET)) {
        debug(DBG_ERROR, "Cannot seek PMT file: %s\n", strerror(errno));
        fclose(fp);
        return -4;
    }

    /* Allocate space for the compressed file and read it in */
    if(!(cbuf = (uint8_t *)malloc(len))) {
        debug(DBG_ERROR, "Cannot allocate space for compressed PMT: %s\n",
              strerror(errno));
        fclose(fp);
        return -5;
    }

    if(fread(cbuf, 1, len, fp) != len) {
        debug(DBG_ERROR, "Cannot read compressed PMT: %s\n", strerror(errno));
        free(cbuf);
        fclose(fp);
        return -6;
    }

    /* Figure out how big the uncompressed data is and allocate space for it */
    ucsz = prs_decompress_size(cbuf);

    if(!(ucbuf = (uint8_t *)malloc(ucsz))) {
        debug(DBG_ERROR, "Cannot allocate space for uncompressed PMT: %s\n",
              strerror(errno));
        free(cbuf);
        fclose(fp);
        return -7;
    }

    /* Decompress the file */
    if(prs_decompress(cbuf, ucbuf) != ucsz) {
        debug(DBG_ERROR, "Error uncompressing PMT!\n");
        free(ucbuf);
        free(cbuf);
        fclose(fp);
        return -8;
    }

    /* Clean up the stuff we can clean up now */
    free(cbuf);
    fclose(fp);

    /* Read in the pointers table. */
    if(read_gcptr_tbl(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -9;
    }

    /* Let's start with weapons... */
    if(read_gc_weapons(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -10;
    }

    /* Grab the guards... */
    if(read_gc_guards(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -11;
    }

    /* Next, read in the units... */
    if(read_gc_units(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -12;
    }

    /* Read in the star values... */
    if(read_gc_stars(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -13;
    }

    /* We're done with the raw PMT data now, clean it up. */
    free(ucbuf);

    /* Make the tables for generating random units */
    if(build_gc_units(norestrict)) {
        return -14;
    }

    have_gc_pmt = 1;

    return 0;
}

int pmt_read_bb(const char *fn, int norestrict) {
    FILE *fp;
    long len;
    uint32_t ucsz;
    uint8_t *cbuf, *ucbuf;
    uint32_t ptrs[23];

    /* Open up the file */
    if(!(fp = fopen(fn, "rb"))) {
        debug(DBG_ERROR, "Cannot open %s: %s\n", fn, strerror(errno));
        return -1;
    }

    /* Figure out how long the file is. */
    if(fseek(fp, 0, SEEK_END)) {
        debug(DBG_ERROR, "Cannot seek PMT file: %s\n", strerror(errno));
        fclose(fp);
        return -2;
    }

    if((len = ftell(fp)) < 0) {
        debug(DBG_ERROR, "Cannot determine size of compressed PMT: %s\n",
              strerror(errno));
        fclose(fp);
        return -3;
    }

    if(fseek(fp, 0, SEEK_SET)) {
        debug(DBG_ERROR, "Cannot seek PMT file: %s\n", strerror(errno));
        fclose(fp);
        return -4;
    }

    /* Allocate space for the compressed file and read it in */
    if(!(cbuf = (uint8_t *)malloc(len))) {
        debug(DBG_ERROR, "Cannot allocate space for compressed PMT: %s\n",
              strerror(errno));
        fclose(fp);
        return -5;
    }

    if(fread(cbuf, 1, len, fp) != len) {
        debug(DBG_ERROR, "Cannot read compressed PMT: %s\n", strerror(errno));
        free(cbuf);
        fclose(fp);
        return -6;
    }

    /* Figure out how big the uncompressed data is and allocate space for it */
    ucsz = prs_decompress_size(cbuf);

    if(!(ucbuf = (uint8_t *)malloc(ucsz))) {
        debug(DBG_ERROR, "Cannot allocate space for uncompressed PMT: %s\n",
              strerror(errno));
        free(cbuf);
        fclose(fp);
        return -7;
    }

    /* Decompress the file */
    if(prs_decompress(cbuf, ucbuf) != ucsz) {
        debug(DBG_ERROR, "Error uncompressing PMT!\n");
        free(ucbuf);
        free(cbuf);
        fclose(fp);
        return -8;
    }

    /* Clean up the stuff we can clean up now */
    free(cbuf);
    fclose(fp);

    /* Read in the pointers table. */
    if(read_bbptr_tbl(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -9;
    }

    /* Let's start with weapons... */
    if(read_bb_weapons(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -10;
    }

    /* Grab the guards... */
    if(read_bb_guards(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -11;
    }

    /* Next, read in the units... */
    if(read_bb_units(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -12;
    }

    /* Read in the star values... */
    if(read_bb_stars(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -13;
    }

    /* We're done with the raw PMT data now, clean it up. */
    free(ucbuf);

    /* Make the tables for generating random units */
    if(build_bb_units(norestrict)) {
        return -14;
    }

    have_bb_pmt = 1;

    return 0;
}

int pmt_v2_enabled(void) {
    return have_v2_pmt;
}

int pmt_gc_enabled(void) {
    return have_gc_pmt;
}

int pmt_bb_enabled(void) {
    return have_bb_pmt;
}

void pmt_cleanup(void) {
    uint32_t i;

    for(i = 0; i < num_weapon_types; ++i) {
        free(weapons[i]);
    }

    free(weapons);
    free(num_weapons);

    for(i = 0; i < num_guard_types; ++i) {
        free(guards[i]);
    }

    free(guards);
    free(num_guards);
    free(units);
    free(units_gc);
    free(units_bb);
    free(star_table);
    free(star_table_gc);
    free(star_table_bb);
    free(units_with_stars);
    free(units_by_stars);
    free(units_with_stars_gc);
    free(units_by_stars_gc);
    free(units_with_stars_bb);
    free(units_by_stars_bb);

    for(i = 0; i < num_weapon_types_gc; ++i) {
        free(weapons_gc[i]);
    }

    free(weapons_gc);
    free(num_weapons_gc);

    for(i = 0; i < num_guard_types_gc; ++i) {
        free(guards_gc[i]);
    }

    free(guards_gc);
    free(num_guards_gc);

    for(i = 0; i < num_weapon_types_bb; ++i) {
        free(weapons_bb[i]);
    }

    free(weapons_bb);
    free(num_weapons_bb);

    for(i = 0; i < num_guard_types_bb; ++i) {
        free(guards_bb[i]);
    }

    free(guards_bb);
    free(num_guards_bb);

    weapons = NULL;
    num_weapons = NULL;
    weapons_gc = NULL;
    num_weapons_gc = NULL;
    weapons_bb = NULL;
    num_weapons_bb = NULL;
    guards = NULL;
    num_guards = NULL;
    guards_gc = NULL;
    num_guards_gc = NULL;
    guards_bb = NULL;
    num_guards_bb = NULL;
    units = NULL;
    units_bb = NULL;
    units_with_stars = NULL;
    units_by_stars = NULL;
    units_with_stars_gc = NULL;
    units_by_stars_gc = NULL;
    units_with_stars_bb = NULL;
    units_by_stars_bb = NULL;
    star_table = NULL;
    star_table_gc = NULL;
    star_table_bb = NULL;
    num_weapon_types = 0;
    num_weapon_types_gc = 0;
    num_weapon_types_bb = 0;
    num_guard_types = 0;
    num_guard_types_gc = 0;
    num_guard_types_bb = 0;
    num_units = 0;
    num_units_gc = 0;
    num_units_bb = 0;
    weapon_lowest = guard_lowest = unit_lowest = 0xFFFFFFFF;
    weapon_lowest_gc = guard_lowest_gc = unit_lowest_gc = 0xFFFFFFFF;
    weapon_lowest_bb = guard_lowest_bb = unit_lowest_bb = 0xFFFFFFFF;
    star_max = 0;
    star_max_gc = 0;
    star_max_bb = 0;
    unit_max_stars = 0;
    unit_max_stars_gc = 0;
    unit_max_stars_bb = 0;
    have_v2_pmt = have_gc_pmt = have_bb_pmt = 0;
}

int pmt_lookup_weapon_v2(uint32_t code, pmt_weapon_v2_t *rv) {
    uint8_t parts[3];

    /* Make sure we loaded the PMT stuff to start with and that there is a place
       to put the returned value */
    if(!have_v2_pmt || !rv) {
        return -1;
    }

    parts[0] = (uint8_t)(code & 0xFF);
    parts[1] = (uint8_t)((code >> 8) & 0xFF);
    parts[2] = (uint8_t)((code >> 16) & 0xFF);

    /* Make sure we're looking up a weapon */
    if(parts[0] != 0x00) {
        return -2;
    }

    /* Make sure that we don't go out of bounds anywhere */
    if(parts[1] > num_weapon_types) {
        return -3;
    }

    if(parts[2] >= num_weapons[parts[1]]) {
        return -4;
    }

    /* Grab the data and copy it out */
    memcpy(rv, &weapons[parts[1]][parts[2]], sizeof(pmt_weapon_v2_t));
    return 0;
}

int pmt_lookup_guard_v2(uint32_t code, pmt_guard_v2_t *rv) {
    uint8_t parts[3];

    /* Make sure we loaded the PMT stuff to start with and that there is a place
       to put the returned value */
    if(!have_v2_pmt || !rv) {
        return -1;
    }

    parts[0] = (uint8_t)(code & 0xFF);
    parts[1] = (uint8_t)((code >> 8) & 0xFF);
    parts[2] = (uint8_t)((code >> 16) & 0xFF);

    /* Make sure we're looking up a guard item */
    if(parts[0] != 0x01) {
        return -2;
    }

    /* Make sure its not a unit */
    if(parts[1] == 0x03) {
        return -3;
    }

    /* Make sure that we don't go out of bounds anywhere */
    if(parts[1] > num_guard_types) {
        return -4;
    }

    if(parts[2] >= num_guards[parts[1] - 1]) {
        return -5;
    }

    /* Grab the data and copy it out */
    memcpy(rv, &guards[parts[1] - 1][parts[2]], sizeof(pmt_guard_v2_t));
    return 0;
}

int pmt_lookup_unit_v2(uint32_t code, pmt_unit_v2_t *rv) {
    uint8_t parts[3];

    /* Make sure we loaded the PMT stuff to start with and that there is a place
       to put the returned value */
    if(!have_v2_pmt || !rv) {
        return -1;
    }

    parts[0] = (uint8_t)(code & 0xFF);
    parts[1] = (uint8_t)((code >> 8) & 0xFF);
    parts[2] = (uint8_t)((code >> 16) & 0xFF);

    /* Make sure we're looking up a unit */
    if(parts[0] != 0x01 || parts[1] != 0x03) {
        return -2;
    }

    if(parts[2] >= num_units) {
        return -3;
    }

    /* Grab the data and copy it out */
    memcpy(rv, &units[parts[2]], sizeof(pmt_unit_v2_t));
    return 0;
}

uint8_t pmt_lookup_stars_v2(uint32_t code) {
    uint8_t parts[3];
    pmt_weapon_v2_t weap;
    pmt_guard_v2_t guard;
    pmt_unit_v2_t unit;

    /* Make sure we loaded the PMT stuff to start with. */
    if(!have_v2_pmt)
        return (uint8_t)-1;

    parts[0] = (uint8_t)(code & 0xFF);
    parts[1] = (uint8_t)((code >> 8) & 0xFF);
    parts[2] = (uint8_t)((code >> 16) & 0xFF);

    switch(parts[0]) {
        case 0x00:                      /* Weapons */
            if(pmt_lookup_weapon_v2(code, &weap))
                return (uint8_t)-1;

            if(weap.index - weapon_lowest > star_max)
                return (uint8_t)-1;

            return star_table[weap.index - weapon_lowest];

        case 0x01:                      /* Guards */
            switch(parts[1]) {
                case 0x01:              /* Armors */
                case 0x02:              /* Shields */
                    if(pmt_lookup_guard_v2(code, &guard))
                        return (uint8_t)-1;

                    if(guard.index - weapon_lowest > star_max)
                        return (uint8_t)-1;

                    return star_table[guard.index - weapon_lowest];

                case 0x03:              /* Units */
                    if(pmt_lookup_unit_v2(code, &unit))
                        return (uint8_t)-1;

                    if(unit.index - weapon_lowest > star_max)
                        return (uint8_t)-1;

                    return star_table[unit.index - weapon_lowest];
            }
    }

    return (uint8_t)-1;
}

int pmt_lookup_weapon_gc(uint32_t code, pmt_weapon_gc_t *rv) {
    uint8_t parts[3];

    /* Make sure we loaded the PMT stuff to start with and that there is a place
       to put the returned value */
    if(!have_gc_pmt || !rv) {
        return -1;
    }

    parts[0] = (uint8_t)(code & 0xFF);
    parts[1] = (uint8_t)((code >> 8) & 0xFF);
    parts[2] = (uint8_t)((code >> 16) & 0xFF);

    /* Make sure we're looking up a weapon */
    if(parts[0] != 0x00) {
        return -2;
    }

    /* Make sure that we don't go out of bounds anywhere */
    if(parts[1] > num_weapon_types_gc) {
        return -3;
    }

    if(parts[2] >= num_weapons_gc[parts[1]]) {
        return -4;
    }

    /* Grab the data and copy it out */
    memcpy(rv, &weapons_gc[parts[1]][parts[2]], sizeof(pmt_weapon_gc_t));
    return 0;
}

int pmt_lookup_guard_gc(uint32_t code, pmt_guard_gc_t *rv) {
    uint8_t parts[3];

    /* Make sure we loaded the PMT stuff to start with and that there is a place
       to put the returned value */
    if(!have_gc_pmt || !rv) {
        return -1;
    }

    parts[0] = (uint8_t)(code & 0xFF);
    parts[1] = (uint8_t)((code >> 8) & 0xFF);
    parts[2] = (uint8_t)((code >> 16) & 0xFF);

    /* Make sure we're looking up a guard item */
    if(parts[0] != 0x01) {
        return -2;
    }

    /* Make sure its not a unit */
    if(parts[1] == 0x03) {
        return -3;
    }

    /* Make sure that we don't go out of bounds anywhere */
    if(parts[1] > num_guard_types_gc) {
        return -4;
    }

    if(parts[2] >= num_guards_gc[parts[1] - 1]) {
        return -5;
    }

    /* Grab the data and copy it out */
    memcpy(rv, &guards_gc[parts[1] - 1][parts[2]], sizeof(pmt_guard_gc_t));
    return 0;
}

int pmt_lookup_unit_gc(uint32_t code, pmt_unit_gc_t *rv) {
    uint8_t parts[3];

    /* Make sure we loaded the PMT stuff to start with and that there is a place
       to put the returned value */
    if(!have_gc_pmt || !rv) {
        return -1;
    }

    parts[0] = (uint8_t)(code & 0xFF);
    parts[1] = (uint8_t)((code >> 8) & 0xFF);
    parts[2] = (uint8_t)((code >> 16) & 0xFF);

    /* Make sure we're looking up a unit */
    if(parts[0] != 0x01 || parts[1] != 0x03) {
        return -2;
    }

    if(parts[2] >= num_units_gc) {
        return -3;
    }

    /* Grab the data and copy it out */
    memcpy(rv, &units_gc[parts[2]], sizeof(pmt_unit_gc_t));
    return 0;
}

uint8_t pmt_lookup_stars_gc(uint32_t code) {
    uint8_t parts[3];
    pmt_weapon_gc_t weap;
    pmt_guard_gc_t guard;
    pmt_unit_gc_t unit;

    /* Make sure we loaded the PMT stuff to start with. */
    if(!have_gc_pmt)
        return (uint8_t)-1;

    parts[0] = (uint8_t)(code & 0xFF);
    parts[1] = (uint8_t)((code >> 8) & 0xFF);
    parts[2] = (uint8_t)((code >> 16) & 0xFF);

    switch(parts[0]) {
        case 0x00:                      /* Weapons */
            if(pmt_lookup_weapon_gc(code, &weap))
                return (uint8_t)-1;

            if(weap.index - weapon_lowest_gc > star_max_gc)
                return (uint8_t)-1;

            return star_table_gc[weap.index - weapon_lowest_gc];

        case 0x01:                      /* Guards */
            switch(parts[1]) {
                case 0x01:              /* Armors */
                case 0x02:              /* Shields */
                    if(pmt_lookup_guard_gc(code, &guard))
                        return (uint8_t)-1;

                    if(guard.index - weapon_lowest_gc > star_max_gc)
                        return (uint8_t)-1;

                    return star_table_gc[guard.index - weapon_lowest_gc];

                case 0x03:              /* Units */
                    if(pmt_lookup_unit_gc(code, &unit))
                        return (uint8_t)-1;

                    if(unit.index - weapon_lowest_gc > star_max_gc)
                        return (uint8_t)-1;

                    return star_table_gc[unit.index - weapon_lowest_gc];
            }
    }

    return (uint8_t)-1;
}

int pmt_lookup_weapon_bb(uint32_t code, pmt_weapon_bb_t *rv) {
    uint8_t parts[3];

    /* Make sure we loaded the PMT stuff to start with and that there is a place
       to put the returned value */
    if(!have_bb_pmt || !rv) {
        return -1;
    }

    parts[0] = (uint8_t)(code & 0xFF);
    parts[1] = (uint8_t)((code >> 8) & 0xFF);
    parts[2] = (uint8_t)((code >> 16) & 0xFF);

    /* Make sure we're looking up a weapon */
    if(parts[0] != 0x00) {
        return -2;
    }

    /* Make sure that we don't go out of bounds anywhere */
    if(parts[1] > num_weapon_types_bb) {
        return -3;
    }

    if(parts[2] >= num_weapons_bb[parts[1]]) {
        return -4;
    }

    /* Grab the data and copy it out */
    memcpy(rv, &weapons_bb[parts[1]][parts[2]], sizeof(pmt_weapon_bb_t));
    return 0;
}

int pmt_lookup_guard_bb(uint32_t code, pmt_guard_bb_t *rv) {
    uint8_t parts[3];

    /* Make sure we loaded the PMT stuff to start with and that there is a place
       to put the returned value */
    if(!have_bb_pmt || !rv) {
        return -1;
    }

    parts[0] = (uint8_t)(code & 0xFF);
    parts[1] = (uint8_t)((code >> 8) & 0xFF);
    parts[2] = (uint8_t)((code >> 16) & 0xFF);

    /* Make sure we're looking up a guard item */
    if(parts[0] != 0x01) {
        return -2;
    }

    /* Make sure its not a unit */
    if(parts[1] == 0x03) {
        return -3;
    }

    /* Make sure that we don't go out of bounds anywhere */
    if(parts[1] > num_guard_types_bb) {
        return -4;
    }

    if(parts[2] >= num_guards_bb[parts[1] - 1]) {
        return -5;
    }

    /* Grab the data and copy it out */
    memcpy(rv, &guards_bb[parts[1] - 1][parts[2]], sizeof(pmt_guard_bb_t));
    return 0;
}

int pmt_lookup_unit_bb(uint32_t code, pmt_unit_bb_t *rv) {
    uint8_t parts[3];

    /* Make sure we loaded the PMT stuff to start with and that there is a place
       to put the returned value */
    if(!have_bb_pmt || !rv) {
        return -1;
    }

    parts[0] = (uint8_t)(code & 0xFF);
    parts[1] = (uint8_t)((code >> 8) & 0xFF);
    parts[2] = (uint8_t)((code >> 16) & 0xFF);

    /* Make sure we're looking up a unit */
    if(parts[0] != 0x01 || parts[1] != 0x03) {
        return -2;
    }

    if(parts[2] >= num_units_bb) {
        return -3;
    }

    /* Grab the data and copy it out */
    memcpy(rv, &units_bb[parts[2]], sizeof(pmt_unit_bb_t));
    return 0;
}

uint8_t pmt_lookup_stars_bb(uint32_t code) {
    uint8_t parts[3];
    pmt_weapon_bb_t weap;
    pmt_guard_bb_t guard;
    pmt_unit_bb_t unit;

    /* Make sure we loaded the PMT stuff to start with. */
    if(!have_bb_pmt)
        return (uint8_t)-1;

    parts[0] = (uint8_t)(code & 0xFF);
    parts[1] = (uint8_t)((code >> 8) & 0xFF);
    parts[2] = (uint8_t)((code >> 16) & 0xFF);

    switch(parts[0]) {
        case 0x00:                      /* Weapons */
            if(pmt_lookup_weapon_bb(code, &weap))
                return (uint8_t)-1;

            if(weap.index - weapon_lowest_bb > star_max_bb)
                return (uint8_t)-1;

            return star_table_bb[weap.index - weapon_lowest_bb];

        case 0x01:                      /* Guards */
            switch(parts[1]) {
                case 0x01:              /* Armors */
                case 0x02:              /* Shields */
                    if(pmt_lookup_guard_bb(code, &guard))
                        return (uint8_t)-1;

                    if(guard.index - weapon_lowest_bb > star_max_bb)
                        return (uint8_t)-1;

                    return star_table_bb[guard.index - weapon_lowest_bb];

                case 0x03:              /* Units */
                    if(pmt_lookup_unit_bb(code, &unit))
                        return (uint8_t)-1;

                    if(unit.index - weapon_lowest_bb > star_max_bb)
                        return (uint8_t)-1;

                    return star_table_bb[unit.index - weapon_lowest_bb];
            }
    }

    return (uint8_t)-1;
}

/*
   Generate a random unit, based off of data for PSOv2. We precalculate most of
   the stuff needed for this up above in the build_v2_units() function.

   Random unit generation is actually pretty simple, compared to the other item
   types. Basically, the game builds up a table of units that have the amount of
   stars specified in the PT data's unit_level value for the floor or less
   and randomly selects from that table for each time its needed. Note that the
   game always has a fallback of a plain Knight/Power in case no other unit can
   be generated (which means that once you hit the star level for Knight/Power,
   it is actually 2x as likely to drop as any other unit). Note that the table
   also holds all the +/++/-/-- combinations for the units as well. Only one
   random number ever has to be generated to pick the unit, since the table
   contains all the boosted/lowered versions of the units.

   Some units (for some reason) have a +/- increment defined in the PMT, but the
   game disallows those units from dropping with + or - ever. As long as you
   don't configure it otherwise, this will work as it does in the game. The
   units affected by this are all the /HP, /TP, /Body, /Luck, /Ability, Resist/,
   /Resist, HP/, TP/, PB/, /Technique, /Battle, State/Maintenance, and
   Trap/Search (i.e, most of the units -- note everything there after /Resist
   is actually defined as a 0 increment anyway).
*/
int pmt_random_unit_v2(uint8_t max, uint32_t item[4],
                       struct mt19937_state *rng) {
    uint64_t unit;

    if(max > unit_max_stars)
        max = unit_max_stars;

    /* Pick one of them, and return it. */
    unit = units_by_stars[mt19937_genrand_int32(rng) % units_with_stars[max]];
    item[0] = (uint32_t)unit;
    item[1] = (uint32_t)(unit >> 32);
    item[2] = item[3] = 0;

    return 0;
}

int pmt_random_unit_gc(uint8_t max, uint32_t item[4],
                       struct mt19937_state *rng) {
    uint64_t unit;

    if(max > unit_max_stars_gc)
        max = unit_max_stars_gc;

    /* Pick one of them, and return it. */
    unit = units_by_stars_gc[mt19937_genrand_int32(rng) %
                             units_with_stars_gc[max]];
    item[0] = (uint32_t)unit;
    item[1] = (uint32_t)(unit >> 32);
    item[2] = item[3] = 0;

    return 0;
}

int pmt_random_unit_bb(uint8_t max, uint32_t item[4],
                       struct mt19937_state *rng) {
    uint64_t unit;

    if(max > unit_max_stars_bb)
        max = unit_max_stars_bb;

    /* Pick one of them, and return it. */
    unit = units_by_stars_bb[mt19937_genrand_int32(rng) %
                             units_with_stars_bb[max]];
    item[0] = (uint32_t)unit;
    item[1] = (uint32_t)(unit >> 32);
    item[2] = item[3] = 0;

    return 0;
}
