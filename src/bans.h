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

#ifndef BANS_H
#define BANS_H

#include <time.h>
#include <netinet/in.h>
#include <sys/queue.h>

/* Forward declaration */
struct ship;

#ifndef SHIP_DEFINED
#define SHIP_DEFINED
typedef struct ship ship_t;
#endif

typedef struct ip_ban {
    TAILQ_ENTRY(ip_ban) qentry;
    int ipv6;
    char *reason;
    time_t start_time;
    time_t end_time;
    uint32_t set_by;
    uint32_t ip_addr[4];
    uint32_t netmask[4];
} ip_ban_t;

typedef struct guildcard_ban {
    TAILQ_ENTRY(guildcard_ban) qentry;
    char *reason;
    time_t start_time;
    time_t end_time;
    uint32_t set_by;
    uint32_t banned_gc;
} guildcard_ban_t;

TAILQ_HEAD(gcban_queue, guildcard_ban);
TAILQ_HEAD(ipban_queue, ip_ban);

int ban_guildcard(ship_t *s, time_t end_time, uint32_t set_by,
                  uint32_t guildcard, const char *reason);
int ban_lift_guildcard_ban(ship_t *s, uint32_t guildcard);

int ban_ip(ship_t *s, time_t end_time, uint32_t set_by,
           const struct sockaddr_storage *ip,
           const struct sockaddr_storage *netmask, const char *reason);
int ban_lift_ip_ban(ship_t *s, const struct sockaddr_storage *ip);

int ban_sweep(ship_t *s);

int is_guildcard_banned(ship_t *s, uint32_t guildcard, char **reason,
                        time_t *until);
int is_ip_banned(ship_t *s, const struct sockaddr_storage *ip, char **reason,
                 time_t *until);

int ban_list_read(const char *fn, ship_t *s);
void ban_list_clear(ship_t *s);

#endif /* !BANS_H */
