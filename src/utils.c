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
    unsigned char *pos = pkt, *row = pkt;
    int line = 0, type = 0;

    /* Print the packet both in hex and ASCII. */
    while(pos < pkt + len) {
        if(type == 0) {
            printf("%02X ", *pos);
        }
        else {
            if(*pos >= 0x20) {
                printf("%c", *pos);
            }
            else {
                printf(".");
            }
        }

        ++line;
        ++pos;

        if(line == 16) {
            if(type == 0) {
                printf("\t");
                pos -= 16;
                pos = row;
                type = 1;
            }
            else {
                printf("\n");
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
            printf("   ");
            ++line;
        }

        pos = row;
        printf("\t");

        /* Here comes the ASCII. */
        while(pos < pkt + len) {
            if(*pos >= 0x20) {
                printf("%c", *pos);
            }
            else {
               printf(".");
            }

            ++pos;
        }

        printf("\n");
    }

    printf("\n\n");
}
