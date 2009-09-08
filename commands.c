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

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "ship_packets.h"

typedef struct command {
    char trigger[8];
    int (*hnd)(ship_client_t *c, dc_chat_pkt *pkt, char *params);
} command_t;

/* Usage: /warp area */
static int handle_warp(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    unsigned long area;

    /* Figure out the floor requested */
    errno = 0;
    area = strtoul(params, NULL, 10);

    if(errno) {
        /* Send a message saying invalid area */
        return send_txt(c, "\tE\tC7Invalid Area!");
    }

    if(area > 17) {
        /* Area too large, give up */
        return send_txt(c, "\tE\tC7Invalid Area!");
    }

    /* Send the person to the requested place */
    return send_warp(c, (uint8_t)area);
}

/* Usage: /kill guildcard */
static int handle_kill(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t gc;
    block_t *b = c->cur_block;
    ship_client_t *i;

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, NULL, 10);

    if(errno) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "\tE\tC7Invalid Guild Card");
    }

    /* Look for the requested user (only on this block) */
    TAILQ_FOREACH(i, b->clients, qentry) {
        /* Disconnect them if we find them */
        if(i->guildcard == gc) {
            i->disconnected = 1;
            return 0;
        }
    }

    /* The person isn't here... There's nothing to do. */
    return 0;
}

static command_t cmds[] = {
    { "warp"   , handle_warp },
    { "kill"   , handle_kill },
    { ""       , NULL        }          /* End marker -- DO NOT DELETE */
};

int command_parse(ship_client_t *c, dc_chat_pkt *pkt) {
    command_t *i = &cmds[0];
    char cmd[8];
    char *ch;
    int len = 0;

    /* Figure out what the command the user has requested is */
    ch = pkt->msg + 3;

    while(*ch != ' ' && len < 7) { 
        cmd[len++] = *ch++;
    }

    cmd[len] = '\0';

    /* Look through the list for the one we want */
    while(i->hnd) {
        /* If this is it, go ahead and handle it */
        if(!strcmp(cmd, i->trigger)) {
            return i->hnd(c, pkt, pkt->msg + 4 + len);
        }

        i++;
    }

    /* XXXX: Send the user a message saying invalid command. */
    return send_txt(c, "\tE\tC7Invalid Command!");
}
