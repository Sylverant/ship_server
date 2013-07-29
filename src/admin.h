/*
    Sylverant Ship Server
    Copyright (C) 2011, 2013 Lawrence Sebald

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

#ifndef ADMIN_H
#define ADMIN_H

#include <sylverant/config.h>
#include "clients.h"

/* Some macros for commonly used privilege checks. */
#define LOCAL_GM(c) \
    ((c->privilege & CLIENT_PRIV_LOCAL_GM) && \
     (c->flags & CLIENT_FLAG_LOGGED_IN))

#define GLOBAL_GM(c) \
    ((c->privilege & CLIENT_PRIV_GLOBAL_GM) && \
     (c->flags & CLIENT_FLAG_LOGGED_IN))

#define LOCAL_ROOT(c) \
    ((c->privilege & CLIENT_PRIV_LOCAL_ROOT) && \
     (c->flags & CLIENT_FLAG_LOGGED_IN))

#define GLOBAL_ROOT(c) \
    ((c->privilege & CLIENT_PRIV_GLOBAL_ROOT) && \
     (c->flags & CLIENT_FLAG_LOGGED_IN))

typedef int (*msgfunc)(ship_client_t *, const char *, ...);

int kill_guildcard(ship_client_t *c, uint32_t gc, const char *reason);

int load_quests(ship_t *s, sylverant_ship_t *cfg, int initial);
void clean_quests(ship_t *s);

int refresh_quests(ship_client_t *c, msgfunc f);
int refresh_gms(ship_client_t *c, msgfunc f);
int refresh_limits(ship_client_t *c, msgfunc f);

int broadcast_message(ship_client_t *c, const char *message, int prefix);

int schedule_shutdown(ship_client_t *c, uint32_t when, int restart, msgfunc f);

int global_ban(ship_client_t *c, uint32_t gc, uint32_t l, const char *reason);

#endif /* !ADMIN_H */
