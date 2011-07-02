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

#ifdef HAVE_LIBMINI18N
mini18n_t langs[CLIENT_LANG_COUNT];
#endif

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

    return send_txt(c, "%s", __(c, "\tE\tC7Thank you for your report"));
}

int pc_bug_report(ship_client_t *c, pc_simple_mail_pkt *pkt) {
    struct timeval rawtime;
    struct tm cooked;
    char filename[64];
    char text[0x91];
    FILE *fp;
    iconv_t ic = iconv_open("SHIFT_JIS", "UTF-16LE");
    ICONV_CONST char *inptr;
    char *outptr;
    size_t in, out;

    if(ic == (iconv_t)-1) {
        return -1;
    }

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
    iconv(ic, &inptr, &in, &outptr, &out);
    iconv_close(ic);

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

    return send_txt(c, "%s", __(c, "\tE\tC7Thank you for your report"));
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
