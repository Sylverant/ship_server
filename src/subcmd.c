/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2012, 2013, 2015, 2016, 2018, 2019,
                  2020 Lawrence Sebald

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
#include <iconv.h>
#include <string.h>
#include <pthread.h>

#include <sylverant/debug.h>
#include <sylverant/mtwist.h>
#include <sylverant/items.h>

#include "subcmd.h"
#include "clients.h"
#include "ship_packets.h"
#include "utils.h"
#include "items.h"
#include "word_select.h"
#include "scripts.h"
#include "shipgate.h"
#include "quest_functions.h"

/* Forward declarations */
static int subcmd_send_shop_inv(ship_client_t *c, subcmd_bb_shop_req_t *req);
static int subcmd_send_drop_stack(ship_client_t *c, uint32_t area, float x,
                                  float z, item_t *item);
static int subcmd_send_picked_up(ship_client_t *c, uint32_t item_data[3],
                                 uint32_t item_id, uint32_t item_data2,
                                 int send_to_client);
static int subcmd_send_destroy_map_item(ship_client_t *c, uint8_t area,
                                        uint32_t item_id);
static int subcmd_send_destroy_item(ship_client_t *c, uint32_t item_id,
                                    uint8_t amt);

#define SWAP32(x) (((x >> 24) & 0x00FF) | \
                   ((x >>  8) & 0xFF00) | \
                   ((x & 0xFF00) <<  8) | \
                   ((x & 0x00FF) << 24))

/* Handle a Guild card send packet. */
int handle_dc_gcsend(ship_client_t *s, ship_client_t *d,
                     subcmd_dc_gcsend_t *pkt) {
    /* This differs based on the destination client's version. */
    switch(d->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_pkt_dc(d, (dc_pkt_hdr_t *)pkt);

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
        {
            subcmd_gc_gcsend_t gc;

            memset(&gc, 0, sizeof(gc));

            /* Copy the name and text over. */
            memcpy(gc.name, pkt->name, 24);
            memcpy(gc.text, pkt->text, 88);

            /* Copy the rest over. */
            gc.hdr.pkt_type = pkt->hdr.pkt_type;
            gc.hdr.flags = pkt->hdr.flags;
            gc.hdr.pkt_len = LE16(0x0098);
            gc.type = pkt->type;
            gc.size = 0x25;
            gc.unused = 0;
            gc.tag = pkt->tag;
            gc.guildcard = pkt->guildcard;
            gc.padding = 0;
            gc.one = 1;
            gc.language = pkt->language;
            gc.section = pkt->section;
            gc.char_class = pkt->char_class;

            return send_pkt_dc(d, (dc_pkt_hdr_t *)&gc);
        }

        case CLIENT_VERSION_PC:
        {
            subcmd_pc_gcsend_t pc;
            size_t in, out;
            ICONV_CONST char *inptr;
            char *outptr;

            /* Don't allow guild cards to be sent to PC NTE, as it doesn't
               support them. */
            if((d->flags & CLIENT_FLAG_IS_NTE)) {
                if(s)
                    return send_txt(s, "%s", __(s, "\tE\tC7Cannot send Guild\n"
                                                   "Card to that user."));
                else
                    return 0;
            }

            memset(&pc, 0, sizeof(pc));

            /* Convert the name (ASCII -> UTF-16). */
            in = 24;
            out = 48;
            inptr = pkt->name;
            outptr = (char *)pc.name;
            iconv(ic_8859_to_utf16, &inptr, &in, &outptr, &out);

            /* Convert the text (ISO-8859-1 or SHIFT-JIS -> UTF-16). */
            in = 88;
            out = 176;
            inptr = pkt->text;
            outptr = (char *)pc.text;

            if(pkt->text[1] == 'J') {
                iconv(ic_sjis_to_utf16, &inptr, &in, &outptr, &out);
            }
            else {
                iconv(ic_8859_to_utf16, &inptr, &in, &outptr, &out);
            }

            /* Copy the rest over. */
            pc.hdr.pkt_type = pkt->hdr.pkt_type;
            pc.hdr.flags = pkt->hdr.flags;
            pc.hdr.pkt_len = LE16(0x00F8);
            pc.type = pkt->type;
            pc.size = 0x3D;
            pc.unused = 0;
            pc.tag = pkt->tag;
            pc.guildcard = pkt->guildcard;
            pc.padding = 0;
            pc.one = 1;
            pc.language = pkt->language;
            pc.section = pkt->section;
            pc.char_class = pkt->char_class;

            return send_pkt_dc(d, (dc_pkt_hdr_t *)&pc);
        }

        case CLIENT_VERSION_BB:
        {
            subcmd_bb_gcsend_t bb;
            size_t in, out;
            ICONV_CONST char *inptr;
            char *outptr;

            memset(&bb, 0, sizeof(subcmd_bb_gcsend_t));

            /* Convert the name (ASCII -> UTF-16). */
            bb.name[0] = LE16('\t');
            bb.name[1] = LE16('E');
            in = 24;
            out = 44;
            inptr = pkt->name;
            outptr = (char *)&bb.name[2];
            iconv(ic_8859_to_utf16, &inptr, &in, &outptr, &out);

            /* Convert the text (ISO-8859-1 or SHIFT-JIS -> UTF-16). */
            in = 88;
            out = 176;
            inptr = pkt->text;
            outptr = (char *)bb.text;

            if(pkt->text[1] == 'J') {
                iconv(ic_sjis_to_utf16, &inptr, &in, &outptr, &out);
            }
            else {
                iconv(ic_8859_to_utf16, &inptr, &in, &outptr, &out);
            }

            /* Copy the rest over. */
            bb.hdr.pkt_len = LE16(0x0114);
            bb.hdr.pkt_type = LE16(GAME_COMMAND2_TYPE);
            bb.hdr.flags = LE32(d->client_id);
            bb.type = SUBCMD_GUILDCARD;
            bb.size = 0x43;
            bb.guildcard = pkt->guildcard;
            bb.one = 1;
            bb.language = pkt->language;
            bb.section = pkt->section;
            bb.char_class = pkt->char_class;

            return send_pkt_bb(d, (bb_pkt_hdr_t *)&bb);
        }
    }

    return 0;
}

static int handle_pc_gcsend(ship_client_t *s, ship_client_t *d,
                            subcmd_pc_gcsend_t *pkt) {
    /* This differs based on the destination client's version. */
    switch(d->version) {
        case CLIENT_VERSION_PC:
            /* Don't allow guild cards to be sent to PC NTE, as it doesn't
               support them. */
            if((d->flags & CLIENT_FLAG_IS_NTE))
                return send_txt(s, "%s", __(s, "\tE\tC7Cannot send Guild\n"
                                               "Card to that user."));

            return send_pkt_dc(d, (dc_pkt_hdr_t *)pkt);

        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        {
            subcmd_dc_gcsend_t dc;
            size_t in, out;
            ICONV_CONST char *inptr;
            char *outptr;

            memset(&dc, 0, sizeof(dc));

            /* Convert the name (UTF-16 -> ASCII). */
            in = 48;
            out = 24;
            inptr = (char *)pkt->name;
            outptr = dc.name;
            iconv(ic_utf16_to_ascii, &inptr, &in, &outptr, &out);

            /* Convert the text (UTF-16 -> ISO-8859-1 or SHIFT-JIS). */
            in = 176;
            out = 88;
            inptr = (char *)pkt->text;
            outptr = dc.text;

            if(pkt->text[1] == LE16('J')) {
                iconv(ic_utf16_to_sjis, &inptr, &in, &outptr, &out);
            }
            else {
                iconv(ic_utf16_to_8859, &inptr, &in, &outptr, &out);
            }

            /* Copy the rest over. */
            dc.hdr.pkt_type = pkt->hdr.pkt_type;
            dc.hdr.flags = pkt->hdr.flags;
            dc.hdr.pkt_len = LE16(0x0088);
            dc.type = pkt->type;
            dc.size = 0x21;
            dc.unused = 0;
            dc.tag = pkt->tag;
            dc.guildcard = pkt->guildcard;
            dc.unused2 = 0;
            dc.one = 1;
            dc.language = pkt->language;
            dc.section = pkt->section;
            dc.char_class = pkt->char_class;
            dc.padding[0] = dc.padding[1] = dc.padding[2] = 0;

            return send_pkt_dc(d, (dc_pkt_hdr_t *)&dc);
        }

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
        {
            subcmd_gc_gcsend_t gc;
            size_t in, out;
            ICONV_CONST char *inptr;
            char *outptr;

            memset(&gc, 0, sizeof(gc));

            /* Convert the name (UTF-16 -> ASCII). */
            in = 48;
            out = 24;
            inptr = (char *)pkt->name;
            outptr = gc.name;
            iconv(ic_utf16_to_ascii, &inptr, &in, &outptr, &out);

            /* Convert the text (UTF-16 -> ISO-8859-1 or SHIFT-JIS). */
            in = 176;
            out = 88;
            inptr = (char *)pkt->text;
            outptr = gc.text;

            if(pkt->text[1] == LE16('J')) {
                iconv(ic_utf16_to_sjis, &inptr, &in, &outptr, &out);
            }
            else {
                iconv(ic_utf16_to_8859, &inptr, &in, &outptr, &out);
            }

            /* Copy the rest over. */
            gc.hdr.pkt_type = pkt->hdr.pkt_type;
            gc.hdr.flags = pkt->hdr.flags;
            gc.hdr.pkt_len = LE16(0x0098);
            gc.type = pkt->type;
            gc.size = 0x25;
            gc.unused = 0;
            gc.tag = pkt->tag;
            gc.guildcard = pkt->guildcard;
            gc.padding = 0;
            gc.one = 1;
            gc.language = pkt->language;
            gc.section = pkt->section;
            gc.char_class = pkt->char_class;

            return send_pkt_dc(d, (dc_pkt_hdr_t *)&gc);
        }

        case CLIENT_VERSION_BB:
        {
            subcmd_bb_gcsend_t bb;

            /* Fill in the packet... */
            memset(&bb, 0, sizeof(subcmd_bb_gcsend_t));
            bb.hdr.pkt_len = LE16(0x0114);
            bb.hdr.pkt_type = LE16(GAME_COMMAND2_TYPE);
            bb.hdr.flags = LE32(d->client_id);
            bb.type = SUBCMD_GUILDCARD;
            bb.size = 0x43;
            bb.guildcard = pkt->guildcard;
            bb.name[0] = LE16('\t');
            bb.name[1] = LE16('E');
            memcpy(&bb.name[2], pkt->name, 28);
            memcpy(bb.text, pkt->text, 176);
            bb.one = 1;
            bb.language = pkt->language;
            bb.section = pkt->section;
            bb.char_class = pkt->char_class;

            return send_pkt_bb(d, (bb_pkt_hdr_t *)&bb);
        }
    }

    return 0;
}

static int handle_gc_gcsend(ship_client_t *s, ship_client_t *d,
                            subcmd_gc_gcsend_t *pkt) {
    /* This differs based on the destination client's version. */
    switch(d->version) {
        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
            return send_pkt_dc(d, (dc_pkt_hdr_t *)pkt);

        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        {
            subcmd_dc_gcsend_t dc;

            memset(&dc, 0, sizeof(dc));

            /* Copy the name and text over. */
            memcpy(dc.name, pkt->name, 24);
            memcpy(dc.text, pkt->text, 88);

            /* Copy the rest over. */
            dc.hdr.pkt_type = pkt->hdr.pkt_type;
            dc.hdr.flags = pkt->hdr.flags;
            dc.hdr.pkt_len = LE16(0x0088);
            dc.type = pkt->type;
            dc.size = 0x21;
            dc.unused = 0;
            dc.tag = pkt->tag;
            dc.guildcard = pkt->guildcard;
            dc.unused2 = 0;
            dc.one = 1;
            dc.language = pkt->language;
            dc.section = pkt->section;
            dc.char_class = pkt->char_class;
            dc.padding[0] = dc.padding[1] = dc.padding[2] = 0;

            return send_pkt_dc(d, (dc_pkt_hdr_t *)&dc);
        }

        case CLIENT_VERSION_PC:
        {
            subcmd_pc_gcsend_t pc;
            size_t in, out;
            ICONV_CONST char *inptr;
            char *outptr;

            /* Don't allow guild cards to be sent to PC NTE, as it doesn't
               support them. */
            if((d->flags & CLIENT_FLAG_IS_NTE))
                return send_txt(s, "%s", __(s, "\tE\tC7Cannot send Guild\n"
                                               "Card to that user."));

            memset(&pc, 0, sizeof(pc));

            /* Convert the name (ASCII -> UTF-16). */
            in = 24;
            out = 48;
            inptr = pkt->name;
            outptr = (char *)pc.name;
            iconv(ic_8859_to_utf16, &inptr, &in, &outptr, &out);

            /* Convert the text (ISO-8859-1 or SHIFT-JIS -> UTF-16). */
            in = 88;
            out = 176;
            inptr = pkt->text;
            outptr = (char *)pc.text;

            if(pkt->text[1] == 'J') {
                iconv(ic_sjis_to_utf16, &inptr, &in, &outptr, &out);
            }
            else {
                iconv(ic_8859_to_utf16, &inptr, &in, &outptr, &out);
            }

            /* Copy the rest over. */
            pc.hdr.pkt_type = pkt->hdr.pkt_type;
            pc.hdr.flags = pkt->hdr.flags;
            pc.hdr.pkt_len = LE16(0x00F8);
            pc.type = pkt->type;
            pc.size = 0x3D;
            pc.unused = 0;
            pc.tag = pkt->tag;
            pc.guildcard = pkt->guildcard;
            pc.padding = 0;
            pc.one = 1;
            pc.language = pkt->language;
            pc.section = pkt->section;
            pc.char_class = pkt->char_class;

            return send_pkt_dc(d, (dc_pkt_hdr_t *)&pc);
        }

        case CLIENT_VERSION_BB:
        {
            subcmd_bb_gcsend_t bb;
            size_t in, out;
            ICONV_CONST char *inptr;
            char *outptr;

            memset(&bb, 0, sizeof(subcmd_bb_gcsend_t));

            /* Convert the name (ASCII -> UTF-16). */
            bb.name[0] = LE16('\t');
            bb.name[1] = LE16('E');
            in = 24;
            out = 44;
            inptr = pkt->name;
            outptr = (char *)&bb.name[2];
            iconv(ic_8859_to_utf16, &inptr, &in, &outptr, &out);

            /* Convert the text (ISO-8859-1 or SHIFT-JIS -> UTF-16). */
            in = 88;
            out = 176;
            inptr = pkt->text;
            outptr = (char *)bb.text;

            if(pkt->text[1] == 'J') {
                iconv(ic_sjis_to_utf16, &inptr, &in, &outptr, &out);
            }
            else {
                iconv(ic_8859_to_utf16, &inptr, &in, &outptr, &out);
            }

            /* Copy the rest over. */
            bb.hdr.pkt_len = LE16(0x0114);
            bb.hdr.pkt_type = LE16(GAME_COMMAND2_TYPE);
            bb.hdr.flags = LE32(d->client_id);
            bb.type = SUBCMD_GUILDCARD;
            bb.size = 0x43;
            bb.guildcard = pkt->guildcard;
            bb.one = 1;
            bb.language = pkt->language;
            bb.section = pkt->section;
            bb.char_class = pkt->char_class;

            return send_pkt_bb(d, (bb_pkt_hdr_t *)&bb);
        }
    }

    return 0;
}

static int handle_bb_gcsend(ship_client_t *s, ship_client_t *d) {
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;

    /* This differs based on the destination client's version. */
    switch(d->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        {
            subcmd_dc_gcsend_t dc;

            memset(&dc, 0, sizeof(dc));

            /* Convert the name (UTF-16 -> ASCII). */
            memset(&dc.name, '-', 16);
            in = 48;
            out = 24;
            inptr = (char *)&s->pl->bb.character.name[2];
            outptr = dc.name;
            iconv(ic_utf16_to_ascii, &inptr, &in, &outptr, &out);

            /* Convert the text (UTF-16 -> ISO-8859-1 or SHIFT-JIS). */
            in = 176;
            out = 88;
            inptr = (char *)s->bb_pl->guildcard_desc;
            outptr = dc.text;

            if(s->bb_pl->guildcard_desc[1] == LE16('J')) {
                iconv(ic_utf16_to_sjis, &inptr, &in, &outptr, &out);
            }
            else {
                iconv(ic_utf16_to_8859, &inptr, &in, &outptr, &out);
            }

            /* Copy the rest over. */
            dc.hdr.pkt_type = GAME_COMMAND2_TYPE;
            dc.hdr.flags = (uint8_t)d->client_id;
            dc.hdr.pkt_len = LE16(0x0088);
            dc.type = SUBCMD_GUILDCARD;
            dc.size = 0x21;
            dc.unused = 0;
            dc.tag = LE32(0x00010000);
            dc.guildcard = LE32(s->guildcard);
            dc.unused2 = 0;
            dc.one = 1;
            dc.language = s->language_code;
            dc.section = s->pl->bb.character.section;
            dc.char_class = s->pl->bb.character.ch_class;
            dc.padding[0] = dc.padding[1] = dc.padding[2] = 0;

            return send_pkt_dc(d, (dc_pkt_hdr_t *)&dc);
        }

        case CLIENT_VERSION_PC:
        {
            subcmd_pc_gcsend_t pc;

            /* Don't allow guild cards to be sent to PC NTE, as it doesn't
               support them. */
            if((d->flags & CLIENT_FLAG_IS_NTE))
                return send_txt(s, "%s", __(s, "\tE\tC7Cannot send Guild\n"
                                               "Card to that user."));

            memset(&pc, 0, sizeof(pc));

            /* First the name and text... */
            memcpy(pc.name, &s->pl->bb.character.name[2], 28);
            memcpy(pc.text, s->bb_pl->guildcard_desc, 176);

            /* Copy the rest over. */
            pc.hdr.pkt_type = GAME_COMMAND2_TYPE;
            pc.hdr.flags = (uint8_t)d->client_id;
            pc.hdr.pkt_len = LE16(0x00F8);
            pc.type = SUBCMD_GUILDCARD;
            pc.size = 0x3D;
            pc.unused = 0;
            pc.tag = LE32(0x00010000);
            pc.guildcard = LE32(s->guildcard);
            pc.padding = 0;
            pc.one = 1;
            pc.language = s->language_code;
            pc.section = s->pl->bb.character.section;
            pc.char_class = s->pl->bb.character.ch_class;

            return send_pkt_dc(d, (dc_pkt_hdr_t *)&pc);
        }

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
        {
            subcmd_gc_gcsend_t gc;

            memset(&gc, 0, sizeof(gc));

            /* Convert the name (UTF-16 -> ASCII). */
            memset(&gc.name, '-', 16);
            in = 48;
            out = 24;
            inptr = (char *)&s->pl->bb.character.name[2];
            outptr = gc.name;
            iconv(ic_utf16_to_ascii, &inptr, &in, &outptr, &out);

            /* Convert the text (UTF-16 -> ISO-8859-1 or SHIFT-JIS). */
            in = 176;
            out = 88;
            inptr = (char *)s->bb_pl->guildcard_desc;
            outptr = gc.text;

            if(s->bb_pl->guildcard_desc[1] == LE16('J')) {
                iconv(ic_utf16_to_sjis, &inptr, &in, &outptr, &out);
            }
            else {
                iconv(ic_utf16_to_8859, &inptr, &in, &outptr, &out);
            }

            /* Copy the rest over. */
            gc.hdr.pkt_type = GAME_COMMAND2_TYPE;
            gc.hdr.flags = (uint8_t)d->client_id;
            gc.hdr.pkt_len = LE16(0x0098);
            gc.type = SUBCMD_GUILDCARD;
            gc.size = 0x25;
            gc.unused = 0;
            gc.tag = LE32(0x00010000);
            gc.guildcard = LE32(s->guildcard);
            gc.padding = 0;
            gc.one = 1;
            gc.language = s->language_code;
            gc.section = s->pl->bb.character.section;
            gc.char_class = s->pl->bb.character.ch_class;

            return send_pkt_dc(d, (dc_pkt_hdr_t *)&gc);
        }

        case CLIENT_VERSION_BB:
        {
            subcmd_bb_gcsend_t bb;

            /* Fill in the packet... */
            memset(&bb, 0, sizeof(subcmd_bb_gcsend_t));
            bb.hdr.pkt_len = LE16(0x0114);
            bb.hdr.pkt_type = LE16(GAME_COMMAND2_TYPE);
            bb.hdr.flags = LE32(d->client_id);
            bb.type = SUBCMD_GUILDCARD;
            bb.size = 0x43;
            bb.guildcard = LE32(s->guildcard);
            memcpy(bb.name, s->pl->bb.character.name, 32);
            memcpy(bb.team_name, s->bb_opts->team_name, 32);
            memcpy(bb.text, s->bb_pl->guildcard_desc, 176);
            bb.one = 1;
            bb.language = s->language_code;
            bb.section = s->pl->bb.character.section;
            bb.char_class = s->pl->bb.character.ch_class;

            return send_pkt_bb(d, (bb_pkt_hdr_t *)&bb);
        }
    }

    return 0;
}

static int handle_gm_itemreq(ship_client_t *c, subcmd_itemreq_t *req) {
    subcmd_itemgen_t gen;
    int r = LE16(req->req);
    int i;
    lobby_t *l = c->cur_lobby;

    /* Fill in the packet we'll send out. */
    gen.hdr.pkt_type = GAME_COMMAND0_TYPE;
    gen.hdr.flags = 0;
    gen.hdr.pkt_len = LE16(0x30);
    gen.type = SUBCMD_ITEMDROP;
    gen.size = 0x0B;
    gen.unused = 0;
    gen.area = req->area;
    gen.what = 0x02;
    gen.req = req->req;
    gen.x = req->x;
    gen.y = req->y;
    gen.unk1 = LE32(0x00000010);

    gen.item[0] = LE32(c->next_item[0]);
    gen.item[1] = LE32(c->next_item[1]);
    gen.item[2] = LE32(c->next_item[2]);
    gen.item2[0] = LE32(c->next_item[3]);
    gen.item2[1] = LE32(0x00000002);

    /* Obviously not "right", but it works though, so we'll go with it. */
    gen.item_id = LE32((r | 0x06010100));

    /* Send the packet to every client in the lobby. */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i]) {
            send_pkt_dc(l->clients[i], (dc_pkt_hdr_t *)&gen);
        }
    }

    /* Clear this out. */
    c->next_item[0] = c->next_item[1] = c->next_item[2] = c->next_item[3] = 0;

    return 0;
}

static int handle_quest_itemreq(ship_client_t *c, subcmd_itemreq_t *req,
                                ship_client_t *dest) {
    uint32_t mid = LE16(req->req);
    uint32_t pti = req->pt_index;
    lobby_t *l = c->cur_lobby;
    uint32_t qdrop = 0xFFFFFFFF;

    if(pti != 0x30 && l->mids)
        qdrop = quest_search_enemy_list(mid, l->mids, l->num_mids, 0);
    if(qdrop == 0xFFFFFFFF && l->mtypes)
        qdrop = quest_search_enemy_list(pti, l->mtypes, l->num_mtypes, 0);

    /* If we found something, the version matters here. Basically, we only care
       about the none option on DC/PC, as rares do not drop in quests. On GC,
       we have to block drops on all options other than free, since we have no
       control over the drop once we send it to the leader. */
    if(qdrop != SYLVERANT_QUEST_ENDROP_FREE) {
        switch(l->version) {
            case CLIENT_VERSION_DCV1:
            case CLIENT_VERSION_DCV2:
            case CLIENT_VERSION_PC:
                if(qdrop == SYLVERANT_QUEST_ENDROP_NONE)
                    return 0;
                break;

            case CLIENT_VERSION_GC:
                return 0;
        }
    }

    /* If we haven't handled it, we're not supposed to, so send it on to the
       leader. */
    return send_pkt_dc(dest, (dc_pkt_hdr_t *)req);
}

static int handle_levelup(ship_client_t *c, subcmd_levelup_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->size != 0x05 || pkt->client_id != c->client_id) {
        return -1;
    }

    /* Copy over the new data to the client's character structure... */
    c->pl->v1.atp = pkt->atp;
    c->pl->v1.mst = pkt->mst;
    c->pl->v1.evp = pkt->evp;
    c->pl->v1.hp = pkt->hp;
    c->pl->v1.dfp = pkt->dfp;
    c->pl->v1.ata = pkt->ata;
    c->pl->v1.level = pkt->level;

    return subcmd_send_lobby_dc(c->cur_lobby, c, (subcmd_pkt_t *)pkt, 0);
}

static int handle_take_item(ship_client_t *c, subcmd_take_item_t *pkt) {
    lobby_t *l = c->cur_lobby;
    sylverant_iitem_t item;
    uint32_t v;
    int i;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT)
        return -1;

    /* Buggy PSO version is buggy... */
    if(c->version == CLIENT_VERSION_DCV1 && pkt->size == 0x06)
        pkt->size = 0x07;

    /* Sanity check... Make sure the size of the subcommand is valid, and
       disconnect the client if it isn't. */
    if(pkt->size != 0x07)
        return -1;

    /* If we have multiple clients in the team, make sure that the client id in
       the packet matches the user sending the packet.
       Note: We don't do this in single player teams because NPCs do weird
       things if you change their equipment in quests. */
    if(l->num_clients != 1 && pkt->client_id != c->client_id)
        return -1;

    /* Run the bank action script, if any. */
    if(script_execute(ScriptActionBankAction, c, SCRIPT_ARG_PTR, c,
                      SCRIPT_ARG_INT, 1, SCRIPT_ARG_UINT32, pkt->data_l[0],
                      SCRIPT_ARG_UINT32, pkt->data_l[1], SCRIPT_ARG_UINT32,
                      pkt->data_l[2], SCRIPT_ARG_UINT32, pkt->data2_l,
                      SCRIPT_ARG_UINT32, pkt->item_id, SCRIPT_ARG_END) < 0) {
        return -1;
    }

    /* If we're in legit mode, we need to check the newly taken item. */
    if((l->flags & LOBBY_FLAG_LEGIT_MODE) && l->limits_list) {
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
                return -1;
        }

        /* Fill in the item structure so we can check it. */
        memcpy(&item.data_l[0], &pkt->data_l[0], sizeof(uint32_t) * 5);

        if(!sylverant_limits_check_item(l->limits_list, &item, v)) {
            debug(DBG_LOG, "Potentially non-legit item in legit mode:\n"
                  "%08x %08x %08x %08x\n", LE32(pkt->data_l[0]),
                  LE32(pkt->data_l[1]), LE32(pkt->data_l[2]),
                  LE32(pkt->data2_l));

            /* The item failed the check, so kick the user. */
            send_message_box(c, "%s\n\n%s\n%s",
                             __(c, "\tEYou have been kicked from the server."),
                             __(c, "Reason:"),
                             __(c, "Attempt to remove a non-legit item from\n"
                                "the bank in a legit-mode game."));
            return -1;
        }
    }

    /* If we get here, either the game is not in legit mode, or the item is
       actually legit, so make a note of the ID, add it to the inventory and
       forward the packet on. */
    l->highest_item[c->client_id] = (uint16_t)LE32(pkt->item_id);

    if(!(c->flags & CLIENT_FLAG_TRACK_INVENTORY))
        goto send_pkt;

    v = LE32(pkt->data_l[0]);

    /* See if its a stackable item, since we have to treat them differently. */
    if(item_is_stackable(v)) {
        /* Its stackable, so see if we have any in the inventory already */
        for(i = 0; i < c->item_count; ++i) {
            /* Found it, add what we're adding in */
            if(c->items[i].data_l[0] == pkt->data_l[0]) {
                c->items[i].data_l[1] += pkt->data_l[1];
                goto send_pkt;
            }
        }
    }

    memcpy(&c->items[c->item_count++].data_l[0], &pkt->data_l[0],
           sizeof(uint32_t) * 5);

send_pkt:
    return subcmd_send_lobby_dc(c->cur_lobby, c, (subcmd_pkt_t *)pkt, 0);
}

static int handle_itemdrop(ship_client_t *c, subcmd_itemgen_t *pkt) {
    lobby_t *l = c->cur_lobby;
    sylverant_iitem_t item;
    uint32_t v;
    int i;
    ship_client_t *c2;
    const char *name;
    subcmd_destroy_item_t dp;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand matches with what we
       expect. Disconnect the client if not. We accept two different sizes here
       0x0B for v2 and later, and 0x0A for v1. */
    if(pkt->size != 0x0B && pkt->size != 0x0A) {
        return -1;
    }

    /* If we're in legit mode, we need to check the item. */
    if((l->flags & LOBBY_FLAG_LEGIT_MODE) && l->limits_list) {
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
                return -1;
        }

        /* Fill in the item structure so we can check it. */
        memcpy(&item.data_l[0], &pkt->item[0], 5 * sizeof(uint32_t));

        if(!sylverant_limits_check_item(l->limits_list, &item, v)) {
            /* The item failed the check, deal with it. */
            debug(DBG_LOG, "Potentially non-legit item dropped in legit mode:\n"
                  "%08x %08x %08x %08x\n", LE32(pkt->item[0]),
                  LE32(pkt->item[1]), LE32(pkt->item[2]), LE32(pkt->item2[0]));

            /* Grab the item name, if we can find it. */
            name = item_get_name((item_t *)&item, v);

            /* Fill in the destroy item packet. */
            memset(&dp, 0, sizeof(subcmd_destroy_item_t));
            dp.hdr.pkt_type = GAME_COMMAND0_TYPE;
            dp.hdr.pkt_len = LE16(0x0010);
            dp.type = SUBCMD_DESTROY_ITEM;
            dp.size = 0x03;
            dp.item_id = pkt->item_id;

            /* Send out a warning message, followed by the drop, followed by a
               packet deleting the drop from the game (to prevent any desync) */
            for(i = 0; i < l->max_clients; ++i) {
                if((c2 = l->clients[i])) {
                    if(name) {
                        send_txt(c2, "%s: %s",
                                 __(c2, "\tE\tC7Potentially hacked drop\n"
                                    "detected."), name);
                    }
                    else {
                        send_txt(c2, "%s",
                                 __(c2, "\tE\tC7Potentially hacked drop\n"
                                    "detected."));
                    }

                    /* Send out the drop item packet. This doesn't go to the
                       person who originated the drop (the team leader). */
                    if(c != c2) {
                        send_pkt_dc(c2, (dc_pkt_hdr_t *)pkt);
                    }

                    /* Send out the destroy drop packet. */
                    send_pkt_dc(c2, (dc_pkt_hdr_t *)&dp);
                }
            }

            /* We're done */
            return 0;
        }
    }

    /* If we get here, either the game is not in legit mode, or the item is
       actually legit, so just forward the packet on. */
    return subcmd_send_lobby_dc(c->cur_lobby, c, (subcmd_pkt_t *)pkt, 0);
}

static int handle_take_damage(ship_client_t *c, subcmd_take_damage_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return -1;
    }

    /* If we're in legit mode or the flag isn't set, then don't do anything. */
    if((l->flags & LOBBY_FLAG_LEGIT_MODE) ||
       !(c->flags & CLIENT_FLAG_INVULNERABLE)) {
        return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
    }

    /* This aught to do it... */
    subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
    return send_lobby_mod_stat(l, c, SUBCMD_STAT_HPUP, 2000);
}

static int handle_bb_take_damage(ship_client_t *c,
                                 subcmd_bb_take_damage_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return -1;
    }

    /* If we're in legit mode or the flag isn't set, then don't do anything. */
    if((l->flags & LOBBY_FLAG_LEGIT_MODE) ||
       !(c->flags & CLIENT_FLAG_INVULNERABLE)) {
        return subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 0);
    }

    /* This aught to do it... */
    subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 0);
    return send_lobby_mod_stat(l, c, SUBCMD_STAT_HPUP, 2000);
}

static int handle_used_tech(ship_client_t *c, subcmd_used_tech_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return -1;
    }

    /* If we're in legit mode or the flag isn't set, then don't do anything. */
    if((l->flags & LOBBY_FLAG_LEGIT_MODE) ||
       !(c->flags & CLIENT_FLAG_INFINITE_TP)) {
        return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
    }

    /* This aught to do it... */
    subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
    return send_lobby_mod_stat(l, c, SUBCMD_STAT_TPUP, 255);
}

static int handle_bb_used_tech(ship_client_t *c, subcmd_bb_used_tech_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return -1;
    }

    /* If we're in legit mode or the flag isn't set, then don't do anything. */
    if((l->flags & LOBBY_FLAG_LEGIT_MODE) ||
       !(c->flags & CLIENT_FLAG_INFINITE_TP)) {
        return subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 0);
    }

    /* This aught to do it... */
    subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 0);
    return send_lobby_mod_stat(l, c, SUBCMD_STAT_TPUP, 255);
}

static void update_qpos(ship_client_t *c, lobby_t *l) {
    uint8_t r;

    if((r = l->qpos_regs[c->client_id][0])) {
        send_sync_register(l->clients[0], r, (uint32_t)c->x);
        send_sync_register(l->clients[0], r + 1, (uint32_t)c->y);
        send_sync_register(l->clients[0], r + 2, (uint32_t)c->z);
        send_sync_register(l->clients[0], r + 3, (uint32_t)c->cur_area);
    }
    if((r = l->qpos_regs[c->client_id][1])) {
        send_sync_register(l->clients[1], r, (uint32_t)c->x);
        send_sync_register(l->clients[1], r + 1, (uint32_t)c->y);
        send_sync_register(l->clients[1], r + 2, (uint32_t)c->z);
        send_sync_register(l->clients[1], r + 3, (uint32_t)c->cur_area);
    }
    if((r = l->qpos_regs[c->client_id][2])) {
        send_sync_register(l->clients[2], r, (uint32_t)c->x);
        send_sync_register(l->clients[2], r + 1, (uint32_t)c->y);
        send_sync_register(l->clients[2], r + 2, (uint32_t)c->z);
        send_sync_register(l->clients[2], r + 3, (uint32_t)c->cur_area);
    }
    if((r = l->qpos_regs[c->client_id][3])) {
        send_sync_register(l->clients[3], r, (uint32_t)c->x);
        send_sync_register(l->clients[3], r + 1, (uint32_t)c->y);
        send_sync_register(l->clients[3], r + 2, (uint32_t)c->z);
        send_sync_register(l->clients[3], r + 3, (uint32_t)c->cur_area);
    }
}

static int handle_set_area(ship_client_t *c, subcmd_set_area_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Make sure the area is valid */
    if(pkt->area > 17) {
        return -1;
    }

    /* Save the new area and move along */
    if(c->client_id == pkt->client_id) {
        script_execute(ScriptActionChangeArea, c, SCRIPT_ARG_PTR, c,
                       SCRIPT_ARG_INT, (int)pkt->area, SCRIPT_ARG_INT,
                       c->cur_area, SCRIPT_ARG_END);
        c->cur_area = pkt->area;

        if((l->flags & LOBBY_FLAG_QUESTING))
            update_qpos(c, l);
    }

    return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
}

static int handle_bb_set_area(ship_client_t *c, subcmd_bb_set_area_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Make sure the area is valid */
    if(pkt->area > 17) {
        return -1;
    }

    /* Save the new area and move along */
    if(c->client_id == pkt->client_id) {
        script_execute(ScriptActionChangeArea, c, SCRIPT_ARG_PTR, c,
                       SCRIPT_ARG_INT, (int)pkt->area, SCRIPT_ARG_INT,
                       c->cur_area, SCRIPT_ARG_END);
        c->cur_area = pkt->area;

        if((l->flags & LOBBY_FLAG_QUESTING))
            update_qpos(c, l);
    }

    return subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 0);
}

static int handle_set_pos(ship_client_t *c, subcmd_set_pos_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Save the new position and move along */
    if(c->client_id == pkt->client_id) {
        c->w = pkt->w;
        c->x = pkt->x;
        c->y = pkt->y;
        c->z = pkt->z;

        if((l->flags & LOBBY_FLAG_QUESTING))
            update_qpos(c, l);
    }

    /* Clear this, in case we're at the lobby counter */
    c->last_info_req = 0;

    return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
}

static int handle_move(ship_client_t *c, subcmd_move_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Save the new position and move along */
    if(c->client_id == pkt->client_id) {
        c->x = pkt->x;
        c->z = pkt->z;

        if((l->flags & LOBBY_FLAG_QUESTING))
            update_qpos(c, l);
    }

    return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
}

static int handle_bb_set_pos(ship_client_t *c, subcmd_bb_set_pos_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Save the new position and move along */
    if(c->client_id == pkt->client_id) {
        c->w = pkt->w;
        c->x = pkt->x;
        c->y = pkt->y;
        c->z = pkt->z;

        if((l->flags & LOBBY_FLAG_QUESTING))
            update_qpos(c, l);
    }

    return subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 0);
}

static int handle_bb_move(ship_client_t *c, subcmd_bb_move_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Save the new position and move along */
    if(c->client_id == pkt->client_id) {
        c->x = pkt->x;
        c->z = pkt->z;

        if((l->flags & LOBBY_FLAG_QUESTING))
            update_qpos(c, l);
    }

    return subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 0);
}

static int handle_delete_inv(ship_client_t *c, subcmd_destroy_item_t *pkt) {
    lobby_t *l = c->cur_lobby;
    int num;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT)
        return -1;

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->size != 0x03)
        return -1;

    if(!(c->flags & CLIENT_FLAG_TRACK_INVENTORY))
        goto send_pkt;

    /* Ignore meseta */
    if(pkt->item_id != 0xFFFFFFFF) {
        /* Remove the item from the user's inventory */
        num = item_remove_from_inv(c->items, c->item_count, pkt->item_id,
                                   LE32(pkt->amount));
        if(num < 0) {
            debug(DBG_WARN, "Couldn't remove item from inventory!\n");
        }
        else {
            c->item_count -= num;
        }
    }

send_pkt:
    return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
}

static int handle_buy(ship_client_t *c, subcmd_buy_t *pkt) {
    lobby_t *l = c->cur_lobby;
    uint32_t ic;
    int i;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT)
        return -1;

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->size != 0x06 || pkt->client_id != c->client_id)
        return -1;

    /* Make a note of the item ID, and add to the inventory */
    l->highest_item[c->client_id] = LE32(pkt->item_id);

    if(!(c->flags & CLIENT_FLAG_TRACK_INVENTORY))
        goto send_pkt;

    ic = LE32(pkt->item[0]);

    /* See if its a stackable item, since we have to treat them differently. */
    if(item_is_stackable(ic)) {
        /* Its stackable, so see if we have any in the inventory already */
        for(i = 0; i < c->item_count; ++i) {
            /* Found it, add what we're adding in */
            if(c->items[i].data_l[0] == pkt->item[0]) {
                c->items[i].data_l[1] += pkt->item[1];
                goto send_pkt;
            }
        }
    }

    memcpy(&c->items[c->item_count].data_l[0], &pkt->item[0],
           sizeof(uint32_t) * 4);
    c->items[c->item_count++].data2_l = 0;

send_pkt:
    return subcmd_send_lobby_dc(c->cur_lobby, c, (subcmd_pkt_t *)pkt, 0);
}

static int handle_use_item(ship_client_t *c, subcmd_use_item_t *pkt) {
    lobby_t *l = c->cur_lobby;
    int num;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT)
        return -1;

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->size != 0x02)
        return -1;

    if(!(c->flags & CLIENT_FLAG_TRACK_INVENTORY))
        goto send_pkt;

    /* Remove the item from the user's inventory */
    num = item_remove_from_inv(c->items, c->item_count, pkt->item_id, 1);
    if(num < 0)
        debug(DBG_WARN, "Couldn't remove item from inventory!\n");
    else
        c->item_count -= num;

send_pkt:
    return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
}

static int handle_bb_drop_item(ship_client_t *c, subcmd_bb_drop_item_t *pkt) {
    lobby_t *l = c->cur_lobby;
    int found = -1, isframe;
    uint32_t i, inv;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " tried to drop item in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->hdr.pkt_len != LE16(0x20) || pkt->size != 0x06 ||
       pkt->client_id != c->client_id) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " sent bad item drop!\n",
              c->guildcard);
        return -1;
    }

    /* Look for the item in the user's inventory. */
    inv = c->bb_pl->inv.item_count;

    for(i = 0; i < inv; ++i) {
        if(c->bb_pl->inv.items[i].item_id == pkt->item_id) {
            found = i;

            /* If it is an equipped frame, we need to unequip all the units
               that are attached to it. */
            if(c->bb_pl->inv.items[i].data_b[0] == ITEM_TYPE_GUARD &&
               c->bb_pl->inv.items[i].data_b[1] == ITEM_SUBTYPE_FRAME &&
               (c->bb_pl->inv.items[i].flags & LE32(0x00000008))) {
                isframe = 1;
            }

            break;
        }
    }

    /* If the item isn't found, then punt the user from the ship. */
    if(found == -1) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " dropped invalid item id!\n",
              c->guildcard);
        return -1;
    }

    /* Clear the equipped flag. */
    c->bb_pl->inv.items[found].flags &= LE32(0xFFFFFFF7);

    /* Unequip any units, if the item was equipped and a frame. */
    if(isframe) {
        for(i = 0; i < inv; ++i) {
            if(c->bb_pl->inv.items[i].data_b[0] == ITEM_TYPE_GUARD &&
               c->bb_pl->inv.items[i].data_b[1] == ITEM_SUBTYPE_UNIT) {
                c->bb_pl->inv.items[i].flags &= LE32(0xFFFFFFF7);
            }
        }
    }

    /* We have the item... Add it to the lobby's inventory. */
    if(!lobby_add_item2_locked(l, &c->bb_pl->inv.items[found])) {
        /* *Gulp* The lobby is probably toast... At least make sure this user is
           still (mostly) safe... */
        debug(DBG_WARN, "Couldn't add item to lobby inventory!\n");
        return -1;
    }

    /* Remove the item from the user's inventory. */
    if(item_remove_from_inv(c->bb_pl->inv.items, c->bb_pl->inv.item_count,
                            pkt->item_id, 0xFFFFFFFF) < 1) {
        debug(DBG_WARN, "Couldn't remove item from client's inventory!\n");
        return -1;
    }

    --c->bb_pl->inv.item_count;

    /* Done. Send the packet on to the lobby. */
    return subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 0);
}

static int handle_bb_drop_pos(ship_client_t *c, subcmd_bb_drop_pos_t *pkt) {
    lobby_t *l = c->cur_lobby;
    int found = -1;
    uint32_t i, meseta, amt;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " set drop pos in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->hdr.pkt_len != LE16(0x20) || pkt->size != 0x06 ||
       pkt->client_id != c->client_id) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " sent bad drop pos!\n",
              c->guildcard);
        return -1;
    }

    /* Look for the item in the user's inventory. */
    if(pkt->item_id != 0xFFFFFFFF) {
        for(i = 0; i < c->bb_pl->inv.item_count; ++i) {
            if(c->bb_pl->inv.items[i].item_id == pkt->item_id) {
                found = i;
                break;
            }
        }

        /* If the item isn't found, then punt the user from the ship. */
        if(found == -1) {
            debug(DBG_WARN, "Guildcard %" PRIu32 " dropped invalid item id!\n",
                  c->guildcard);
            return -1;
        }
    }
    else {
        meseta = LE32(c->bb_pl->character.meseta);
        amt = LE32(pkt->amount);

        if(meseta < amt) {
            debug(DBG_WARN, "Guildcard %" PRIu32 " droppped too much money!\n",
                  c->guildcard);
            return -1;
        }
    }

    /* We have the item... Record the information for use with the subcommand
       0x29 that should follow. */
    c->drop_x = pkt->x;
    c->drop_z = pkt->z;
    c->drop_area = pkt->area;
    c->drop_item = pkt->item_id;
    c->drop_amt = pkt->amount;

    /* Done. Send the packet on to the lobby. */
    return subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 0);
}

static int handle_bb_drop_stack(ship_client_t *c,
                                subcmd_bb_destroy_item_t *pkt) {
    lobby_t *l = c->cur_lobby;
    int found = -1;
    uint32_t i, tmp, tmp2;
    item_t item_data;
    item_t *it;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " drop stack in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->hdr.pkt_len != LE16(0x14) || pkt->size != 0x03 ||
       pkt->client_id != c->client_id) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " sent bad drop stack!\n",
              c->guildcard);
        return -1;
    }

    if(pkt->item_id != 0xFFFFFFFF) {
        /* Look for the item in the user's inventory. */
        for(i = 0; i < c->bb_pl->inv.item_count; ++i) {
            if(c->bb_pl->inv.items[i].item_id == pkt->item_id) {
                found = i;
                break;
            }
        }

        /* If the item isn't found, then punt the user from the ship. */
        if(found == -1) {
            debug(DBG_WARN, "Guildcard %" PRIu32 " dropped invalid item "
                  "stack!\n", c->guildcard);
            return -1;
        }

        /* Grab the item from the client's inventory and set up the split */
        item_data = c->items[found];
        item_data.item_id = LE32((++l->highest_item[c->client_id]));
        item_data.data_b[5] = (uint8_t)(LE32(pkt->amount));
    }
    else {
        item_data.data_l[0] = LE32(Item_Meseta);
        item_data.data_l[1] = item_data.data_l[2] = 0;
        item_data.data2_l = pkt->amount;
        item_data.item_id = LE32((++l->highest_item[c->client_id]));
    }

    /* Make sure the item id and amount match the most recent 0xC3. */
    if(pkt->item_id != c->drop_item || pkt->amount != c->drop_amt) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " dropped different item stack!\n",
              c->guildcard);
        return -1;
    }

    /* We have the item... Add it to the lobby's inventory. */
    if(!(it = lobby_add_item2_locked(l, &item_data))) {
        /* *Gulp* The lobby is probably toast... At least make sure this user is
           still (mostly) safe... */
        debug(DBG_WARN, "Couldn't add item to lobby inventory!\n");
        return -1;
    }

    if(pkt->item_id != 0xFFFFFFFF) {
        /* Remove the item from the user's inventory. */
        found = item_remove_from_inv(c->bb_pl->inv.items,
                                     c->bb_pl->inv.item_count, pkt->item_id,
                                     LE32(pkt->amount));
        if(found < 0) {
            debug(DBG_WARN, "Couldn't remove item from client's inventory!\n");
            return -1;
        }

        c->bb_pl->inv.item_count -= found;
    }
    else {
        /* Remove the meseta from the character data */
        tmp = LE32(pkt->amount);
        tmp2 = LE32(c->bb_pl->character.meseta);

        if(tmp > tmp2) {
            debug(DBG_WARN, "Guildcard %" PRIu32 " dropping too much meseta\n",
                  c->guildcard);
            return -1;
        }

        c->bb_pl->character.meseta = LE32(tmp2 - tmp);
        c->pl->bb.character.meseta = c->bb_pl->character.meseta;
    }

    /* Now we have two packets to send on. First, send the one telling everyone
       that there's an item dropped. Then, send the one removing the item from
       the client's inventory. The first must go to everybody, the second to
       everybody except the person who sent this packet in the first place. */
    subcmd_send_drop_stack(c, c->drop_area, c->drop_x, c->drop_z, it);

    /* Done. Send the packet on to the lobby. */
    return subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 0);
}

static int handle_bb_pick_up(ship_client_t *c, subcmd_bb_pick_up_t *pkt) {
    lobby_t *l = c->cur_lobby;
    int found;
    uint32_t item, tmp;
    item_t item_data;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " picked up item in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->hdr.pkt_len != LE16(0x14) || pkt->size != 0x03 ||
       pkt->client_id != c->client_id) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " sent back pick up line!\n",
              c->guildcard);
        return -1;
    }

    /* Try to remove the item from the lobby... */
    found = lobby_remove_item_locked(l, pkt->item_id, &item_data);
    if(found < 0) {
        return -1;
    }
    else if(found > 0) {
        /* Assume someone else already picked it up, and just ignore it... */
        return 0;
    }
    else {
        item = LE32(item_data.data_l[0]);

        /* Is it meseta, or an item? */
        if(item == Item_Meseta) {
            tmp = LE32(item_data.data2_l) + LE32(c->bb_pl->character.meseta);

            /* Cap at 999,999 meseta. */
            if(tmp > 999999)
                tmp = 999999;

            c->bb_pl->character.meseta = LE32(tmp);
            c->pl->bb.character.meseta = c->bb_pl->character.meseta;
        }
        else {
            item_data.flags = 0;
            item_data.equipped = LE16(1);
            item_data.tech = 0;

            /* Add the item to the client's inventory. */
            found = item_add_to_inv(c->bb_pl->inv.items,
                                    c->bb_pl->inv.item_count, &item_data);

            if(found == -1) {
                debug(DBG_WARN, "Guildcard %" PRIu32 " has no item space!\n",
                      c->guildcard);
                return -1;
            }

            c->bb_pl->inv.item_count += found;
        }
    }

    /* Let everybody know that the client picked it up, and remove it from the
       view. */
    subcmd_send_picked_up(c, item_data.data_l, item_data.item_id,
                          item_data.data2_l, 1);

    return subcmd_send_destroy_map_item(c, pkt->area, item_data.item_id);
}

static int handle_word_select(ship_client_t *c, subcmd_word_select_t *pkt) {
    /* Don't send the message if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can chat."));
    }

    /* Don't send chats for a STFUed client. */
    if((c->flags & CLIENT_FLAG_STFU)) {
        return 0;
    }

    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return word_select_send_dc(c, pkt);

        case CLIENT_VERSION_PC:
            return word_select_send_pc(c, pkt);

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_EP3:
        case CLIENT_VERSION_BB:
            return word_select_send_gc(c, pkt);
    }

    return 0;
}

static int handle_bb_word_select(ship_client_t *c,
                                 subcmd_bb_word_select_t *pkt) {
    subcmd_word_select_t gc;

    /* Don't send the message if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can chat."));
    }

    /* Don't send chats for a STFUed client. */
    if((c->flags & CLIENT_FLAG_STFU)) {
        return 0;
    }

    memcpy(&gc, pkt, sizeof(subcmd_word_select_t) - sizeof(dc_pkt_hdr_t));
    gc.client_id_gc = pkt->client_id;
    gc.hdr.pkt_type = (uint8_t)(LE16(pkt->hdr.pkt_type));
    gc.hdr.flags = (uint8_t)pkt->hdr.flags;
    gc.hdr.pkt_len = LE16((LE16(pkt->hdr.pkt_len) - 4));

    return word_select_send_gc(c, &gc);
}

static int handle_symbol_chat(ship_client_t *c, subcmd_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Don't send the message if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can chat."));
    }

    /* Don't send chats for a STFUed client. */
    if((c->flags & CLIENT_FLAG_STFU)) {
        return 0;
    }

    return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 1);
}

static int handle_bb_symbol_chat(ship_client_t *c, bb_subcmd_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Don't send the message if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can chat."));
    }

    /* Don't send chats for a STFUed client. */
    if((c->flags & CLIENT_FLAG_STFU)) {
        return 0;
    }

    return subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 1);
}

static int handle_cmode_grave(ship_client_t *c, subcmd_pkt_t *pkt) {
    int i;
    lobby_t *l = c->cur_lobby;
    subcmd_pc_grave_t pc = { { 0 } };
    subcmd_dc_grave_t dc = { { 0 } };
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;

    /* Deal with converting the different versions... */
    switch(c->version) {
        case CLIENT_VERSION_DCV2:
            memcpy(&dc, pkt, sizeof(subcmd_dc_grave_t));

            /* Make a copy to send to PC players... */
            pc.hdr.pkt_type = GAME_COMMAND0_TYPE;
            pc.hdr.pkt_len = LE16(0x00E4);

            pc.type = SUBCMD_CMODE_GRAVE;
            pc.size = 0x38;
            pc.unused1 = dc.unused1;
            pc.client_id = dc.client_id;
            pc.unk0 = dc.unk0;
            pc.unk1 = dc.unk1;

            for(i = 0; i < 0x0C; ++i) {
                pc.string[i] = LE16(dc.string[i]);
            }

            memcpy(pc.unk2, dc.unk2, 0x24);
            pc.grave_unk4 = dc.grave_unk4;
            pc.deaths = dc.deaths;
            pc.coords_time[0] = dc.coords_time[0];
            pc.coords_time[1] = dc.coords_time[1];
            pc.coords_time[2] = dc.coords_time[2];
            pc.coords_time[3] = dc.coords_time[3];
            pc.coords_time[4] = dc.coords_time[4];

            /* Convert the team name */
            in = 20;
            out = 40;
            inptr = dc.team;
            outptr = (char *)pc.team;

            if(dc.team[1] == 'J') {
                iconv(ic_sjis_to_utf16, &inptr, &in, &outptr, &out);
            }
            else {
                iconv(ic_8859_to_utf16, &inptr, &in, &outptr, &out);
            }

            /* Convert the message */
            in = 24;
            out = 48;
            inptr = dc.message;
            outptr = (char *)pc.message;

            if(dc.message[1] == 'J') {
                iconv(ic_sjis_to_utf16, &inptr, &in, &outptr, &out);
            }
            else {
                iconv(ic_8859_to_utf16, &inptr, &in, &outptr, &out);
            }

            memcpy(pc.times, dc.times, 36);
            pc.unk = dc.unk;
            break;

        case CLIENT_VERSION_PC:
            memcpy(&pc, pkt, sizeof(subcmd_pc_grave_t));

            /* Make a copy to send to DC players... */
            dc.hdr.pkt_type = GAME_COMMAND0_TYPE;
            dc.hdr.pkt_len = LE16(0x00AC);

            dc.type = SUBCMD_CMODE_GRAVE;
            dc.size = 0x2A;
            dc.unused1 = pc.unused1;
            dc.client_id = pc.client_id;
            dc.unk0 = pc.unk0;
            dc.unk1 = pc.unk1;

            for(i = 0; i < 0x0C; ++i) {
                dc.string[i] = (char)LE16(dc.string[i]);
            }

            memcpy(dc.unk2, pc.unk2, 0x24);
            dc.grave_unk4 = pc.grave_unk4;
            dc.deaths = pc.deaths;
            dc.coords_time[0] = pc.coords_time[0];
            dc.coords_time[1] = pc.coords_time[1];
            dc.coords_time[2] = pc.coords_time[2];
            dc.coords_time[3] = pc.coords_time[3];
            dc.coords_time[4] = pc.coords_time[4];

            /* Convert the team name */
            in = 40;
            out = 20;
            inptr = (char *)pc.team;
            outptr = dc.team;

            if(pc.team[1] == LE16('J')) {
                iconv(ic_utf16_to_sjis, &inptr, &in, &outptr, &out);
            }
            else {
                iconv(ic_utf16_to_8859, &inptr, &in, &outptr, &out);
            }

            /* Convert the message */
            in = 48;
            out = 24;
            inptr = (char *)pc.message;
            outptr = dc.message;

            if(pc.message[1] == LE16('J')) {
                iconv(ic_utf16_to_sjis, &inptr, &in, &outptr, &out);
            }
            else {
                iconv(ic_utf16_to_8859, &inptr, &in, &outptr, &out);
            }

            memcpy(dc.times, pc.times, 36);
            dc.unk = pc.unk;
            break;

        default:
            return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
    }

    /* Send the packet to everyone in the lobby */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] && l->clients[i] != c) {
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV2:
                    send_pkt_dc(l->clients[i], (dc_pkt_hdr_t *)&dc);
                    break;

                case CLIENT_VERSION_PC:
                    send_pkt_dc(l->clients[i], (dc_pkt_hdr_t *)&pc);
                    break;
            }
        }
    }

    return 0;
}

static int handle_bb_bank(ship_client_t *c, subcmd_bb_bank_open_t *req) {
    lobby_t *l = c->cur_lobby;
    uint8_t *sendbuf = get_sendbuf();
    subcmd_bb_bank_inv_t *pkt = (subcmd_bb_bank_inv_t *)sendbuf;
    uint32_t num_items = LE32(c->bb_pl->bank.item_count);
    uint16_t size = sizeof(subcmd_bb_bank_inv_t) + num_items *
        sizeof(sylverant_bitem_t);
    block_t *b = c->cur_block;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " opened bank in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(req->hdr.pkt_len != LE16(0x10) || req->size != 0x02) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " sent bad bank open request!\n",
              c->guildcard);
        return -1;
    }

    /* Clean up the user's bank first... */
    cleanup_bb_bank(c);

    /* Fill in the packet. */
    pkt->hdr.pkt_len = LE16(size);
    pkt->hdr.pkt_type = LE16(GAME_COMMANDC_TYPE);
    pkt->hdr.flags = 0;
    pkt->type = SUBCMD_BANK_INV;
    pkt->unused[0] = pkt->unused[1] = pkt->unused[2] = 0;
    pkt->size = LE32(size);
    pkt->checksum = mt19937_genrand_int32(&b->rng); /* Client doesn't care */
    memcpy(&pkt->item_count, &c->bb_pl->bank, sizeof(sylverant_bank_t));

    return crypt_send(c, (int)size, sendbuf);
}

static int handle_bb_bank_action(ship_client_t *c, subcmd_bb_bank_act_t *pkt) {
    lobby_t *l = c->cur_lobby;
    uint32_t item_id;
    uint32_t amt, bank, inv, i;
    int found = -1, stack, isframe = 0;
    item_t item;
    sylverant_bitem_t bitem;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " did bank action in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->hdr.pkt_len != LE16(0x18) || pkt->size != 0x04) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " sent bad bank action!\n",
              c->guildcard);
        return -1;
    }

    switch(pkt->action) {
        case SUBCMD_BANK_ACT_CLOSE:
        case SUBCMD_BANK_ACT_DONE:
            return 0;

        case SUBCMD_BANK_ACT_DEPOSIT:
            item_id = LE32(pkt->item_id);

            /* Are they depositing meseta or an item? */
            if(item_id == 0xFFFFFFFF) {
                amt = LE32(pkt->meseta_amount);
                inv = LE32(c->bb_pl->character.meseta);

                /* Make sure they aren't trying to do something naughty... */
                if(amt > inv) {
                    debug(DBG_WARN, "Guildcard %" PRIu32 " depositing more "
                          "meseta than they have!\n", c->guildcard);
                    return -1;
                }

                bank = LE32(c->bb_pl->bank.meseta);
                if(amt + bank > 999999) {
                    debug(DBG_WARN, "Guildcard %" PRIu32 " depositing too much "
                          "money at the bank!\n", c->guildcard);
                    return -1;
                }

                c->bb_pl->character.meseta = LE32((inv - amt));
                c->pl->bb.character.meseta = c->bb_pl->character.meseta;
                c->bb_pl->bank.meseta = LE32((bank + amt));

                /* No need to tell everyone else, I guess? */
                return 0;
            }
            else {
                /* Look for the item in the user's inventory. */
                inv = c->bb_pl->inv.item_count;
                for(i = 0; i < inv; ++i) {
                    if(c->bb_pl->inv.items[i].item_id == pkt->item_id) {
                        item = c->bb_pl->inv.items[i];
                        found = i;

                        /* If it is an equipped frame, we need to unequip all
                           the units that are attached to it. */
                        if(item.data_b[0] == ITEM_TYPE_GUARD &&
                           item.data_b[1] == ITEM_SUBTYPE_FRAME &&
                           (item.flags & LE32(0x00000008))) {
                            isframe = 1;
                        }

                        break;
                    }
                }

                /* If the item isn't found, then punt the user from the ship. */
                if(found == -1) {
                    debug(DBG_WARN, "Guildcard %" PRIu32 " banked item that "
                          "they do not have!\n", c->guildcard);
                    return -1;
                }

                stack = item_is_stackable(item.data_l[0]);

                if(!stack && pkt->item_amount > 1) {
                    debug(DBG_WARN, "Guildcard %" PRIu32 " banking multiple of "
                          "a non-stackable item!\n", c->guildcard);
                    return -1;
                }

                found = item_remove_from_inv(c->bb_pl->inv.items, inv,
                                             pkt->item_id, pkt->item_amount);
                if(found < 0 || found > 1) {
                    debug(DBG_WARN, "Error removing item from inventory for "
                          "banking!\n", c->guildcard);
                    return -1;
                }

                c->bb_pl->inv.item_count = (inv -= found);

                /* Fill in the bank item. */
                if(stack) {
                    item.data_b[5] = pkt->item_amount;
                    bitem.amount = LE16(pkt->item_amount);
                }
                else {
                    bitem.amount = LE16(1);
                }

                bitem.flags = LE16(1);
                bitem.data_l[0] = item.data_l[0];
                bitem.data_l[1] = item.data_l[1];
                bitem.data_l[2] = item.data_l[2];
                bitem.item_id = item.item_id;
                bitem.data2_l = item.data2_l;

                /* Unequip any units, if the item was equipped and a frame. */
                if(isframe) {
                    for(i = 0; i < inv; ++i) {
                        item = c->bb_pl->inv.items[i];
                        if(item.data_b[0] == ITEM_TYPE_GUARD &&
                           item.data_b[1] == ITEM_SUBTYPE_UNIT) {
                            c->bb_pl->inv.items[i].flags &= LE32(0xFFFFFFF7);
                        }
                    }
                }

                /* Deposit it! */
                if(item_deposit_to_bank(c, &bitem) < 0) {
                    debug(DBG_WARN, "Error depositing to bank for guildcard %"
                          PRIu32 "!\n", c->guildcard);
                    return -1;
                }

                return subcmd_send_destroy_item(c, item.item_id,
                                                pkt->item_amount);
            }

        case SUBCMD_BANK_ACT_TAKE:
            item_id = LE32(pkt->item_id);

            /* Are they taking meseta or an item? */
            if(item_id == 0xFFFFFFFF) {
                amt = LE32(pkt->meseta_amount);
                inv = LE32(c->bb_pl->character.meseta);

                /* Make sure they aren't trying to do something naughty... */
                if(amt + inv > 999999) {
                    debug(DBG_WARN, "Guildcard %" PRIu32 " taking too much "
                          "money out of bank!\n", c->guildcard);
                    return -1;
                }

                bank = LE32(c->bb_pl->bank.meseta);
                if(amt > bank) {
                    debug(DBG_WARN, "Guildcard %" PRIu32 " taking out more "
                          "money than they have in the bank!\n", c->guildcard);
                    return -1;
                }

                c->bb_pl->character.meseta = LE32((inv + amt));
                c->pl->bb.character.meseta = c->bb_pl->character.meseta;
                c->bb_pl->bank.meseta = LE32((bank - amt));

                /* No need to tell everyone else... */
                return 0;
            }
            else {
                /* Try to take the item out of the bank. */
                found = item_take_from_bank(c, pkt->item_id, pkt->item_amount,
                                            &bitem);
                if(found < 0) {
                    debug(DBG_WARN, "Guildcard %" PRIu32 " taking invalid item "
                          "from bank!\n", c->guildcard);
                    return -1;
                }

                /* Ok, we have the item... Convert the bank item to an inventory
                   one... */
                item.equipped = LE16(0x0001);
                item.tech = LE16(0x0000);
                item.flags = 0;
                item.data_l[0] = bitem.data_l[0];
                item.data_l[1] = bitem.data_l[1];
                item.data_l[2] = bitem.data_l[2];
                item.item_id = LE32(l->item_id);
                item.data2_l = bitem.data2_l;
                ++l->item_id;

                /* Time to add it to the inventory... */
                found = item_add_to_inv(c->bb_pl->inv.items,
                                        c->bb_pl->inv.item_count, &item);
                if(found < 0) {
                    /* Uh oh... Guess we should put it back in the bank... */
                    item_deposit_to_bank(c, &bitem);
                    return -1;
                }

                c->bb_pl->inv.item_count += found;

                /* Let everyone know about it. */
                return subcmd_send_picked_up(c, item.data_l, item.item_id,
                                             item.data2_l, 1);
            }

        default:
            debug(DBG_WARN, "Guildcard %" PRIu32 " sent unk bank action: %d!\n",
                  c->guildcard, pkt->action);
            print_packet((unsigned char *)pkt, 0x18);
            return -1;
    }
}

static int handle_bb_equip(ship_client_t *c, subcmd_bb_equip_t *pkt) {
    lobby_t *l = c->cur_lobby;
    uint32_t inv, i;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " eqipped in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->hdr.pkt_len != LE16(0x14) || pkt->size != 0x03 ||
       pkt->client_id != c->client_id) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " sent bad equip message!\n",
              c->guildcard);
        return -1;
    }

    /* Find the item and equip it. */
    inv = c->bb_pl->inv.item_count;
    for(i = 0; i < inv; ++i) {
        if(c->bb_pl->inv.items[i].item_id == pkt->item_id) {
            /* XXXX: Should really make sure we can equip it first... */
            c->bb_pl->inv.items[i].flags |= LE32(0x00000008);
            break;
        }
    }

    /* Did we find something to equip? */
    if(i >= inv) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " equipped unknown item!\n",
              c->guildcard);
        return -1;
    }

    /* Done, let everyone else know. */
    return subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 0);
}

static int handle_bb_unequip(ship_client_t *c, subcmd_bb_equip_t *pkt) {
    lobby_t *l = c->cur_lobby;
    uint32_t inv, i, isframe = 0;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " uneqipped in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->hdr.pkt_len != LE16(0x14) || pkt->size != 0x03 ||
       pkt->client_id != c->client_id) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " sent bad unequip message!\n",
              c->guildcard);
        return -1;
    }

    /* Find the item and remove the equip flag. */
    inv = c->bb_pl->inv.item_count;
    for(i = 0; i < inv; ++i) {
        if(c->bb_pl->inv.items[i].item_id == pkt->item_id) {
            c->bb_pl->inv.items[i].flags &= LE32(0xFFFFFFF7);

            /* If its a frame, we have to make sure to unequip any units that
               may be equipped as well. */
            if(c->bb_pl->inv.items[i].data_b[0] == ITEM_TYPE_GUARD &&
               c->bb_pl->inv.items[i].data_b[1] == ITEM_SUBTYPE_FRAME) {
                isframe = 1;
            }

            break;
        }
    }

    /* Did we find something to equip? */
    if(i >= inv) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " unequipped unknown item!\n",
              c->guildcard);
        return -1;
    }

    /* Clear any units if we unequipped a frame. */
    if(isframe) {
        for(i = 0; i < inv; ++i) {
            if(c->bb_pl->inv.items[i].data_b[0] == ITEM_TYPE_GUARD &&
               c->bb_pl->inv.items[i].data_b[1] == ITEM_SUBTYPE_UNIT) {
                c->bb_pl->inv.items[i].flags &= LE32(0xFFFFFFF7);
            }
        }
    }

    /* Done, let everyone else know. */
    return subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 0);
}

static int handle_bb_medic(ship_client_t *c, bb_subcmd_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " used medical center in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->hdr.pkt_len != LE16(0x0C) || pkt->size != 0x01 ||
       pkt->data[0] != c->client_id) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " sent bad medic message!\n",
              c->guildcard);
        return -1;
    }

    /* Subtract 10 meseta from the client. */
    c->bb_pl->character.meseta -= 10;
    c->pl->bb.character.meseta -= 10;

    /* Send it along to the rest of the lobby. */
    return subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 0);
}

static int handle_bb_sort_inv(ship_client_t *c, subcmd_bb_sort_inv_t *pkt) {
    lobby_t *l = c->cur_lobby;
    sylverant_inventory_t inv;
    int i, j;
    int item_used[30] = { 0 };

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " sorted inventory in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand matches with what we
       expect. Disconnect the client if not. */
    if(pkt->hdr.pkt_len != LE16(0x84) || pkt->size != 0x1F) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " sent bad sort message!\n",
              c->guildcard);
        return -1;
    }

    /* Copy over the beginning of the inventory... */
    inv.item_count = c->bb_pl->inv.item_count;
    inv.hpmats_used = c->bb_pl->inv.hpmats_used;
    inv.tpmats_used = c->bb_pl->inv.tpmats_used;
    inv.language = c->bb_pl->inv.language;

    /* Copy over each item as its in the sorted list. */
    for(i = 0; i < 30; ++i) {
        /* Have we reached the end of the list? */
        if(pkt->item_ids[i] == 0xFFFFFFFF)
            break;

        /* Look for the item in question. */
        for(j = 0; j < inv.item_count; ++j) {
            if(c->bb_pl->inv.items[j].item_id == pkt->item_ids[i]) {
                /* Make sure they haven't used this one yet. */
                if(item_used[j]) {
                    debug(DBG_WARN, "Guildcard %" PRIu32 " listed item twice "
                          "in inventory sort!\n", c->guildcard);
                    return -1;
                }

                /* Copy the item and set it as used in the list. */
                memcpy(&inv.items[i], &c->bb_pl->inv.items[j], sizeof(item_t));
                item_used[j] = 1;
                break;
            }
        }

        /* Make sure the item got sorted in right. */
        if(j >= inv.item_count) {
            debug(DBG_WARN, "Guildcard %" PRIu32 " sorted unknown item!\n",
                  c->guildcard);
            return -1;
        }
    }

    /* Make sure we got everything... */
    if(i != inv.item_count) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " forgot item in sort!\n",
              c->guildcard);
        return -1;
    }

    /* We're good, so copy the inventory into the client's data. */
    memcpy(&c->bb_pl->inv, &inv, sizeof(sylverant_inventory_t));
    memcpy(&c->pl->bb.inv, &inv, sizeof(sylverant_inventory_t));

    /* Nobody else really needs to care about this one... */
    return 0;
}

/* XXXX: We need to handle the b0rked nature of the Gamecube packet for this one
   still (for more than just kill tracking). */
static int handle_mhit(ship_client_t *c, subcmd_mhit_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;
    uint16_t mid;
    game_enemy_t *en;
    uint32_t flags;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guild card %" PRIu32 " hit monster in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand matches with what we
       expect. Disconnect the client if not. */
    if(pkt->hdr.pkt_len != LE16(0x0010) || pkt->size != 0x03) {
        debug(DBG_WARN, "Guild card %" PRIu32 " sent bad mhit message!\n",
              c->guildcard);
        return -1;
    }

    /* Grab relevant information from the packet */
    mid = LE16(pkt->enemy_id);
    flags = LE32(pkt->flags);

    /* Swap the flags on the packet if the user is on GC... Looks like Sega
       decided that it should be the opposite order as it is on DC/PC/BB. */
    if(c->version == CLIENT_VERSION_GC)
        flags = SWAP32(flags);

    /* Bail out now if we don't have any enemy data on the team. */
    if(!l->map_enemies) {
        script_execute(ScriptActionEnemyHit, c, SCRIPT_ARG_PTR, c,
                       SCRIPT_ARG_UINT16, mid, SCRIPT_ARG_END);

        if(flags & 0x00000800)
            script_execute(ScriptActionEnemyKill, c, SCRIPT_ARG_PTR, c,
                           SCRIPT_ARG_UINT16, mid, SCRIPT_ARG_END);

        return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
    }

    /* Make sure the enemy is in range. */
    if(mid > l->map_enemies->count) {
#ifdef DEBUG
        debug(DBG_WARN, "Guild card %" PRIu32 " hit invalid enemy (%d -- max: "
              "%d)!\n"
              "Episode: %d, Floor: %d, Map: (%d, %d)\n", c->guildcard, mid,
              l->map_enemies->count, l->episode, c->cur_area,
              l->maps[c->cur_area << 1], l->maps[(c->cur_area << 1) + 1]);

        if((l->flags & LOBBY_FLAG_QUESTING))
            debug(DBG_WARN, "Quest ID: %d, Version: %d\n", l->qid, l->version);
#endif

        if(l->logfp) {
            fdebug(l->logfp, DBG_WARN, "Guild card %" PRIu32 " hit invalid "
                   "enemy (%d -- max: %d)!\n"
                   "Episode: %d, Floor: %d, Map: (%d, %d)\n", c->guildcard, mid,
                   l->map_enemies->count, l->episode, c->cur_area,
                   l->maps[c->cur_area << 1], l->maps[(c->cur_area << 1) + 1]);

            if((l->flags & LOBBY_FLAG_QUESTING))
                fdebug(l->logfp, DBG_WARN, "Quest ID: %d, Version: %d\n",
                       l->qid, l->version);
        }

        script_execute(ScriptActionEnemyHit, c, SCRIPT_ARG_PTR, c,
                       SCRIPT_ARG_UINT16, mid, SCRIPT_ARG_END);

        if(flags & 0x00000800)
            script_execute(ScriptActionEnemyKill, c, SCRIPT_ARG_PTR, c,
                           SCRIPT_ARG_UINT16, mid, SCRIPT_ARG_END);

        /* If server-side drops aren't on, then just send it on and hope for the
           best. We've probably got a bug somewhere on our end anyway... */
        if(!(l->flags & LOBBY_FLAG_SERVER_DROPS))
            return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);

        return -1;
    }

    /* Make sure it looks like they're in the right area for this... */
    /* XXXX: There are some issues still with Episode 2, so only spit this out
       for now on Episode 1. */
#ifdef DEBUG
    if(c->cur_area != l->map_enemies->enemies[mid].area && l->episode == 1 &&
       !(l->flags & LOBBY_FLAG_QUESTING)) {
        debug(DBG_WARN, "Guild card %" PRIu32 " hit enemy in wrong area "
              "(%d -- max: %d)!\n Episode: %d, Area: %d, Enemy Area: %d "
              "Map: (%d, %d)\n", c->guildcard, mid, l->map_enemies->count,
              l->episode, c->cur_area, l->map_enemies->enemies[mid].area,
              l->maps[c->cur_area << 1], l->maps[(c->cur_area << 1) + 1]);
    }
#endif

    if(l->logfp && c->cur_area != l->map_enemies->enemies[mid].area &&
       !(l->flags & LOBBY_FLAG_QUESTING)) {
        fdebug(l->logfp, DBG_WARN, "Guild card %" PRIu32 " hit enemy in wrong "
               "area (%d -- max: %d)!\n Episode: %d, Area: %d, Enemy Area: %d "
               "Map: (%d, %d)\n", c->guildcard, mid, l->map_enemies->count,
               l->episode, c->cur_area, l->map_enemies->enemies[mid].area,
               l->maps[c->cur_area << 1], l->maps[(c->cur_area << 1) + 1]);
    }

    /* Make sure the person's allowed to be on this floor in the first place. */
    if((l->flags & LOBBY_FLAG_ONLY_ONE) && !(l->flags & LOBBY_FLAG_QUESTING)) {
        if(l->episode == 1) {
            switch(c->cur_area) {
                case 5:     /* Cave 3 */
                case 12:    /* De Rol Le */
                    debug(DBG_WARN, "Guild card %" PRIu32 " hit enemy in area "
                          "impossible to\nreach in a single-player team (%d)\n"
                          "Team Flags: %08" PRIx32 "\n",
                          c->guildcard, c->cur_area, l->flags);
                    break;
            }
        }
    }

    /* Save the hit, assuming the enemy isn't already dead. */
    en = &l->map_enemies->enemies[mid];
    if(!(en->clients_hit & 0x80)) {
        en->clients_hit |= (1 << c->client_id);
        en->last_client = c->client_id;

        script_execute(ScriptActionEnemyHit, c, SCRIPT_ARG_PTR, c,
                       SCRIPT_ARG_UINT16, mid, SCRIPT_ARG_UINT32, en->bp_entry,
                       SCRIPT_ARG_UINT8, en->rt_index, SCRIPT_ARG_UINT8,
                       en->clients_hit, SCRIPT_ARG_END);

        /* If the kill flag is set, mark it as dead and update the client's
           counter. */
        if(flags & 0x00000800) {
            en->clients_hit |= 0x80;

            script_execute(ScriptActionEnemyKill, c, SCRIPT_ARG_PTR, c,
                           SCRIPT_ARG_UINT16, mid, SCRIPT_ARG_UINT32,
                           en->bp_entry, SCRIPT_ARG_UINT8, en->rt_index,
                           SCRIPT_ARG_UINT8, en->clients_hit, SCRIPT_ARG_END);

            if(en->bp_entry < 0x60 && !(l->flags & LOBBY_FLAG_HAS_NPC))
                ++c->enemy_kills[en->bp_entry];
        }
    }

    return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
}

static int handle_bb_mhit(ship_client_t *c, subcmd_bb_mhit_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;
    uint16_t mid;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " hit monster in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand matches with what we
       expect. Disconnect the client if not. */
    if(pkt->hdr.pkt_len != LE16(0x0014) || pkt->size != 0x03) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " sent bad mhit message!\n",
              c->guildcard);
        return -1;
    }

    /* Make sure the enemy is in range. */
    mid = LE16(pkt->enemy_id);
    if(mid > l->map_enemies->count) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " hit invalid enemy (%d -- max: "
              "%d)!\n", c->guildcard, mid, l->map_enemies->count);
        return -1;
    }

    /* Save the hit, assuming the enemy isn't already dead. */
    if(!(l->map_enemies->enemies[mid].clients_hit & 0x80)) {
        l->map_enemies->enemies[mid].clients_hit |= (1 << c->client_id);
        l->map_enemies->enemies[mid].last_client = c->client_id;
    }

    return subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 0);
}

static void handle_objhit_common(ship_client_t *c, lobby_t *l, uint16_t bid) {
    uint32_t obj_type;

    /* What type of object was hit? */
    if((bid & 0xF000) == 0x4000) {
        /* An object was hit */
        bid &= 0x0FFF;

        /* Make sure the object is in range. */
        if(bid > l->map_objs->count) {
            debug(DBG_WARN, "Guild card %" PRIu32 " hit invalid object "
                  "(%d -- max: %d)!\n"
                  "Episode: %d, Floor: %d, Map: (%d, %d)\n", c->guildcard,
                  bid, l->map_objs->count, l->episode, c->cur_area,
                  l->maps[c->cur_area << 1],
                  l->maps[(c->cur_area << 1) + 1]);

            if((l->flags & LOBBY_FLAG_QUESTING))
                debug(DBG_WARN, "Quest ID: %d, Version: %d\n", l->qid,
                      l->version);

            /* Just continue on and hope for the best. We've probably got a
               bug somewhere on our end anyway... */
            return;
        }

        /* Make sure it isn't marked as hit already. */
        if((l->map_objs->objs[bid].flags & 0x80000000))
            return;

        /* Now, see if we care about the type of the object that was hit. */
        obj_type = l->map_objs->objs[bid].data.skin & 0xFFFF;

        /* We'll probably want to do a bit more with this at some point, but
           for now this will do. */
        switch(obj_type) {
            case OBJ_SKIN_REG_BOX:
            case OBJ_SKIN_FIXED_BOX:
            case OBJ_SKIN_RUINS_REG_BOX:
            case OBJ_SKIN_RUINS_FIXED_BOX:
            case OBJ_SKIN_CCA_REG_BOX:
            case OBJ_SKIN_CCA_FIXED_BOX:
                /* Run the box broken script. */
                script_execute(ScriptActionBoxBreak, c, SCRIPT_ARG_PTR, c,
                               SCRIPT_ARG_UINT16, bid, SCRIPT_ARG_UINT16,
                               obj_type, SCRIPT_ARG_END);
                break;
        }

        /* Mark it as hit. */
        l->map_objs->objs[bid].flags |= 0x80000000;
    }
    else if((bid & 0xF000) == 0x1000) {
        /* An enemy was hit. We don't really do anything with these here,
           as there is a better packet for handling them. */
        return;
    }
    else if((bid & 0xF000) == 0x0000) {
        /* An ally was hit. We don't really care to do anything here. */
        return;
    }
    else {
        debug(DBG_LOG, "Unknown object type hit: %04" PRIx16 "\n", bid);
    }
}

static int handle_objhit_phys(ship_client_t *c, subcmd_objhit_phys_t *pkt) {
    lobby_t *l = c->cur_lobby;
    uint8_t i;

    /* We can't get these in lobbies without someone messing with something that
       they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guild card %" PRIu32 " hit object in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand matches with what we
       expect. Disconnect the client if not. */
    if(LE16(pkt->hdr.pkt_len) != (4 + (pkt->size << 2)) || pkt->size < 0x02) {
        debug(DBG_WARN, "Guild card %" PRIu32 " sent bad objhit message!\n",
              c->guildcard);
        print_packet((unsigned char *)pkt, LE16(pkt->hdr.pkt_len));
        return -1;
    }

    /* Check the type of the object that was hit. If there's no object data
       loaded here, we pretty much have to bail now. */
    if(!l->map_objs || !l->map_enemies)
        return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);

    /* Handle each thing that was hit */
    for(i = 0; i < pkt->size - 2; ++i) {
        handle_objhit_common(c, l, LE16(pkt->objects[i].obj_id));
    }

    /* We're not doing any more interesting parsing with this one for now, so
       just send it along. */
    return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
}

static inline int tlindex(uint8_t l) {
    switch(l) {
        case 0: case 1: case 2: case 3: case 4: return 0;
        case 5: case 6: case 7: case 8: case 9: return 1;
        case 10: case 11: case 12: case 13: case 14: return 2;
        case 15: case 16: case 17: case 18: case 19: return 3;
        case 20: case 21: case 22: case 23: case 25: return 4;
        default: return 5;
    }
}

#define BARTA_TIMING 1500
#define GIBARTA_TIMING 2200

static const uint16_t gifoie_timing[6] = { 5000, 6000, 7000, 8000, 9000, 10000 };
static const uint16_t gizonde_timing[6] = { 1000, 900, 700, 700, 700, 700 };
static const uint16_t rafoie_timing[6] = { 1500, 1400, 1300, 1200, 1100, 1100 };
static const uint16_t razonde_timing[6] = { 1200, 1100, 950, 800, 750, 750 };
static const uint16_t rabarta_timing[6] = { 1200, 1100, 950, 800, 750, 750 };

static int handle_objhit_tech(ship_client_t *c, subcmd_objhit_tech_t *pkt) {
    lobby_t *l = c->cur_lobby;
    uint8_t tech_level;

    /* We can't get these in lobbies without someone messing with something that
       they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guild card %" PRIu32 " hit object in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand matches with what we
       expect. Disconnect the client if not. */
    if(LE16(pkt->hdr.pkt_len) != (4 + (pkt->size << 2)) || pkt->size < 0x02) {
        debug(DBG_WARN, "Guild card %" PRIu32 " sent bad objhit message!\n",
              c->guildcard);
        print_packet((unsigned char *)pkt, LE16(pkt->hdr.pkt_len));
        return -1;
    }

    /* Sanity check... Does the character have that level of technique? */
    tech_level = c->pl->v1.techniques[pkt->tech];
    if(tech_level == 0xFF) {
        /* This might happen if the user learns a new tech in a team. Until we
           have real inventory tracking, we'll have to fudge this. Once we have
           real, full inventory tracking, this condition should probably
           disconnect the client */
        tech_level = pkt->level;
    }

    if(c->version >= CLIENT_VERSION_DCV2)
        tech_level += c->pl->v1.inv.items[pkt->tech].tech;

    if(tech_level < pkt->level) {
        /* This might happen if the user learns a new tech in a team. Until we
           have real inventory tracking, we'll have to fudge this. Once we have
           real, full inventory tracking, this condition should probably
           disconnect the client */
        tech_level = pkt->level;
    }

    /* Check the type of the object that was hit. If there's no object data
       loaded here, we pretty much have to bail now. */
    if(!l->map_objs || !l->map_enemies)
        return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);

    /* See what technique was used... */
    switch(pkt->tech) {
        /* These work just like physical hits and can only hit one target, so
           handle them the simple way... */
        case TECHNIQUE_FOIE:
        case TECHNIQUE_ZONDE:
        case TECHNIQUE_GRANTS:
            if(pkt->size == 3)
                handle_objhit_common(c, l, LE16(pkt->objects[0].obj_id));
            break;

        /* None of these can hit boxes (which is all we care about right now), so
           don't do anything special with them. */
        case TECHNIQUE_DEBAND:
        case TECHNIQUE_JELLEN:
        case TECHNIQUE_ZALURE:
        case TECHNIQUE_SHIFTA:
        case TECHNIQUE_RYUKER:
        case TECHNIQUE_RESTA:
        case TECHNIQUE_ANTI:
        case TECHNIQUE_REVERSER:
        case TECHNIQUE_MEGID:
            break;

        /* AoE spells are... special (why, Sega?). They never have more than one
           item hit in the packet, and just act in a broken manner in general. We
           have to do some annoying stuff to handle them here. */
        case TECHNIQUE_BARTA:
            c->aoe_timer = get_ms_time() + BARTA_TIMING;
            break;

        case TECHNIQUE_GIBARTA:
            c->aoe_timer = get_ms_time() + GIBARTA_TIMING;
            break;

        case TECHNIQUE_GIFOIE:
            c->aoe_timer = get_ms_time() + gifoie_timing[tlindex(tech_level)];
            break;

        case TECHNIQUE_RAFOIE:
            c->aoe_timer = get_ms_time() + rafoie_timing[tlindex(tech_level)];
            break;

        case TECHNIQUE_GIZONDE:
            c->aoe_timer = get_ms_time() + gizonde_timing[tlindex(tech_level)];
            break;

        case TECHNIQUE_RAZONDE:
            c->aoe_timer = get_ms_time() + razonde_timing[tlindex(tech_level)];
            break;

        case TECHNIQUE_RABARTA:
            c->aoe_timer = get_ms_time() + rabarta_timing[tlindex(tech_level)];
            break;

        default:
            debug(DBG_WARN, "Guildcard %" PRIu32 " used bad technique: %d\n",
                  c->guildcard, (int)pkt->tech);
            return -1;
    }

    /* We're not doing any more interesting parsing with this one for now, so
       just send it along. */
    return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
}

static int handle_objhit(ship_client_t *c, subcmd_bhit_pkt_t *pkt) {
    uint64_t now = get_ms_time();
    lobby_t *l = c->cur_lobby;

    /* We can't get these in lobbies without someone messing with something that
       they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guild card %" PRIu32 " hit object in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* We only care about these if the AoE timer is set on the sender. */
    if(c->aoe_timer < now)
        return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);

    /* Check the type of the object that was hit. As the AoE timer can't be set
       if the objects aren't loaded, this shouldn't ever trip up... */
    if(!l->map_objs || !l->map_enemies)
        return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);

    /* Handle the object marked as hit, if appropriate. */
    handle_objhit_common(c, l, LE16(pkt->box_id2));

    return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
}

static int handle_bb_req_exp(ship_client_t *c, subcmd_bb_req_exp_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;
    uint16_t mid;
    uint32_t bp, exp;
    game_enemy_t *en;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " requested exp in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand matches with what we
       expect. Disconnect the client if not. */
    if(pkt->hdr.pkt_len != LE16(0x0014) || pkt->size != 0x03) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " sent bad exp req message!\n",
              c->guildcard);
        return -1;
    }

    /* Make sure the enemy is in range. */
    mid = LE16(pkt->enemy_id);
    if(mid > l->map_enemies->count) {
        debug(DBG_WARN, "Guildcard %" PRIu32 " killed invalid enemy (%d -- "
              "max: %d)!\n", c->guildcard, mid, l->map_enemies->count);
        return -1;
    }

    /* Make sure this client actually hit the enemy and that the client didn't
       already claim their experience. */
    en = &l->map_enemies->enemies[mid];

    if(!(en->clients_hit & (1 << c->client_id))) {
        return 0;
    }

    /* Set that the client already got their experience and that the monster is
       indeed dead. */
    en->clients_hit = (en->clients_hit & (~(1 << c->client_id))) | 0x80;

    /* Give the client their experience! */
    bp = en->bp_entry;
    exp = l->bb_params[bp].exp;

    if(!pkt->last_hitter) {
        exp = (exp * 80) / 100;
    }

    return client_give_exp(c, exp);
}

static int handle_spawn_npc(ship_client_t *c, subcmd_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Attempt by GC %" PRIu32 " to spawn NPC in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* The only quests that allow NPCs to be loaded are those that require there
       to only be one player, so limit that here. Also, we only allow /npc in
       single-player teams, so that'll fall into line too. */
    if(l->num_clients > 1) {
        debug(DBG_WARN, "Attempt by GC %" PRIu32 " to spawn NPC in multi-"
              "player team!\n", c->guildcard);
        return -1;
    }

    /* Either this is a legitimate request to spawn a quest NPC, or the player
       is doing something stupid like trying to NOL himself. We don't care if
       someone is stupid enough to NOL themselves, so send the packet on now. */
    return subcmd_send_lobby_dc(l, c, pkt, 0);
}

static int handle_bb_spawn_npc(ship_client_t *c, bb_subcmd_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Attempt by GC %" PRIu32 " to spawn NPC in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* The only quests that allow NPCs to be loaded are those that require there
       to only be one player, so limit that here. Also, we only allow /npc in
       single-player teams, so that'll fall into line too. */
    if(l->num_clients > 1) {
        debug(DBG_WARN, "Attempt by GC %" PRIu32 " to spawn NPC in multi-"
              "player team!\n", c->guildcard);
        return -1;
    }

    /* Either this is a legitimate request to spawn a quest NPC, or the player
       is doing something stupid like trying to NOL himself. We don't care if
       someone is stupid enough to NOL themselves, so send the packet on now. */
    return subcmd_send_lobby_bb(l, c, pkt, 0);
}

static int handle_create_pipe(ship_client_t *c, subcmd_pipe_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        debug(DBG_WARN, "Attempt by GC %" PRIu32 " to spawn pipe in lobby!\n",
              c->guildcard);
        return -1;
    }

    /* See if the user is creating a pipe or destroying it. Destroying a pipe
       always matches the created pipe, but sets the area to 0. We could keep
       track of all of the pipe data, but that's probably overkill. For now,
       blindly accept any pipes where the area is 0. */
    if(pkt->area_id != 0) {
        /* Make sure the user is sending a pipe from the area he or she is
           currently in. */
        if(pkt->area_id != c->cur_area) {
            debug(DBG_WARN, "Attempt by GC %" PRIu32 " to spawn pipe to area "
                "he/she is not in (in: %d, pipe: %d).\n", c->guildcard,
                c->cur_area, (int)pkt->area_id);
            return -1;
        }
    }

    return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
}

static inline int reg_sync_index(lobby_t *l, uint8_t regnum) {
    int i;

    if(!(l->q_flags & LOBBY_QFLAG_SYNC_REGS))
        return -1;

    for(i = 0; i < l->num_syncregs; ++i) {
        if(regnum == l->syncregs[i])
            return i;
    }

    return -1;
}

static int handle_sync_reg(ship_client_t *c, subcmd_sync_reg_t *pkt) {
    lobby_t *l = c->cur_lobby;
    uint32_t val = LE32(pkt->value);
    int done = 0, idx;
    uint32_t ctl;

    /* XXXX: Probably should do some checking here... */
    /* Run the register sync script, if one is set. If the script returns
       non-zero, then assume that it has adequately handled the sync. */
    if((script_execute(ScriptActionQuestSyncRegister, c, SCRIPT_ARG_PTR, c,
                        SCRIPT_ARG_PTR, l, SCRIPT_ARG_UINT8, pkt->reg_num,
                        SCRIPT_ARG_UINT32, val, SCRIPT_ARG_END))) {
        done = 1;
    }

    /* Does this quest use global flags? If so, then deal with them... */
    if((l->q_flags & LOBBY_QFLAG_SHORT) && pkt->reg_num == l->q_shortflag_reg) {
        /* Check the control bits for sensibility... */
        ctl = (val >> 29) & 0x07;

        /* Make sure the error or response bits aren't set. */
        if((ctl & 0x06)) {
            debug(DBG_LOG, "Quest set flag register with illegal ctl!\n");
            send_sync_register(c, pkt->reg_num, 0x8000FFFE);
        }
        /* Make sure we don't have anything with any reserved ctl bits set
           (unless a script has already handled the sync). */
        else if((val & 0x17000000) && !done) {
            debug(DBG_LOG, "Quest set flag register with reserved ctl!\n");
            send_sync_register(c, pkt->reg_num, 0x8000FFFE);
        }
        else if((val & 0x08000000) && !done) {
            /* Delete the flag... */
            shipgate_send_qflag(&ship->sg, c, 1,
                                ((val >> 16) & 0xFF) | QFLAG_DELETE_FLAG,
                                c->cur_lobby->qid, 0);
        }
        else {
            /* Send the request to the shipgate... */
            shipgate_send_qflag(&ship->sg, c, ctl & 0x01, (val >> 16) & 0xFF,
                                c->cur_lobby->qid, val & 0xFFFF);
        }
        done = 1;
    }

    /* Does this quest use server data calls? If so, deal with it... */
    if((l->q_flags & LOBBY_QFLAG_DATA)) {
        if(pkt->reg_num == l->q_data_reg) {
            if(c->q_stack_top < CLIENT_MAX_QSTACK) {
                if(!(c->flags & CLIENT_FLAG_QSTACK_LOCK)) {
                    c->q_stack[c->q_stack_top++] = val;

                    /* Check if we've got everything we expected... */
                    if(c->q_stack_top >= 3 &&
                       c->q_stack_top == 3 + c->q_stack[1] + c->q_stack[2]) {
                        /* Call the function requested and reset the stack. */
                        ctl = quest_function_dispatch(c, l);

                        if(ctl != QUEST_FUNC_RET_NOT_YET) {
                            send_sync_register(c, pkt->reg_num, ctl);
                            c->q_stack_top = 0;
                        }
                    }
                }
                else {
                    /* The stack is locked, ignore the write and report the
                       error. */
                    send_sync_register(c, pkt->reg_num,
                                       QUEST_FUNC_RET_STACK_LOCKED);
                }
            }
            else if(c->q_stack_top == CLIENT_MAX_QSTACK) {
                /* Eat the stack push and report an error. */
                send_sync_register(c, pkt->reg_num,
                                   QUEST_FUNC_RET_STACK_OVERFLOW);
            }

            done = 1;
        }
        else if(pkt->reg_num == l->q_ctl_reg) {
            /* For now, the only reason we'll have one of these is to reset the
               stack. There might be other reasons later, but this will do, for
               the time being... */
            c->q_stack_top = 0;
            done = 1;
        }
    }

    /* Does this register have to be synced? */
    if((idx = reg_sync_index(l, pkt->reg_num)) != -1) {
        l->regvals[idx] = val;
    }

    if(!done)
        return subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);

    return 0;
}

static int handle_set_pos24(ship_client_t *c, subcmd_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Ugh... For some reason, v1 really likes to send these at the start of
       quests. And by "really likes to send these", I mean that everybody sends
       one of these for themselves and everybody else in the team... That can
       cause some interesting problems if clients are out of sync at the
       beginning of a quest for any reason (like a PSOPC player playing with
       a v1 player might be, for instance). Thus, we have to ignore these sent
       by v1 players with other players' client IDs at the beginning of a
       quest. */
    if(c->version == CLIENT_VERSION_DCV1) {
        /* Sanity check... */
        if(pkt->hdr.dc.pkt_len != LE16(0x0018) || pkt->size != 0x05) {
            debug(DBG_WARN, "Client %" PRIu32 " sent invalid setpos24!\n",
                  c->guildcard);
            return -1;
        }

        /* Oh look, misusing other portions of the client structure so that I
           don't have to make a new field... */
        if(c->autoreply_len && pkt->data[0] != c->client_id) {
            /* Silently drop the packet. */
            --c->autoreply_len;
            return 0;
        }
    }

    return subcmd_send_lobby_dc(l, c, pkt, 0);
}

/* Handle a 0x62/0x6D packet. */
int subcmd_handle_one(ship_client_t *c, subcmd_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;
    ship_client_t *dest;
    uint8_t type = pkt->type;
    int rv = -1;

    /* Ignore these if the client isn't in a lobby. */
    if(!l)
        return 0;

    pthread_mutex_lock(&l->mutex);

    /* Find the destination. */
    dest = l->clients[pkt->hdr.dc.flags];

    /* The destination is now offline, don't bother sending it. */
    if(!dest) {
        pthread_mutex_unlock(&l->mutex);
        return 0;
    }

    /* If there's a burst going on in the lobby, delay most packets */
    if(l->flags & LOBBY_FLAG_BURSTING) {
        rv = 0;

        switch(type) {
            case SUBCMD_BURST1:
            case SUBCMD_BURST2:
            case SUBCMD_BURST3:
            case SUBCMD_BURST4:
                if(l->flags & LOBBY_FLAG_QUESTING)
                    rv = lobby_enqueue_burst(l, c, (dc_pkt_hdr_t *)pkt);
                /* Fall through... */

            case SUBCMD_BURST5:
            case SUBCMD_BURST6:
            case SUBCMD_BURST7:
                rv |= send_pkt_dc(dest, (dc_pkt_hdr_t *)pkt);
                break;

            default:
                rv = lobby_enqueue_pkt(l, c, (dc_pkt_hdr_t *)pkt);
        }

        pthread_mutex_unlock(&l->mutex);
        return rv;
    }

    switch(type) {
        case SUBCMD_GUILDCARD:
            /* Make sure the recipient is not ignoring the sender... */
            if(client_has_ignored(dest, c->guildcard)) {
                rv = 0;
                break;
            }

            switch(c->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                    rv = handle_dc_gcsend(c, dest, (subcmd_dc_gcsend_t *)pkt);
                    break;

                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_EP3:
                    rv = handle_gc_gcsend(c, dest, (subcmd_gc_gcsend_t *)pkt);
                    break;

                case CLIENT_VERSION_PC:
                    rv = handle_pc_gcsend(c, dest, (subcmd_pc_gcsend_t *)pkt);
                    break;
            }
            break;

        case SUBCMD_ITEMREQ:
        case SUBCMD_BITEMREQ:
            /* There's only three ways we pay attention to this one: First, if
               the lobby is not in legit mode and a GM has used /item. Second,
               if the lobby has a drop function (for server-side drops). Third,
               if there is a quest going on with modified drops. */
            if(c->next_item[0] && !(l->flags & LOBBY_FLAG_LEGIT_MODE)) {
                rv = handle_gm_itemreq(c, (subcmd_itemreq_t *)pkt);
            }
            else if(l->dropfunc && (l->flags & LOBBY_FLAG_SERVER_DROPS)) {
                rv = l->dropfunc(c, l, pkt);
            }
            else if((l->num_mtypes || l->num_mids) &&
                    (l->flags & LOBBY_FLAG_QUESTING)) {
                rv = handle_quest_itemreq(c, (subcmd_itemreq_t *)pkt, dest);
            }
            else {
                rv = send_pkt_dc(dest, (dc_pkt_hdr_t *)pkt);
            }
            break;

        default:
#ifdef LOG_UNKNOWN_SUBS
            debug(DBG_LOG, "Unknown 0x62/0x6D: 0x%02X\n", type);
            print_packet((unsigned char *)pkt, LE16(pkt->hdr.dc.pkt_len));
#endif /* LOG_UNKNOWN_SUBS */
            /* Forward the packet unchanged to the destination. */
            rv = send_pkt_dc(dest, (dc_pkt_hdr_t *)pkt);
    }

    pthread_mutex_unlock(&l->mutex);
    return rv;
}

int subcmd_bb_handle_one(ship_client_t *c, bb_subcmd_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;
    ship_client_t *dest;
    uint8_t type = pkt->type;
    int rv = -1;
    uint32_t dnum = LE32(pkt->hdr.flags);

    /* Ignore these if the client isn't in a lobby. */
    if(!l) {
        return 0;
    }

    pthread_mutex_lock(&l->mutex);

    /* Find the destination. */
    dest = l->clients[dnum];

    /* The destination is now offline, don't bother sending it. */
    if(!dest) {
        pthread_mutex_unlock(&l->mutex);
        return 0;
    }

    switch(type) {
        case SUBCMD_GUILDCARD:
            /* Make sure the recipient is not ignoring the sender... */
            if(client_has_ignored(dest, c->guildcard)) {
                rv = 0;
                break;
            }

            rv = handle_bb_gcsend(c, dest);
            break;

        case SUBCMD_PICK_UP:
            rv = handle_bb_pick_up(c, (subcmd_bb_pick_up_t *)pkt);
            break;

        case SUBCMD_SHOPREQ:
            rv = subcmd_send_shop_inv(c, (subcmd_bb_shop_req_t *)pkt);
            break;

        case SUBCMD_OPEN_BANK:
            rv = handle_bb_bank(c, (subcmd_bb_bank_open_t *)pkt);
            break;

        case SUBCMD_BANK_ACTION:
            rv = handle_bb_bank_action(c, (subcmd_bb_bank_act_t *)pkt);
            break;

        case SUBCMD_ITEMREQ:
        case SUBCMD_BITEMREQ:
            /* Unlike earlier versions, we have to handle this here... */
            rv = l->dropfunc(c, l, pkt);
            break;

        default:
#ifdef BB_LOG_UNKNOWN_SUBS
            debug(DBG_LOG, "Unknown 0x62/0x6D: 0x%02X\n", type);
            print_packet((unsigned char *)pkt, LE16(pkt->hdr.pkt_len));
#endif /* BB_LOG_UNKNOWN_SUBS */
            /* Forward the packet unchanged to the destination. */
            rv = send_pkt_bb(dest, (bb_pkt_hdr_t *)pkt);
    }

    pthread_mutex_unlock(&l->mutex);
    return rv;
}

/* Handle a 0x60 packet. */
int subcmd_handle_bcast(ship_client_t *c, subcmd_pkt_t *pkt) {
    uint8_t type = pkt->type;
    lobby_t *l = c->cur_lobby;
    int rv, sent = 1, i;

    /* The DC NTE must be treated specially, so deal with that elsewhere... */
    if(c->version == CLIENT_VERSION_DCV1 && (c->flags & CLIENT_FLAG_IS_NTE))
        return subcmd_dcnte_handle_bcast(c, pkt);

    /* Ignore these if the client isn't in a lobby. */
    if(!l)
        return 0;

    pthread_mutex_lock(&l->mutex);

    /* If there's a burst going on in the lobby, delay most packets */
    if(l->flags & LOBBY_FLAG_BURSTING) {
        switch(type) {
            case SUBCMD_LOAD_3B:
            case SUBCMD_BURST_DONE:
                rv = subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);
                break;

            case SUBCMD_SET_AREA:
                rv = handle_set_area(c, (subcmd_set_area_t *)pkt);
                break;

            case SUBCMD_SET_POS_3F:
                rv = handle_set_pos(c, (subcmd_set_pos_t *)pkt);
                break;

            case SUBCMD_CMODE_GRAVE:
                rv = handle_cmode_grave(c, pkt);
                break;

            default:
                rv = lobby_enqueue_pkt(l, c, (dc_pkt_hdr_t *)pkt);
        }

        pthread_mutex_unlock(&l->mutex);
        return rv;
    }

    switch(type) {
        case SUBCMD_TAKE_ITEM:
            rv = handle_take_item(c, (subcmd_take_item_t *)pkt);
            break;

        case SUBCMD_LEVELUP:
            rv = handle_levelup(c, (subcmd_levelup_t *)pkt);
            break;

        case SUBCMD_USED_TECH:
            rv = handle_used_tech(c, (subcmd_used_tech_t *)pkt);
            break;

        case SUBCMD_TAKE_DAMAGE1:
        case SUBCMD_TAKE_DAMAGE2:
            rv = handle_take_damage(c, (subcmd_take_damage_t *)pkt);
            break;

        case SUBCMD_ITEMDROP:
            rv = handle_itemdrop(c, (subcmd_itemgen_t *)pkt);
            break;

        case SUBCMD_SET_AREA:
        case SUBCMD_SET_AREA_21:
            rv = handle_set_area(c, (subcmd_set_area_t *)pkt);
            break;

        case SUBCMD_SET_POS_3E:
        case SUBCMD_SET_POS_3F:
            rv = handle_set_pos(c, (subcmd_set_pos_t *)pkt);
            break;

        case SUBCMD_MOVE_SLOW:
        case SUBCMD_MOVE_FAST:
            rv = handle_move(c, (subcmd_move_t *)pkt);
            break;

        case SUBCMD_DELETE_ITEM:
            rv = handle_delete_inv(c, (subcmd_destroy_item_t *)pkt);
            break;

        case SUBCMD_BUY:
            rv = handle_buy(c, (subcmd_buy_t *)pkt);
            break;

        case SUBCMD_USE_ITEM:
            rv = handle_use_item(c, (subcmd_use_item_t *)pkt);
            break;

        case SUBCMD_WORD_SELECT:
            rv = handle_word_select(c, (subcmd_word_select_t *)pkt);
            break;

        case SUBCMD_SYMBOL_CHAT:
            rv = handle_symbol_chat(c, pkt);
            break;

        case SUBCMD_CMODE_GRAVE:
            rv = handle_cmode_grave(c, pkt);
            break;

        case SUBCMD_HIT_MONSTER:
            /* Don't even try to deal with these in battle or challenge mode. */
            if(l->challenge || l->battle) {
                sent = 0;
                break;
            }

            rv = handle_mhit(c, (subcmd_mhit_pkt_t *)pkt);
            break;

        case SUBCMD_OBJHIT_PHYS:
            /* Don't even try to deal with these in battle or challenge mode. */
            if(l->challenge || l->battle) {
                sent = 0;
                break;
            }

            rv = handle_objhit_phys(c, (subcmd_objhit_phys_t *)pkt);
            break;

        case SUBCMD_OBJHIT_TECH:
            /* Don't even try to deal with these in battle or challenge mode. */
            if(l->challenge || l->battle) {
                sent = 0;
                break;
            }

            rv = handle_objhit_tech(c, (subcmd_objhit_tech_t *)pkt);
            break;

        case SUBCMD_HIT_OBJ:
            /* Don't even try to deal with these in battle or challenge mode. */
            if(l->challenge || l->battle) {
                sent = 0;
                break;
            }

            rv = handle_objhit(c, (subcmd_bhit_pkt_t *)pkt);
            break;

        case SUBCMD_SPAWN_NPC:
            rv = handle_spawn_npc(c, pkt);
            break;

        case SUBCMD_CREATE_PIPE:
            rv = handle_create_pipe(c, (subcmd_pipe_pkt_t *)pkt);
            break;

        case SUBCMD_SYNC_REG:
            rv = handle_sync_reg(c, (subcmd_sync_reg_t *)pkt);
            break;

        case SUBCMD_SET_POS_24:
            rv = handle_set_pos24(c, pkt);
            break;

        default:
#ifdef LOG_UNKNOWN_SUBS
            debug(DBG_LOG, "Unknown 0x60: 0x%02X\n", type);
            print_packet((unsigned char *)pkt, LE16(pkt->hdr.dc.pkt_len));
#endif /* LOG_UNKNOWN_SUBS */
            sent = 0;
            break;

        case SUBCMD_FINISH_LOAD:
            if(l->type == LOBBY_TYPE_DEFAULT) {
                for(i = 0; i < l->max_clients; ++i) {
                    if(l->clients[i] && l->clients[i] != c &&
                       subcmd_send_pos(c, l->clients[i])) {
                        rv = -1;
                        break;
                    }
                }
            }

        case SUBCMD_LOAD_22:
        case SUBCMD_TALK_NPC:
        case SUBCMD_DONE_NPC:
        case SUBCMD_LOAD_3B:
        case SUBCMD_TALK_DESK:
        case SUBCMD_WARP_55:
        case SUBCMD_LOBBY_ACTION:
        case SUBCMD_GOGO_BALL:
        case SUBCMD_LOBBY_CHAIR:
        case SUBCMD_CHAIR_DIR:
        case SUBCMD_CHAIR_MOVE:
            sent = 0;
    }

    /* Broadcast anything we don't care to check anything about. */
    if(!sent)
        rv = subcmd_send_lobby_dc(l, c, (subcmd_pkt_t *)pkt, 0);

    pthread_mutex_unlock(&l->mutex);
    return rv;
}

int subcmd_bb_handle_bcast(ship_client_t *c, bb_subcmd_pkt_t *pkt) {
    uint8_t type = pkt->type;
    lobby_t *l = c->cur_lobby;
    int rv, sent = 1, i;

    /* Ignore these if the client isn't in a lobby. */
    if(!l)
        return 0;

    pthread_mutex_lock(&l->mutex);

    switch(type) {
        case SUBCMD_SYMBOL_CHAT:
            rv = handle_bb_symbol_chat(c, pkt);
            break;

        case SUBCMD_HIT_MONSTER:
            rv = handle_bb_mhit(c, (subcmd_bb_mhit_pkt_t *)pkt);
            break;

        case SUBCMD_SET_AREA:
        case SUBCMD_SET_AREA_21:
            rv = handle_bb_set_area(c, (subcmd_bb_set_area_t *)pkt);
            break;

        case SUBCMD_EQUIP:
            rv = handle_bb_equip(c, (subcmd_bb_equip_t *)pkt);
            break;

        case SUBCMD_REMOVE_EQUIP:
            rv = handle_bb_unequip(c, (subcmd_bb_equip_t *)pkt);
            break;

        case SUBCMD_DROP_ITEM:
            rv = handle_bb_drop_item(c, (subcmd_bb_drop_item_t *)pkt);
            break;

        case SUBCMD_SET_POS_3E:
        case SUBCMD_SET_POS_3F:
            rv = handle_bb_set_pos(c, (subcmd_bb_set_pos_t *)pkt);
            break;

        case SUBCMD_MOVE_SLOW:
        case SUBCMD_MOVE_FAST:
            rv = handle_bb_move(c, (subcmd_bb_move_t *)pkt);
            break;

        case SUBCMD_DELETE_ITEM:
            rv = handle_bb_drop_stack(c, (subcmd_bb_destroy_item_t *)pkt);
            break;

        case SUBCMD_WORD_SELECT:
            rv = handle_bb_word_select(c, (subcmd_bb_word_select_t *)pkt);
            break;

        case SUBCMD_DROP_POS:
            rv = handle_bb_drop_pos(c, (subcmd_bb_drop_pos_t *)pkt);
            break;

        case SUBCMD_SORT_INV:
            rv = handle_bb_sort_inv(c, (subcmd_bb_sort_inv_t *)pkt);
            break;

        case SUBCMD_MEDIC:
            rv = handle_bb_medic(c, (bb_subcmd_pkt_t *)pkt);
            break;

        case SUBCMD_REQ_EXP:
            rv = handle_bb_req_exp(c, (subcmd_bb_req_exp_pkt_t *)pkt);
            break;

        case SUBCMD_USED_TECH:
            rv = handle_bb_used_tech(c, (subcmd_bb_used_tech_t *)pkt);
            break;

        case SUBCMD_TAKE_DAMAGE1:
        case SUBCMD_TAKE_DAMAGE2:
            rv = handle_bb_take_damage(c, (subcmd_bb_take_damage_t *)pkt);
            break;

        case SUBCMD_SPAWN_NPC:
            rv = handle_bb_spawn_npc(c, pkt);
            break;

        default:
#ifdef BB_LOG_UNKNOWN_SUBS
            debug(DBG_LOG, "Unknown 0x60: 0x%02X\n", type);
            print_packet((unsigned char *)pkt, LE16(pkt->hdr.pkt_len));
#endif /* BB_LOG_UNKNOWN_SUBS */
            sent = 0;
            break;

        case SUBCMD_FINISH_LOAD:
            if(l->type == LOBBY_TYPE_DEFAULT) {
                for(i = 0; i < l->max_clients; ++i) {
                    if(l->clients[i] && l->clients[i] != c &&
                       subcmd_send_pos(c, l->clients[i])) {
                        rv = -1;
                        break;
                    }
                }
            }

        case SUBCMD_LOAD_22:
        case SUBCMD_TALK_NPC:
        case SUBCMD_DONE_NPC:
        case SUBCMD_LOAD_3B:
        case SUBCMD_TALK_DESK:
        case SUBCMD_WARP_55:
        case SUBCMD_LOBBY_ACTION:
        case SUBCMD_GOGO_BALL:
        case SUBCMD_LOBBY_CHAIR:
        case SUBCMD_CHAIR_DIR:
        case SUBCMD_CHAIR_MOVE:
            sent = 0;
    }

    /* Broadcast anything we don't care to check anything about. */
    if(!sent)
        rv = subcmd_send_lobby_bb(l, c, (bb_subcmd_pkt_t *)pkt, 0);

    pthread_mutex_unlock(&l->mutex);
    return rv;
}

/* Handle a 0xC9/0xCB packet. */
int subcmd_handle_ep3_bcast(ship_client_t *c, subcmd_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;
    int rv;

    /* Ignore these if the client isn't in a lobby. */
    if(!l) {
        return 0;
    }

    pthread_mutex_lock(&l->mutex);

    /* We don't do anything special with these just yet... */
    rv = lobby_send_pkt_ep3(l, c, (dc_pkt_hdr_t *)pkt);

    pthread_mutex_unlock(&l->mutex);
    return rv;
}

int subcmd_send_lobby_item(lobby_t *l, subcmd_itemreq_t *req,
                           const uint32_t item[4]) {
    subcmd_itemgen_t gen;
    int i;
    uint32_t tmp = LE32(req->unk2[0]) & 0x0000FFFF;

    /* Fill in the packet we'll send out. */
    gen.hdr.pkt_type = GAME_COMMAND0_TYPE;
    gen.hdr.flags = 0;
    gen.hdr.pkt_len = LE16(0x0030);
    gen.type = SUBCMD_ITEMDROP;
    gen.size = 0x0B;
    gen.unused = 0;
    gen.area = req->area;
    gen.what = req->pt_index;   /* Probably not right... but whatever. */
    gen.req = req->req;
    gen.x = req->x;
    gen.y = req->y;
    gen.unk1 = LE32(tmp);       /* ??? */

    gen.item[0] = LE32(item[0]);
    gen.item[1] = LE32(item[1]);
    gen.item[2] = LE32(item[2]);
    gen.item2[0] = LE32(item[3]);
    gen.item2[1] = LE32(0x00000002);

    gen.item_id = LE32(l->item_id);
    ++l->item_id;

    /* Send the packet to every client in the lobby. */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i]) {
            send_pkt_dc(l->clients[i], (dc_pkt_hdr_t *)&gen);
        }
    }

    return 0;
}

int subcmd_send_bb_lobby_item(lobby_t *l, subcmd_bb_itemreq_t *req,
                              const item_t *item) {
    subcmd_bb_itemgen_t gen;
    int i;
    uint32_t tmp = LE32(req->unk2[0]) & 0x0000FFFF;

    /* Fill in the packet we'll send out. */
    gen.hdr.pkt_type = GAME_COMMAND0_TYPE;
    gen.hdr.flags = 0;
    gen.hdr.pkt_len = LE16(0x0030);
    gen.type = SUBCMD_ITEMDROP;
    gen.size = 0x0B;
    gen.unused = 0;
    gen.area = req->area;
    gen.what = req->pt_index;   /* Probably not right... but whatever. */
    gen.req = req->req;
    gen.x = req->x;
    gen.y = req->y;
    gen.unk1 = LE32(tmp);       /* ??? */

    gen.item[0] = LE32(item->data_l[0]);
    gen.item[1] = LE32(item->data_l[1]);
    gen.item[2] = LE32(item->data_l[2]);
    gen.item2 = LE32(item->data2_l);

    gen.item_id = LE32(item->item_id);

    /* Send the packet to every client in the lobby. */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i]) {
            send_pkt_bb(l->clients[i], (bb_pkt_hdr_t *)&gen);
        }
    }

    return 0;
}

static int subcmd_send_shop_inv(ship_client_t *c, subcmd_bb_shop_req_t *req) {
    /* XXXX: Hard coded for now... */
    subcmd_bb_shop_inv_t shop;
    int i;
    block_t *b = c->cur_block;

    memset(&shop, 0, sizeof(shop));

    shop.hdr.pkt_len = LE16(0x00EC);
    shop.hdr.pkt_type = LE16(GAME_COMMAND0_TYPE);
    shop.hdr.flags = 0;
    shop.type = SUBCMD_SHOPINV;
    shop.size = 0x3B;
    shop.shop_type = req->shop_type;
    shop.num_items = 0x0B;

    for(i = 0; i < 0x0B; ++i) {
        shop.items[i].item_data[0] = LE32((0x03 | (i << 8)));
        shop.items[i].reserved = 0xFFFFFFFF;
        shop.items[i].cost = LE32((mt19937_genrand_int32(&b->rng) % 255));
    }

    return send_pkt_bb(c, (bb_pkt_hdr_t *)&shop);
}

/* It is assumed that all parameters are already in little-endian order. */
static int subcmd_send_drop_stack(ship_client_t *c, uint32_t area, float x,
                                  float z, item_t *item) {
    subcmd_bb_drop_stack_t drop;

    /* Fill in the packet... */
    drop.hdr.pkt_len = LE16(0x002C);
    drop.hdr.pkt_type = LE16(GAME_COMMAND0_TYPE);
    drop.hdr.flags = 0;
    drop.type = SUBCMD_DROP_STACK;
    drop.size = 0x09;
    drop.client_id = c->client_id;
    drop.unused = 0;
    drop.area = area;
    drop.x = x;
    drop.z = z;
    drop.item[0] = item->data_l[0];
    drop.item[1] = item->data_l[1];
    drop.item[2] = item->data_l[2];
    drop.item_id = item->item_id;
    drop.item2 = item->data2_l;

    return subcmd_send_lobby_bb(c->cur_lobby, NULL, (bb_subcmd_pkt_t *)&drop,
                                0);
}

static int subcmd_send_picked_up(ship_client_t *c, uint32_t data_l[3],
                                 uint32_t item_id, uint32_t data2_l,
                                 int send_to_client) {
    subcmd_bb_create_item_t pick;

    /* Fill in the packet. */
    pick.hdr.pkt_len = LE16(0x0024);
    pick.hdr.pkt_type = LE16(GAME_COMMAND0_TYPE);
    pick.hdr.flags = 0;
    pick.type = SUBCMD_CREATE_ITEM;
    pick.size = 0x07;
    pick.client_id = c->client_id;
    pick.unused = 0;
    pick.item[0] = data_l[0];
    pick.item[1] = data_l[1];
    pick.item[2] = data_l[2];
    pick.item_id = item_id;
    pick.item2 = data2_l;
    pick.unused2 = 0;

    if(send_to_client)
        return subcmd_send_lobby_bb(c->cur_lobby, NULL,
                                    (bb_subcmd_pkt_t *)&pick, 0);
    else
        return subcmd_send_lobby_bb(c->cur_lobby, c, (bb_subcmd_pkt_t *)&pick,
                                    0);
}

static int subcmd_send_destroy_map_item(ship_client_t *c, uint8_t area,
                                        uint32_t item_id) {
    subcmd_bb_destroy_map_item_t d;

    /* Fill in the packet. */
    d.hdr.pkt_len = LE16(0x0014);
    d.hdr.pkt_type = LE16(GAME_COMMAND0_TYPE);
    d.hdr.flags = 0;
    d.type = SUBCMD_DEL_MAP_ITEM;
    d.size = 0x03;
    d.client_id = c->client_id;
    d.unused = 0;
    d.client_id2 = c->client_id;
    d.unused2 = 0;
    d.area = area;
    d.unused3 = 0;
    d.item_id = item_id;

    return subcmd_send_lobby_bb(c->cur_lobby, NULL, (bb_subcmd_pkt_t *)&d, 0);
}

static int subcmd_send_destroy_item(ship_client_t *c, uint32_t item_id,
                                    uint8_t amt) {
    subcmd_bb_destroy_item_t d;

    /* Fill in the packet. */
    d.hdr.pkt_len = LE16(0x0014);
    d.hdr.pkt_type = LE16(GAME_COMMAND0_TYPE);
    d.hdr.flags = 0;
    d.type = SUBCMD_DELETE_ITEM;
    d.size = 0x03;
    d.client_id = c->client_id;
    d.unused = 0;
    d.item_id = item_id;
    d.amount = LE32(amt);

    return subcmd_send_lobby_bb(c->cur_lobby, c, (bb_subcmd_pkt_t *)&d, 0);
}

int subcmd_send_bb_exp(ship_client_t *c, uint32_t exp) {
    subcmd_bb_exp_t pkt;

    /* Fill in the packet. */
    pkt.hdr.pkt_len = LE16(0x0010);
    pkt.hdr.pkt_type = LE16(GAME_COMMAND0_TYPE);
    pkt.hdr.flags = 0;
    pkt.type = SUBCMD_GIVE_EXP;
    pkt.size = 0x02;
    pkt.client_id = c->client_id;
    pkt.unused = 0;
    pkt.exp = LE32(exp);

    return subcmd_send_lobby_bb(c->cur_lobby, NULL, (bb_subcmd_pkt_t *)&pkt, 0);
}

int subcmd_send_bb_level(ship_client_t *c) {
    subcmd_bb_level_t pkt;
    int i;
    uint16_t base, mag;

    /* Fill in the packet. */
    pkt.hdr.pkt_len = LE16(0x001C);
    pkt.hdr.pkt_type = LE16(GAME_COMMAND0_TYPE);
    pkt.hdr.flags = 0;
    pkt.type = SUBCMD_LEVELUP;
    pkt.size = 0x05;
    pkt.client_id = c->client_id;
    pkt.unused = 0;

    /* Fill in the base statistics. These are all in little-endian already. */
    pkt.atp = c->bb_pl->character.atp;
    pkt.mst = c->bb_pl->character.mst;
    pkt.evp = c->bb_pl->character.evp;
    pkt.hp = c->bb_pl->character.hp;
    pkt.dfp = c->bb_pl->character.dfp;
    pkt.ata = c->bb_pl->character.ata;
    pkt.level = c->bb_pl->character.level;

    /* Add in the mag's bonus. */
    for(i = 0; i < c->bb_pl->inv.item_count; ++i) {
        if((c->bb_pl->inv.items[i].flags & LE32(0x00000008)) &&
           c->bb_pl->inv.items[i].data_b[0] == 0x02) {
            base = LE16(pkt.dfp);
            mag = LE16(c->bb_pl->inv.items[i].data_w[2]) / 100;
            pkt.dfp = LE16((base + mag));

            base = LE16(pkt.atp);
            mag = LE16(c->bb_pl->inv.items[i].data_w[3]) / 50;
            pkt.atp = LE16((base + mag));

            base = LE16(pkt.ata);
            mag = LE16(c->bb_pl->inv.items[i].data_w[4]) / 200;
            pkt.ata = LE16((base + mag));

            base = LE16(pkt.mst);
            mag = LE16(c->bb_pl->inv.items[i].data_w[5]) / 50;
            pkt.mst = LE16((base + mag));

            break;
        }
    }

    return subcmd_send_lobby_bb(c->cur_lobby, NULL, (bb_subcmd_pkt_t *)&pkt, 0);
}

int subcmd_send_lobby_dc(lobby_t *l, ship_client_t *c, subcmd_pkt_t *pkt,
                         int igcheck) {
    int i;

    /* Send the packet to every connected client. */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] && l->clients[i] != c) {
            /* If we're supposed to check the ignore list, and this client is on
               it, don't send the packet. */
            if(igcheck && client_has_ignored(l->clients[i], c->guildcard)) {
                continue;
            }

            if(l->clients[i]->version != CLIENT_VERSION_DCV1 ||
               !(l->clients[i]->flags & CLIENT_FLAG_IS_NTE))
                send_pkt_dc(l->clients[i], (dc_pkt_hdr_t *)pkt);
            else
                subcmd_translate_dc_to_nte(l->clients[i], pkt);
        }
    }

    return 0;
}

int subcmd_send_lobby_bb(lobby_t *l, ship_client_t *c, bb_subcmd_pkt_t *pkt,
                         int igcheck) {
    int i;

    /* Send the packet to every connected client. */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] && l->clients[i] != c) {
            /* If we're supposed to check the ignore list, and this client is on
               it, don't send the packet. */
            if(igcheck && client_has_ignored(l->clients[i], c->guildcard)) {
                continue;
            }

            if(l->clients[i]->version != CLIENT_VERSION_DCV1 ||
               !(l->clients[i]->flags & CLIENT_FLAG_IS_NTE))
                send_pkt_bb(l->clients[i], (bb_pkt_hdr_t *)pkt);
            else
                subcmd_translate_bb_to_nte(l->clients[i], pkt);
        }
    }

    return 0;
}

int subcmd_send_pos(ship_client_t *dst, ship_client_t *src) {
    subcmd_set_pos_t dc;
    subcmd_bb_set_pos_t bb;

    if(dst->version == CLIENT_VERSION_BB) {
        bb.hdr.pkt_type = LE16(GAME_COMMAND0_TYPE);
        bb.hdr.flags = 0;
        bb.hdr.pkt_len = LE16(0x0020);
        bb.type = 0x20;
        bb.size = 6;
        bb.client_id = src->client_id;
        bb.unused = 0;
        dc.size = 6;
        dc.client_id = src->client_id;
        bb.unused = 0;
        bb.unk = LE32(0x0000000F);          /* Area */
        bb.w = src->x;                      /* X */
        bb.x = 0;                           /* Y */
        bb.y = src->z;                      /* Z */
        bb.z = 0;                           /* Facing, perhaps? */

        return send_pkt_bb(dst, (bb_pkt_hdr_t *)&bb);
    }
    else {
        dc.hdr.pkt_type = GAME_COMMAND0_TYPE;
        dc.hdr.flags = 0;
        dc.hdr.pkt_len = LE16(0x001C);

        if(dst->version == CLIENT_VERSION_DCV1 &&
           (dst->flags & CLIENT_FLAG_IS_NTE))
            dc.type = 0x1C;
        else
            dc.type = 0x20;

        dc.size = 6;
        dc.client_id = src->client_id;
        dc.unused = 0;
        dc.unk = LE32(0x0000000F);          /* Area */
        dc.w = src->x;                      /* X */
        dc.x = 0;                           /* Y */
        dc.y = src->z;                      /* Z */
        dc.z = 0;                           /* Facing, perhaps? */

        send_pkt_dc(dst, (dc_pkt_hdr_t *)&dc);
        return 0;
    }
}
