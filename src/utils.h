/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010 Lawrence Sebald

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

#ifndef UTILS_H
#define UTILS_H

#include "ship_packets.h"

#define BUG_REPORT_GC   1

void print_packet(unsigned char *pkt, int pkt_len);

int dc_bug_report(ship_client_t *c, dc_simple_mail_pkt *pkt);
int pc_bug_report(ship_client_t *c, pc_simple_mail_pkt *pkt);

/* Actually implemented in list.c, not utils.c. */
int send_player_list(ship_client_t *c, char *params);

/* Internationalization support */
#ifdef HAVE_LIBMINI18N
#include <mini18n-multi.h>
#include "clients.h"

extern mini18n_t langs[CLIENT_LANG_COUNT];
#define __(c, s) mini18n_get(langs[c->language_code], s)
#else
#define __(c, s) s
#endif

void init_i18n(void);
void cleanup_i18n(void);

#endif /* !UTILS_H */
