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
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <iconv.h>

#include <sylverant/debug.h>

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

/* Usage: /refresh [quests or gms] */
static int handle_refresh(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    ship_t *s = c->cur_ship;
    sylverant_quest_list_t quests;

    /* Make sure the requester is a GM. */
    if(!c->is_gm) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

    if(!strcmp(params, "quests")) {
        if(s->cfg->quests_file[0]) {
            if(sylverant_quests_read(s->cfg->quests_file, &quests)) {
                debug(DBG_ERROR, "%s: Couldn't read quests file!\n",
                      s->cfg->name);
                return send_txt(c, "\tE\tC7Couldn't read quests file!");
            }

            /* Lock the mutex to prevent anyone from trying anything funny. */
            pthread_mutex_lock(&s->qmutex);

            /* Out with the old, and in with the new. */
            sylverant_quests_destroy(&s->quests);
            s->quests = quests;

            /* Unlock the lock, we're done. */
            pthread_mutex_unlock(&s->qmutex);
            return send_txt(c, "\tE\tC7Updated quest list");
        }
        else {
            return send_txt(c, "\tE\tC7No configured quests list!");
        }
    }
    else if(!strcmp(params, "gms")) {
        if(s->cfg->gm_file[0]) {
            /* Try to read the GM file. This will clean out the old list as
               well, if needed. */
            if(gm_list_read(s->cfg->gm_file, s)) {
                return send_txt(c, "\tE\tC7Couldn't read GM list!");
            }

            return send_txt(c, "\tE\tC7Updated GMs list");
        }
        else {
            return send_txt(c, "\tE\tC7No configured GM list!");
        }
    }
    else {
        return send_txt(c, "\tE\tC7Unknown item to refresh");
    }
}

/* Usage: /save slot */
static int handle_save(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;
    uint32_t slot;

    /* Make sure that the requester is in a lobby lobby, not a game lobby */
    if(l->type & LOBBY_TYPE_GAME) {
        return send_txt(c, "\tE\tC7Only valid in a non-game lobby.");
    }

    /* Figure out the slot requested */
    errno = 0;
    slot = (uint32_t)strtoul(params, NULL, 10);

    if(errno || slot > 4 || slot < 1) {
        /* Send a message saying invalid slot */
        return send_txt(c, "\tE\tC7Invalid Slot Value");
    }

    /* Adjust so we don't go into the Blue Burst character data */
    slot += 4;

    /* Send the character data to the shipgate */
    if(shipgate_send_cdata(&c->cur_ship->sg, c->guildcard, slot, c->pl)) {
        /* Send a message saying we couldn't save */
        return send_txt(c, "\tE\tC7Couldn't save character data");
    }

    return send_txt(c, "\tE\tC7Saved character data");
}

/* Usage: /restore slot */
static int handle_restore(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;
    uint32_t slot;

    /* Make sure that the requester is in a lobby lobby, not a game lobby */
    if(l->type & LOBBY_TYPE_GAME) {
        return send_txt(c, "\tE\tC7Only valid in a non-game lobby.");
    }

    /* Figure out the slot requested */
    errno = 0;
    slot = (uint32_t)strtoul(params, NULL, 10);

    if(errno || slot > 4 || slot < 1) {
        /* Send a message saying invalid slot */
        return send_txt(c, "\tE\tC7Invalid Slot Value");
    }

    /* Adjust so we don't go into the Blue Burst character data */
    slot += 4;

    /* Send the request to the shipgate. */
    if(shipgate_send_creq(&c->cur_ship->sg, c->guildcard, slot)) {
        /* Send a message saying we couldn't request */
        return send_txt(c, "\tE\tC7Couldn't request character data");
    }

    return 0;
}

/* Usage: /bstat */
static int handle_bstat(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    block_t *b = c->cur_block;
    lobby_t *i;
    ship_client_t *i2;
    int games = 0, players = 0;
    char string[256];

    pthread_mutex_lock(&b->mutex);

    /* Determine the number of games currently active. */
    TAILQ_FOREACH(i, &b->lobbies, qentry) {
        pthread_mutex_lock(&i->mutex);

        if(i->type & LOBBY_TYPE_GAME) {
            ++games;
        }

        pthread_mutex_unlock(&i->mutex);
    }

    /* And the number of players active. */
    TAILQ_FOREACH(i2, b->clients, qentry) {
        pthread_mutex_lock(&i2->mutex);

        if(i2->pl) {
            ++players;
        }

        pthread_mutex_unlock(&i2->mutex);
    }

    pthread_mutex_unlock(&b->mutex);

    /* Fill in the string. */
    sprintf(string, "\tE\tC7BLOCK%02d:\n%d Players\n%d Games", b->b, players,
            games);
    return send_txt(c, string);
}

/* Usage /bcast message */
static int handle_bcast(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    ship_t *s = c->cur_ship;
    block_t *b;
    int i;
    ship_client_t *i2;
    char string[256];

    /* Make sure the requester is a GM. */
    if(!c->is_gm) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

    sprintf(string, "Global Message:\n%s", params);

    /* Go through each block and send the message to anyone that is alive. */
    for(i = 0; i < s->cfg->blocks; ++i) {
        b = s->blocks[i];
        pthread_mutex_lock(&b->mutex);

        /* Send the message to each player. */
        TAILQ_FOREACH(i2, b->clients, qentry) {
            pthread_mutex_lock(&i2->mutex);

            if(i2->pl) {
                send_txt(i2, string);
            }

            pthread_mutex_unlock(&i2->mutex);
        }

        pthread_mutex_unlock(&b->mutex);
    }

    return 0;
}

static command_t cmds[] = {
    { "warp"   , handle_warp      },
    { "kill"   , handle_kill      },
    { "minlvl" , handle_min_level },
    { "maxlvl" , handle_max_level },
    { "refresh", handle_refresh   },
    { "save"   , handle_save      },
    { "restore", handle_restore   },
    { "bstat"  , handle_bstat     },
    { "bcast"  , handle_bcast     },
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

int wcommand_parse(ship_client_t *c, dc_chat_pkt *pkt) {
    int len = LE16(pkt->hdr.dc.pkt_len), tlen = len - 12;
    iconv_t ic;
    size_t in, out;
    char *inptr, *outptr;
    unsigned char buf[len];
    dc_chat_pkt *p2 = (dc_chat_pkt *)buf;

    ic = iconv_open("SHIFT_JIS", "UTF-16LE");
    if(ic == (iconv_t)-1) {
        return -1;
    }

    /* Convert the text to Shift-JIS. */
    in = out = tlen;
    inptr = pkt->msg;
    outptr = p2->msg;
    iconv(ic, &inptr, &in, &outptr, &out);
    iconv_close(ic);

    /* Fill in the rest of the packet. */
    p2->hdr.dc.pkt_type = SHIP_CHAT_TYPE;
    p2->hdr.dc.flags = 0;
    p2->hdr.dc.pkt_len = 12 + (tlen - out);
    p2->padding = 0;
    p2->guildcard = pkt->guildcard;

    /* Hand off to the normal command parsing code. */
    return command_parse(c, p2);
}
