/*
    Sylverant Ship Server
    Copyright (C) 2011 Lawrence Sebald

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

#include <stdlib.h>
#include <string.h>

#include "quests.h"

/* Find a quest by ID, if it exists */
quest_map_elem_t *quest_lookup(quest_map_t *map, uint32_t qid) {
    quest_map_elem_t *i;

    TAILQ_FOREACH(i, map, qentry) {
        if(qid == i->qid) {
            return i;
        }
    }

    return NULL;
}

/* Add a quest to the list */
quest_map_elem_t *quest_add(quest_map_t *map, uint32_t qid) {
    quest_map_elem_t *el;

    /* Create the element */
    el = (quest_map_elem_t *)malloc(sizeof(quest_map_elem_t));

    if(!el) {
        return NULL;
    }

    /* Clean it out, and fill in the id */
    memset(el, 0, sizeof(quest_map_elem_t));
    el->qid = qid;

    /* Add to the list */
    TAILQ_INSERT_TAIL(map, el, qentry);
    return el;
}

/* Clean the list out */
void quest_cleanup(quest_map_t *map) {
    quest_map_elem_t *tmp, *i;

    /* Remove all elements, freeing them as we go along */
    i = TAILQ_FIRST(map);
    while(i) {
        tmp = TAILQ_NEXT(i, qentry);

        free(i);
        i = tmp;
    }

    /* Reinit the map, just in case we reuse it */
    TAILQ_INIT(map);
}

/* Process an entire list of quests read in for a version/language combo. */
int quest_map(quest_map_t *map, sylverant_quest_list_t *list, int version,
              int language) {
    int i, j;
    sylverant_quest_category_t *cat;
    sylverant_quest_t *q;
    quest_map_elem_t *elem;

    if(version >= CLIENT_VERSION_COUNT || language >= CLIENT_LANG_COUNT) {
        return -1;
    }

    for(i = 0; i < list->cat_count; ++i) {
        cat = &list->cats[i];

        for(j = 0; j < cat->quest_count; ++j) {
            q = &cat->quests[j];

            /* Find the quest if we have it */
            elem = quest_lookup(map, q->qid);

            if(elem) {
                elem->qptr[version][language] = q;
            }
            else {
                elem = quest_add(map, q->qid);

                if(!elem) {
                    return -2;
                }

                elem->qptr[version][language] = q;
            }

            q->user_data = elem;
        }
    }

    return 0;
}
