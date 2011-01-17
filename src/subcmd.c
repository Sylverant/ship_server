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

#include <stdio.h>
#include <iconv.h>
#include <string.h>
#include <pthread.h>

#include <sylverant/debug.h>

#include "subcmd.h"
#include "clients.h"
#include "ship_packets.h"
#include "utils.h"
#include "items.h"
#include "word_select.h"

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
            ICONV_CONST char *inptr;
            char *outptr;

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
            ICONV_CONST char *inptr;
            char *outptr;
    
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
            ICONV_CONST char *inptr;
            char *outptr;

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
            ICONV_CONST char *inptr;
            char *outptr;

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

    return lobby_send_pkt_dc(c->cur_lobby, c, (dc_pkt_hdr_t *)pkt);
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

    /* Sanity check... Make sure the size of the subcommand and the client id
       match with what we expect. Disconnect the client if not. */
    if(pkt->size != 0x07 || pkt->client_id != c->client_id) {
        return -1;
    }

    /* If we're in legit mode, we need to check the newly taken item. */
    if((l->flags & LOBBY_FLAG_LEGIT_MODE) && c->cur_ship->limits) {
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

        if(!sylverant_limits_check_item(c->cur_ship->limits, &item, v)) {
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
    return lobby_send_pkt_dc(c->cur_lobby, c, (dc_pkt_hdr_t *)pkt);
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
    if((l->flags & LOBBY_FLAG_LEGIT_MODE) && c->cur_ship->limits) {
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

        if(!sylverant_limits_check_item(c->cur_ship->limits, &item, v)) {
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
                                    "detected"), name);
                    }
                    else {
                        send_txt(c2, "%s",
                                 __(c2, "\tE\tC7Potentially hacked drop\n"
                                    "detected"));
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
    return lobby_send_pkt_dc(c->cur_lobby, c, (dc_pkt_hdr_t *)pkt);
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
        return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt);
    }

    /* This aught to do it... */
    lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt);
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
        return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt);
    }

    /* This aught to do it... */
    lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt);
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

    return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt);
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

    return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt);
}

static int handle_move(ship_client_t *c, subcmd_move_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Save the new position and move along */
    if(c->client_id == pkt->client_id) {
        c->x = pkt->x;
        c->z = pkt->z;
    }

    return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt);
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

    if(!(l->flags & LOBBY_FLAG_SINGLEPLAYER) &&
       pkt->client_id != c->client_id) {
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

    return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt);
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
    l->highest_item[c->client_id] = (uint16_t)LE32(pkt->item_id);
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
    return lobby_send_pkt_dc(c->cur_lobby, c, (dc_pkt_hdr_t *)pkt);
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

    if(!(l->flags & LOBBY_FLAG_SINGLEPLAYER) &&
       pkt->client_id != c->client_id) {
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

    return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt);
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
            return word_select_send_gc(c, pkt);
    }

    return 0;
}

static int handle_symbol_chat(ship_client_t *c, subcmd_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;

    /* Don't send chats for a STFUed client. */
    if((c->flags & CLIENT_FLAG_STFU)) {
        return 0;
    }

    return lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt);
}

/* Handle a 0x62/0x6D packet. */
int subcmd_handle_one(ship_client_t *c, subcmd_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;
    ship_client_t *dest;
    uint8_t type = pkt->type;
    int rv = -1;

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
            switch(c->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                    rv = handle_dc_gcsend(dest, (subcmd_dc_gcsend_t *)pkt);
                    break;

                case CLIENT_VERSION_GC:
                    rv = handle_gc_gcsend(dest, (subcmd_gc_gcsend_t *)pkt);
                    break;

                case CLIENT_VERSION_PC:
                    rv = handle_pc_gcsend(dest, (subcmd_pc_gcsend_t *)pkt);
                    break;
            }
            break;

        case SUBCMD_ITEMREQ:
            /* Only pay attention if an item has been set and we're not in
               legit mode. */
            if(c->next_item[0] && !(l->flags & LOBBY_FLAG_LEGIT_MODE)) {
                rv = handle_itemreq(c, (subcmd_itemreq_t *)pkt);
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

/* Handle a 0x60 packet. */
int subcmd_handle_bcast(ship_client_t *c, subcmd_pkt_t *pkt) {
    uint8_t type = pkt->type;
    lobby_t *l = c->cur_lobby;
    int rv, sent = 1;

    pthread_mutex_lock(&l->mutex);

    /* If there's a burst going on in the lobby, delay most packets */
    if(l->flags & LOBBY_FLAG_BURSTING) {
        switch(type) {
            case SUBCMD_UNK_3B:
            case SUBCMD_UNK_7C:
            case SUBCMD_BURST_DONE:
                rv = lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt);
                break;

            case SUBCMD_SET_AREA:
                rv = handle_set_area(c, (subcmd_set_area_t *)pkt);
                break;

            case SUBCMD_SET_POS_3F:
                rv = handle_set_pos(c, (subcmd_set_pos_t *)pkt);
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

        default:
#ifdef LOG_UNKNOWN_SUBS
            debug(DBG_LOG, "Unknown 0x60: 0x%02X\n", type);
            print_packet((unsigned char *)pkt, LE16(pkt->hdr.dc.pkt_len));
#endif /* LOG_UNKNOWN_SUBS */
            sent = 0;
    }

    /* Broadcast anything we don't care to check anything about. */
    if(!sent) {
        rv = lobby_send_pkt_dc(l, c, (dc_pkt_hdr_t *)pkt);
    }

    pthread_mutex_unlock(&l->mutex);
    return rv;
}

/* Handle a 0xC9/0xCB packet. */
int subcmd_handle_ep3_bcast(ship_client_t *c, subcmd_pkt_t *pkt) {
    lobby_t *l = c->cur_lobby;
    int rv;

    pthread_mutex_lock(&l->mutex);

    /* We don't do anything special with these just yet... */
    rv = lobby_send_pkt_ep3(l, c, (dc_pkt_hdr_t *)pkt);

    pthread_mutex_unlock(&l->mutex);
    return rv;
}
