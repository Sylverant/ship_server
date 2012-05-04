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

#include "pmtdata.h"
#include "utils.h"
#include "packets.h"

static pmt_guard_v2_t **guards = NULL;
static uint32_t *num_guards = NULL;
static uint32_t num_guard_types = 0;

static pmt_unit_v2_t *units = NULL;
static uint32_t num_units = 0;

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

static int read_v2_guards(const uint8_t *pmt, uint32_t sz,
                          const uint32_t ptrs[21]) {
    uint32_t cnt, i, values[2];
#if defined(__BIG_ENDIAN__) || defined(WORDS_BIGENDIAN)
    uint32_t j;
#endif

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

#if defined(__BIG_ENDIAN__) || defined(WORDS_BIGENDIAN)
        for(j = 0; j < values[0]; ++j) {
            guards[i][j].index = LE16(guards[i][j].index);
            guards[i][j].base_dfp = LE16(guards[i][j].base_dfp);
            guards[i][j].base_evp = LE16(guards[i][j].base_evp);
        }
#endif
    }

    return 0;
}

static int read_v2_units(const uint8_t *pmt, uint32_t sz,
                         const uint32_t ptrs[21]) {
    uint32_t values[2];
#if defined(__BIG_ENDIAN__) || defined(WORDS_BIGENDIAN)
    uint32_t i;
#endif

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

#if defined(__BIG_ENDIAN__) || defined(WORDS_BIGENDIAN)
    for(i = 0; i < values[0]; ++i) {
        units[i].index = LE16(units[i].index);
        units[i].stat = LE16(units[i].stat);
        units[i].amount = LE16(units[i].amount);
    }
#endif

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

    /* Read in guards first... */
    if(read_v2_guards(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -10;
    }

    /* Next, read in the units... */
    if(read_v2_units(ucbuf, ucsz, ptrs)) {
        free(ucbuf);
        return -11;
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

    for(i = 0; i < num_guard_types; ++i) {
        free(guards[i]);
    }

    free(guards);
    free(num_guards);

    guards = NULL;
    num_guards = NULL;
    num_guard_types = 0;
    have_v2_pmt = 0;
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
