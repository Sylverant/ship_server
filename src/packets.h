/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2012, 2013, 2014 Lawrence Sebald

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

#ifndef PACKETS_H_HAVE_HEADERS
#define PACKETS_H_HAVE_HEADERS

/* Always define the headers of packets, if they haven't already been taken
   care of. */

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

typedef struct dc_pkt_hdr {
    uint8_t pkt_type;
    uint8_t flags;
    uint16_t pkt_len;
} PACKED dc_pkt_hdr_t;

typedef struct pc_pkt_hdr {
    uint16_t pkt_len;
    uint8_t pkt_type;
    uint8_t flags;
} PACKED pc_pkt_hdr_t;

typedef struct bb_pkt_hdr {
    uint16_t pkt_len;
    uint16_t pkt_type;
    uint32_t flags;
} PACKED bb_pkt_hdr_t;

typedef union pkt_header {
    dc_pkt_hdr_t dc;
    pc_pkt_hdr_t pc;
    bb_pkt_hdr_t bb;
} pkt_header_t;

#undef PACKED

#endif /* !PACKETS_H_HAVE_HEADERS */

#ifndef PACKETS_H_HEADERS_ONLY
#ifndef PACKETS_H_HAVE_PACKETS
#define PACKETS_H_HAVE_PACKETS

/* Only do these if we really want them for the file we're compiling. */

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

/* The welcome packet for setting up encryption keys */
typedef struct dc_welcome {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    char copyright[0x40];
    uint32_t svect;
    uint32_t cvect;
} PACKED dc_welcome_pkt;

typedef struct bb_welcome {
    bb_pkt_hdr_t hdr;
    char copyright[0x60];
    uint8_t svect[48];
    uint8_t cvect[48];
} PACKED bb_welcome_pkt;

/* The menu selection packet that the client sends to us */
typedef struct dc_select {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    uint32_t menu_id;
    uint32_t item_id;
} PACKED dc_select_pkt;

typedef struct bb_select {
    bb_pkt_hdr_t hdr;
    uint32_t menu_id;
    uint32_t item_id;
} PACKED bb_select_pkt;

/* Various login packets */
typedef struct dc_login_90 {
    dc_pkt_hdr_t hdr;
    char serial[8];
    uint8_t padding1[9];
    char access_key[8];
    uint8_t padding2[11];
} PACKED dc_login_90_pkt;

typedef struct dc_login_92_93 {
    dc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t guildcard;
    uint32_t unk[3];
    uint8_t unused1;
    uint8_t language_code;
    uint16_t unused2;
    char serial[8];
    uint8_t padding1[9];
    char access_key[8];
    uint8_t padding2[9];
    char dc_id[8];
    uint8_t padding3[88];
    char name[16];
    uint8_t padding4[2];
    uint8_t sec_data[0];
} PACKED dc_login_92_pkt;

typedef struct dc_login_92_93 dc_login_93_pkt;

typedef struct dcv2_login_9a {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    uint8_t unused[32];
    char serial[8];
    uint8_t padding1[8];
    char access_key[8];
    uint8_t padding2[10];
    uint8_t unk[7];
    uint8_t padding3[3];
    char dc_id[8];
    uint8_t padding4[88];
    uint8_t email[32];
    uint8_t padding5[16];
} PACKED dcv2_login_9a_pkt;

typedef struct gc_login_9c {
    dc_pkt_hdr_t hdr;
    uint8_t padding1[8];
    uint8_t version;
    uint8_t padding2[4];
    uint8_t language_code;
    uint8_t padding3[2];
    char serial[8];
    uint8_t padding4[40];
    char access_key[12];
    uint8_t padding5[36];
    char password[16];
    uint8_t padding6[32];
} gc_login_9c_pkt;

typedef struct dcv2_login_9d {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    uint32_t tag;
    uint32_t guildcard;
    uint8_t padding1[8];
    uint8_t version;
    uint8_t padding2[4];
    uint8_t language_code;
    uint8_t padding3[34];
    char serial[8];
    uint8_t padding4[8];
    char access_key[8];
    uint8_t padding5[8];
    char dc_id[8];
    uint8_t padding6[88];
    uint16_t unk2;
    uint8_t padding7[14];
    uint8_t sec_data[0];
} PACKED dcv2_login_9d_pkt;

typedef struct gc_login_9e {
    dc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t guildcard;
    uint8_t padding1[8];
    uint8_t version;
    uint8_t padding2[4];
    uint8_t language_code;
    uint8_t padding3[34];
    char serial[8];
    uint8_t padding4[8];
    char access_key[12];
    uint8_t padding5[4];
    char serial2[8];
    uint8_t padding6[40];
    char access_key2[12];
    uint8_t padding7[36];
    char name[16];
    uint8_t padding8[32];
    uint8_t sec_data[0];
} PACKED gc_login_9e_pkt;

typedef struct bb_login_93 {
    bb_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t guildcard;
    uint16_t version;
    uint8_t unk2[6];
    uint32_t team_id;
    char username[16];
    uint8_t unused1[32];
    char password[16];
    uint8_t unused2[40];
    uint8_t hwinfo[8];
    uint8_t security_data[40];
} PACKED bb_login_93_pkt;

/* The first packet sent by the Dreamcast Network Trial Edition. Note that the
   serial number and access key are both 16-character strings with NUL
   terminators at the end. */
typedef struct dcnte_login_88 {
    dc_pkt_hdr_t hdr;
    char serial[16];
    uint8_t nul1;
    char access_key[16];
    uint8_t nul2;
} PACKED dcnte_login_88_pkt;

typedef struct dcnte_login_8a {
    dc_pkt_hdr_t hdr;
    char dc_id[8];
    uint8_t unk[8];
    char username[48];
    char password[48];
    char email[48];
} PACKED dcnte_login_8a_pkt;

/* This one is the "real" login packet from the DC NTE. If the client is still
   connected to the login server, you get more unused space at the end for some
   reason, but otherwise the packet is exactly the same between the ship and the
   login server. */
typedef struct dcnte_login_8b {
    dc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t guildcard;
    uint8_t dc_id[8];
    uint8_t unk[8];
    char serial[16];
    uint8_t nul1;
    char access_key[16];
    uint8_t nul2;
    char username[48];
    char password[48];
    char char_name[16];
    uint8_t unused[2];
} PACKED dcnte_login_8b_pkt;

/* The packet to verify that a hunter's license has been procured. */
typedef struct login_gc_hlcheck {
    dc_pkt_hdr_t hdr;
    uint8_t padding1[32];
    char serial[8];
    uint8_t padding2[8];
    char access_key[12];
    uint8_t padding3[12];
    uint8_t version;
    uint8_t padding4[3];
    char serial2[8];
    uint8_t padding5[40];
    char access_key2[12];
    uint8_t padding6[36];
    char password[16];
    uint8_t padding7[32];
} PACKED gc_hlcheck_pkt;

/* The packet sent to redirect clients */
typedef struct dc_redirect {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    uint32_t ip_addr;       /* Big-endian */
    uint16_t port;          /* Little-endian */
    uint8_t padding[2];
} PACKED dc_redirect_pkt;

typedef struct bb_redirect {
    bb_pkt_hdr_t hdr;
    uint32_t ip_addr;       /* Big-endian */
    uint16_t port;          /* Little-endian */
    uint8_t padding[2];
} PACKED bb_redirect_pkt;

typedef struct dc_redirect6 {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    uint8_t ip_addr[16];
    uint16_t port;          /* Little-endian */
    uint8_t padding[2];
} PACKED dc_redirect6_pkt;

typedef struct bb_redirect6 {
    bb_pkt_hdr_t hdr;
    uint8_t ip_addr[16];
    uint16_t port;          /* Little-endian */
    uint8_t padding[2];
} PACKED bb_redirect6_pkt;

/* The packet sent as a timestamp */
typedef struct dc_timestamp {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    char timestamp[28];
} PACKED dc_timestamp_pkt;

typedef struct bb_timestamp {
    bb_pkt_hdr_t hdr;
    char timestamp[28];
} PACKED bb_timestamp_pkt;

/* The packet sent to inform clients of their security data */
typedef struct dc_security {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    uint32_t tag;
    uint32_t guildcard;
    uint8_t security_data[0];
} PACKED dc_security_pkt;

typedef struct bb_security {
    bb_pkt_hdr_t hdr;
    uint32_t err_code;
    uint32_t tag;
    uint32_t guildcard;
    uint32_t team_id;
    uint8_t security_data[40];
    uint32_t caps;
} PACKED bb_security_pkt;

/* The packet used for the information reply */
typedef struct dc_info_reply {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    uint32_t odd[2];
    char msg[];
} PACKED dc_info_reply_pkt;

typedef struct bb_info_reply {
    bb_pkt_hdr_t hdr;
    uint32_t unused[2];
    uint16_t msg[];
} PACKED bb_info_reply_pkt;

typedef struct bb_info_reply_pkt bb_scroll_msg_pkt;

/* The ship list packet send to tell clients what blocks are up */
typedef struct dc_block_list {
    dc_pkt_hdr_t hdr;           /* The flags field says the entry count */
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint16_t flags;
        char name[0x12];
    } entries[0];
} PACKED dc_block_list_pkt;

typedef struct pc_block_list {
    pc_pkt_hdr_t hdr;           /* The flags field says the entry count */
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint16_t flags;
        uint16_t name[0x11];
    } entries[0];
} PACKED pc_block_list_pkt;

typedef struct bb_block_list {
    bb_pkt_hdr_t hdr;           /* The flags field says the entry count */
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint16_t flags;
        uint16_t name[0x11];
    } entries[0];
} PACKED bb_block_list_pkt;

typedef struct dc_lobby_list {
    union {                     /* The flags field says the entry count */
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint32_t padding;
    } entries[0];
} PACKED dc_lobby_list_pkt;

typedef struct bb_lobby_list {
    bb_pkt_hdr_t hdr;
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint32_t padding;
    } entries[0];
} PACKED bb_lobby_list_pkt;

#ifdef PLAYER_H

/* The packet sent by clients to send their character data */
typedef struct dc_char_data {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    player_t data;
} PACKED dc_char_data_pkt;

typedef struct bb_char_data {
    bb_pkt_hdr_t hdr;
    sylverant_bb_player_t data;
} PACKED bb_char_data_pkt;

/* The packet sent to clients when they join a lobby */
typedef struct dcnte_lobby_join {
    dc_pkt_hdr_t hdr;
    uint8_t client_id;
    uint8_t leader_id;
    uint16_t padding;
    struct {
        dc_player_hdr_t hdr;
        v1_player_t data;
    } entries[0];
} PACKED dcnte_lobby_join_pkt;

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
        v1_player_t data;
    } entries[0];
} PACKED dc_lobby_join_pkt;

typedef struct pc_lobby_join {
    pc_pkt_hdr_t hdr;
    uint8_t client_id;
    uint8_t leader_id;
    uint8_t one;                        /* Always 1 */
    uint8_t lobby_num;
    uint16_t block_num;
    uint16_t event;
    uint32_t padding;
    struct {
        pc_player_hdr_t hdr;
        v1_player_t data;
    } entries[0];
} PACKED pc_lobby_join_pkt;

typedef struct bb_lobby_join {
    bb_pkt_hdr_t hdr;
    uint8_t client_id;
    uint8_t leader_id;
    uint8_t one;                        /* Always 1 */
    uint8_t lobby_num;
    uint16_t block_num;
    uint16_t event;
    uint32_t padding;
    struct {
        bb_player_hdr_t hdr;
        sylverant_inventory_t inv;
        sylverant_bb_char_t data;
    } entries[0];
} PACKED bb_lobby_join_pkt;

#endif

/* The packet sent to clients when someone leaves a lobby */
typedef struct dc_lobby_leave {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    uint8_t client_id;
    uint8_t leader_id;
    uint16_t padding;
} PACKED dc_lobby_leave_pkt;

typedef struct bb_lobby_leave {
    bb_pkt_hdr_t hdr;
    uint8_t client_id;
    uint8_t leader_id;
    uint16_t padding;
} PACKED bb_lobby_leave_pkt;

/* The packet sent from/to clients for sending a normal chat */
typedef struct dc_chat {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    uint32_t padding;
    uint32_t guildcard;
    char msg[0];
} PACKED dc_chat_pkt;

typedef struct bb_chat {
    bb_pkt_hdr_t hdr;
    uint32_t padding;
    uint32_t guildcard;
    uint16_t msg[];
} PACKED bb_chat_pkt;

/* The packet sent to search for a player */
typedef struct dc_guild_search {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    uint32_t tag;
    uint32_t gc_search;
    uint32_t gc_target;
} PACKED dc_guild_search_pkt;

typedef struct bb_guild_search {
    bb_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t gc_search;
    uint32_t gc_target;
} PACKED bb_guild_search_pkt;

/* The packet sent to reply to a guild card search */
typedef struct dc_guild_reply {
    dc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t gc_search;
    uint32_t gc_target;
    uint32_t padding1;
    uint32_t ip;
    uint16_t port;
    uint16_t padding2;
    char location[0x44];
    uint32_t menu_id;
    uint32_t item_id;
    char padding3[0x3C];
    char name[0x20];
} PACKED dc_guild_reply_pkt;

typedef struct pc_guild_reply {
    pc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t gc_search;
    uint32_t gc_target;
    uint32_t padding1;
    uint32_t ip;
    uint16_t port;
    uint16_t padding2;
    uint16_t location[0x44];
    uint32_t menu_id;
    uint32_t item_id;
    uint8_t padding3[0x3C];
    uint16_t name[0x20];
} PACKED pc_guild_reply_pkt;

typedef struct bb_guild_reply {
    bb_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t gc_search;
    uint32_t gc_target;
    uint32_t padding1;
    uint32_t padding2;
    uint32_t ip;
    uint16_t port;
    uint16_t padding3;
    uint16_t location[0x44];
    uint32_t menu_id;
    uint32_t item_id;
    uint8_t padding4[0x3C];
    uint16_t name[0x20];
} PACKED bb_guild_reply_pkt;

/* IPv6 versions of the above two packets */
typedef struct dc_guild_reply6 {
    dc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t gc_search;
    uint32_t gc_target;
    uint32_t padding1;
    uint8_t ip[16];
    uint16_t port;
    uint16_t padding2;
    char location[0x44];
    uint32_t menu_id;
    uint32_t item_id;
    char padding3[0x3C];
    char name[0x20];
} PACKED dc_guild_reply6_pkt;

typedef struct pc_guild_reply6 {
    pc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t gc_search;
    uint32_t gc_target;
    uint32_t padding1;
    uint8_t ip[16];
    uint16_t port;
    uint16_t padding2;
    uint16_t location[0x44];
    uint32_t menu_id;
    uint32_t item_id;
    uint8_t padding3[0x3C];
    uint16_t name[0x20];
} PACKED pc_guild_reply6_pkt;

typedef struct bb_guild_reply6 {
    bb_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t gc_search;
    uint32_t gc_target;
    uint32_t padding1;
    uint32_t padding2;
    uint8_t ip[16];
    uint16_t port;
    uint16_t padding3;
    uint16_t location[0x44];
    uint32_t menu_id;
    uint32_t item_id;
    uint8_t padding4[0x3C];
    uint16_t name[0x20];
} PACKED bb_guild_reply6_pkt;

/* The packet sent to send/deliver simple mail */
typedef struct dc_simple_mail {
    dc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t gc_sender;
    char name[16];
    uint32_t gc_dest;
    char stuff[0x200]; /* Start = 0x20, end = 0xB0 */
} PACKED dc_simple_mail_pkt;

typedef struct pc_simple_mail {
    pc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t gc_sender;
    uint16_t name[16];
    uint32_t gc_dest;
    char stuff[0x400]; /* Start = 0x30, end = 0x150 */
} PACKED pc_simple_mail_pkt;

typedef struct bb_simple_mail {
    bb_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t gc_sender;
    uint16_t name[16];
    uint32_t gc_dest;
    uint16_t timestamp[20];
    uint16_t message[0xAC];
    uint8_t unk2[0x2A0];
} PACKED bb_simple_mail_pkt;

/* The packet sent by clients to create a game */
typedef struct dcnte_game_create {
    dc_pkt_hdr_t hdr;
    uint32_t unused[2];
    char name[16];
    char password[16];
} PACKED dcnte_game_create_pkt;

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

typedef struct pc_game_create {
    pc_pkt_hdr_t hdr;
    uint32_t unused[2];
    uint16_t name[16];
    uint16_t password[16];
    uint8_t difficulty;
    uint8_t battle;
    uint8_t challenge;
    uint8_t padding;
} PACKED pc_game_create_pkt;

typedef struct gc_game_create {
    dc_pkt_hdr_t hdr;
    uint32_t unused[2];
    char name[16];
    char password[16];
    uint8_t difficulty;
    uint8_t battle;
    uint8_t challenge;
    uint8_t episode;
} PACKED gc_game_create_pkt;

typedef struct bb_game_create {
    bb_pkt_hdr_t hdr;
    uint32_t unused[2];
    uint16_t name[16];
    uint16_t password[16];
    uint8_t difficulty;
    uint8_t battle;
    uint8_t challenge;
    uint8_t episode;
    uint8_t single_player;
    uint8_t padding[3];
} PACKED bb_game_create_pkt;

#ifdef PLAYER_H

/* The packet sent to clients to join a game */
typedef struct dcnte_game_join {
    dc_pkt_hdr_t hdr;
    uint8_t client_id;
    uint8_t leader_id;
    uint8_t one;
    uint8_t unused;
    uint32_t maps[0x20];
    dc_player_hdr_t players[4];
} PACKED dcnte_game_join_pkt;

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
    uint32_t rand_seed;
} PACKED dc_game_join_pkt;

typedef struct pc_game_join {
    pc_pkt_hdr_t hdr;
    uint32_t maps[0x20];
    pc_player_hdr_t players[4];
    uint8_t client_id;
    uint8_t leader_id;
    uint8_t one;                        /* Always 1. */
    uint8_t difficulty;
    uint8_t battle;
    uint8_t event;
    uint8_t section;
    uint8_t challenge;
    uint32_t rand_seed;
} PACKED pc_game_join_pkt;

typedef struct gc_game_join {
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
    uint32_t rand_seed;
    uint8_t episode;
    uint8_t one2;                       /* Always 1. */
    uint16_t padding;
} PACKED gc_game_join_pkt;

typedef struct ep3_game_join {
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
    uint32_t rand_seed;
    uint8_t episode;
    uint8_t one2;                       /* Always 1. */
    uint16_t padding;
    v1_player_t player_data[4];
} PACKED ep3_game_join_pkt;

typedef struct bb_game_join {
    bb_pkt_hdr_t hdr;
    uint32_t maps[0x20];
    bb_player_hdr_t players[4];
    uint8_t client_id;
    uint8_t leader_id;
    uint8_t one;                        /* Always 1. */
    uint8_t difficulty;
    uint8_t battle;
    uint8_t event;
    uint8_t section;
    uint8_t challenge;
    uint32_t rand_seed;
    uint8_t episode;
    uint8_t one2;                       /* Always 1. */
    uint8_t single_player;
    uint8_t unused;
} PACKED bb_game_join_pkt;

#endif

/* The packet sent to clients to give them the game select list */
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

typedef struct pc_game_list {
    pc_pkt_hdr_t hdr;
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint8_t difficulty;
        uint8_t players;
        uint16_t name[16];
        uint8_t v2;
        uint8_t flags;
    } entries[0];
} PACKED pc_game_list_pkt;

typedef struct bb_game_list {
    bb_pkt_hdr_t hdr;
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint8_t difficulty;
        uint8_t players;
        uint16_t name[16];
        uint8_t episode;
        uint8_t flags;
    } entries[0];
} PACKED bb_game_list_pkt;

/* The packet sent to display a large message to the user */
typedef struct dc_msg_box {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    char msg[];
} PACKED dc_msg_box_pkt;

typedef struct bb_msg_box {
    bb_pkt_hdr_t hdr;
    char msg[];
} PACKED bb_msg_box_pkt;

/* The packet used to send the quest list */
typedef struct dc_quest_list {
    dc_pkt_hdr_t hdr;
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        char name[32];
        char desc[112];
    } entries[0];
} PACKED dc_quest_list_pkt;

typedef struct pc_quest_list {
    pc_pkt_hdr_t hdr;
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint16_t name[32];
        uint16_t desc[112];
    } entries[0];
} PACKED pc_quest_list_pkt;

typedef struct bb_quest_list {
    bb_pkt_hdr_t hdr;
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint16_t name[32];
        uint16_t desc[122];
    } entries[0];
} PACKED bb_quest_list_pkt;

/* The packet sent to inform a client of a quest file that will be coming down
   the pipe */
typedef struct dc_quest_file {
    dc_pkt_hdr_t hdr;
    char name[32];
    uint8_t unused1[3];
    char filename[16];
    uint8_t unused2;
    uint32_t length;
} PACKED dc_quest_file_pkt;

typedef struct pc_quest_file {
    pc_pkt_hdr_t hdr;
    char name[32];
    uint16_t unused;
    uint16_t flags;
    char filename[16];
    uint32_t length;
} PACKED pc_quest_file_pkt;

typedef struct gc_quest_file {
    dc_pkt_hdr_t hdr;
    char name[32];
    uint16_t unused;
    uint16_t flags;
    char filename[16];
    uint32_t length;
} PACKED gc_quest_file_pkt;

typedef struct bb_quest_file {
    bb_pkt_hdr_t hdr;
    char unused1[32];
    uint16_t unused2;
    uint16_t flags;
    char filename[16];
    uint32_t length;
    char name[24];
} PACKED bb_quest_file_pkt;

/* The packet sent to actually send quest data */
typedef struct dc_quest_chunk {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    char filename[16];
    char data[1024];
    uint32_t length;
} PACKED dc_quest_chunk_pkt;

typedef struct bb_quest_chunk {
    bb_pkt_hdr_t hdr;
    char filename[16];
    char data[1024];
    uint32_t length;
} PACKED bb_quest_chunk_pkt;

/* The packet sent to update the list of arrows in a lobby */
typedef struct dc_arrow_list {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    struct {
        uint32_t tag;
        uint32_t guildcard;
        uint32_t arrow;
    } entries[0];
} PACKED dc_arrow_list_pkt;

typedef struct bb_arrow_list {
    bb_pkt_hdr_t hdr;
    struct {
        uint32_t tag;
        uint32_t guildcard;
        uint32_t arrow;
    } entries[0];
} PACKED bb_arrow_list_pkt;

/* The ship list packet sent to tell clients what ships are up */
typedef struct dc_ship_list {
    dc_pkt_hdr_t hdr;           /* The flags field says how many entries */
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint16_t flags;
        char name[0x12];
    } entries[0];
} PACKED dc_ship_list_pkt;

typedef struct pc_ship_list {
    pc_pkt_hdr_t hdr;           /* The flags field says how many entries */
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint16_t flags;
        uint16_t name[0x11];
    } entries[0];
} PACKED pc_ship_list_pkt;

typedef struct bb_ship_list {
    bb_pkt_hdr_t hdr;           /* The flags field says how many entries */
    struct {
        uint32_t menu_id;
        uint32_t item_id;
        uint16_t flags;
        uint16_t name[0x11];
    } entries[0];
} PACKED bb_ship_list_pkt;

/* The choice search options packet sent to tell clients what they can actually
   search on */
typedef struct dc_choice_search {
    dc_pkt_hdr_t hdr;
    struct {
        uint16_t menu_id;
        uint16_t item_id;
        char text[0x1C];
    } entries[0];
} PACKED dc_choice_search_pkt;

typedef struct pc_choice_search {
    pc_pkt_hdr_t hdr;
    struct {
        uint16_t menu_id;
        uint16_t item_id;
        uint16_t text[0x1C];
    } entries[0];
} PACKED pc_choice_search_pkt;

/* The packet sent to set up the user's choice search settings or to actually
   perform a choice search */
typedef struct dc_choice_set {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    uint8_t off;
    uint8_t padding[3];
    struct {
        uint16_t menu_id;
        uint16_t item_id;
    } entries[5];
} PACKED dc_choice_set_pkt;

/* The packet sent as a reply to a choice search */
typedef struct dc_choice_reply {
    dc_pkt_hdr_t hdr;
    struct {
        uint32_t guildcard;
        char name[0x10];
        char cl_lvl[0x20];
        char location[0x30];
        uint32_t padding;
        uint32_t ip;
        uint16_t port;
        uint16_t padding2;
        uint32_t menu_id;
        uint32_t item_id;
        uint8_t padding3[0x5C];
    } entries[0];
} PACKED dc_choice_reply_pkt;

typedef struct pc_choice_reply {
    pc_pkt_hdr_t hdr;
    struct {
        uint32_t guildcard;
        uint16_t name[0x10];
        uint16_t cl_lvl[0x20];
        uint16_t location[0x30];
        uint32_t padding;
        uint32_t ip;
        uint16_t port;
        uint16_t padding2;
        uint32_t menu_id;
        uint32_t item_id;
        uint8_t padding3[0x7C];
    } entries[0];
} PACKED pc_choice_reply_pkt;

/* The packet sent as a reply to a choice search in IPv6 mode */
typedef struct dc_choice_reply6 {
    dc_pkt_hdr_t hdr;
    struct {
        uint32_t guildcard;
        char name[0x10];
        char cl_lvl[0x20];
        char location[0x30];
        uint32_t padding;
        uint8_t ip[16];
        uint16_t port;
        uint16_t padding2;
        uint32_t menu_id;
        uint32_t item_id;
        uint8_t padding3[0x5C];
    } entries[0];
} PACKED dc_choice_reply6_pkt;

typedef struct pc_choice_reply6 {
    pc_pkt_hdr_t hdr;
    struct {
        uint32_t guildcard;
        uint16_t name[0x10];
        uint16_t cl_lvl[0x20];
        uint16_t location[0x30];
        uint32_t padding;
        uint8_t ip[16];
        uint16_t port;
        uint16_t padding2;
        uint32_t menu_id;
        uint32_t item_id;
        uint8_t padding3[0x7C];
    } entries[0];
} PACKED pc_choice_reply6_pkt;

/* The packet used to ask for a GBA file */
typedef struct gc_gba_req {
    dc_pkt_hdr_t hdr;
    char filename[16];
} PACKED gc_gba_req_pkt;

/* The packet sent to clients to read the info board */
typedef struct gc_read_info {
    dc_pkt_hdr_t hdr;
    struct {
        char name[0x10];
        char msg[0xAC];
    } entries[0];
} PACKED gc_read_info_pkt;

typedef struct bb_read_info {
    bb_pkt_hdr_t hdr;
    struct {
        uint16_t name[0x10];
        uint16_t msg[0xAC];
    } entries[0];
} PACKED bb_read_info_pkt;

/* The packet used in trading items */
typedef struct gc_trade {
    dc_pkt_hdr_t hdr;
    uint8_t who;
    uint8_t unk[];
} PACKED gc_trade_pkt;

/* The packet used to send C-Rank Data */
typedef struct dc_c_rank_update {
    dc_pkt_hdr_t hdr;
    struct {
        uint32_t client_id;
        union {
            uint8_t c_rank[0xB8];
            struct {
                uint32_t unk1;
                char string[0x0C];
                uint8_t unk2[0x24];
                uint16_t grave_unk4;
                uint16_t grave_deaths;
                uint32_t grave_coords_time[5];
                char grave_team[20];
                char grave_message[24];
                uint32_t times[9];
                uint32_t battle[7];
            };
        };
    } entries[0];
} PACKED dc_c_rank_update_pkt;

typedef struct pc_c_rank_update {
    pc_pkt_hdr_t hdr;
    struct {
        uint32_t client_id;
        union {
            uint8_t c_rank[0xF0];
            struct {
                uint32_t unk1;
                uint16_t string[0x0C];
                uint8_t unk2[0x24];
                uint16_t grave_unk4;
                uint16_t grave_deaths;
                uint32_t grave_coords_time[5];
                uint16_t grave_team[20];
                uint16_t grave_message[24];
                uint32_t times[9];
                uint32_t battle[7];
            };
        };
    } entries[0];
} PACKED pc_c_rank_update_pkt;

typedef struct gc_c_rank_update {
    dc_pkt_hdr_t hdr;
    struct {
        uint32_t client_id;
        union {
            uint8_t c_rank[0x0118];
            struct {
                uint32_t unk1;          /* Flip the words for dc/pc! */
                uint32_t times[9];
                uint8_t unk2[0xB0];
                char string[0x0C];
                uint8_t unk3[0x18];
                uint32_t battle[7];
            };
        };
    } entries[0];
} PACKED gc_c_rank_update_pkt;

/* The packet used to update the blocked senders list */
typedef struct gc_blacklist_update {
    union {
        dc_pkt_hdr_t gc;
        pc_pkt_hdr_t pc;
    } hdr;
    uint32_t list[30];
} PACKED gc_blacklist_update_pkt;

typedef struct bb_blacklist_update {
    bb_pkt_hdr_t hdr;
    uint32_t list[28];
} PACKED bb_blacklist_update_pkt;

/* The packet used to set a simple mail autoreply */
typedef struct autoreply_set {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    char msg[];
} PACKED autoreply_set_pkt;

typedef struct bb_autoreply_set {
    bb_pkt_hdr_t hdr;
    uint16_t msg[];
} PACKED bb_autoreply_set_pkt;

/* The packet used to write to the infoboard */
typedef autoreply_set_pkt gc_write_info_pkt;
typedef bb_autoreply_set_pkt bb_write_info_pkt;

/* The packet used to send the Episode 3 rank */
typedef struct ep3_rank_update {
    dc_pkt_hdr_t hdr;
    uint32_t rank;
    char rank_txt[12];
    uint32_t meseta;
    uint32_t max_meseta;
    uint32_t jukebox;
} ep3_rank_update_pkt;

/* The packet used to send the Episode 3 card list */
typedef struct ep3_card_update {
    dc_pkt_hdr_t hdr;
    uint32_t size;
    uint8_t data[];
} PACKED ep3_card_update_pkt;

/* The packet used to change the music on the Episode 3 jukebox */
typedef struct ep3_jukebox {
    dc_pkt_hdr_t hdr;
    uint32_t unk1;
    uint32_t unk2;
    uint16_t unk3;
    uint16_t music;
} PACKED ep3_jukebox_pkt;

/* The packet used to communiate various requests and such to the server from
   Episode 3 */
typedef struct ep3_server_data {
    dc_pkt_hdr_t hdr;
    uint8_t unk[4];
    uint8_t data[];
} PACKED ep3_server_data_pkt;

/* The packet used by Episode 3 clients to create a game */
typedef struct ep3_game_create {
    dc_pkt_hdr_t hdr;
    uint32_t unused[2];
    char name[16];
    char password[16];
    uint8_t unused2[2];
    uint8_t view_battle;
    uint8_t episode;
} PACKED ep3_game_create_pkt;

#ifdef SYLVERANT__CHARACTERS_H

/* Blue Burst option configuration packet */
typedef struct bb_opt_config {
    bb_pkt_hdr_t hdr;
    sylverant_bb_key_team_config_t data;
} PACKED bb_opt_config_pkt;

#endif

/* Blue Burst packet used to select a character. */
typedef struct bb_char_select {
    bb_pkt_hdr_t hdr;
    uint8_t slot;
    uint8_t padding1[3];
    uint8_t reason;
    uint8_t padding2[3];
} PACKED bb_char_select_pkt;

/* Blue Burst packet to acknowledge a character select. */
typedef struct bb_char_ack {
    bb_pkt_hdr_t hdr;
    uint8_t slot;
    uint8_t padding1[3];
    uint8_t code;
    uint8_t padding2[3];
} PACKED bb_char_ack_pkt;

/* Blue Burst packet to send the client's checksum */
typedef struct bb_checksum {
    bb_pkt_hdr_t hdr;
    uint32_t checksum;
    uint32_t padding;
} PACKED bb_checksum_pkt;

/* Blue Burst packet to acknowledge the client's checksum. */
typedef struct bb_checksum_ack {
    bb_pkt_hdr_t hdr;
    uint32_t ack;
} PACKED bb_checksum_ack_pkt;

/* Blue Burst packet that acts as a header for the client's guildcard data. */
typedef struct bb_guildcard_hdr {
    bb_pkt_hdr_t hdr;
    uint8_t one;
    uint8_t padding1[3];
    uint16_t len;
    uint8_t padding2[2];
    uint32_t checksum;
} PACKED bb_guildcard_hdr_pkt;

/* Blue Burst packet that requests guildcard data. */
typedef struct bb_guildcard_req {
    bb_pkt_hdr_t hdr;
    uint32_t unk;
    uint32_t chunk;
    uint32_t cont;
} PACKED bb_guildcard_req_pkt;

/* Blue Burst packet for sending a chunk of guildcard data. */
typedef struct bb_guildcard_chunk {
    bb_pkt_hdr_t hdr;
    uint32_t unk;
    uint32_t chunk;
    uint8_t data[];
} PACKED bb_guildcard_chunk_pkt;

/* Blue Burst packet that's a header for the parameter files. */
typedef struct bb_param_hdr {
    bb_pkt_hdr_t hdr;
    struct {
        uint32_t size;
        uint32_t checksum;
        uint32_t offset;
        char filename[0x40];
    } entries[];
} PACKED bb_param_hdr_pkt;

/* Blue Burst packet for sending a chunk of the parameter files. */
typedef struct bb_param_chunk {
    bb_pkt_hdr_t hdr;
    uint32_t chunk;
    uint8_t data[];
} PACKED bb_param_chunk_pkt;

/* Blue Burst packet for setting flags (dressing room flag, for instance). */
typedef struct bb_setflag {
    bb_pkt_hdr_t hdr;
    uint32_t flags;
} PACKED bb_setflag_pkt;

#ifdef SYLVERANT__CHARACTERS_H

/* Blue Burst packet for creating/updating a character as well as for the
   previews sent for the character select screen. */
typedef struct bb_char_preview {
    bb_pkt_hdr_t hdr;
    uint8_t slot;
    uint8_t unused[3];
    sylverant_bb_mini_char_t data;
} PACKED bb_char_preview_pkt;

/* Blue Burst packet for sending the full character data and options */
typedef struct bb_full_char {
    bb_pkt_hdr_t hdr;
    sylverant_bb_full_char_t data;
} PACKED bb_full_char_pkt;

#endif /* SYLVERANT__CHARACTERS_H */

/* Blue Burst packet for updating options */
typedef struct bb_options_update {
    bb_pkt_hdr_t hdr;
    uint8_t data[];
} PACKED bb_options_update_pkt;

/* Blue Burst packet for adding a Guildcard to the user's list */
typedef struct bb_guildcard_add {
    bb_pkt_hdr_t hdr;
    uint32_t guildcard;
    uint16_t name[24];
    uint16_t team_name[16];
    uint16_t text[88];
    uint8_t one;
    uint8_t language;
    uint8_t section;
    uint8_t char_class;
} PACKED bb_guildcard_add_pkt;

typedef bb_guildcard_add_pkt bb_blacklist_add_pkt;
typedef bb_guildcard_add_pkt bb_guildcard_set_txt_pkt;

/* Blue Burst packet for deleting a Guildcard */
typedef struct bb_guildcard_del {
    bb_pkt_hdr_t hdr;
    uint32_t guildcard;
} PACKED bb_guildcard_del_pkt;

typedef bb_guildcard_del_pkt bb_blacklist_del_pkt;

/* Blue Burst packet for sorting Guildcards */
typedef struct bb_guildcard_sort {
    bb_pkt_hdr_t hdr;
    uint32_t guildcard1;
    uint32_t guildcard2;
} PACKED bb_guildcard_sort_pkt;

/* Blue Burst packet for setting a comment on a Guildcard */
typedef struct bb_guildcard_comment {
    bb_pkt_hdr_t hdr;
    uint32_t guildcard;
    uint16_t text[88];
} PACKED bb_guildcard_comment_pkt;

/* Gamecube quest statistics packet (from Maximum Attack 2). */
typedef struct gc_quest_stats {
    dc_pkt_hdr_t hdr;
    uint32_t stats[10];
} PACKED gc_quest_stats_pkt;

#undef PACKED

/* Parameters for the various packets. */
#define MSG1_TYPE                       0x0001
#define WELCOME_TYPE                    0x0002
#define BB_WELCOME_TYPE                 0x0003
#define SECURITY_TYPE                   0x0004
#define TYPE_05                         0x0005
#define CHAT_TYPE                       0x0006
#define BLOCK_LIST_TYPE                 0x0007
#define GAME_LIST_TYPE                  0x0008
#define INFO_REQUEST_TYPE               0x0009
#define DC_GAME_CREATE_TYPE             0x000C
#define MENU_SELECT_TYPE                0x0010
#define INFO_REPLY_TYPE                 0x0011
#define QUEST_CHUNK_TYPE                0x0013
#define LOGIN_WELCOME_TYPE              0x0017
#define REDIRECT_TYPE                   0x0019
#define MSG_BOX_TYPE                    0x001A
#define PING_TYPE                       0x001D
#define LOBBY_INFO_TYPE                 0x001F
#define GUILD_SEARCH_TYPE               0x0040
#define GUILD_REPLY_TYPE                0x0041
#define QUEST_FILE_TYPE                 0x0044
#define GAME_COMMAND0_TYPE              0x0060
#define CHAR_DATA_TYPE                  0x0061
#define GAME_COMMAND2_TYPE              0x0062
#define GAME_JOIN_TYPE                  0x0064
#define GAME_ADD_PLAYER_TYPE            0x0065
#define GAME_LEAVE_TYPE                 0x0066
#define LOBBY_JOIN_TYPE                 0x0067
#define LOBBY_ADD_PLAYER_TYPE           0x0068
#define LOBBY_LEAVE_TYPE                0x0069
#define GAME_COMMANDC_TYPE              0x006C
#define GAME_COMMANDD_TYPE              0x006D
#define DONE_BURSTING_TYPE              0x006F
#define SIMPLE_MAIL_TYPE                0x0081
#define LOBBY_LIST_TYPE                 0x0083
#define LOBBY_CHANGE_TYPE               0x0084
#define LOBBY_ARROW_LIST_TYPE           0x0088
#define LOGIN_88_TYPE                   0x0088  /* DC Network Trial Edition */
#define LOBBY_ARROW_CHANGE_TYPE         0x0089
#define LOBBY_NAME_TYPE                 0x008A
#define LOGIN_8A_TYPE                   0x008A  /* DC Network Trial Edition */
#define LOGIN_8B_TYPE                   0x008B  /* DC Network Trial Edition */
#define DCNTE_CHAR_DATA_REQ_TYPE        0x008D  /* DC Network Trial Edition */
#define DCNTE_SHIP_LIST_TYPE            0x008E  /* DC Network Trial Edition */
#define DCNTE_BLOCK_LIST_REQ_TYPE       0x008F  /* DC Network Trial Edition */
#define LOGIN_90_TYPE                   0x0090
#define LOGIN_92_TYPE                   0x0092
#define LOGIN_93_TYPE                   0x0093
#define CHAR_DATA_REQUEST_TYPE          0x0095
#define CHECKSUM_TYPE                   0x0096
#define CHECKSUM_REPLY_TYPE             0x0097
#define LEAVE_GAME_PL_DATA_TYPE         0x0098
#define SHIP_LIST_REQ_TYPE              0x0099
#define LOGIN_9A_TYPE                   0x009A
#define LOGIN_9C_TYPE                   0x009C
#define LOGIN_9D_TYPE                   0x009D
#define LOGIN_9E_TYPE                   0x009E
#define SHIP_LIST_TYPE                  0x00A0
#define BLOCK_LIST_REQ_TYPE             0x00A1
#define QUEST_LIST_TYPE                 0x00A2
#define QUEST_INFO_TYPE                 0x00A3
#define DL_QUEST_LIST_TYPE              0x00A4
#define DL_QUEST_FILE_TYPE              0x00A6
#define DL_QUEST_CHUNK_TYPE             0x00A7
#define QUEST_END_LIST_TYPE             0x00A9
#define QUEST_STATS_TYPE                0x00AA
#define QUEST_LOAD_DONE_TYPE            0x00AC
#define TEXT_MSG_TYPE                   0x00B0
#define TIMESTAMP_TYPE                  0x00B1
#define EP3_RANK_UPDATE_TYPE            0x00B7
#define EP3_CARD_UPDATE_TYPE            0x00B8
#define EP3_COMMAND_TYPE                0x00BA
#define CHOICE_OPTION_TYPE              0x00C0
#define GAME_CREATE_TYPE                0x00C1
#define CHOICE_SETTING_TYPE             0x00C2
#define CHOICE_SEARCH_TYPE              0x00C3
#define CHOICE_REPLY_TYPE               0x00C4
#define C_RANK_TYPE                     0x00C5
#define BLACKLIST_TYPE                  0x00C6
#define AUTOREPLY_SET_TYPE              0x00C7
#define AUTOREPLY_CLEAR_TYPE            0x00C8
#define GAME_COMMAND_C9_TYPE            0x00C9
#define EP3_SERVER_DATA_TYPE            0x00CA
#define GAME_COMMAND_CB_TYPE            0x00CB
#define TRADE_0_TYPE                    0x00D0
#define TRADE_1_TYPE                    0x00D1
#define TRADE_2_TYPE                    0x00D2
#define TRADE_3_TYPE                    0x00D3
#define TRADE_4_TYPE                    0x00D4
#define GC_MSG_BOX_TYPE                 0x00D5
#define GC_MSG_BOX_CLOSED_TYPE          0x00D6
#define GC_GBA_FILE_REQ_TYPE            0x00D7
#define INFOBOARD_TYPE                  0x00D8
#define INFOBOARD_WRITE_TYPE            0x00D9
#define LOBBY_EVENT_TYPE                0x00DA
#define GC_VERIFY_LICENSE_TYPE          0x00DB
#define EP3_MENU_CHANGE_TYPE            0x00DC
#define BB_GUILDCARD_HEADER_TYPE        0x01DC
#define BB_GUILDCARD_CHUNK_TYPE         0x02DC
#define BB_GUILDCARD_CHUNK_REQ_TYPE     0x03DC
#define BB_OPTION_REQUEST_TYPE          0x00E0
#define BB_OPTION_CONFIG_TYPE           0x00E2
#define BB_CHARACTER_SELECT_TYPE        0x00E3
#define BB_CHARACTER_ACK_TYPE           0x00E4
#define BB_CHARACTER_UPDATE_TYPE        0x00E5
#define BB_SECURITY_TYPE                0x00E6
#define BB_FULL_CHARACTER_TYPE          0x00E7
#define BB_CHECKSUM_TYPE                0x01E8
#define BB_CHECKSUM_ACK_TYPE            0x02E8
#define BB_GUILD_REQUEST_TYPE           0x03E8
#define BB_ADD_GUILDCARD_TYPE           0x04E8
#define BB_DEL_GUILDCARD_TYPE           0x05E8
#define BB_SET_GUILDCARD_TEXT_TYPE      0x06E8
#define BB_ADD_BLOCKED_USER_TYPE        0x07E8
#define BB_DEL_BLOCKED_USER_TYPE        0x08E8
#define BB_SET_GUILDCARD_COMMENT_TYPE   0x09E8
#define BB_SORT_GUILDCARD_TYPE          0x0AE8
#define BB_PARAM_HEADER_TYPE            0x01EB
#define BB_PARAM_CHUNK_TYPE             0x02EB
#define BB_PARAM_CHUNK_REQ_TYPE         0x03EB
#define BB_PARAM_HEADER_REQ_TYPE        0x04EB
#define EP3_GAME_CREATE_TYPE            0x00EC
#define BB_SETFLAG_TYPE                 0x00EC
#define BB_UPDATE_OPTION_FLAGS          0x01ED
#define BB_UPDATE_SYMBOL_CHAT           0x02ED
#define BB_UPDATE_SHORTCUTS             0x03ED
#define BB_UPDATE_KEY_CONFIG            0x04ED
#define BB_UPDATE_PAD_CONFIG            0x05ED
#define BB_UPDATE_TECH_MENU             0x06ED
#define BB_UPDATE_CONFIG                0x07ED
#define BB_SCROLL_MSG_TYPE              0x00EE

#define DC_WELCOME_LENGTH               0x004C
#define BB_WELCOME_LENGTH               0x00C8
#define BB_SECURITY_LENGTH              0x0044
#define DC_REDIRECT_LENGTH              0x000C
#define BB_REDIRECT_LENGTH              0x0010
#define DC_REDIRECT6_LENGTH             0x0018
#define BB_REDIRECT6_LENGTH             0x001C
#define DC_TIMESTAMP_LENGTH             0x0020
#define BB_TIMESTAMP_LENGTH             0x0024
#define DC_LOBBY_LIST_LENGTH            0x00C4
#define EP3_LOBBY_LIST_LENGTH           0x0100
#define BB_LOBBY_LIST_LENGTH            0x00C8
#define DC_CHAR_DATA_LENGTH             0x0420
#define DC_LOBBY_LEAVE_LENGTH           0x0008
#define BB_LOBBY_LEAVE_LENGTH           0x000C
#define DC_GUILD_REPLY_LENGTH           0x00C4
#define PC_GUILD_REPLY_LENGTH           0x0128
#define BB_GUILD_REPLY_LENGTH           0x0130
#define DC_GUILD_REPLY6_LENGTH          0x00D0
#define PC_GUILD_REPLY6_LENGTH          0x0134
#define BB_GUILD_REPLY6_LENGTH          0x013C
#define DC_GAME_JOIN_LENGTH             0x0110
#define GC_GAME_JOIN_LENGTH             0x0114
#define EP3_GAME_JOIN_LENGTH            0x1184
#define DC_QUEST_INFO_LENGTH            0x0128
#define PC_QUEST_INFO_LENGTH            0x024C
#define BB_QUEST_INFO_LENGTH            0x0250
#define DC_QUEST_FILE_LENGTH            0x003C
#define BB_QUEST_FILE_LENGTH            0x0058
#define DC_QUEST_CHUNK_LENGTH           0x0418
#define BB_QUEST_CHUNK_LENGTH           0x041C
#define DC_SIMPLE_MAIL_LENGTH           0x0220
#define PC_SIMPLE_MAIL_LENGTH           0x0430
#define BB_SIMPLE_MAIL_LENGTH           0x045C
#define BB_OPTION_CONFIG_LENGTH         0x0AF8
#define BB_FULL_CHARACTER_LENGTH        0x399C

/* Responses to login packets... */
/* DC Network Trial Edition - Responses to Packet 0x88. */
#define LOGIN_88_NEW_USER                   0
#define LOGIN_88_OK                         1

/* DC Network Trial Edition - Responses to Packet 0x8A. */
#define LOGIN_8A_REGISTER_OK                1

/* DCv1 - Responses to Packet 0x90. */
#define LOGIN_90_OK                         0
#define LOGIN_90_NEW_USER                   1
#define LOGIN_90_OK2                        2
#define LOGIN_90_BAD_SNAK                   3

/* DCv1 - Responses to Packet 0x92. */
#define LOGIN_92_BAD_SNAK                   0
#define LOGIN_92_OK                         1

/* DCv2/PC - Responses to Packet 0x9A. */
#define LOGIN_9A_OK                         0
#define LOGIN_9A_NEW_USER                   1
#define LOGIN_9A_OK2                        2
#define LOGIN_9A_BAD_ACCESS                 3
#define LOGIN_9A_BAD_SERIAL                 4
#define LOGIN_9A_ERROR                      5

/* DCv2/PC - Responses to Packet 0x9C. */
#define LOGIN_9CV2_REG_FAIL                 0
#define LOGIN_9CV2_OK                       1

/* Gamecube - Responses to Packet 0xDB. */
#define LOGIN_DB_OK                         0
#define LOGIN_DB_NEW_USER                   1
#define LOGIN_DB_OK2                        2
#define LOGIN_DB_BAD_ACCESS                 3
#define LOGIN_DB_BAD_SERIAL                 4
#define LOGIN_DB_NET_ERROR                  5   /* Also 6, 9, 10, 20-255. */
#define LOGIN_DB_NO_HL                      7   /* Also 18. */
#define LOGIN_DB_EXPIRED_HL                 8   /* Also 17. */
#define LOGIN_DB_BAD_HL                     11  /* Also 12, 13 - Diff errnos. */
#define LOGIN_DB_CONN_ERROR                 14
#define LOGIN_DB_SUSPENDED                  15  /* Also 16. */
#define LOGIN_DB_MAINTENANCE                19

/* Gamecube - Responses to Packet 0x9C. */
#define LOGIN_9CGC_BAD_PWD                  0
#define LOGIN_9CGC_OK                       1

/* Blue Burst - Responses to Packet 0x93. */
#define LOGIN_93BB_OK                       0
#define LOGIN_93BB_UNKNOWN_ERROR            1
#define LOGIN_93BB_BAD_USER_PWD             2
#define LOGIN_93BB_BAD_USER_PWD2            3
#define LOGIN_93BB_MAINTENANCE              4
#define LOGIN_93BB_ALREADY_ONLINE           5
#define LOGIN_93BB_BANNED                   6
#define LOGIN_93BB_BANNED2                  7
#define LOGIN_93BB_NO_USER_RECORD           8
#define LOGIN_93BB_PAY_UP                   9
#define LOGIN_93BB_LOCKED                   10  /* Improper shutdown */
#define LOGIN_93BB_BAD_VERSION              11
#define LOGIN_93BB_FORCED_DISCONNECT        12

/* Episode 3 - Types of 0xBA commands. */
#define EP3_COMMAND_JUKEBOX_REQUEST         2
#define EP3_COMMAND_JUKEBOX_SET             3

/* Blue Burst - Character Acknowledgement codes. */
#define BB_CHAR_ACK_UPDATE                  0
#define BB_CHAR_ACK_SELECT                  1
#define BB_CHAR_ACK_NONEXISTANT             2

#endif /* !PACKETS_H_HAVE_PACKETS */
#endif /* !PACKETS_H_HEADERS_ONLY */
