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
#define QUEST_FUNC_RET_STACK_LOCKED     0x8000FFF9
#define QUEST_FUNC_RET_SHIPGATE_ERR     0x8000FFF8

#define QUEST_FUNC_RET_NOT_YET          0xDEADBEEF

/*  Function 0:   get_section_id
    Arguments:    1: int id -- Set to a client id from 0-3 for one player.
                               Set to -1 for all players in the team.
    Returns:      1 or 4 values of the requested section IDs. */
#define QUEST_FUNC_GET_SECTION          0

/* Function 1:   get_server_time
   Arguments:    None
   Returns:      1 value: The server's current time as an unsigned number of
                          seconds since 1/1/1970 @ 0:00:00 UTC.
   Note: This may be a signed number if the underlying OS of the system uses
         32-bit signed values for the time() function. */
#define QUEST_FUNC_TIME                 1

/* Function 2:   get_client_count
   Arguments:    None
   Returns:      1 value: The number of clients currently in the team. */
#define QUEST_FUNC_CLIENT_COUNT         2

/* Function 3:   get_character_class
   Arguments:    1: int id -- Set to a client id from 0-3 for one player.
                              Set to -1 for all players in the team.
   Returns:      1 or 4 values of the requested character classes. */
#define QUEST_FUNC_GET_CLASS            3

/* Function 4:   get_character_gender
   Arguments:    1: int id -- Set to a client id from 0-3 for one player.
                             Set to -1 for all players in the team.
   Returns:      1 or 4 values of the requested genders. */
#define QUEST_FUNC_GET_GENDER           4

/* Function 5:   get_character_race
   Arguments:    1: int id -- Set to a client id from 0-3 for one player.
                              Set to -1 for all players in the team.
   Returns:      1 or 4 values of the requested character races. */
#define QUEST_FUNC_GET_RACE             5

/* Function 6:   get_character_job
   Arguments:    1: int id -- Set to a client id from 0-3 for one player.
                              Set to -1 for all players in the team.
   Returns:      1 or 4 values of the requested character jobs. */
#define QUEST_FUNC_GET_JOB              6

/* Function 7:   get_client_floor
   Arguments:    1: int id -- Set to a client id from 0-3 for one player.
                              Set to -1 for all players in the team.
   Returns:      1 or 4 values of the requested client(s)'s floor. */
#define QUEST_FUNC_GET_FLOOR            7

/* Function 8:   get_position
   Arguments:    1: int id -- Set to a client id from 0-3 for one player.
                              Set to -1 for all playes in the team.
   Returns:      1 or 4 values of the requested client(s)'s positions.
   Note: Each return value takes up three registers. Only the first of the
         three are specified. */
#define QUEST_FUNC_GET_POSITION         8

/* Function 9:   get_random_integer
   Arguments:    1: int min -- The minimum value for the random number.
                 2: int max -- The maximum value for the random number.
   Returns:      1 value: The randomly generated 32-bit integer. */
#define QUEST_FUNC_GET_RANDOM           9

/* Function 10:  get_ship_client_count
   Arguments:    None
   Returns:      1 value: The number of clients currently on the ship. */
#define QUEST_FUNC_SHIP_CLIENTS         10

/* Function 11:  get_block_client_count
   Arguments:    None
   Returns:      1 value: The number of clients currently on the block. */
#define QUEST_FUNC_BLOCK_CLIENTS        11

/* Function 12:  get_short_qflag
   Arguments:    1: int flag -- The flag number to request from the server.
   Returns:      1 value: The value of the specified flag on the shipgate.
                 On error, this will be negative.
   Error Values: -1: Invalid flag number.
                 -2: Shipgate has disappeared.
                 -3: Flag is currently unset. */
#define QUEST_FUNC_GET_SHORTFLAG        12

/* Function 13:  set_short_qflag
   Arguments:    1: int flag -- The flag number to request from the server.
                 2: uint16_t val -- The value to set in the flag.
   Returns:      1 value: 0 on success.
                 On error, this will be negative.
   Error Values: -1: Invalid flag number.
                 -2: Shipgate has disappeared. */
#define QUEST_FUNC_SET_SHORTFLAG        13

/* Function 14:  get_long_qflag
   Arguments:    1: int flag -- The flag number to request from the server.
   Returns:      1 value: The value of the specified flag on the shipgate.
                 On error, this will be negative.
   Error Values: -1: Invalid flag number.
                 -2: Shipgate has disappeared.
                 -3: Flag is currently unset. */
#define QUEST_FUNC_GET_LONGFLAG        14

/* Function 15:  set_long_qflag
   Arguments:    1: int flag -- The flag number to request from the server.
                 2: uint32_t val -- The value to set in the flag.
   Returns:      1 value: 0 on success.
                 On error, this will be negative.
   Error Values: -1: Invalid flag number.
                 -2: Shipgate has disappeared. */
#define QUEST_FUNC_SET_LONGFLAG        15

/* Function 16:  del_short_qflag
   Arguments:    1: int flag -- The flag number to delete
   Returns:      1 value: 0 on success.
                 On error, this will be negative.
   Error Values: -1: Invalid flag number.
                 -2: Shipgate has disappeared. */
#define QUEST_FUNC_DEL_SHORTFLAG       16

/* Function 17:  del_long_qflag
   Arguments:    1: int flag -- The flag number to delete
   Returns:      1 value: 0 on success.
                 On error, this will be negative.
   Error Values: -1: Invalid flag number.
                 -2: Shipgate has disappeared. */
#define QUEST_FUNC_DEL_LONGFLAG        17

/* Function 18:  word_censor_check
   Arguments:    1...n: char str[] -- The string to check against the censor.
                        This string may be NUL terminated, but is not required
                        to be. Only ASCII values are accepted. The maximum
                        length accepted is 24 characters.
   Returns:      1 value: 0 on nothing matched by the censor, 1 if matched. */
#define QUEST_FUNC_WORD_CENSOR_CHK      18

/* Function 18:  word_censor_check2
   Arguments:    1...n: char str[] -- The string to check against the censor.
                        This string may be NUL terminated, but is not required
                        to be. Only values 0-26 are accepted (mapping to NUL,
                        then A-Z). The maximum length accepted is 24 characters.
   Returns:      1 value: 0 on nothing matched by the censor, 1 if matched. */
#define QUEST_FUNC_WORD_CENSOR_CHK2     19

extern uint32_t quest_function_dispatch(ship_client_t *c, lobby_t *l);

#define QFLAG_REPLY_GET     0x00000001
#define QFLAG_REPLY_SET     0x00000002
#define QFLAG_REPLY_ERROR   0x80000000

extern int quest_flag_reply(ship_client_t *c, uint32_t reason, uint32_t value);

#endif /* !QUEST_FUNCTIONS_H */
