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

#ifndef QUEST_FUNCTIONS_H
#define QUEST_FUNCTIONS_H

#include <stdint.h>

#include "clients.h"
#include "lobby.h"

/* Return codes to the quest. */
#define QUEST_FUNC_RET_NO_ERROR         0
#define QUEST_FUNC_RET_STACK_OVERFLOW   0x8000FFFF
#define QUEST_FUNC_RET_INVALID_FUNC     0x8000FFFE
#define QUEST_FUNC_RET_BAD_ARG_COUNT    0x8000FFFD
#define QUEST_FUNC_RET_BAD_RET_COUNT    0x8000FFFC
#define QUEST_FUNC_RET_INVALID_ARG      0x8000FFFB
#define QUEST_FUNC_RET_INVALID_REGISTER 0x8000FFFA

/*  Function 0:   get_section_id
    Arguments:    1: int id -- Set to a client id from 0-3 for one player.
                               Set to 0xFFFFFFFF for all players in the team.
    Returns:      1 or 4 values of the requested section IDs. */
#define QUEST_FUNC_GET_SECTION          0

/* Function 1:   get_server_time
   Arguments:    None
   Returns:      1 value: The server's current time as an unsigned number of
                          seconds since 1/1/1970 @ 0:00:00 UTC.
   Note: This may be a signed number if the underlying OS of the system uses
         32-bit signed values for the time() function. */
#define QUEST_FUNC_TIME                 1

extern uint32_t quest_function_dispatch(ship_client_t *c, lobby_t *l);

#endif /* !QUEST_FUNCTIONS_H */
