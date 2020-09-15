/*
    Sylverant Ship Server
    Copyright (C) 2011, 2013, 2014, 2015, 2017 Lawrence Sebald

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

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/stat.h>

#include <sylverant/debug.h>

#include <psoarchive/PRS.h>

#include "quests.h"
#include "clients.h"
#include "mapdata.h"
#include "ship.h"
#include "packets.h"

uint32_t quest_search_enemy_list(uint32_t id, qenemy_t *list, int len, int sd) {
    int i;
    uint32_t mask = sd ? SYLVERANT_QUEST_ENDROP_SDROPS :
        SYLVERANT_QUEST_ENDROP_CDROPS;

    for(i = 0; i < len; ++i) {
        if(list[i].key == id && (list[i].mask & mask))
            return list[i].value & 0xFF;
    }

    return 0xFFFFFFFF;
}

/* Find a quest by ID, if it exists */
quest_map_elem_t *quest_lookup(quest_map_t *map, uint32_t qid) {
    quest_map_elem_t *i;

    TAILQ_FOREACH(i, map, qentry) {
        if(qid == i->qid) {
            return i;
        }
    }

    return NULL;
}

/* Add a quest to the list */
quest_map_elem_t *quest_add(quest_map_t *map, uint32_t qid) {
    quest_map_elem_t *el;

    /* Create the element */
    el = (quest_map_elem_t *)malloc(sizeof(quest_map_elem_t));

    if(!el) {
        return NULL;
    }

    /* Clean it out, and fill in the id */
    memset(el, 0, sizeof(quest_map_elem_t));
    el->qid = qid;

    /* Add to the list */
    TAILQ_INSERT_TAIL(map, el, qentry);
    return el;
}

/* Clean the list out */
void quest_cleanup(quest_map_t *map) {
    quest_map_elem_t *tmp, *i;

    /* Remove all elements, freeing them as we go along */
    i = TAILQ_FIRST(map);
    while(i) {
        tmp = TAILQ_NEXT(i, qentry);

        free(i);
        i = tmp;
    }

    /* Reinit the map, just in case we reuse it */
    TAILQ_INIT(map);
}

/* Process an entire list of quests read in for a version/language combo. */
int quest_map(quest_map_t *map, sylverant_quest_list_t *list, int version,
              int language) {
    int i, j;
    sylverant_quest_category_t *cat;
    sylverant_quest_t *q;
    quest_map_elem_t *elem;

    if(version >= CLIENT_VERSION_COUNT || language >= CLIENT_LANG_COUNT) {
        return -1;
    }

    for(i = 0; i < list->cat_count; ++i) {
        cat = &list->cats[i];

        for(j = 0; j < cat->quest_count; ++j) {
            q = &cat->quests[j];

            /* Find the quest if we have it */
            elem = quest_lookup(map, q->qid);

            if(elem) {
                elem->qptr[version][language] = q;
            }
            else {
                elem = quest_add(map, q->qid);

                if(!elem) {
                    return -2;
                }

                elem->qptr[version][language] = q;
            }

            q->user_data = elem;
        }
    }

    return 0;
}

static uint32_t quest_cat_type(ship_t *s, int ver, int lang,
                               sylverant_quest_t *q) {
    int i, j;

    /* Look for it. */
    for(i = 0; i < s->qlist[ver][lang].cat_count; ++i) {
        for(j = 0; j < s->qlist[ver][lang].cats[i].quest_count; ++j) {
            if(q == &s->qlist[ver][lang].cats[i].quests[j])
                return s->qlist[ver][lang].cats[i].type;
        }
    }

    return 0;
}

static int check_cache_age(const char *fn1, const char *fn2) {
    struct stat s1, s2;

    if(stat(fn1, &s1))
        return -1;

    if(stat(fn2, &s2))
        /* Assume it doesn't exist */
        return 1;

    if(s1.st_mtime >= s2.st_mtime)
        /* The quest file is newer than the cache, regenerate it. */
        return 1;

    /* The cache file is newer than the quest, so we should be safe to use what
       we already have. */
    return 0;
}

static uint8_t *decompress_dat(uint8_t *inbuf, uint32_t insz, uint32_t *osz) {
    uint8_t *rv;
    int sz;

    if((sz = pso_prs_decompress_buf(inbuf, &rv, (size_t)insz)) < 0) {
        debug(DBG_WARN, "Cannot decompress data: %s\n", pso_strerror(sz));
        return NULL;
    }

    *osz = (uint32_t)sz;
    return rv;
}

static uint8_t *read_and_dec_dat(const char *fn, uint32_t *osz) {
    FILE *fp;
    off_t sz;
    uint8_t *buf, *rv;

    /* Read the file in. */
    if(!(fp = fopen(fn, "rb"))) {
        debug(DBG_WARN, "Cannot open quest file \"%s\": %s\n", fn,
              strerror(errno));
        return NULL;
    }

    fseeko(fp, 0, SEEK_END);
    sz = ftello(fp);
    fseeko(fp, 0, SEEK_SET);

    if(!(buf = (uint8_t *)malloc(sz))) {
        debug(DBG_WARN, "Cannot allocate memory to read dat: %s\n",
              strerror(errno));
        fclose(fp);
        return NULL;
    }

    if(fread(buf, 1, sz, fp) != sz) {
        debug(DBG_WARN, "Cannot read dat: %s\n", strerror(errno));
        free(buf);
        fclose(fp);
        return NULL;
    }

    fclose(fp);

    /* Return it decompressed. */
    rv = decompress_dat(buf, (uint32_t)sz, osz);
    free(buf);
    return rv;
}

static uint32_t qst_dat_size(const uint8_t *buf, int ver) {
    const dc_quest_file_pkt *dchdr = (const dc_quest_file_pkt *)buf;
    const pc_quest_file_pkt *pchdr = (const pc_quest_file_pkt *)buf;
    const bb_quest_file_pkt *bbhdr = (const bb_quest_file_pkt *)buf;
    char fn[32];
    char *ptr;

    /* Figure out the size of the .dat portion. */
    switch(ver) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            /* Check the first file to see if it is the dat. */
            strncpy(fn, dchdr->filename, 16);
            fn[16] = 0;

            if((ptr = strrchr(fn, '.')) && !strcmp(ptr, ".dat"))
                return LE32(dchdr->length);

            /* Try the second file in the qst */
            dchdr = (const dc_quest_file_pkt *)(buf + 0x3C);
            strncpy(fn, dchdr->filename, 16);
            fn[16] = 0;

            if((ptr = strrchr(fn, '.')) && !strcmp(ptr, ".dat"))
                return LE32(dchdr->length);

            /* Didn't find it, punt. */
            return 0;

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_PC:
            /* Check the first file to see if it is the dat. */
            strncpy(fn, pchdr->filename, 16);
            fn[16] = 0;

            if((ptr = strrchr(fn, '.')) && !strcmp(ptr, ".dat"))
                return LE32(pchdr->length);

            /* Try the second file in the qst */
            pchdr = (const pc_quest_file_pkt *)(buf + 0x3C);
            strncpy(fn, pchdr->filename, 16);
            fn[16] = 0;

            if((ptr = strrchr(fn, '.')) && !strcmp(ptr, ".dat"))
                return LE32(pchdr->length);

            /* Didn't find it, punt. */
            return 0;

        case CLIENT_VERSION_BB:
            /* Check the first file to see if it is the dat. */
            strncpy(fn, bbhdr->filename, 16);
            fn[16] = 0;

            if((ptr = strrchr(fn, '.')) && !strcmp(ptr, ".dat"))
                return LE32(bbhdr->length);

            /* Try the second file in the qst */
            bbhdr = (const bb_quest_file_pkt *)(buf + 0x58);
            strncpy(fn, bbhdr->filename, 16);
            fn[16] = 0;

            if((ptr = strrchr(fn, '.')) && !strcmp(ptr, ".dat"))
                return LE32(bbhdr->length);

            /* Didn't find it, punt. */
            return 0;
    }

    return 0;
}

static int copy_dc_qst_dat(const uint8_t *buf, uint8_t *rbuf, off_t sz,
                           uint32_t dsz) {
    const dc_quest_chunk_pkt *ck;
    uint32_t ptr = 120, optr = 0;
    char fn[32];
    char *cptr;
    uint32_t clen;

    while(ptr < sz) {
        ck = (const dc_quest_chunk_pkt *)(buf + ptr);

        /* Check the chunk for validity. */
        if(ck->hdr.dc.pkt_type != QUEST_CHUNK_TYPE ||
           ck->hdr.dc.pkt_len != LE16(0x0418)) {
            debug(DBG_WARN, "Unknown or damaged quest chunk!\n");
            return -1;
        }

        /* Grab the vitals... */
        strncpy(fn, ck->filename, 16);
        fn[16] = 0;
        clen = LE32(ck->length);
        cptr = strrchr(fn, '.');

        /* Sanity check... */
        if(clen > 1024 || !cptr) {
            debug(DBG_WARN, "Damaged quest chunk!\n");
            return -1;
        }

        /* See if this is part of the .dat file */
        if(!strcmp(cptr, ".dat")) {
            if(optr + clen > dsz) {
                debug(DBG_WARN, "Quest file appears to be corrupted!\n");
                return -1;
            }

            memcpy(rbuf + optr, ck->data, clen);
            optr += clen;
        }

        ptr += 0x0418;
    }

    if(optr != dsz) {
        debug(DBG_WARN, "Quest file appears to be corrupted!\n");
        return -1;
    }

    return 0;
}

static int copy_pc_qst_dat(const uint8_t *buf, uint8_t *rbuf, off_t sz,
                           uint32_t dsz) {
    const dc_quest_chunk_pkt *ck;
    uint32_t ptr = 120, optr = 0;
    char fn[32];
    char *cptr;
    uint32_t clen;

    while(ptr < sz) {
        ck = (const dc_quest_chunk_pkt *)(buf + ptr);

        /* Check the chunk for validity. */
        if(ck->hdr.pc.pkt_type != QUEST_CHUNK_TYPE ||
           ck->hdr.pc.pkt_len != LE16(0x0418)) {
            debug(DBG_WARN, "Unknown or damaged quest chunk!\n");
            return -1;
        }

        /* Grab the vitals... */
        strncpy(fn, ck->filename, 16);
        fn[16] = 0;
        clen = LE32(ck->length);
        cptr = strrchr(fn, '.');

        /* Sanity check... */
        if(clen > 1024 || !cptr) {
            debug(DBG_WARN, "Damaged quest chunk!\n");
            return -1;
        }

        /* See if this is part of the .dat file */
        if(!strcmp(cptr, ".dat")) {
            if(optr + clen > dsz) {
                debug(DBG_WARN, "Quest file appears to be corrupted!\n");
                return -1;
            }

            memcpy(rbuf + optr, ck->data, clen);
            optr += clen;
        }

        ptr += 0x0418;
    }

    if(optr != dsz) {
        debug(DBG_WARN, "Quest file appears to be corrupted!\n");
        return -1;
    }

    return 0;
}

static int copy_bb_qst_dat(const uint8_t *buf, uint8_t *rbuf, off_t sz,
                           uint32_t dsz) {
    const bb_quest_chunk_pkt *ck;
    uint32_t ptr = 176, optr = 0;
    char fn[32];
    char *cptr;
    uint32_t clen;

    while(ptr < sz) {
        ck = (const bb_quest_chunk_pkt *)(buf + ptr);

        /* Check the chunk for validity. */
        if(ck->hdr.pkt_type != LE16(QUEST_CHUNK_TYPE) ||
           ck->hdr.pkt_len != LE16(0x041C)) {
            debug(DBG_WARN, "Unknown or damaged quest chunk!\n");
            return -1;
        }

        /* Grab the vitals... */
        strncpy(fn, ck->filename, 16);
        fn[16] = 0;
        clen = LE32(ck->length);
        cptr = strrchr(fn, '.');

        /* Sanity check... */
        if(clen > 1024 || !cptr) {
            debug(DBG_WARN, "Damaged quest chunk!\n");
            return -1;
        }

        /* See if this is part of the .dat file */
        if(!strcmp(cptr, ".dat")) {
            if(optr + clen > dsz) {
                debug(DBG_WARN, "Quest file appears to be corrupted!\n");
                return -1;
            }

            memcpy(rbuf + optr, ck->data, clen);
            optr += clen;
        }

        ptr += 0x0420;
    }

    if(optr != dsz) {
        debug(DBG_WARN, "Quest file appears to be corrupted!\n");
        return -1;
    }

    return 0;
}

static uint8_t *read_and_dec_qst(const char *fn, uint32_t *osz, int ver) {
    FILE *fp;
    off_t sz;
    uint8_t *buf, *buf2, *rv;
    uint32_t dsz;

    /* Read the file in. */
    if(!(fp = fopen(fn, "rb"))) {
        debug(DBG_WARN, "Cannot open quest file \"%s\": %s\n", fn,
              strerror(errno));
        return NULL;
    }

    fseeko(fp, 0, SEEK_END);
    sz = ftello(fp);
    fseeko(fp, 0, SEEK_SET);

    /* Make sure the file's size is sane. */
    if(sz < 120) {
        debug(DBG_WARN, "Quest file \"%s\" too small\n", fn);
        fclose(fp);
        return NULL;
    }

    if(!(buf = (uint8_t *)malloc(sz))) {
        debug(DBG_WARN, "Cannot allocate memory to read qst: %s\n",
              strerror(errno));
        fclose(fp);
        return NULL;
    }

    if(fread(buf, 1, sz, fp) != sz) {
        debug(DBG_WARN, "Cannot read qst: %s\n", strerror(errno));
        free(buf);
        fclose(fp);
        return NULL;
    }

    fclose(fp);

    /* Figure out how big the .dat portion is. */
    if(!(dsz = qst_dat_size(buf, ver))) {
        debug(DBG_WARN, "Cannot find dat size in qst \"%s\"\n", fn);
        free(buf);
        return NULL;
    }

    /* Allocate space for it. */
    if(!(buf2 = (uint8_t *)malloc(dsz))) {
        debug(DBG_WARN, "Cannot allocate memory to decode qst: %s\n",
              strerror(errno));
        free(buf);
        return NULL;
    }

    /* Note, we'll never get PC quests in here, since we don't look at them. The
       primary thing this means is that PSOPC and DCv2 must have the same set of
       quests. */
    switch(ver) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_GC:
            if(copy_dc_qst_dat(buf, buf2, sz, dsz)) {
                debug(DBG_WARN, "Error decoding qst \"%s\", see above.\n", fn);
                free(buf2);
                free(buf);
                return NULL;
            }

            break;

        case CLIENT_VERSION_PC:
            if(copy_pc_qst_dat(buf, buf2, sz, dsz)) {
                debug(DBG_WARN, "Error decoding qst \"%s\", see above.\n", fn);
                free(buf2);
                free(buf);
                return NULL;
            }

            break;

        case CLIENT_VERSION_BB:
            if(copy_bb_qst_dat(buf, buf2, sz, dsz)) {
                debug(DBG_WARN, "Error decoding qst \"%s\", see above.\n", fn);
                free(buf2);
                free(buf);
                return NULL;
            }

            break;

        default:
            free(buf2);
            free(buf);
            return NULL;
    }

    /* We're done with the first buffer, so clean it up. */
    free(buf);

    /* Return the dat decompressed. */
    rv = decompress_dat(buf2, (uint32_t)dsz, osz);
    free(buf2);
    return rv;
}

/* Build/rebuild the quest enemy/object data cache. */
int quest_cache_maps(ship_t *s, quest_map_t *map, const char *dir) {
    quest_map_elem_t *i;
    size_t dlen = strlen(dir);
    char mdir[dlen + 20];
    char *fn1, *fn2;
    int j, k;
    sylverant_quest_t *q;
    const static char exts[2][4] = { "dat", "qst" };
    uint8_t *dat;
    uint32_t dat_sz, tmp;

    /* Make sure we have all the directories we'll need. */
    sprintf(mdir, "%s/.mapcache", dir);
    if(mkdir(mdir, 0755) && errno != EEXIST) {
        debug(DBG_ERROR, "Error creating map cache directory: %s\n",
              strerror(errno));
        return -1;
    }

    sprintf(mdir, "%s/.mapcache/v1", dir);
    if(mkdir(mdir, 0755) && errno != EEXIST) {
        debug(DBG_ERROR, "Error creating map cache directory: %s\n",
              strerror(errno));
        return -1;
    }

    sprintf(mdir, "%s/.mapcache/v2", dir);
    if(mkdir(mdir, 0755) && errno != EEXIST) {
        debug(DBG_ERROR, "Error creating map cache directory: %s\n",
              strerror(errno));
        return -1;
    }

    sprintf(mdir, "%s/.mapcache/gc", dir);
    if(mkdir(mdir, 0755) && errno != EEXIST) {
        debug(DBG_ERROR, "Error creating map cache directory: %s\n",
              strerror(errno));
        return -1;
    }

    sprintf(mdir, "%s/.mapcache/bb", dir);
    if(mkdir(mdir, 0755) && errno != EEXIST) {
        debug(DBG_ERROR, "Error creating map cache directory: %s\n",
              strerror(errno));
        return -1;
    }

    TAILQ_FOREACH(i, map, qentry) {
        /* Process it. */
        for(j = 0; j < CLIENT_VERSION_COUNT; ++j) {
            /* Skip PC, it is the same as v2. */
            if(j == CLIENT_VERSION_PC)
                continue;

            for(k = 0; k < CLIENT_LANG_COUNT; ++k) {
                if((q = i->qptr[j][k])) {
                    /* Don't bother with battle or challenge quests. */
                    tmp = quest_cat_type(s, j, k, q);
                    if(tmp & (SYLVERANT_QUEST_BATTLE |
                              SYLVERANT_QUEST_CHALLENGE))
                        break;

                    if(!(fn1 = (char *)malloc(dlen + 25 + strlen(q->prefix)))) {
                        debug(DBG_ERROR, "Error allocating memory: %s\n",
                              strerror(errno));
                        return -1;
                    }

                    if(!(fn2 = (char *)malloc(dlen + 35))) {
                        debug(DBG_ERROR, "Error allocating memory: %s\n",
                              strerror(errno));
                        free(fn1);
                        return -1;
                    }

                    sprintf(fn1, "%s/%s-%s/%s.%s", dir, version_codes[j],
                            language_codes[k], q->prefix, exts[q->format]);
                    sprintf(fn2, "%s/.mapcache/%s/%08x", dir, version_codes[j],
                            q->qid);

                    if(check_cache_age(fn1, fn2)) {
                        debug(DBG_LOG, "Cache for %s-%s %d needs updating!\n",
                              version_codes[j], language_codes[k], q->qid);
                    }

                    if(q->format == SYLVERANT_QUEST_BINDAT) {
                        if((dat = read_and_dec_dat(fn1, &dat_sz))) {
                            cache_quest_enemies(fn2, dat, dat_sz, q->episode);
                            free(dat);
                        }
                    }
                    else {
                        if((dat = read_and_dec_qst(fn1, &dat_sz, j))) {
                            cache_quest_enemies(fn2, dat, dat_sz, q->episode);
                            free(dat);
                        }
                    }

                    free(fn2);
                    free(fn1);

                    break;
                }
            }
        }
    }

    return 0;
}
