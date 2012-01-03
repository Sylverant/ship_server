/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011 Lawrence Sebald

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

#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdint.h>
#include <iconv.h>

#include "ship_packets.h"

#define BUG_REPORT_GC   1

void print_packet(const unsigned char *pkt, int pkt_len);
void fprint_packet(FILE *fp, const unsigned char *pkt, int len, int rec);

int dc_bug_report(ship_client_t *c, dc_simple_mail_pkt *pkt);
int pc_bug_report(ship_client_t *c, pc_simple_mail_pkt *pkt);
int bb_bug_report(ship_client_t *c, bb_simple_mail_pkt *pkt);

int pkt_log_start(ship_client_t *i);
int pkt_log_stop(ship_client_t *i);

char *istrncpy(iconv_t ic, char *outs, const char *ins, int out_len);
size_t strlen16(const uint16_t *str);
char *istrncpy16(iconv_t ic, char *outs, const uint16_t *ins, int out_len);
uint16_t *strcpy16(uint16_t *d, const uint16_t *s);
uint16_t *strcat16(uint16_t *d, const uint16_t *s);

void *xmalloc(size_t size);
const void *my_ntop(struct sockaddr_storage *addr, char str[INET6_ADDRSTRLEN]);
int open_sock(int family, uint16_t port);

const char *skip_lang_code(const char *input);

void make_disp_data(ship_client_t *s, ship_client_t *d, void *buf);
void update_lobby_event(void);

/* Actually implemented in list.c, not utils.c. */
int send_player_list(ship_client_t *c, const char *params);

/* Various iconv contexts that we'll use... */
extern iconv_t ic_utf8_to_utf16;
extern iconv_t ic_utf16_to_utf8;
extern iconv_t ic_8859_to_utf8;
extern iconv_t ic_utf8_to_8859;
extern iconv_t ic_sjis_to_utf8;
extern iconv_t ic_utf8_to_sjis;
extern iconv_t ic_utf16_to_ascii;
extern iconv_t ic_8859_to_utf16;
extern iconv_t ic_sjis_to_utf16;
extern iconv_t ic_utf16_to_8859;
extern iconv_t ic_utf16_to_sjis;

int init_iconv(void);
void cleanup_iconv(void);

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
