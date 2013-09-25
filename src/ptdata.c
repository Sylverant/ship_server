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

#include <arpa/inet.h>

#include <sylverant/items.h>
#include <sylverant/debug.h>
#include <sylverant/mtwist.h>

#include "ptdata.h"
#include "pmtdata.h"
#include "rtdata.h"
#include "subcmd.h"
#include "items.h"
#include "utils.h"

#define MAX(x, y) (x > y ? x : y)
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

int pt_read_v3(const char *fn, int bb) {
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

                if(bb) {
                    if(fread(&bb_ptdata[i][j][k], 1, sizeof(pt_v3_entry_t),
                             fp) != sizeof(pt_v3_entry_t)) {
                        debug(DBG_ERROR, "Error reading file: %s\n",
                              strerror(errno));
                        rv = -2;
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
                    if(fread(&gc_ptdata[i][j][k], 1, sizeof(pt_v3_entry_t),
                             fp) != sizeof(pt_v3_entry_t)) {
                        debug(DBG_ERROR, "Error reading file: %s\n",
                              strerror(errno));
                        rv = -2;
                        goto out;
                    }

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
    fclose(fp);
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
                              struct mt19937_state *rng, int picked, int v1) {
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
                              struct mt19937_state *rng, int picked, int bb) {
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
                             struct mt19937_state *rng, int picked) {
    uint32_t rnd;
    int i, armor = -1;
    uint8_t *item_b = (uint8_t *)item;
    uint16_t *item_w = (uint16_t *)item;
    pmt_guard_v2_t guard;

    if(!picked) {
        /* Go through each slot in the armor rankings to figure out which one
           that we'll be generating. */
        rnd = mt19937_genrand_int32(rng) % 100;
        for(i = 0; i < 5; ++i) {
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
        armor = MAX(0, (ent->armor_level - 3 + area + armor));
        item[0] = 0x00000101 | (armor << 16);
    }

    item[1] = item[2] = item[3] = 0;

    /* Pick a number of unit slots */
    rnd = mt19937_genrand_int32(rng) % 100;
    for(i = 0; i < 5; ++i) {
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

    if(guard.dfp_range) {
        rnd = mt19937_genrand_int32(rng) % (guard.dfp_range + 1);
        item_w[3] = (uint16_t)rnd;
    }

    if(guard.evp_range) {
        rnd = mt19937_genrand_int32(rng) % (guard.evp_range + 1);
        item_w[4] = (uint16_t)rnd;
    }

    return 0;
}

static int generate_armor_v3(pt_v3_entry_t *ent, int area, uint32_t item[4],
                             struct mt19937_state *rng, int picked, int bb) {
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
        for(i = 0; i < 5; ++i) {
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
        armor = MAX(0, (ent->armor_level - 3 + area + armor));
        item[0] = 0x00000101 | (armor << 16);
    }

    item[1] = item[2] = item[3] = 0;

    /* Pick a number of unit slots */
    rnd = mt19937_genrand_int32(rng) % 100;
    for(i = 0; i < 5; ++i) {
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

    if(dfp) {
        rnd = mt19937_genrand_int32(rng) % (dfp + 1);
        item_w[3] = (uint16_t)rnd;
    }

    if(evp) {
        rnd = mt19937_genrand_int32(rng) % (evp + 1);
        item_w[4] = (uint16_t)rnd;
    }

    return 0;
}

/* Generate a random shield, based on data for PSOv2. This is exactly the same
   as the armor version, but without unit slots. */
static int generate_shield_v2(pt_v2_entry_t *ent, int area, uint32_t item[4],
                              struct mt19937_state *rng, int picked) {
    uint32_t rnd;
    int i, armor = -1;
    uint16_t *item_w = (uint16_t *)item;
    pmt_guard_v2_t guard;

    if(!picked) {
        /* Go through each slot in the armor rankings to figure out which one
           that we'll be generating. */
        rnd = mt19937_genrand_int32(rng) % 100;
        for(i = 0; i < 5; ++i) {
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
        armor = MAX(0, (ent->armor_level - 3 + area + armor));
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

    if(guard.dfp_range) {
        rnd = mt19937_genrand_int32(rng) % (guard.dfp_range + 1);
        item_w[3] = (uint16_t)rnd;
    }

    if(guard.evp_range) {
        rnd = mt19937_genrand_int32(rng) % (guard.evp_range + 1);
        item_w[4] = (uint16_t)rnd;
    }

    return 0;
}

static int generate_shield_v3(pt_v3_entry_t *ent, int area, uint32_t item[4],
                              struct mt19937_state *rng, int picked, int bb) {
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
        for(i = 0; i < 5; ++i) {
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
        armor = MAX(0, (ent->armor_level - 3 + area + armor));
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

    if(dfp) {
        rnd = mt19937_genrand_int32(rng) % (dfp + 1);
        item_w[3] = (uint16_t)rnd;
    }

    if(evp) {
        rnd = mt19937_genrand_int32(rng) % (evp + 1);
        item_w[4] = (uint16_t)rnd;
    }

    return 0;
}

static uint32_t generate_tool_base(uint16_t freqs[28][10], int area,
                                   struct mt19937_state *rng) {
    uint32_t rnd = mt19937_genrand_int32(rng) % 10000;
    int i;

    for(i = 0; i < 28; ++i) {
        if((rnd -= LE16(freqs[i][area])) > 10000) {
            return tool_base[i];
        }
    }

    return Item_NoSuchItem;
}

static int generate_tech(uint8_t freqs[19][10], int8_t levels[19][20],
                         int area, uint32_t item[4],
                         struct mt19937_state *rng) {
    uint32_t rnd, tech, level;
    uint32_t t1, t2;
    int i;

    rnd = mt19937_genrand_int32(rng);
    tech = rnd % 1000;
    rnd /= 1000;

    for(i = 0; i < 19; ++i) {
        if((tech -= freqs[i][area]) > 1000) {
            t1 = levels[i][area << 1];
            t2 = levels[i][(area << 1) + 1];

            if(t1 == -1 || t1 > t2)
                return -1;

            if(t1 < t2)
                level = (rnd % ((t2 + 1) - t1)) + t1;
            else
                level = t1;

            item[1] = i;
            item[0] |= (level << 16);
            return 0;
        }
    }

    /* Shouldn't get here... */
    return -1;
}

static int generate_tool_v2(pt_v2_entry_t *ent, int area, uint32_t item[4],
                            struct mt19937_state *rng) {
    item[0] = generate_tool_base(ent->tool_frequency, area, rng);

    /* Neither of these should happen, but just in case... */
    if(item[0] == Item_Photon_Drop || item[0] == Item_NoSuchItem) {
        debug(DBG_WARN, "Generated invalid v2 tool! Please check your "
              "ItemPT.afs file for validity!\n");
        return -1;
    }

    /* Clear the rest of the item. */
    item[1] = item[2] = item[3] = 0;

    /* If its a stackable item, make sure to give it a quantity of 1 */
    if(item_is_stackable(item[0]))
        item[1] = (1 << 8);

    if(item[0] == Item_Disk_Lv01) {
        if(generate_tech(ent->tech_frequency, ent->tech_levels, area,
                         item, rng)) {
            debug(DBG_WARN, "Generated invalid technique! Please check "
                  "your ItemPT.afs file for validity!\n");
            return -1;
        }
    }

    return 0;
}

static int generate_tool_v3(pt_v3_entry_t *ent, int area, uint32_t item[4],
                            struct mt19937_state *rng) {
    item[0] = generate_tool_base(ent->tool_frequency, area, rng);

    /* Neither of these should happen, but just in case... */
    if(item[0] == Item_Photon_Drop || item[0] == Item_NoSuchItem) {
        debug(DBG_WARN, "Generated invalid v3 tool! Please check your "
              "ItemPT.gsl file for validity!\n");
        return -1;
    }

    /* Clear the rest of the item. */
    item[1] = item[2] = item[3] = 0;

    /* If its a stackable item, make sure to give it a quantity of 1 */
    if(item_is_stackable(item[0]))
        item[1] = (1 << 8);

    if(item[0] == Item_Disk_Lv01) {
        if(generate_tech(ent->tech_frequency, ent->tech_levels, area,
                         item, rng)) {
            debug(DBG_WARN, "Generated invalid technique! Please check "
                  "your ItemPT.gsl file for validity!\n");
            return -1;
        }
    }

    return 0;
}

static int generate_meseta(int min, int max, uint32_t item[4],
                           struct mt19937_state *rng) {
    uint32_t rnd;

    if(min < max)
        rnd = (mt19937_genrand_int32(rng) % ((max + 1) - min)) + min;
    else
        rnd = min;

    if(rnd) {
        item[0] = 0x00000004;
        item[1] = item[2] = 0x00000000;
        item[3] = rnd;
        return 0;
    }

    return -1;
}

static int check_and_send(ship_client_t *c, lobby_t *l, uint32_t item[4],
                          int area, subcmd_itemreq_t *req) {
    uint32_t v;
    sylverant_iitem_t iitem;

    if(ship->limits) {
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

        if(!sylverant_limits_check_item(ship->limits, &iitem, v)) {
            debug(DBG_LOG, "Potentially non-legit dropped by server:\n"
                  "%08x %08x %08x %08x\n"
                  "Team Info: Difficulty: %d, Section: %d, flags: %08x\n"
                  "Version: %d, Floor: %d (%d %d)\n", item[0], item[1], item[2],
                  item[3], l->difficulty, l->section, l->flags, l->version,
                  area, l->maps[(area << 1)], l->maps[(area << 1) + 1]);

            /* The item failed the check, so don't send it! */
            return 0;
        }
    }

    /* See it is cool to drop "semi-rare" items. */
    if(l->qid && !(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_SRARES)) {
        switch(l->version) {
            case CLIENT_VERSION_DCV1:
            case CLIENT_VERSION_DCV2:
            case CLIENT_VERSION_PC:
                if(pmt_lookup_stars_v2(item[0]) >= 9)
                    /* We aren't supposed to drop rares, and this item qualifies
                       as one (according to Sega's rules), so don't drop it. */
                    return 0;

            case CLIENT_VERSION_GC:
                if(pmt_lookup_stars_gc(item[0]) >= 9)
                    /* We aren't supposed to drop rares, and this item qualifies
                       as one (according to Sega's rules), so don't drop it. */
                    return 0;
        }
    }

ok:
    return subcmd_send_lobby_item(l, req, item);
}

static int check_and_send_bb(ship_client_t *c, lobby_t *l, uint32_t item[4],
                             int area, subcmd_bb_itemreq_t *req) {
    int rv;
    item_t *it;

    /* See it is cool to drop "semi-rare" items. */
    if(l->qid && !(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_SRARES)) {
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
    pt_v2_entry_t *ent = &v2_ptdata[l->difficulty][l->section];
    uint32_t rnd;
    uint32_t item[4];
    int area, do_rare = 1;
    struct mt19937_state *rng = &c->cur_block->rng;
    uint16_t mid;
    game_enemy_t *enemy;

    /* Make sure the PT index in the packet is sane */
    if(req->pt_index > 0x33)
        return -1;

    /* If the PT index is 0x30, this is a box, not an enemy! */
    if(req->pt_index == 0x30)
        return pt_generate_v2_boxdrop(c, l, r);

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
        debug(DBG_WARN, "Guildcard %u requested enemy drop on Pioneer 2\n",
              c->guildcard);
        return -1;
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
    if(l->qid && !(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_RARES))
        do_rare = 0;

    /* See if the user is lucky today... */
    if(do_rare && (item[0] = rt_generate_v2_rare(c, l, req->pt_index, 0))) {
        switch(item[0] & 0xFF) {
            case 0:
                /* Weapon -- add percentages and (potentially) grind values and
                   such... */
                if(generate_weapon_v2(ent, area, item, rng, 1,
                                      l->version == CLIENT_VERSION_DCV1))
                    return 0;
                break;

            case 1:
                /* Armor/Shield/Unit */
                switch((item[0] >> 8) & 0xFF) {
                    case 1:
                        /* Armor -- Add DFP/EVP boosts and slots */
                        if(generate_armor_v2(ent, area, item, rng, 1))
                            return 0;
                        break;

                    case 2:
                        /* Shield -- Add DFP/EVP boosts */
                        if(generate_shield_v2(ent, area, item, rng, 1))
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
                debug(DBG_WARN, "ItemRT generated an invalid item: %08x\n",
                      item[0]);
                return 0;
        }

        return check_and_send(c, l, item, c->cur_area, req);
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
                                          l->version == CLIENT_VERSION_DCV1)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req);

                case BOX_TYPE_ARMOR:
                    /* Drop an armor */
                    if(generate_armor_v2(ent, area, item, rng, 0)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req);

                case BOX_TYPE_SHIELD:
                    /* Drop a shield */
                    if(generate_shield_v2(ent, area, item, rng, 0)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req);

                case BOX_TYPE_UNIT:
                    /* Drop a unit */
                    if(pmt_random_unit_v2(ent->unit_level[area], item, rng)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req);

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
            if(generate_tool_v2(ent, area, item, rng)) {
                return 0;
            }

            return check_and_send(c, l, item, c->cur_area, req);

        case 2:
            /* Drop meseta */
            if(generate_meseta(ent->enemy_meseta[req->pt_index][0],
                               ent->enemy_meseta[req->pt_index][1],
                               item, rng)) {
                return 0;
            }

            return check_and_send(c, l, item, c->cur_area, req);
    }

    /* Shouldn't ever get here... */
    return 0;
}

int pt_generate_v2_boxdrop(ship_client_t *c, lobby_t *l, void *r) {
    subcmd_itemreq_t *req = (subcmd_itemreq_t *)r;
    pt_v2_entry_t *ent = &v2_ptdata[l->difficulty][l->section];
    uint16_t obj_id;
    game_object_t *gobj;
    map_object_t *obj;
    uint32_t rnd, t1, t2;
    int area, do_rare = 1;
    uint32_t item[4];
    float f1, f2;
    struct mt19937_state *rng = &c->cur_block->rng;

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

    /* See if the object is fixed-type box */
    t1 = LE32(obj->dword[0]);
    f1 = *((float *)&t1);
    if((obj->skin == LE32(0x00000092) || obj->skin == LE32(0x00000161)) &&
       f1 < 1.0f + EPSILON && f1 > 1.0f - EPSILON) {
        /* See if it is a fully-fixed item */
        t2 = LE32(obj->dword[1]);
        f2 = *((float *)&t2);

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

            return check_and_send(c, l, item, c->cur_area, req);
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

    /* See if we'll do a rare roll. */
    if(l->qid && !(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_RARES))
        do_rare = 0;

    /* See if the user is lucky today... */
    if(do_rare && (item[0] = rt_generate_v2_rare(c, l, -1, area + 1))) {
        switch(item[0] & 0xFF) {
            case 0:
                /* Weapon -- add percentages and (potentially) grind values and
                   such... */
                if(generate_weapon_v2(ent, area, item, rng, 1,
                                      l->version == CLIENT_VERSION_DCV1))
                    return 0;
                break;

            case 1:
                /* Armor/Shield/Unit */
                switch((item[0] >> 8) & 0xFF) {
                    case 1:
                        /* Armor -- Add DFP/EVP boosts and slots */
                        if(generate_armor_v2(ent, area, item, rng, 1))
                            return 0;
                        break;

                    case 2:
                        /* Shield -- Add DFP/EVP boosts */
                        if(generate_shield_v2(ent, area, item, rng, 1))
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
                debug(DBG_WARN, "ItemRT generated an invalid item: %08x\n",
                      item[0]);
                return 0;
        }

        return check_and_send(c, l, item, c->cur_area, req);
    }

    /* Generate an item, according to the PT data */
    rnd = mt19937_genrand_int32(rng) % 100;

    if((rnd -= ent->box_drop[BOX_TYPE_WEAPON][area]) > 100) {
generate_weapon:
        /* Generate a weapon */
        if(generate_weapon_v2(ent, area, item, rng, 0,
                              l->version == CLIENT_VERSION_DCV1))
            return 0;

        return check_and_send(c, l, item, c->cur_area, req);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_ARMOR][area]) > 100) {
generate_armor:
        /* Generate an armor */
        if(generate_armor_v2(ent, area, item, rng, 0))
            return 0;

        return check_and_send(c, l, item, c->cur_area, req);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_SHIELD][area]) > 100) {
        /* Generate a shield */
        if(generate_shield_v2(ent, area, item, rng, 0))
            return 0;

        return check_and_send(c, l, item, c->cur_area, req);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_UNIT][area]) > 100) {
        /* Generate a unit */
        if(pmt_random_unit_v2(ent->unit_level[area], item, rng))
            return 0;

        return check_and_send(c, l, item, c->cur_area, req);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_TOOL][area]) > 100) {
generate_tool:
        /* Generate a tool */
        if(generate_tool_v2(ent, area, item, rng))
            return 0;

        return check_and_send(c, l, item, c->cur_area, req);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_MESETA][area]) > 100) {
generate_meseta:
        /* Generate money! */
        if(generate_meseta(ent->box_meseta[area][0], ent->box_meseta[area][1],
                           item, rng))
            return 0;

        return check_and_send(c, l, item, c->cur_area, req);
    }

    /* You get nothing! */
    return 0;
}

/* Generate an item drop from the PT data. This version uses the v3 PT data set.
   This function only works for PSOGC. */
int pt_generate_gc_drop(ship_client_t *c, lobby_t *l, void *r) {
    subcmd_itemreq_t *req = (subcmd_itemreq_t *)r;
    pt_v3_entry_t *ent = &gc_ptdata[l->episode - 1][l->difficulty][l->section];
    uint32_t rnd;
    uint32_t item[4];
    int area, do_rare = 1;
    struct mt19937_state *rng = &c->cur_block->rng;
    uint16_t mid;
    game_enemy_t *enemy;

    /* Make sure the PT index in the packet is sane */
    //if(req->pt_index > 0x33)
    //    return -1;

    /* If the PT index is 0x30, this is a box, not an enemy! */
    if(req->pt_index == 0x30)
        return pt_generate_gc_boxdrop(c, l, r);

    /* Figure out the area we'll be worried with */
    area = c->cur_area;

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
    if(l->qid && !(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_RARES))
        do_rare = 0;

    /* See if the user is lucky today... */
    if(do_rare && (item[0] = rt_generate_gc_rare(c, l, req->pt_index, 0))) {
        switch(item[0] & 0xFF) {
            case 0:
                /* Weapon -- add percentages and (potentially) grind values and
                   such... */
                if(generate_weapon_v3(ent, area, item, rng, 1, 0))
                    return 0;
                break;

            case 1:
                /* Armor/Shield/Unit */
                switch((item[0] >> 8) & 0xFF) {
                    case 1:
                        /* Armor -- Add DFP/EVP boosts and slots */
                        if(generate_armor_v3(ent, area, item, rng, 1, 0))
                            return 0;
                        break;

                    case 2:
                        /* Shield -- Add DFP/EVP boosts */
                        if(generate_shield_v3(ent, area, item, rng, 1, 0))
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
                item[3] = 0x280000FF;
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

        return check_and_send(c, l, item, c->cur_area, req);
    }
    
    /* Figure out what type to drop... */
    rnd = mt19937_genrand_int32(rng) % 3;
    switch(rnd) {
        case 0:
            /* Drop the enemy's designated type of item. */
            switch(ent->enemy_drop[req->pt_index]) {
                case BOX_TYPE_WEAPON:
                    /* Drop a weapon */
                    if(generate_weapon_v3(ent, area, item, rng, 0, 0)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req);

                case BOX_TYPE_ARMOR:
                    /* Drop an armor */
                    if(generate_armor_v3(ent, area, item, rng, 0, 0)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req);

                case BOX_TYPE_SHIELD:
                    /* Drop a shield */
                    if(generate_shield_v3(ent, area, item, rng, 0, 0)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req);

                case BOX_TYPE_UNIT:
                    /* Drop a unit */
                    if(pmt_random_unit_gc(ent->unit_level[area], item, rng)) {
                        return 0;
                    }

                    return check_and_send(c, l, item, c->cur_area, req);

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
            if(generate_tool_v3(ent, area, item, rng)) {
                return 0;
            }

            return check_and_send(c, l, item, c->cur_area, req);

        case 2:
            /* Drop meseta */
            if(generate_meseta(ent->enemy_meseta[req->pt_index][0],
                               ent->enemy_meseta[req->pt_index][1],
                               item, rng)) {
                return 0;
            }

            return check_and_send(c, l, item, c->cur_area, req);
    }

    /* Shouldn't ever get here... */
    return 0;
}

int pt_generate_gc_boxdrop(ship_client_t *c, lobby_t *l, void *r) {
    subcmd_bitemreq_t *req = (subcmd_bitemreq_t *)r;
    pt_v3_entry_t *ent = &gc_ptdata[l->episode][l->difficulty][l->section];
    uint16_t obj_id;
    game_object_t *gobj;
    map_object_t *obj;
    uint32_t rnd, t1, t2;
    int area, do_rare = 1;
    uint32_t item[4];
    float f1, f2;
    struct mt19937_state *rng = &c->cur_block->rng;

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

    /* Mark the box as spent now... */
    gobj->flags |= 0x00000001;

    /* See if the object is fixed-type box */
    t1 = LE32(obj->dword[0]);
    f1 = *((float *)&t1);
    if((obj->skin == LE32(0x00000092) || obj->skin == LE32(0x00000161)) &&
       f1 < 1.0f + EPSILON && f1 > 1.0f - EPSILON) {
        /* See if it is a fully-fixed item */
        t2 = LE32(obj->dword[1]);
        f2 = *((float *)&t2);

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

            return check_and_send(c, l, item, c->cur_area,
                                  (subcmd_itemreq_t *)req);
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

    /* See if we'll do a rare roll. */
    if(l->qid && !(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_RARES))
        do_rare = 0;

    /* See if the user is lucky today... */
    if(do_rare && (item[0] = rt_generate_gc_rare(c, l, -1, area + 1))) {
        switch(item[0] & 0xFF) {
            case 0:
                /* Weapon -- add percentages and (potentially) grind values and
                   such... */
                if(generate_weapon_v3(ent, area, item, rng, 1, 0))
                    return 0;
                break;

            case 1:
                /* Armor/Shield/Unit */
                switch((item[0] >> 8) & 0xFF) {
                    case 1:
                        /* Armor -- Add DFP/EVP boosts and slots */
                        if(generate_armor_v3(ent, area, item, rng, 1, 0))
                            return 0;
                        break;

                    case 2:
                        /* Shield -- Add DFP/EVP boosts */
                        if(generate_shield_v3(ent, area, item, rng, 1, 0))
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
                item[3] = 0x280000FF;
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

        return check_and_send(c, l, item, c->cur_area, (subcmd_itemreq_t *)req);
    }

    /* Generate an item, according to the PT data */
    rnd = mt19937_genrand_int32(rng) % 100;

    if((rnd -= ent->box_drop[BOX_TYPE_WEAPON][area]) > 100) {
generate_weapon:
        /* Generate a weapon */
        if(generate_weapon_v3(ent, area, item, rng, 0, 0))
            return 0;

        return check_and_send(c, l, item, c->cur_area, (subcmd_itemreq_t *)req);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_ARMOR][area]) > 100) {
generate_armor:
        /* Generate an armor */
        if(generate_armor_v3(ent, area, item, rng, 0, 0))
            return 0;

        return check_and_send(c, l, item, c->cur_area, (subcmd_itemreq_t *)req);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_SHIELD][area]) > 100) {
        /* Generate a shield */
        if(generate_shield_v3(ent, area, item, rng, 0, 0))
            return 0;

        return check_and_send(c, l, item, c->cur_area, (subcmd_itemreq_t *)req);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_UNIT][area]) > 100) {
        /* Generate a unit */
        if(pmt_random_unit_gc(ent->unit_level[area], item, rng))
            return 0;

        return check_and_send(c, l, item, c->cur_area, (subcmd_itemreq_t *)req);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_TOOL][area]) > 100) {
generate_tool:
        /* Generate a tool */
        if(generate_tool_v3(ent, area, item, rng))
            return 0;

        return check_and_send(c, l, item, c->cur_area, (subcmd_itemreq_t *)req);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_MESETA][area]) > 100) {
generate_meseta:
        /* Generate money! */
        if(generate_meseta(ent->box_meseta[area][0], ent->box_meseta[area][1],
                           item, rng))
            return 0;

        return check_and_send(c, l, item, c->cur_area, (subcmd_itemreq_t *)req);
    }

    /* You get nothing! */
    return 0;
}

int pt_generate_bb_drop(ship_client_t *c, lobby_t *l, void *r) {
    subcmd_bb_itemreq_t *req = (subcmd_bb_itemreq_t *)r;
    pt_v3_entry_t *ent;
    uint32_t rnd;
    uint32_t item[4];
    int area, do_rare = 1;
    struct mt19937_state *rng = &c->cur_block->rng;
    uint16_t mid;
    game_enemy_t *enemy;

    /* XXXX: Handle Episode 4 */
    if(l->episode == 3)
        return 0;

    ent = &bb_ptdata[l->episode - 1][l->difficulty][l->section];

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
    if(l->qid && !(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_RARES))
        do_rare = 0;

    /* See if the user is lucky today... */
    if(do_rare && (item[0] = rt_generate_gc_rare(c, l, req->pt_index, 0))) {
        switch(item[0] & 0xFF) {
            case 0:
                /* Weapon -- add percentages and (potentially) grind values and
                   such... */
                if(generate_weapon_v3(ent, area, item, rng, 1, 1))
                    return 0;
                break;

            case 1:
                /* Armor/Shield/Unit */
                switch((item[0] >> 8) & 0xFF) {
                    case 1:
                        /* Armor -- Add DFP/EVP boosts and slots */
                        if(generate_armor_v3(ent, area, item, rng, 1, 1))
                            return 0;
                        break;

                    case 2:
                        /* Shield -- Add DFP/EVP boosts */
                        if(generate_shield_v3(ent, area, item, rng, 1, 1))
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

        return check_and_send_bb(c, l, item, c->cur_area, req);
    }
    
    /* Figure out what type to drop... */
    rnd = mt19937_genrand_int32(rng) % 3;
    switch(rnd) {
        case 0:
            /* Drop the enemy's designated type of item. */
            switch(ent->enemy_drop[req->pt_index]) {
                case BOX_TYPE_WEAPON:
                    /* Drop a weapon */
                    if(generate_weapon_v3(ent, area, item, rng, 0, 1)) {
                        return 0;
                    }

                    return check_and_send_bb(c, l, item, c->cur_area, req);

                case BOX_TYPE_ARMOR:
                    /* Drop an armor */
                    if(generate_armor_v3(ent, area, item, rng, 0, 1)) {
                        return 0;
                    }

                    return check_and_send_bb(c, l, item, c->cur_area, req);

                case BOX_TYPE_SHIELD:
                    /* Drop a shield */
                    if(generate_shield_v3(ent, area, item, rng, 0, 1)) {
                        return 0;
                    }

                    return check_and_send_bb(c, l, item, c->cur_area, req);

                case BOX_TYPE_UNIT:
                    /* Drop a unit */
                    if(pmt_random_unit_bb(ent->unit_level[area], item, rng)) {
                        return 0;
                    }

                    return check_and_send_bb(c, l, item, c->cur_area, req);

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
            if(generate_tool_v3(ent, area, item, rng)) {
                return 0;
            }

            return check_and_send_bb(c, l, item, c->cur_area, req);

        case 2:
            /* Drop meseta */
            if(generate_meseta(ent->enemy_meseta[req->pt_index][0],
                               ent->enemy_meseta[req->pt_index][1],
                               item, rng)) {
                return 0;
            }

            return check_and_send_bb(c, l, item, c->cur_area, req);
    }

    /* Shouldn't ever get here... */
    return 0;
}

int pt_generate_bb_boxdrop(ship_client_t *c, lobby_t *l, void *r) {
    subcmd_bb_bitemreq_t *req = (subcmd_bb_bitemreq_t *)r;
    pt_v3_entry_t *ent;
    uint16_t obj_id;
    game_object_t *gobj;
    map_object_t *obj;
    uint32_t rnd, t1, t2;
    int area, do_rare = 1;
    uint32_t item[4];
    float f1, f2;
    struct mt19937_state *rng = &c->cur_block->rng;

    /* XXXX: Handle Episode 4 */
    if(l->episode == 3)
        return 0;

    ent = &bb_ptdata[l->episode][l->difficulty][l->section];

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

    /* Mark the box as spent now... */
    gobj->flags |= 0x00000001;

    /* See if the object is fixed-type box */
    t1 = LE32(obj->dword[0]);
    f1 = *((float *)&t1);
    if((obj->skin == LE32(0x00000092) || obj->skin == LE32(0x00000161)) &&
       f1 < 1.0f + EPSILON && f1 > 1.0f - EPSILON) {
        /* See if it is a fully-fixed item */
        t2 = LE32(obj->dword[1]);
        f2 = *((float *)&t2);

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
                                     (subcmd_bb_itemreq_t *)req);
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

    /* See if we'll do a rare roll. */
    if(l->qid && !(ship->cfg->local_flags & SYLVERANT_SHIP_QUEST_RARES))
        do_rare = 0;

    /* See if the user is lucky today... */
    if(do_rare && (item[0] = rt_generate_gc_rare(c, l, -1, area + 1))) {
        switch(item[0] & 0xFF) {
            case 0:
                /* Weapon -- add percentages and (potentially) grind values and
                   such... */
                if(generate_weapon_v3(ent, area, item, rng, 1, 1))
                    return 0;
                break;

            case 1:
                /* Armor/Shield/Unit */
                switch((item[0] >> 8) & 0xFF) {
                    case 1:
                        /* Armor -- Add DFP/EVP boosts and slots */
                        if(generate_armor_v3(ent, area, item, rng, 1, 1))
                            return 0;
                        break;

                    case 2:
                        /* Shield -- Add DFP/EVP boosts */
                        if(generate_shield_v3(ent, area, item, rng, 1, 1))
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
                                 (subcmd_bb_itemreq_t *)req);
    }

    /* Generate an item, according to the PT data */
    rnd = mt19937_genrand_int32(rng) % 100;

    if((rnd -= ent->box_drop[BOX_TYPE_WEAPON][area]) > 100) {
generate_weapon:
        /* Generate a weapon */
        if(generate_weapon_v3(ent, area, item, rng, 0, 1))
            return 0;

        return check_and_send_bb(c, l, item, c->cur_area,
                                 (subcmd_bb_itemreq_t *)req);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_ARMOR][area]) > 100) {
generate_armor:
        /* Generate an armor */
        if(generate_armor_v3(ent, area, item, rng, 0, 1))
            return 0;

        return check_and_send_bb(c, l, item, c->cur_area,
                                 (subcmd_bb_itemreq_t *)req);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_SHIELD][area]) > 100) {
        /* Generate a shield */
        if(generate_shield_v3(ent, area, item, rng, 0, 1))
            return 0;

        return check_and_send_bb(c, l, item, c->cur_area,
                                 (subcmd_bb_itemreq_t *)req);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_UNIT][area]) > 100) {
        /* Generate a unit */
        if(pmt_random_unit_bb(ent->unit_level[area], item, rng))
            return 0;

        return check_and_send_bb(c, l, item, c->cur_area,
                                 (subcmd_bb_itemreq_t *)req);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_TOOL][area]) > 100) {
generate_tool:
        /* Generate a tool */
        if(generate_tool_v3(ent, area, item, rng))
            return 0;

        return check_and_send_bb(c, l, item, c->cur_area,
                                 (subcmd_bb_itemreq_t *)req);
    }
    else if((rnd -= ent->box_drop[BOX_TYPE_MESETA][area]) > 100) {
generate_meseta:
        /* Generate money! */
        if(generate_meseta(ent->box_meseta[area][0], ent->box_meseta[area][1],
                           item, rng))
            return 0;

        return check_and_send_bb(c, l, item, c->cur_area,
                                 (subcmd_bb_itemreq_t *)req);
    }

    /* You get nothing! */
    return 0;
}
