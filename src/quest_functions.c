/*
    Sylverant Ship Server
    Copyright (C) 2019 Lawrence Sebald

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

#include "clients.h"
#include "lobby.h"
#include "quest_functions.h"
#include "ship_packets.h"

static uint32_t get_section_id(ship_client_t *c, lobby_t *l) {
    if(l->q_stack[1] != 1)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    /* Are we requesting everyone or just one person? */
    if(l->q_stack[3] == 0xFFFFFFFF) {
        if(l->q_stack[2] != 4)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(l->q_stack[4] > 255 || l->q_stack[5] > 255 ||
           l->q_stack[6] > 255 || l->q_stack[7] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[0])
            send_sync_register(c, l->q_stack[4],
                               l->clients[0]->pl->v1.section);
        else
            send_sync_register(c, l->q_stack[4], 0xFFFFFFFF);

        if(l->clients[1])
            send_sync_register(c, l->q_stack[5],
                               l->clients[1]->pl->v1.section);
        else
            send_sync_register(c, l->q_stack[5], 0xFFFFFFFF);

        if(l->clients[2])
            send_sync_register(c, l->q_stack[6],
                               l->clients[2]->pl->v1.section);
        else
            send_sync_register(c, l->q_stack[6], 0xFFFFFFFF);

        if(l->clients[3])
            send_sync_register(c, l->q_stack[7],
                               l->clients[3]->pl->v1.section);
        else
            send_sync_register(c, l->q_stack[7], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else if(l->q_stack[3] < 4) {
        if(l->q_stack[2] != 1)
            return QUEST_FUNC_RET_BAD_RET_COUNT;

        if(l->q_stack[4] > 255)
            return QUEST_FUNC_RET_INVALID_REGISTER;

        if(l->clients[l->q_stack[3]])
            send_sync_register(c, l->q_stack[4],
                               l->clients[l->q_stack[3]]->pl->v1.section);
        else
            send_sync_register(c, l->q_stack[l->q_stack[3]], 0xFFFFFFFF);

        /* Done. */
        return QUEST_FUNC_RET_NO_ERROR;
    }
    else {
        return QUEST_FUNC_RET_INVALID_ARG;
    }
}

uint32_t get_time(ship_client_t *c, lobby_t *l) {
    if(l->q_stack[1] != 0)
        return QUEST_FUNC_RET_BAD_ARG_COUNT;

    if(l->q_stack[2] != 1)
        return QUEST_FUNC_RET_BAD_RET_COUNT;

    if(l->q_stack[3] > 255)
        return QUEST_FUNC_RET_INVALID_REGISTER;

    send_sync_register(c, l->q_stack[3], (uint32_t)time(NULL));
    return QUEST_FUNC_RET_NO_ERROR;
}

uint32_t quest_function_dispatch(ship_client_t *c, lobby_t *l) {
    /* Call the requested function... */
    switch(l->q_stack[0]) {
        case QUEST_FUNC_GET_SECTION:
            return get_section_id(c, l);

        case QUEST_FUNC_TIME:
            return get_time(c, l);

        default:
            return QUEST_FUNC_RET_INVALID_FUNC;
    }
}
