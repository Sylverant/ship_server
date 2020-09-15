/*
    Sylverant Ship Server
    Copyright (C) 2012, 2013, 2014, 2015, 2016, 2020 Lawrence Sebald

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

#include <arpa/inet.h>

#include <sylverant/items.h>
#include <sylverant/debug.h>
#include <sylverant/mtwist.h>

#include <psoarchive/AFS.h>
#include <psoarchive/GSL.h>

#include "ptdata.h"
#include "pmtdata.h"
#include "rtdata.h"
#include "subcmd.h"
#include "items.h"
#include "utils.h"
#include "quests.h"

#define LOG(team, ...) team_log_write(team, TLOG_DROPS, __VA_ARGS__)
#define LOGV(team, ...) team_log_write(team, TLOG_DROPSV, __VA_ARGS__)

#define MIN(x, y) (x < y ? x : y)

static int have_v2pt = 0;
static int have_gcpt = 0;
static int have_bbpt = 0;

static pt_v2_entry_t v2_ptdata[4][10];
static pt_v3_entry_t gc_ptdata[2][4][10];
static pt_v3_entry_t bb_ptdata[2][4][10];

static const int tool_base[28] = {
    Item_Monomate, Item_Dimate, Item_Trimate,
    Item_Monofluid, Item_Difluid, Item_Trifluid,
    Item_Antidote, Item_Antiparalysis, Item_Sol_Atomizer,
    Item_Moon_Atomizer, Item_Star_Atomizer, Item_Telepipe,
    Item_Trap_Vision, Item_Monogrinder, Item_Digrinder,
    Item_Trigrinder, Item_Power_Material, Item_Mind_Material,
    Item_Evade_Material, Item_HP_Material, Item_TP_Material,
    Item_Def_Material, Item_Hit_Material, Item_Luck_Material,
    Item_Scape_Doll, Item_Disk_Lv01, Item_Photon_Drop,
    Item_NoSuchItem
};

/* Elemental attributes, sorted by their ranking. This is based on the table
   that is in Tethealla. This is probably in some data file somewhere, and we
   should probably read it from that data file, but this will work for now. */
static const sylverant_weapon_attr_t attr_list[4][12] = {
    {
        Weapon_Attr_Draw, Weapon_Attr_Heart, Weapon_Attr_Ice,
        Weapon_Attr_Bind, Weapon_Attr_Heat, Weapon_Attr_Shock,
        Weapon_Attr_Dim, Weapon_Attr_Panic, Weapon_Attr_None,
        Weapon_Attr_None, Weapon_Attr_None, Weapon_Attr_None
    },
    {
        Weapon_Attr_Drain, Weapon_Attr_Mind, Weapon_Attr_Frost,
        Weapon_Attr_Hold, Weapon_Attr_Fire, Weapon_Attr_Thunder,
        Weapon_Attr_Shadow, Weapon_Attr_Riot, Weapon_Attr_Masters,
        Weapon_Attr_Charge, Weapon_Attr_None, Weapon_Attr_None
    },
    {
        Weapon_Attr_Fill, Weapon_Attr_Soul, Weapon_Attr_Freeze,
        Weapon_Attr_Seize, Weapon_Attr_Flame, Weapon_Attr_Storm,
        Weapon_Attr_Dark, Weapon_Attr_Havoc, Weapon_Attr_Lords,
        Weapon_Attr_Charge, Weapon_Attr_Spirit, Weapon_Attr_Devils
    },
    {
        Weapon_Attr_Gush, Weapon_Attr_Geist, Weapon_Attr_Blizzard,
        Weapon_Attr_Arrest, Weapon_Attr_Burning, Weapon_Attr_Tempest,
        Weapon_Attr_Hell, Weapon_Attr_Chaos, Weapon_Attr_Kings,
        Weapon_Attr_Charge, Weapon_Attr_Berserk, Weapon_Attr_Demons
    }
};

static const int attr_count[4] = { 8, 10, 12, 12 };

#define EPSILON 0.001f

int pt_read_v2(const char *fn) {
    pso_afs_read_t *a;
    pso_error_t err;
    ssize_t sz;
    int rv = 0, i, j;
#if defined(__BIG_ENDIAN__) || defined(WORDS_BIGENDIAN)
    int k, l;
#endif

    /* Open up the file and make sure it looks sane enough... */
    if(!(a = pso_afs_read_open(fn, 0, &err))) {
        debug(DBG_ERROR, "Cannot read %s: %s\n", fn, pso_strerror(err));
        return -1;
    }

    /* Make sure the archive has the correct number of entries. */
    if(pso_afs_file_count(a) != 40) {
        debug(DBG_ERROR, "%s appears to be incomplete.\n", fn);
        rv = -2;
        goto out;
    }

    /* Parse each entry... */
    for(i = 0; i < 4; ++i) {
        for(j = 0; j < 10; ++j) {
            sz = pso_afs_file_size(a, i * 10 + j);
            if(sz != 0x0940) {
                debug(DBG_ERROR, "%s has invalid sized entry!\n", fn);
                rv = -3;
                goto out;
            }

            /* Only grab the data we care about... */
            sz = sizeof(pt_v2_entry_t);
            if(pso_afs_file_read(a, i * 10 + j, (uint8_t *)&v2_ptdata[i][j],
                                 (size_t)sz) != sz) {
                debug(DBG_ERROR, "Cannot read data from %s!\n", i * 10 + j, fn);
                rv = -4;
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
    pso_afs_read_close(a);
    return rv;
}

int pt_read_v3(const char *fn, int bb) {
    pso_gsl_read_t *a;
    const char difficulties[4] = { 'n', 'h', 'v', 'u' };
    const char *episodes[2] = { "", "l" };
    char filename[32];
    uint32_t hnd;
    const size_t sz = sizeof(pt_v3_entry_t);
    pso_error_t err;
    int rv = 0, i, j, k;
#if !defined(__BIG_ENDIAN__) && !defined(WORDS_BIGENDIAN)
    int l, m;
#endif

    /* Open up the file and make sure it looks sane enough... */
    if(!(a = pso_gsl_read_open(fn, 0, &err))) {
        debug(DBG_ERROR, "Cannot read %s: %s\n", fn, pso_strerror(err));
        return -1;
    }

    /* Make sure the archive has the correct number of entries. */
    if(pso_gsl_file_count(a) != 160) {
        debug(DBG_ERROR, "%s appears to be incomplete.\n", fn);
        rv = -2;
        goto out;
    }

    /* Now, parse each entry... */
    for(i = 0; i < 2; ++i) {
        for(j = 0; j < 4; ++j) {
            for(k = 0; k < 10; ++k) {
                /* Figure out the name of the file in the archive that we're
                   looking for... */
                sprintf(filename, "ItemPT%s%c%d.rel", episodes[i],
                        difficulties[j], k);

                /* Grab a handle to that file. */
                hnd = pso_gsl_file_lookup(a, filename);
                if(hnd == PSOARCHIVE_HND_INVALID) {
                    debug(DBG_ERROR, "%s is missing file %s!\n", fn, filename);
                    rv = -3;
                    goto out;
                }

                /* Make sure the size is correct. */
                if(pso_gsl_file_size(a, hnd) != 0x9E0) {
                    debug(DBG_ERROR, "%s file %s has invalid size!\n", fn,
                          filename);
                    rv = -4;
                    goto out;
                }

                if(bb) {
                    if(pso_gsl_file_read(a, hnd, (uint8_t *)&bb_ptdata[i][j][k],
                                         sz) != sz) {
                        debug(DBG_ERROR, "Error reading %s from %s!\n",
                              filename, fn);
                        rv = -5;
                        goto out;
                    }

                    /* Swap entries, if we need to */
#if !defined(__BIG_ENDIAN__) && !defined(WORDS_BIGENDIAN)
                    for(l = 0; l < 28; ++l) {
                        for(m = 0; m < 10; ++m) {
                            bb_ptdata[i][j][k].tool_frequency[l][m] =
                                ntohs(bb_ptdata[i][j][k].tool_frequency[l][m]);
                        }
                    }

                    for(l = 0; l < 23; ++l) {
                        for(m = 0; m < 6; ++m) {
                            bb_ptdata[i][j][k].percent_pattern[l][m] =
                                ntohs(bb_ptdata[i][j][k].percent_pattern[l][m]);
                        }
                    }

                    for(l = 0; l < 100; ++l) {
                        for(m = 0; m < 2; ++m) {
                            bb_ptdata[i][j][k].enemy_meseta[l][m] =
                                ntohs(bb_ptdata[i][j][k].enemy_meseta[l][m]);
                        }
                    }

                    for(l = 0; l < 10; ++l) {
                        for(m = 0; m < 2; ++m) {
                            bb_ptdata[i][j][k].box_meseta[l][m] =
                                ntohs(bb_ptdata[i][j][k].box_meseta[l][m]);
                        }
                    }

                    for(l = 0; l < 18; ++l) {
                        bb_ptdata[i][j][k].pointers[l] =
                            ntohl(bb_ptdata[i][j][k].pointers[l]);
                    }

                    bb_ptdata[i][j][k].armor_level =
                        ntohl(bb_ptdata[i][j][k].armor_level);
#endif
                }
                else {
                    if(pso_gsl_file_read(a, hnd, (uint8_t *)&gc_ptdata[i][j][k],
                                         sz) != sz) {
                        debug(DBG_ERROR, "Error reading %s from %s!\n",
                              filename, fn);
                        rv = -5;
                        goto out;
                    }

#ifdef DEBUG
                    debug(DBG_LOG, "PSOGC ItemPT.gsl pointer for file %s: %p\n",
                          filename, &gc_ptdata[i][j][k]);
#endif

                    /* Swap entries, if we need to */
#if !defined(__BIG_ENDIAN__) && !defined(WORDS_BIGENDIAN)
                    for(l = 0; l < 28; ++l) {
                        for(m = 0; m < 10; ++m) {
                            gc_ptdata[i][j][k].tool_frequency[l][m] =
                                ntohs(gc_ptdata[i][j][k].tool_frequency[l][m]);
                        }
                    }

                    for(l = 0; l < 23; ++l) {
                        for(m = 0; m < 6; ++m) {
                            gc_ptdata[i][j][k].percent_pattern[l][m] =
                                ntohs(gc_ptdata[i][j][k].percent_pattern[l][m]);
                        }
                    }

                    for(l = 0; l < 100; ++l) {
                        for(m = 0; m < 2; ++m) {
                            gc_ptdata[i][j][k].enemy_meseta[l][m] =
                                ntohs(gc_ptdata[i][j][k].enemy_meseta[l][m]);
                        }
                    }

                    for(l = 0; l < 10; ++l) {
                        for(m = 0; m < 2; ++m) {
                            gc_ptdata[i][j][k].box_meseta[l][m] =
                                ntohs(gc_ptdata[i][j][k].box_meseta[l][m]);
                        }
                    }

                    for(l = 0; l < 18; ++l) {
                        gc_ptdata[i][j][k].pointers[l] =
                            ntohl(gc_ptdata[i][j][k].pointers[l]);
                    }

                    gc_ptdata[i][j][k].armor_level =
                        ntohl(gc_ptdata[i][j][k].armor_level);
#endif
                }
            }
        }
    }

    if(bb)
        have_bbpt = 1;
    else
        have_gcpt = 1;

out:
    pso_gsl_read_close(a);
    return rv;
}

int pt_v2_enabled(void) {
    return have_v2pt;
}

int pt_gc_enabled(void) {
    return have_gcpt;
}

int pt_bb_enabled(void) {
    return have_bbpt;
}

/*
   Generate a random weapon, based off of data for PSOv2. This is a rather ugly
   process, so here's a "short" description of how it actually works (note, the
   code itself will probably end up being shorter than the description (at least
   in number of bytes), but I felt this was needed to explain the insanity
   below)...

   First, we have to decide what weapon types are actually going to be available
   to generate. This involves looking at three things, the weapon ratio for the
   weapon type in the PT data, the minimum rank information (also in the PT
   data) and the area we're generating for. If the weapon ratio is zero, we
   don't have to look any further (this is expected to be a somewhat rare
   occurance, hence why its actually checked second in the code). Next, take the
   minimum rank and add the area number to it (where 0 = Forest 1, 9 = Ruins 3
   boss areas use the area that comes immediately after them, except Falz, which
   uses Ruins 3). If that result is greater than or equal to zero, then we can
   actually generate this type of weapon, so we add it to the list of weapons we
   might generate, and add its ratio into the total.

   The next thing we have to do is to decide what rank of weapon will be
   generated for each valid weapon type. To do this, we need to look at three
   pieces of data again: the weapon type's minimum rank, the weapon type's
   upgrade floor value (once again, in the PT data), and the area number. If the
   minimum rank is greater than or equal to zero, start with that value as the
   rank that will be generated. Otherwise, start with 0. Next, calculate the
   effective area for the rest of the calculation. For weapon types where the
   minimum rank is greater than or equal to zero, this will be the area number
   itself. For those where the minimum rank is less than zero, this will be the
   sum of the minimum rank and the area. Next, take the upgrade floor value and
   subtract it from the effective area. If the result is greater than or equal
   to zero, add one to the weapon's rank. Continue doing this until the result
   is less than zero.

   From here, we can actually decide what weapon itself to generate. To do this,
   simply take sum of the chances of all valid weapon types and generate a
   random number between 0 and that sum (including 0, excluding the sum). Then,
   run through and compare with the chances that each type has until you figure
   out what type to generate. Shift the weapon rank that was calculated above
   into place with the weapon type value, and there you go.

   To pick a grind value, take the remainder from the upgrade floor/effective
   area calculation above, and use that as an index into the power patterns in
   the PT data (the second index, not the first one). If this value is greater
   than 3 for some reason, cap it at 3. The power paterns in here should be used
   to decide what grind value to use. Pick a random number between 0 and 100
   (not including 100, but including 0) and see where you end up in the array.
   Whatever index you end up on is the grind value to use.

   To deal with percentages, we have a few things from the PT data to look at.
   For each of the three percentage slots, look at the "area pattern" element
   for the slot and the area the weapon is being generated for. This controls
   which set of items in the percent pattern to look at. If this is less than
   zero, move onto the next possible slot. From there, generate a random number
   between 0 and 100 (not including 100, including 0) and look through that
   column of the matrix formed by the percent pattern to figure out where you
   fall. The value of the percentage will be five times the quantity of whatever
   row of the matrix the random number ends up resulting in being selected,
   minus two. If its zero, you can obviously stop processing here, and move onto
   the next possible slot. Next, generate a new random number between 0 and 100,
   following the same rules as always. For this one, look through the percent
   attachment matrix with the random number (with the area as the column, as
   usual). If you end up in the zeroth row, then you won't actually apply a
   percentage. If not, set the value in the weapon in the appropriate place and
   put the percentage in too. Now you can finally move onto the next slot!
   Wasn't that fun? Yes, you could rearrange the random choices in here to be a
   bit more sane, but that's less fun!

   To pick an element, first check if the area has elements available. This is
   done by looking at the PT data's element ranking and element probability for
   the area. If either is 0, then there shall be no elements on weapons made on
   that floor. If both are non-zero, then generate a random number between 0 and
   100 (not including 100, but including 0). If that value is less than the
   elemental probability for the floor, then look and see how many elements are
   on the ranking level for the floor. Generate a random number to index into
   the elemental grid for that ranking level, and set that as the elemental
   attribute byte. Also, set the high-order bit of that byte so that the user
   has to take the item to the tekker.

   That takes care of all of the tasks with generating a random common weapon!
   If you're still reading this at the end of this comment, I sorta feel a bit
   sorry for you. Just think how I feel while writing the comment and the code
   below. :P
*/
static int generate_weapon_v2(pt_v2_entry_t *ent, int area, uint32_t item[4],
                              struct mt19937_state *rng, int picked, int v1,
                              lobby_t *l) {
    uint32_t rnd, upcts = 0;
    int i, j = 0, k, wchance = 0, warea = 0, npcts = 0;
    int wtypes[12] = { 0 }, wranks[12] = { 0 }, gptrn[12] = { 0 };
    uint8_t *item_b = (uint8_t *)item;
    int semirare = 0, rare = 0;

    /* Ugly... but I'm lazy and don't feel like rebalancing things for another
       level of indentation... */
    if(picked) {
        /* Determine if the item is a rare or a "semi-rare" item. */
        if(item_b[1] > 12)
            rare = semirare = 1;
        else if((item_b[1] >= 10 && item_b[2] > 3) || item_b[2] > 4)
            semirare = 1;

        /* All RT-generated items use the last grind pattern... */
        warea = 3;
        item[1] = item[2] = item[3] = 0;
        goto already_picked;
    }

    item[0] = item[1] = item[2] = item[3] = 0;

    /* Go through each weapon type to see what ones we actually will have to
       work with right now... */
    for(i = 0; i < 12; ++i) {
        if((ent->weapon_minrank[i] + area) >= 0 && ent->weapon_ratio[i] > 0) {
            wtypes[j] = i;
            wchance += ent->weapon_ratio[i];

            if(ent->weapon_minrank[i] >= 0) {
                warea = area;
                wranks[j] = ent->weapon_minrank[i];
            }
            else {
                warea = ent->weapon_minrank[i] + area;
                wranks[j] = 0;
            }

            /* Sanity check... Make sure this is sane before we go to the loop
               below, since it will end up being an infinite loop if its not
               sane... */
            if(ent->weapon_upgfloor[i] <= 0) {
                debug(DBG_WARN, "Invalid v2 weapon upgrade floor value for "
                      "floor %d, weapon type %d. Please check your ItemPT.afs "
                      "file for validity!\n", area, i);
                return -1;
            }

            while((warea - ent->weapon_upgfloor[i]) >= 0) {
                ++wranks[j];
                warea -= ent->weapon_upgfloor[i];
            }

            gptrn[j] = MIN(warea, 3);
            ++j;
        }
    }

    /* Sanity check... This shouldn't happen! */
    if(!j) {
        debug(DBG_WARN, "No v2 weapon to generate on floor %d, please check "
              "your ItemPT.afs file for validity!\n", area);
        return -1;
    }

    /* Roll the dice! */
    rnd = mt19937_genrand_int32(rng) % wchance;
    for(i = 0; i < j; ++i) {
        if((rnd -= ent->weapon_ratio[wtypes[i]]) > wchance) {
            item[0] = ((wtypes[i] + 1) << 8) | (wranks[i] << 16);

            /* Save off the grind pattern to use... */
            warea = gptrn[i];
            break;
        }
    }

    /* Sanity check... Once again, this shouldn't happen! */
    if(!item[0]) {
        debug(DBG_WARN, "Generated invalid v2 weapon. Please report this "
              "error!\n");
        return -1;
    }

    /* See if we made a "semi-rare" item. */
    if((item_b[1] >= 10 && item_b[2] > 3) || item_b[2] > 4)
        semirare = 1;

already_picked:
    /* Next up, determine the grind value. */
    rnd = mt19937_genrand_int32(rng) % 100;
    for(i = 0; i < 9; ++i) {
        if((rnd -= ent->power_pattern[i][warea]) > 100) {
            item[0] |= (i << 24);
            break;
        }
    }

    /* Sanity check... */
    if(i >= 9) {
        debug(DBG_WARN, "Invalid power pattern for floor %d, pattern "
              "number %d. Please check your ItemPT.afs for validity!\n",
              area, warea);
        return -1;
    }

    /* Let's generate us some percentages, shall we? This isn't necessarily the
       way I would have designed this, but based on the way the data is laid
       out in the PT file, this is the implied structure of it... */
    for(i = 0; i < 3; ++i) {
        if(ent->area_pattern[i][area] < 0)
            continue;

        rnd = mt19937_genrand_int32(rng) % 100;
        warea = ent->area_pattern[i][area];

        for(j = 0; j < 23; ++j) {
            /* See if we're going to generate this one... */
            if((rnd -= ent->percent_pattern[j][warea]) > 100) {
                /* If it would be 0%, don't bother... */
                if(j == 2) {
                    break;
                }

                /* Lets see what type we'll generate now... */
                rnd = mt19937_genrand_int32(rng) % 100;
                for(k = 0; k < 6; ++k) {
                    if((rnd -= ent->percent_attachment[k][area]) > 100) {
                        if(k == 0 || (upcts & (1 << k)))
                            break;

                        j = (j - 2) * 5;
                        item_b[(npcts << 1) + 6] = k;
                        item_b[(npcts << 1) + 7] = (uint8_t)j;
                        ++npcts;
                        upcts |= 1 << k;
                        break;
                    }
                }

                break;
            }
        }
    }

    /* Finally, lets see if there's going to be an elemental attribute applied
       to this weapon, or if its rare and we need to set the flag. */
    if(!semirare && ent->element_ranking[area]) {
        rnd = mt19937_genrand_int32(rng) % 100;
        if(rnd < ent->element_probability[area]) {
            rnd = mt19937_genrand_int32(rng) %
                attr_count[ent->element_ranking[area] - 1];
            item[1] = 0x80 | attr_list[ent->element_ranking[area] - 1][rnd];
        }
    }
    else if(rare || (v1 && semirare)) {
        item[1] = 0x80;
    }

    return 0;
}

static int generate_weapon_v3(pt_v3_entry_t *ent, int area, uint32_t item[4],
                              struct mt19937_state *rng, int picked, int bb,
                              lobby_t *l) {
    uint32_t rnd, upcts = 0;
    int i, j = 0, k, wchance = 0, warea = 0, npcts = 0;
    int wtypes[12] = { 0 }, wranks[12] = { 0 }, gptrn[12] = { 0 };
    uint8_t *item_b = (uint8_t *)item;
    int semirare = 0, rare = 0;

    /* Ugly... but I'm lazy and don't feel like rebalancing things for another
       level of indentation... */
    if(picked) {
        /* Determine if the item is a rare or a "semi-rare" item. */
        if(item_b[1] > 12)
            rare = semirare = 1;
        else if((item_b[1] >= 10 && item_b[2] > 3) || item_b[2] > 4)
            semirare = 1;

        /* All RT-generated items use the last grind pattern... */
        warea = 3;
        item[1] = item[2] = item[3] = 0;
        goto already_picked;
    }

    item[0] = item[1] = item[2] = item[3] = 0;

    /* Go through each weapon type to see what ones we actually will have to
       work with right now... */
    for(i = 0; i < 12; ++i) {
        if((ent->weapon_minrank[i] + area) >= 0 && ent->weapon_ratio[i] > 0) {
            wtypes[j] = i;
            wchance += ent->weapon_ratio[i];

            if(ent->weapon_minrank[i] >= 0) {
                warea = area;
                wranks[j] = ent->weapon_minrank[i];
            }
            else {
                warea = ent->weapon_minrank[i] + area;
                wranks[j] = 0;
            }

            /* Sanity check... Make sure this is sane before we go to the loop
               below, since it will end up being an infinite loop if its not
               sane... */
            if(ent->weapon_upgfloor[i] <= 0) {
                debug(DBG_WARN, "Invalid v3 weapon upgrade floor value for "
                      "floor %d, weapon type %d. Please check your ItemPT.gsl "
                      "file (%s) for validity!\n", area, i, bb ? "BB" : "GC");
                return -1;
            }

            while((warea - ent->weapon_upgfloor[i]) >= 0) {
                ++wranks[j];
                warea -= ent->weapon_upgfloor[i];
            }

            gptrn[j] = MIN(warea, 3);
            ++j;
        }
    }

    /* Sanity check... This shouldn't happen! */
    if(!j) {
        debug(DBG_WARN, "No v3 weapon to generate on floor %d, please check "
              "your ItemPT.gsl file (%s) for validity!\n", area,
              bb ? "BB" : "GC");
        return -1;
    }

    /* Roll the dice! */
    rnd = mt19937_genrand_int32(rng) % wchance;
    for(i = 0; i < j; ++i) {
        if((rnd -= ent->weapon_ratio[wtypes[i]]) > wchance) {
            item[0] = ((wtypes[i] + 1) << 8) | (wranks[i] << 16);

            /* Save off the grind pattern to use... */
            warea = gptrn[i];
            break;
        }
    }

    /* Sanity check... Once again, this shouldn't happen! */
    if(!item[0]) {
        debug(DBG_WARN, "Generated invalid v3 weapon. Please report this "
              "error!\n");
        return -1;
    }

    /* See if we made a "semi-rare" item. */
    if((item_b[1] >= 10 && item_b[2] > 3) || item_b[2] > 4)
        semirare = 1;

already_picked:
    /* Next up, determine the grind value. */
    rnd = mt19937_genrand_int32(rng) % 100;
    for(i = 0; i < 9; ++i) {
        if((rnd -= ent->power_pattern[i][warea]) > 100) {
            item[0] |= (i << 24);
            break;
        }
    }

    /* Sanity check... */
    if(i >= 9) {
        debug(DBG_WARN, "Invalid power pattern for floor %d, pattern "
              "number %d. Please check your ItemPT.gsl (%s) for validity!\n",
              area, warea, bb ? "BB" : "GC");
        return -1;
    }

    /* Let's generate us some percentages, shall we? This isn't necessarily the
       way I would have designed this, but based on the way the data is laid
       out in the PT file, this is the implied structure of it... */
    for(i = 0; i < 3; ++i) {
        if(ent->area_pattern[i][area] < 0)
            continue;

        rnd = mt19937_genrand_int32(rng) % 10000;
        warea = ent->area_pattern[i][area];

        for(j = 0; j < 23; ++j) {
            /* See if we're going to generate this one... */
            if((rnd -= ent->percent_pattern[j][warea]) > 10000) {
                /* If it would be 0%, don't bother... */
                if(j == 2) {
                    break;
                }

                /* Lets see what type we'll generate now... */
                rnd = mt19937_genrand_int32(rng) % 100;
                for(k = 0; k < 6; ++k) {
                    if((rnd -= ent->percent_attachment[k][area]) > 100) {
                        if(k == 0 || (upcts & (1 << k)))
                            break;

                        j = (j - 2) * 5;
                        item_b[(npcts << 1) + 6] = k;
                        item_b[(npcts << 1) + 7] = (uint8_t)j;
                        ++npcts;
                        upcts |= 1 << k;
                        break;
                    }
                }

                break;
            }
        }
    }

    /* Finally, lets see if there's going to be an elemental attribute applied
       to this weapon, or if its rare and we need to set the flag. */
    if(!semirare && ent->element_ranking[area]) {
        rnd = mt19937_genrand_int32(rng) % 100;
        if(rnd < ent->element_probability[area]) {
            rnd = mt19937_genrand_int32(rng) %
                attr_count[ent->element_ranking[area] - 1];
            item[1] = 0x80 | attr_list[ent->element_ranking[area] - 1][rnd];
        }
    }
    else if(rare) {
        item[1] = 0x80;
    }

    return 0;
}

/*
   Generate a random armor, based on data for PSOv2. Luckily, this is a lot
   simpler than the case for weapons, so it needs a lot less explanation.

   Each floor has 5 "virtual slots" for different armor types that can be
   dropped on the floor. The armors that are in these slots depends on two
   things: the index of the floor (0-9) and the armor level value in the ItemPT
   file. The basic pattern for a slot is as follows (for i in [0, 4]):
     slot[i] = MAX(0, armor_level - 3 + floor + i)
   So, for example, using a Whitill Section ID character on Normal difficulty
   (where the armor level = 0), the slots for Forest 1 would be filled as
   follows: { 0, 0, 0, 0, 1 }. The first three slots all generate negative
   values which get clipped off to 0 by the MAX operator above.

   The values in the slots correspond to the low-order byte of the high-order
   word of the first dword of the item data (or the 3rd byte of the item data,
   in other words). The low-order word of the first dword is always 0x0101, to
   say that it is a frame/armor.

   The probability of getting each of the armors above is controlled by the
   armor ranking array in the ItemPT data. This is just a list of probabilities
   (out of 100) for each slot, much like what is done with the weapon data. I
   won't bother to explain how to pick random numbers here again.

   Each armor has up to 4 unit slots in it. The amount of unit slots is
   determined by looking at the probabilities in the slot ranking array in the
   ItemPT data. Once again, this is a list of probabilities out of 100. The
   index you end up in determines how many unit slots you get, from 0 to 4.

   Random DFP and EVP boosts require data from the ItemPMT.prs file (see the
   pmtdata.[ch] files for more information about that). Basically, they're
   handled by generating a random number in [0, max] where max is the dfp or
   evp range defined in the PMT data.
*/
static int generate_armor_v2(pt_v2_entry_t *ent, int area, uint32_t item[4],
                             struct mt19937_state *rng, int picked,
                             lobby_t *l) {
    uint32_t rnd;
    int i, armor = -1;
    uint8_t *item_b = (uint8_t *)item;
    uint16_t *item_w = (uint16_t *)item;
    pmt_guard_v2_t guard;

    if(!picked) {
        /* Go through each slot in the armor rankings to figure out which one
           that we'll be generating. */
        rnd = mt19937_genrand_int32(rng) % 100;

#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "generate_armor_v2: RNG picked %" PRIu32 "\n", rnd);
#endif

        for(i = 0; i < 5; ++i) {
#ifdef DEBUG
            if(l->flags & LOBBY_FLAG_DBG_SDROPS)
                debug(DBG_LOG, "    Index %d, value: %" PRId8 ", left: %"
                      PRIu32 "\n", i, ent->armor_ranking[i], rnd);
#endif

            if((rnd -= ent->armor_ranking[i]) > 100) {
                armor = i;
                break;
            }
        }

        /* Sanity check... */
        if(armor == -1) {
            debug(DBG_WARN, "Couldn't find a v2 armor to generate. Please "
                  "check your ItemPT.afs file for validity!\n");
            return -1;
        }

        /* Figure out what the byte we'll use is */
        armor = ((int)ent->armor_level) - 3 + area + armor;

        if(armor < 0)
            armor = 0;

#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "    Armor level byte: %d (%d + %d + %d - 3)\n",
                  armor, (int)ent->armor_level, area, i);
#endif

        item[0] = 0x00000101 | (armor << 16);
    }

    item[1] = item[2] = item[3] = 0;

    /* Pick a number of unit slots */
    rnd = mt19937_genrand_int32(rng) % 100;

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "generate_armor_v2: RNG picked %" PRIu32 " for slots\n",
              rnd);
#endif

    for(i = 0; i < 5; ++i) {
#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "    Index %d, value: %" PRId8 ", left: %"
                  PRIu32 "\n", i, ent->slot_ranking[i], rnd);
#endif

        if((rnd -= ent->slot_ranking[i]) > 100) {
            item_b[5] = i;
            break;
        }
    }

    /* Look up the item in the ItemPMT data so we can see what boosts we might
       apply... */
    if(pmt_lookup_guard_v2(item[0], &guard)) {
        debug(DBG_WARN, "ItemPMT.prs file for v2 seems to be missing an armor "
              "type item (code %08x).\n", item[0]);
        return -2;
    }

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "generate_armor_v2: DFP Range: %d, EVP Range: %d\n",
              guard.dfp_range, guard.evp_range);
#endif

    if(guard.dfp_range) {
        rnd = mt19937_genrand_int32(rng) % (guard.dfp_range + 1);
        item_w[3] = (uint16_t)rnd;
    }

    if(guard.evp_range) {
        rnd = mt19937_genrand_int32(rng) % (guard.evp_range + 1);
        item_w[4] = (uint16_t)rnd;
    }

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "    DFP: %" PRIu16 ", EVP: %" PRIu16 "\n", item_w[3],
              item_w[4]);
#endif

    return 0;
}

static int generate_armor_v3(pt_v3_entry_t *ent, int area, uint32_t item[4],
                             struct mt19937_state *rng, int picked, int bb,
                             lobby_t *l) {
    uint32_t rnd;
    int i, armor = -1;
    uint8_t *item_b = (uint8_t *)item;
    uint16_t *item_w = (uint16_t *)item;
    pmt_guard_gc_t gcg;
    pmt_guard_bb_t bbg;
    uint8_t dfp, evp;

    if(!picked) {
        /* Go through each slot in the armor rankings to figure out which one
           that we'll be generating. */
        rnd = mt19937_genrand_int32(rng) % 100;

#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "generate_armor_v3: RNG picked %" PRIu32 "\n", rnd);
#endif

        for(i = 0; i < 5; ++i) {
#ifdef DEBUG
            if(l->flags & LOBBY_FLAG_DBG_SDROPS)
                debug(DBG_LOG, "    Index %d, value: %" PRId8 ", left: %"
                      PRIu32 "\n", i, ent->armor_ranking[i], rnd);
#endif

            if((rnd -= ent->armor_ranking[i]) > 100) {
                armor = i;
                break;
            }
        }

        /* Sanity check... */
        if(armor == -1) {
            debug(DBG_WARN, "Couldn't find a %s armor to generate. Please "
                  "check your ItemPT.gsl file for validity!\n",
                  bb ? "BB" : "GC");
            return -1;
        }

        /* Figure out what the byte we'll use is */
        armor = ((int)ent->armor_level) - 3 + area + armor;

        if(armor < 0)
            armor = 0;

#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "    Armor level byte: %d (%d + %d + %d - 3)\n",
                  armor, (int)ent->armor_level, area, i);
#endif

        item[0] = 0x00000101 | (armor << 16);
    }

    item[1] = item[2] = item[3] = 0;

    /* Pick a number of unit slots */
    rnd = mt19937_genrand_int32(rng) % 100;
#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "generate_armor_v3: RNG picked %" PRIu32 " for slots\n",
              rnd);
#endif

    for(i = 0; i < 5; ++i) {
#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "    Index %d, value: %" PRId8 ", left: %"
                  PRIu32 "\n", i, ent->slot_ranking[i], rnd);
#endif

        if((rnd -= ent->slot_ranking[i]) > 100) {
            item_b[5] = i;
            break;
        }
    }

    /* Look up the item in the ItemPMT data so we can see what boosts we might
       apply... */
    if(!bb) {
        if(pmt_lookup_guard_gc(item[0], &gcg)) {
            debug(DBG_WARN, "ItemPMT.prs file for GC seems to be missing an "
                  "armor type item (code %08x).\n", item[0]);
            return -2;
        }

        dfp = gcg.dfp_range;
        evp = gcg.evp_range;
    }
    else {
        if(pmt_lookup_guard_bb(item[0], &bbg)) {
            debug(DBG_WARN, "ItemPMT.prs file for BB seems to be missing an "
                  "armor type item (code %08x).\n", item[0]);
            return -2;
        }

        dfp = bbg.dfp_range;
        evp = bbg.evp_range;
    }

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "generate_amor_v3: DFP Range: %d, EVP Range: %d\n",
              dfp, evp);
#endif

    if(dfp) {
        rnd = mt19937_genrand_int32(rng) % (dfp + 1);
        item_w[3] = (uint16_t)rnd;
    }

    if(evp) {
        rnd = mt19937_genrand_int32(rng) % (evp + 1);
        item_w[4] = (uint16_t)rnd;
    }

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "    DFP: %" PRIu16 ", EVP: %" PRIu16 "\n", item_w[3],
              item_w[4]);
#endif

    return 0;
}

/* Generate a random shield, based on data for PSOv2. This is exactly the same
   as the armor version, but without unit slots. */
static int generate_shield_v2(pt_v2_entry_t *ent, int area, uint32_t item[4],
                              struct mt19937_state *rng, int picked,
                              lobby_t *l) {
    uint32_t rnd;
    int i, armor = -1;
    uint16_t *item_w = (uint16_t *)item;
    pmt_guard_v2_t guard;

    if(!picked) {
        /* Go through each slot in the armor rankings to figure out which one
           that we'll be generating. */
        rnd = mt19937_genrand_int32(rng) % 100;

#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "generate_shield_v2: RNG picked %" PRIu32 "\n", rnd);
#endif

        for(i = 0; i < 5; ++i) {
#ifdef DEBUG
            if(l->flags & LOBBY_FLAG_DBG_SDROPS)
                debug(DBG_LOG, "    Index %d, value: %" PRId8 ", left: %"
                      PRIu32 "\n", i, ent->armor_ranking[i], rnd);
#endif

            if((rnd -= ent->armor_ranking[i]) > 100) {
                armor = i;
                break;
            }
        }

        /* Sanity check... */
        if(armor == -1) {
            debug(DBG_WARN, "Couldn't find a v2 shield to generate. Please "
                  "check your ItemPT.afs file for validity!\n");
            return -1;
        }

        /* Figure out what the byte we'll use is */
        armor = ((int)ent->armor_level) - 3 + area + armor;

        if(armor < 0)
            armor = 0;

#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "    Armor level byte: %d (%d + %d + %d - 3)\n",
                  armor, (int)ent->armor_level, area, i);
#endif

        item[0] = 0x00000201 | (armor << 16);
    }

    item[1] = item[2] = item[3] = 0;

    /* Look up the item in the ItemPMT data so we can see what boosts we might
       apply... */
    if(pmt_lookup_guard_v2(item[0], &guard)) {
        debug(DBG_WARN, "ItemPMT.prs file for v2 seems to be missing a shield "
              "type item (code %08x).\n", item[0]);
        return -2;
    }

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "generate_shield_v2: DFP Range: %d, EVP Range: %d\n",
              guard.dfp_range, guard.evp_range);
#endif

    if(guard.dfp_range) {
        rnd = mt19937_genrand_int32(rng) % (guard.dfp_range + 1);
        item_w[3] = (uint16_t)rnd;
    }

    if(guard.evp_range) {
        rnd = mt19937_genrand_int32(rng) % (guard.evp_range + 1);
        item_w[4] = (uint16_t)rnd;
    }

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "    DFP: %" PRIu16 ", EVP: %" PRIu16 "\n", item_w[3],
              item_w[4]);
#endif

    return 0;
}

static int generate_shield_v3(pt_v3_entry_t *ent, int area, uint32_t item[4],
                              struct mt19937_state *rng, int picked, int bb,
                              lobby_t *l) {
    uint32_t rnd;
    int i, armor = -1;
    uint16_t *item_w = (uint16_t *)item;
    pmt_guard_gc_t gcg;
    pmt_guard_bb_t bbg;
    uint8_t dfp, evp;

    if(!picked) {
        /* Go through each slot in the armor rankings to figure out which one
           that we'll be generating. */
        rnd = mt19937_genrand_int32(rng) % 100;

#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "generate_shield_v3: RNG picked %" PRIu32 "\n", rnd);
#endif

        for(i = 0; i < 5; ++i) {
#ifdef DEBUG
            if(l->flags & LOBBY_FLAG_DBG_SDROPS)
                debug(DBG_LOG, "    Index %d, value: %" PRId8 ", left: %"
                      PRIu32 "\n", i, ent->armor_ranking[i], rnd);
#endif

            if((rnd -= ent->armor_ranking[i]) > 100) {
                armor = i;
                break;
            }
        }

        /* Sanity check... */
        if(armor == -1) {
            debug(DBG_WARN, "Couldn't find a %s shield to generate. Please "
                  "check your ItemPT.gsl file for validity!\n",
                  bb ? "BB" : "GC");
            return -1;
        }

        /* Figure out what the byte we'll use is */
        armor = ((int)ent->armor_level) - 3 + area + armor;

        if(armor < 0)
            armor = 0;

#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "    Armor level byte: %d (%d + %d + %d - 3)\n",
                  armor, (int)ent->armor_level, area, i);
#endif

        item[0] = 0x00000201 | (armor << 16);
    }

    item[1] = item[2] = item[3] = 0;

    /* Look up the item in the ItemPMT data so we can see what boosts we might
       apply... */
    if(!bb) {
        if(pmt_lookup_guard_gc(item[0], &gcg)) {
            debug(DBG_WARN, "ItemPMT.prs file for GC seems to be missing a "
                  "shield type item (code %08x).\n", item[0]);
            return -2;
        }

        dfp = gcg.dfp_range;
        evp = gcg.evp_range;
    }
    else {
        if(pmt_lookup_guard_bb(item[0], &bbg)) {
            debug(DBG_WARN, "ItemPMT.prs file for BB seems to be missing a "
                  "shield type item (code %08x).\n", item[0]);
            return -2;
        }

        dfp = bbg.dfp_range;
        evp = bbg.evp_range;
    }

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "generate_shield_v3: DFP Range: %d, EVP Range: %d\n",
              dfp, evp);
#endif

    if(dfp) {
        rnd = mt19937_genrand_int32(rng) % (dfp + 1);
        item_w[3] = (uint16_t)rnd;
    }

    if(evp) {
        rnd = mt19937_genrand_int32(rng) % (evp + 1);
        item_w[4] = (uint16_t)rnd;
    }

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "    DFP: %" PRIu16 ", EVP: %" PRIu16 "\n", item_w[3],
              item_w[4]);
#endif

    return 0;
}

static uint32_t generate_tool_base(uint16_t freqs[28][10], int area,
                                   struct mt19937_state *rng, lobby_t *l) {
    uint32_t rnd = mt19937_genrand_int32(rng) % 10000;
    int i;

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS) {
        debug(DBG_LOG, "generate_tool_base: RNG picked %" PRIu32 "\n", rnd);
    }
#endif

    for(i = 0; i < 28; ++i) {
#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "    index: %d, code %08" PRIx32 " chance: %" PRIu16
                  " left: %" PRIu32 "\n", i, tool_base[i], LE16(freqs[i][area]),
                  rnd);
#endif
        if((rnd -= LE16(freqs[i][area])) > 10000) {
#ifdef DEBUG
            if(l->flags & LOBBY_FLAG_DBG_SDROPS)
                debug(DBG_LOG, "    Generating item.\n");
#endif

            return tool_base[i];
        }
    }

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "    Can't find item to drop! This shouldn't happen.\n");
#endif

    return Item_NoSuchItem;
}

/* XXXX: There's something afoot here generating invalid techs. */
static int generate_tech(uint8_t freqs[19][10], int8_t levels[19][20],
                         int area, uint32_t item[4],
                         struct mt19937_state *rng, lobby_t *l) {
    uint32_t rnd, tech, level;
    int8_t t1, t2;
    int i;

    rnd = mt19937_genrand_int32(rng);
    tech = rnd % 1000;

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "generate_tech: RNG generated %" PRIu32 " tech part: %"
              PRIu32 "\n", rnd, tech);
#endif

    rnd /= 1000;

    for(i = 0; i < 19; ++i) {
#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "    index: %d, chance: %" PRIu8 " left: %" PRIu32
                  "\n", i, freqs[i][area], tech);
#endif

        if((tech -= freqs[i][area]) > 1000) {
#ifdef DEBUG
            if(l->flags & LOBBY_FLAG_DBG_SDROPS)
                debug(DBG_LOG, "    Generating tech.\n");
#endif

            t1 = levels[i][area << 1];
            t2 = levels[i][(area << 1) + 1];

#ifdef DEBUG
            if(l->flags & LOBBY_FLAG_DBG_SDROPS)
                debug(DBG_LOG, "    Min: %" PRId8 " Max: " PRId8 "\n", t1, t2);
#endif

            /* Make sure that the minimum level isn't -1 and that the minimum is
               actually less than the maximum. */
            if(t1 == -1 || t1 > t2) {
                debug(DBG_WARN, "Invalid tech level set for area %d, tech %d\n",
                      area, i);
                return -1;
            }

            /* Cap the levels from the ItemPT data, since Sega's files sometimes
               have stupid values here. */
            if(t1 >= 30)
                t1 = 29;

            if(t2 >= 30)
                t2 = 29;

            if(t1 < t2)
                level = (rnd % ((t2 + 1) - t1)) + t1;
            else
                level = t1;

#ifdef DEBUG
            if(l->flags & LOBBY_FLAG_DBG_SDROPS)
                debug(DBG_LOG, "    Level selected: %" PRIu32 "\n", level);
#endif

            item[1] = i;
            item[0] |= (level << 16);
            return 0;
        }
    }

    /* Shouldn't get here... */
    debug(DBG_WARN, "Couldn't find technique to drop for area %d!\n", area);
    return -1;
}

static int generate_tool_v2(pt_v2_entry_t *ent, int area, uint32_t item[4],
                            struct mt19937_state *rng, lobby_t *l) {
    item[0] = generate_tool_base(ent->tool_frequency, area, rng, l);

    /* Neither of these should happen, but just in case... */
    if(item[0] == Item_Photon_Drop || item[0] == Item_NoSuchItem) {
        debug(DBG_WARN, "Generated invalid v2 tool! Please check your "
              "ItemPT.afs file for validity!\n");
        return -1;
    }

    /* Clear the rest of the item. */
    item[1] = item[2] = item[3] = 0;

    /* If its a stackable item, make sure to give it a quantity of 1 */
    if(item_is_stackable(item[0])) {
#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "Item is stackable. Setting quantity to 1.\n");
#endif

        item[1] = (1 << 8);
    }

    if(item[0] == Item_Disk_Lv01) {
#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "Item is technique disk. Picking technique.\n");
#endif

        if(generate_tech(ent->tech_frequency, ent->tech_levels, area,
                         item, rng, l)) {
            debug(DBG_WARN, "Generated invalid technique! Please check "
                  "your ItemPT.afs file for validity!\n");
            return -1;
        }
    }

    return 0;
}

static int generate_tool_v3(pt_v3_entry_t *ent, int area, uint32_t item[4],
                            struct mt19937_state *rng, lobby_t *l) {
    item[0] = generate_tool_base(ent->tool_frequency, area, rng, l);

    /* This shouldn't happen happen, but just in case... */
    if(item[0] == Item_NoSuchItem) {
        debug(DBG_WARN, "Generated invalid v3 tool! Please check your "
              "ItemPT.gsl file for validity!\n");
        return -1;
    }

    /* Clear the rest of the item. */
    item[1] = item[2] = item[3] = 0;

    /* If its a stackable item, make sure to give it a quantity of 1 */
    if(item_is_stackable(item[0])) {
#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "Item is stackable. Setting quantity to 1.\n");
#endif

        item[1] = (1 << 8);
    }

    if(item[0] == Item_Disk_Lv01) {
#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "Item is technique disk. Picking technique.\n");
#endif

        if(generate_tech(ent->tech_frequency, ent->tech_levels, area,
                         item, rng, l)) {
            debug(DBG_WARN, "Generated invalid technique! Please check "
                  "your ItemPT.gsl file for validity!\n");
            return -1;
        }
    }

    return 0;
}

static int generate_meseta(int min, int max, uint32_t item[4],
                           struct mt19937_state *rng, lobby_t *l) {
    uint32_t rnd;

    if(min < max)
        rnd = (mt19937_genrand_int32(rng) % ((max + 1) - min)) + min;
    else
        rnd = min;

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS) {
        debug(DBG_LOG, "generate_meseta: generated %" PRIu32 " meseta. (min, "
              " max) = (%d, %d)\n", rnd, min, max);
    }
#endif

    if(rnd) {
        item[0] = 0x00000004;
        item[1] = item[2] = 0x00000000;
        item[3] = rnd;
        return 0;
    }

    return -1;
}

static int check_and_send(ship_client_t *c, lobby_t *l, uint32_t item[4],
                          int area, subcmd_itemreq_t *req, int csr) {
    uint32_t v;
    sylverant_iitem_t iitem;
    int section;
    uint8_t stars = 0;

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "Final dropped item: %08" PRIx32 " %08" PRIx32 " %08"
              PRIx32 " %08" PRIx32 "\n", item[0], item[1], item[2], item[3]);
#endif

    if(l->limits_list) {
        switch(c->version) {
            case CLIENT_VERSION_DCV1:
                v = ITEM_VERSION_V1;
                break;

            case CLIENT_VERSION_DCV2:
            case CLIENT_VERSION_PC:
                v = ITEM_VERSION_V2;
                break;

            case CLIENT_VERSION_GC:
                v = ITEM_VERSION_GC;
                break;

            default:
                goto ok;
        }

        /* Fill in the item structure so we can check it. */
        iitem.data_l[0] = LE32(item[0]);
        iitem.data_l[1] = LE32(item[1]);
        iitem.data_l[2] = LE32(item[2]);
        iitem.data2_l = LE32(item[3]);

        if(!sylverant_limits_check_item(l->limits_list, &iitem, v)) {
            section = l->clients[l->leader_id]->pl->v1.section;
            debug(DBG_LOG, "Potentially non-legit dropped by server:\n"
                  "%08x %08x %08x %08x\n"
                  "Team Info: Difficulty: %d, Section: %d, flags: %08x\n"
                  "Version: %d, Floor: %d (%d %d)\n", item[0], item[1], item[2],
                  item[3], l->difficulty, section, l->flags, l->version, area,
                  l->maps[(area << 1)], l->maps[(area << 1) + 1]);

            /* The item failed the check, so don't send it! */
            return 0;
        }
    }

    /* See it is cool to drop "semi-rare" items. */
    if(csr) {
        switch(l->version) {
            case CLIENT_VERSION_DCV1:
            case CLIENT_VERSION_DCV2:
            case CLIENT_VERSION_PC:
                stars = pmt_lookup_stars_v2(item[0]);
                break;

            case CLIENT_VERSION_GC:
                stars = pmt_lookup_stars_gc(item[0]);
                break;
        }
    }

    if(stars != (uint8_t)-1 && stars >= 9) {
        /* We aren't supposed to drop rares, and this item qualifies
           as one (according to Sega's rules), so don't drop it. */
#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "Item drop blocked by semi-rare restriction. Has %d "
                  "stars.\n", stars);
#endif

        return 0;
    }

ok:
#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "Sending item drop to team.\n");
#endif

    return subcmd_send_lobby_item(l, req, item);
}

static int check_and_send_bb(ship_client_t *c, lobby_t *l, uint32_t item[4],
                             int area, subcmd_bb_itemreq_t *req, int csr) {
    int rv;
    item_t *it;

    /* See it is cool to drop "semi-rare" items. */
    if(csr) {
        if(pmt_lookup_stars_bb(item[0]) >= 9)
            /* We aren't supposed to drop rares, and this item qualifies
               as one (according to Sega's rules), so don't drop it. */
            return 0;
    }

    pthread_mutex_lock(&l->mutex);
    it = lobby_add_item_locked(l, item);
    rv = subcmd_send_bb_lobby_item(l, req, it);
    pthread_mutex_unlock(&l->mutex);

    return rv;
}

/* Generate an item drop from the PT data. This version uses the v2 PT data set,
   and thus is appropriate for any version before PSOGC. */
int pt_generate_v2_drop(ship_client_t *c, lobby_t *l, void *r) {
    subcmd_itemreq_t *req = (subcmd_itemreq_t *)r;
    int section = l->clients[l->leader_id]->pl->v1.section;
    pt_v2_entry_t *ent;
    uint32_t rnd;
    uint32_t item[4];
    int area, rarea, do_rare = 1;
    struct mt19937_state *rng = &c->cur_block->rng;
    uint16_t mid;
    game_enemy_t *enemy;
    int csr = 0;
    uint32_t qdrop = 0xFFFFFFFF;

    /* Make sure the PT index in the packet is sane */
    if(req->pt_index > 0x33)
        return -1;

    /* If the PT index is 0x30, this is a box, not an enemy! */
    if(req->pt_index == 0x30)
        return pt_generate_v2_boxdrop(c, l, r);

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        ent = &v2_ptdata[l->sdrops_diff][l->sdrops_section];
    else
#endif
    ent = &v2_ptdata[l->difficulty][section];

    /* Figure out the area we'll be worried with */
    rarea = area = c->cur_area;

    /* Dragon -> Cave 1 */
    if(area == 11)
        area = 3;
    /* De Rol Le -> Mine 1 */
    else if(area == 12)
        area = 6;
    /* Vol Opt -> Ruins 1 */
    else if(area == 13)
        area = 8;
    /* Dark Falz -> Ruins 3 */
    else if(area == 14)
        area = 10;
    /* Everything after Dark Falz -> Ruins 3 */
    else if(area > 14)
        area = 10;
    /* Invalid areas... */
    else if(area == 0) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " requested enemy drop on Pioneer "
              "2\n", c->guildcard);
        LOG(l, "Guildcard %" PRIu32 " requested enemy drop on Pioneer 2\n",
            c->guildcard);
        return -1;
    }

    /* Subtract one, since we want the index in the box_drop array */
    --area;

    /* Make sure the enemy's id is sane... */
    mid = LE16(req->req);
    if(mid > l->map_enemies->count) {
#ifdef DEBUG
        debug(DBG_WARN, "Guildcard %" PRIu32 " requested v2 drop for invalid "
              "enemy (%d -- max: %d, quest=%" PRIu32 ")!\n", c->guildcard,
              mid, l->map_enemies->count, l->qid);
#endif

        LOG(l, "Guildcard %" PRIu32 " requested v2 drop for invalid enemy (%d "
            "-- max: %d, quest=%" PRIu32 ")!\n", c->guildcard, mid,
            l->map_enemies->count, l->qid);
        return -1;
    }

    /* Grab the map enemy to make sure it hasn't already dropped something. */
    enemy = &l->map_enemies->enemies[mid];

    LOG(l, "Guildcard %" PRIu32 " requested v2 drop...\n"
        "mid: %d (max: %d), pt: %d (%d), area: %d (%d), quest: %" PRIu32
        "section: %d, difficulty: %d\n",
        c->guildcard, mid, l->map_enemies->count, req->pt_index,
        enemy->rt_index, area + 1, rarea, l->qid, section, l->difficulty);

    if(enemy->drop_done) {
        LOGV(l, "Drop already done.\n");
        return 0;
    }

    enemy->drop_done = 1;

    /* See if the enemy is going to drop anything at all this time... */
    rnd = mt19937_genrand_int32(rng) % 100;

    if(rnd >= ent->enemy_dar[req->pt_index]) {
        /* Nope. You get nothing! */
        LOGV(l, "DAR roll failed.\n");
        return 0;
    }

    /* See if we'll do a rare roll. */
    if(l->qid) {
        if(l->mids)
            qdrop = quest_search_enemy_list(mid, l->mids, l->num_mids, 1);
        if(qdrop == 0xFFFFFFFF && l->mtypes)
            qdrop = quest_search_enemy_list(req->pt_index, l->mtypes,
                                            l->num_mtypes, 1);

        switch(qdrop) {
            case SYLVERANT_QUEST_ENDROP_NONE:
                LOGV(l, "Enemy fixed to drop nothing by policy.\n");
                return 0;

            case SYLVERANT_QUEST_ENDROP_NORARE:
                do_rare = 0;
                csr = 1;
                break;

            case SYLVERANT_QUEST_ENDROP_PARTIAL:
                do_rare = 0;
                csr = 0;
                break;

            case SYLVERANT_QUEST_ENDROP_FREE:
                do_rare = 1;
                csr = 0;
                break;

            default:
                if(!(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_RARES))
                    do_rare = 0;
                if(!(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_SRARES))
                    csr = 1;
        }
    }

    /* See if the user is lucky today... */
    if(do_rare && (item[0] = rt_generate_v2_rare(c, l, req->pt_index, 0))) {
        LOGV(l, "Rare roll succeeded, generating %08" PRIx32 "\n", item[0]);

        switch(item[0] & 0xFF) {
            case 0:
                /* Weapon -- add percentages and (potentially) grind values and
                   such... */
                if(generate_weapon_v2(ent, area, item, rng, 1,
                                      l->version == CLIENT_VERSION_DCV1, l))
                    return 0;
                break;

            case 1:
                /* Armor/Shield/Unit */
                switch((item[0] >> 8) & 0xFF) {
                    case 1:
                        /* Armor -- Add DFP/EVP boosts and slots */
                        if(generate_armor_v2(ent, area, item, rng, 1, l))
                            return 0;
                        break;

                    case 2:
                        /* Shield -- Add DFP/EVP boosts */
                        if(generate_shield_v2(ent, area, item, rng, 1, l))
                            return 0;
                        break;

                    case 3:
                        /* Unit -- Nothing to do here */
                        break;

                    default:
#ifdef DEBUG
                        debug(DBG_WARN, "V2 ItemRT generated an invalid item: "
                              "%08" PRIx32 "\n", item[0]);
#endif

                        LOG(l, "V2 ItemRT generated an invalid item: %08"
                            PRIx32 "\n", item[0]);
                        return 0;
                }
                break;

            case 2:
                /* Mag -- Give it 5 DFP and 40% Synchro and an unset color */
                item[0] = 0x00050002;
                item[1] = 0x000101F4;
                item[2] = 0x00010001;
                item[3] = 0x00280000;
                break;

            case 3:
                /* Tool -- Give it a quantity of 1 if its stackable. */
                item[1] = item[2] = item[3] = 0;

                if(item_is_stackable(item[0]))
                    item[1] = (1 << 8);
                break;

            default:
#ifdef DEBUG
                debug(DBG_WARN, "V2 ItemRT generated an invalid item: %08"
                      PRIx32 "\n", item[0]);
#endif

                LOG(l, "V2 ItemRT generated an invalid item: %08" PRIx32 "\n",
                    item[0]);
                return 0;
        }

        return check_and_send(c, l, item, c->cur_area, req, csr);
    }

    /* Figure out what type to drop... */
    rnd = mt19937_genrand_int32(rng) % 3;
    switch(rnd) {
        case 0:
            /* Drop the enemy's designated type of item. */
            switch(ent->enemy_drop[req->pt_index]) {
                case BOX_TYPE_WEAPON:
                    /* Drop a weapon */
                    if(generate_weapon_v2(ent, area, item, rng, 0,
                                          l->version == CLIENT_VERSION_DCV1,
                                          l)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req, csr);

                case BOX_TYPE_ARMOR:
                    /* Drop an armor */
                    if(generate_armor_v2(ent, area, item, rng, 0, l)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req, csr);

                case BOX_TYPE_SHIELD:
                    /* Drop a shield */
                    if(generate_shield_v2(ent, area, item, rng, 0, l)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req, csr);

                case BOX_TYPE_UNIT:
                    /* Drop a unit */
                    if(pmt_random_unit_v2(ent->unit_level[area], item, rng,
                                          l)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req, csr);

                case -1:
                    /* This shouldn't happen, but if it does, don't drop
                       anything at all. */
                    LOGV(l, "Designated drop type set to invalid value.\n");
                    return 0;

                default:
#ifdef DEBUG
                    debug(DBG_WARN, "Unknown/Invalid v2 enemy drop (%d) for "
                          "index %d\n", ent->enemy_drop[req->pt_index],
                          req->pt_index);
#endif

                    LOG(l, "Unknown/Invalid v2 enemy drop (%d) for index %d\n",
                        ent->enemy_drop[req->pt_index], req->pt_index);
                    return 0;
            }

            break;

        case 1:
            /* Drop a tool */
            if(generate_tool_v2(ent, area, item, rng, l)) {
                return 0;
            }

            return check_and_send(c, l, item, c->cur_area, req, csr);

        case 2:
            /* Drop meseta */
            if(generate_meseta(ent->enemy_meseta[req->pt_index][0],
                               ent->enemy_meseta[req->pt_index][1],
                               item, rng, l)) {
                return 0;
            }

            return check_and_send(c, l, item, c->cur_area, req, csr);
    }

    /* Shouldn't ever get here... */
    return 0;
}

int pt_generate_v2_boxdrop(ship_client_t *c, lobby_t *l, void *r) {
    subcmd_itemreq_t *req = (subcmd_itemreq_t *)r;
    int section = l->clients[l->leader_id]->pl->v1.section;
    pt_v2_entry_t *ent;
    uint16_t obj_id;
    game_object_t *gobj;
    map_object_t *obj;
    uint32_t rnd, t1, t2;
    int area, do_rare = 1;
    uint32_t item[4];
    float f1, f2;
    struct mt19937_state *rng = &c->cur_block->rng;
    int csr = 0;
    uint32_t qdrop = 0xFFFFFFFF;

    /* Make sure this is actually a box drop... */
    if(req->pt_index != 0x30)
        return -1;

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        ent = &v2_ptdata[l->sdrops_diff][l->sdrops_section];
    else
#endif
    ent = &v2_ptdata[l->difficulty][section];

    /* Grab the object ID and make sure its sane, then grab the object itself */
    obj_id = LE16(req->req);
    if(obj_id > l->map_objs->count) {
        debug(DBG_WARN, "Guildard %u requested drop from invalid box\n",
              c->guildcard);
        return -1;
    }

    /* Don't bother if the box has already been opened */
    gobj = &l->map_objs->objs[obj_id];
    if(gobj->flags & 0x00000001)
        return 0;

    obj = &gobj->data;

    /* Figure out the area we'll be worried with */
    area = c->cur_area;

    /* Dragon -> Cave 1 */
    if(area == 11)
        area = 3;
    /* De Rol Le -> Mine 1 */
    else if(area == 12)
        area = 6;
    /* Vol Opt -> Ruins 1 */
    else if(area == 13)
        area = 8;
    /* Dark Falz -> Ruins 3 */
    else if(area == 14)
        area = 10;
    /* Everything after Dark Falz -> Ruins 3 */
    else if(area > 14)
        area = 10;
    /* Invalid areas... */
    else if(area == 0) {
        debug(DBG_WARN, "Guildcard %u requested box drop on Pioneer 2\n",
              c->guildcard);
        return -1;
    }

    /* Subtract one, since we want the index in the box_drop array */
    --area;

    /* Mark the box as spent now... */
    gobj->flags |= 0x00000001;

    /* See if we'll do a rare roll. */
    if(l->qid) {
        if(l->mtypes)
            qdrop = quest_search_enemy_list(0x30, l->mtypes, l->num_mtypes, 1);

        switch(qdrop) {
            case SYLVERANT_QUEST_ENDROP_NONE:
                return 0;

            case SYLVERANT_QUEST_ENDROP_NORARE:
                do_rare = 0;
                csr = 1;
                break;

            case SYLVERANT_QUEST_ENDROP_PARTIAL:
                do_rare = 0;
                csr = 0;
                break;

            case SYLVERANT_QUEST_ENDROP_FREE:
                do_rare = 1;
                csr = 0;
                break;

            default:
                if(!(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_RARES))
                    do_rare = 0;
                if(!(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_SRARES))
                    csr = 1;
        }
    }

    /* See if the object is fixed-type box */
    t1 = LE32(obj->dword[0]);
    memcpy(&f1, &t1, sizeof(float));
    if((obj->skin == LE32(0x00000092) || obj->skin == LE32(0x00000161)) &&
       f1 < 1.0f + EPSILON && f1 > 1.0f - EPSILON) {
        /* See if it is a fully-fixed item */
        t2 = LE32(obj->dword[1]);
        memcpy(&f2, &t2, sizeof(float));

        if(f2 < 1.0f + EPSILON && f2 > 1.0f - EPSILON) {
            /* Drop the requested item */
            item[0] = ntohl(obj->dword[2]);
            item[1] = item[2] = item[3] = 0;

            /* If its a stackable item, make sure to give it a quantity of 1 */
            if(item_is_stackable(item[0]))
                item[1] = (1 << 8);

            /* This will make the meseta boxes for Vol Opt work... */
            if(item[0] == 0x00000004) {
                t1 = LE32(obj->dword[3]) >> 16;
                item[3] = t1 * 10;
            }

            return check_and_send(c, l, item, c->cur_area, req, csr);
        }

        t1 = ntohl(obj->dword[2]);
        switch(t1 & 0xFF) {
            case 0:
                goto generate_weapon;

            case 1:
                goto generate_armor;

            case 3:
                goto generate_tool;

            case 4:
                goto generate_meseta;

            default:
                debug(DBG_WARN, "Invalid type detected from fixed-type box!\n");
                return 0;
        }
    }

    /* See if the user is lucky today... */
    if(do_rare && (item[0] = rt_generate_v2_rare(c, l, -1, area + 1))) {
        switch(item[0] & 0xFF) {
            case 0:
                /* Weapon -- add percentages and (potentially) grind values and
                   such... */
                if(generate_weapon_v2(ent, area, item, rng, 1,
                                      l->version == CLIENT_VERSION_DCV1, l))
                    return 0;
                break;

            case 1:
                /* Armor/Shield/Unit */
                switch((item[0] >> 8) & 0xFF) {
                    case 1:
                        /* Armor -- Add DFP/EVP boosts and slots */
                        if(generate_armor_v2(ent, area, item, rng, 1, l))
                            return 0;
                        break;

                    case 2:
                        /* Shield -- Add DFP/EVP boosts */
                        if(generate_shield_v2(ent, area, item, rng, 1, l))
                            return 0;
                        break;

                    case 3:
                        /* Unit -- Nothing to do here */
                        break;

                    default:
#ifdef DEBUG
                        debug(DBG_WARN, "V2 ItemRT generated an invalid item: "
                              "%08x\n", item[0]);
#endif
                        LOG(l, "V2 ItemRT generated an invalid box item: "
                            "%08x\n", item[0]);
                        return 0;
                }

                break;

            case 2:
                /* Mag -- Give it 5 DFP and 40% Synchro and an unset color */
                item[0] = 0x00050002;
                item[1] = 0x000101F4;
                item[2] = 0x00010001;
                item[3] = 0x00280000;
                break;

            case 3:
                /* Tool -- Give it a quantity of 1 if its stackable. */
                item[1] = item[2] = item[3] = 0;

                if(item_is_stackable(item[0]))
                    item[1] = (1 << 8);
                break;

            default:
#ifdef DEBUG
                debug(DBG_WARN, "V2 ItemRT generated an invalid item: %08x\n",
                      item[0]);
#endif

                LOG(l, "V2 ItemRT generated an invalid box item: %08x\n",
                    item[0]);
                return 0;
        }

        return check_and_send(c, l, item, c->cur_area, req, csr);
    }

    /* Generate an item, according to the PT data */
    rnd = mt19937_genrand_int32(rng) % 100;

    if((rnd -= ent->box_drop[BOX_TYPE_WEAPON][area]) > 100) {
generate_weapon:
        /* Generate a weapon */
        if(generate_weapon_v2(ent, area, item, rng, 0,
                              l->version == CLIENT_VERSION_DCV1, l))
            return 0;

        return check_and_send(c, l, item, c->cur_area, req, csr);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_ARMOR][area]) > 100) {
generate_armor:
        /* Generate an armor */
        if(generate_armor_v2(ent, area, item, rng, 0, l))
            return 0;

        return check_and_send(c, l, item, c->cur_area, req, csr);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_SHIELD][area]) > 100) {
        /* Generate a shield */
        if(generate_shield_v2(ent, area, item, rng, 0, l))
            return 0;

        return check_and_send(c, l, item, c->cur_area, req, csr);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_UNIT][area]) > 100) {
        /* Generate a unit */
        if(pmt_random_unit_v2(ent->unit_level[area], item, rng, l))
            return 0;

        return check_and_send(c, l, item, c->cur_area, req, csr);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_TOOL][area]) > 100) {
generate_tool:
        /* Generate a tool */
        if(generate_tool_v2(ent, area, item, rng, l))
            return 0;

        return check_and_send(c, l, item, c->cur_area, req, csr);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_MESETA][area]) > 100) {
generate_meseta:
        /* Generate money! */
        if(generate_meseta(ent->box_meseta[area][0], ent->box_meseta[area][1],
                           item, rng, l))
            return 0;

        return check_and_send(c, l, item, c->cur_area, req, csr);
    }

    /* You get nothing! */
    return 0;
}

/* Generate an item drop from the PT data. This version uses the v3 PT data set.
   This function only works for PSOGC. */
int pt_generate_gc_drop(ship_client_t *c, lobby_t *l, void *r) {
    subcmd_itemreq_t *req = (subcmd_itemreq_t *)r;
    int section = l->clients[l->leader_id]->pl->v1.section;
    pt_v3_entry_t *ent;
    uint32_t rnd;
    uint32_t item[4];
    int area, darea, do_rare = 1;
    struct mt19937_state *rng = &c->cur_block->rng;
    uint16_t mid;
    game_enemy_t *enemy;
    int csr = 0;

    /* Make sure the PT index in the packet is sane */
    //if(req->pt_index > 0x33)
    //    return -1;

    /* If the PT index is 0x30, this is a box, not an enemy! */
    if(req->pt_index == 0x30)
        return pt_generate_gc_boxdrop(c, l, r);

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        ent = &gc_ptdata[l->sdrops_ep - 1][l->sdrops_diff][l->sdrops_section];
    else
#endif
    ent = &gc_ptdata[l->episode - 1][l->difficulty][section];

    /* Figure out the area we'll be worried with */
    area = darea = c->cur_area;

    /* Episode II area list:
       0 = Pioneer 2, 1 = VR Temple Alpha, 2 = VR Temple Beta,
       3 = VR Space Ship Alpha, 4 = VR Space Ship Beta,
       5 = Central Control Area, 6 = Jungle North, 7 = Jungle South,
       8 = Mountains, 9 = SeaSide Daytime, 10 = SeaBed Upper Levels,
       11 = SeaBed Lower Levels, 12 = Gal Gryphon, 13 = Olga Flow,
       14 = Barba Ray, 15 = Gol Dragon, 16 = SeaSide Nighttime (battle only),
       17 = Tower 4th Floor (battle only) */
    switch(l->episode) {
        case 0:
        case 1:
            /* Dragon -> Cave 1 */
            if(area == 11)
                darea = 3;
            /* De Rol Le -> Mine 1 */
            else if(area == 12)
                darea = 6;
            /* Vol Opt -> Ruins 1 */
            else if(area == 13)
                darea = 8;
            /* Dark Falz -> Ruins 3 */
            else if(area == 14)
                darea = 10;
            /* Everything after Dark Falz -> Ruins 3 */
            else if(area > 14)
                darea = 10;
            /* Invalid areas... */
            else if(area == 0) {
                debug(DBG_WARN, "Guildcard %u requested enemy drop on Pioneer "
                      "2\n", c->guildcard);
                return -1;
            }

            break;

        case 2:
            /* Barba Ray -> VR Space Ship Alpha */
            if(area == 14)
                darea = 3;
            /* Gol Dragon -> Jungle North */
            else if(area == 15)
                darea = 6;
            /* Gal Gryphon -> SeaSide Daytime */
            else if(area == 12)
                darea = 9;
            /* Olga Flow -> SeaBed Upper Levels */
            else if(area == 13)
                darea = 10;
            /* All others after SeaBed Upper Levels -> SeaBed Upper Levels */
            else if(area > 10)
                darea = 10;
            else if(area == 0) {
                debug(DBG_WARN, "Guildcard %u requested enemy drop on Pioneer "
                      "2\n", c->guildcard);
                return -1;
            }

            break;
    }

    /* Subtract one, since we want the index in the box_drop array */
    --darea;

    /* Make sure the enemy's id is sane... */
    mid = LE16(req->req);

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS) {
        debug(DBG_LOG, "Guildcard %u Debug Drop (GC).\n"
                       "        Episode %d, Section: %d, Difficulty: %d,\n"
                       "        Area: %d (%d), Enemy ID: %d,\n"
                       "        PT Entry Pointer: %p\n",
              c->guildcard, l->sdrops_ep, l->sdrops_section, l->sdrops_diff,
              area, darea, mid, ent);
    }
#endif

    /* We only really need this separate for debugging... */
    area = darea;

    if(mid > l->map_enemies->count) {
#ifdef DEBUG
        debug(DBG_WARN, "Guildcard %" PRIu32 " requested GC drop for invalid "
              "enemy (%d -- max: %d, quest=%" PRIu32 ")!\n", c->guildcard, mid,
              l->map_enemies->count, l->qid);
#endif

        if(l->logfp) {
            fdebug(l->logfp, DBG_WARN, "Guildcard %" PRIu32 " requested GC "
                   "drop for invalid enemy (%d -- max: %d, quest=%" PRIu32
                   ")!\n", c->guildcard, mid, l->map_enemies->count, l->qid);
        }

        return -1;
    }

    /* Grab the map enemy to make sure it hasn't already dropped something. */
    enemy = &l->map_enemies->enemies[mid];
    if(enemy->drop_done) {
#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "Drop already done. Returning no item.\n");
#endif
        return 0;
    }

    enemy->drop_done = 1;

    /* See if the enemy is going to drop anything at all this time... */
    rnd = mt19937_genrand_int32(rng) % 100;

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "DAR: %d, Rolled: %d\n", ent->enemy_dar[req->pt_index],
              rnd);
#endif

    if(rnd >= ent->enemy_dar[req->pt_index]) {
        /* Nope. You get nothing! */
        return 0;
    }

    /* See if we'll do a rare roll. */
    if(l->qid) {
        if(!(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_RARES))
            do_rare = 0;
        if(!(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_SRARES))
            csr = 1;
    }

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "To do rare roll: %s, Allow quest semi-rares: %s\n",
              do_rare ? "Yes" : "No", csr ? "No" : "Yes");
#endif

    /* See if the user is lucky today... */
    if(do_rare && (item[0] = rt_generate_gc_rare(c, l, req->pt_index, 0))) {
#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "Rare roll succeeded, generating %08x\n", item[0]);
#endif

        switch(item[0] & 0xFF) {
            case 0:
                /* Weapon -- add percentages and (potentially) grind values and
                   such... */
                if(generate_weapon_v3(ent, area, item, rng, 1, 0, l))
                    return 0;
                break;

            case 1:
                /* Armor/Shield/Unit */
                switch((item[0] >> 8) & 0xFF) {
                    case 1:
                        /* Armor -- Add DFP/EVP boosts and slots */
                        if(generate_armor_v3(ent, area, item, rng, 1, 0, l))
                            return 0;
                        break;

                    case 2:
                        /* Shield -- Add DFP/EVP boosts */
                        if(generate_shield_v3(ent, area, item, rng, 1, 0, l))
                            return 0;
                        break;

                    case 3:
                        /* Unit -- Nothing to do here */
                        break;

                    default:
#ifdef DEBUG
                        debug(DBG_WARN, "GC ItemRT generated an invalid item: "
                              "%08x\n", item[0]);
#endif

                        if(l->logfp) {
                            fdebug(l->logfp, DBG_WARN, "GC ItemRT generated an "
                                   "invalid item: %08x\n", item[0]);
                        }

                        return 0;
                }
                break;

            case 2:
                /* Mag -- Give it 5 DFP and 40% Synchro and an unset color */
                item[0] = 0x00050002;
                item[1] = 0x000001F4;
                item[2] = 0x00000000;
                item[3] = 0x280000FF;
                break;

            case 3:
                /* Tool -- Give it a quantity of 1 if its stackable. */
                item[1] = item[2] = item[3] = 0;

                if(item_is_stackable(item[0]))
                    item[1] = (1 << 8);
                break;

            default:
#ifdef DEBUG
                debug(DBG_WARN, "ItemRT generated an invalid item: %08x\n",
                      item[0]);
#endif

                if(l->logfp) {
                    fdebug(l->logfp, DBG_WARN, "GC ItemRT generated an invalid "
                           "item: %08x\n", item[0]);
                }

                return 0;
        }

        return check_and_send(c, l, item, c->cur_area, req, csr);
    }

    /* Figure out what type to drop... */
    rnd = mt19937_genrand_int32(rng) % 3;

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS) {
        switch(rnd) {
            case 0:
                debug(DBG_LOG, "Drop designated item type: %d.\n",
                      ent->enemy_drop[req->pt_index]);
                break;
            case 1:
                debug(DBG_LOG, "Drop tool\n");
                break;
            case 2:
                debug(DBG_LOG, "Drop meseta\n");
                break;
        }
    }
#endif

    switch(rnd) {
        case 0:
            /* Drop the enemy's designated type of item. */
            switch(ent->enemy_drop[req->pt_index]) {
                case BOX_TYPE_WEAPON:
                    /* Drop a weapon */
                    if(generate_weapon_v3(ent, area, item, rng, 0, 0, l)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req, csr);

                case BOX_TYPE_ARMOR:
                    /* Drop an armor */
                    if(generate_armor_v3(ent, area, item, rng, 0, 0, l)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req, csr);

                case BOX_TYPE_SHIELD:
                    /* Drop a shield */
                    if(generate_shield_v3(ent, area, item, rng, 0, 0, l)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req, csr);

                case BOX_TYPE_UNIT:
                    /* Drop a unit */
                    if(pmt_random_unit_gc(ent->unit_level[area], item, rng,
                                          l)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req, csr);

                case -1:
                    /* This shouldn't happen, but if it does, don't drop
                       anything at all. */
                    return 0;

                default:
#ifdef DEBUG
                    debug(DBG_WARN, "Unknown/Invalid GC enemy drop (%d) for "
                          "index %d\n", ent->enemy_drop[req->pt_index],
                          req->pt_index);
#endif

                    if(l->logfp) {
                        fdebug(l->logfp, DBG_WARN, "Unknown/Invalid GC enemy "
                              "drop (%d) for index %d\n",
                              ent->enemy_drop[req->pt_index], req->pt_index);
                    }

                    return 0;
            }

            break;

        case 1:
            /* Drop a tool */
            if(generate_tool_v3(ent, area, item, rng, l)) {
                return 0;
            }

            return check_and_send(c, l, item, c->cur_area, req, csr);

        case 2:
            /* Drop meseta */
            if(generate_meseta(ent->enemy_meseta[req->pt_index][0],
                               ent->enemy_meseta[req->pt_index][1],
                               item, rng, l)) {
                return 0;
            }

            return check_and_send(c, l, item, c->cur_area, req, csr);
    }

    /* Shouldn't ever get here... */
    return 0;
}

int pt_generate_gc_boxdrop(ship_client_t *c, lobby_t *l, void *r) {
    subcmd_bitemreq_t *req = (subcmd_bitemreq_t *)r;
    int section = l->clients[l->leader_id]->pl->v1.section;
    pt_v3_entry_t *ent;
    uint16_t obj_id;
    game_object_t *gobj;
    map_object_t *obj;
    uint32_t rnd, t1, t2;
    int area, darea, do_rare = 1;
    uint32_t item[4];
    float f1, f2;
    struct mt19937_state *rng = &c->cur_block->rng;
    int csr = 0;

    /* Make sure this is actually a box drop... */
    if(req->pt_index != 0x30)
        return -1;

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        ent = &gc_ptdata[l->sdrops_ep - 1][l->sdrops_diff][l->sdrops_section];
    else
#endif
    ent = &gc_ptdata[l->episode - 1][l->difficulty][section];

    /* Grab the object ID and make sure its sane, then grab the object itself */
    obj_id = LE16(req->req);
    if(obj_id > l->map_objs->count) {
        debug(DBG_WARN, "Guildard %u requested drop from invalid box\n",
              c->guildcard);
        return -1;
    }

    /* Don't bother if the box has already been opened */
    gobj = &l->map_objs->objs[obj_id];
    if(gobj->flags & 0x00000001) {
#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "Requested drop from opened box: %d\n", obj_id);
#endif

        return 0;
    }

    obj = &gobj->data;

    /* Figure out the area we'll be worried with */
    area = darea = c->cur_area;

    switch(l->episode) {
        case 0:
        case 1:
            /* Dragon -> Cave 1 */
            if(area == 11)
                darea = 3;
            /* De Rol Le -> Mine 1 */
            else if(area == 12)
                darea = 6;
            /* Vol Opt -> Ruins 1 */
            else if(area == 13)
                darea = 8;
            /* Dark Falz -> Ruins 3 */
            else if(area == 14)
                darea = 10;
            /* Everything after Dark Falz -> Ruins 3 */
            else if(area > 14)
                darea = 10;
            /* Invalid areas... */
            else if(area == 0) {
                debug(DBG_WARN, "Guildcard %u requested box drop on Pioneer "
                      "2\n", c->guildcard);
                return -1;
            }

            break;

        case 2:
            /* Barba Ray -> VR Space Ship Alpha */
            if(area == 14)
                darea = 3;
            /* Gol Dragon -> Jungle North */
            else if(area == 15)
                darea = 6;
            /* Gal Gryphon -> SeaSide Daytime */
            else if(area == 12)
                darea = 9;
            /* Olga Flow -> SeaBed Upper Levels */
            else if(area == 13)
                darea = 10;
            /* All others after SeaBed Upper Levels -> SeaBed Upper Levels */
            else if(area > 10)
                darea = 10;
            else if(area == 0) {
                debug(DBG_WARN, "Guildcard %u requested box drop on Pioneer "
                      "2\n", c->guildcard);
                return -1;
            }

            break;
    }

    /* Subtract one, since we want the index in the box_drop array */
    --darea;

    /* Mark the box as spent now... */
    gobj->flags |= 0x00000001;

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS) {
        debug(DBG_LOG, "Guildcard %u Debug Box Drop (GC).\n"
                       "        Episode %d, Section: %d, Difficulty: %d,\n"
                       "        Area: %d (%d), Object ID: %d,\n"
                       "        Object Skin: %08" PRIx32 ",\n"
                       "        PT Entry Pointer: %p\n",
              c->guildcard, l->sdrops_ep, l->sdrops_section, l->sdrops_diff,
              area, darea, obj_id, LE32(obj->skin), ent);
    }
#endif

    /* We only really need this separate for debugging... */
    area = darea;

    /* See if we'll do a rare roll. */
    if(l->qid) {
        if(!(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_RARES))
            do_rare = 0;
        if(!(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_SRARES))
            csr = 1;
    }

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "To do rare roll: %s, Allow quest semi-rares: %s\n",
              do_rare ? "Yes" : "No", csr ? "No" : "Yes");
#endif

    /* See if the object is fixed-type box */
    t1 = LE32(obj->dword[0]);
    memcpy(&f1, &t1, sizeof(float));
    if((obj->skin == LE32(0x00000092) || obj->skin == LE32(0x00000161)) &&
       f1 < 1.0f + EPSILON && f1 > 1.0f - EPSILON) {
#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "Hit fixed box...\n");
#endif

        /* See if it is a fully-fixed item */
        t2 = LE32(obj->dword[1]);
        memcpy(&f2, &t2, sizeof(float));

        if(f2 < 1.0f + EPSILON && f2 > 1.0f - EPSILON) {
            /* Drop the requested item */
            item[0] = ntohl(obj->dword[2]);
            item[1] = item[2] = item[3] = 0;

#ifdef DEBUG
            if(l->flags & LOBBY_FLAG_DBG_SDROPS)
                debug(DBG_LOG, "Fully-fixed box. Dropping %08" PRIx32 "\n",
                      ntohl(obj->dword[2]));
#endif

            /* If its a stackable item, make sure to give it a quantity of 1 */
            if(item_is_stackable(item[0]))
                item[1] = (1 << 8);

            /* This will make the meseta boxes for Vol Opt work... */
            if(item[0] == 0x00000004) {
                t1 = LE32(obj->dword[3]) >> 16;
                item[3] = t1 * 10;
            }

            return check_and_send(c, l, item, c->cur_area,
                                  (subcmd_itemreq_t *)req, csr);
        }

        t1 = ntohl(obj->dword[2]);

#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "Fixed-type box. Generating type %d\n", t1 & 0xFF);
#endif

        switch(t1 & 0xFF) {
            case 0:
                goto generate_weapon;

            case 1:
                goto generate_armor;

            case 3:
                goto generate_tool;

            case 4:
                goto generate_meseta;

            default:
                debug(DBG_WARN, "Invalid type detected from fixed-type box!\n");
                return 0;
        }
    }

    /* See if the user is lucky today... */
    if(do_rare && (item[0] = rt_generate_gc_rare(c, l, -1, area + 1))) {
#ifdef DEBUG
        if(l->flags & LOBBY_FLAG_DBG_SDROPS)
            debug(DBG_LOG, "Rare roll succeeded, generating %08x\n", item[0]);
#endif

        switch(item[0] & 0xFF) {
            case 0:
                /* Weapon -- add percentages and (potentially) grind values and
                   such... */
                if(generate_weapon_v3(ent, area, item, rng, 1, 0, l))
                    return 0;
                break;

            case 1:
                /* Armor/Shield/Unit */
                switch((item[0] >> 8) & 0xFF) {
                    case 1:
                        /* Armor -- Add DFP/EVP boosts and slots */
                        if(generate_armor_v3(ent, area, item, rng, 1, 0, l))
                            return 0;
                        break;

                    case 2:
                        /* Shield -- Add DFP/EVP boosts */
                        if(generate_shield_v3(ent, area, item, rng, 1, 0, l))
                            return 0;
                        break;

                    case 3:
                        /* Unit -- Nothing to do here */
                        break;

                    default:
#ifdef DEBUG
                        debug(DBG_WARN, "GC ItemRT generated an invalid item: "
                              "%08x\n", item[0]);
#endif

                        if(l->logfp) {
                            fdebug(l->logfp, DBG_WARN, "GC ItemRT generated an "
                                   "invalid item: %08x\n", item[0]);
                        }

                        return 0;
                }

                break;

            case 2:
                /* Mag -- Give it 5 DFP and 40% Synchro and an unset color */
                item[0] = 0x00050002;
                item[1] = 0x000001F4;
                item[2] = 0x00000000;
                item[3] = 0x280000FF;
                break;

            case 3:
                /* Tool -- Give it a quantity of 1 if its stackable. */
                item[1] = item[2] = item[3] = 0;

                if(item_is_stackable(item[0]))
                    item[1] = (1 << 8);
                break;

            default:
#ifdef DEBUG
                debug(DBG_WARN, "ItemRT generated an invalid item: %08x\n",
                      item[0]);
#endif

                if(l->logfp) {
                    fdebug(l->logfp, DBG_WARN, "GC ItemRT generated an invalid "
                           "item: %08x\n", item[0]);
                }

                return 0;
        }

        return check_and_send(c, l, item, c->cur_area, (subcmd_itemreq_t *)req,
                              csr);
    }

    /* Generate an item, according to the PT data */
    rnd = mt19937_genrand_int32(rng) % 100;

#ifdef DEBUG
    if(l->flags & LOBBY_FLAG_DBG_SDROPS)
        debug(DBG_LOG, "RNG generated %d\n", rnd);
#endif

    if((rnd -= ent->box_drop[BOX_TYPE_WEAPON][area]) > 100) {
generate_weapon:
        /* Generate a weapon */
        if(generate_weapon_v3(ent, area, item, rng, 0, 0, l))
            return 0;

        return check_and_send(c, l, item, c->cur_area, (subcmd_itemreq_t *)req,
                              csr);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_ARMOR][area]) > 100) {
generate_armor:
        /* Generate an armor */
        if(generate_armor_v3(ent, area, item, rng, 0, 0, l))
            return 0;

        return check_and_send(c, l, item, c->cur_area, (subcmd_itemreq_t *)req,
                              csr);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_SHIELD][area]) > 100) {
        /* Generate a shield */
        if(generate_shield_v3(ent, area, item, rng, 0, 0, l))
            return 0;

        return check_and_send(c, l, item, c->cur_area, (subcmd_itemreq_t *)req,
                              csr);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_UNIT][area]) > 100) {
        /* Generate a unit */
        if(pmt_random_unit_gc(ent->unit_level[area], item, rng, l))
            return 0;

        return check_and_send(c, l, item, c->cur_area, (subcmd_itemreq_t *)req,
                              csr);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_TOOL][area]) > 100) {
generate_tool:
        /* Generate a tool */
        if(generate_tool_v3(ent, area, item, rng, l))
            return 0;

        return check_and_send(c, l, item, c->cur_area, (subcmd_itemreq_t *)req,
                              csr);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_MESETA][area]) > 100) {
generate_meseta:
        /* Generate money! */
        if(generate_meseta(ent->box_meseta[area][0], ent->box_meseta[area][1],
                           item, rng, l))
            return 0;

        return check_and_send(c, l, item, c->cur_area, (subcmd_itemreq_t *)req,
                              csr);
    }

    /* You get nothing! */
    return 0;
}

int pt_generate_bb_drop(ship_client_t *c, lobby_t *l, void *r) {
    subcmd_bb_itemreq_t *req = (subcmd_bb_itemreq_t *)r;
    int section = l->clients[l->leader_id]->pl->bb.character.section;
    pt_v3_entry_t *ent;
    uint32_t rnd;
    uint32_t item[4];
    int area, do_rare = 1;
    struct mt19937_state *rng = &c->cur_block->rng;
    uint16_t mid;
    game_enemy_t *enemy;
    int csr = 0;

    /* XXXX: Handle Episode 4 */
    if(l->episode == 3)
        return 0;

    ent = &bb_ptdata[l->episode - 1][l->difficulty][section];

    /* Make sure the PT index in the packet is sane */
    //if(req->pt_index > 0x33)
    //    return -1;

    /* If the PT index is 0x30, this is a box, not an enemy! */
    if(req->pt_index == 0x30)
        return pt_generate_bb_boxdrop(c, l, r);

    /* Figure out the area we'll be worried with */
    area = c->cur_area;
    switch(l->episode) {
        case 0:
        case 1:
            /* Dragon -> Cave 1 */
            if(area == 11)
                area = 3;
            /* De Rol Le -> Mine 1 */
            else if(area == 12)
                area = 6;
            /* Vol Opt -> Ruins 1 */
            else if(area == 13)
                area = 8;
            /* Dark Falz -> Ruins 3 */
            else if(area == 14)
                area = 10;
            /* Everything after Dark Falz -> Ruins 3 */
            else if(area > 14)
                area = 10;
            /* Invalid areas... */
            else if(area == 0) {
                debug(DBG_WARN, "Guildcard %u requested enemy drop on Pioneer "
                      "2\n", c->guildcard);
                return -1;
            }

            break;

        case 2:
            /* Barba Ray -> VR Space Ship Alpha */
            if(area == 14)
                area = 3;
            /* Gol Dragon -> Jungle North */
            else if(area == 15)
                area = 6;
            /* Gal Gryphon -> SeaSide Daytime */
            else if(area == 12)
                area = 9;
            /* Olga Flow -> SeaBed Upper Levels */
            else if(area == 13)
                area = 10;
            /* All others after SeaBed Upper Levels -> SeaBed Upper Levels */
            else if(area > 10)
                area = 10;
            else if(area == 0) {
                debug(DBG_WARN, "Guildcard %u requested enemy drop on Pioneer "
                      "2\n", c->guildcard);
                return -1;
            }

            break;
    }

    /* Subtract one, since we want the index in the box_drop array */
    --area;

    /* Make sure the enemy's id is sane... */
    mid = LE16(req->req);
    if(mid > l->map_enemies->count) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " requested drop for invalid "
              "enemy (%d -- max: %d, quest=%" PRIu32 ")!\n", c->guildcard, mid,
              l->map_enemies->count, l->qid);
        return -1;
    }

    /* Grab the map enemy to make sure it hasn't already dropped something. */
    enemy = &l->map_enemies->enemies[mid];
    if(enemy->drop_done)
        return 0;

    enemy->drop_done = 1;

    /* See if the enemy is going to drop anything at all this time... */
    rnd = mt19937_genrand_int32(rng) % 100;

    if(rnd >= ent->enemy_dar[req->pt_index])
        /* Nope. You get nothing! */
        return 0;

    /* See if we'll do a rare roll. */
    if(l->qid) {
        if(!(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_RARES))
            do_rare = 0;
        if(!(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_SRARES))
            csr = 1;
    }

    /* See if the user is lucky today... */
    if(do_rare && (item[0] = rt_generate_gc_rare(c, l, req->pt_index, 0))) {
        switch(item[0] & 0xFF) {
            case 0:
                /* Weapon -- add percentages and (potentially) grind values and
                   such... */
                if(generate_weapon_v3(ent, area, item, rng, 1, 1, l))
                    return 0;
                break;

            case 1:
                /* Armor/Shield/Unit */
                switch((item[0] >> 8) & 0xFF) {
                    case 1:
                        /* Armor -- Add DFP/EVP boosts and slots */
                        if(generate_armor_v3(ent, area, item, rng, 1, 1, l))
                            return 0;
                        break;

                    case 2:
                        /* Shield -- Add DFP/EVP boosts */
                        if(generate_shield_v3(ent, area, item, rng, 1, 1, l))
                            return 0;
                        break;

                    case 3:
                        /* Unit -- Nothing to do here */
                        break;

                    default:
                        debug(DBG_WARN, "ItemRT generated an invalid item: "
                              "%08x\n", item[0]);
                        return 0;
                }
                break;

            case 2:
                /* Mag -- Give it 5 DFP and 40% Synchro and an unset color */
                item[0] = 0x00050002;
                item[1] = 0x000001F4;
                item[2] = 0x00000000;
                item[3] = 0xFF000028;
                break;

            case 3:
                /* Tool -- Give it a quantity of 1 if its stackable. */
                item[1] = item[2] = item[3] = 0;

                if(item_is_stackable(item[0]))
                    item[1] = (1 << 8);
                break;

            default:
                debug(DBG_WARN, "ItemRT generated an invalid item: %08x\n",
                      item[0]);
                return 0;
        }

        return check_and_send_bb(c, l, item, c->cur_area, req, csr);
    }

    /* Figure out what type to drop... */
    rnd = mt19937_genrand_int32(rng) % 3;
    switch(rnd) {
        case 0:
            /* Drop the enemy's designated type of item. */
            switch(ent->enemy_drop[req->pt_index]) {
                case BOX_TYPE_WEAPON:
                    /* Drop a weapon */
                    if(generate_weapon_v3(ent, area, item, rng, 0, 1, l)) {
                        return 0;
                    }

                    return check_and_send_bb(c, l, item, c->cur_area, req, csr);

                case BOX_TYPE_ARMOR:
                    /* Drop an armor */
                    if(generate_armor_v3(ent, area, item, rng, 0, 1, l)) {
                        return 0;
                    }

                    return check_and_send_bb(c, l, item, c->cur_area, req, csr);

                case BOX_TYPE_SHIELD:
                    /* Drop a shield */
                    if(generate_shield_v3(ent, area, item, rng, 0, 1, l)) {
                        return 0;
                    }

                    return check_and_send_bb(c, l, item, c->cur_area, req, csr);

                case BOX_TYPE_UNIT:
                    /* Drop a unit */
                    if(pmt_random_unit_bb(ent->unit_level[area], item, rng,
                                          l)) {
                        return 0;
                    }

                    return check_and_send_bb(c, l, item, c->cur_area, req, csr);

                case -1:
                    /* This shouldn't happen, but if it does, don't drop
                       anything at all. */
                    return 0;

                default:
                    debug(DBG_WARN, "Unknown/Invalid enemy drop (%d) for index "
                          "%d\n", ent->enemy_drop[req->pt_index],
                          req->pt_index);
                    return 0;
            }

            break;

        case 1:
            /* Drop a tool */
            if(generate_tool_v3(ent, area, item, rng, l)) {
                return 0;
            }

            return check_and_send_bb(c, l, item, c->cur_area, req, csr);

        case 2:
            /* Drop meseta */
            if(generate_meseta(ent->enemy_meseta[req->pt_index][0],
                               ent->enemy_meseta[req->pt_index][1],
                               item, rng, l)) {
                return 0;
            }

            return check_and_send_bb(c, l, item, c->cur_area, req, csr);
    }

    /* Shouldn't ever get here... */
    return 0;
}

int pt_generate_bb_boxdrop(ship_client_t *c, lobby_t *l, void *r) {
    subcmd_bb_bitemreq_t *req = (subcmd_bb_bitemreq_t *)r;
    int section = l->clients[l->leader_id]->pl->bb.character.section;
    pt_v3_entry_t *ent;
    uint16_t obj_id;
    game_object_t *gobj;
    map_object_t *obj;
    uint32_t rnd, t1, t2;
    int area, do_rare = 1;
    uint32_t item[4];
    float f1, f2;
    struct mt19937_state *rng = &c->cur_block->rng;
    int csr = 0;

    /* XXXX: Handle Episode 4 */
    if(l->episode == 3)
        return 0;

    ent = &bb_ptdata[l->episode][l->difficulty][section];

    /* Make sure this is actually a box drop... */
    if(req->pt_index != 0x30)
        return -1;

    /* Grab the object ID and make sure its sane, then grab the object itself */
    obj_id = LE16(req->req);
    if(obj_id > l->map_objs->count) {
        debug(DBG_WARN, "Guildard %u requested drop from invalid box\n",
              c->guildcard);
        return -1;
    }

    /* Don't bother if the box has already been opened */
    gobj = &l->map_objs->objs[obj_id];
    if(gobj->flags & 0x00000001)
        return 0;

    obj = &gobj->data;

    /* Figure out the area we'll be worried with */
    area = c->cur_area;

    switch(l->episode) {
        case 0:
        case 1:
            /* Dragon -> Cave 1 */
            if(area == 11)
                area = 3;
            /* De Rol Le -> Mine 1 */
            else if(area == 12)
                area = 6;
            /* Vol Opt -> Ruins 1 */
            else if(area == 13)
                area = 8;
            /* Dark Falz -> Ruins 3 */
            else if(area == 14)
                area = 10;
            /* Everything after Dark Falz -> Ruins 3 */
            else if(area > 14)
                area = 10;
            /* Invalid areas... */
            else if(area == 0) {
                debug(DBG_WARN, "Guildcard %u requested box drop on Pioneer "
                      "2\n", c->guildcard);
                return -1;
            }

            break;

        case 2:
            /* Barba Ray -> VR Space Ship Alpha */
            if(area == 14)
                area = 3;
            /* Gol Dragon -> Jungle North */
            else if(area == 15)
                area = 6;
            /* Gal Gryphon -> SeaSide Daytime */
            else if(area == 12)
                area = 9;
            /* Olga Flow -> SeaBed Upper Levels */
            else if(area == 13)
                area = 10;
            /* All others after SeaBed Upper Levels -> SeaBed Upper Levels */
            else if(area > 10)
                area = 10;
            else if(area == 0) {
                debug(DBG_WARN, "Guildcard %u requested box drop on Pioneer "
                      "2\n", c->guildcard);
                return -1;
            }

            break;
    }

    /* Subtract one, since we want the index in the box_drop array */
    --area;

    /* Mark the box as spent now... */
    gobj->flags |= 0x00000001;

    /* See if we'll do a rare roll. */
    if(l->qid) {
        if(!(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_RARES))
            do_rare = 0;
        if(!(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_SRARES))
            csr = 1;
    }

    /* See if the object is fixed-type box */
    t1 = LE32(obj->dword[0]);
    memcpy(&f1, &t1, sizeof(float));
    if((obj->skin == LE32(0x00000092) || obj->skin == LE32(0x00000161)) &&
       f1 < 1.0f + EPSILON && f1 > 1.0f - EPSILON) {
        /* See if it is a fully-fixed item */
        t2 = LE32(obj->dword[1]);
        memcpy(&f2, &t2, sizeof(float));

        if(f2 < 1.0f + EPSILON && f2 > 1.0f - EPSILON) {
            /* Drop the requested item */
            item[0] = ntohl(obj->dword[2]);
            item[1] = item[2] = item[3] = 0;

            /* If its a stackable item, make sure to give it a quantity of 1 */
            if(item_is_stackable(item[0]))
                item[1] = (1 << 8);

            /* This will make the meseta boxes for Vol Opt work... */
            if(item[0] == 0x00000004) {
                t1 = LE32(obj->dword[3]) >> 16;
                item[3] = t1 * 10;
            }

            return check_and_send_bb(c, l, item, c->cur_area,
                                     (subcmd_bb_itemreq_t *)req, csr);
        }

        t1 = ntohl(obj->dword[2]);
        switch(t1 & 0xFF) {
            case 0:
                goto generate_weapon;

            case 1:
                goto generate_armor;

            case 3:
                goto generate_tool;

            case 4:
                goto generate_meseta;

            default:
                debug(DBG_WARN, "Invalid type detected from fixed-type box!\n");
                return 0;
        }
    }

    /* See if the user is lucky today... */
    if(do_rare && (item[0] = rt_generate_gc_rare(c, l, -1, area + 1))) {
        switch(item[0] & 0xFF) {
            case 0:
                /* Weapon -- add percentages and (potentially) grind values and
                   such... */
                if(generate_weapon_v3(ent, area, item, rng, 1, 1, l))
                    return 0;
                break;

            case 1:
                /* Armor/Shield/Unit */
                switch((item[0] >> 8) & 0xFF) {
                    case 1:
                        /* Armor -- Add DFP/EVP boosts and slots */
                        if(generate_armor_v3(ent, area, item, rng, 1, 1, l))
                            return 0;
                        break;

                    case 2:
                        /* Shield -- Add DFP/EVP boosts */
                        if(generate_shield_v3(ent, area, item, rng, 1, 1, l))
                            return 0;
                        break;

                    case 3:
                        /* Unit -- Nothing to do here */
                        break;

                    default:
                        debug(DBG_WARN, "ItemRT generated an invalid item: "
                              "%08x\n", item[0]);
                        return 0;
                }

                break;

            case 2:
                /* Mag -- Give it 5 DFP and 40% Synchro and an unset color */
                item[0] = 0x00050002;
                item[1] = 0x000001F4;
                item[2] = 0x00000000;
                item[3] = 0xFF000028;
                break;

            case 3:
                /* Tool -- Give it a quantity of 1 if its stackable. */
                item[1] = item[2] = item[3] = 0;

                if(item_is_stackable(item[0]))
                    item[1] = (1 << 8);
                break;

            default:
                debug(DBG_WARN, "ItemRT generated an invalid item: %08x\n",
                      item[0]);
                return 0;
        }

        return check_and_send_bb(c, l, item, c->cur_area,
                                 (subcmd_bb_itemreq_t *)req, csr);
    }

    /* Generate an item, according to the PT data */
    rnd = mt19937_genrand_int32(rng) % 100;

    if((rnd -= ent->box_drop[BOX_TYPE_WEAPON][area]) > 100) {
generate_weapon:
        /* Generate a weapon */
        if(generate_weapon_v3(ent, area, item, rng, 0, 1, l))
            return 0;

        return check_and_send_bb(c, l, item, c->cur_area,
                                 (subcmd_bb_itemreq_t *)req, csr);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_ARMOR][area]) > 100) {
generate_armor:
        /* Generate an armor */
        if(generate_armor_v3(ent, area, item, rng, 0, 1, l))
            return 0;

        return check_and_send_bb(c, l, item, c->cur_area,
                                 (subcmd_bb_itemreq_t *)req, csr);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_SHIELD][area]) > 100) {
        /* Generate a shield */
        if(generate_shield_v3(ent, area, item, rng, 0, 1, l))
            return 0;

        return check_and_send_bb(c, l, item, c->cur_area,
                                 (subcmd_bb_itemreq_t *)req, csr);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_UNIT][area]) > 100) {
        /* Generate a unit */
        if(pmt_random_unit_bb(ent->unit_level[area], item, rng, l))
            return 0;

        return check_and_send_bb(c, l, item, c->cur_area,
                                 (subcmd_bb_itemreq_t *)req, csr);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_TOOL][area]) > 100) {
generate_tool:
        /* Generate a tool */
        if(generate_tool_v3(ent, area, item, rng, l))
            return 0;

        return check_and_send_bb(c, l, item, c->cur_area,
                                 (subcmd_bb_itemreq_t *)req, csr);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_MESETA][area]) > 100) {
generate_meseta:
        /* Generate money! */
        if(generate_meseta(ent->box_meseta[area][0], ent->box_meseta[area][1],
                           item, rng, l))
            return 0;

        return check_and_send_bb(c, l, item, c->cur_area,
                                 (subcmd_bb_itemreq_t *)req, csr);
    }

    /* You get nothing! */
    return 0;
}
