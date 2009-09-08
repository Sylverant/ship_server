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

#include "utils.h"

void print_packet(unsigned char *pkt, int len) {
    unsigned char *pos = pkt;
    int line = 0;

    /* Hex first. */
    while(pos < pkt + len) {
        printf("%02X ", *pos);
        ++line;
        ++pos;

        if(line == 16) {
            printf("\n");
            line = 0;
        }
    }

    printf("\n");

    pos = pkt;
    line = 0;

    /* Now ASCII */
    while(pos < pkt + len) {
        if(*pos >= 0x20) {
            printf("%c", *pos);
        }
        else {
            printf(".");
        }

        ++line;
        ++pos;

        if(line == 16) {
            printf("\n");
            line = 0;
        }
    }

    printf("\n\n");
}
