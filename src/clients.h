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

#ifndef CLIENTS_H_COUNTS
#define CLIENTS_H_COUNTS

#define CLIENT_VERSION_COUNT    6
#define CLIENT_LANG_COUNT       8

#endif /* !CLIENTS_H_COUNTS */

#ifndef CLIENTS_H_COUNTS_ONLY
#ifndef CLIENTS_H
#define CLIENTS_H

#ifdef HAVE_PYTHON
#include <Python.h>
#endif

#include <time.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/queue.h>
#include <netinet/in.h>

#include <sylverant/config.h>

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

    uint8_t privilege;
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

    time_t last_message;
    time_t last_sent;
    time_t join_time;

    bb_security_data_t sec_data;
    sylverant_bb_db_char_t *bb_pl;
    sylverant_bb_db_opts_t *bb_opts;

#ifdef HAVE_PYTHON
    PyObject *pyobj;
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

/* The list of language codes for the quest directories. */
static const char language_codes[][3] __attribute__((unused)) = {
    "jp", "en", "de", "fr", "es", "cs", "ct", "kr"
};

/* The list of version codes for the quest directories. */
static const char version_codes[][3] __attribute__((unused)) = {
    "v1", "v2", "pc", "gc", "e3"
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

#ifdef HAVE_PYTHON

/* Initialize the Client class for Python */
void client_init_scripting(PyObject *m);

#endif

#endif /* !CLIENTS_H */
#endif /* !CLIENTS_H_COUNTS_ONLY */
