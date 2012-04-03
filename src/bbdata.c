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
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sylverant/debug.h>
#include <sylverant/prs.h>

#include "bbdata.h"
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
static bb_parsed_map_t parsed_maps[2][3][0x10];

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

static int parse_map(bb_map_enemy_t *en, int en_ct, bb_game_enemies_t *game,
                     int ep, int alt) {
    int i, j;
    bb_game_enemy_t *gen;
    void *tmp;
    uint32_t count = 0;
    int acc;

    /* Allocate the space first */
    if(!(gen = (bb_game_enemy_t *)malloc(sizeof(bb_game_enemy_t) * 0xB50))) {
        debug(DBG_ERROR, "Cannot allocate enemies: %s\n", strerror(errno));
        return -1;
    }

    /* Clear it */
    memset(gen, 0, sizeof(bb_game_enemy_t) * 0xB50);

    /* Parse each enemy. */
    for(i = 0; i < en_ct; ++i) {
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
                            gen[count].rt_index = (uint16_t)-1;
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

                if(!en[i].num_clones)
                    en[i].num_clones = 4;
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

            case 0x00A1:    /* Chaos Sorcerer + 2 Bits */
                gen[count].bp_entry = 0x0A;
                gen[count].rt_index = 0x1E;
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

            case 0x00C8:    /* Dark Falz + 510 Helpers */
                gen[count].bp_entry = 0x37; /* Adjust for other difficulties */
                gen[count].rt_index = 0x2F;

                for(j = 0; j < 510; ++j) {
                    ++count;
                    gen[count].bp_entry = 0x35;
                }
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

                for(j = 1; j <= en[i].num_clones; ++j) {
                    gen[count + j].bp_entry = 0x42;
                    gen[count + j].rt_index = 0x44;
                }
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
        if(en[i].num_clones)
            count += en[i].num_clones;
        ++count;
    }

    /* Resize, so as not to waste space */
    if(!(tmp = realloc(gen, sizeof(bb_game_enemy_t) * count))) {
        debug(DBG_WARN, "Cannot resize enemies: %s\n", strerror(errno));
        tmp = gen;
    }

    game->enemies = (bb_game_enemy_t *)tmp;
    game->count = count;

    return 0;
}

static int read_map_set(int solo, int i, int j) {
    int srv;
    char fn[256];
    int k, l, nmaps, nvars;
    FILE *fp;
    long sz;
    bb_map_enemy_t *en;
    bb_game_enemies_t *tmp;

    if(!solo) {
        nmaps = maps[i][j << 1];
        nvars = maps[i][(j << 1) + 1];
    }
    else {
        nmaps = sp_maps[i][j << 1];
        nvars = sp_maps[i][(j << 1) + 1];
    }

    parsed_maps[solo][i][j].map_count = nmaps;
    parsed_maps[solo][i][j].variation_count = nvars;

    if(!(tmp = (bb_game_enemies_t *)malloc(sizeof(bb_game_enemies_t) *
                                           nmaps * nvars))) {
        debug(DBG_ERROR, "Cannot allocate for maps: %s\n",
              strerror(errno));
        return 10;
    }

    parsed_maps[solo][i][j].data = tmp;

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
                    debug(DBG_ERROR, "Cannot read map: %s\n",
                          strerror(errno));
                    return 2;
                }
            }

            debug(DBG_LOG, "Reading map %s\n", fn);

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
            if(!(en = (bb_map_enemy_t *)malloc(sz))) {
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
        }
    }

    return 0;
}

static int read_map_files(void) {
    int srv, i, j;

    for(i = 0; i < 3; ++i) {                            /* Episode */
        for(j = 0; j < 16 && j <= max_area[i]; ++j) {   /* Area */
            /* Read both the multi-player and single-player maps. */
            if((srv = read_map_set(0, i, j)))
                return srv;
            if((srv = read_map_set(1, i, j)))
                return srv;
        }
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
    rv = read_map_files();

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

void bb_free_params(void) {
    int i, j, k;
    uint32_t l, nmaps;
    bb_parsed_map_t *m;

    for(i = 0; i < 2; ++i) {
        for(j = 0; j < 3; ++j) {
            for(k = 0; k < 0x10; ++k) {
                m = &parsed_maps[i][j][k];
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

int bb_load_game_enemies(lobby_t *l) {
    bb_game_enemies_t *en;
    int solo = (l->flags & LOBBY_FLAG_SINGLEPLAYER) ? 1 : 0, i;
    uint32_t enemies = 0, index;
    bb_parsed_map_t *maps;
    bb_game_enemies_t *sets[0x10];

    /* Figure out the parameter set that will be in use first... */
    l->bb_params = battle_params[solo][l->episode - 1][l->difficulty];

    /* Figure out the total number of enemies that the game will have... */
    for(i = 0; i < 0x20; i += 2) {
        maps = &parsed_maps[solo][l->episode - 1][i >> 1];

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
        sets[i >> 1] = &maps->data[index];
    }

    /* Allocate space for the enemy set and the enemies therein. */
    if(!(en = (bb_game_enemies_t *)malloc(sizeof(bb_game_enemies_t)))) {
        debug(DBG_ERROR, "Error allocating enemy set: %s\n", strerror(errno));
        return -2;
    }

    if(!(en->enemies = (bb_game_enemy_t *)malloc(sizeof(bb_game_enemy_t) *
                                                 enemies))) {
        debug(DBG_ERROR, "Error allocating enemies: %s\n", strerror(errno));
        free(en);
        return -3;
    }

    en->count = enemies;
    index = 0;

    /* Copy in the enemy data. */
    for(i = 0; i < 0x10; ++i) {
        if(!sets[i])
            break;

        memcpy(&en->enemies[index], sets[i]->enemies,
               sizeof(bb_game_enemy_t) * sets[i]->count);
        index += sets[i]->count;
    }

    /* Fixup Dark Falz' data for difficulties other than normal and the special
       Rappy data too... */
    for(i = 0; i < en->count; ++i) {
        if(en->enemies[i].bp_entry == 0x37 && l->difficulty) {
            en->enemies[i].bp_entry = 0x38;
        }
        else if(en->enemies[i].rt_index == (uint16_t)-1) {
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
    l->bb_enemies = en;
    return 0;
}

void bb_free_game_enemies(lobby_t *l) {
    free(l->bb_enemies->enemies);
    free(l->bb_enemies);
    l->bb_enemies = NULL;
    l->bb_params = NULL;
}
