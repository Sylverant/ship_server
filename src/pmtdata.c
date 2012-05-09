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

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <sylverant/prs.h>
#include <sylverant/debug.h>
#include <sylverant/mtwist.h>

#include "pmtdata.h"
#include "utils.h"
#include "packets.h"

static pmt_weapon_v2_t **weapons = NULL;
static uint32_t *num_weapons = NULL;
static uint32_t num_weapon_types = 0;
static uint16_t weapon_lowest = 0xFFFF;

static pmt_guard_v2_t **guards = NULL;
static uint32_t *num_guards = NULL;
static uint32_t num_guard_types = 0;
static uint16_t guard_lowest = 0xFFFF;

static pmt_unit_v2_t *units = NULL;
static uint32_t num_units = 0;
static uint16_t unit_lowest = 0xFFFF;

static uint8_t *star_table = NULL;
static uint32_t star_max = 0;

static int have_v2_pmt = 0;

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
            weapons[i][j].index = LE16(weapons[i][j].index);
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
            guards[i][j].index = LE16(guards[i][j].index);
            guards[i][j].base_dfp = LE16(guards[i][j].base_dfp);
            guards[i][j].base_evp = LE16(guards[i][j].base_evp);
#endif

            if(guards[i][j].index < guard_lowest)
                guard_lowest = guards[i][j].index;
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
        return -3;
    }

    memcpy(units, pmt + values[1], sizeof(pmt_unit_v2_t) * values[0]);

    for(i = 0; i < values[0]; ++i) {
#if defined(__BIG_ENDIAN__) || defined(WORDS_BIGENDIAN)
        units[i].index = LE16(units[i].index);
        units[i].stat = LE16(units[i].stat);
        units[i].amount = LE16(units[i].amount);
#endif

        if(units[i].index < unit_lowest)
            unit_lowest = units[i].index;
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

int pmt_read_v2(const char *fn) {
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

    /* Clean up the rest of the stuff we can */
    free(ucbuf);
    have_v2_pmt = 1;

    return 0;
}

int pmt_v2_enabled(void) {
    return have_v2_pmt;
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
    free(star_table);

    weapons = NULL;
    num_weapons = NULL;
    guards = NULL;
    num_guards = NULL;
    units = NULL;
    num_weapon_types = 0;
    num_guard_types = 0;
    num_units = 0;
    weapon_lowest = guard_lowest = unit_lowest = 0xFFFF;
    star_max = 0;
    have_v2_pmt = 0;
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
    memcpy(rv, &weapons[parts[1] - 1][parts[2]], sizeof(pmt_weapon_v2_t));
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

    if(parts[2] >= num_guards[parts[1]]) {
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
