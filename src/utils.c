/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010 Lawrence Sebald

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

#include <sylverant/debug.h>

#include "utils.h"

#ifdef HAVE_LIBMINI18N
mini18n_t langs[CLIENT_LANG_COUNT];
#endif

void print_packet(unsigned char *pkt, int len) {
    fprint_packet(stdout, pkt, len, -1);
}

void fprint_packet(FILE *fp, unsigned char *pkt, int len, int dir) {
    unsigned char *pos = pkt, *row = pkt;
    int line = 0, type = 0;
    time_t now = time(NULL);

    if(dir != -1) {
        fprintf(fp, "Packet %s at %lu:\n", dir ? "received" : "sent",
                (unsigned long)now);
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

    return send_txt(c, "\tE\tC7Thank you for your report");
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

    return send_txt(c, "\tE\tC7Thank you for your report");
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
