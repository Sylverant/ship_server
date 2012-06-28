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

#include <sylverant/debug.h>
#include <sylverant/mtwist.h>

#include "rtdata.h"
#include "ship_packets.h"

/* Our internal representation of the ItemRT entry. This way, we don't have to
   expand it every time we want to use it. */
typedef struct rt_data {
    double prob;
    uint32_t item_data;
    uint32_t area;                      /* Unused for enemies */
} rt_data_t;

/* A set of rare item data. We store one of these for each (difficulty, section)
   pair. */
typedef struct rt_set {
    rt_data_t enemy_rares[101];
    rt_data_t box_rares[30];
} rt_set_t;

static int have_v2rt = 0;
static rt_set_t v2_rtdata[4][10];

/* This function based on information from a couple of different sources, namely
   Fuzziqer's newserv and information from Lee (through Aleron Ives). */
static double expand_rate(uint8_t rate) {
    int tmp = (rate >> 3) - 4;
    uint32_t expd;

    if(tmp < 0)
        tmp = 0;

    expd = (2 << tmp) * ((rate & 7) + 7);
    return (double)expd / (double)0x100000000ULL;
}

int rt_read_v2(const char *fn) {
    FILE *fp;
    uint8_t buf[30];
    int rv = 0, i, j, k;
    uint32_t offsets[40], tmp;
    rt_entry_t ent;

    have_v2rt = 0;

    /* Open up the file */
    if(!(fp = fopen(fn, "rb"))) {
        debug(DBG_ERROR, "Cannot open %s: %s\n", fn, strerror(errno));
        return -1;
    }

    /* Make sure that it looks like a sane AFS file. */
    if(fread(buf, 1, 4, fp) != 4) {
        debug(DBG_ERROR, "Error reading file: %s\n", strerror(errno));
        rv = -2;
        goto out;
    }

    if(buf[0] != 0x41 || buf[1] != 0x46 || buf[2] != 0x53 || buf[3] != 0x00) {
        debug(DBG_ERROR, "%s is not an AFS archive!\n", fn);
        rv = -3;
        goto out;
    }

    /* Make sure there are exactly 40 entries */
    if(fread(buf, 1, 4, fp) != 4) {
        debug(DBG_ERROR, "Error reading file: %s\n", strerror(errno));
        rv = -2;
        goto out;
    }

    if(buf[0] != 40 || buf[1] != 0 || buf[2] != 0 || buf[3] != 0) {
        debug(DBG_ERROR, "%s does not appear to be an ItemRT.afs file\n", fn);
        rv = -4;
        goto out;
    }

    /* Read in the offsets and lengths */
    for(i = 0; i < 40; ++i) {
        if(fread(buf, 1, 4, fp) != 4) {
            debug(DBG_ERROR, "Error reading file: %s\n", strerror(errno));
            rv = -2;
            goto out;
        }

        offsets[i] = (buf[0]) | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);

        if(fread(buf, 1, 4, fp) != 4) {
            debug(DBG_ERROR, "Error reading file: %s\n", strerror(errno));
            rv = -2;
            goto out;
        }

        if(buf[0] != 0x80 || buf[1] != 0x02 || buf[2] != 0 || buf[3] != 0) {
            debug(DBG_ERROR, "Invalid sized entry in ItemRT.afs!\n");
            rv = 5;
            goto out;
        }
    }

    /* Now, parse each entry... */
    for(i = 0; i < 4; ++i) {
        for(j = 0; j < 10; ++j) {
            if(fseek(fp, (long)offsets[i * 10 + j], SEEK_SET)) {
                debug(DBG_ERROR, "fseek error: %s\n", strerror(errno));
                rv = -2;
                goto out;
            }

            /* Read in the enemy entries */
            for(k = 0; k < 0x65; ++k) {
                if(fread(&ent, 1, sizeof(rt_entry_t), fp) !=
                   sizeof(rt_entry_t)) {
                    debug(DBG_ERROR, "Error reading RT: %s\n", strerror(errno));
                    rv = -2;
                    goto out;
                }

                tmp = ent.item_data[0] | (ent.item_data[1] << 8) |
                    (ent.item_data[2] << 16);
                v2_rtdata[i][j].enemy_rares[k].prob = expand_rate(ent.prob);
                v2_rtdata[i][j].enemy_rares[k].item_data = tmp;
                v2_rtdata[i][j].enemy_rares[k].area = 0; /* Unused */
            }

            /* Read in the box entries */
            if(fread(buf, 1, 30, fp) != 30) {
                debug(DBG_ERROR, "Error reading RT: %s\n", strerror(errno));
                rv = -2;
                goto out;
            }

            for(k = 0; k < 30; ++k) {
                if(fread(&ent, 1, sizeof(rt_entry_t), fp) !=
                   sizeof(rt_entry_t)) {
                    debug(DBG_ERROR, "Error reading RT: %s\n", strerror(errno));
                    rv = -2;
                    goto out;
                }

                tmp = ent.item_data[0] | (ent.item_data[1] << 8) |
                    (ent.item_data[2] << 16);
                v2_rtdata[i][j].box_rares[k].prob = expand_rate(ent.prob);
                v2_rtdata[i][j].box_rares[k].item_data = tmp;
                v2_rtdata[i][j].box_rares[k].area = buf[k];
            }
        }
    }

    have_v2rt = 1;

out:
    fclose(fp);
    return rv;
}

int rt_v2_enabled(void) {
    return have_v2rt;
}

uint32_t rt_generate_v2_rare(ship_client_t *c, lobby_t *l, int rt_index,
                             int area) {
    struct mt19937_state *rng = &c->cur_block->rng;
    double rnd;
    rt_set_t *set;
    int i;

    /* Make sure we read in a rare table and we have a sane index */
    if(!have_v2rt)
        return 0;

    if(rt_index < -1 || rt_index > 100)
        return -1;

    /* Grab the rare set for the game */
    set = &v2_rtdata[l->difficulty][l->section];

    /* Are we doing a drop for an enemy or a box? */
    if(rt_index >= 0) {
        rnd = mt19937_genrand_real1(rng);

        if(rnd < set->enemy_rares[rt_index].prob)
            return set->enemy_rares[rt_index].item_data;
    }
    else {
        for(i = 0; i < 30; ++i) {
            if(set->box_rares[i].area == area) {
                rnd = mt19937_genrand_real1(rng);

                if(rnd < set->box_rares[i].prob)
                    return set->box_rares[i].item_data;
            }
        }
    }

    return 0;
}
