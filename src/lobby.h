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

#ifndef LOBBY_H
#define LOBBY_H

#include <pthread.h>
#include <inttypes.h>
#include <sys/queue.h>

#define LOBBY_MAX_CLIENTS 12

/* Forward declaration. */
struct ship_client;
struct block;

#ifndef SHIP_CLIENT_DEFINED
#define SHIP_CLIENT_DEFINED
typedef struct ship_client ship_client_t;
#endif

#ifndef BLOCK_DEFINED
#define BLOCK_DEFINED
typedef struct block block_t;
#endif

struct lobby {
    TAILQ_ENTRY(lobby) qentry;

    pthread_mutex_t mutex;

    uint32_t lobby_id;
    uint32_t type;
    int max_clients;
    int num_clients;

    block_t *block;
    uint32_t flags;

    uint8_t leader_id;
    uint8_t difficulty;
    uint8_t battle;
    uint8_t challenge;

    uint8_t v2;
    uint8_t section;
    uint8_t event;
    uint8_t episode;

    int version;
    uint32_t min_level;
    uint32_t max_level;
    uint32_t rand_seed;

    char name[16];
    char passwd[16];
    uint32_t maps[0x20];

    ship_client_t *clients[LOBBY_MAX_CLIENTS];
};

#ifndef LOBBY_DEFINED
#define LOBBY_DEFINED
typedef struct lobby lobby_t;
#endif

TAILQ_HEAD(lobby_queue, lobby);

/* Possible values for the type parameter. */
#define LOBBY_TYPE_DEFAULT      0x00000001
#define LOBBY_TYPE_GAME         0x00000002

/* Possible values for the flags parameter. */
#define LOBBY_FLAG_BURSTING     0x00000001
#define LOBBY_FLAG_QUESTING     0x00000002
#define LOBBY_FLAG_QUESTSEL     0x00000004

/* The required level for various difficulties. */
const static int game_required_level[4] = { 0, 20, 40, 80 };

lobby_t *lobby_create_default(block_t *block, uint32_t lobby_id);
lobby_t *lobby_create_game(block_t *block, char name[16], char passwd[16],
                           uint8_t difficulty, uint8_t battle, uint8_t chal,
                           uint8_t v2, int version, uint8_t section,
                           uint8_t event, uint8_t episode);
void lobby_destroy(lobby_t *l);

/* Add the client to any available lobby on the current block. */
int lobby_add_to_any(ship_client_t *c);

/* Send a packet to all people in a lobby. */
int lobby_send_pkt_dc(lobby_t *l, ship_client_t *c, void *hdr);

/* Move the client to the requested lobby, if possible. */
int lobby_change_lobby(ship_client_t *c, lobby_t *req);

/* Remove a player from a lobby without changing their lobby (for instance, if
   they disconnected). */
int lobby_remove_player(ship_client_t *c);

/* Send an information reply packet with information about the lobby. */
int lobby_info_reply(ship_client_t *c, uint32_t lobby);

#endif /* !LOBBY_H */
