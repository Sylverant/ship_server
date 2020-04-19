/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018,
                  2019, 2020 Lawrence Sebald

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

#ifndef CLIENTS_H_COUNTS
#define CLIENTS_H_COUNTS

#define CLIENT_VERSION_COUNT    6
#define CLIENT_LANG_COUNT       8

#endif /* !CLIENTS_H_COUNTS */

#ifndef CLIENTS_H_COUNTS_ONLY
#ifndef CLIENTS_H
#define CLIENTS_H

#include <time.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/queue.h>
#include <netinet/in.h>

#include <sylverant/config.h>
#include <sylverant/items.h>

#include "ship.h"
#include "block.h"
#include "player.h"

/* Pull in the packet header types. */
#define PACKETS_H_HEADERS_ONLY
#include "packets.h"
#undef PACKETS_H_HEADERS_ONLY

#include <sylverant/encryption.h>

/* Forward declarations. */
struct lobby;

#ifndef LOBBY_DEFINED
#define LOBBY_DEFINED
typedef struct lobby lobby_t;
#endif

#define CLIENT_IGNORE_LIST_SIZE     10
#define CLIENT_MAX_QSTACK           32

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

/* Data that is set on the client via the 0xE6 packet */
typedef struct bb_security_data {
    uint32_t magic;                     /* Must be 0xDEADBEEF */
    uint8_t slot;                       /* Selected character */
    uint8_t sel_char;                   /* Have they selected a character? */
    uint8_t reserved[34];               /* Set to 0 */
} PACKED bb_security_data_t;

#undef PACKED

/* Ship server client structure. */
struct ship_client {
    TAILQ_ENTRY(ship_client) qentry;

    pthread_mutex_t mutex;
    pkt_header_t pkt;

    CRYPT_SETUP ckey;
    CRYPT_SETUP skey;

    int version;
    int sock;
    int hdr_size;
    int client_id;

    int language_code;
    int cur_area;
    int recvbuf_cur;
    int recvbuf_size;

    int sendbuf_cur;
    int sendbuf_size;
    int sendbuf_start;
    int item_count;

    int autoreply_len;
    int lobby_id;

    float x;
    float y;
    float z;
    float w;

    struct sockaddr_storage ip_addr;

    uint32_t guildcard;
    uint32_t flags;
    uint32_t arrow;

    uint32_t next_item[4];
    uint32_t ignore_list[CLIENT_IGNORE_LIST_SIZE];

    uint32_t last_info_req;

    float drop_x;
    float drop_z;
    uint32_t drop_area;
    uint32_t drop_item;
    uint32_t drop_amt;

    uint32_t privilege;
    uint8_t cc_char;
    uint8_t q_lang;
    uint8_t autoreply_on;

    item_t items[30];

    block_t *cur_block;
    lobby_t *cur_lobby;
    player_t *pl;

    unsigned char *recvbuf;
    unsigned char *sendbuf;
    void *autoreply;
    FILE *logfile;

    char *infoboard;                    /* Points into the player struct. */
    uint8_t *c_rank;                    /* Points into the player struct. */
    lobby_t *create_lobby;
    uint32_t *blacklist;                /* Points into the player struct. */

    uint32_t *next_maps;
    uint32_t *enemy_kills;
    sylverant_limits_t *limits;

    time_t last_message;
    time_t last_sent;
    time_t join_time;
    time_t login_time;

    bb_security_data_t sec_data;
    sylverant_bb_db_char_t *bb_pl;
    sylverant_bb_db_opts_t *bb_opts;

    int script_ref;
    uint64_t aoe_timer;

    uint32_t q_stack[CLIENT_MAX_QSTACK];
    int q_stack_top;

#ifdef DEBUG
    uint8_t sdrops_ver;
    uint8_t sdrops_ep;
    uint8_t sdrops_diff;
    uint8_t sdrops_section;
#endif
};

#define CLIENT_PRIV_LOCAL_GM    0x01
#define CLIENT_PRIV_GLOBAL_GM   0x02
#define CLIENT_PRIV_LOCAL_ROOT  0x04
#define CLIENT_PRIV_GLOBAL_ROOT 0x08

/* Character classes */
typedef enum client_classes {
    HUmar = 0,
    HUnewearl,
    HUcast,
    RAmar,
    RAcast,
    RAcaseal,
    FOmarl,
    FOnewm,
    FOnewearl,
    HUcaseal,
    FOmar,
    RAmarl,
    DCPCClassMax = FOnewearl
} client_class_t;

/* String versions of the character classes. */
static const char *classes[12] __attribute__((unused)) = {
    "HUmar", "HUnewearl", "HUcast",
    "RAmar", "RAcast", "RAcaseal",
    "FOmarl", "FOnewm", "FOnewearl",
    "HUcaseal", "FOmar", "RAmarl"
};

#ifndef SHIP_CLIENT_DEFINED
#define SHIP_CLIENT_DEFINED
typedef struct ship_client ship_client_t;
#endif

TAILQ_HEAD(client_queue, ship_client);

/* The key for accessing our thread-specific receive buffer. */
extern pthread_key_t recvbuf_key;

/* The key used for the thread-specific send buffer. */
extern pthread_key_t sendbuf_key;

/* Possible values for the type field of ship_client_t */
#define CLIENT_TYPE_SHIP        0
#define CLIENT_TYPE_BLOCK       1

/* Possible values for the version field of ship_client_t */
#define CLIENT_VERSION_DCV1     0
#define CLIENT_VERSION_DCV2     1
#define CLIENT_VERSION_PC       2
#define CLIENT_VERSION_GC       3
#define CLIENT_VERSION_EP3      4
#define CLIENT_VERSION_BB       5

/* Language codes. */
#define CLIENT_LANG_JAPANESE        0
#define CLIENT_LANG_ENGLISH         1
#define CLIENT_LANG_GERMAN          2
#define CLIENT_LANG_FRENCH          3
#define CLIENT_LANG_SPANISH         4
#define CLIENT_LANG_CHINESE_SIMP    5
#define CLIENT_LANG_CHINESE_TRAD    6
#define CLIENT_LANG_KOREAN          7

#define CLIENT_FLAG_HDR_READ        0x00000001
#define CLIENT_FLAG_GOT_05          0x00000002
#define CLIENT_FLAG_INVULNERABLE    0x00000004
#define CLIENT_FLAG_INFINITE_TP     0x00000008
#define CLIENT_FLAG_DISCONNECTED    0x00000010
#define CLIENT_FLAG_TYPE_SHIP       0x00000020
#define CLIENT_FLAG_SENT_MOTD       0x00000040
#define CLIENT_FLAG_SHOW_DCPC_ON_GC 0x00000080
#define CLIENT_FLAG_LOGGED_IN       0x00000100
#define CLIENT_FLAG_STFU            0x00000200
#define CLIENT_FLAG_BURSTING        0x00000400
#define CLIENT_FLAG_OVERRIDE_GAME   0x00000800
#define CLIENT_FLAG_IPV6            0x00001000
#define CLIENT_FLAG_AUTO_BACKUP     0x00002000
#define CLIENT_FLAG_SERVER_DROPS    0x00004000
#define CLIENT_FLAG_GC_PROTECT      0x00008000
#define CLIENT_FLAG_IS_NTE          0x00010000
#define CLIENT_FLAG_TRACK_INVENTORY 0x00020000
#define CLIENT_FLAG_TRACK_KILLS     0x00040000
#define CLIENT_FLAG_QLOAD_DONE      0x00080000
#define CLIENT_FLAG_DBG_SDROPS      0x00100000
#define CLIENT_FLAG_LEGIT           0x00200000
#define CLIENT_FLAG_GC_MSG_BOXES    0x00400000
#define CLIENT_FLAG_CDATA_CHECK     0x00800000
#define CLIENT_FLAG_ALWAYS_LEGIT    0x01000000
#define CLIENT_FLAG_WAIT_QPING      0x02000000
#define CLIENT_FLAG_QSTACK_LOCK     0x04000000
#define CLIENT_FLAG_WORD_CENSOR     0x08000000

/* Technique numbers */
#define TECHNIQUE_FOIE              0
#define TECHNIQUE_GIFOIE            1
#define TECHNIQUE_RAFOIE            2
#define TECHNIQUE_BARTA             3
#define TECHNIQUE_GIBARTA           4
#define TECHNIQUE_RABARTA           5
#define TECHNIQUE_ZONDE             6
#define TECHNIQUE_GIZONDE           7
#define TECHNIQUE_RAZONDE           8
#define TECHNIQUE_GRANTS            9
#define TECHNIQUE_DEBAND            10
#define TECHNIQUE_JELLEN            11
#define TECHNIQUE_ZALURE            12
#define TECHNIQUE_SHIFTA            13
#define TECHNIQUE_RYUKER            14
#define TECHNIQUE_RESTA             15
#define TECHNIQUE_ANTI              16
#define TECHNIQUE_REVERSER          17
#define TECHNIQUE_MEGID             18

/* The list of language codes for the quest directories. */
static const char language_codes[][3] __attribute__((unused)) = {
    "jp", "en", "de", "fr", "es", "cs", "ct", "kr"
};

/* The list of version codes for the quest directories. */
static const char version_codes[][3] __attribute__((unused)) = {
    "v1", "v2", "pc", "gc", "e3", "bb"
};

/* Sizes of the headers sent on packets */
static const int hdr_sizes[] __attribute__((unused)) = {
    4, 4, 4, 4, 4, 8
};

/* Initialize the clients system, allocating any thread specific keys */
int client_init(sylverant_ship_t *cfg);

/* Clean up the clients system. */
void client_shutdown(void);

/* Create a new connection, storing it in the list of clients. */
ship_client_t *client_create_connection(int sock, int version, int type,
                                        struct client_queue *clients,
                                        ship_t *ship, block_t *block,
                                        struct sockaddr *ip, socklen_t size);

/* Destroy a connection, closing the socket and removing it from the list. */
void client_destroy_connection(ship_client_t *c, struct client_queue *clients);

/* Read data from a client that is connected to any port. */
int client_process_pkt(ship_client_t *c);

/* Retrieve the thread-specific recvbuf for the current thread. */
uint8_t *get_recvbuf(void);

/* Set up a simple mail autoreply. */
int client_set_autoreply(ship_client_t *c, void *buf, uint16_t len);

/* Disable the user's simple mail autoreply (if set). */
int client_disable_autoreply(ship_client_t *c);

/* Check if a client has blacklisted someone. */
int client_has_blacklisted(ship_client_t *c, uint32_t gc);

/* Check if a client has /ignore'd someone. */
int client_has_ignored(ship_client_t *c, uint32_t gc);

/* Send a message to a client telling them that a friend has logged on/off */
void client_send_friendmsg(ship_client_t *c, int on, const char *fname,
                           const char *ship, uint32_t block, const char *nick);

/* Give a Blue Burst client some experience. */
int client_give_exp(ship_client_t *c, uint32_t exp);

/* Give a Blue Burst client some free level ups. */
int client_give_level(ship_client_t *c, uint32_t level_req);

/* Give a PSOv2 client some free level ups. */
int client_give_level_v2(ship_client_t *c, uint32_t level_req);

/* Check if a client's newly sent character data looks corrupted. */
int client_check_character(ship_client_t *c, player_t *pl, uint8_t ver);

/* Run a legit check on a given client. */
int client_legit_check(ship_client_t *c, sylverant_limits_t *limits);

#ifdef ENABLE_LUA
#include <lua.h>

int client_register_lua(lua_State *l);
#endif

#endif /* !CLIENTS_H */
#endif /* !CLIENTS_H_COUNTS_ONLY */
