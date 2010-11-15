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

#ifndef SHIP_PACKETS_H
#define SHIP_PACKETS_H

#include <inttypes.h>
#include <netinet/in.h>

#include <sylverant/encryption.h>
#include <sylverant/quest.h>

#include "clients.h"
#include "player.h"
#include "ship.h"

/* Pull in the structures for the packets themselves. */
#include "packets.h"

#ifdef __GNUC__
#ifndef __printflike
#define __printflike(n1, n2) __attribute__((format(printf, n1, n2)))
#endif
#else
#ifndef __printflike
#define __printflike(n1, n2)
#endif
#endif

/* This must be placed into the copyright field in the DC welcome packet. */
const static char dc_welcome_copyright[] =
    "DreamCast Lobby Server. Copyright SEGA Enterprises. 1999";

/* Retrieve the thread-specific sendbuf for the current thread. */
uint8_t *get_sendbuf();

/* Send a Dreamcast welcome packet to the given client. */
int send_dc_welcome(ship_client_t *c, uint32_t svect, uint32_t cvect);

/* Send the Dreamcast security packet to the given client. */
int send_dc_security(ship_client_t *c, uint32_t gc, uint8_t *data,
                     int data_len);

/* Send a redirect packet to the given client. */
int send_redirect(ship_client_t *c, in_addr_t ip, uint16_t port);

/* Send a timestamp packet to the given client. */
int send_timestamp(ship_client_t *c);

/* Send the list of blocks to the client. */
int send_block_list(ship_client_t *c, ship_t *s);

/* Send a block/ship information reply packet to the client. */
int send_info_reply(ship_client_t *c, const char *msg);

/* Send a simple (header-only) packet to the client */
int send_simple(ship_client_t *c, int type, int flags);

/* Send the lobby list packet to the client. */
int send_lobby_list(ship_client_t *c);

/* Send the packet to join a lobby to the client. */
int send_lobby_join(ship_client_t *c, lobby_t *l);

/* Send a prepared packet to the given client. */
int send_pkt_dc(ship_client_t *c, dc_pkt_hdr_t *pkt);

/* Send a packet to all clients in the lobby when a new player joins. */
int send_lobby_add_player(lobby_t *l, ship_client_t *c);

/* Send a packet to all clients in the lobby when a player leaves. */
int send_lobby_leave(lobby_t *l, ship_client_t *c, int client_id);

/* Send a chat packet to the specified lobby. */
int send_lobby_chat(lobby_t *l, ship_client_t *sender, char msg[]);

/* Send a chat packet to the specified lobby (UTF-16). */
int send_lobby_wchat(lobby_t *l, ship_client_t *sender, uint16_t *msg,
                     size_t len);

/* Send a guild card search reply to the specified client. */
int send_guild_reply(ship_client_t *c, uint32_t gc, in_addr_t ip, uint16_t port,
                     char game[], int block, char ship[], uint32_t lobby,
                     char name[]);

/* Send a message to the client. */
int send_message1(ship_client_t *c, const char *fmt, ...) __printflike(2, 3);

/* Send a text message to the client (i.e, for stuff related to commands). */
int send_txt(ship_client_t *c, const char *fmt, ...) __printflike(2, 3);

/* Send a packet to the client indicating information about the game they're
   joining. */
int send_game_join(ship_client_t *c, lobby_t *l);

/* Send a packet to a client giving them the list of games on the block. */
int send_game_list(ship_client_t *c, block_t *b);

/* Send a packet containing the lobby info menu to the client. */
int send_info_list(ship_client_t *c, ship_t *s);

/* Send a message box packet to the client. */
int send_message_box(ship_client_t *c, const char *fmt, ...) __printflike(2, 3);

/* Send the list of quest categories to the client. */
int send_quest_categories(ship_client_t *c, sylverant_quest_list_t *l);

/* Send the list of quests in a category to the client. */
int send_quest_list(ship_client_t *c, int cat, sylverant_quest_category_t *l);

/* Send information about a quest to the lobby. */
int send_quest_info(lobby_t *l, sylverant_quest_t *q);

/* Send a quest to everyone in a lobby. */
int send_quest(lobby_t *l, sylverant_quest_t *q);

/* Send the lobby name to the client. */
int send_lobby_name(ship_client_t *c, lobby_t *l);

/* Send a packet to all clients in the lobby letting them know about a change to
   the arrows displayed. */
int send_lobby_arrows(lobby_t *l);

/* Send a packet to ONE client letting them know about the arrow colors in the
   given lobby. */
int send_arrows(ship_client_t *c, lobby_t *l);

/* Send a ship list packet to the client. */
int send_ship_list(ship_client_t *c, ship_t *s, uint16_t menu_code);

/* Send a warp command to the client. */
int send_warp(ship_client_t *c, uint8_t area);

/* Send a warp command to a whole lobby (DCv2 and higher only). */
int send_lobby_warp(lobby_t *l, uint8_t area);

/* Send the choice search option list to the client. */
int send_choice_search(ship_client_t *c);

/* Send a reply to a choice search to the client. */
int send_choice_reply(ship_client_t *c, dc_choice_set_t *search);

/* Send a premade guild card search reply to the specified client. */
int send_guild_reply_sg(ship_client_t *c, dc_guild_reply_pkt *pkt);

/* Send a simple mail packet, doing any needed transformations. */
int send_simple_mail(int version, ship_client_t *c, dc_pkt_hdr_t *pkt);

/* Send the lobby's info board to the client. */
int send_infoboard(ship_client_t *c, lobby_t *l);

/* Send the lobby's C-Rank data to the client. */
int send_lobby_c_rank(ship_client_t *c, lobby_t *l);

/* Send a C-Rank update for a single client to the whole lobby. */
int send_c_rank_update(ship_client_t *c, lobby_t *l);

/* This is a special case of the information select menu for PSOPC. This allows
   the user to pick to make a V1 compatible game or not. */
int send_pc_game_type_sel(ship_client_t *c);

/* Send a statistics mod packet to the lobby. */
int send_lobby_mod_stat(lobby_t *l, ship_client_t *c, int stat, int amt);

#endif /* !SHIP_PACKETS_H */
