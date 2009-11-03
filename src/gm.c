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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <expat.h>
#include <inttypes.h>

#include "gm.h"
#include "ship.h"

#define BUF_SIZE 8192

static void gm_start_hnd(void *d, const XML_Char *name,
                         const XML_Char **attrs) {
    int i, done = 0;
    void *tmp;
    ship_t *s = (ship_t *)d;

    if(!strcmp(name, "gm")) {
        /* Attempt to make space for the new GM first. */
        tmp = realloc(s->gm_list, (s->gm_count + 1) * sizeof(local_gm_t));

        if(!tmp) {
            return;
        }

        s->gm_list = (local_gm_t *)tmp;

        /* Parse out what we care about. */
        for(i = 0; attrs[i]; i += 2) {
            if(!strcmp(attrs[i], "serial")) {
                strncpy(s->gm_list[s->gm_count].serial_num, attrs[i + 1], 8);
                s->gm_list[s->gm_count].serial_num[8] = '\0';
                done |= 1;
            }
            else if(!strcmp(attrs[i], "accesskey")) {
                strncpy(s->gm_list[s->gm_count].access_key, attrs[i + 1], 8);
                s->gm_list[s->gm_count].access_key[8] = '\0';
                done |= 2;
            }
            else if(!strcmp(attrs[i], "guildcard")) {
                s->gm_list[s->gm_count].guildcard =
                    (uint32_t)strtoul(attrs[i + 1], NULL, 0);
                done |= 4;
            }
        }

        /* Make sure we got everything, then increment the count. */
        if(done == 7) {
            ++s->gm_count;
        }
    }
}

static void gm_end_hnd(void *d, const XML_Char *name) {
}

int gm_list_read(const char *fn, ship_t *s) {
    FILE *fp;
    XML_Parser p;
    int bytes;
    void *buf;
    local_gm_t *oldlist = NULL;
    int oldcount = 0;

    /* If we're reloading, save the old list. */
    if(s->gm_list) {
        oldlist = s->gm_list;
        oldcount = s->gm_count;
        s->gm_list = NULL;
        s->gm_count = 0;
    }

    /* Open the GM list file for reading. */
    fp = fopen(fn, "r");

    if(!fp) {
        s->gm_list = oldlist;
        s->gm_count = oldcount;
        return -1;
    }

    /* Create the XML parser object. */
    p = XML_ParserCreate(NULL);

    if(!p)  {
        fclose(fp);
        s->gm_list = oldlist;
        s->gm_count = oldcount;
        return -2;
    }

    XML_SetElementHandler(p, &gm_start_hnd, &gm_end_hnd);
    XML_SetUserData(p, s);

    for(;;) {
        /* Grab the buffer to read into. */
        buf = XML_GetBuffer(p, BUF_SIZE);

        if(!buf)    {
            XML_ParserFree(p);
            free(s->gm_list);
            s->gm_list = oldlist;
            s->gm_count = oldcount;
            return -2;
        }

        /* Read in from the file. */
        bytes = fread(buf, 1, BUF_SIZE, fp);

        if(bytes < 0)   {
            XML_ParserFree(p);
            free(s->gm_list);
            s->gm_list = oldlist;
            s->gm_count = oldcount;
            return -2;
        }

        /* Parse the bit we read in. */
        if(!XML_ParseBuffer(p, bytes, !bytes))  {
            XML_ParserFree(p);
            free(s->gm_list);
            s->gm_list = oldlist;
            s->gm_count = oldcount;
            return -3;
        }

        if(!bytes)  {
            break;
        }
    }

    XML_ParserFree(p);

    /* If we had an old list, clear it. */
    if(oldlist) {
        free(oldlist);
    }

    return 0;
}

int is_gm(uint32_t guildcard, char serial[9], char access[9], ship_t *s) {
    int i;

    /* Look through the list for this person. */
    for(i = 0; i < s->gm_count; ++i) {
        if(guildcard == s->gm_list[i].guildcard &&
           !strcmp(serial, s->gm_list[i].serial_num) &&
           !strcmp(access, s->gm_list[i].access_key)) {
            return 1;
        }
    }

    /* Didn't find them, they're not a GM. */
    return 0;
}
