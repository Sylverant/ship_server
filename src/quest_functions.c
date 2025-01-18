/*
    Sylverant Ship Server
    Copyright (C) 2019, 2020, 2021, 2025 Lawrence Sebald

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

#include <time.h>
#include <stdint.h>
#include <string.h>

#include "clients.h"
#include "lobby.h"
#include "quest_functions.h"
#include "ship_packets.h"
#include "smutdata.h"
#include "scripts.h"

static uint32_t get_section_id(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 1)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    /* Are we requesting everyone or just one person? */
    if(c->q_stack[3] == 0xFFFFFFFF) {
        if(c->q_stack[2] != 4)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255 || c->q_stack[5] > 255 ||
           c->q_stack[6] > 255 || c->q_stack[7] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[0])
            send_sync_register(c, c->q_stack[4],
                               l->clients[0]->pl->v1.section);
        else
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);

        if(l->clients[1])
            send_sync_register(c, c->q_stack[5],
                               l->clients[1]->pl->v1.section);
        else
            send_sync_register(c, c->q_stack[5], 0xFFFFFFFF);

        if(l->clients[2])
            send_sync_register(c, c->q_stack[6],
                               l->clients[2]->pl->v1.section);
        else
            send_sync_register(c, c->q_stack[6], 0xFFFFFFFF);

        if(l->clients[3])
            send_sync_register(c, c->q_stack[7],
                               l->clients[3]->pl->v1.section);
        else
            send_sync_register(c, c->q_stack[7], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else if(c->q_stack[3] < 4) {
        if(c->q_stack[2] != 1)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[c->q_stack[3]])
            send_sync_register(c, c->q_stack[4],
                               l->clients[c->q_stack[3]]->pl->v1.section);
        else
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else {
        return QUEST_FUNC_RET_INVALID_ARG;
    }
}

uint32_t get_time(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 0)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(c->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(c->q_stack[3] > 255)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    send_sync_register(c, c->q_stack[3], (uint32_t)time(NULL));
    return QUEST_FUNC_RET_NO_ERROR;
}

uint32_t get_client_count(ship_client_t *c, lobby_t *l, int which) {
    if(c->q_stack[1] != 0)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(c->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(c->q_stack[3] > 255)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    if(which == 0)      /* Team clients */
        send_sync_register(c, c->q_stack[3], l->num_clients);
    else if(which == 1) /* Ship clients */
        send_sync_register(c, c->q_stack[3], ship->num_clients);
    else if(which == 2) /* Block clients */
        send_sync_register(c, c->q_stack[3], c->cur_block->num_clients);

    return QUEST_FUNC_RET_NO_ERROR;
}

static uint32_t get_char_class(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 1)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    /* Are we requesting everyone or just one person? */
    if(c->q_stack[3] == 0xFFFFFFFF) {
        if(c->q_stack[2] != 4)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255 || c->q_stack[5] > 255 ||
           c->q_stack[6] > 255 || c->q_stack[7] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[0])
            send_sync_register(c, c->q_stack[4],
                               l->clients[0]->pl->v1.ch_class);
        else
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);

        if(l->clients[1])
            send_sync_register(c, c->q_stack[5],
                               l->clients[1]->pl->v1.ch_class);
        else
            send_sync_register(c, c->q_stack[5], 0xFFFFFFFF);

        if(l->clients[2])
            send_sync_register(c, c->q_stack[6],
                               l->clients[2]->pl->v1.ch_class);
        else
            send_sync_register(c, c->q_stack[6], 0xFFFFFFFF);

        if(l->clients[3])
            send_sync_register(c, c->q_stack[7],
                               l->clients[3]->pl->v1.ch_class);
        else
            send_sync_register(c, c->q_stack[7], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else if(c->q_stack[3] < 4) {
        if(c->q_stack[2] != 1)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[c->q_stack[3]])
            send_sync_register(c, c->q_stack[4],
                               l->clients[c->q_stack[3]]->pl->v1.ch_class);
        else
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else {
        return QUEST_FUNC_RET_INVALID_ARG;
    }
}

static int genders[12] = { 0, 1, 0, 0, 0, 1, 1, 0, 1, 1, 0, 1 };
static int races[12] = { 0, 1, 2, 0, 2, 2, 0, 1, 1, 2, 0, 0 };
static int jobs[12] = { 0, 0, 0, 1, 1, 1, 2, 2, 2, 0, 2, 1 };

#define ATTR(x, y) (((x) < 12) ? y[(x)] : -1)
#define GENDER(x) ATTR((x), genders)
#define RACE(x) ATTR((x), races)
#define JOB(x) ATTR((x), jobs)

static uint32_t get_char_gender(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 1)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    /* Are we requesting everyone or just one person? */
    if(c->q_stack[3] == 0xFFFFFFFF) {
        if(c->q_stack[2] != 4)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255 || c->q_stack[5] > 255 ||
           c->q_stack[6] > 255 || c->q_stack[7] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[0])
            send_sync_register(c, c->q_stack[4],
                               GENDER(l->clients[0]->pl->v1.ch_class));
        else
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);

        if(l->clients[1])
            send_sync_register(c, c->q_stack[5],
                               GENDER(l->clients[1]->pl->v1.ch_class));
        else
            send_sync_register(c, c->q_stack[5], 0xFFFFFFFF);

        if(l->clients[2])
            send_sync_register(c, c->q_stack[6],
                               GENDER(l->clients[2]->pl->v1.ch_class));
        else
            send_sync_register(c, c->q_stack[6], 0xFFFFFFFF);

        if(l->clients[3])
            send_sync_register(c, c->q_stack[7],
                               GENDER(l->clients[3]->pl->v1.ch_class));
        else
            send_sync_register(c, c->q_stack[7], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else if(c->q_stack[3] < 4) {
        int cl = c->q_stack[3];

        if(c->q_stack[2] != 1)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[cl])
            send_sync_register(c, c->q_stack[4],
                               GENDER(l->clients[cl]->pl->v1.ch_class));
        else
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else {
        return QUEST_FUNC_RET_INVALID_ARG;
    }
}

static uint32_t get_char_race(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 1)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    /* Are we requesting everyone or just one person? */
    if(c->q_stack[3] == 0xFFFFFFFF) {
        if(c->q_stack[2] != 4)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255 || c->q_stack[5] > 255 ||
           c->q_stack[6] > 255 || c->q_stack[7] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[0])
            send_sync_register(c, c->q_stack[4],
                               RACE(l->clients[0]->pl->v1.ch_class));
        else
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);

        if(l->clients[1])
            send_sync_register(c, c->q_stack[5],
                               RACE(l->clients[1]->pl->v1.ch_class));
        else
            send_sync_register(c, c->q_stack[5], 0xFFFFFFFF);

        if(l->clients[2])
            send_sync_register(c, c->q_stack[6],
                               RACE(l->clients[2]->pl->v1.ch_class));
        else
            send_sync_register(c, c->q_stack[6], 0xFFFFFFFF);

        if(l->clients[3])
            send_sync_register(c, c->q_stack[7],
                               RACE(l->clients[3]->pl->v1.ch_class));
        else
            send_sync_register(c, c->q_stack[7], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else if(c->q_stack[3] < 4) {
        int cl = c->q_stack[3];

        if(c->q_stack[2] != 1)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[cl])
            send_sync_register(c, c->q_stack[4],
                               RACE(l->clients[cl]->pl->v1.ch_class));
        else
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else {
        return QUEST_FUNC_RET_INVALID_ARG;
    }
}

static uint32_t get_char_job(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 1)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    /* Are we requesting everyone or just one person? */
    if(c->q_stack[3] == 0xFFFFFFFF) {
        if(c->q_stack[2] != 4)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255 || c->q_stack[5] > 255 ||
           c->q_stack[6] > 255 || c->q_stack[7] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[0])
            send_sync_register(c, c->q_stack[4],
                               JOB(l->clients[0]->pl->v1.ch_class));
        else
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);

        if(l->clients[1])
            send_sync_register(c, c->q_stack[5],
                               JOB(l->clients[1]->pl->v1.ch_class));
        else
            send_sync_register(c, c->q_stack[5], 0xFFFFFFFF);

        if(l->clients[2])
            send_sync_register(c, c->q_stack[6],
                               JOB(l->clients[2]->pl->v1.ch_class));
        else
            send_sync_register(c, c->q_stack[6], 0xFFFFFFFF);

        if(l->clients[3])
            send_sync_register(c, c->q_stack[7],
                               JOB(l->clients[3]->pl->v1.ch_class));
        else
            send_sync_register(c, c->q_stack[7], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else if(c->q_stack[3] < 4) {
        int cl = c->q_stack[3];

        if(c->q_stack[2] != 1)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[cl])
            send_sync_register(c, c->q_stack[4],
                               JOB(l->clients[cl]->pl->v1.ch_class));
        else
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else {
        return QUEST_FUNC_RET_INVALID_ARG;
    }
}

static uint32_t get_client_floor(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 1)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    /* Are we requesting everyone or just one person? */
    if(c->q_stack[3] == 0xFFFFFFFF) {
        if(c->q_stack[2] != 4)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255 || c->q_stack[5] > 255 ||
           c->q_stack[6] > 255 || c->q_stack[7] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[0])
            send_sync_register(c, c->q_stack[4], l->clients[0]->cur_area);
        else
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);

        if(l->clients[1])
            send_sync_register(c, c->q_stack[5], l->clients[1]->cur_area);
        else
            send_sync_register(c, c->q_stack[5], 0xFFFFFFFF);

        if(l->clients[2])
            send_sync_register(c, c->q_stack[6], l->clients[2]->cur_area);
        else
            send_sync_register(c, c->q_stack[6], 0xFFFFFFFF);

        if(l->clients[3])
            send_sync_register(c, c->q_stack[7], l->clients[3]->cur_area);
        else
            send_sync_register(c, c->q_stack[7], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else if(c->q_stack[3] < 4) {
        int cl = c->q_stack[3];

        if(c->q_stack[2] != 1)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[cl])
            send_sync_register(c, c->q_stack[4], l->clients[cl]->cur_area);
        else
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else {
        return QUEST_FUNC_RET_INVALID_ARG;
    }
}

static uint32_t get_client_position(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 1)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    /* Are we requesting everyone or just one person? */
    if(c->q_stack[3] == 0xFFFFFFFF) {
        if(c->q_stack[2] != 4)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255 || c->q_stack[5] > 255 ||
           c->q_stack[6] > 255 || c->q_stack[7] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[0]) {
            send_sync_register(c, c->q_stack[4], (uint32_t)l->clients[0]->x);
            send_sync_register(c, c->q_stack[4] + 1,
                               (uint32_t)l->clients[0]->y);
            send_sync_register(c, c->q_stack[4] + 2,
                               (uint32_t)l->clients[0]->z);
        }
        else {
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[4] + 1, 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[4] + 2, 0xFFFFFFFF);
        }

        if(l->clients[1]) {
            send_sync_register(c, c->q_stack[5], (uint32_t)l->clients[1]->x);
            send_sync_register(c, c->q_stack[5] + 1,
                               (uint32_t)l->clients[1]->y);
            send_sync_register(c, c->q_stack[5] + 2,
                               (uint32_t)l->clients[1]->z);
        }
        else {
            send_sync_register(c, c->q_stack[5], 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[5] + 1, 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[5] + 2, 0xFFFFFFFF);
        }

        if(l->clients[2]) {
            send_sync_register(c, c->q_stack[6], (uint32_t)l->clients[2]->x);
            send_sync_register(c, c->q_stack[6] + 1,
                               (uint32_t)l->clients[2]->y);
            send_sync_register(c, c->q_stack[6] + 2,
                               (uint32_t)l->clients[2]->z);
        }
        else {
            send_sync_register(c, c->q_stack[6], 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[6] + 1, 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[6] + 2, 0xFFFFFFFF);
        }

        if(l->clients[3]) {
            send_sync_register(c, c->q_stack[7], (uint32_t)l->clients[3]->x);
            send_sync_register(c, c->q_stack[7] + 1,
                               (uint32_t)l->clients[3]->y);
            send_sync_register(c, c->q_stack[7] + 2,
                               (uint32_t)l->clients[3]->z);
        }
        else {
            send_sync_register(c, c->q_stack[7], 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[7] + 1, 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[7] + 2, 0xFFFFFFFF);
        }

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else if(c->q_stack[3] < 4) {
        int cl = c->q_stack[3];

        if(c->q_stack[2] != 1)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[cl]) {
            send_sync_register(c, c->q_stack[4], (uint32_t)l->clients[cl]->x);
            send_sync_register(c, c->q_stack[4] + 1,
                               (uint32_t)l->clients[cl]->y);
            send_sync_register(c, c->q_stack[4] + 2,
                               (uint32_t)l->clients[cl]->z);
        }
        else {
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[4] + 1, 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[4] + 2, 0xFFFFFFFF);
        }

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else {
        return QUEST_FUNC_RET_INVALID_ARG;
    }
}

static uint32_t get_random_integer(ship_client_t *c, lobby_t *l) {
    uint32_t min, max, rnd;

    if(c->q_stack[1] != 2)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(c->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(c->q_stack[5] > 255)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    min = c->q_stack[3];
    max = c->q_stack[4];

    if(min >= max)
        return QUEST_FUNC_RET_INVALID_ARG;

    max -= min;

    rnd = (uint32_t)(mt19937_genrand_int32(&l->block->rng) %
                     ((uint64_t)max + 1) + min);

    send_sync_register(c, c->q_stack[5], rnd);
    return QUEST_FUNC_RET_NO_ERROR;
}

static uint32_t get_quest_sflag(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 1)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(c->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(c->q_stack[4] > 255)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    /* Send the request to the shipgate... */
    if(shipgate_send_qflag(&ship->sg, c, 0, c->q_stack[3], l->qid, 0, 0))
        return QUEST_FUNC_RET_SHIPGATE_ERR;

    /* Set the lock and make sure that we don't return to the client yet. */
    c->flags |= CLIENT_FLAG_QSTACK_LOCK;
    return QUEST_FUNC_RET_NOT_YET;
}

static uint32_t set_quest_sflag(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 2)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(c->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(c->q_stack[4] & 0xFFFF0000)
        return QUEST_FUNC_RET_INVALID_ARG;

    if(c->q_stack[5] > 255)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    /* Send the request to the shipgate... */
    if(shipgate_send_qflag(&ship->sg, c, 1, c->q_stack[3], l->qid,
                           c->q_stack[4], 0))
        return QUEST_FUNC_RET_SHIPGATE_ERR;

    /* Set the lock and make sure that we don't return to the client yet. */
    c->flags |= CLIENT_FLAG_QSTACK_LOCK;
    return QUEST_FUNC_RET_NOT_YET;
}

static uint32_t get_quest_lflag(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 1)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(c->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(c->q_stack[4] > 255)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    /* Send the request to the shipgate... */
    if(shipgate_send_qflag(&ship->sg, c, 0, c->q_stack[3], l->qid, 0,
                           QFLAG_LONG_FLAG))
        return QUEST_FUNC_RET_SHIPGATE_ERR;

    /* Set the lock and make sure that we don't return to the client yet. */
    c->flags |= CLIENT_FLAG_QSTACK_LOCK;
    return QUEST_FUNC_RET_NOT_YET;
}

static uint32_t set_quest_lflag(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 2)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(c->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(c->q_stack[5] > 255)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    /* Send the request to the shipgate... */
    if(shipgate_send_qflag(&ship->sg, c, 1, c->q_stack[3], l->qid,
                           c->q_stack[4], QFLAG_LONG_FLAG))
        return QUEST_FUNC_RET_SHIPGATE_ERR;

    /* Set the lock and make sure that we don't return to the client yet. */
    c->flags |= CLIENT_FLAG_QSTACK_LOCK;
    return QUEST_FUNC_RET_NOT_YET;
}

static uint32_t del_quest_sflag(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 1)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(c->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(c->q_stack[3] > 255)
        return QUEST_FUNC_RET_INVALID_ARG;

    if(c->q_stack[5] > 255)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    /* Send the request to the shipgate... */
    if(shipgate_send_qflag(&ship->sg, c, 1, c->q_stack[3], l->qid,
                           c->q_stack[4], QFLAG_DELETE_FLAG))
        return QUEST_FUNC_RET_SHIPGATE_ERR;

    /* Set the lock and make sure that we don't return to the client yet. */
    c->flags |= CLIENT_FLAG_QSTACK_LOCK;
    return QUEST_FUNC_RET_NOT_YET;
}

static uint32_t del_quest_lflag(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 1)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(c->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(c->q_stack[3] > 255)
        return QUEST_FUNC_RET_INVALID_ARG;

    if(c->q_stack[5] > 255)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    /* Send the request to the shipgate... */
    if(shipgate_send_qflag(&ship->sg, c, 1, c->q_stack[3], l->qid,
                           c->q_stack[4], QFLAG_LONG_FLAG | QFLAG_DELETE_FLAG))
        return QUEST_FUNC_RET_SHIPGATE_ERR;

    /* Set the lock and make sure that we don't return to the client yet. */
    c->flags |= CLIENT_FLAG_QSTACK_LOCK;
    return QUEST_FUNC_RET_NOT_YET;
}

static uint32_t word_censor_check(ship_client_t *c, lobby_t *l, int sr) {
    char str[25];
    uint32_t i;
    int rv;

    if(c->q_stack[1] < 1 || c->q_stack[1] > 24)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(c->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(c->q_stack[c->q_stack[1] + 3] > 255)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    /* Read in the string... */
    if(!sr) {
        for(i = 0; i < c->q_stack[1]; ++i) {
            if(c->q_stack[i + 3] > 127)
                return QUEST_FUNC_RET_INVALID_ARG;

            str[i] = (char)c->q_stack[i + 3];
        }
    }
    else {
        for(i = 0; i < c->q_stack[1]; ++i) {
            if(c->q_stack[i + 3] > 26)
                return QUEST_FUNC_RET_INVALID_ARG;

            if(c->q_stack[i + 3] == 0) {
                str[i] = 0;
                break;
            }

            str[i] = (char)(c->q_stack[i + 3] + 64);
        }
    }

    str[i] = 0;

    /* Check it against the censor. */
    rv = smutdata_check_string(str, SMUTDATA_WEST);
    send_sync_register(c, c->q_stack[c->q_stack[1] + 3], (uint32_t)rv);

    return QUEST_FUNC_RET_NO_ERROR;
}

static uint32_t get_team_seed(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 0)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(c->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(c->q_stack[3] > 255)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    send_sync_register(c, c->q_stack[3], l->rand_seed);
    return QUEST_FUNC_RET_NO_ERROR;
}

static uint32_t get_pos_updates(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 1)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    /* Are we requesting everyone or just one person? */
    if(c->q_stack[3] == 0xFFFFFFFF) {
        if(c->q_stack[2] != 4)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255 || c->q_stack[5] > 255 ||
           c->q_stack[6] > 255 || c->q_stack[7] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        l->qpos_regs[0][c->client_id] = c->q_stack[4];
        l->qpos_regs[1][c->client_id] = c->q_stack[5];
        l->qpos_regs[2][c->client_id] = c->q_stack[6];
        l->qpos_regs[3][c->client_id] = c->q_stack[7];

        if(l->clients[0]) {
            send_sync_register(c, c->q_stack[4], (uint32_t)l->clients[0]->x);
            send_sync_register(c, c->q_stack[4] + 1,
                               (uint32_t)l->clients[0]->y);
            send_sync_register(c, c->q_stack[4] + 2,
                               (uint32_t)l->clients[0]->z);
            send_sync_register(c, c->q_stack[4] + 3,
                               (uint32_t)l->clients[0]->cur_area);
        }
        else {
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[4] + 1, 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[4] + 2, 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[4] + 3, 0xFFFFFFFF);
        }

        if(l->clients[1]) {
            send_sync_register(c, c->q_stack[5], (uint32_t)l->clients[1]->x);
            send_sync_register(c, c->q_stack[5] + 1,
                               (uint32_t)l->clients[1]->y);
            send_sync_register(c, c->q_stack[5] + 2,
                               (uint32_t)l->clients[1]->z);
            send_sync_register(c, c->q_stack[5] + 3,
                               (uint32_t)l->clients[1]->cur_area);
        }
        else {
            send_sync_register(c, c->q_stack[5], 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[5] + 1, 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[5] + 2, 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[5] + 3, 0xFFFFFFFF);
        }

        if(l->clients[2]) {
            send_sync_register(c, c->q_stack[6], (uint32_t)l->clients[2]->x);
            send_sync_register(c, c->q_stack[6] + 1,
                               (uint32_t)l->clients[2]->y);
            send_sync_register(c, c->q_stack[6] + 2,
                               (uint32_t)l->clients[2]->z);
            send_sync_register(c, c->q_stack[6] + 3,
                               (uint32_t)l->clients[2]->cur_area);
        }
        else {
            send_sync_register(c, c->q_stack[6], 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[6] + 1, 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[6] + 2, 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[6] + 3, 0xFFFFFFFF);
        }

        if(l->clients[3]) {
            send_sync_register(c, c->q_stack[7], (uint32_t)l->clients[3]->x);
            send_sync_register(c, c->q_stack[7] + 1,
                               (uint32_t)l->clients[3]->y);
            send_sync_register(c, c->q_stack[7] + 2,
                               (uint32_t)l->clients[3]->z);
            send_sync_register(c, c->q_stack[7] + 3,
                               (uint32_t)l->clients[3]->cur_area);
        }
        else {
            send_sync_register(c, c->q_stack[7], 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[7] + 1, 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[7] + 2, 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[7] + 3, 0xFFFFFFFF);
        }

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else if(c->q_stack[3] < 4) {
        int cl = c->q_stack[3];

        if(c->q_stack[2] != 1)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        l->qpos_regs[cl][c->client_id] = c->q_stack[4];

        if(l->clients[cl]) {
            send_sync_register(c, c->q_stack[4], (uint32_t)l->clients[cl]->x);
            send_sync_register(c, c->q_stack[4] + 1,
                               (uint32_t)l->clients[cl]->y);
            send_sync_register(c, c->q_stack[4] + 2,
                               (uint32_t)l->clients[cl]->z);
            send_sync_register(c, c->q_stack[4] + 3,
                               (uint32_t)l->clients[cl]->cur_area);
        }
        else {
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[4] + 1, 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[4] + 2, 0xFFFFFFFF);
            send_sync_register(c, c->q_stack[4] + 3, 0xFFFFFFFF);
        }

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else {
        return QUEST_FUNC_RET_INVALID_ARG;
    }
}

static uint32_t get_level(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 1)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    /* Are we requesting everyone or just one person? */
    if(c->q_stack[3] == 0xFFFFFFFF) {
        if(c->q_stack[2] != 4)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255 || c->q_stack[5] > 255 ||
           c->q_stack[6] > 255 || c->q_stack[7] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[0])
            send_sync_register(c, c->q_stack[4],
                               l->clients[0]->pl->v1.level + 1);
        else
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);

        if(l->clients[1])
            send_sync_register(c, c->q_stack[5],
                               l->clients[1]->pl->v1.level + 1);
        else
            send_sync_register(c, c->q_stack[5], 0xFFFFFFFF);

        if(l->clients[2])
            send_sync_register(c, c->q_stack[6],
                               l->clients[2]->pl->v1.level + 1);
        else
            send_sync_register(c, c->q_stack[6], 0xFFFFFFFF);

        if(l->clients[3])
            send_sync_register(c, c->q_stack[7],
                               l->clients[3]->pl->v1.level + 1);
        else
            send_sync_register(c, c->q_stack[7], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else if(c->q_stack[3] < 4) {
        if(c->q_stack[2] != 1)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(c->q_stack[4] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[c->q_stack[3]])
            send_sync_register(c, c->q_stack[4],
                               l->clients[c->q_stack[3]]->pl->v1.level + 1);
        else
            send_sync_register(c, c->q_stack[4], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else {
        return QUEST_FUNC_RET_INVALID_ARG;
    }
}

static uint32_t get_ship_name(ship_client_t *c, lobby_t *l) {
    uint32_t tmp;
    uint8_t tmpname[12] = { 0 };

    if(c->q_stack[1] != 0)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(c->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(c->q_stack[3] > 253)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    if(strlen(ship->cfg->name) < 12)
        strcpy((char *)tmpname, ship->cfg->name);
    else
        memcpy(tmpname, ship->cfg->name, 12);

    /* Send the ship's name in 3 registers... */
    tmp = tmpname[0] | ((tmpname[1]) << 8) | (tmpname[2] << 16) |
          (tmpname[3] << 24);
    send_sync_register(c, c->q_stack[3], LE32(tmp));

    tmp = tmpname[4] | (tmpname[5] << 8) | (tmpname[6] << 16) |
          (tmpname[7] << 24);
    send_sync_register(c, c->q_stack[3] + 1, LE32(tmp));

    tmp = tmpname[8] | (tmpname[9] << 8) | (tmpname[10] << 16) |
          (tmpname[11] << 24);
    send_sync_register(c, c->q_stack[3] + 2, LE32(tmp));

    return QUEST_FUNC_RET_NO_ERROR;
}

static uint32_t get_ship_name_utf16(ship_client_t *c, lobby_t *l) {
    uint32_t tmp;
    uint8_t tmpname[12] = { 0 };

    if(c->q_stack[1] != 0)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(c->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(c->q_stack[3] > 250)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    if(strlen(ship->cfg->name) < 12)
        strcpy((char *)tmpname, ship->cfg->name);
    else
        memcpy(tmpname, ship->cfg->name, 12);

    /* Send the ship's name in 6 registers... */
    tmp = tmpname[0] | (tmpname[1] << 16);
    send_sync_register(c, c->q_stack[3], LE32(tmp));

    tmp = tmpname[2] | (tmpname[3] << 16);
    send_sync_register(c, c->q_stack[3] + 1, LE32(tmp));

    tmp = tmpname[4] | (tmpname[5] << 16);
    send_sync_register(c, c->q_stack[3] + 2, LE32(tmp));

    tmp = tmpname[6] | (tmpname[7] << 16);
    send_sync_register(c, c->q_stack[3] + 3, LE32(tmp));

    tmp = tmpname[8] | (tmpname[9] << 16);
    send_sync_register(c, c->q_stack[3] + 4, LE32(tmp));

    tmp = tmpname[10] | (tmpname[11] << 16);
    send_sync_register(c, c->q_stack[3] + 5, LE32(tmp));

    return QUEST_FUNC_RET_NO_ERROR;
}

static uint32_t get_max_function(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 0)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(c->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(c->q_stack[3] > 255)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    send_sync_register(c, c->q_stack[3], QUEST_FUNC_MAX);
    return QUEST_FUNC_RET_NO_ERROR;
}

static uint32_t get_client_count_updates(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[1] != 0)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(c->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(c->q_stack[3] > 255)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    l->qcount_reg[c->client_id] = c->q_stack[3];

    /* Send the current count along now. */
    send_sync_register(c, c->q_stack[3], l->num_clients);

    /* Done. */
    return QUEST_FUNC_RET_NO_ERROR;
}

uint32_t quest_function_dispatch(ship_client_t *c, lobby_t *l) {
    if(c->q_stack[0] > QUEST_SCRIPT_START) {
        return script_execute_qfunc(c, l);
    }

    /* Call the requested function... */
    switch(c->q_stack[0]) {
        case QUEST_FUNC_GET_SECTION:
            return get_section_id(c, l);

        case QUEST_FUNC_TIME:
            return get_time(c, l);

        case QUEST_FUNC_CLIENT_COUNT:
            return get_client_count(c, l, 0);

        case QUEST_FUNC_GET_CLASS:
            return get_char_class(c, l);

        case QUEST_FUNC_GET_GENDER:
            return get_char_gender(c, l);

        case QUEST_FUNC_GET_RACE:
            return get_char_race(c, l);

        case QUEST_FUNC_GET_JOB:
            return get_char_job(c, l);

        case QUEST_FUNC_GET_FLOOR:
            return get_client_floor(c, l);

        case QUEST_FUNC_GET_POSITION:
            return get_client_position(c, l);

        case QUEST_FUNC_GET_RANDOM:
            return get_random_integer(c, l);

        case QUEST_FUNC_SHIP_CLIENTS:
            return get_client_count(c, l, 1);

        case QUEST_FUNC_BLOCK_CLIENTS:
            return get_client_count(c, l, 2);

        case QUEST_FUNC_GET_SHORTFLAG:
            return get_quest_sflag(c, l);

        case QUEST_FUNC_SET_SHORTFLAG:
            return set_quest_sflag(c, l);

        case QUEST_FUNC_GET_LONGFLAG:
            return get_quest_lflag(c, l);

        case QUEST_FUNC_SET_LONGFLAG:
            return set_quest_lflag(c, l);

        case QUEST_FUNC_DEL_SHORTFLAG:
            return del_quest_sflag(c, l);

        case QUEST_FUNC_DEL_LONGFLAG:
            return del_quest_lflag(c, l);

        case QUEST_FUNC_WORD_CENSOR_CHK:
            return word_censor_check(c, l, 0);

        case QUEST_FUNC_WORD_CENSOR_CHK2:
            return word_censor_check(c, l, 1);

        case QUEST_FUNC_GET_TEAM_SEED:
            return get_team_seed(c, l);

        case QUEST_FUNC_POS_UPDATES:
            return get_pos_updates(c, l);

        case QUEST_FUNC_GET_LEVEL:
            return get_level(c, l);

        case QUEST_FUNC_GET_SHIP_NAME:
            return get_ship_name(c, l);

        case QUEST_FUNC_GET_SHIP_NAME_UTF16:
            return get_ship_name_utf16(c, l);

        case QUEST_FUNC_GET_MAX_FUNCTION:
            return get_max_function(c, l);

        case QUEST_FUNC_CLCT_UPDATES:
            return get_client_count_updates(c, l);

        default:
            return QUEST_FUNC_RET_INVALID_FUNC;
    }
}

int quest_flag_reply(ship_client_t *c, uint32_t reason, uint32_t value) {
    lobby_t *l;
    uint8_t regnum;

    /* Sanity check... */
    if(!(c->flags & CLIENT_FLAG_QSTACK_LOCK))
        return -1;

    l = c->cur_lobby;

    if((reason & QFLAG_REPLY_SET)) {
        regnum = c->q_stack[5];

        if(!(reason & QFLAG_REPLY_ERROR))
            value = 0;
    }
    else {
        regnum = c->q_stack[4];
    }

    /* Send the response... */
    send_sync_register(c, regnum, value);

    pthread_mutex_lock(&l->mutex);

    if(!(reason & QFLAG_REPLY_ERROR))
        send_sync_register(c, l->q_data_reg, QUEST_FUNC_RET_NO_ERROR);
    else
        send_sync_register(c, l->q_data_reg, QUEST_FUNC_RET_RETVAL_ERROR);

    pthread_mutex_unlock(&l->mutex);

    /* Reset the stack and release the lock. */
    c->q_stack_top = 0;
    c->flags &= ~CLIENT_FLAG_QSTACK_LOCK;

    return 0;
}
