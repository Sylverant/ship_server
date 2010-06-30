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
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <iconv.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <sylverant/debug.h>

#include "ship_packets.h"
#include "lobby.h"
#include "subcmd.h"
#include "utils.h"

typedef struct command {
    char trigger[8];
    int (*hnd)(ship_client_t *c, dc_chat_pkt *pkt, char *params);
} command_t;

/* Usage: /warp area */
static int handle_warp(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    unsigned long area;
    lobby_t *l = c->cur_lobby;

    /* Make sure the requester is a GM. */
    if(!c->is_gm) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(!(l->type & LOBBY_TYPE_GAME)) {
        return send_txt(c, "\tE\tC7Only valid in a game lobby.");
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

/* Usage: /warpall area */
static int handle_warpall(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    unsigned long area;
    lobby_t *l = c->cur_lobby;

    /* Make sure the requester is a GM. */
    if(!c->is_gm) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(!(l->type & LOBBY_TYPE_GAME)) {
        return send_txt(c, "\tE\tC7Only valid in a game lobby.");
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
    return send_lobby_warp(l, (uint8_t)area);
}

/* Usage: /kill guildcard reason */
static int handle_kill(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t gc;
    block_t *b = c->cur_block;
    ship_client_t *i;
    char *reason;
    char msg[256];

    /* Make sure the requester is a GM. */
    if(!c->is_gm) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "\tE\tC7Invalid Guild Card");
    }

    /* Look for the requested user (only on this block) */
    TAILQ_FOREACH(i, b->clients, qentry) {
        /* Disconnect them if we find them */
        if(i->guildcard == gc) {
            if(strlen(reason) > 1) {
                sprintf(msg, "\tEYou have been kicked by a GM.\n\n"
                             "Reason:\n%s", reason + 1);
            }
            else {
                strcpy(msg, "\tEYou have been kicked by a GM.");
            }

            send_message_box(i, msg);                                
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
    return send_txt(c, "\tE\tC7BLOCK%02d:\n%d Players\n%d Games", b->b,
                    players, games);
}

/* Usage /bcast message */
static int handle_bcast(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    ship_t *s = c->cur_ship;
    block_t *b;
    int i;
    ship_client_t *i2;

    /* Make sure the requester is a GM. */
    if(!c->is_gm) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

    /* Go through each block and send the message to anyone that is alive. */
    for(i = 0; i < s->cfg->blocks; ++i) {
        b = s->blocks[i];
        pthread_mutex_lock(&b->mutex);

        /* Send the message to each player. */
        TAILQ_FOREACH(i2, b->clients, qentry) {
            pthread_mutex_lock(&i2->mutex);

            if(i2->pl) {
                send_txt(i2, "\tE\tC7Global Message:\n%s", params);
            }

            pthread_mutex_unlock(&i2->mutex);
        }

        pthread_mutex_unlock(&b->mutex);
    }

    return 0;
}

/* Usage /arrow color_number */
static int handle_arrow(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    int i;

    /* Set the arrow color and send the packet to the lobby. */
    i = atoi(params);
    c->arrow = i;

    send_txt(c, "\tE\tC7Arrow set");

    return send_lobby_arrows(c->cur_lobby);
}

/* Usage /login username password */
static int handle_login(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    char username[32], password[32];
    int len = 0;
    char *ch = params;

    /* Copy over the username/password. */
    while(*ch != ' ' && len < 32) {
        username[len++] = *ch++;
    }

    if(len == 32) {
        return send_txt(c, "\tE\tC7Invalid request");
    }

    username[len] = '\0';

    len = 0;
    ++ch;

    while(*ch != ' ' && *ch != '\0' && len < 32) {
        password[len++] = *ch++;
    }

    if(len == 32) {
        return send_txt(c, "\tE\tC7Invalid request");
    }

    password[len] = '\0';

    /* We'll get success/failure later from the shipgate. */
    return shipgate_send_gmlogin(&c->cur_ship->sg, c->guildcard,
                                 c->cur_block->b, username, password);
}

/* Usage /item item1,item2,item3,item4 */
static int handle_item(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t item[4] = { 0, 0, 0, 0 };
    int count;

    /* Make sure the requester is a GM. */
    if(!c->is_gm) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

    /* Copy over the item data. */
    count = sscanf(params, "%x,%x,%x,%x", item + 0, item + 1, item + 2,
                   item + 3);

    if(count == EOF || count == 0) {
        return send_txt(c, "\tE\tC7Invalid Item code");
    }

    c->next_item[0] = item[0];
    c->next_item[1] = item[1];
    c->next_item[2] = item[2];
    c->next_item[3] = item[3];

    return send_txt(c, "\tE\tC7Next item set successfully");
}

/* Usage /item4 item4 */
static int handle_item4(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t item;
    int count;

    /* Make sure the requester is a GM. */
    if(!c->is_gm) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

    /* Copy over the item data. */
    count = sscanf(params, "%x", &item);

    if(count == EOF || count == 0) {
        return send_txt(c, "\tE\tC7Invalid Item code");
    }

    c->next_item[3] = item;

    return send_txt(c, "\tE\tC7Item4 set succesfully");
}

/* Usage: /event number */
static int handle_event(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;
    ship_t *s = c->cur_ship;
    ship_client_t *c2;
    block_t *b;
    int event, gevent, i, j;

    /* Make sure the requester is a GM. */
    if(!c->is_gm) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

    /* Make sure that the requester is in a lobby lobby, not a game lobby */
    if(l->type & LOBBY_TYPE_GAME) {
        return send_txt(c, "\tE\tC7Only valid in a non-game lobby.");
    }

    /* Grab the event number */
    event = atoi(params);

    if(event > 7) {
        gevent = 0;
    }
    else if(event == 7) {
        gevent = 2;
    }
    else {
        gevent = event;
    }

    if(event < 0 || event > 14) {
        return send_txt(c, "\tE\tC7Invalid event code.");
    }

    /* Go through all the blocks... */
    for(i = 0; i < s->cfg->blocks; ++i) {
        if(s->blocks[i]) {
            b = s->blocks[i];
            pthread_mutex_lock(&b->mutex);

            /* ... and set the event code on each default lobby. */
            TAILQ_FOREACH(l, &b->lobbies, qentry) {
                pthread_mutex_lock(&l->mutex);

                if(l->type & LOBBY_TYPE_DEFAULT) {
                    l->event = event;
                    l->gevent = gevent;

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

    return send_txt(c, "\tE\tC7Event set.");
}

/* Usage: /passwd newpass */
static int handle_passwd(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(!(l->type & LOBBY_TYPE_GAME)) {
        return send_txt(c, "\tE\tC7Only valid in a game lobby.");
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        return send_txt(c, "\tE\tC7Only the leader may use this command.");
    }

    /* Check the length of the provided password. */
    if(strlen(params) > 16) {
        return send_txt(c, "\tE\tC7Password too long.");
    }

    pthread_mutex_lock(&l->mutex);

    /* Copy the new password in. */
    strcpy(l->passwd, params);

    pthread_mutex_unlock(&l->mutex);

    return send_txt(c, "\tE\tC7Password set.");
}

/* Usage: /lname newname */
static int handle_lname(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(!(l->type & LOBBY_TYPE_GAME)) {
        return send_txt(c, "\tE\tC7Only valid in a game lobby.");
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        return send_txt(c, "\tE\tC7Only the leader may use this command.");
    }

    /* Check the length of the provided lobby name. */
    if(strlen(params) > 16) {
        return send_txt(c, "\tE\tC7Lobby name too long.");
    }

    pthread_mutex_lock(&l->mutex);

    /* Copy the new name in. */
    strcpy(l->name, params);

    pthread_mutex_unlock(&l->mutex);

    return send_txt(c, "\tE\tC7Lobby name set.");
}

/* Usage: /bug */
static int handle_bug(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    subcmd_dc_gcsend_t gcpkt;
    extern int handle_dc_gcsend(ship_client_t *d, subcmd_dc_gcsend_t *pkt);

    /* Forge a guildcard send packet. */
    gcpkt.hdr.pkt_type = GAME_COMMAND2_TYPE;
    gcpkt.hdr.flags = c->client_id;
    gcpkt.hdr.pkt_len = LE16(0x88);
    gcpkt.type = SUBCMD_GUILDCARD;
    gcpkt.size = 0x21;
    gcpkt.unused = 0;
    gcpkt.tag = LE32(0x00010000);
    gcpkt.guildcard = LE32(BUG_REPORT_GC);
    gcpkt.unused2 = 0;
    gcpkt.one = 1;
    gcpkt.language = CLIENT_LANG_ENGLISH;
    gcpkt.section = 0;
    gcpkt.char_class = 8;
    gcpkt.padding[0] = gcpkt.padding[1] = gcpkt.padding[1] = 0;
    sprintf(gcpkt.name, "Report Bug");
    sprintf(gcpkt.text, "Send a Simple Mail to this guildcard to report a bug");

    send_txt(c, "\tE\tC7Send a mail to the\n"
                "'Report Bug' user to report\n"
                "a bug.");

    return handle_dc_gcsend(c, &gcpkt);
}

/* Usage /clinfo client_id */
static int handle_clinfo(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;
    int id, count;
    ship_client_t *cl;
    char ip[INET_ADDRSTRLEN];

    /* Make sure the requester is a GM. */
    if(!c->is_gm) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

    /* Copy over the item data. */
    count = sscanf(params, "%d", &id);

    if(count == EOF || count == 0 || id >= l->max_clients || id < 0) {
        return send_txt(c, "\tE\tC7Invalid Client ID");
    }

    /* Make sure there is such a client. */
    if(!(cl = l->clients[id])) {
        return send_txt(c, "\tE\tC7No such client");
    }

    /* Fill in the client's info. */
    inet_ntop(AF_INET, &cl->addr, ip, INET_ADDRSTRLEN);
    return send_txt(c, "\tE\tC7Name: %s\nIP: %s\nGC: %u\n%s Lv.%d",
                    c->pl->v1.name, ip, cl->guildcard,
                    classes[cl->pl->v1.ch_class], cl->pl->v1.level + 1);
}

/* Usage: /gban:d guildcard reason */
static int handle_gban_d(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t gc;
    block_t *b = c->cur_block;
    ship_client_t *i;
    char *reason;
    char msg[256];

    /* Make sure the requester is a global GM. */
    if(c->is_gm >= 2) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "\tE\tC7Invalid Guild Card");
    }

    /* Set the ban with the shipgate first (86400s = 1 day). */
    if(shipgate_send_ban(&c->cur_ship->sg, SHDR_TYPE_GCBAN, c->guildcard, gc,
                         time(NULL) + 86400, reason + 1)) {
        return send_txt(c, "\tE\tC7Error setting ban!");
    }

    /* Look for the requested user and kick them if they're currently connected
       (only on this block). */
    TAILQ_FOREACH(i, b->clients, qentry) {
        /* Disconnect them if we find them */
        if(i->guildcard == gc) {
            if(strlen(reason) > 1) {
                sprintf(msg, "\tEYou have been banned by a GM\n"
                        "Ban Length: 1 day\n"
                        "Reason:\n%s", reason + 1);
            }
            else {
                strcpy(msg, "\tEYou have been banned by a GM\n"
                       "Ban Length: 1 day");
            }

            send_message_box(i, msg);                                
            i->disconnected = 1;
            return 0;
        }
    }

    /* The person isn't here... There's nothing left to do. */
    return 0;
}

/* Usage: /gban:w guildcard reason */
static int handle_gban_w(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t gc;
    block_t *b = c->cur_block;
    ship_client_t *i;
    char *reason;
    char msg[256];

    /* Make sure the requester is a global GM. */
    if(c->is_gm >= 2) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "\tE\tC7Invalid Guild Card");
    }

    /* Set the ban with the shipgate first (604800s = 1 week). */
    if(shipgate_send_ban(&c->cur_ship->sg, SHDR_TYPE_GCBAN, c->guildcard, gc,
                         time(NULL) + 604800, reason + 1)) {
        return send_txt(c, "\tE\tC7Error setting ban!");
    }

    /* Look for the requested user and kick them if they're currently connected
       (only on this block). */
    TAILQ_FOREACH(i, b->clients, qentry) {
        /* Disconnect them if we find them */
        if(i->guildcard == gc) {
            if(strlen(reason) > 1) {
                sprintf(msg, "\tEYou have been banned by a GM\n"
                        "Ban Length: 1 week\n"
                        "Reason:\n%s", reason + 1);
            }
            else {
                strcpy(msg, "\tEYou have been banned by a GM\n"
                       "Ban Length: 1 week");
            }

            send_message_box(i, msg);                                
            i->disconnected = 1;
            return 0;
        }
    }

    /* The person isn't here... There's nothing left to do. */
    return 0;
}

/* Usage: /gban:m guildcard reason */
static int handle_gban_m(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t gc;
    block_t *b = c->cur_block;
    ship_client_t *i;
    char *reason;
    char msg[256];

    /* Make sure the requester is a global GM. */
    if(c->is_gm >= 2) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "\tE\tC7Invalid Guild Card");
    }

    /* Set the ban with the shipgate first (2,592,000s = 30 days). */
    if(shipgate_send_ban(&c->cur_ship->sg, SHDR_TYPE_GCBAN, c->guildcard, gc,
                         time(NULL) + 2592000, reason + 1)) {
        return send_txt(c, "\tE\tC7Error setting ban!");
    }

    /* Look for the requested user and kick them if they're currently connected
       (only on this block). */
    TAILQ_FOREACH(i, b->clients, qentry) {
        /* Disconnect them if we find them */
        if(i->guildcard == gc) {
            if(strlen(reason) > 1) {
                sprintf(msg, "\tEYou have been banned by a GM\n"
                        "Ban Length: 30 days\n"
                        "Reason:\n%s", reason + 1);
            }
            else {
                strcpy(msg, "\tEYou have been banned by a GM\n"
                       "Ban Length: 30 days");
            }

            send_message_box(i, msg);                                
            i->disconnected = 1;
            return 0;
        }
    }

    /* The person isn't here... There's nothing left to do. */
    return 0;
}

/* Usage: /gban:p guildcard reason */
static int handle_gban_p(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t gc;
    block_t *b = c->cur_block;
    ship_client_t *i;
    char *reason;
    char msg[256];

    /* Make sure the requester is a global GM. */
    if(c->is_gm >= 2) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "\tE\tC7Invalid Guild Card");
    }

    /* Set the ban with the shipgate first (0xFFFFFFFF = forever (or close
       enough anyway)). */
    if(shipgate_send_ban(&c->cur_ship->sg, SHDR_TYPE_GCBAN, c->guildcard, gc,
                         0xFFFFFFFF, reason + 1)) {
        return send_txt(c, "\tE\tC7Error setting ban!");
    }

    /* Look for the requested user and kick them if they're currently connected
       (only on this block). */
    TAILQ_FOREACH(i, b->clients, qentry) {
        /* Disconnect them if we find them */
        if(i->guildcard == gc) {
            if(strlen(reason) > 1) {
                sprintf(msg, "\tEYou have been banned by a GM\n"
                        "Ban Length: Forever\n"
                        "Reason:\n%s", reason + 1);
            }
            else {
                strcpy(msg, "\tEYou have been banned by a GM\n"
                       "Ban Length: Forever");
            }

            send_message_box(i, msg);                                
            i->disconnected = 1;
            return 0;
        }
    }

    /* The person isn't here... There's nothing left to do. */
    return 0;
}

/* Usage: /list parameters (there's too much to put here) */
static int handle_list(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    /* Make sure the requester is a global GM. */
    if(!c->is_gm) {
        return send_txt(c, "\tE\tC7Nice try.");
    }

    /* Pass off to the player list code... */
    return send_player_list(c, params);
}

/* Usage: /legit */
static int handle_legit(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;
    int i;

    /* Lock the lobby mutex... we've got some work to do. */
    pthread_mutex_lock(&l->mutex);

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(!(l->type & LOBBY_TYPE_GAME)) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "\tE\tC7Only valid in a game lobby.");
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "\tE\tC7Only the leader may use this command.");
    }

    /* Set the temporarily unavailable flag on the lobby so that we can do the
       legit check, as well as legit check flag (so we know that we're doing the
       legit check). */
    l->flags |= LOBBY_FLAG_TEMP_UNAVAIL | LOBBY_FLAG_LEGIT_CHECK;
    l->legit_check_passed = 0;
    l->legit_check_done = 0;

    /* Ask each player for updated player data to do the legit check. */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i]) {
            if(send_simple(l->clients[i], CHAR_DATA_REQUEST_TYPE, 0)) {
                l->clients[i]->disconnected = 1;
            }
        }
    }

    /* We're done with the lobby for now... */
    pthread_mutex_unlock(&l->mutex);

    /* Now, we wait for the legit check to go on. */
    return 0;
}

/* Usage: /normal */
static int handle_normal(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;
    int i;

    /* Lock the lobby mutex... we've got some work to do. */
    pthread_mutex_lock(&l->mutex);

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(!(l->type & LOBBY_TYPE_GAME)) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "\tE\tC7Only valid in a game lobby.");
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "\tE\tC7Only the leader may use this command.");
    }

    /* Can't use this while waiting on a legit check. */
    if((l->flags & LOBBY_FLAG_LEGIT_CHECK)) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "\tE\tC7Please wait a while before\n"
                        "using this command.");
    }

    /* If we're not in legit mode, then this command doesn't do anything... */
    if(!(l->flags & LOBBY_FLAG_LEGIT_MODE)) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "\tE\tC7Already in normal mode.");
    }

    /* Clear the flag */
    l->flags &= ~(LOBBY_FLAG_LEGIT_MODE);

    /* Let everyone know legit mode has been turned off. */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i]) {
            send_txt(l->clients[i], "\tE\tC7Legit mode deactivated.");
        }
    }

    /* Unlock, we're done. */
    pthread_mutex_unlock(&l->mutex);

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
    { "arrow"  , handle_arrow     },
    { "login"  , handle_login     },
    { "item"   , handle_item      },
    { "item4"  , handle_item4     },
    { "event"  , handle_event     },
    { "passwd" , handle_passwd    },
    { "lname"  , handle_lname     },
    { "warpall", handle_warpall   },
    { "bug"    , handle_bug       },
    { "clinfo" , handle_clinfo    },
    { "gban:d" , handle_gban_d    },
    { "gban:w" , handle_gban_w    },
    { "gban:m" , handle_gban_m    },
    { "gban:p" , handle_gban_p    },
    { "list"   , handle_list      },
    { "legit"  , handle_legit     },
    { "normal" , handle_normal    },
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
    p2->hdr.dc.pkt_type = CHAT_TYPE;
    p2->hdr.dc.flags = 0;
    p2->hdr.dc.pkt_len = 12 + (tlen - out);
    p2->padding = 0;
    p2->guildcard = pkt->guildcard;

    /* Hand off to the normal command parsing code. */
    return command_parse(c, p2);
}
