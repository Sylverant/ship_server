/*
    Sylverant Ship Server
    Copyright (C) 2012, 2013 Lawrence Sebald

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

#ifndef RTDATA_H
#define RTDATA_H

#include <stdint.h>

#include "lobby.h"

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

/* Entry in one of the ItemRT files. The same format is used by all versions of
   PSO. */
typedef struct rt_entry {
    uint8_t prob;
    uint8_t item_data[3];
} PACKED rt_entry_t;

#undef PACKED

int rt_read_v2(const char *fn);
int rt_read_gc(const char *fn);
int rt_v2_enabled(void);
int rt_gc_enabled(void);

uint32_t rt_generate_v2_rare(ship_client_t *c, lobby_t *l, int rt_index,
                             int area);
uint32_t rt_generate_gc_rare(ship_client_t *c, lobby_t *l, int rt_index,
                             int area);

#endif /* !RTDATA_H */
