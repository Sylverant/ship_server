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
#include "lobby.h"

typedef struct command {
    char trigger[8];
    int (*hnd)(ship_client_t *c, dc_chat_pkt *pkt, char *params);
} command_t;

/* Usage: /warp area */
static int handle_warp(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    unsigned long area;

    /* Make sure the requester is a GM. */
    if(!c->is_gm) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

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

    /* Make sure the requester is a GM. */
    if(!c->is_gm) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

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

/* Usage: /minlvl level */
static int handle_min_level(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    int lvl;
    lobby_t *l = c->cur_lobby;

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(!(l->type & LOBBY_TYPE_GAME)) {
        return send_txt(c, "\tE\tC7Only valid in a game lobby.");
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        return send_txt(c, "\tE\tC7Only the leader may use this command.");
    }

    /* Figure out the level requested */
    errno = 0;
    lvl = (int)strtoul(params, NULL, 10);

    if(errno || lvl > 200 || lvl < 1) {
        /* Send a message saying invalid level */
        return send_txt(c, "\tE\tC7Invalid Level Value");
    }

    /* Make sure the requested level is greater than or equal to the value for
       the game's difficulty. */
    if(lvl < game_required_level[l->difficulty]) {
        return send_txt(c, "\tE\tC7Invalid level for this difficulty.");
    }

    /* Make sure the requested level is less than or equal to the game's maximum
       level. */
    if(lvl > l->max_level + 1) {
        return send_txt(c, "\tE\tC7Minimum level must be <= maximum.");
    }

    /* Set the value in the structure, and be on our way. */
    l->min_level = lvl - 1;

    return send_txt(c, "\tE\tC7Minimum level set.");
}

/* Usage: /maxlvl level */
static int handle_max_level(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    int lvl;
    lobby_t *l = c->cur_lobby;

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(!(l->type & LOBBY_TYPE_GAME)) {
        return send_txt(c, "\tE\tC7Only valid in a game lobby.");
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        return send_txt(c, "\tE\tC7Only the leader may use this command.");
    }

    /* Figure out the level requested */
    errno = 0;
    lvl = (int)strtoul(params, NULL, 10);

    if(errno || lvl > 200 || lvl < 1) {
        /* Send a message saying invalid level */
        return send_txt(c, "\tE\tC7Invalid Level Value");
    }

    /* Make sure the requested level is greater than or equal to the value for
       the game's minimum level. */
    if(lvl < l->min_level + 1) {
        return send_txt(c, "\tE\tC7Maximum level must be >= minimum.");
    }
    
    /* Set the value in the structure, and be on our way. */
    l->max_level = lvl - 1;

    return send_txt(c, "\tE\tC7Maximum level set.");
}

static command_t cmds[] = {
    { "warp"   , handle_warp      },
    { "kill"   , handle_kill      },
    { "minlvl" , handle_min_level },
    { "maxlvl" , handle_max_level },
    { ""       , NULL             }     /* End marker -- DO NOT DELETE */
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

    /* Send the user a message saying invalid command. */
    return send_txt(c, "\tE\tC7Invalid Command!");
}
