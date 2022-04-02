/*
    Sylverant Ship Server
    Copyright (C) 2011, 2016, 2018, 2019, 2020, 2021, 2022 Lawrence Sebald

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

#ifndef SCRIPTS_H
#define SCRIPTS_H

#include <stdint.h>
#include <sys/queue.h>

#include "clients.h"
#include "ship.h"

/* Scriptable actions */
typedef enum script_action {
    ScriptActionInvalid = -1,
    ScriptActionFirst = 0,
    ScriptActionStartup = 0,
    ScriptActionShutdown,
    ScriptActionClientShipLogin,
    ScriptActionClientShipLogout,
    ScriptActionClientBlockLogin,
    ScriptActionClientBlockLogout,
    ScriptActionUnknownShipPacket,
    ScriptActionUnknownBlockPacket,
    ScriptActionUnknownEp3Packet,
    ScriptActionTeamCreate,
    ScriptActionTeamDestroy,
    ScriptActionTeamJoin,
    ScriptActionTeamLeave,
    ScriptActionEnemyKill,
    ScriptActionEnemyHit,
    ScriptActionBoxBreak,
    ScriptActionUnknownCommand,
    ScriptActionSData,
    ScriptActionUnknownMenu,
    ScriptActionBankAction,
    ScriptActionChangeArea,
    ScriptActionQuestSyncRegister,
    ScriptActionQuestLoad,
    ScriptActionBeforeQuestLoad,
    ScriptActionCount
} script_action_t;

/* Argument types. */
#define SCRIPT_ARG_NONE     0
#define SCRIPT_ARG_END      SCRIPT_ARG_NONE
#define SCRIPT_ARG_INT      1
#define SCRIPT_ARG_PTR      2
#define SCRIPT_ARG_FLOAT    3
#define SCRIPT_ARG_UINT8    4
#define SCRIPT_ARG_UINT16   5
#define SCRIPT_ARG_UINT32   6
#define SCRIPT_ARG_STRING   7               /* Length-prepended string */
#define SCRIPT_ARG_CSTRING  8               /* NUL-terminated string */

/* Call the script function for the given event with the args listed */
int script_execute(script_action_t event, ship_client_t *c, ...);

/* Call the script function for the given event that involves an unknown pkt */
int script_execute_pkt(script_action_t event, ship_client_t *c, const void *pkt,
                       uint16_t len);

/* Call the script function for the called quest function, if it exists */
uint32_t script_execute_qfunc(ship_client_t *c, lobby_t *l);

void init_scripts(ship_t *s);
void cleanup_scripts(ship_t *s);

int script_add(script_action_t action, const char *filename);
int script_add_lobby_locked(lobby_t *l, script_action_t action);
int script_add_lobby_qfunc_locked(lobby_t *l, uint32_t id, int args, int rvs);
int script_remove(script_action_t action);
int script_remove_lobby_locked(lobby_t *l, script_action_t action);
int script_remove_lobby_qfunc_locked(lobby_t *l, uint32_t id);
int script_cleanup_lobby_locked(lobby_t *l);
int script_update_module(const char *modname);

int script_execute_file(const char *fn, lobby_t *l);

#endif /* !SCRIPTS_H */
