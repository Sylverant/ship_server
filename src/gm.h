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

#ifndef GM_H
#define GM_H

#include <inttypes.h>

/* Forward declaration */
#ifndef SHIP_DEFINED
#define SHIP_DEFINED
typedef struct ship ship_t;
#endif

typedef struct local_gm {
    uint32_t guildcard;
    char serial_num[9];
    char access_key[9];
} local_gm_t;

int gm_list_read(const char *fn, ship_t *s);
int is_gm(uint32_t guildcard, char serial[9], char access[9], ship_t *s);

#endif /* !GM_H */
