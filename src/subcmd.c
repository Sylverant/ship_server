/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010 Lawrence Sebald

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

#include <iconv.h>
#include <string.h>

#include "subcmd.h"
#include "clients.h"
#include "ship_packets.h"
#include "utils.h"

/* Handle a Guild card send packet. */
int handle_dc_gcsend(ship_client_t *d, subcmd_dc_gcsend_t *pkt) {
    /* This differs based on the destination client's version. */
    switch(d->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
            return send_pkt_dc(d, (dc_pkt_hdr_t *)pkt);

        case CLIENT_VERSION_GC:
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
            iconv_t ic;
            size_t in, out;
            char *inptr, *outptr;

            /* Convert from Shift-JIS/ISO-8859-1 to UTF-16. */
            if(pkt->text[1] == 'J') {
                ic = iconv_open("UTF-16LE", "SHIFT_JIS");
            }
            else {
                ic = iconv_open("UTF-16LE", "ISO-8859-1");
            }

            if(ic == (iconv_t)-1) {
                return 0;
            }

            memset(&pc, 0, sizeof(pc));

            /* First the name. */
            in = 24;
            out = 48;
            inptr = pkt->name;
            outptr = (char *)pc.name;
            iconv(ic, &inptr, &in, &outptr, &out);

            /* Then the text. */
            in = 88;
            out = 176;
            inptr = pkt->text;
            outptr = (char *)pc.text;
            iconv(ic, &inptr, &in, &outptr, &out);
            iconv_close(ic);

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
            iconv_t ic;
            size_t in, out;
            char *inptr, *outptr;
    
            /* Convert from UTF-16 to Shift-JIS/ISO-8859-1. */
            if(LE16(pkt->text[1]) == (uint16_t)('J')) {
                ic = iconv_open("SHIFT_JIS", "UTF-16LE");
            }
            else {
                ic = iconv_open("ISO-8859-1", "UTF-16LE");
            }

            if(ic == (iconv_t)-1) {
                return 0;
            }

            memset(&dc, 0, sizeof(dc));

            /* First the name. */
            in = 48;
            out = 24;
            inptr = (char *)pkt->name;
            outptr = dc.name;
            iconv(ic, &inptr, &in, &outptr, &out);

            /* Then the text. */
            in = 176;
            out = 88;
            inptr = (char *)pkt->text;
            outptr = dc.text;
            iconv(ic, &inptr, &in, &outptr, &out);
            iconv_close(ic);

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
        {
            subcmd_gc_gcsend_t gc;
            iconv_t ic;
            size_t in, out;
            char *inptr, *outptr;

            /* Convert from UTF-16 to Shift-JIS/ISO-8859-1. */
            if(LE16(pkt->text[1]) == (uint16_t)('J')) {
                ic = iconv_open("SHIFT_JIS", "UTF-16LE");
            }
            else {
                ic = iconv_open("ISO-8859-1", "UTF-16LE");
            }

            if(ic == (iconv_t)-1) {
                return 0;
            }

            memset(&gc, 0, sizeof(gc));

            /* First the name. */
            in = 48;
            out = 24;
            inptr = (char *)pkt->name;
            outptr = gc.name;
            iconv(ic, &inptr, &in, &outptr, &out);

            /* Then the text. */
            in = 176;
            out = 88;
            inptr = (char *)pkt->text;
            outptr = gc.text;
            iconv(ic, &inptr, &in, &outptr, &out);
            iconv_close(ic);

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

    }

    return 0;
}

static int handle_gc_gcsend(ship_client_t *d, subcmd_gc_gcsend_t *pkt) {
    /* This differs based on the destination client's version. */
    switch(d->version) {
        case CLIENT_VERSION_GC:
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
            iconv_t ic;
            size_t in, out;
            char *inptr, *outptr;

            /* Convert from Shift-JIS/ISO-8859-1 to UTF-16. */
            if(pkt->text[1] == 'J') {
                ic = iconv_open("UTF-16LE", "SHIFT_JIS");
            }
            else {
                ic = iconv_open("UTF-16LE", "ISO-8859-1");
            }

            if(ic == (iconv_t)-1) {
                return 0;
            }

            memset(&pc, 0, sizeof(pc));

            /* First the name. */
            in = 24;
            out = 48;
            inptr = pkt->name;
            outptr = (char *)pc.name;
            iconv(ic, &inptr, &in, &outptr, &out);

            /* Then the text. */
            in = 88;
            out = 176;
            inptr = pkt->text;
            outptr = (char *)pc.text;
            iconv(ic, &inptr, &in, &outptr, &out);
            iconv_close(ic);

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
    gen.unk2 = LE32((r | 0x06010100));

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

    return lobby_send_pkt_dc(c->cur_lobby, c, (dc_pkt_hdr_t *)pkt);
}

static int handle_take_item(ship_client_t *c, subcmd_take_item_t *pkt) {
    lobby_t *l = c->cur_lobby;
    sylverant_iitem_t item;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->size != 0x07 || pkt->client_id != c->client_id) {
        return -1;
    }

    /* If we're in legit mode, we need to check the newly taken item. */
    if(l->legit_mode && c->cur_ship->limits) {
        /* Fill in the item structure so we can check it. */
        memcpy(&item.data_l[0], &pkt->data_l[0], 5 * sizeof(uint32_t));

        if(!sylverant_limits_check_item(c->cur_ship->limits, &item)) {
            /* The item failed the check, so kick the user. */
            send_message_box(c, "\tEYou have been kicked from the server.\n\n"
                             "Reason:\n"
                             "Attempt to remove a non-legit item from\n"
                             "the bank in a legit-mode game.");
            return -1;
        }
    }

    /* If we get here, either the game is not in legit mode, or the item is
       actually legit, so just forward the packet on. */
    return lobby_send_pkt_dc(c->cur_lobby, c, (dc_pkt_hdr_t *)pkt);
}

static int handle_itemdrop(ship_client_t *c, subcmd_itemgen_t *pkt) {
    lobby_t *l = c->cur_lobby;
    sylverant_iitem_t item;

    /* We can't get these in default lobbies without someone messing with
       something that they shouldn't be... Disconnect anyone that tries. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return -1;
    }

    /* Sanity check... Make sure the size of the subcommand matches with what we
       expect. Disconnect the client if not. */
    if(pkt->size != 0x0B) {
        return -1;
    }

    /* If we're in legit mode, we need to check the item. */
    if(l->legit_mode && c->cur_ship->limits) {
        /* Fill in the item structure so we can check it. */
        memcpy(&item.data_l[0], &pkt->item[0], 5 * sizeof(uint32_t));

        if(!sylverant_limits_check_item(c->cur_ship->limits, &item)) {
            /* The item failed the check, so silently ignore the packet, and
               nobody will see it drop. */
            return 0;
        }
    }

    /* If we get here, either the game is not in legit mode, or the item is
       actually legit, so just forward the packet on. */
    return lobby_send_pkt_dc(c->cur_lobby, c, (dc_pkt_hdr_t *)pkt);
}

/* Handle a 0x62/0x6D packet. */
int subcmd_handle_one(ship_client_t *c, subcmd_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;
    ship_client_t *dest;
    uint8_t type = pkt->type;
    
    /* Find the destination. */
    dest = l->clients[pkt->hdr.flags];
    
    /* The destination is now offline, don't bother sending it. */
    if(!dest) {
        return 0;
    }

    switch(type) {
        case SUBCMD_GUILDCARD:
            switch(c->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                    return handle_dc_gcsend(dest, (subcmd_dc_gcsend_t *)pkt);

                case CLIENT_VERSION_GC:
                    return handle_gc_gcsend(dest, (subcmd_gc_gcsend_t *)pkt);

                case CLIENT_VERSION_PC:
                    return handle_pc_gcsend(dest, (subcmd_pc_gcsend_t *)pkt);
            }
            break;

        case SUBCMD_ITEMREQ:
            /* Only pay attention if an item has been set and we're not in
               legit mode. */
            if(c->next_item[0] && !l->legit_mode) {
                return handle_itemreq(c, (subcmd_itemreq_t *)pkt);
            }

        default:
            /* Forward the packet unchanged to the destination. */
            return send_pkt_dc(dest, (dc_pkt_hdr_t *)pkt);
    }

    return -1;
}

/* Handle a 0x60 packet. */
int subcmd_handle_bcast(ship_client_t *c, subcmd_pkt_t *pkt) {
    uint8_t type = pkt->type;

    switch(type) {
        case SUBCMD_TAKE_ITEM:
            return handle_take_item(c, (subcmd_take_item_t *)pkt);

        case SUBCMD_LEVELUP:
            return handle_levelup(c, (subcmd_levelup_t *)pkt);

        case SUBCMD_ITEMDROP:
            return handle_itemdrop(c, (subcmd_itemgen_t *)pkt);
    }

    /* Broadcast anything we don't care to check anything about. */
    return lobby_send_pkt_dc(c->cur_lobby, c, (dc_pkt_hdr_t *)pkt);
}
