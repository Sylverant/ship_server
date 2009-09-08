/*
    Sylverant Ship Server
    Copyright (C) 2009 Lawrence Sebald

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3 as
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SHIPPACKETS_H
#define SHIPPACKETS_H

#include <inttypes.h>
#include <netinet/in.h>

#include <sylverant/characters.h>
#include <sylverant/encryption.h>
#include <sylverant/quest.h>

#include "clients.h"
#include "player.h"

#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__)
#define LE16(x) (((x >> 8) & 0xFF) | ((x & 0xFF) << 8))
#define LE32(x) (((x >> 24) & 0x00FF) | \
                 ((x >>  8) & 0xFF00) | \
                 ((x & 0xFF00) <<  8) | \
                 ((x & 0x00FF) << 24))
#define LE64(x) (((x >> 56) & 0x000000FF) | \
                 ((x >> 40) & 0x0000FF00) | \
                 ((x >> 24) & 0x00FF0000) | \
                 ((x >>  8) & 0xFF000000) | \
                 ((x & 0xFF000000) <<  8) | \
                 ((x & 0x00FF0000) << 24) | \
                 ((x & 0x0000FF00) << 40) | \
                 ((x & 0x000000FF) << 56))
#else
#define LE16(x) x
#define LE32(x) x
#define LE64(x) x
#endif

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

/* The welcome packet for setting up encryption keys (Dreamcast). */
typedef struct dc_welcome {
    dc_pkt_hdr_t hdr;
    char copyright[0x40];
    uint32_t svect;
    uint32_t cvect;
} PACKED dc_welcome_pkt;

/* The menu selection packet that the client sends to us (Dreamcast). */
typedef struct dc_select {
    dc_pkt_hdr_t hdr;
    uint32_t menu_id;
    uint32_t item_id;
} PACKED dc_select_pkt;

/* The login packet that the client sends to us (Dreamcast V1). */
typedef struct dc_login {
    dc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t guildcard;
    uint32_t unk[4];
    char serial[8];
    uint8_t padding1[9];
    char access_key[8];
    uint8_t padding2[9];
    char dc_id[8];
    uint8_t padding3[88];
    char name[16];
    uint8_t padding4[2];
    uint8_t sec_data[0];
} PACKED dc_login_pkt;

/* The login packet that the client sends to us (Dreamcast V2). */
typedef struct dcv2_login {
    dc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t guildcard;
    uint32_t unk[4];
    uint8_t padding1[32];
    char serial[8];
    uint8_t padding2[8];
    char access_key[8];
    uint8_t padding3[8];
    char dc_id[8];
    uint8_t padding4[88];
    uint16_t unk2;
    uint8_t padding5[14];
    uint8_t sec_data[0];
} PACKED dcv2_login_pkt;

/* The packet sent to redirect clients (Dreamcast). */
typedef struct dc_redirect {
    dc_pkt_hdr_t hdr;
    uint32_t ip_addr;       /* Big-endian */
    uint16_t port;          /* Little-endian */
    uint8_t padding2[2];
} PACKED dc_redirect_pkt;

/* The packet sent as a timestamp (Dreamcast). */
typedef struct dc_timestamp {
    dc_pkt_hdr_t hdr;
    char timestamp[28];
} PACKED dc_timestamp_pkt;

/* The packet sent to inform clients of their security data (Dreamcast). */
typedef struct dc_security {
    dc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t guildcard;
    uint8_t security_data[0];
} PACKED dc_security_pkt;

/* The packet used for the information reply on the Dreamcast version. */
typedef struct dc_info_reply {
    dc_pkt_hdr_t hdr;
    uint32_t odd[2];
    char msg[];
} PACKED dc_info_reply_pkt;

/* The ship list packet send to tell clients what blocks are up (Dreamcast). */
typedef struct dc_block_list {
    dc_pkt_hdr_t hdr;           /* The flags field says the entry count */
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint16_t flags;
        char name[0x12];
    } entries[0];
} PACKED dc_block_list_pkt;

/* The lobby list packet sent to clients (Dreamcast). */
typedef struct dc_lobby_list {
    dc_pkt_hdr_t hdr;           /* The flags field says the entry count */
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint32_t padding;
    } entries[16];
} PACKED dc_lobby_list_pkt;

/* The packet sent by clients to send their character data (Dreamcast). */
typedef struct dc_char_data {
    dc_pkt_hdr_t hdr;
    player_t data;
} PACKED dc_char_data_pkt;

/* The packet sent to clients when they join a lobby (Dreamcast). */
typedef struct dc_lobby_join {
    dc_pkt_hdr_t hdr;
    uint8_t client_id;
    uint8_t leader_id;
    uint8_t one;                        /* Always 1 */
    uint8_t lobby_num;
    uint16_t block_num;
    uint16_t event;
    uint32_t padding;
    struct {
        dc_player_hdr_t hdr;
        player_t data;
    } entries[0];
} PACKED dc_lobby_join_pkt;

/* The packet sent to clients when they leave a lobby (Dreamcast). */
typedef struct dc_lobby_leave {
    dc_pkt_hdr_t hdr;
    uint8_t client_id;
    uint8_t leader_id;
    uint16_t padding;
} PACKED dc_lobby_leave_pkt;

/* The packet sent from/to clients for sending a normal chat (Dreamcast). */
typedef struct dc_chat {
    dc_pkt_hdr_t hdr;
    uint32_t padding;
    uint32_t guildcard;
    char msg[0];
} PACKED dc_chat_pkt;

/* The packet sent to search for a player (Dreamcast). */
typedef struct dc_guild_search {
    dc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t gc_search;
    uint32_t gc_target;
} PACKED dc_guild_search_pkt;

/* The packet sent to reply to a guild card search (Dreamcast). */
typedef struct dc_guild_reply {
    dc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t gc_search;
    uint32_t gc_target;
    uint32_t padding1;
    in_addr_t ip;
    uint16_t port;
    uint16_t padding2;
    char location[0x44];
    uint32_t menu_id;
    uint32_t item_id;
    char padding3[0x3C];
    char name[0x20];
} PACKED dc_guild_reply_pkt;

/* The packet sent to send/deliver simple mail (Dreamcast). */
typedef struct dc_simple_mail {
    dc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t gc_sender;
    char name[16];
    uint32_t gc_dest;
    char stuff[0x200];
} PACKED dc_simple_mail_pkt;

/* The packet sent by clients to create a game (Dreamcast). */
typedef struct dc_game_create {
    dc_pkt_hdr_t hdr;
    uint32_t unused[2];
    char name[16];
    char password[16];
    uint8_t difficulty;
    uint8_t battle;
    uint8_t challenge;
    uint8_t version;                    /* Set to 1 for v2 games, 0 otherwise */
} PACKED dc_game_create_pkt;

/* The packet sent to clients to join a game (Dreamcast). */
typedef struct dc_game_join {
    dc_pkt_hdr_t hdr;
    uint32_t maps[0x20];
    dc_player_hdr_t players[4];
    uint8_t client_id;
    uint8_t leader_id;
    uint8_t one;                        /* Always 1. */
    uint8_t difficulty;
    uint8_t battle;
    uint8_t event;
    uint8_t section;
    uint8_t challenge;
    uint32_t game_id;
} PACKED dc_game_join_pkt;

/* The packet sent to clients to give them the game select list (Dreamcast). */
typedef struct dc_game_list {
    dc_pkt_hdr_t hdr;
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint8_t difficulty;
        uint8_t players;
        char name[16];
        uint8_t v2;
        uint8_t flags;
    } entries[0];
} PACKED dc_game_list_pkt;

/* The packet sent to display a large message to the user (Dreamcast). */
typedef struct dc_msg_box {
    dc_pkt_hdr_t hdr;
    char msg[0];
} PACKED dc_msg_box_pkt;

/* The packet used to send the quest list (Dreamcast). */
typedef struct dc_quest_list {
    dc_pkt_hdr_t hdr;
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        char name[32];
        char desc[112];
    } entries[0];
} dc_quest_list_pkt;

/* The packet sent to inform a client of a quest file that will be coming down
   the pipe (Dreamcast). */
typedef struct dc_quest_file {
    dc_pkt_hdr_t hdr;
    char name[32];
    uint8_t unused1[3];
    char filename[16];
    uint8_t unused2;
    uint32_t length;
} dc_quest_file_pkt;

/* The packet sent to actually send quest data (Dreamcast). */
typedef struct dc_quest_chunk {
    dc_pkt_hdr_t hdr;
    char filename[16];
    char data[1024];
    uint32_t length;
} dc_quest_chunk_pkt;

/* The packet sent to update the list of arrows in a lobby (Dreamcast). */
typedef struct dc_arrow_list {
    dc_pkt_hdr_t hdr;
    struct {
        uint32_t tag;
        uint32_t guildcard;
        uint32_t arrow;
    } entries[0];
} dc_arrow_list_pkt;

/* The ship list packet send to tell clients what ships are up (Dreamcast). */
typedef struct dc_ship_list {
    dc_pkt_hdr_t hdr;           /* The flags field says how entries are below */
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint16_t flags;
        char name[0x12];
    } entries[0];
} PACKED dc_ship_list_pkt;

#undef PACKED

/* Parameters for the various packets. */
#define SHIP_MSG1_TYPE                      0x0001
#define SHIP_DC_WELCOME_TYPE                0x0002
#define SHIP_DC_SECURITY_TYPE               0x0004
#define SHIP_TYPE_05                        0x0005
#define SHIP_CHAT_TYPE                      0x0006
#define SHIP_DC_BLOCK_LIST_TYPE             0x0007
#define SHIP_GAME_LIST_TYPE                 0x0008
#define SHIP_INFO_REQUEST_TYPE              0x0009
#define SHIP_DC_GAME_CREATE_TYPE            0x000C
#define SHIP_MENU_SELECT_TYPE               0x0010
#define SHIP_INFO_REPLY_TYPE                0x0011
#define SHIP_QUEST_CHUNK_TYPE               0x0013
#define SHIP_REDIRECT_TYPE                  0x0019
#define SHIP_MSG_BOX_TYPE                   0x001A
#define SHIP_LOBBY_INFO_TYPE                0x001F
#define SHIP_GUILD_SEARCH_TYPE              0x0040
#define SHIP_DC_GUILD_REPLY_TYPE            0x0041
#define SHIP_QUEST_FILE_TYPE                0x0044
#define SHIP_GAME_COMMAND0_TYPE             0x0060
#define SHIP_DC_CHAR_DATA_TYPE              0x0061
#define SHIP_GAME_COMMAND2_TYPE             0x0062
#define SHIP_GAME_JOIN_TYPE                 0x0064
#define SHIP_GAME_ADD_PLAYER_TYPE           0x0065
#define SHIP_GAME_LEAVE_TYPE                0x0066
#define SHIP_LOBBY_JOIN_TYPE                0x0067
#define SHIP_LOBBY_ADD_PLAYER_TYPE          0x0068
#define SHIP_LOBBY_LEAVE_TYPE               0x0069
#define SHIP_GAME_COMMANDD_TYPE             0x006D
#define SHIP_DONE_BURSTING_TYPE             0x006F
#define SHIP_SIMPLE_MAIL_TYPE               0x0081
#define SHIP_LOBBY_LIST_TYPE                0x0083
#define SHIP_LOBBY_CHANGE_TYPE              0x0084
#define SHIP_LOBBY_ARROW_LIST_TYPE          0x0088
#define SHIP_LOBBY_ARROW_CHANGE_TYPE        0x0089
#define SHIP_LOBBY_NAME_TYPE                0x008A
#define SHIP_LOGIN_TYPE                     0x0093
#define SHIP_LEAVE_GAME_PL_DATA_TYPE        0x0098
#define SHIP_CHAR_DATA_REQUEST_TYPE         0x0095
#define SHIP_DCV2_LOGIN_TYPE                0x009D
#define SHIP_SHIP_LIST_TYPE                 0x00A0
#define SHIP_BLOCK_LIST_REQ_TYPE            0x00A1
#define SHIP_QUEST_LIST_TYPE                0x00A2
#define SHIP_QUEST_INFO_TYPE                0x00A3
#define SHIP_QUEST_END_LIST_TYPE            0x00A9
#define SHIP_TEXT_MSG_TYPE                  0x00B0
#define SHIP_TIMESTAMP_TYPE                 0x00B1
#define SHIP_GAME_CREATE_TYPE               0x00C1

#define SHIP_DC_WELCOME_LENGTH              0x004C
#define SHIP_DC_REDIRECT_LENGTH             0x000C
#define SHIP_DC_TIMESTAMP_LENGTH            0x0020
#define SHIP_DC_LOBBY_LIST_LENGTH           0x00C4
#define SHIP_DC_CHAR_DATA_LENGTH            0x0420
#define SHIP_DC_LOBBY_LEAVE_LENGTH          0x0008
#define SHIP_DC_GUILD_REPLY_LENGTH          0x00C4
#define SHIP_DC_GAME_JOIN_LENGTH            0x0110
#define SHIP_DC_QUEST_INFO_LENGTH           0x0128
#define SHIP_DC_QUEST_FILE_LENGTH           0x003C
#define SHIP_DC_QUEST_CHUNK_LENGTH          0x0418

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
int send_info_reply(ship_client_t *c, char msg[]);

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

/* Send a guild card search reply to the specified client. */
int send_guild_reply(ship_client_t *c, uint32_t gc, in_addr_t ip, uint16_t port,
                     char game[], int block, char ship[], uint32_t lobby,
                     char name[]);

/* Send a message to the client. */
int send_message1(ship_client_t *c, char msg[]);

/* Send a text message to the client (i.e, for stuff related to commands). */
int send_txt(ship_client_t *c, char msg[]);

/* Send a packet to the client indicating information about the game they're
   joining. */
int send_game_join(ship_client_t *c, lobby_t *l);

/* Send a packet to all clients in the lobby letting them know the new player
   has finished bursting. */
int send_lobby_done_burst(lobby_t *l, ship_client_t *c);

/* Send a packet to a client giving them the list of games on the block. */
int send_game_list(ship_client_t *c, block_t *b);

/* Send a packet containing the lobby info menu to the client. */
int send_info_list(ship_client_t *c, ship_t *s);

/* Send a message box packet to the client. */
int send_message_box(ship_client_t *c, char msg[]);

/* Send the list of quest categories to the client. */
int send_quest_categories(ship_client_t *c, sylverant_quest_list_t *l);

/* Send the list of quests in a category to the client. */
int send_quest_list(ship_client_t *c, int cat, sylverant_quest_category_t *l);

/* Send information about a quest to the client. */
int send_quest_info(ship_client_t *c, sylverant_quest_t *q);

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
int send_ship_list(ship_client_t *c, miniship_t *l, int ships);

/* Send a warp command to the client. */
int send_warp(ship_client_t *c, uint8_t area);

#endif /* !SHIPPACKETS_H */
