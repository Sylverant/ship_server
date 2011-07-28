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

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <iconv.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <sylverant/debug.h>

#include "utils.h"
#include "clients.h"
#include "player.h"

#ifdef HAVE_LIBMINI18N
mini18n_t langs[CLIENT_LANG_COUNT];
#endif

/* iconv contexts that are used throughout the code */
iconv_t ic_utf8_to_utf16;
iconv_t ic_utf16_to_utf8;
iconv_t ic_8859_to_utf8;
iconv_t ic_utf8_to_8859;
iconv_t ic_sjis_to_utf8;
iconv_t ic_utf8_to_sjis;
iconv_t ic_utf16_to_ascii;
iconv_t ic_8859_to_utf16;
iconv_t ic_sjis_to_utf16;
iconv_t ic_utf16_to_8859;
iconv_t ic_utf16_to_sjis;

void print_packet(const unsigned char *pkt, int len) {
    /* With NULL, this will simply grab the current output for the debug log.
       It won't try to set the file to NULL. */
    FILE *fp = debug_set_file(NULL);

    if(!fp) {
        fp = stdout;
    }

    fprint_packet(fp, pkt, len, -1);
    fflush(fp);
}

void fprint_packet(FILE *fp, const unsigned char *pkt, int len, int rec) {
    const unsigned char *pos = pkt, *row = pkt;
    int line = 0, type = 0;
    time_t now;
    char tstr[26];

    if(rec != -1) {
        now = time(NULL);
        ctime_r(&now, tstr);
        tstr[strlen(tstr) - 1] = 0;
        fprintf(fp, "[%s] Packet %s by server\n", tstr,
                rec ? "received" : "sent");
    }

    /* Print the packet both in hex and ASCII. */
    while(pos < pkt + len) {
        if(line == 0 && type == 0) {
            fprintf(fp, "%04X ", (uint16_t)(pos - pkt));
        }

        if(type == 0) {
            fprintf(fp, "%02X ", *pos);
        }
        else {
            if(*pos >= 0x20 && *pos < 0x7F) {
                fprintf(fp, "%c", *pos);
            }
            else {
                fprintf(fp, ".");
            }
        }

        ++line;
        ++pos;

        if(line == 16) {
            if(type == 0) {
                fprintf(fp, "\t");
                pos = row;
                type = 1;
                line = 0;
            }
            else {
                fprintf(fp, "\n");
                line = 0;
                row = pos;
                type = 0;
            }
        }
    }

    /* Finish off the last row's ASCII if needed. */
    if(len & 0x1F) {
        /* Put spaces in place of the missing hex stuff. */
        while(line != 16) {
            fprintf(fp, "   ");
            ++line;
        }

        pos = row;
        fprintf(fp, "\t");

        /* Here comes the ASCII. */
        while(pos < pkt + len) {
            if(*pos >= 0x20 && *pos < 0x7F) {
                fprintf(fp, "%c", *pos);
            }
            else {
                fprintf(fp, ".");
            }

            ++pos;
        }

        fprintf(fp, "\n");
    }

    fflush(fp);
}

int dc_bug_report(ship_client_t *c, dc_simple_mail_pkt *pkt) {
    struct timeval rawtime;
    struct tm cooked;
    char filename[64];
    char text[0x91];
    FILE *fp;

    /* Get the timestamp */
    gettimeofday(&rawtime, NULL);
    
    /* Get UTC */
    gmtime_r(&rawtime.tv_sec, &cooked);

    /* Figure out the name of the file we'll be writing to. */
    sprintf(filename, "bugs/%u.%02u.%02u.%02u.%02u.%02u.%03u-%d",
            cooked.tm_year + 1900, cooked.tm_mon + 1, cooked.tm_mday,
            cooked.tm_hour, cooked.tm_min, cooked.tm_sec,
            (unsigned int)(rawtime.tv_usec / 1000), c->guildcard);

    strncpy(text, pkt->stuff, 0x90);
    text[0x90] = '\0';

    /* Attempt to open up the file. */
    fp = fopen(filename, "w");

    if(!fp) {
        return -1;
    }

    /* Write the bug report out. */
    fprintf(fp, "Bug report from %s (%d) v%d @ %u.%02u.%02u %02u:%02u:%02u\n\n",
            c->pl->v1.name, c->guildcard, c->version, cooked.tm_year + 1900,
            cooked.tm_mon + 1, cooked.tm_mday, cooked.tm_hour, cooked.tm_min,
            cooked.tm_sec);

    fprintf(fp, "%s", text);

    fclose(fp);

    return send_txt(c, "%s", __(c, "\tE\tC7Thank you for your report."));
}

int pc_bug_report(ship_client_t *c, pc_simple_mail_pkt *pkt) {
    struct timeval rawtime;
    struct tm cooked;
    char filename[64];
    char text[0x91];
    FILE *fp;
    ICONV_CONST char *inptr;
    char *outptr;
    size_t in, out;

    /* Get the timestamp */
    gettimeofday(&rawtime, NULL);

    /* Get UTC */
    gmtime_r(&rawtime.tv_sec, &cooked);

    /* Figure out the name of the file we'll be writing to. */
    sprintf(filename, "bugs/%u.%02u.%02u.%02u.%02u.%02u.%03u-%d",
            cooked.tm_year + 1900, cooked.tm_mon + 1, cooked.tm_mday,
            cooked.tm_hour, cooked.tm_min, cooked.tm_sec,
            (unsigned int)(rawtime.tv_usec / 1000), c->guildcard);

    in = 0x120;
    out = 0x90;
    inptr = pkt->stuff;
    outptr = text;
    iconv(ic_utf16_to_utf8, &inptr, &in, &outptr, &out);

    text[0x90] = '\0';

    /* Attempt to open up the file. */
    fp = fopen(filename, "w");

    if(!fp) {
        return -1;
    }

    /* Write the bug report out. */
    fprintf(fp, "Bug report from %s (%d) v%d @ %u.%02u.%02u %02u:%02u:%02u\n\n",
            c->pl->v1.name, c->guildcard, c->version, cooked.tm_year + 1900,
            cooked.tm_mon + 1, cooked.tm_mday, cooked.tm_hour, cooked.tm_min,
            cooked.tm_sec);

    fprintf(fp, "%s", text);

    fclose(fp);

    return send_txt(c, "%s", __(c, "\tE\tC7Thank you for your report."));
}

/* Begin logging the specified client's packets */
int pkt_log_start(ship_client_t *i) {
    struct timeval rawtime;
    struct tm cooked;
    char str[128];
    FILE *fp;
    time_t now;

    pthread_mutex_lock(&i->mutex);

    if(i->logfile) {
        pthread_mutex_unlock(&i->mutex);
        return -1;
    }

    /* Get the timestamp */
    gettimeofday(&rawtime, NULL);

    /* Get UTC */
    gmtime_r(&rawtime.tv_sec, &cooked);

    /* Figure out the name of the file we'll be writing to */
    if(i->guildcard) {
        sprintf(str, "logs/%u.%02u.%02u.%02u.%02u.%02u.%03u-%d",
                cooked.tm_year + 1900, cooked.tm_mon + 1, cooked.tm_mday,
                cooked.tm_hour, cooked.tm_min, cooked.tm_sec,
                (unsigned int)(rawtime.tv_usec / 1000), i->guildcard);
    }
    else {
        sprintf(str, "logs/%u.%02u.%02u.%02u.%02u.%02u.%03u",
                cooked.tm_year + 1900, cooked.tm_mon + 1, cooked.tm_mday,
                cooked.tm_hour, cooked.tm_min, cooked.tm_sec,
                (unsigned int)(rawtime.tv_usec / 1000));
    }

    fp = fopen(str, "wt");

    if(!fp) {
        pthread_mutex_unlock(&i->mutex);
        return -2;
    }

    /* Write a nice header to the log */
    now = time(NULL);
    ctime_r(&now, str);
    str[strlen(str) - 1] = 0;

    fprintf(fp, "[%s] Packet log started\n", str);
    i->logfile = fp;

    /* We're done, so clean up */
    pthread_mutex_unlock(&i->mutex);
    return 0;
}

/* Stop logging the specified client's packets */
int pkt_log_stop(ship_client_t *i) {
    time_t now;
    char str[64];

    pthread_mutex_lock(&i->mutex);

    if(!i->logfile) {
        pthread_mutex_unlock(&i->mutex);
        return -1;
    }

    /* Write a nice footer to the log */
    now = time(NULL);
    ctime_r(&now, str);
    str[strlen(str) - 1] = 0;

    fprintf(i->logfile, "[%s] Packet log ended\n", str);
    fclose(i->logfile);
    i->logfile = NULL;

    /* We're done, so clean up */
    pthread_mutex_unlock(&i->mutex);
    return 0;
}

char *istrncpy(iconv_t ic, char *outs, const char *ins, int out_len) {
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;

    /* Clear the output buffer first */
    memset(outs, 0, out_len);

    in = strlen(ins);
    out = out_len;
    inptr = (ICONV_CONST char *)ins;
    outptr = outs;
    iconv(ic, &inptr, &in, &outptr, &out);

    return outptr;
}

size_t strlen16(const uint16_t *str) {
    size_t sz = 0;

    while(*str++) ++sz;
    return sz;
}

char *istrncpy16(iconv_t ic, char *outs, const uint16_t *ins, int out_len) {
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;

    /* Clear the output buffer first */
    memset(outs, 0, out_len);

    in = strlen16(ins) * 2;
    out = out_len;
    inptr = (ICONV_CONST char *)ins;
    outptr = outs;
    iconv(ic, &inptr, &in, &outptr, &out);

    return outptr;
}

uint16_t *strcpy16(uint16_t *d, const uint16_t *s) {
    uint16_t *rv = d;
    while((*d++ = *s++)) ;
    return rv;
}

uint16_t *strcat16(uint16_t *d, const uint16_t *s) {
    uint16_t *rv = d;

    /* Move to the end of the string */
    while(*d++) ;

    /* Tack on the new part */
    --d;
    while((*d++ = *s++)) ;
    return rv;
}

void *xmalloc(size_t size) {
    void *rv = malloc(size);

    if(!rv) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    return rv;
}

const void *my_ntop(struct sockaddr_storage *addr, char str[INET6_ADDRSTRLEN]) {
    int family = addr->ss_family;

    switch(family) {
        case AF_INET:
        {
            struct sockaddr_in *a = (struct sockaddr_in *)addr;
            return inet_ntop(family, &a->sin_addr, str, INET6_ADDRSTRLEN);
        }

        case AF_INET6:
        {
            struct sockaddr_in6 *a = (struct sockaddr_in6 *)addr;
            return inet_ntop(family, &a->sin6_addr, str, INET6_ADDRSTRLEN);
        }
    }

    return NULL;
}

int open_sock(int family, uint16_t port) {
    int sock = -1, val;
    struct sockaddr_in addr;
    struct sockaddr_in6 addr6;

    /* Create the socket and listen for connections. */
    sock = socket(family, SOCK_STREAM, IPPROTO_TCP);

    if(sock < 0) {
        perror("socket");
        return -1;
    }

    /* Set SO_REUSEADDR so we don't run into issues when we kill the ship
       server and bring it back up quickly... */
    val = 1;
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int))) {
        perror("setsockopt");
        /* We can ignore this error, pretty much... its just a convenience thing
           anyway... */
    }

    if(family == AF_INET) {
        addr.sin_family = family;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        memset(addr.sin_zero, 0, 8);

        if(bind(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in))) {
            perror("bind");
            close(sock);
            return -1;
        }

        if(listen(sock, 10)) {
            perror("listen");
            close(sock);
            return -1;
        }
    }
    else if(family == AF_INET6) {
        /* Since we create separate sockets for IPv4 and IPv6, make this one
           support ONLY IPv6. */
        val = 1;
        if(setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &val, sizeof(int))) {
            perror("setsockopt IPV6_V6ONLY");
            close(sock);
            return -1;
        }

        memset(&addr6, 0, sizeof(struct sockaddr_in6));

        addr6.sin6_family = family;
        addr6.sin6_addr = in6addr_any;
        addr6.sin6_port = htons(port);

        if(bind(sock, (struct sockaddr *)&addr6, sizeof(struct sockaddr_in6))) {
            perror("bind");
            close(sock);
            return -1;
        }

        if(listen(sock, 10)) {
            perror("listen");
            close(sock);
            return -1;
        }
    }
    else {
        debug(DBG_ERROR, "Unknown socket family\n");
        close(sock);
        return -1;
    }

    return sock;
}

const char *skip_lang_code(const char *input) {
    if(!input || input[0] == '\0') {
        return NULL;
    }

    if(input[0] == '\t' && (input[1] == 'E' || input[1] == 'J')) {
        return input + 2;
    }

    return input;
}

static void convert_dcpcgc_to_bb(ship_client_t *s, uint8_t *buf) {
    sylverant_bb_char_t *c;
    v1_player_t *sp = &s->pl->v1;
    int i;

    /* Inventory doesn't change... */
    memcpy(buf, &s->pl->v1.inv, sizeof(sylverant_inventory_t));

    /* Copy the character data now... */
    c = (sylverant_bb_char_t *)(buf + sizeof(sylverant_inventory_t));
    memset(c, 0, sizeof(sylverant_bb_char_t));
    c->atp = sp->atp;
    c->mst = sp->mst;
    c->evp = sp->evp;
    c->hp = sp->hp;
    c->dfp = sp->dfp;
    c->ata = sp->ata;
    c->lck = sp->lck;
    c->unk1 = sp->unk1;
    c->unk2[0] = sp->unk2[0];
    c->unk2[1] = sp->unk2[1];
    c->level = sp->level;
    c->exp = sp->exp;
    c->meseta = sp->meseta;
    strcpy(c->guildcard_str, "         0");
    c->unk3[0] = sp->unk3[0];
    c->unk3[1] = sp->unk3[1];
    c->name_color = sp->name_color;
    c->model = sp->model;
    memcpy(c->unused, sp->unused, 15);
    c->name_color_checksum = sp->name_color_checksum;
    c->section = sp->section;
    c->ch_class = sp->ch_class;
    c->v2flags = sp->v2flags;
    c->version = sp->version;
    c->v1flags = sp->v1flags;
    c->costume = sp->costume;
    c->skin = sp->skin;
    c->face = sp->face;
    c->head = sp->head;
    c->hair = sp->hair;
    c->hair_r = sp->hair_r;
    c->hair_g = sp->hair_g;
    c->hair_b = sp->hair_b;
    c->prop_x = sp->prop_x;
    c->prop_y = sp->prop_y;
    memcpy(c->config, sp->config, 0x48);
    memcpy(c->techniques, sp->techniques, 0x14);

    /* Copy the name over */
    c->name[0] = LE16('\t');
    c->name[1] = LE16('J');

    for(i = 2; i < 16; ++i) {
        c->name[i] = LE16(sp->name[i - 2]);
    }
}

static void convert_bb_to_dcpcgc(ship_client_t *s, uint8_t *buf) {
    sylverant_bb_char_t *sp = &s->pl->bb.character;
    v1_player_t *c = (v1_player_t *)buf;

    memset(c, 0, sizeof(v1_player_t));

    /* Inventory doesn't change... */
    memcpy(buf, &s->pl->bb.inv, sizeof(sylverant_inventory_t));

    /* Copy the character data now... */
    c->atp = sp->atp;
    c->mst = sp->mst;
    c->evp = sp->evp;
    c->hp = sp->hp;
    c->dfp = sp->dfp;
    c->ata = sp->ata;
    c->lck = sp->lck;
    c->unk1 = sp->unk1;
    c->unk2[0] = sp->unk2[0];
    c->unk2[1] = sp->unk2[1];
    c->level = sp->level;
    c->exp = sp->exp;
    c->meseta = sp->meseta;
    strcpy(c->name, "---");
    c->unk3[0] = sp->unk3[0];
    c->unk3[1] = sp->unk3[1];
    c->name_color = sp->name_color;
    c->model = sp->model;
    memcpy(c->unused, sp->unused, 15);
    c->name_color_checksum = sp->name_color_checksum;
    c->section = sp->section;
    c->ch_class = sp->ch_class;
    c->v2flags = sp->v2flags;
    c->version = sp->version;
    c->v1flags = sp->v1flags;
    c->costume = sp->costume;
    c->skin = sp->skin;
    c->face = sp->face;
    c->head = sp->head;
    c->hair = sp->hair;
    c->hair_r = sp->hair_r;
    c->hair_g = sp->hair_g;
    c->hair_b = sp->hair_b;
    c->prop_x = sp->prop_x;
    c->prop_y = sp->prop_y;
    memcpy(c->config, sp->config, 0x48);
    memcpy(c->techniques, sp->techniques, 0x14);

    /* Copy the name over */
    istrncpy16(ic_utf16_to_ascii, c->name, &sp->name[2], 16);
}

void make_disp_data(ship_client_t *s, ship_client_t *d, void *buf) {
    uint8_t *bp = (uint8_t *)buf;

    if(s->version < CLIENT_VERSION_BB && d->version < CLIENT_VERSION_BB) {
        /* Neither are Blue Burst -- trivial */
        memcpy(buf, &s->pl->v1, sizeof(v1_player_t));
    }
    else if(s->version == d->version) {
        /* Both are Blue Burst -- easy */
        memcpy(bp, &s->pl->bb.inv, sizeof(sylverant_inventory_t));
        bp += sizeof(sylverant_inventory_t);
        memcpy(bp, &s->pl->bb.character, sizeof(sylverant_bb_char_t));
    }
    else if(s->version != CLIENT_VERSION_BB) {
        /* The data we're copying is from an earlier version... */
        convert_dcpcgc_to_bb(s, bp);
    }
    else if(d->version != CLIENT_VERSION_BB) {
        /* The data we're copying is from Blue Burst... */
        convert_bb_to_dcpcgc(s, bp);
    }
}

void update_lobby_event(void) {
    int i, j;
    block_t *b;
    lobby_t *l;
    ship_client_t *c2;
    uint8_t event = ship->lobby_event;

    /* Go through all the blocks... */
    for(i = 0; i < ship->cfg->blocks; ++i) {
        b = ship->blocks[i];

        if(b && b->run) {
            pthread_mutex_lock(&b->mutex);

            /* ... and set the event code on each default lobby. */
            TAILQ_FOREACH(l, &b->lobbies, qentry) {
                pthread_mutex_lock(&l->mutex);

                if(l->type == LOBBY_TYPE_DEFAULT) {
                    l->event = event;

                    for(j = 0; j < l->max_clients; ++j) {
                        if(l->clients[j] != NULL) {
                            c2 = l->clients[j];

                            pthread_mutex_lock(&c2->mutex);

                            if(c2->version > CLIENT_VERSION_PC) {
                                send_simple(c2, LOBBY_EVENT_TYPE, event);
                            }

                            pthread_mutex_unlock(&c2->mutex);
                        }
                    }
                }

                pthread_mutex_unlock(&l->mutex);
            }

            pthread_mutex_unlock(&b->mutex);
        }
    }
}

int init_iconv(void) {
    ic_utf8_to_utf16 = iconv_open("UTF-16LE", "UTF-8");

    if(ic_utf8_to_utf16 == (iconv_t)-1) {
        return -1;
    }

    ic_utf16_to_utf8 = iconv_open("UTF-8", "UTF-16LE");

    if(ic_utf16_to_utf8 == (iconv_t)-1) {
        goto out1;
    }
        
    ic_8859_to_utf8 = iconv_open("UTF-8", "ISO-8859-1");
    
    if(ic_8859_to_utf8 == (iconv_t)-1) {
        goto out2;
    }
    
    ic_utf8_to_8859 = iconv_open("ISO-8859-1", "UTF-8");

    if(ic_utf8_to_8859 == (iconv_t)-1) {
        goto out3;
    }

    ic_sjis_to_utf8 = iconv_open("UTF-8", "SHIFT_JIS");

    if(ic_sjis_to_utf8 == (iconv_t)-1) {
        goto out4;
    }

    ic_utf8_to_sjis = iconv_open("SHIFT_JIS", "UTF-8");

    if(ic_utf8_to_sjis == (iconv_t)-1) {
        goto out5;
    }

    ic_utf16_to_ascii = iconv_open("ASCII", "UTF-16LE");

    if(ic_utf16_to_ascii == (iconv_t)-1) {
        goto out6;
    }

    ic_utf16_to_sjis = iconv_open("SHIFT_JIS", "UTF-16LE");

    if(ic_utf16_to_sjis == (iconv_t)-1) {
        goto out7;
    }

    ic_utf16_to_8859 = iconv_open("ISO-8859-1", "UTF-16LE");

    if(ic_utf16_to_8859 == (iconv_t)-1) {
        goto out8;
    }

    ic_sjis_to_utf16 = iconv_open("UTF-16LE", "SHIFT_JIS");

    if(ic_sjis_to_utf16 == (iconv_t)-1) {
        goto out9;
    }

    ic_8859_to_utf16 = iconv_open("UTF-16LE", "ISO-8859-1");

    if(ic_8859_to_utf16 == (iconv_t)-1) {
        goto out10;
    }

    return 0;

out10:
    iconv_close(ic_sjis_to_utf16);
out9:
    iconv_close(ic_utf16_to_8859);
out8:
    iconv_close(ic_utf16_to_sjis);
out7:
    iconv_close(ic_utf16_to_ascii);
out6:
    iconv_close(ic_utf8_to_sjis);
out5:
    iconv_close(ic_sjis_to_utf8);
out4:
    iconv_close(ic_utf8_to_8859);
out3:
    iconv_close(ic_8859_to_utf8);
out2:
    iconv_close(ic_utf16_to_utf8);
out1:
    iconv_close(ic_utf8_to_utf16);
    return -1;
}

void cleanup_iconv(void) {
    iconv_close(ic_8859_to_utf16);
    iconv_close(ic_sjis_to_utf16);
    iconv_close(ic_utf16_to_8859);
    iconv_close(ic_utf16_to_sjis);
    iconv_close(ic_utf16_to_ascii);
    iconv_close(ic_utf8_to_sjis);
    iconv_close(ic_sjis_to_utf8);
    iconv_close(ic_utf8_to_8859);
    iconv_close(ic_8859_to_utf8);
    iconv_close(ic_utf16_to_utf8);
    iconv_close(ic_utf8_to_utf16);
}

/* Initialize mini18n support. */
void init_i18n(void) {
#ifdef HAVE_LIBMINI18N
	int i;
	char filename[256];

	for(i = 0; i < CLIENT_LANG_COUNT; ++i) {
		langs[i] = mini18n_create();

		if(langs[i]) {
			sprintf(filename, "l10n/ship_server-%s.yts", language_codes[i]);

			/* Attempt to load the l10n file. */
			if(mini18n_load(langs[i], filename)) {
				/* If we didn't get it, clean up. */
				mini18n_destroy(langs[i]);
				langs[i] = NULL;
			}
			else {
				debug(DBG_LOG, "Read l10n file for %s\n", language_codes[i]);
			}
		}
	}
#endif
}

/* Clean up when we're done with mini18n. */
void cleanup_i18n(void) {
#ifdef HAVE_LIBMINI18N
	int i;

	/* Just call the destroy function... It'll handle null values fine. */
	for(i = 0; i < CLIENT_LANG_COUNT; ++i) {
		mini18n_destroy(langs[i]);
	}
#endif
}
