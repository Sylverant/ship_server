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

#include "ptdata.h"
#include "subcmd.h"

static int have_v2pt = 0;
static int have_v3pt = 0;

static pt_v2_entry_t v2_ptdata[4][10];
static pt_v3_entry_t v3_ptdata[2][4][10];

#if !defined(__BIG_ENDIAN__) && !defined(WORDS_BIGENDIAN)
#define BE16(x) (((x >> 8) & 0xFF) | ((x & 0xFF) << 8))
#define BE32(x) (((x >> 24) & 0x00FF) | \
                 ((x >>  8) & 0xFF00) | \
                 ((x & 0xFF00) <<  8) | \
                 ((x & 0x00FF) << 24))
#endif

int pt_read_v2(const char *fn) {
    FILE *fp;
    uint8_t buf[4];
    int rv = 0, i, j;
    uint32_t offsets[40];
#if defined(__BIG_ENDIAN__) || defined(WORDS_BIGENDIAN)
    int k, l;
#endif

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
        debug(DBG_ERROR, "%s does not appear to be an ItemPT.afs file\n", fn);
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

        if(buf[0] != 0x40 || buf[1] != 0x09 || buf[2] != 0 || buf[3] != 0) {
            debug(DBG_ERROR, "Invalid sized entry in ItemPT.afs!\n");
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

            if(fread(&v2_ptdata[i][j], 1, sizeof(pt_v2_entry_t), fp) !=
               sizeof(pt_v2_entry_t)) {
                debug(DBG_ERROR, "Error reading file: %s\n", strerror(errno));
                rv = -2;
                goto out;
            }

            /* Swap entries, if we need to */
#if defined(__BIG_ENDIAN__) || defined(WORDS_BIGENDIAN)
            for(k = 0; k < 28; ++k) {
                for(l = 0; l < 10; ++l) {
                    v2_ptdata[i][j].tool_frequency[k][l] =
                        LE16(v2_ptdata[i][j].tool_frequency[k][l]);
                }
            }

            for(k = 0; k < 100; ++k) {
                for(l = 0; l < 2; ++l) {
                    v2_ptdata[i][j].enemy_meseta[k][l] = 
                        LE16(v2_ptdata[i][j].enemy_meseta[k][l]);
                }
            }

            for(k = 0; k < 10; ++k) {
                for(l = 0; l < 2; ++l) {
                    v2_ptdata[i][j].box_meseta[k][l] = 
                        LE16(v2_ptdata[i][j].box_meseta[k][l]);
                }
            }

            for(k = 0; k < 18; ++k) {
                v2_ptdata[i][j].pointers[k] = LE32(v2_ptdata[i][j].pointers[k]);
            }

            v2_ptdata[i][j].armor_level = LE32(v2_ptdata[i][j].armor_level);
#endif
        }
    }

    have_v2pt = 1;

out:
    fclose(fp);
    return rv;
}

int pt_read_v3(const char *fn) {
    FILE *fp;
    uint8_t buf[4];
    int rv = 0, i, j, k;
    uint32_t offsets[80];
#if !defined(__BIG_ENDIAN__) && !defined(WORDS_BIGENDIAN)
    int l, m;
#endif

    /* Open up the file */
    if(!(fp = fopen(fn, "rb"))) {
        debug(DBG_ERROR, "Cannot open %s: %s\n", fn, strerror(errno));
        return -1;
    }

    /* Read in the offsets and lengths for the Episode I & II data. */
    for(i = 0; i < 80; ++i) {
        if(fseek(fp, 32, SEEK_CUR)) {
            debug(DBG_ERROR, "fseek error: %s\n", strerror(errno));
            rv = -2;
            goto out;
        }

        if(fread(buf, 1, 4, fp) != 4) {
            debug(DBG_ERROR, "Error reading file: %s\n", strerror(errno));
            rv = -2;
            goto out;
        }

        /* The offsets are in 2048 byte blocks. */
        offsets[i] = (buf[3]) | (buf[2] << 8) | (buf[1] << 16) | (buf[0] << 24);
        offsets[i] <<= 11;

        if(fread(buf, 1, 4, fp) != 4) {
            debug(DBG_ERROR, "Error reading file: %s\n", strerror(errno));
            rv = -2;
            goto out;
        }

        if(buf[0] != 0 || buf[1] != 0 || buf[2] != 0x09 || buf[3] != 0xE0) {
            debug(DBG_ERROR, "Invalid sized entry in ItemPT.gsl!\n");
            rv = 5;
            goto out;
        }

        /* Skip over the padding. */
        if(fseek(fp, 8, SEEK_CUR)) {
            debug(DBG_ERROR, "fseek error: %s\n", strerror(errno));
            rv = -2;
            goto out;
        }
    }

    /* Now, parse each entry... */
    for(i = 0; i < 2; ++i) {
        for(j = 0; j < 4; ++j) {
            for(k = 0; k < 10; ++k) {
                if(fseek(fp, (long)offsets[i * 40 + j * 10 + k], SEEK_SET)) {
                    debug(DBG_ERROR, "fseek error: %s\n", strerror(errno));
                    rv = -2;
                    goto out;
                }

                if(fread(&v3_ptdata[i][j][k], 1, sizeof(pt_v3_entry_t), fp) !=
                   sizeof(pt_v3_entry_t)) {
                    debug(DBG_ERROR, "Error reading file: %s\n",
                          strerror(errno));
                    rv = -2;
                    goto out;
                }

                /* Swap entries, if we need to */
#if !defined(__BIG_ENDIAN__) && !defined(WORDS_BIGENDIAN)
                for(l = 0; l < 28; ++l) {
                    for(m = 0; m < 10; ++m) {
                        v3_ptdata[i][j][k].tool_frequency[l][m] =
                            BE16(v3_ptdata[i][j][k].tool_frequency[l][m]);
                    }
                }

                for(l = 0; l < 23; ++l) {
                    for(m = 0; m < 6; ++m) {
                        v3_ptdata[i][j][k].percent_pattern[l][m] =
                            BE16(v3_ptdata[i][j][k].percent_pattern[l][m]);
                    }
                }

                for(l = 0; l < 100; ++l) {
                    for(m = 0; m < 2; ++m) {
                        v3_ptdata[i][j][k].enemy_meseta[l][m] = 
                            BE16(v3_ptdata[i][j][k].enemy_meseta[l][m]);
                    }
                }

                for(l = 0; l < 10; ++l) {
                    for(m = 0; m < 2; ++m) {
                        v3_ptdata[i][j][k].box_meseta[l][m] = 
                            BE16(v3_ptdata[i][j][k].box_meseta[l][m]);
                    }
                }

                for(l = 0; l < 18; ++l) {
                    v3_ptdata[i][j][k].pointers[l] =
                        BE32(v3_ptdata[i][j][k].pointers[l]);
                }

                v3_ptdata[i][j][k].armor_level =
                    BE32(v3_ptdata[i][j][k].armor_level);
#endif
            }
        }
    }

    have_v3pt = 1;

out:
    fclose(fp);
    return rv;
}

int pt_v2_enabled(void) {
    return have_v2pt;
}

int pt_v3_enabled(void) {
    return have_v3pt;
}

/* Generate an item drop from the PT data. This version uses the v2 PT data set,
   and thus is appropriate for any version before PSOGC. */
int pt_generate_v2_drop(lobby_t *l, void *r) {
    subcmd_itemreq_t *req = (subcmd_itemreq_t *)r;
    pt_v2_entry_t *ent = &v2_ptdata[l->difficulty][l->section];
    uint8_t dar;
    uint32_t rnd;
    uint16_t t1, t2;
    uint32_t i[4];

    /* Make sure the PT index in the packet is sane */
    if(req->pt_index > 0x30)
        return -1;

    /* If the PT index is 0x30, this is a box, not an enemy! */
    if(req->pt_index == 0x30)
        /* XXXX */
        return 0;

    dar = ent->enemy_dar[req->pt_index];

    /* See if the enemy is going to drop anything at all this time... */
    rnd = genrand_int32() % 100;

    if(rnd >= dar)
        /* Nope. You get nothing! */
        return 0;

    /* XXXX: For now, just drop meseta... We'll worry about the rest later. */
    t1 = ent->enemy_meseta[req->pt_index][0];
    t2 = ent->enemy_meseta[req->pt_index][1];
    if(t1 < t2)
        rnd = (genrand_int32() % (t2 - t1)) + t1;
    else
        rnd = (uint32_t)t1;

    if(rnd) {
        i[0] = 4;
        i[1] = i[2] = 0;
        i[3] = rnd;

        return subcmd_send_lobby_item(l, req, i);
    }

    return 0;
}

/* Generate an item drop from the PT data. This version uses the v3 PT data set.
   This function only works for PSOGC. */
int pt_generate_v3_drop(lobby_t *l, void *r) {
    subcmd_itemreq_t *req = (subcmd_itemreq_t *)r;
    pt_v3_entry_t *ent = &v3_ptdata[l->episode - 1][l->difficulty][l->section];
    uint8_t dar;
    uint32_t rnd;
    uint16_t t1, t2;
    uint32_t i[4];

    dar = ent->enemy_dar[req->pt_index];

    /* See if the enemy is going to drop anything at all this time... */
    rnd = genrand_int32() % 100;

    if(rnd >= dar)
        /* Nope. You get nothing! */
        return 0;

    /* XXXX: For now, just drop meseta... We'll worry about the rest later. */
    t1 = ent->enemy_meseta[req->pt_index][0];
    t2 = ent->enemy_meseta[req->pt_index][1];
    if(t1 < t2)
        rnd = (genrand_int32() % (t2 - t1)) + t1;
    else
        rnd = (uint32_t)t1;

    if(rnd) {
        i[0] = 4;
        i[1] = i[2] = 0;
        i[3] = rnd;

        return subcmd_send_lobby_item(l, req, i);
    }

    return 0;
}

/* Generate an item drop from the PT data. This version uses the v3 PT data set.
   This function only works for PSOBB. */
int pt_generate_bb_drop(lobby_t *l, void *r) {
    subcmd_bb_itemreq_t *req = (subcmd_bb_itemreq_t *)r;
    pt_v3_entry_t *ent;
    uint8_t dar;
    uint32_t rnd;
    uint16_t t1, t2;
    uint32_t i[4];
    item_t *item;
    int rv;

    /* XXXX: Handle Episode 4! */
    if(l->episode == 3)
        return 0;

    ent = &v3_ptdata[l->episode - 1][l->difficulty][l->section];
    dar = ent->enemy_dar[req->pt_index];

    /* See if the enemy is going to drop anything at all this time... */
    rnd = genrand_int32() % 100;

    if(rnd >= dar)
        /* Nope. You get nothing! */
        return 0;

    /* XXXX: For now, just drop meseta... We'll worry about the rest later. */
    t1 = ent->enemy_meseta[req->pt_index][0];
    t2 = ent->enemy_meseta[req->pt_index][1];
    if(t1 < t2)
        rnd = (genrand_int32() % (t2 - t1)) + t1;
    else
        rnd = (uint32_t)t1;

    if(rnd) {
        i[0] = 4;
        i[1] = i[2] = 0;
        i[3] = rnd;

        pthread_mutex_lock(&l->mutex);
        item = lobby_add_item_locked(l, i);
        rv = subcmd_send_bb_lobby_item(l, req, item);
        pthread_mutex_unlock(&l->mutex);

        return rv;
    }

    return 0;
}
