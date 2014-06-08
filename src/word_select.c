/*
    Sylverant Ship Server
    Copyright (C) 2010, 2011, 2012, 2014 Lawrence Sebald

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

#include "clients.h"
#include "lobby.h"
#include "subcmd.h"
#include "utils.h"
#include "ship_packets.h"
#include "word_select.h"
#include "word_select-dc.h"
#include "word_select-pc.h"
#include "word_select-gc.h"

int word_select_send_dc(ship_client_t *c, subcmd_word_select_t *pkt) {
    lobby_t *l = c->cur_lobby;
    int i;
    subcmd_word_select_t pc, gc;
    subcmd_bb_word_select_t bb;
    int pcusers = 0, gcusers = 0;
    int pcuntrans = 0, gcuntrans = 0;
    uint16_t dcw, pcw, gcw;

    /* Fill in the translated packets */
    pc.hdr.pkt_type = GAME_COMMAND0_TYPE;
    pc.hdr.flags = pkt->hdr.flags;
    pc.hdr.pkt_len = LE16(0x0024);
    pc.type = SUBCMD_WORD_SELECT;
    pc.size = 0x08;
    pc.client_id = pkt->client_id;
    pc.client_id_gc = 0;
    pc.num_words = pkt->num_words;
    pc.unused1 = 0;
    pc.ws_type = pkt->ws_type;
    pc.unused2 = 0;

    gc.hdr.pkt_type = GAME_COMMAND0_TYPE;
    gc.hdr.flags = pkt->hdr.flags;
    gc.hdr.pkt_len = LE16(0x0024);
    gc.type = SUBCMD_WORD_SELECT;
    gc.size = 0x08;
    gc.client_id = 0;
    gc.client_id_gc = pkt->client_id;
    gc.num_words = pkt->num_words;
    gc.unused1 = 0;
    gc.ws_type = pkt->ws_type;
    gc.unused2 = 0;

    bb.hdr.pkt_type = LE16(GAME_COMMAND0_TYPE);
    bb.hdr.flags = LE32(pkt->hdr.flags);
    bb.hdr.pkt_len = LE16(0x0028);
    bb.type = SUBCMD_WORD_SELECT;
    bb.size = 0x08;
    bb.client_id = pkt->client_id;
    bb.client_id_gc = 0;
    bb.num_words = pkt->num_words;
    bb.unused1 = 0;
    bb.ws_type = pkt->ws_type;
    bb.unused2 = 0;

    /* No versions other than PSODC sport the lovely LIST ALL menu. Oh well, I
       guess I can't go around saying "HELL HELL HELL" to everyone. */
    if(pkt->ws_type == 6) {
        pcuntrans = 1;
        gcuntrans = 1;
    }

    for(i = 0; i < 8; ++i) {
        dcw = LE16(pkt->words[i]);

        /* Make sure each word is valid */
        if(dcw > WORD_SELECT_DC_MAX && dcw != 0xFFFF) {
            return send_txt(c, __(c, "\tE\tC7Invalid word select."));
        }

        /* Grab the words from the map */
        if(dcw != 0xFFFF) {
            pcw = word_select_dc_map[dcw][0];
            gcw = word_select_dc_map[dcw][1];
        }
        else {
            pcw = gcw = 0xFFFF;
        }

        /* See if we have an untranslateable word */
        if(pcw == 0xFFFF && dcw != 0xFFFF) {
            pcuntrans = 1;
        }

        if(gcw == 0xFFFF && dcw != 0xFFFF) {
            gcuntrans = 1;
        }

        /* Throw them into the packets */
        pc.words[i] = LE16(pcw);
        gc.words[i] = LE16(gcw);
        bb.words[i] = LE16(gcw);
    }

    /* Deal with amounts and such... */
    pc.words[8] = pkt->words[8];
    pc.words[9] = pkt->words[9];
    pc.words[10] = pkt->words[10];
    pc.words[11] = pkt->words[11];
    gc.words[8] = pkt->words[8];
    gc.words[9] = pkt->words[9];
    gc.words[10] = pkt->words[10];
    gc.words[11] = pkt->words[11];
    bb.words[8] = pkt->words[8];
    bb.words[9] = pkt->words[9];
    bb.words[10] = pkt->words[10];
    bb.words[11] = pkt->words[11];

    /* Send the packet to everyone we can */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] && l->clients[i] != c &&
           !client_has_ignored(l->clients[i], c->guildcard)) {
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                    send_pkt_dc(l->clients[i], (dc_pkt_hdr_t *)pkt);
                    break;

                case CLIENT_VERSION_PC:
                    if(!pcuntrans) {
                        send_pkt_dc(l->clients[i], (dc_pkt_hdr_t *)&pc);
                    }

                    pcusers = 1;
                    break;

                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_EP3:
                    if(!gcuntrans) {
                        send_pkt_dc(l->clients[i], (dc_pkt_hdr_t *)&gc);
                    }

                    gcusers = 1;
                    break;

                case CLIENT_VERSION_BB:
                    if(!gcuntrans) {
                        send_pkt_bb(l->clients[i], (bb_pkt_hdr_t *)&bb);
                    }

                    gcusers = 1;
                    break;
            }
        }
    }

    /* See if we had anyone that we couldn't send it to */
    if((pcusers && pcuntrans) || (gcusers && gcuntrans)) {
        send_txt(c, __(c, "\tE\tC7Some clients did not\n"
                          "receive your last word\nselect."));
    }

    return 0;
}

int word_select_send_pc(ship_client_t *c, subcmd_word_select_t *pkt) {
    lobby_t *l = c->cur_lobby;
    int i;
    subcmd_word_select_t dc, gc;
    subcmd_bb_word_select_t bb;
    int dcusers = 0, gcusers = 0;
    int dcuntrans = 0, gcuntrans = 0;
    uint16_t dcw, pcw, gcw;

    /* Fill in the translated packets */
    dc.hdr.pkt_type = GAME_COMMAND0_TYPE;
    dc.hdr.flags = pkt->hdr.flags;
    dc.hdr.pkt_len = LE16(0x0024);
    dc.type = SUBCMD_WORD_SELECT;
    dc.size = 0x08;
    dc.client_id = pkt->client_id;
    dc.client_id_gc = 0;
    dc.num_words = pkt->num_words;
    dc.unused1 = 0;
    dc.ws_type = pkt->ws_type;
    dc.unused2 = 0;

    gc.hdr.pkt_type = GAME_COMMAND0_TYPE;
    gc.hdr.flags = pkt->hdr.flags;
    gc.hdr.pkt_len = LE16(0x0024);
    gc.type = SUBCMD_WORD_SELECT;
    gc.size = 0x08;
    gc.client_id = 0;
    gc.client_id_gc = pkt->client_id;
    gc.num_words = pkt->num_words;
    gc.unused1 = 0;
    gc.ws_type = pkt->ws_type;
    gc.unused2 = 0;

    bb.hdr.pkt_type = LE16(GAME_COMMAND0_TYPE);
    bb.hdr.flags = LE32(pkt->hdr.flags);
    bb.hdr.pkt_len = LE16(0x0028);
    bb.type = SUBCMD_WORD_SELECT;
    bb.size = 0x08;
    bb.client_id = pkt->client_id;
    bb.client_id_gc = 0;
    bb.num_words = pkt->num_words;
    bb.unused1 = 0;
    bb.ws_type = pkt->ws_type;
    bb.unused2 = 0;

    for(i = 0; i < 8; ++i) {
        pcw = LE16(pkt->words[i]);

        /* Make sure each word is valid */
        if(pcw > WORD_SELECT_PC_MAX && pcw != 0xFFFF) {
            return send_txt(c, __(c, "\tE\tC7Invalid word select."));
        }

        /* Grab the words from the map */
        if(pcw != 0xFFFF && pcw <= WORD_SELECT_PC_MAX) {
            dcw = word_select_pc_map[pcw][0];
            gcw = word_select_pc_map[pcw][1];
        }
        else {
            dcw = gcw = 0xFFFF;
        }

        /* See if we have an untranslateable word */
        if(dcw == 0xFFFF && pcw != 0xFFFF) {
            dcuntrans = 1;
        }

        if(gcw == 0xFFFF && pcw != 0xFFFF) {
            gcuntrans = 1;
        }

        /* Throw them into the packets */
        dc.words[i] = LE16(dcw);
        gc.words[i] = LE16(gcw);
        bb.words[i] = LE16(gcw);
    }

    /* Deal with amounts and such... */
    dc.words[8] = pkt->words[8];
    dc.words[9] = pkt->words[9];
    dc.words[10] = pkt->words[10];
    dc.words[11] = pkt->words[11];
    gc.words[8] = pkt->words[8];
    gc.words[9] = pkt->words[9];
    gc.words[10] = pkt->words[10];
    gc.words[11] = pkt->words[11];
    bb.words[8] = pkt->words[8];
    bb.words[9] = pkt->words[9];
    bb.words[10] = pkt->words[10];
    bb.words[11] = pkt->words[11];

    /* Send the packet to everyone we can */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] && l->clients[i] != c &&
           !client_has_ignored(l->clients[i], c->guildcard)) {
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                    if(!dcuntrans) {
                        send_pkt_dc(l->clients[i], (dc_pkt_hdr_t *)&dc);
                    }

                    dcusers = 1;
                    break;

                case CLIENT_VERSION_PC:
                    send_pkt_dc(l->clients[i], (dc_pkt_hdr_t *)pkt);
                    break;

                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_EP3:
                    if(!gcuntrans) {
                        send_pkt_dc(l->clients[i], (dc_pkt_hdr_t *)&gc);
                    }

                    gcusers = 1;
                    break;

                case CLIENT_VERSION_BB:
                    if(!gcuntrans) {
                        send_pkt_bb(l->clients[i], (bb_pkt_hdr_t *)&bb);
                    }

                    gcusers = 1;
                    break;
            }
        }
    }

    /* See if we had anyone that we couldn't send it to */
    if((dcusers && dcuntrans) || (gcusers && gcuntrans)) {
        send_txt(c, __(c, "\tE\tC7Some clients did not\n"
                       "receive your last word\nselect."));
    }

    return 0;
}

int word_select_send_gc(ship_client_t *c, subcmd_word_select_t *pkt) {
    lobby_t *l = c->cur_lobby;
    int i;
    subcmd_word_select_t pc, dc;
    subcmd_bb_word_select_t bb;
    int pcusers = 0, dcusers = 0;
    int pcuntrans = 0, dcuntrans = 0;
    uint16_t dcw, pcw, gcw;

    /* Fill in the translated packets */
    pc.hdr.pkt_type = GAME_COMMAND0_TYPE;
    pc.hdr.flags = pkt->hdr.flags;
    pc.hdr.pkt_len = LE16(0x0024);
    pc.type = SUBCMD_WORD_SELECT;
    pc.size = 0x08;
    pc.client_id = pkt->client_id_gc;
    pc.client_id_gc = 0;
    pc.num_words = pkt->num_words;
    pc.unused1 = 0;
    pc.ws_type = pkt->ws_type;
    pc.unused2 = 0;

    dc.hdr.pkt_type = GAME_COMMAND0_TYPE;
    dc.hdr.flags = pkt->hdr.flags;
    dc.hdr.pkt_len = LE16(0x0024);
    dc.type = SUBCMD_WORD_SELECT;
    dc.size = 0x08;
    dc.client_id = pkt->client_id_gc;
    dc.client_id_gc = 0;
    dc.num_words = pkt->num_words;
    dc.unused1 = 0;
    dc.ws_type = pkt->ws_type;
    dc.unused2 = 0;

    bb.hdr.pkt_type = LE16(GAME_COMMAND0_TYPE);
    bb.hdr.flags = LE32(pkt->hdr.flags);
    bb.hdr.pkt_len = LE16(0x0028);
    bb.type = SUBCMD_WORD_SELECT;
    bb.size = 0x08;
    bb.client_id = pkt->client_id_gc;
    bb.client_id_gc = 0;
    bb.num_words = pkt->num_words;
    bb.unused1 = 0;
    bb.ws_type = pkt->ws_type;
    bb.unused2 = 0;

    for(i = 0; i < 8; ++i) {
        gcw = LE16(pkt->words[i]);

        /* Make sure each word is valid */
        if(gcw > WORD_SELECT_GC_MAX && gcw != 0xFFFF) {
            return send_txt(c, __(c, "\tE\tC7Invalid word select."));
        }

        /* Grab the words from the map */
        if(gcw != 0xFFFF) {
            dcw = word_select_gc_map[gcw][0];
            pcw = word_select_gc_map[gcw][1];
        }
        else {
            pcw = dcw = 0xFFFF;
        }

        /* See if we have an untranslateable word */
        if(pcw == 0xFFFF && gcw != 0xFFFF) {
            pcuntrans = 1;
        }

        if(dcw == 0xFFFF && gcw != 0xFFFF) {
            dcuntrans = 1;
        }

        /* Throw them into the packets */
        pc.words[i] = LE16(pcw);
        dc.words[i] = LE16(dcw);
        bb.words[i] = LE16(gcw);
    }

    /* Deal with amounts and such... */
    dc.words[8] = pkt->words[8];
    dc.words[9] = pkt->words[9];
    dc.words[10] = pkt->words[10];
    dc.words[11] = pkt->words[11];
    pc.words[8] = pkt->words[8];
    pc.words[9] = pkt->words[9];
    pc.words[10] = pkt->words[10];
    pc.words[11] = pkt->words[11];
    bb.words[8] = pkt->words[8];
    bb.words[9] = pkt->words[9];
    bb.words[10] = pkt->words[10];
    bb.words[11] = pkt->words[11];

    /* Send the packet to everyone we can */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i] && l->clients[i] != c &&
           !client_has_ignored(l->clients[i], c->guildcard)) {
            switch(l->clients[i]->version) {
                case CLIENT_VERSION_DCV1:
                case CLIENT_VERSION_DCV2:
                    if(!dcuntrans) {
                        send_pkt_dc(l->clients[i], (dc_pkt_hdr_t *)&dc);
                    }

                    dcusers = 1;
                    break;

                case CLIENT_VERSION_PC:
                    if(!pcuntrans) {
                        send_pkt_dc(l->clients[i], (dc_pkt_hdr_t *)&pc);
                    }

                    pcusers = 1;
                    break;

                case CLIENT_VERSION_GC:
                case CLIENT_VERSION_EP3:
                    send_pkt_dc(l->clients[i], (dc_pkt_hdr_t *)pkt);
                    break;

                case CLIENT_VERSION_BB:
                    send_pkt_bb(l->clients[i], (bb_pkt_hdr_t *)&bb);
                    break;
            }
        }
    }

    /* See if we had anyone that we couldn't send it to */
    if((pcusers && pcuntrans) || (dcusers && dcuntrans)) {
        send_txt(c, __(c, "\tE\tC7Some clients did not\n"
                       "receive your last word\nselect."));
    }

    return 0;
}
