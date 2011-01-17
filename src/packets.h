/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011 Lawrence Sebald

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

typedef union pkt_header {
    dc_pkt_hdr_t dc;
    pc_pkt_hdr_t pc;
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

/* The menu selection packet that the client sends to us */
typedef struct dc_select {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    uint32_t menu_id;
    uint32_t item_id;
} PACKED dc_select_pkt;

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

/* The packet sent as a timestamp */
typedef struct dc_timestamp {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    char timestamp[28];
} PACKED dc_timestamp_pkt;

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

/* The packet used for the information reply */
typedef struct dc_info_reply {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    uint32_t odd[2];
    char msg[];
} PACKED dc_info_reply_pkt;

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

#ifdef PLAYER_H

/* The packet sent by clients to send their character data */
typedef struct dc_char_data {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    player_t data;
} PACKED dc_char_data_pkt;

/* The packet sent to clients when they join a lobby */
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

#endif

/* The packet sent to clients when they leave a lobby */
typedef struct dc_lobby_leave {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    uint8_t client_id;
    uint8_t leader_id;
    uint16_t padding;
} PACKED dc_lobby_leave_pkt;

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

/* The packet sent to search for a player */
typedef struct dc_guild_search {
    dc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t gc_search;
    uint32_t gc_target;
} PACKED dc_guild_search_pkt;

/* The packet sent to reply to a guild card search */
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

typedef struct pc_guild_reply {
    pc_pkt_hdr_t hdr;
    uint32_t tag;
    uint32_t gc_search;
    uint32_t gc_target;
    uint32_t padding1;
    in_addr_t ip;
    uint16_t port;
    uint16_t padding2;
    uint16_t location[0x44];
    uint32_t menu_id;
    uint32_t item_id;
    uint8_t padding3[0x3C];
    uint16_t name[0x20];
} PACKED pc_guild_reply_pkt;

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

/* The packet sent by clients to create a game */
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

#ifdef PLAYER_H

/* The packet sent to clients to join a game */
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
    uint32_t padding;
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

/* The packet sent to display a large message to the user */
typedef struct dc_msg_box {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    char msg[0];
} PACKED dc_msg_box_pkt;

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

/* The choice search options packet sent to tell clients what they can actually
   search on */
typedef struct dc_choice_search {
    dc_pkt_hdr_t hdr;           /* The flags field says how many entries */
    struct {
        uint16_t menu_id;
        uint16_t item_id;
        char text[0x1C];
    } entries[0];
} PACKED dc_choice_search_pkt;

/* The choice search options packet sent to tell clients what they can actually
   search on */
typedef struct pc_choice_search {
    pc_pkt_hdr_t hdr;           /* The flags field says how many entries */
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
} PACKED dc_choice_set_t;

/* The packet sent as a reply to a choice search */
typedef struct dc_choice_reply {
    dc_pkt_hdr_t hdr;           /* The flags field says how many entries */
    struct {
        uint32_t guildcard;
        char name[0x10];
        char cl_lvl[0x20];
        char location[0x30];
        uint32_t padding;
        in_addr_t ip;
        uint16_t port;
        uint16_t padding2;
        uint32_t menu_id;
        uint32_t item_id;
        uint8_t padding3[0x5C];
    } entries[0];
} PACKED dc_choice_reply_t;

/* The packet sent as a reply to a choice search */
typedef struct pc_choice_reply {
    pc_pkt_hdr_t hdr;           /* The flags field says how many entries */
    struct {
        uint32_t guildcard;
        uint16_t name[0x10];
        uint16_t cl_lvl[0x20];
        uint16_t location[0x30];
        uint32_t padding;
        in_addr_t ip;
        uint16_t port;
        uint16_t padding2;
        uint32_t menu_id;
        uint32_t item_id;
        uint8_t padding3[0x7C];
    } entries[0];
} PACKED pc_choice_reply_t;

/* The packet used to ask for a GBA file */
typedef struct gc_gba_req {
    dc_pkt_hdr_t hdr;
    char filename[16];
} PACKED gc_gba_req_pkt;

/* The packet used to write to the info board */
typedef struct gc_write_info {
    dc_pkt_hdr_t hdr;
    char msg[];
} PACKED gc_write_info_pkt;

/* The packet sent to clients to read the info board */
typedef struct gc_read_info {
    dc_pkt_hdr_t hdr;
    struct {
        char name[0x10];
        char msg[0xAC];
    } entries[0];
} PACKED gc_read_info_pkt;

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
                uint8_t unk2[0x68];
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
                uint8_t unk2[0x94];
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

/* The packet used to set a simple mail autoreply */
typedef struct autoreply_set {
    union {
        dc_pkt_hdr_t dc;
        pc_pkt_hdr_t pc;
    } hdr;
    char msg[];
} PACKED autoreply_set_pkt;

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

#undef PACKED

/* Parameters for the various packets. */
#define MSG1_TYPE                       0x0001
#define WELCOME_TYPE                    0x0002
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
#define GAME_COMMANDD_TYPE              0x006D
#define DONE_BURSTING_TYPE              0x006F
#define SIMPLE_MAIL_TYPE                0x0081
#define LOBBY_LIST_TYPE                 0x0083
#define LOBBY_CHANGE_TYPE               0x0084
#define LOBBY_ARROW_LIST_TYPE           0x0088
#define LOBBY_ARROW_CHANGE_TYPE         0x0089
#define LOBBY_NAME_TYPE                 0x008A
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
#define QUEST_LOAD_DONE_TYPE            0x00AC
#define TEXT_MSG_TYPE                   0x00B0
#define TIMESTAMP_TYPE                  0x00B1
#define EP3_RANK_UPDATE_TYPE            0x00B7
#define EP3_CARD_UPDATE_TYPE            0x00B8
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
#define GAME_COMMAND_CB_TYPE            0x00CB
#define TRADE_0_TYPE                    0x00D0
#define TRADE_1_TYPE                    0x00D1
#define TRADE_2_TYPE                    0x00D2
#define TRADE_3_TYPE                    0x00D3
#define TRADE_4_TYPE                    0x00D4
#define GC_MSG_BOX_TYPE                 0x00D5
#define GC_MSG_BOX_CLOSED_TYPE          0x00D6
#define GC_GBA_FILE_REQ_TYPE            0x00D7
#define GC_INFOBOARD_REQ_TYPE           0x00D8
#define GC_INFOBOARD_WRITE_TYPE         0x00D9
#define LOBBY_EVENT_TYPE                0x00DA
#define GC_VERIFY_LICENSE_TYPE          0x00DB
 
#define DC_WELCOME_LENGTH               0x004C
#define DC_REDIRECT_LENGTH              0x000C
#define DC_TIMESTAMP_LENGTH             0x0020
#define DC_LOBBY_LIST_LENGTH            0x00C4
#define EP3_LOBBY_LIST_LENGTH           0x0100
#define DC_CHAR_DATA_LENGTH             0x0420
#define DC_LOBBY_LEAVE_LENGTH           0x0008
#define PC_GUILD_REPLY_LENGTH           0x0128
#define DC_GUILD_REPLY_LENGTH           0x00C4
#define DC_GAME_JOIN_LENGTH             0x0114
#define GC_GAME_JOIN_LENGTH             0x0114
#define DC_QUEST_INFO_LENGTH            0x0128
#define PC_QUEST_INFO_LENGTH            0x024C
#define DC_QUEST_FILE_LENGTH            0x003C
#define DC_QUEST_CHUNK_LENGTH           0x0418
#define DC_SIMPLE_MAIL_LENGTH           0x0220
#define PC_SIMPLE_MAIL_LENGTH           0x0430

/* Responses to login packets... */
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

#endif /* !PACKETS_H_HAVE_PACKETS */ 
#endif /* !PACKETS_H_HEADERS_ONLY */
