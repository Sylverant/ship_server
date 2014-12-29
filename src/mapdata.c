/*
    Sylverant Ship Server
    Copyright (C) 2012, 2013, 2014 Lawrence Sebald

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
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sylverant/debug.h>
#include <sylverant/prs.h>

#include "mapdata.h"
#include "lobby.h"
#include "clients.h"

/* Enemy battle parameters. The array is organized in the following levels:
   multi/single player, episode, difficulty, entry.*/
static bb_battle_param_t battle_params[2][3][4][0x60];

/* Player levelup data */
bb_level_table_t char_stats;

/* Parsed enemy data. Organized similarly to the battle parameters, except that
   the last level is the actual areas themselves (and there's no difficulty
   level in there). */
static parsed_map_t bb_parsed_maps[2][3][0x10];
static parsed_objs_t bb_parsed_objs[2][3][0x10];

/* V2 Parsed enemy data. This is much simpler, since there's no episodes nor
   single-player mode to worry about. */
static parsed_map_t v2_parsed_maps[0x10];
static parsed_objs_t v2_parsed_objs[0x10];

/* Did we read in v2 map data? */
static int have_v2_maps = 0;

/* GC Parsed enemy data. Midway point between v2 and BB. Two episodes, but no
   single player mode. */
static parsed_map_t gc_parsed_maps[2][0x10];
static parsed_objs_t gc_parsed_objs[2][0x10];

/* Did we read in gc map data? */
static int have_gc_maps = 0;

/* Header for sections of the .dat files for quests. */
typedef struct quest_dat_hdr {
    uint32_t obj_type;
    uint32_t next_hdr;
    uint32_t area;
    uint32_t size;
    uint8_t data[];
} quest_dat_hdr_t;

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
    uint8_t *buf;
    int decsize;

#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__)
    int i, j;
#endif

    /* Read in the file and decompress it. */
    if((decsize = prs_decompress_file(fn, &buf)) < 0) {
        debug(DBG_ERROR, "Cannot read levels %s: %s\n", fn, strerror(-decsize));
        return -1;
    }

    memcpy(&char_stats, buf, sizeof(bb_level_table_t));

#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__)
    /* Swap all the exp values */
    for(j = 0; j < 12; ++j) {
        for(i = 0; i < 200; ++i) {
            char_stats[j][i].exp = LE32(char_stats[j][i].exp);
        }
    }
#endif

    /* Clean up... */
    free(buf);

    return 0;
}

static const uint32_t maps[3][0x20] = {
    {1,1,1,5,1,5,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,1,1,1,1,1,1,1,1,1,1},
    {1,1,2,1,2,1,2,1,2,1,1,3,1,3,1,3,2,2,1,3,2,2,2,2,1,1,1,1,1,1,1,1},
    {1,1,1,3,1,3,1,3,1,3,1,3,3,1,1,3,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

static const uint32_t sp_maps[3][0x20] = {
    {1,1,1,3,1,3,3,1,3,1,3,1,3,2,3,2,3,2,3,2,3,2,1,1,1,1,1,1,1,1,1,1},
    {1,1,2,1,2,1,2,1,2,1,1,3,1,3,1,3,2,2,1,3,2,1,2,1,1,1,1,1,1,1,1,1},
    {1,1,1,3,1,3,1,3,1,3,1,3,3,1,1,3,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

static const int max_area[3] = { 0x0E, 0x0F, 0x09 };

static int parse_map(map_enemy_t *en, int en_ct, game_enemies_t *game,
                     int ep, int alt) {
    int i, j;
    game_enemy_t *gen;
    void *tmp;
    uint32_t count = 0;
    uint16_t n_clones;
    int acc;

    /* Allocate the space first */
    if(!(gen = (game_enemy_t *)malloc(sizeof(game_enemy_t) * 0xB50))) {
        debug(DBG_ERROR, "Cannot allocate enemies: %s\n", strerror(errno));
        return -1;
    }

    /* Clear it */
    memset(gen, 0, sizeof(game_enemy_t) * 0xB50);

    /* Parse each enemy. */
    for(i = 0; i < en_ct; ++i) {
        n_clones = en[i].num_clones;

        switch(en[i].base) {
            case 0x0040:    /* Hildebear & Hildetorr */
                acc = en[i].skin & 0x01;
                gen[count].bp_entry = 0x49 + acc;
                gen[count].rt_index = 0x01 + acc;
                break;

            case 0x0041:    /* Rappies */
                acc = en[i].skin & 0x01;
                if(ep == 3) {   /* Del Rappy & Sand Rappy */
                    if(alt) {
                        gen[count].bp_entry = 0x17 + acc;
                        gen[count].rt_index = 0x11 + acc;
                    }
                    else {
                        gen[count].bp_entry = 0x05 + acc;
                        gen[count].rt_index = 0x11 + acc;
                    }
                }
                else {
                    if(acc) {
                        gen[count].bp_entry = 0x19;

                        if(ep == 1) {
                            gen[count].rt_index = 0x06;
                        }
                        else {
                            /* We need to fill this in when we make the lobby,
                               since it's dependent on the event. */
                            gen[count].rt_index = (uint8_t)-1;
                        }
                    }
                    else {
                        gen[count].bp_entry = 0x18;
                        gen[count].rt_index = 0x05;
                    }
                }
                break;

            case 0x0042:    /* Monest + 30 Mothmants */
                gen[count].bp_entry = 0x01;
                gen[count].rt_index = 0x04;

                for(j = 0; j < 30; ++j) {
                    ++count;
                    gen[count].bp_entry = 0x00;
                    gen[count].rt_index = 0x03;
                }
                break;

            case 0x0043:    /* Savage Wolf & Barbarous Wolf */
                acc = (en[i].reserved[10] & 0x800000) ? 1 : 0;
                gen[count].bp_entry = 0x02 + acc;
                gen[count].rt_index = 0x07 + acc;
                break;

            case 0x0044:    /* Booma family */
                acc = en[i].skin % 3;
                gen[count].bp_entry = 0x4B + acc;
                gen[count].rt_index = 0x09 + acc;
                break;

            case 0x0060:    /* Grass Assassin */
                gen[count].bp_entry = 0x4E;
                gen[count].rt_index = 0x0C;
                break;

            case 0x0061:    /* Del Lily, Poison Lily, Nar Lily */
                if(ep == 2 && alt) {
                    gen[count].bp_entry = 0x25;
                    gen[count].rt_index = 0x53;
                }
                else {
                    acc = (en[i].reserved[10] & 0x800000) ? 1 : 0;
                    gen[count].bp_entry = 0x04 + acc;
                    gen[count].rt_index = 0x0D + acc;
                }
                break;

            case 0x0062:    /* Nano Dragon */
                gen[count].bp_entry = 0x1A;
                gen[count].rt_index = 0x0E;
                break;

            case 0x0063:    /* Shark Family */
                acc = en[i].skin % 3;
                gen[count].bp_entry = 0x4F + acc;
                gen[count].rt_index = 0x10 + acc;
                break;

            case 0x0064:    /* Slime + 4 clones */
                acc = (en[i].reserved[10] & 0x800000) ? 1 : 0;
                gen[count].bp_entry = 0x30 - acc;
                gen[count].rt_index = 0x13 + acc;

                for(j = 0; j < 4; ++j) {
                    ++count;
                    gen[count].bp_entry = 0x30;
                    gen[count].rt_index = 0x13;
                }
                break;

            case 0x0065:    /* Pan Arms, Migium, Hidoom */
                for(j = 0; j < 3; ++j) {
                    gen[count + j].bp_entry = 0x31 + j;
                    gen[count + j].rt_index = 0x15 + j;
                }

                count += 2;
                break;

            case 0x0080:    /* Dubchic & Gilchic */
                acc = en[i].skin & 0x01;
                gen[count].bp_entry = 0x1B + acc;
                gen[count].rt_index = (0x18 + acc) << acc;
                break;

            case 0x0081:    /* Garanz */
                gen[count].bp_entry = 0x1D;
                gen[count].rt_index = 0x19;
                break;

            case 0x0082:    /* Sinow Beat & Sinow Gold */
                acc = (en[i].reserved[10] & 0x800000) ? 1 : 0;
                if(acc) {
                    gen[count].bp_entry = 0x13;
                    gen[count].rt_index = 0x1B;
                }
                else {
                    gen[count].bp_entry = 0x06;
                    gen[count].rt_index = 0x1A;
                }

                if(!n_clones)
                    n_clones = 4;
                break;

            case 0x0083:    /* Canadine */
                gen[count].bp_entry = 0x07;
                gen[count].rt_index = 0x1C;
                break;

            case 0x0084:    /* Canadine Group */
                gen[count].bp_entry = 0x09;
                gen[count].rt_index = 0x1D;

                for(j = 0; j < 8; ++j) {
                    ++count;
                    gen[count].bp_entry = 0x08;
                    gen[count].rt_index = 0x1C;
                }
                break;

            case 0x0085:    /* Dubwitch */
                break;

            case 0x00A0:    /* Delsaber */
                gen[count].bp_entry = 0x52;
                gen[count].rt_index = 0x1E;
                break;

            case 0x00A1:    /* Chaos Sorcerer */
                gen[count].bp_entry = 0x0A;
                gen[count].rt_index = 0x1F;

                /* Bee L */
                gen[count + 1].bp_entry = 0x0B;
                gen[count + 1].rt_index = 0x00;

                /* Bee R */
                gen[count + 2].bp_entry = 0x0C;
                gen[count + 2].rt_index = 0x00;
                count += 2;
                break;

            case 0x00A2:    /* Dark Gunner */
                gen[count].bp_entry = 0x1E;
                gen[count].rt_index = 0x22;
                break;

            case 0x00A3:    /* Death Gunner? */
                break;

            case 0x00A4:    /* Chaos Bringer */
                gen[count].bp_entry = 0x0D;
                gen[count].rt_index = 0x24;
                break;

            case 0x00A5:    /* Dark Belra */
                gen[count].bp_entry = 0x0E;
                gen[count].rt_index = 0x25;
                break;

            case 0x00A6:    /* Dimenian Family */
                acc = en[i].skin % 3;
                gen[count].bp_entry = 0x53 + acc;
                gen[count].rt_index = 0x29 + acc;
                break;

            case 0x00A7:    /* Bulclaw + 4 Claws */
                gen[count].bp_entry = 0x1F;
                gen[count].rt_index = 0x28;

                for(j = 0; j < 4; ++j) {
                    ++count;
                    gen[count].bp_entry = 0x20;
                    gen[count].rt_index = 0x26;
                }
                break;

            case 0x00A8:    /* Claw */
                gen[count].bp_entry = 0x20;
                gen[count].rt_index = 0x26;
                break;

            case 0x00C0:    /* Dragon or Gal Gryphon */
                if(ep == 1) {
                    gen[count].bp_entry = 0x12;
                    gen[count].rt_index = 0x2C;
                }
                else {
                    gen[count].bp_entry = 0x1E;
                    gen[count].rt_index = 0x4D;
                }
                break;

            case 0x00C1:    /* De Rol Le */
                gen[count].bp_entry = 0x0F;
                gen[count].rt_index = 0x2D;
                break;

            case 0x00C2:    /* Vol Opt (form 1) */
                break;

            case 0x00C5:    /* Vol Opt (form 2) */
                gen[count].bp_entry = 0x25;
                gen[count].rt_index = 0x2E;
                break;

            case 0x00C8:    /* Dark Falz (3 forms) + 510 Darvants */
                /* Deal with the Darvants first. */
                for(j = 0; j < 510; ++j) {
                    gen[count].bp_entry = 0x35;
                    gen[count++].rt_index = 0;
                }

                /* The forms are backwards in their ordering... */
                gen[count].bp_entry = 0x38;
                gen[count++].rt_index = 0x2F;

                gen[count].bp_entry = 0x37;
                gen[count++].rt_index = 0x2F;

                gen[count].bp_entry = 0x36;
                gen[count].rt_index = 0x2F;
                break;

            case 0x00CA:    /* Olga Flow */
                gen[count].bp_entry = 0x2C;
                gen[count].rt_index = 0x4E;
                count += 512;
                break;

            case 0x00CB:    /* Barba Ray */
                gen[count].bp_entry = 0x0F;
                gen[count].rt_index = 0x49;
                count += 47;
                break;

            case 0x00CC:    /* Gol Dragon */
                gen[count].bp_entry = 0x12;
                gen[count].rt_index = 0x4C;
                count += 5;
                break;

            case 0x00D4:    /* Sinow Berill & Spigell */
                /* XXXX: How to do rare? Tethealla looks at skin, Newserv at the
                   reserved[10] value... */
                acc = en[i].skin >= 0x01 ? 1 : 0;
                if(acc) {
                    gen[count].bp_entry = 0x13;
                    gen[count].rt_index = 0x3F;
                }
                else {
                    gen[count].bp_entry = 0x06;
                    gen[count].rt_index = 0x3E;
                }

                count += 4; /* Add 4 clones which are never used... */
                break;

            case 0x00D5:    /* Merillia & Meriltas */
                acc = en[i].skin & 0x01;
                gen[count].bp_entry = 0x4B + acc;
                gen[count].rt_index = 0x34 + acc;
                break;

            case 0x00D6:    /* Mericus, Merikle, or Mericarol */
                acc = en[i].skin % 3;
                if(acc)
                    gen[count].bp_entry = 0x44 + acc;
                else
                    gen[count].bp_entry = 0x3A;

                gen[count].rt_index = 0x38 + acc;
                break;

            case 0x00D7:    /* Ul Gibbon & Zol Gibbon */
                acc = en[i].skin & 0x01;
                gen[count].bp_entry = 0x3B + acc;
                gen[count].rt_index = 0x3B + acc;
                break;

            case 0x00D8:    /* Gibbles */
                gen[count].bp_entry = 0x3D;
                gen[count].rt_index = 0x3D;
                break;

            case 0x00D9:    /* Gee */
                gen[count].bp_entry = 0x07;
                gen[count].rt_index = 0x36;
                break;

            case 0x00DA:    /* Gi Gue */
                gen[count].bp_entry = 0x1A;
                gen[count].rt_index = 0x37;
                break;

            case 0x00DB:    /* Deldepth */
                gen[count].bp_entry = 0x30;
                gen[count].rt_index = 0x47;
                break;

            case 0x00DC:    /* Delbiter */
                gen[count].bp_entry = 0x0D;
                gen[count].rt_index = 0x48;
                break;

            case 0x00DD:    /* Dolmolm & Dolmdarl */
                acc = en[i].skin & 0x01;
                gen[count].bp_entry = 0x4F + acc;
                gen[count].rt_index = 0x40 + acc;
                break;

            case 0x00DE:    /* Morfos */
                gen[count].bp_entry = 0x41;
                gen[count].rt_index = 0x42;
                break;

            case 0x00DF:    /* Recobox & Recons */
                gen[count].bp_entry = 0x41;
                gen[count].rt_index = 0x43;

                for(j = 1; j <= n_clones; ++j) {
                    gen[++count].bp_entry = 0x42;
                    gen[count].rt_index = 0x44;
                }

                /* Don't double count them. */
                n_clones = 0;
                break;

            case 0x00E0:    /* Epsilon, Sinow Zoa & Zele */
                if(ep == 2 && alt) {
                    gen[count].bp_entry = 0x23;
                    gen[count].rt_index = 0x54;
                    count += 4;
                }
                else {
                    acc = en[i].skin & 0x01;
                    gen[count].bp_entry = 0x43 + acc;
                    gen[count].rt_index = 0x45 + acc;
                }
                break;

            case 0x00E1:    /* Ill Gill */
                gen[count].bp_entry = 0x26;
                gen[count].rt_index = 0x52;
                break;

            case 0x0110:    /* Astark */
                gen[count].bp_entry = 0x09;
                gen[count].rt_index = 0x01;
                break;

            case 0x0111:    /* Satellite Lizard & Yowie */
                acc = (en[i].reserved[10] & 0x800000) ? 1 : 0;
                if(alt)
                    gen[count].bp_entry = 0x0D + acc + 0x10;
                else
                    gen[count].bp_entry = 0x0D + acc;

                gen[count].rt_index = 0x02 + acc;
                break;

            case 0x0112:    /* Merissa A/AA */
                acc = en[i].skin & 0x01;
                gen[count].bp_entry = 0x19 + acc;
                gen[count].rt_index = 0x04 + acc;
                break;

            case 0x0113:    /* Girtablulu */
                gen[count].bp_entry = 0x1F;
                gen[count].rt_index = 0x06;
                break;

            case 0x0114:    /* Zu & Pazuzu */
                acc = en[i].skin & 0x01;
                if(alt)
                    gen[count].bp_entry = 0x07 + acc + 0x14;
                else
                    gen[count].bp_entry = 0x07 + acc;

                gen[count].rt_index = 7 + acc;
                break;

            case 0x0115:    /* Boota Family */
                acc = en[i].skin % 3;
                gen[count].rt_index = 0x09 + acc;
                if(en[i].skin & 0x02)
                    gen[count].bp_entry = 0x03;
                else
                    gen[count].bp_entry = 0x00 + acc;
                break;

            case 0x0116:    /* Dorphon & Eclair */
                acc = en[i].skin & 0x01;
                gen[count].bp_entry = 0x0F + acc;
                gen[count].rt_index = 0x0C + acc;
                break;

            case 0x0117:    /* Goran Family */
                acc = en[i].skin % 3;
                gen[count].bp_entry = 0x11 + acc;
                if(en[i].skin & 0x02)
                    gen[count].rt_index = 0x0F;
                else if(en[i].skin & 0x01)
                    gen[count].rt_index = 0x10;
                else
                    gen[count].rt_index = 0x0E;
                break;

            case 0x119: /* Saint Million, Shambertin, & Kondrieu */
                acc = en[i].skin & 0x01;
                gen[count].bp_entry = 0x22;
                if(en[i].reserved[10] & 0x800000)
                    gen[count].rt_index = 0x15;
                else
                    gen[count].rt_index = 0x13 + acc;
                break;

            default:
#ifdef VERBOSE_DEBUGGING
                debug(DBG_WARN, "Unknown enemy ID: %04X\n", en[i].base);
#endif
                break;
        }

        /* Increment the counter, as needed */
        if(n_clones) {
            for(j = 0; j < n_clones; ++j, ++count) {
                gen[count + 1].rt_index = gen[count].rt_index;
                gen[count + 1].bp_entry = gen[count].bp_entry;
            }
        }
        ++count;
    }

    /* Resize, so as not to waste space */
    if(!(tmp = realloc(gen, sizeof(game_enemy_t) * count))) {
        debug(DBG_WARN, "Cannot resize enemies: %s\n", strerror(errno));
        tmp = gen;
    }

    game->enemies = (game_enemy_t *)tmp;
    game->count = count;

    return 0;
}

static int read_bb_map_set(int solo, int i, int j) {
    int srv;
    char fn[256];
    int k, l, nmaps, nvars, m;
    FILE *fp;
    long sz;
    map_enemy_t *en;
    map_object_t *obj;
    game_object_t *gobj;
    game_enemies_t *tmp;
    game_objs_t *tmp2;

    if(!solo) {
        nmaps = maps[i][j << 1];
        nvars = maps[i][(j << 1) + 1];
    }
    else {
        nmaps = sp_maps[i][j << 1];
        nvars = sp_maps[i][(j << 1) + 1];
    }

    bb_parsed_maps[solo][i][j].map_count = nmaps;
    bb_parsed_maps[solo][i][j].variation_count = nvars;
    bb_parsed_objs[solo][i][j].map_count = nmaps;
    bb_parsed_objs[solo][i][j].variation_count = nvars;

    if(!(tmp = (game_enemies_t *)malloc(sizeof(game_enemies_t) * nmaps *
                                        nvars))) {
        debug(DBG_ERROR, "Cannot allocate for maps: %s\n",
              strerror(errno));
        return 10;
    }

    bb_parsed_maps[solo][i][j].data = tmp;

    if(!(tmp2 = (game_objs_t *)malloc(sizeof(game_objs_t) * nmaps * nvars))) {
        debug(DBG_ERROR, "Cannot allocate for objs: %s\n", strerror(errno));
        return 11;
    }

    bb_parsed_objs[solo][i][j].data = tmp2;

    for(k = 0; k < nmaps; ++k) {                /* Map Number */
        for(l = 0; l < nvars; ++l) {            /* Variation */
            tmp[k * nvars + l].count = 0;
            fp = NULL;

            /* For single-player mode, try the single-player specific map first,
               then try the multi-player one (since some maps are shared). */
            if(solo) {
                srv = snprintf(fn, 256, "s%d%X%d%d.dat", i + 1, j, k, l);
                if(srv >= 256) {
                    return 1;
                }

                fp = fopen(fn, "rb");
            }

            if(!fp) {
                srv = snprintf(fn, 256, "m%d%X%d%d.dat", i + 1, j, k, l);
                if(srv >= 256) {
                    return 1;
                }

                if(!(fp = fopen(fn, "rb"))) {
                    debug(DBG_ERROR, "Cannot read map \"%s\": %s\n", fn,
                          strerror(errno));
                    return 2;
                }
            }

            /* Figure out how long the file is, so we know what to read in... */
            if(fseek(fp, 0, SEEK_END) < 0) {
                debug(DBG_ERROR, "Cannot seek: %s\n", strerror(errno));
                fclose(fp);
                return 3;
            }

            if((sz = ftell(fp)) < 0) {
                debug(DBG_ERROR, "ftell: %s\n", strerror(errno));
                fclose(fp);
                return 4;
            }

            if(fseek(fp, 0, SEEK_SET) < 0) {
                debug(DBG_ERROR, "Cannot seek: %s\n", strerror(errno));
                fclose(fp);
                return 5;
            }

            /* Make sure the size is sane */
            if(sz % 0x48) {
                debug(DBG_ERROR, "Invalid map size!\n");
                fclose(fp);
                return 6;
            }

            /* Allocate memory and read in the file. */
            if(!(en = (map_enemy_t *)malloc(sz))) {
                debug(DBG_ERROR, "malloc: %s\n", strerror(errno));
                fclose(fp);
                return 7;
            }

            if(fread(en, 1, sz, fp) != (size_t)sz) {
                debug(DBG_ERROR, "Cannot read file!\n");
                free(en);
                fclose(fp);
                return 8;
            }

            /* We're done with the file, so close it */
            fclose(fp);

            /* Parse */
            if(parse_map(en, sz / 0x48, &tmp[k * nvars + l], i + 1,
                         0)) {
                free(en);
                return 9;
            }

            /* Clean up, we're done with this for now... */
            free(en);
            fp = NULL;

            /* Now, grab the objects */
            if(solo) {
                srv = snprintf(fn, 256, "s%d%X%d%d_o.dat", i + 1, j, k, l);
                if(srv >= 256) {
                    return 1;
                }

                fp = fopen(fn, "rb");
            }

            if(!fp) {
                srv = snprintf(fn, 256, "m%d%X%d%d_o.dat", i + 1, j, k, l);
                if(srv >= 256) {
                    return 1;
                }

                if(!(fp = fopen(fn, "rb"))) {
                    debug(DBG_ERROR, "Cannot read objects file \"%s\": %s\n",
                          fn, strerror(errno));
                    return 2;
                }
            }

            /* Figure out how long the file is, so we know what to read in... */
            if(fseek(fp, 0, SEEK_END) < 0) {
                debug(DBG_ERROR, "Cannot seek: %s\n", strerror(errno));
                fclose(fp);
                return 3;
            }

            if((sz = ftell(fp)) < 0) {
                debug(DBG_ERROR, "ftell: %s\n", strerror(errno));
                fclose(fp);
                return 4;
            }

            if(fseek(fp, 0, SEEK_SET) < 0) {
                debug(DBG_ERROR, "Cannot seek: %s\n", strerror(errno));
                fclose(fp);
                return 5;
            }

            /* Make sure the size is sane */
            if(sz % 0x44) {
                debug(DBG_ERROR, "Invalid map size!\n");
                fclose(fp);
                return 6;
            }

            /* Allocate memory and read in the file. */
            if(!(obj = (map_object_t *)malloc(sz))) {
                debug(DBG_ERROR, "malloc: %s\n", strerror(errno));
                fclose(fp);
                return 7;
            }

            if(fread(obj, 1, sz, fp) != (size_t)sz) {
                debug(DBG_ERROR, "Cannot read file!\n");
                free(obj);
                fclose(fp);
                return 8;
            }

            /* We're done with the file, so close it */
            fclose(fp);

            /* Make space for the game object representation. */
            gobj = (game_object_t *)malloc((sz / 0x44) * sizeof(game_object_t));
            if(!gobj) {
                debug(DBG_ERROR, "Cannot allocate game objects: %s\n",
                      strerror(errno));
                free(obj);
                fclose(fp);
                return 9;
            }

            /* Store what we'll actually use later... */
            for(m = 0; m < sz / 0x44; ++m) {
                gobj[m].data = obj[m];
                gobj[m].flags = 0;
            }

            /* Save it into the struct */
            tmp2[k * nvars + l].count = sz / 0x44;
            tmp2[k * nvars + l].objs = gobj;
        }
    }

    return 0;
}

static int read_v2_map_set(int j, int gcep) {
    int srv, ep;
    char fn[256];
    int k, l, nmaps, nvars, i;
    FILE *fp;
    long sz;
    map_enemy_t *en;
    map_object_t *obj;
    game_object_t *gobj;
    game_enemies_t *tmp;
    game_objs_t *tmp2;

    if(!gcep) {
        nmaps = maps[0][j << 1];
        nvars = maps[0][(j << 1) + 1];
        ep = 1;
    }
    else {
        nmaps = maps[gcep - 1][j << 1];
        nvars = maps[gcep - 1][(j << 1) + 1];
        ep = gcep;
    }

    if(!gcep) {
        v2_parsed_maps[j].map_count = nmaps;
        v2_parsed_maps[j].variation_count = nvars;
        v2_parsed_objs[j].map_count = nmaps;
        v2_parsed_objs[j].variation_count = nvars;
    }
    else {
        gc_parsed_maps[gcep - 1][j].map_count = nmaps;
        gc_parsed_maps[gcep - 1][j].variation_count = nvars;
        gc_parsed_objs[gcep - 1][j].map_count = nmaps;
        gc_parsed_objs[gcep - 1][j].variation_count = nvars;
    }

    if(!(tmp = (game_enemies_t *)malloc(sizeof(game_enemies_t) * nmaps *
                                        nvars))) {
        debug(DBG_ERROR, "Cannot allocate for maps: %s\n", strerror(errno));
        return 10;
    }

    if(!gcep)
        v2_parsed_maps[j].data = tmp;
    else
        gc_parsed_maps[gcep - 1][j].data = tmp;

    if(!(tmp2 = (game_objs_t *)malloc(sizeof(game_objs_t) * nmaps * nvars))) {
        debug(DBG_ERROR, "Cannot allocate for objs: %s\n", strerror(errno));
        return 11;
    }

    if(!gcep)
        v2_parsed_objs[j].data = tmp2;
    else
        gc_parsed_objs[gcep - 1][j].data = tmp2;

    for(k = 0; k < nmaps; ++k) {                /* Map Number */
        for(l = 0; l < nvars; ++l) {            /* Variation */
            tmp[k * nvars + l].count = 0;

            if(!gcep)
                srv = snprintf(fn, 256, "m%X%d%d.dat", j, k, l);
            else
                srv = snprintf(fn, 256, "m%d%X%d%d.dat", gcep, j, k, l);

            if(srv >= 256) {
                return 1;
            }

            if(!(fp = fopen(fn, "rb"))) {
                debug(DBG_ERROR, "Cannot read map %s: %s\n", fn,
                      strerror(errno));
                return 2;
            }

            /* Figure out how long the file is, so we know what to read in... */
            if(fseek(fp, 0, SEEK_END) < 0) {
                debug(DBG_ERROR, "Cannot seek: %s\n", strerror(errno));
                fclose(fp);
                return 3;
            }

            if((sz = ftell(fp)) < 0) {
                debug(DBG_ERROR, "ftell: %s\n", strerror(errno));
                fclose(fp);
                return 4;
            }

            if(fseek(fp, 0, SEEK_SET) < 0) {
                debug(DBG_ERROR, "Cannot seek: %s\n", strerror(errno));
                fclose(fp);
                return 5;
            }

            /* Make sure the size is sane */
            if(sz % 0x48) {
                debug(DBG_ERROR, "Invalid map size!\n");
                fclose(fp);
                return 6;
            }

            /* Allocate memory and read in the file. */
            if(!(en = (map_enemy_t *)malloc(sz))) {
                debug(DBG_ERROR, "malloc: %s\n", strerror(errno));
                fclose(fp);
                return 7;
            }

            if(fread(en, 1, sz, fp) != (size_t)sz) {
                debug(DBG_ERROR, "Cannot read file!\n");
                free(en);
                fclose(fp);
                return 8;
            }

            /* We're done with the file, so close it */
            fclose(fp);

            /* Parse */
            if(parse_map(en, sz / 0x48, &tmp[k * nvars + l], ep, 0)) {
                free(en);
                return 9;
            }

            /* Clean up, we're done with this for now... */
            free(en);

            /* Now, grab the objects */
            if(!gcep)
                srv = snprintf(fn, 256, "m%X%d%d_o.dat", j, k, l);
            else
                srv = snprintf(fn, 256, "m%d%X%d%d_o.dat", gcep, j, k, l);

            if(srv >= 256) {
                return 1;
            }

            if(!(fp = fopen(fn, "rb"))) {
                debug(DBG_ERROR, "Cannot read objects: %s\n", strerror(errno));
                return 2;
            }

            /* Figure out how long the file is, so we know what to read in... */
            if(fseek(fp, 0, SEEK_END) < 0) {
                debug(DBG_ERROR, "Cannot seek: %s\n", strerror(errno));
                fclose(fp);
                return 3;
            }

            if((sz = ftell(fp)) < 0) {
                debug(DBG_ERROR, "ftell: %s\n", strerror(errno));
                fclose(fp);
                return 4;
            }

            if(fseek(fp, 0, SEEK_SET) < 0) {
                debug(DBG_ERROR, "Cannot seek: %s\n", strerror(errno));
                fclose(fp);
                return 5;
            }

            /* Make sure the size is sane */
            if(sz % 0x44) {
                debug(DBG_ERROR, "Invalid map size!\n");
                fclose(fp);
                return 6;
            }

            /* Allocate memory and read in the file. */
            if(!(obj = (map_object_t *)malloc(sz))) {
                debug(DBG_ERROR, "malloc: %s\n", strerror(errno));
                fclose(fp);
                return 7;
            }

            if(fread(obj, 1, sz, fp) != (size_t)sz) {
                debug(DBG_ERROR, "Cannot read file!\n");
                free(obj);
                fclose(fp);
                return 8;
            }

            /* We're done with the file, so close it */
            fclose(fp);

            /* Make space for the game object representation. */
            gobj = (game_object_t *)malloc((sz / 0x44) * sizeof(game_object_t));
            if(!gobj) {
                debug(DBG_ERROR, "Cannot allocate game objects: %s\n",
                      strerror(errno));
                free(obj);
                fclose(fp);
                return 9;
            }

            /* Store what we'll actually use later... */
            for(i = 0; i < sz / 0x44; ++i) {
                gobj[i].data = obj[i];
                gobj[i].flags = 0;
            }

            /* Save it into the struct */
            tmp2[k * nvars + l].count = sz / 0x44;
            tmp2[k * nvars + l].objs = gobj;
        }
    }

    return 0;
}

static int read_bb_map_files(void) {
    int srv, i, j;

    for(i = 0; i < 3; ++i) {                            /* Episode */
        for(j = 0; j < 16 && j <= max_area[i]; ++j) {   /* Area */
            /* Read both the multi-player and single-player maps. */
            if((srv = read_bb_map_set(0, i, j)))
                return srv;
            if((srv = read_bb_map_set(1, i, j)))
                return srv;
        }
    }

    return 0;
}

static int read_v2_map_files(void) {
    int srv, j;

    for(j = 0; j < 16 && j <= max_area[0]; ++j) {
        if((srv = read_v2_map_set(j, 0)))
            return srv;
    }

    return 0;
}

static int read_gc_map_files(void) {
    int srv, j;

    for(j = 0; j < 16 && j <= max_area[0]; ++j) {
        if((srv = read_v2_map_set(j, 1)))
            return srv;
    }

    for(j = 0; j < 16 && j <= max_area[1]; ++j) {
        if((srv = read_v2_map_set(j, 2)))
            return srv;
    }

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

    /* Next, try to read the map data */
    if(chdir(cfg->bb_map_dir)) {
        debug(DBG_ERROR, "Error changing to Blue Burst param dir: %s\n",
              strerror(errno));
        free(buf);
        return 1;
    }

    debug(DBG_LOG, "Loading Blue Burst Map Enemy Data...\n");
    rv = read_bb_map_files();

    /* Change back to the original directory */
    if(chdir(path)) {
        debug(DBG_ERROR, "Cannot change back to original dir: %s\n",
              strerror(errno));
        free(buf);
        return -1;
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

int v2_read_params(sylverant_ship_t *cfg) {
    int rv = 0;
    long sz;
    char *buf, *path;

    /* Make sure we have a directory set... */
    if(!cfg->v2_map_dir) {
        debug(DBG_WARN, "No v2 map directory set. Will disable server-side "
              "drop support.\n");
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

    /* Next, try to read the map data */
    if(chdir(cfg->v2_map_dir)) {
        debug(DBG_ERROR, "Error changing to v2 map dir: %s\n",
              strerror(errno));
        rv = 1;
        goto bail;
    }

    debug(DBG_LOG, "Loading v2 Map Enemy Data...\n");
    rv = read_v2_map_files();

    /* Change back to the original directory */
    if(chdir(path)) {
        debug(DBG_ERROR, "Cannot change back to original dir: %s\n",
              strerror(errno));
        free(buf);
        return -1;
    }

bail:
    if(rv) {
        debug(DBG_ERROR, "Error reading v2 parameter data. Server-side drops "
              "will be disabled for v1/v2.\n");
    }
    else {
        have_v2_maps = 1;
    }

    /* Clean up and return. */
    free(buf);
    return rv;
}

int gc_read_params(sylverant_ship_t *cfg) {
    int rv = 0;
    long sz;
    char *buf, *path;

    /* Make sure we have a directory set... */
    if(!cfg->gc_map_dir) {
        debug(DBG_WARN, "No GC map directory set. Will disable server-side "
              "drop support.\n");
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

    /* Next, try to read the map data */
    if(chdir(cfg->gc_map_dir)) {
        debug(DBG_ERROR, "Error changing to GC map dir: %s\n",
              strerror(errno));
        rv = 1;
        goto bail;
    }

    debug(DBG_LOG, "Loading GC Map Enemy Data...\n");
    rv = read_gc_map_files();

    /* Change back to the original directory */
    if(chdir(path)) {
        debug(DBG_ERROR, "Cannot change back to original dir: %s\n",
              strerror(errno));
        free(buf);
        return -1;
    }

bail:
    if(rv) {
        debug(DBG_ERROR, "Error reading GC parameter data. Server-side drops "
              "will be disabled for PSOGC.\n");
    }
    else {
        have_gc_maps = 1;
    }

    /* Clean up and return. */
    free(buf);
    return rv;
}

void bb_free_params(void) {
    int i, j, k;
    uint32_t l, nmaps;
    parsed_map_t *m;

    for(i = 0; i < 2; ++i) {
        for(j = 0; j < 3; ++j) {
            for(k = 0; k < 0x10; ++k) {
                m = &bb_parsed_maps[i][j][k];
                nmaps = m->map_count * m->variation_count;

                for(l = 0; l < nmaps; ++l) {
                    free(m->data[l].enemies);
                }

                free(m->data);
                m->data = NULL;
                m->map_count = m->variation_count = 0;
            }
        }
    }
}

void v2_free_params(void) {
    int k;
    uint32_t l, nmaps;
    parsed_map_t *m;

    for(k = 0; k < 0x10; ++k) {
        m = &v2_parsed_maps[k];
        nmaps = m->map_count * m->variation_count;

        for(l = 0; l < nmaps; ++l) {
            free(m->data[l].enemies);
        }

        free(m->data);
        m->data = NULL;
        m->map_count = m->variation_count = 0;
    }
}

void gc_free_params(void) {
    int k, j;
    uint32_t l, nmaps;
    parsed_map_t *m;

    for(j = 0; j < 2; ++j) {
        for(k = 0; k < 0x10; ++k) {
            m = &gc_parsed_maps[j][k];
            nmaps = m->map_count * m->variation_count;

            for(l = 0; l < nmaps; ++l) {
                free(m->data[l].enemies);
            }

            free(m->data);
            m->data = NULL;
            m->map_count = m->variation_count = 0;
        }
    }
}

int bb_load_game_enemies(lobby_t *l) {
    game_enemies_t *en;
    game_objs_t *ob;
    int solo = (l->flags & LOBBY_FLAG_SINGLEPLAYER) ? 1 : 0, i;
    uint32_t enemies = 0, index, objects = 0, index2;
    parsed_map_t *maps;
    parsed_objs_t *objs;
    game_enemies_t *sets[0x10];
    game_objs_t *osets[0x10];

    /* Figure out the parameter set that will be in use first... */
    l->bb_params = battle_params[solo][l->episode - 1][l->difficulty];

    /* Figure out the total number of enemies that the game will have... */
    for(i = 0; i < 0x20; i += 2) {
        maps = &bb_parsed_maps[solo][l->episode - 1][i >> 1];
        objs = &bb_parsed_objs[solo][l->episode - 1][i >> 1];

        /* If we hit zeroes, then we're done already... */
        if(maps->map_count == 0 && maps->variation_count == 0) {
            sets[i >> 1] = NULL;
            break;
        }

        /* Sanity Check! */
        if(l->maps[i] > maps->map_count ||
           l->maps[i + 1] > maps->variation_count) {
            debug(DBG_ERROR, "Invalid map set generated for level %d (ep %d): "
                  "(%d %d)\n", i, l->episode, l->maps[i], l->maps[i + 1]);
            return -1;
        }

        index = l->maps[i] * maps->variation_count + l->maps[i + 1];
        enemies += maps->data[index].count;
        objects += objs->data[index].count;
        sets[i >> 1] = &maps->data[index];
        osets[i >> 1] = &objs->data[index];
    }

    /* Allocate space for the enemy set and the enemies therein. */
    if(!(en = (game_enemies_t *)malloc(sizeof(game_enemies_t)))) {
        debug(DBG_ERROR, "Error allocating enemy set: %s\n", strerror(errno));
        return -2;
    }

    if(!(en->enemies = (game_enemy_t *)malloc(sizeof(game_enemy_t) *
                                              enemies))) {
        debug(DBG_ERROR, "Error allocating enemies: %s\n", strerror(errno));
        free(en);
        return -3;
    }

    /* Allocate space for the object set and the objects therein. */
    if(!(ob = (game_objs_t *)malloc(sizeof(game_objs_t)))) {
        debug(DBG_ERROR, "Error allocating object set: %s\n", strerror(errno));
        free(en->enemies);
        free(en);
        return -4;
    }

    if(!(ob->objs = (game_object_t *)malloc(sizeof(game_object_t) * objects))) {
        debug(DBG_ERROR, "Error allocating objects: %s\n", strerror(errno));
        free(ob);
        free(en->enemies);
        free(en);
        return -5;
    }

    en->count = enemies;
    ob->count = objects;
    index = index2 = 0;

    /* Copy in the enemy data. */
    for(i = 0; i < 0x10; ++i) {
        if(!sets[i] || !osets[i])
            break;

        memcpy(&en->enemies[index], sets[i]->enemies,
               sizeof(game_enemy_t) * sets[i]->count);
        index += sets[i]->count;
        memcpy(&ob->objs[index2], osets[i]->objs,
               sizeof(game_object_t) * osets[i]->count);
        index2 += osets[i]->count;
    }

    /* Fixup Dark Falz' data for difficulties other than normal and the special
       Rappy data too... */
    for(i = 0; i < en->count; ++i) {
        if(en->enemies[i].bp_entry == 0x37 && l->difficulty) {
            en->enemies[i].bp_entry = 0x38;
        }
        else if(en->enemies[i].rt_index == (uint8_t)-1) {
            switch(l->event) {
                case LOBBY_EVENT_CHRISTMAS:
                    en->enemies[i].rt_index = 79;
                    break;
                case LOBBY_EVENT_EASTER:
                    en->enemies[i].rt_index = 81;
                    break;
                case LOBBY_EVENT_HALLOWEEN:
                    en->enemies[i].rt_index = 80;
                    break;
                default:
                    en->enemies[i].rt_index = 51;
                    break;
            }
        }
    }

    /* Done! */
    l->map_enemies = en;
    l->map_objs = ob;
    return 0;
}

int v2_load_game_enemies(lobby_t *l) {
    game_enemies_t *en;
    game_objs_t *ob;
    int i;
    uint32_t enemies = 0, index, objects = 0, index2;
    parsed_map_t *maps;
    parsed_objs_t *objs;
    game_enemies_t *sets[0x10];
    game_objs_t *osets[0x10];

    /* Figure out the total number of enemies that the game will have... */
    for(i = 0; i < 0x20; i += 2) {
        maps = &v2_parsed_maps[i >> 1];
        objs = &v2_parsed_objs[i >> 1];

        /* If we hit zeroes, then we're done already... */
        if(maps->map_count == 0 && maps->variation_count == 0) {
            sets[i >> 1] = NULL;
            break;
        }

        /* Sanity Check! */
        if(l->maps[i] > maps->map_count ||
           l->maps[i + 1] > maps->variation_count) {
            debug(DBG_ERROR, "Invalid map set generated for level %d (ep %d): "
                  "(%d %d)\n", i, l->episode, l->maps[i], l->maps[i + 1]);
            return -1;
        }

        index = l->maps[i] * maps->variation_count + l->maps[i + 1];
        enemies += maps->data[index].count;
        objects += objs->data[index].count;
        sets[i >> 1] = &maps->data[index];
        osets[i >> 1] = &objs->data[index];
    }

    /* Allocate space for the enemy set and the enemies therein. */
    if(!(en = (game_enemies_t *)malloc(sizeof(game_enemies_t)))) {
        debug(DBG_ERROR, "Error allocating enemy set: %s\n", strerror(errno));
        return -2;
    }

    if(!(en->enemies = (game_enemy_t *)malloc(sizeof(game_enemy_t) *
                                              enemies))) {
        debug(DBG_ERROR, "Error allocating enemies: %s\n", strerror(errno));
        free(en);
        return -3;
    }

    /* Allocate space for the object set and the objects therein. */
    if(!(ob = (game_objs_t *)malloc(sizeof(game_objs_t)))) {
        debug(DBG_ERROR, "Error allocating object set: %s\n", strerror(errno));
        free(en->enemies);
        free(en);
        return -4;
    }

    if(!(ob->objs = (game_object_t *)malloc(sizeof(game_object_t) * objects))) {
        debug(DBG_ERROR, "Error allocating objects: %s\n", strerror(errno));
        free(ob);
        free(en->enemies);
        free(en);
        return -5;
    }

    en->count = enemies;
    ob->count = objects;
    index = index2 = 0;

    /* Copy in the enemy data. */
    for(i = 0; i < 0x10; ++i) {
        if(!sets[i] || !osets[i])
            break;

        memcpy(&en->enemies[index], sets[i]->enemies,
               sizeof(game_enemy_t) * sets[i]->count);
        index += sets[i]->count;

        memcpy(&ob->objs[index2], osets[i]->objs,
               sizeof(game_object_t) * osets[i]->count);
        index2 += osets[i]->count;
    }

    /* Fixup Dark Falz' data for difficulties other than normal... */
    for(i = 0; i < en->count; ++i) {
        if(en->enemies[i].bp_entry == 0x37 && l->difficulty) {
            en->enemies[i].bp_entry = 0x38;
        }
    }

    /* Done! */
    l->map_enemies = en;
    l->map_objs = ob;
    return 0;
}

int gc_load_game_enemies(lobby_t *l) {
    game_enemies_t *en;
    game_objs_t *ob;
    int i;
    uint32_t enemies = 0, index, objects = 0, index2;
    parsed_map_t *maps;
    parsed_objs_t *objs;
    game_enemies_t *sets[0x10];
    game_objs_t *osets[0x10];

    /* Figure out the total number of enemies that the game will have... */
    for(i = 0; i < 0x20; i += 2) {
        maps = &gc_parsed_maps[l->episode - 1][i >> 1];
        objs = &gc_parsed_objs[l->episode - 1][i >> 1];

        /* If we hit zeroes, then we're done already... */
        if(maps->map_count == 0 && maps->variation_count == 0) {
            sets[i >> 1] = NULL;
            break;
        }

        /* Sanity Check! */
        if(l->maps[i] > maps->map_count ||
           l->maps[i + 1] > maps->variation_count) {
            debug(DBG_ERROR, "Invalid map set generated for level %d (ep %d): "
                  "(%d %d)\n", i, l->episode, l->maps[i], l->maps[i + 1]);
            return -1;
        }

        index = l->maps[i] * maps->variation_count + l->maps[i + 1];
        enemies += maps->data[index].count;
        objects += objs->data[index].count;
        sets[i >> 1] = &maps->data[index];
        osets[i >> 1] = &objs->data[index];
    }

    /* Allocate space for the enemy set and the enemies therein. */
    if(!(en = (game_enemies_t *)malloc(sizeof(game_enemies_t)))) {
        debug(DBG_ERROR, "Error allocating enemy set: %s\n", strerror(errno));
        return -2;
    }

    if(!(en->enemies = (game_enemy_t *)malloc(sizeof(game_enemy_t) *
                                              enemies))) {
        debug(DBG_ERROR, "Error allocating enemies: %s\n", strerror(errno));
        free(en);
        return -3;
    }

    /* Allocate space for the object set and the objects therein. */
    if(!(ob = (game_objs_t *)malloc(sizeof(game_objs_t)))) {
        debug(DBG_ERROR, "Error allocating object set: %s\n", strerror(errno));
        free(en->enemies);
        free(en);
        return -4;
    }

    if(!(ob->objs = (game_object_t *)malloc(sizeof(game_object_t) * objects))) {
        debug(DBG_ERROR, "Error allocating objects: %s\n", strerror(errno));
        free(ob);
        free(en->enemies);
        free(en);
        return -5;
    }

    en->count = enemies;
    ob->count = objects;
    index = index2 = 0;

    /* Copy in the enemy data. */
    for(i = 0; i < 0x10; ++i) {
        if(!sets[i] || !osets[i])
            break;

        memcpy(&en->enemies[index], sets[i]->enemies,
               sizeof(game_enemy_t) * sets[i]->count);
        index += sets[i]->count;

        memcpy(&ob->objs[index2], osets[i]->objs,
               sizeof(game_object_t) * osets[i]->count);
        index2 += osets[i]->count;
    }

    /* Fixup Dark Falz' data for difficulties other than normal and the special
       Rappy data too... */
    for(i = 0; i < en->count; ++i) {
        if(en->enemies[i].bp_entry == 0x37 && l->difficulty) {
            en->enemies[i].bp_entry = 0x38;
        }
        else if(en->enemies[i].rt_index == (uint8_t)-1) {
            switch(l->event) {
                case LOBBY_EVENT_CHRISTMAS:
                    en->enemies[i].rt_index = 79;
                    break;
                case LOBBY_EVENT_EASTER:
                    en->enemies[i].rt_index = 81;
                    break;
                case LOBBY_EVENT_HALLOWEEN:
                    en->enemies[i].rt_index = 80;
                    break;
                default:
                    en->enemies[i].rt_index = 51;
                    break;
            }
        }
    }

    /* Done! */
    l->map_enemies = en;
    l->map_objs = ob;
    return 0;
}

void free_game_enemies(lobby_t *l) {
    if(l->map_enemies) {
        free(l->map_enemies->enemies);
        free(l->map_enemies);
    }

    if(l->map_objs) {
        free(l->map_objs->objs);
        free(l->map_objs);
    }

    l->map_enemies = NULL;
    l->bb_params = NULL;
}

int map_have_v2_maps(void) {
    return have_v2_maps;
}

int map_have_gc_maps(void) {
    return have_gc_maps;
}

static void parse_quest_objects(const uint8_t *data, uint32_t len,
                                uint32_t *obj_cnt,
                                const quest_dat_hdr_t *ptrs[2][17]) {
    const quest_dat_hdr_t *hdr = (const quest_dat_hdr_t *)data;
    uint32_t ptr = 0;
    uint32_t obj_count = 0;

    while(ptr < len) {
        switch(LE32(hdr->obj_type)) {
            case 0x01:                      /* Objects */
                ptrs[0][LE32(hdr->area)] = hdr;
                obj_count += LE32(hdr->size) / sizeof(map_object_t);
                ptr += hdr->next_hdr;
                hdr = (const quest_dat_hdr_t *)(data + ptr);
                break;

            case 0x02:                      /* Enemies */
                ptrs[1][LE32(hdr->area)] = hdr;
                ptr += hdr->next_hdr;
                hdr = (const quest_dat_hdr_t *)(data + ptr);
                break;

            case 0x03:                      /* ??? - Skip */
                ptr += hdr->next_hdr;
                hdr = (const quest_dat_hdr_t *)(data + ptr);
                break;

            default:
                /* Padding at the end of the file... */
                ptr = len;
                break;
        }
    }

    *obj_cnt = obj_count;
}

int cache_quest_enemies(const char *ofn, const uint8_t *dat, uint32_t sz,
                        int episode) {
    int i, alt;
    uint32_t index, area, objects, j;
    const quest_dat_hdr_t *ptrs[2][17] = { { 0 } };
    game_enemies_t tmp_en;
    FILE *fp;
    const quest_dat_hdr_t *hdr;
    off_t offs;

    /* Open the cache file... */
    if(!(fp = fopen(ofn, "wb"))) {
        debug(DBG_WARN, "Cannot open cache file \"%s\" for writing: %s\n", ofn,
              strerror(errno));
        return -1;
    }

    /* Figure out the total number of enemies that the quest has... */
    parse_quest_objects(dat, sz, &objects, ptrs);

    /* Write out the objects in exactly the same form that they'll be needed
       when loaded later on. */
    objects = LE32(objects);
    if(fwrite(&objects, 1, 4, fp) != 4) {
        debug(DBG_WARN, "Error writing to cache file \"%s\": %s\n", ofn,
              strerror(errno));
        fclose(fp);
        return -2;
    }

    /* Run through each area and write them in order. */
    for(i = 0; i < 17; ++i) {
        if((hdr = ptrs[0][i])) {
            sz = LE32(hdr->size);
            objects = sz / sizeof(map_object_t);

            for(j = 0; j < objects; ++j) {
                /* Write the object itself. */
                if(fwrite(hdr->data + j * sizeof(map_object_t), 1,
                          sizeof(map_object_t), fp) != sizeof(map_object_t)) {
                    debug(DBG_WARN, "Error writing to cache file \"%s\": %s\n",
                          ofn, strerror(errno));
                    fclose(fp);
                    return -3;
                }
            }
        }
    }

    /* Save our position, as we don't know in advance how many parsed enemies
       we will have... */
    offs = ftello(fp);
    fseeko(fp, 4, SEEK_CUR);
    index = 0;

    /* Copy in the enemy data. */
    for(i = 0; i < 17; ++i) {
        if((hdr = ptrs[1][i])) {
            /* XXXX: Ugly! */
            sz = LE32(hdr->size);
            area = LE32(hdr->area);
            alt = 0;

            if((episode == 3 && area > 5) || (episode == 2 && area > 15))
                alt = 1;

            if(parse_map((map_enemy_t *)(hdr->data), sz / sizeof(map_enemy_t),
                         &tmp_en, episode, alt)) {
                debug(DBG_WARN, "Canot parse map for cache!\n");
                fclose(fp);
                return -4;
            }

            sz = tmp_en.count * sizeof(game_enemy_t);
            if(fwrite(tmp_en.enemies, 1, sz, fp) != sz) {
                debug(DBG_WARN, "Error writing to cache file \"%s\": %s\n", ofn,
                      strerror(errno));
                free(tmp_en.enemies);
                fclose(fp);
                return -5;
            }

            index += tmp_en.count;
            free(tmp_en.enemies);
        }
    }

    /* Go back and write the amount of enemies we have. */
    fseeko(fp, offs, SEEK_SET);
    index = LE32(index);

    if(fwrite(&index, 1, 4, fp) != 4) {
        debug(DBG_WARN, "Error writing to cache file \"%s\": %s\n", ofn,
              strerror(errno));
        fclose(fp);
        return -6;
    }

    /* We're done with the cache file now, so clean up and return. */
    fclose(fp);
    return 0;
}

int load_quest_enemies(lobby_t *l, uint32_t qid, int ver) {
    FILE *fp;
    size_t dlen = strlen(ship->cfg->quests_dir);
    char fn[dlen + 40];
    void *tmp;
    uint32_t cnt, i;
    sylverant_quest_t *q;
    quest_map_elem_t *el;

    /* If we aren't doing server-side drops on this game, don't bother. */
    if(!(l->flags & LOBBY_FLAG_SERVER_DROPS))
        return 0;

    /* Map PC->DCv2. */
    if(ver == CLIENT_VERSION_PC)
        ver = CLIENT_VERSION_DCV2;

    /* Figure out where we're looking... */
    sprintf(fn, "%s/.mapcache/%s/%08x", ship->cfg->quests_dir,
            version_codes[ver], qid);

    if(!(fp = fopen(fn, "rb"))) {
        debug(DBG_WARN, "Cannot open file \"%s\": %s\n", fn, strerror(errno));
        return -1;
    }

    /* Start by reading in the objects array. */
    if(fread(&cnt, 1, 4, fp) != 4) {
        debug(DBG_WARN, "Cannot read file \"%s\": %s\n", fn, strerror(errno));
        fclose(fp);
        return -2;
    }

    /* Unset this, in case something screws up. */
    l->flags &= ~LOBBY_FLAG_SERVER_DROPS;

    /* Reallocate the objects array. */
    l->map_objs->count = cnt = LE32(cnt);
    if(!(tmp = realloc(l->map_objs->objs, cnt * sizeof(game_object_t)))) {
        debug(DBG_WARN, "Cannot reallocate objects array: %s\n",
              strerror(errno));
        fclose(fp);
        return -3;
    }

    l->map_objs->objs = (game_object_t *)tmp;

    /* Read the objects in from the cache file. */
    for(i = 0; i < cnt; ++i) {
        if(fread(&l->map_objs->objs[i].data, 1, sizeof(map_object_t),
                 fp) != sizeof(map_object_t)) {
            debug(DBG_WARN, "Cannot read map cache: %s\n", strerror(errno));
            fclose(fp);
            return -4;
        }

        l->map_objs->objs[i].flags = 0;
    }

    if(fread(&cnt, 1, 4, fp) != 4) {
        debug(DBG_WARN, "Cannot read file \"%s\": %s\n", fn, strerror(errno));
        fclose(fp);
        return -5;
    }

    /* Reallocate the enemies array. */
    l->map_enemies->count = cnt = LE32(cnt);
    if(!(tmp = realloc(l->map_enemies->enemies, cnt * sizeof(game_enemy_t)))) {
        debug(DBG_WARN, "Cannot reallocate enemies array: %s\n",
              strerror(errno));
        fclose(fp);
        return -6;
    }

    l->map_enemies->enemies = (game_enemy_t *)tmp;

    /* Read the enemies in from the cache file. */
    if(fread(l->map_enemies->enemies, sizeof(game_enemy_t), cnt, fp) != cnt) {
        debug(DBG_WARN, "Cannot read map cache: %s\n", strerror(errno));
        fclose(fp);
        return -7;
    }

    /* Fixup Dark Falz' data for difficulties other than normal and the special
       Rappy data too... */
    for(i = 0; i < cnt; ++i) {
        if(l->map_enemies->enemies[i].bp_entry == 0x37 && l->difficulty) {
            l->map_enemies->enemies[i].bp_entry = 0x38;
        }
        else if(l->map_enemies->enemies[i].rt_index == (uint8_t)-1) {
            switch(l->event) {
                case LOBBY_EVENT_CHRISTMAS:
                    l->map_enemies->enemies[i].rt_index = 79;
                    break;
                case LOBBY_EVENT_EASTER:
                    l->map_enemies->enemies[i].rt_index = 81;
                    break;
                case LOBBY_EVENT_HALLOWEEN:
                    l->map_enemies->enemies[i].rt_index = 80;
                    break;
                default:
                    l->map_enemies->enemies[i].rt_index = 51;
                    break;
            }
        }
    }

    /* Find the quest since we need to check the enemies later for drops... */
    if(!(el = quest_lookup(&ship->qmap, qid))) {
        debug(DBG_WARN, "Cannot look up quest?!\n");
        fclose(fp);
        return -8;
    }

    /* Try to find a monster list associated with the quest. Basically, we look
       through each language of the quest we're loading for one that has the
       monster list set. Thus, you don't have to provide one for each and every
       language -- only one per version (other than PC) will do. */
    for(i = 0; i < CLIENT_LANG_COUNT; ++i, q = el->qptr[ver][i]) {
        if(!(q = el->qptr[ver][i]))
            continue;

        if(q->num_monster_ids || q->num_monster_types)
            break;
    }

    /* If we never found a monster list, then we don't care about it at all. */
    if(!q || (!q->num_monster_ids && !q->num_monster_types))
        goto done;

    /* Make a copy of the monster data from the quest. */
    l->num_mtypes = q->num_monster_types;
    if(!(l->mtypes = (qenemy_t *)malloc(sizeof(qenemy_t) * l->num_mtypes))) {
        debug(DBG_WARN, "Cannot allocate monster types: %s\n", strerror(errno));
        fclose(fp);
        l->num_mtypes = 0;
        return -9;
    }

    l->num_mids = q->num_monster_ids;
    if(!(l->mids = (qenemy_t *)malloc(sizeof(qenemy_t) * l->num_mids))) {
        debug(DBG_WARN, "Cannot allocate monster ids: %s\n", strerror(errno));
        fclose(fp);
        free(l->mtypes);
        l->mtypes = NULL;
        l->num_mtypes = 0;
        l->num_mids = 0;
        return -10;
    }

    memcpy(l->mtypes, q->monster_types, sizeof(qenemy_t) * l->num_mtypes);
    memcpy(l->mids, q->monster_ids, sizeof(qenemy_t) * l->num_mids);

done:
    /* Re-set the server drops flag and clean up. */
    l->flags |= LOBBY_FLAG_SERVER_DROPS;
    fclose(fp);

    return 0;
}
