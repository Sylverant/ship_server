/*
    Sylverant Ship Server
    Copyright (C) 2011, 2020 Lawrence Sebald

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

#ifndef QUESTS_H
#define QUESTS_H

#include <stdio.h>
#include <inttypes.h>
#include <sys/queue.h>
#include <sylverant/quest.h>

#define CLIENTS_H_COUNTS_ONLY
#include "clients.h"
#undef CLIENTS_H_COUNTS_ONLY

#ifndef SHIP_DEFINED
#define SHIP_DEFINED
struct ship;
typedef struct ship ship_t;
#endif

#ifndef QENEMY_DEFINED
#define QENEMY_DEFINED
typedef struct sylverant_quest_enemy qenemy_t;
#endif

typedef struct quest_map_elem {
    TAILQ_ENTRY(quest_map_elem) qentry;
    uint32_t qid;

    sylverant_quest_t *qptr[CLIENT_VERSION_COUNT][CLIENT_LANG_COUNT];
} quest_map_elem_t;

TAILQ_HEAD(quest_map, quest_map_elem);
typedef struct quest_map quest_map_t;

/* Find a quest by ID, if it exists */
quest_map_elem_t *quest_lookup(quest_map_t *map, uint32_t qid);

/* Add a quest to the list */
quest_map_elem_t *quest_add(quest_map_t *map, uint32_t qid);

/* Clean the list out */
void quest_cleanup(quest_map_t *map);

/* Process an entire list of quests read in for a version/language combo. */
int quest_map(quest_map_t *map, sylverant_quest_list_t *list, int version,
              int language);

/* Build/rebuild the quest enemy/object data cache. */
int quest_cache_maps(ship_t *s, quest_map_t *map, const char *dir);

/* Search an enemy list from a quest for an entry. */
uint32_t quest_search_enemy_list(uint32_t id, qenemy_t *list, int len, int sd);

#endif /* !QUESTS_H */
