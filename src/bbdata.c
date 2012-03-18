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
#include <unistd.h>

#include <sylverant/debug.h>
#include <sylverant/prs.h>

#include "bbdata.h"
#include "clients.h"

/* Enemy battle parameters. The array is organized in the following levels:
   multi/single player, episode, difficulty, entry.*/
bb_battle_param_t battle_params[2][3][4][0x60];

/* Player levelup data */
bb_level_table_t char_stats;

/* Parsed enemy data. Organized similarly to the battle parameters, except that
   the last level is the actual areas themselves. */
bb_parsed_map_t parsed_maps[2][3][4][0x10];

static int read_param_file(bb_battle_param_t dst[4][0x60], const char *fn) {
    FILE *fp;
    const size_t sz = 0x60 * sizeof(bb_battle_param_t);

    if(!(fp = fopen(fn, "rb"))) {
        debug(DBG_ERROR, "Cannot open %s for reading: %s\n", fn,
              strerror(errno));
        return 1;
    }

    /* Read each difficulty in... */
    if(fread(dst[0], 1, sz, fp) != sz) {
        debug(DBG_ERROR, "Cannot read data from %s: %s\n", fn, strerror(errno));
        return 1;
    }

    if(fread(dst[1], 1, sz, fp) != sz) {
        debug(DBG_ERROR, "Cannot read data from %s: %s\n", fn, strerror(errno));
        return 1;
    }

    if(fread(dst[2], 1, sz, fp) != sz) {
        debug(DBG_ERROR, "Cannot read data from %s: %s\n", fn, strerror(errno));
        return 1;
    }

    if(fread(dst[3], 1, sz, fp) != sz) {
        debug(DBG_ERROR, "Cannot read data from %s: %s\n", fn, strerror(errno));
        return 1;
    }

    fclose(fp);
    return 0;
}

static int read_level_data(const char *fn) {
    FILE *fp;
    uint8_t *buf, *buf2;
    long size;
    uint32_t decsize;

#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__)
    int i, j;
#endif

    if(!(fp = fopen(fn, "rb"))) {
        debug(DBG_ERROR, "Cannot open %s for reading: %s\n", fn,
              strerror(errno));
        return 1;
    }

    /* Figure out how long it is and read it in... */
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if(!(buf = (uint8_t *)malloc(size))) {
        debug(DBG_ERROR, "Couldn't allocate space for level table.\n%s\n",
              strerror(errno));
        fclose(fp);
        return -200;
    }

    if(fread(buf, 1, size, fp) != size) {
        debug(DBG_ERROR, "Cannot read data from %s: %s\n", fn, strerror(errno));
        fclose(fp);
        free(buf);
        return 1;
    }

    /* Done with the file, decompress the data */
    fclose(fp);
    decsize = prs_decompress_size(buf);

    if(!(buf2 = (uint8_t *)malloc(decsize))) {
        debug(DBG_ERROR, "Couldn't allocate space for decompressing level "
              "table.\n%s\n", strerror(errno));
        free(buf);
        return -200;
    }

    prs_decompress(buf, buf2);
    memcpy(&char_stats, buf2, sizeof(bb_level_table_t));

#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__)
    /* Swap all the exp values */
    for(j = 0; j < 12; ++j) {
        for(i = 0; i < 200; ++i) {
            char_stats[j][i].exp = LE32(char_stats[j][i].exp);
        }
    }
#endif

    /* Clean up... */
    free(buf2);
    free(buf);

    return 0;
}

int bb_read_params(sylverant_ship_t *cfg) {
    int rv = 0;
    long sz;
    char *buf, *path;

    /* Make sure we have a directory set... */
    if(!cfg->bb_param_dir || !cfg->bb_map_dir) {
        debug(DBG_WARN, "No Blue Burst parameter and/or map directory set!\n"
              "Disabling Blue Burst support.\n");
        return 1;
    }

    /* Save the current working directory, so we can do this a bit easier. */
    sz = pathconf(".", _PC_PATH_MAX);
    if(!(buf = malloc(sz))) {
        debug(DBG_ERROR, "Error allocating memory: %s\n", strerror(errno));
        return -1;
    }

    if(!(path = getcwd(buf, (size_t)sz))) {
        debug(DBG_ERROR, "Error getting current dir: %s\n", strerror(errno));
        free(buf);
        return -1;
    }

    if(chdir(cfg->bb_param_dir)) {
        debug(DBG_ERROR, "Error changing to Blue Burst param dir: %s\n",
              strerror(errno));
        free(buf);
        return 1;
    }

    /* Attempt to read all the files. */
    debug(DBG_LOG, "Loading Blue Burst battle parameter data...\n");
    rv = read_param_file(battle_params[0][0], "BattleParamEntry_on.dat");
    rv += read_param_file(battle_params[0][1], "BattleParamEntry_lab_on.dat");
    rv += read_param_file(battle_params[0][2], "BattleParamEntry_ep4_on.dat");
    rv += read_param_file(battle_params[1][0], "BattleParamEntry.dat");
    rv += read_param_file(battle_params[1][1], "BattleParamEntry_lab.dat");
    rv += read_param_file(battle_params[1][2], "BattleParamEntry_ep4.dat");

    /* Try to read the levelup data */
    debug(DBG_LOG, "Loading Blue Burst levelup table...\n");
    rv += read_level_data("PlyLevelTbl.prs");

    /* Change back to the original directory. */
    if(chdir(path)) {
        debug(DBG_ERROR, "Cannot change back to original dir: %s\n",
              strerror(errno));
        free(buf);
        return -1;
    }

    /* Bail out early, if appropriate. */
    if(rv) {
        goto bail;
    }

bail:
    if(rv) {
        debug(DBG_ERROR, "Error reading Blue Burst data, disabling Blue Burst "
              "support!\n");
    }

    /* Clean up and return. */
    free(buf);
    return rv;
}
