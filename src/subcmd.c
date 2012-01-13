/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2012 Lawrence Sebald

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

/* Handle a Guild card send packet. */
int handle_dc_gcsend(ship_client_t *d, subcmd_dc_gcsend_t *pkt) {
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

static int handle_pc_gcsend(ship_client_t *d, subcmd_pc_gcsend_t *pkt) {
    /* This differs based on the destination client's version. */
    switch(d->version) {
        case CLIENT_VERSION_PC:
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

static int handle_gc_gcsend(ship_client_t *d, subcmd_gc_gcsend_t *pkt) {
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

static int handle_itemreq(ship_client_t *c, subcmd_itemreq_t *req) {
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

    /* Who knows if this is right? It works though, so we'll go with it. */
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

    return lobby_send_pkt_dc(c->cur_lobby, c, (dc_pkt_hdr_t *)pkt, 0);
}

static int handle_take_item(ship_client_t *c, subcmd_take_item_t *pkt) {
    lobby_t *l = c->cur_lobby;
    sylverant_iitem_t item;
    uint32_t v;
    int i;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return -1;
    }

    /* Buggy PSO version is buggy... */
    if(c->version == CLIENT_VERSION_DCV1 && pkt->size == 0x06) {
        pkt->size = 0x07;
    }

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->size != 0x07 || pkt->client_id != c->client_id) {
        return -1;
    }

    /* If we're in legit mode, we need to check the newly taken item. */
    if((l->flags & LOBBY_FLAG_LEGIT_MODE) && ship->limits) {
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

        if(!sylverant_limits_check_item(ship->limits, &item, v)) {
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
#if 0
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
#endif
    return lobby_send_pkt_dc(c->cur_lobby, c, (dc_pkt_hdr_t *)pkt, 0);
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
    if((l->flags & LOBBY_FLAG_LEGIT_MODE) && ship->limits) {
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

        if(!sylverant_limits_check_item(ship->limits, &item, v)) {
            /* The item failed the check, deal with it. */
            debug(DBG_LOG, "Potentially non-legit item dropped in legit mode:\n"
                  "%08x %08x %08x %08x\n", LE32(pkt->item[0]), 
                  LE32(pkt->item[1]), LE32(pkt->item[2]), LE32(pkt->item2[0]));

            /* Grab the item name, if we can find it. */
            name = item_get_name((item_t *)&item);

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
    return lobby_send_pkt_dc(c->cur_lobby, c, (dc_pkt_hdr_t *)pkt, 0);
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
        return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt, 0);
    }

    /* This aught to do it... */
    lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt, 0);
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
        return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt, 0);
    }

    /* This aught to do it... */
    lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt, 0);
    return send_lobby_mod_stat(l, c, SUBCMD_STAT_TPUP, 255);
}

static int handle_set_area(ship_client_t *c, subcmd_set_area_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Make sure the area is valid */
    if(pkt->area > 17) {
        return -1;
    }

    /* Save the new area and move along */
    if(c->client_id == pkt->client_id) {
        c->cur_area = pkt->area;
    }

    return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt, 0);
}

static int handle_bb_set_area(ship_client_t *c, subcmd_bb_set_area_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Make sure the area is valid */
    if(pkt->area > 17) {
        return -1;
    }

    /* Save the new area and move along */
    if(c->client_id == pkt->client_id) {
        c->cur_area = pkt->area;
    }

    return lobby_send_pkt_bb(l, c, (bb_pkt_hdr_t *)pkt, 0);
}

static int handle_set_pos(ship_client_t *c, subcmd_set_pos_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Save the new position and move along */
    if(c->client_id == pkt->client_id) {
        c->w = pkt->w;
        c->x = pkt->x;
        c->y = pkt->y;
        c->z = pkt->z;
    }

    /* Clear this, in case we're at the lobby counter */
    c->last_info_req = 0;

    return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt, 0);
}

static int handle_move(ship_client_t *c, subcmd_move_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Save the new position and move along */
    if(c->client_id == pkt->client_id) {
        c->x = pkt->x;
        c->z = pkt->z;
    }

    return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt, 0);
}

static int handle_bb_set_pos(ship_client_t *c, subcmd_bb_set_pos_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Save the new position and move along */
    if(c->client_id == pkt->client_id) {
        c->w = pkt->w;
        c->x = pkt->x;
        c->y = pkt->y;
        c->z = pkt->z;
    }

    return lobby_send_pkt_bb(l, c, (bb_pkt_hdr_t *)pkt, 0);
}

static int handle_bb_move(ship_client_t *c, subcmd_bb_move_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Save the new position and move along */
    if(c->client_id == pkt->client_id) {
        c->x = pkt->x;
        c->z = pkt->z;
    }

    return lobby_send_pkt_bb(l, c, (bb_pkt_hdr_t *)pkt, 0);
}

static int handle_delete_inv(ship_client_t *c, subcmd_destroy_item_t *pkt) {
    lobby_t *l = c->cur_lobby;
    int num;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->size != 0x03) {
        return -1;
    }

#if 0
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
#endif

    return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt, 0);
}

static int handle_buy(ship_client_t *c, subcmd_buy_t *pkt) {
    lobby_t *l = c->cur_lobby;
    uint32_t ic;
    int i;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->size != 0x06 || pkt->client_id != c->client_id) {
        return -1;
    }

    /* Make a note of the item ID, and add to the inventory */
    l->highest_item[c->client_id] = LE32(pkt->item_id);
#if 0
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
#endif
    return lobby_send_pkt_dc(c->cur_lobby, c, (dc_pkt_hdr_t *)pkt, 0);
}

static int handle_use_item(ship_client_t *c, subcmd_use_item_t *pkt) {
    lobby_t *l = c->cur_lobby;
    int num;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->size != 0x02) {
        return -1;
    }

#if 0
    /* Remove the item from the user's inventory */
    num = item_remove_from_inv(c->items, c->item_count, pkt->item_id, 1);
    if(num < 0) {
        debug(DBG_WARN, "Couldn't remove item from inventory!\n");
    }
    else {
        c->item_count -= num;
    }
#endif

    return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt, 0);
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
    return lobby_send_pkt_bb(l, c, (bb_pkt_hdr_t *)pkt, 0);
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
    return lobby_send_pkt_bb(l, c, (bb_pkt_hdr_t *)pkt, 0);
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
    return lobby_send_pkt_bb(l, c, (bb_pkt_hdr_t *)pkt, 0);
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

    /* Don't send chats for a STFUed client. */
    if((c->flags & CLIENT_FLAG_STFU)) {
        return 0;
    }

    return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt, 1);
}

static int handle_bb_symbol_chat(ship_client_t *c, bb_subcmd_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Don't send chats for a STFUed client. */
    if((c->flags & CLIENT_FLAG_STFU)) {
        return 0;
    }

    return lobby_send_pkt_bb(l, c, (bb_pkt_hdr_t *)pkt, 1);
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
            return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt, 0);
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
    pkt->checksum = genrand_int32();  /* Client doesn't care */
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
    return lobby_send_pkt_bb(l, c, (bb_pkt_hdr_t *)pkt, 0);
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
    return lobby_send_pkt_bb(l, c, (bb_pkt_hdr_t *)pkt, 0);
}

/* Handle a 0x62/0x6D packet. */
int subcmd_handle_one(ship_client_t *c, subcmd_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;
    ship_client_t *dest;
    uint8_t type = pkt->type;
    int rv = -1;

    /* Ignore these if the client isn't in a lobby. */
    if(!l) {
        return 0;
    }

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
        switch(type) {
            case SUBCMD_BURST1:
            case SUBCMD_BURST2:
            case SUBCMD_BURST3:
            case SUBCMD_BURST4:
            case SUBCMD_BURST5:
            case SUBCMD_BURST6:
            case SUBCMD_BURST7:
                rv = send_pkt_dc(dest, (dc_pkt_hdr_t *)pkt);
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
                    rv = handle_dc_gcsend(dest, (subcmd_dc_gcsend_t *)pkt);
                    break;

                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_EP3:
                    rv = handle_gc_gcsend(dest, (subcmd_gc_gcsend_t *)pkt);
                    break;

                case CLIENT_VERSION_PC:
                    rv = handle_pc_gcsend(dest, (subcmd_pc_gcsend_t *)pkt);
                    break;
            }
            break;

        case SUBCMD_ITEMREQ:
            /* There's only two ways we pay attention to this one: First, if the
               lobby is not in legit mode and a GM has used /item. Second, if
               the lobby has a drop function (for server-side drops). */
            if(c->next_item[0] && !(l->flags & LOBBY_FLAG_LEGIT_MODE)) {
                rv = handle_itemreq(c, (subcmd_itemreq_t *)pkt);
            }
            else if(l->dropfunc) {
                rv = l->dropfunc(l, (subcmd_itemreq_t *)pkt);
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
    int rv, sent = 1;

    /* Ignore these if the client isn't in a lobby. */
    if(!l) {
        return 0;
    }

    pthread_mutex_lock(&l->mutex);

    /* If there's a burst going on in the lobby, delay most packets */
    if(l->flags & LOBBY_FLAG_BURSTING) {
        switch(type) {
            case SUBCMD_LOAD_3B:
            case SUBCMD_BURST_DONE:
                rv = lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt, 0);
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

        default:
#ifdef LOG_UNKNOWN_SUBS
            debug(DBG_LOG, "Unknown 0x60: 0x%02X\n", type);
            print_packet((unsigned char *)pkt, LE16(pkt->hdr.dc.pkt_len));
#endif /* LOG_UNKNOWN_SUBS */

        case SUBCMD_SET_AREA_21:
        case SUBCMD_LOAD_22:
        case SUBCMD_FINISH_LOAD:
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
    if(!sent) {
        rv = lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt, 0);
    }

    pthread_mutex_unlock(&l->mutex);
    return rv;
}

int subcmd_bb_handle_bcast(ship_client_t *c, bb_subcmd_pkt_t *pkt) {
    uint8_t type = pkt->type;
    lobby_t *l = c->cur_lobby;
    int rv, sent = 1;

    /* Ignore these if the client isn't in a lobby. */
    if(!l) {
        return 0;
    }

    pthread_mutex_lock(&l->mutex);

    switch(type) {
        case SUBCMD_SYMBOL_CHAT:
            rv = handle_bb_symbol_chat(c, pkt);
            break;

        case SUBCMD_SET_AREA:
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

        default:
#ifdef BB_LOG_UNKNOWN_SUBS
            debug(DBG_LOG, "Unknown 0x60: 0x%02X\n", type);
            print_packet((unsigned char *)pkt, LE16(pkt->hdr.pkt_len));
#endif /* BB_LOG_UNKNOWN_SUBS */

        case SUBCMD_SET_AREA_21:
        case SUBCMD_LOAD_22:
        case SUBCMD_FINISH_LOAD:
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
    if(!sent) {
        rv = lobby_send_pkt_bb(l, c, (bb_pkt_hdr_t *)pkt, 0);
    }

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
    gen.hdr.pkt_len = LE16(0x30);
    gen.type = SUBCMD_ITEMDROP;
    gen.size = 0x0B;
    gen.unused = 0;
    gen.area = req->area;
    gen.what = 0x02;            /* 0x02 for boxes, 0x01 for monsters? */
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

static int subcmd_send_shop_inv(ship_client_t *c, subcmd_bb_shop_req_t *req) {
    /* XXXX: Hard coded for now... */
    subcmd_bb_shop_inv_t shop;
    int i;

    memset(&shop, 0, sizeof(shop));

    shop.hdr.pkt_len = LE16(0xEC);
    shop.hdr.pkt_type = LE16(GAME_COMMAND0_TYPE);
    shop.hdr.flags = 0;
    shop.type = SUBCMD_SHOPINV;
    shop.size = 0x3B;
    shop.shop_type = req->shop_type;
    shop.num_items = 0x0B;

    for(i = 0; i < 0x0B; ++i) {
        shop.items[i].item_data[0] = LE32((0x03 | (i << 8)));
        shop.items[i].reserved = 0xFFFFFFFF;
        shop.items[i].cost = LE32((genrand_int32() % 255));
    }

    return send_pkt_bb(c, (bb_pkt_hdr_t *)&shop);
}

/* It is assumed that all parameters are already in little-endian order. */
static int subcmd_send_drop_stack(ship_client_t *c, uint32_t area, float x,
                                  float z, item_t *item) {
    subcmd_bb_drop_stack_t drop;

    /* Fill in the packet... */
    drop.hdr.pkt_len = LE16(0x2C);
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

    return lobby_send_pkt_bb(c->cur_lobby, NULL, &drop, 0);
}

static int subcmd_send_picked_up(ship_client_t *c, uint32_t data_l[3],
                                 uint32_t item_id, uint32_t data2_l,
                                 int send_to_client) {
    subcmd_bb_create_item_t pick;

    /* Fill in the packet. */
    pick.hdr.pkt_len = LE16(0x24);
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
        return lobby_send_pkt_bb(c->cur_lobby, NULL, &pick, 0);
    else
        return lobby_send_pkt_bb(c->cur_lobby, c, &pick, 0);
}

static int subcmd_send_destroy_map_item(ship_client_t *c, uint8_t area,
                                        uint32_t item_id) {
    subcmd_bb_destroy_map_item_t d;

    /* Fill in the packet. */
    d.hdr.pkt_len = LE16(0x14);
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

    return lobby_send_pkt_bb(c->cur_lobby, NULL, &d, 0);
}

static int subcmd_send_destroy_item(ship_client_t *c, uint32_t item_id,
                                    uint8_t amt) {
    subcmd_bb_destroy_item_t d;

    /* Fill in the packet. */
    d.hdr.pkt_len = LE16(0x14);
    d.hdr.pkt_type = LE16(GAME_COMMAND0_TYPE);
    d.hdr.flags = 0;
    d.type = SUBCMD_DELETE_ITEM;
    d.size = 0x03;
    d.client_id = c->client_id;
    d.unused = 0;
    d.item_id = item_id;
    d.amount = LE32(amt);

    return lobby_send_pkt_bb(c->cur_lobby, c, &d, 0);
}
