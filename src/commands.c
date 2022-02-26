/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018,
                  2019, 2020, 2021, 2022 Lawrence Sebald

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
#include <time.h>
#include <ctype.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <sylverant/debug.h>
#include <sylverant/memory.h>

#include "ship_packets.h"
#include "lobby.h"
#include "subcmd.h"
#include "utils.h"
#include "shipgate.h"
#include "items.h"
#include "bans.h"
#include "admin.h"
#include "ptdata.h"
#include "pmtdata.h"
#include "mapdata.h"
#include "rtdata.h"
#include "scripts.h"
#include "version.h"

int handle_dc_gcsend(ship_client_t *s, ship_client_t *d,
                     subcmd_dc_gcsend_t *pkt);

typedef struct command {
    char trigger[10];
    int (*hnd)(ship_client_t *c, const char *params);
} command_t;

/* Usage: /warp area */
static int handle_warp(ship_client_t *c, const char *params) {
    unsigned long area;
    lobby_t *l = c->cur_lobby;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* Figure out the floor requested */
    errno = 0;
    area = strtoul(params, NULL, 10);

    if(errno) {
        /* Send a message saying invalid area */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid area."));
    }

    if(area > 17) {
        /* Area too large, give up */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid area."));
    }

    /* Send the person to the requested place */
    return send_warp(c, (uint8_t)area);
}

/* Usage: /warpall area */
static int handle_warpall(ship_client_t *c, const char *params) {
    unsigned long area;
    lobby_t *l = c->cur_lobby;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* Figure out the floor requested */
    errno = 0;
    area = strtoul(params, NULL, 10);

    if(errno) {
        /* Send a message saying invalid area */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid area."));
    }

    if(area > 17) {
        /* Area too large, give up */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid area."));
    }

    /* Send the person to the requested place */
    return send_lobby_warp(l, (uint8_t)area);
}

/* Usage: /kill guildcard reason */
static int handle_kill(ship_client_t *c, const char *params) {
    uint32_t gc;
    char *reason;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Hand it off to the admin functionality to do the dirty work... */
    if(strlen(reason) > 1) {
        return kill_guildcard(c, gc, reason + 1);
    }
    else {
        return kill_guildcard(c, gc, NULL);
    }
}

/* Usage: /minlvl level */
static int handle_min_level(ship_client_t *c, const char *params) {
    int lvl;
    lobby_t *l = c->cur_lobby;

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Only the leader may use this command."));
    }

    /* Figure out the level requested */
    errno = 0;
    lvl = (int)strtoul(params, NULL, 10);

    if(errno || lvl > 200 || lvl < 1) {
        /* Send a message saying invalid level */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid level value."));
    }

    /* Make sure the requested level is greater than or equal to the value for
       the game's difficulty. */
    if(lvl < game_required_level[l->difficulty]) {
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Invalid level for this difficulty."));
    }

    /* Make sure the requested level is less than or equal to the game's maximum
       level. */
    if(lvl > l->max_level) {
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Minimum level must be <= maximum."));
    }

    /* Set the value in the structure, and be on our way. */
    l->min_level = lvl;

    return send_txt(c, "%s", __(c, "\tE\tC7Minimum level set."));
}

/* Usage: /maxlvl level */
static int handle_max_level(ship_client_t *c, const char *params) {
    int lvl;
    lobby_t *l = c->cur_lobby;

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Only the leader may use this command."));
    }

    /* Figure out the level requested */
    errno = 0;
    lvl = (int)strtoul(params, NULL, 10);

    if(errno || lvl > 200 || lvl < 1) {
        /* Send a message saying invalid level */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid level value."));
    }

    /* Make sure the requested level is greater than or equal to the value for
       the game's minimum level. */
    if(lvl < l->min_level) {
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Maximum level must be >= minimum."));
    }

    /* Set the value in the structure, and be on our way. */
    l->max_level = lvl;

    return send_txt(c, "%s", __(c, "\tE\tC7Maximum level set."));
}

/* Usage: /refresh [quests, gms, or limits] */
static int handle_refresh(ship_client_t *c, const char *params) {
    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    if(!strcmp(params, "quests")) {
        return refresh_quests(c, send_txt);
    }
    else if(!strcmp(params, "gms")) {
        /* Make sure the requester is a local root. */
        if(!LOCAL_ROOT(c)) {
            return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
        }

        return refresh_gms(c, send_txt);
    }
    else if(!strcmp(params, "limits")) {
        return refresh_limits(c, send_txt);
    }
    else {
        return send_txt(c, "%s", __(c, "\tE\tC7Unknown item to refresh."));
    }
}

/* Usage: /save slot */
static int handle_save(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;
    uint32_t slot;

    /* Don't allow this if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can do that."));
    }

    /* Make sure that the requester is in a lobby, not a team */
    if(l->type != LOBBY_TYPE_DEFAULT) {
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid in a game."));
    }

    /* Not valid for Blue Burst clients */
    if(c->version == CLIENT_VERSION_BB) {
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid on Blue Burst."));
    }

    /* Figure out the slot requested */
    errno = 0;
    slot = (uint32_t)strtoul(params, NULL, 10);

    if(errno || slot > 4 || slot < 1) {
        /* Send a message saying invalid slot */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid slot value."));
    }

    /* Adjust so we don't go into the Blue Burst character data */
    slot += 4;

    /* Send the character data to the shipgate */
    if(shipgate_send_cdata(&ship->sg, c->guildcard, slot, c->pl, 1052,
                           c->cur_block->b)) {
        /* Send a message saying we couldn't save */
        return send_txt(c, "%s", __(c, "\tE\tC7Couldn't save character data."));
    }

    /* An error or success message will be sent when the shipgate gets its
       response. */
    return 0;
}

/* Usage: /restore slot */
static int handle_restore(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;
    uint32_t slot;

    /* Don't allow this if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can do that."));
    }

    /* Make sure that the requester is in a lobby, not a team */
    if(l->type != LOBBY_TYPE_DEFAULT) {
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid in a game."));
    }

    /* Not valid for Blue Burst clients */
    if(c->version == CLIENT_VERSION_BB) {
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid on Blue Burst."));
    }

    /* Figure out the slot requested */
    errno = 0;
    slot = (uint32_t)strtoul(params, NULL, 10);

    if(errno || slot > 4 || slot < 1) {
        /* Send a message saying invalid slot */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid slot value."));
    }

    /* Adjust so we don't go into the Blue Burst character data */
    slot += 4;

    /* Send the request to the shipgate. */
    if(shipgate_send_creq(&ship->sg, c->guildcard, slot)) {
        /* Send a message saying we couldn't request */
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Couldn't request character data."));
    }

    return 0;
}

/* Usage: /bstat */
static int handle_bstat(ship_client_t *c, const char *params) {
    block_t *b = c->cur_block;
    int games, players;

    /* Grab the stats from the block structure */
    pthread_rwlock_rdlock(&b->lobby_lock);
    games = b->num_games;
    pthread_rwlock_unlock(&b->lobby_lock);

    pthread_rwlock_rdlock(&b->lock);
    players = b->num_clients;
    pthread_rwlock_unlock(&b->lock);

    /* Fill in the string. */
    return send_txt(c, "\tE\tC7BLOCK%02d:\n%d %s\n%d %s", b->b, players,
                    __(c, "Users"), games, __(c, "Teams"));
}

/* Usage /bcast message */
static int handle_bcast(ship_client_t *c, const char *params) {
    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    return broadcast_message(c, params, 1);
}

/* Usage /arrow color_number */
static int handle_arrow(ship_client_t *c, const char *params) {
    int i;

    /* Don't allow this if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can do that."));
    }

    /* Set the arrow color and send the packet to the lobby. */
    i = atoi(params);
    c->arrow = i;

    send_txt(c, "%s", __(c, "\tE\tC7Arrow set."));

    return send_lobby_arrows(c->cur_lobby);
}

/* Usage /login username password */
static int handle_login(ship_client_t *c, const char *params) {
    char username[32], password[32];
    int len = 0;
    const char *ch = params;

    /* Make sure the user isn't doing something stupid. */
    if(!*params) {
        return send_txt(c, "%s", __(c, "\tE\tC7You must specify\n"
                                    "your username and\n"
                                    "password."));
    }

    /* Copy over the username/password. */
    while(*ch != ' ' && len < 32) {
        username[len++] = *ch++;
    }

    if(len == 32)
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid username."));

    username[len] = '\0';

    len = 0;
    ++ch;

    while(*ch != ' ' && *ch != '\0' && len < 32) {
        password[len++] = *ch++;
    }

    if(len == 32)
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid password."));

    password[len] = '\0';

    /* We'll get success/failure later from the shipgate. */
    return shipgate_send_usrlogin(&ship->sg, c->guildcard, c->cur_block->b,
                                  username, password, 0);
}

/* Usage /item item1,item2,item3,item4 */
static int handle_item(ship_client_t *c, const char *params) {
    uint32_t item[4] = { 0, 0, 0, 0 };
    int count;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Copy over the item data. */
    count = sscanf(params, "%x,%x,%x,%x", item + 0, item + 1, item + 2,
                   item + 3);

    if(count == EOF || count == 0) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid item code."));
    }

    c->next_item[0] = item[0];
    c->next_item[1] = item[1];
    c->next_item[2] = item[2];
    c->next_item[3] = item[3];

    return send_txt(c, "%s", __(c, "\tE\tC7Next item set successfully."));
}

/* Usage /item4 item4 */
static int handle_item4(ship_client_t *c, const char *params) {
    uint32_t item;
    int count;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Copy over the item data. */
    count = sscanf(params, "%x", &item);

    if(count == EOF || count == 0) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid item code."));
    }

    c->next_item[3] = item;

    return send_txt(c, "%s", __(c, "\tE\tC7Next item set successfully."));
}

/* Usage: /event number */
static int handle_event(ship_client_t *c, const char *params) {
    int event;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Grab the event number */
    event = atoi(params);

    if(event < 0 || event > 14) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid event code."));
    }

    ship->lobby_event = event;
    update_lobby_event();

    return send_txt(c, "%s", __(c, "\tE\tC7Event set."));
}

/* Usage: /passwd newpass */
static int handle_passwd(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;
    int len = strlen(params), i;

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Only the leader may use this command."));
    }

    /* Check the length of the provided password. */
    if(len > 16) {
        return send_txt(c, "%s", __(c, "\tE\tC7Password too long."));
    }

    /* Make sure the password only has ASCII characters */
    for(i = 0; i < len; ++i) {
        if(params[i] < 0x20 || params[i] >= 0x7F) {
            return send_txt(c, "%s",
                            __(c, "\tE\tC7Illegal character in password."));
        }
    }

    pthread_mutex_lock(&l->mutex);

    /* Copy the new password in. */
    strcpy(l->passwd, params);

    pthread_mutex_unlock(&l->mutex);

    return send_txt(c, "%s", __(c, "\tE\tC7Password set."));
}

/* Usage: /lname newname */
static int handle_lname(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Only the leader may use this command."));
    }

    /* Check the length of the provided lobby name. */
    if(strlen(params) > 16) {
        return send_txt(c, "%s", __(c, "\tE\tC7Lobby name too long."));
    }

    pthread_mutex_lock(&l->mutex);

    /* Copy the new name in. */
    strcpy(l->name, params);

    pthread_mutex_unlock(&l->mutex);

    return send_txt(c, "%s", __(c, "\tE\tC7Lobby name set."));
}

/* Usage: /bug */
static int handle_bug(ship_client_t *c, const char *params) {
    subcmd_dc_gcsend_t gcpkt;

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
    gcpkt.padding[0] = gcpkt.padding[1] = gcpkt.padding[2] = 0;
    sprintf(gcpkt.name, __(c, "Report Bug"));
    sprintf(gcpkt.text, __(c, "Send a Simple Mail to this guildcard to report "
                           "a bug."));

    send_txt(c, "%s", __(c, "\tE\tC7Send a mail to the\n"
                         "'Report Bug' user to report\n"
                         "a bug."));

    return handle_dc_gcsend(NULL, c, &gcpkt);
}

/* Usage /clinfo client_id */
static int handle_clinfo(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;
    int id, count;
    ship_client_t *cl;
    char ip[INET6_ADDRSTRLEN];

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Copy over the item data. */
    count = sscanf(params, "%d", &id);

    if(count == EOF || count == 0 || id >= l->max_clients || id < 0) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Client ID."));
    }

    /* Make sure there is such a client. */
    if(!(cl = l->clients[id])) {
        return send_txt(c, "%s", __(c, "\tE\tC7No such client."));
    }

    /* Fill in the client's info. */
    my_ntop(&cl->ip_addr, ip);
    return send_txt(c, "\tE\tC7Name: %s\nIP: %s\nGC: %u\n%s Lv.%d",
                    cl->pl->v1.name, ip, cl->guildcard,
                    classes[cl->pl->v1.ch_class], cl->pl->v1.level + 1);
}

/* Usage: /gban:d guildcard reason */
static int handle_gban_d(ship_client_t *c, const char *params) {
    uint32_t gc;
    char *reason;

    /* Make sure the requester is a global GM. */
    if(!GLOBAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Set the ban (86,400s = 1 day). */
    if(strlen(reason) > 1) {
        return global_ban(c, gc, 86400, reason + 1);
    }
    else {
        return global_ban(c, gc, 86400, NULL);
    }
}

/* Usage: /gban:w guildcard reason */
static int handle_gban_w(ship_client_t *c, const char *params) {
    uint32_t gc;
    char *reason;

    /* Make sure the requester is a global GM. */
    if(!GLOBAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Set the ban (604,800s = 1 week). */
    if(strlen(reason) > 1) {
        return global_ban(c, gc, 604800, reason + 1);
    }
    else {
        return global_ban(c, gc, 604800, NULL);
    }
}

/* Usage: /gban:m guildcard reason */
static int handle_gban_m(ship_client_t *c, const char *params) {
    uint32_t gc;
    char *reason;

    /* Make sure the requester is a global GM. */
    if(!GLOBAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Set the ban (2,592,000s = 30 days). */
    if(strlen(reason) > 1) {
        return global_ban(c, gc, 2592000, reason + 1);
    }
    else {
        return global_ban(c, gc, 2592000, NULL);
    }
}

/* Usage: /gban:p guildcard reason */
static int handle_gban_p(ship_client_t *c, const char *params) {
    uint32_t gc;
    char *reason;

    /* Make sure the requester is a global GM. */
    if(!GLOBAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Set the ban (0xFFFFFFFF = forever (or close enough for now)). */
    if(strlen(reason) > 1) {
        return global_ban(c, gc, 0xFFFFFFFF, reason + 1);
    }
    else {
        return global_ban(c, gc, 0xFFFFFFFF, NULL);
    }
}

/* Usage: /list parameters (there's too much to put here) */
static int handle_list(ship_client_t *c, const char *params) {
    /* Make sure the requester is a local GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Pass off to the player list code... */
    return send_player_list(c, params);
}

/* Usage: /legit [off] */
static int handle_legit(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;
    uint32_t v;
    sylverant_iitem_t *item;
    sylverant_limits_t *limits;
    int j, irv;

    /* Make sure the requester is in a lobby, not a team. */
    if(l->type != LOBBY_TYPE_DEFAULT)
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid in a game."));

    /* Figure out what version they're on. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
            v = ITEM_VERSION_V1;
            break;

        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
            v = ITEM_VERSION_V2;
            break;

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_XBOX:
            v = ITEM_VERSION_GC;
            break;

        case CLIENT_VERSION_EP3:
            return send_txt(c, "%s", __(c, "\tE\tC7Not valid on Episode 3."));

        case CLIENT_VERSION_BB:
            return send_txt(c, "%s", __(c, "\tE\tC7Not valid on Blue Burst."));

        default:
            return -1;
    }

    /* See if we're turning the flag off. */
    if(!strcmp(params, "off")) {
        c->flags &= ~CLIENT_FLAG_LEGIT;
        return send_txt(c, "%s", __(c, "\tE\tC7Legit mode off\n"
                                    "for any new teams."));
    }

    /* XXXX: Select the appropriate limits file. */
    pthread_rwlock_rdlock(&ship->llock);
    if(!ship->def_limits) {
        pthread_rwlock_unlock(&ship->llock);
        return send_txt(c, "%s", __(c, "\tE\tC7Legit mode not\n"
                                    "available on this\n"
                                    "ship."));
    }

    limits = ship->def_limits;

    /* Make sure the player qualifies for legit mode... */
    for(j = 0; j < c->pl->v1.inv.item_count; ++j) {
        item = (sylverant_iitem_t *)&c->pl->v1.inv.items[j];
        irv = sylverant_limits_check_item(limits, item, v);

        if(!irv) {
            debug(DBG_LOG, "Potentially non-legit item in legit mode:\n"
                  "%08x %08x %08x %08x\n", LE32(item->data_l[0]),
                  LE32(item->data_l[1]), LE32(item->data_l[2]),
                  LE32(item->data2_l));
            pthread_rwlock_unlock(&ship->llock);
            return send_txt(c, "%s", __(c, "\tE\tC7You failed the legit "
                                           "check."));
        }
    }

    /* Set the flag and retain the limits list on the client. */
    c->flags |= CLIENT_FLAG_LEGIT;
    c->limits = retain(limits);

    pthread_rwlock_unlock(&ship->llock);

    return send_txt(c, "%s", __(c, "\tE\tC7Legit mode on\n"
                                "for your next team."));
}

/* Usage: /normal */
static int handle_normal(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;
    int i;

    /* Lock the lobby mutex... we've got some work to do. */
    pthread_mutex_lock(&l->mutex);

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Only the leader may use this command."));
    }

    /* If we're not in legit mode, then this command doesn't do anything... */
    if(!(l->flags & LOBBY_FLAG_LEGIT_MODE)) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Already in normal mode."));
    }

    /* Clear the flag */
    l->flags &= ~(LOBBY_FLAG_LEGIT_MODE);

    /* Let everyone know legit mode has been turned off. */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i]) {
            send_txt(l->clients[i], "%s",
                     __(l->clients[i], "\tE\tC7Legit mode deactivated."));
        }
    }

    /* Unlock, we're done. */
    pthread_mutex_unlock(&l->mutex);

    return 0;
}

/* Usage: /shutdown minutes */
static int handle_shutdown(ship_client_t *c, const char *params) {
    uint32_t when;

    /* Make sure the requester is a local root. */
    if(!LOCAL_ROOT(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out when we're supposed to shut down. */
    errno = 0;
    when = (uint32_t)strtoul(params, NULL, 10);

    if(errno != 0) {
        /* Send a message saying invalid time */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid time."));
    }

    /* Give everyone at least a minute */
    if(when < 1) {
        when = 1;
    }

    return schedule_shutdown(c, when, 0, send_txt);
}

/* Usage: /log guildcard */
static int handle_log(ship_client_t *c, const char *params) {
    uint32_t gc;
    block_t *b = c->cur_block;
    ship_client_t *i;
    int rv;

    /* Make sure the requester is a local root. */
    if(!LOCAL_ROOT(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, NULL, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Look for the requested user and start the log */
    TAILQ_FOREACH(i, b->clients, qentry) {
        /* Start logging them if we find them */
        if(i->guildcard == gc) {
            rv = pkt_log_start(i);

            if(!rv) {
                return send_txt(c, "%s", __(c, "\tE\tC7Logging started."));
            }
            else if(rv == -1) {
                return send_txt(c, "%s", __(c, "\tE\tC7The user is already\n"
                                            "being logged."));
            }
            else if(rv == -2) {
                return send_txt(c, "%s",
                                __(c, "\tE\tC7Cannot create log file."));
            }
        }
    }

    /* The person isn't here... There's nothing left to do. */
    return send_txt(c, "%s", __(c, "\tE\tC7Requested user not\nfound."));
}

/* Usage: /endlog guildcard */
static int handle_endlog(ship_client_t *c, const char *params) {
    uint32_t gc;
    block_t *b = c->cur_block;
    ship_client_t *i;
    int rv;

    /* Make sure the requester is a local root. */
    if(!LOCAL_ROOT(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, NULL, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Look for the requested user and end the log */
    TAILQ_FOREACH(i, b->clients, qentry) {
        /* Finish logging them if we find them */
        if(i->guildcard == gc) {
            rv = pkt_log_stop(i);

            if(!rv) {
                return send_txt(c, "%s", __(c, "\tE\tC7Logging ended."));
            }
            else if(rv == -1) {
                return send_txt(c, "%s", __(c,"\tE\tC7The user is not\n"
                                            "being logged."));
            }
        }
    }

    /* The person isn't here... There's nothing left to do. */
    return send_txt(c, "%s", __(c, "\tE\tC7Requested user not\nfound."));
}

/* Usage: /motd */
static int handle_motd(ship_client_t *c, const char *params) {
    return send_motd(c);
}

/* Usage: /friendadd guildcard nickname */
static int handle_friendadd(ship_client_t *c, const char *params) {
    uint32_t gc;
    char *nick;

    /* Don't allow this if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can do that."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &nick, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Make sure the nickname is valid. */
    if(!nick || nick[0] != ' ' || nick[1] == '\0') {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Nickname."));
    }

    /* Send a request to the shipgate to do the rest */
    shipgate_send_friend_add(&ship->sg, c->guildcard, gc, nick + 1);

    /* Any further messages will be handled by the shipgate handler */
    return 0;
}

/* Usage: /frienddel guildcard */
static int handle_frienddel(ship_client_t *c, const char *params) {
    uint32_t gc;

    /* Don't allow this if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can do that."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, NULL, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Send a request to the shipgate to do the rest */
    shipgate_send_friend_del(&ship->sg, c->guildcard, gc);

    /* Any further messages will be handled by the shipgate handler */
    return 0;
}

/* Usage: /dconly [off] */
static int handle_dconly(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;
    int i;

    /* Lock the lobby mutex... we've got some work to do. */
    pthread_mutex_lock(&l->mutex);

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Only the leader may use this command."));
    }

    /* See if we're turning the flag off. */
    if(!strcmp(params, "off")) {
        l->flags &= ~LOBBY_FLAG_DCONLY;
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Dreamcast-only mode off."));
    }

    /* Check to see if all players are on a Dreamcast version */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i]) {
            if(l->clients[i]->version != CLIENT_VERSION_DCV1 &&
               l->clients[i]->version != CLIENT_VERSION_DCV2) {
                pthread_mutex_unlock(&l->mutex);
                return send_txt(c, "%s", __(c, "\tE\tC7At least one "
                                            "non-Dreamcast player is in the "
                                            "game."));
            }
        }
    }

    /* We passed the check, set the flag and unlock the lobby. */
    l->flags |= LOBBY_FLAG_DCONLY;
    pthread_mutex_unlock(&l->mutex);

    /* Tell the leader that the command has been activated. */
    return send_txt(c, "%s", __(c, "\tE\tC7Dreamcast-only mode on."));
}

/* Usage: /v1only [off] */
static int handle_v1only(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;
    int i;

    /* Lock the lobby mutex... we've got some work to do. */
    pthread_mutex_lock(&l->mutex);

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Only the leader may use this command."));
    }

    /* See if we're turning the flag off. */
    if(!strcmp(params, "off")) {
        l->flags &= ~LOBBY_FLAG_V1ONLY;
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7V1-only mode off."));
    }

    /* Check to see if all players are on V1 */
    for(i = 0; i < l->max_clients; ++i) {
        if(l->clients[i]) {
            if(l->clients[i]->version != CLIENT_VERSION_DCV1) {
                pthread_mutex_unlock(&l->mutex);
                return send_txt(c, "%s", __(c, "\tE\tC7At least one "
                                            "non-PSOv1 player is in the "
                                            "game."));
            }
        }
    }

    /* We passed the check, set the flag and unlock the lobby. */
    l->flags |= LOBBY_FLAG_V1ONLY;
    pthread_mutex_unlock(&l->mutex);

    /* Tell the leader that the command has been activated. */
    return send_txt(c, "%s", __(c, "\tE\tC7V1-only mode on."));
}

/* Usage: /forgegc guildcard name */
static int handle_forgegc(ship_client_t *c, const char *params) {
    uint32_t gc;
    char *name = NULL;
    subcmd_dc_gcsend_t gcpkt;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &name, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Make sure a name was given */
    if(!name || name[0] != ' ' || name[1] == '\0') {
        return send_txt(c, "%s", __(c, "\tE\tC7No name given."));
    }

    /* Forge the guildcard send */
    gcpkt.hdr.pkt_type = GAME_COMMAND2_TYPE;
    gcpkt.hdr.flags = c->client_id;
    gcpkt.hdr.pkt_len = LE16(0x0088);
    gcpkt.type = SUBCMD_GUILDCARD;
    gcpkt.size = 0x21;
    gcpkt.unused = 0;
    gcpkt.tag = LE32(0x00010000);
    gcpkt.guildcard = LE32(gc);
    strncpy(gcpkt.name, name + 1, 16);
    gcpkt.name[15] = 0;
    memset(gcpkt.text, 0, 88);
    gcpkt.unused2 = 0;
    gcpkt.one = 1;
    gcpkt.language = CLIENT_LANG_ENGLISH;
    gcpkt.section = 0;
    gcpkt.char_class = 8;
    gcpkt.padding[0] = gcpkt.padding[1] = gcpkt.padding[2] = 0;

    /* Send the packet */
    return handle_dc_gcsend(NULL, c, &gcpkt);
}

/* Usage: /invuln [off] */
static int handle_invuln(ship_client_t *c, const char *params) {
    pthread_mutex_lock(&c->mutex);

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        pthread_mutex_unlock(&c->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* See if we're turning the flag off. */
    if(!strcmp(params, "off")) {
        c->flags &= ~CLIENT_FLAG_INVULNERABLE;
        pthread_mutex_unlock(&c->mutex);

        return send_txt(c, "%s", __(c, "\tE\tC7Invulnerability off."));
    }

    /* Set the flag since we're turning it on. */
    c->flags |= CLIENT_FLAG_INVULNERABLE;

    pthread_mutex_unlock(&c->mutex);
    return send_txt(c, "%s", __(c, "\tE\tC7Invulnerability on."));
}

/* Usage: /inftp [off] */
static int handle_inftp(ship_client_t *c, const char *params) {
    pthread_mutex_lock(&c->mutex);

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        pthread_mutex_unlock(&c->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* See if we're turning the flag off. */
    if(!strcmp(params, "off")) {
        c->flags &= ~CLIENT_FLAG_INFINITE_TP;
        pthread_mutex_unlock(&c->mutex);

        return send_txt(c, "%s", __(c, "\tE\tC7Infinite TP off."));
    }

    /* Set the flag since we're turning it on. */
    c->flags |= CLIENT_FLAG_INFINITE_TP;

    pthread_mutex_unlock(&c->mutex);
    return send_txt(c, "%s", __(c, "\tE\tC7Infinite TP on."));
}

/* Usage: /smite clientid hp tp */
static int handle_smite(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;
    int count, id, hp, tp;
    ship_client_t *cl;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* Copy over the item data. */
    count = sscanf(params, "%d %d %d", &id, &hp, &tp);

    if(count == EOF || count < 3 || id >= l->max_clients || id < 0 || hp < 0 ||
       tp < 0 || hp > 2040 || tp > 2040) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Parameter."));
    }

    pthread_mutex_lock(&l->mutex);

    /* Make sure there is such a client. */
    if(!(cl = l->clients[id])) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7No such client."));
    }

    /* Smite the client */
    count = 0;

    if(hp) {
        send_lobby_mod_stat(l, cl, SUBCMD_STAT_HPDOWN, hp);
        ++count;
    }

    if(tp) {
        send_lobby_mod_stat(l, cl, SUBCMD_STAT_TPDOWN, tp);
        ++count;
    }

    /* Finish up */
    pthread_mutex_unlock(&l->mutex);

    if(count) {
        send_txt(cl, "%s", __(c, "\tE\tC7You have been smitten."));
        return send_txt(c, "%s", __(c, "\tE\tC7Client smitten."));
    }
    else {
        return send_txt(c, "%s", __(c, "\tE\tC7Nothing to do."));
    }
}

/* Usage: /makeitem */
static int handle_makeitem(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;
    item_t *item;
    subcmd_drop_stack_t p2;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    pthread_mutex_lock(&l->mutex);

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* Make sure there's something set with /item */
    if(!c->next_item[0]) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Need to set an item first."));
    }

    /* If we're on Blue Burst, add the item to the lobby's inventory first. */
    if(l->version == CLIENT_VERSION_BB) {
        item = lobby_add_item_locked(l, c->next_item);

        if(!item) {
            pthread_mutex_unlock(&l->mutex);
            return send_txt(c, "%s", __(c, "\tE\tC7No space for new item."));
        }
    }
    else {
        ++l->item_id;
    }

    /* Generate the packet to drop the item */
    p2.hdr.pkt_type = GAME_COMMAND0_TYPE;
    p2.hdr.pkt_len = sizeof(subcmd_drop_stack_t);
    p2.hdr.flags = 0;
    p2.type = SUBCMD_DROP_STACK;
    p2.size = 0x0A;
    p2.client_id = c->client_id;
    p2.unused = 0;
    p2.area = LE16(c->cur_area);
    p2.unk = LE16(0);
    p2.x = c->x;
    p2.z = c->z;
    p2.item[0] = LE32(c->next_item[0]);
    p2.item[1] = LE32(c->next_item[1]);
    p2.item[2] = LE32(c->next_item[2]);
    p2.item_id = LE32((l->item_id - 1));
    p2.item2 = LE32(c->next_item[3]);
    p2.two = LE32(0x00000002);

    /* Clear the set item */
    c->next_item[0] = 0;
    c->next_item[1] = 0;
    c->next_item[2] = 0;
    c->next_item[3] = 0;

    /* Send the packet to everyone in the lobby */
    pthread_mutex_unlock(&l->mutex);
    return lobby_send_pkt_dc(l, NULL, (dc_pkt_hdr_t *)&p2, 0);
}

/* Usage: /teleport client */
static int handle_teleport(ship_client_t *c, const char *params) {
    int client;
    lobby_t *l = c->cur_lobby;
    ship_client_t *c2;
    subcmd_teleport_t p2;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* Figure out the user requested */
    errno = 0;
    client = strtoul(params, NULL, 10);

    if(errno) {
        /* Send a message saying invalid client ID */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Client ID."));
    }

    if(client > l->max_clients) {
        /* Client ID too large, give up */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Client ID."));
    }

    if(!(c2 = l->clients[client])) {
        /* Client doesn't exist */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Client ID."));
    }

    /* See if we need to warp first */
    if(c2->cur_area != c->cur_area) {
        /* Send the person to the other user's area */
        return send_warp(c, (uint8_t)c2->cur_area);
    }
    else {
        /* Now, set up the teleport packet */
        p2.hdr.pkt_type = GAME_COMMAND0_TYPE;
        p2.hdr.pkt_len = sizeof(subcmd_teleport_t);
        p2.hdr.flags = 0;
        p2.type = SUBCMD_TELEPORT;
        p2.size = 5;
        p2.client_id = c->client_id;
        p2.unused = 0;
        p2.x = c2->x;
        p2.y = c2->y;
        p2.z = c2->z;
        p2.w = c2->w;

        /* Update the teleporter's position. */
        c->x = c2->x;
        c->y = c2->y;
        c->z = c2->z;
        c->w = c2->w;

        /* Send the packet to everyone in the lobby */
        return lobby_send_pkt_dc(l, NULL, (dc_pkt_hdr_t *)&p2, 0);
    }
}

static void dumpinv_internal(ship_client_t *c) {
    char name[64];
    int i;
    int v = c->version;

    if(v != CLIENT_VERSION_BB) {
        debug(DBG_LOG, "Inventory dump for %s (%d)\n", c->pl->v1.name,
              c->guildcard);

        for(i = 0; i < c->item_count; ++i) {
            debug(DBG_LOG, "%d (%08x): %08x %08x %08x %08x: %s\n", i,
                   LE32(c->items[i].item_id), LE32(c->items[i].data_l[0]),
                   LE32(c->items[i].data_l[1]), LE32(c->items[i].data_l[2]),
                   LE32(c->items[i].data2_l), item_get_name(&c->items[i], v));
        }
    }
    else {
        istrncpy16(ic_utf16_to_utf8, name, &c->bb_pl->character.name[2],
                   64);
        debug(DBG_LOG, "Inventory dump for %s (%d)\n", name, c->guildcard);

        for(i = 0; i < c->bb_pl->inv.item_count; ++i) {
            debug(DBG_LOG, "%d (%08x): %08x %08x %08x %08x: %s\n", i,
                  LE32(c->bb_pl->inv.items[i].item_id),
                  LE32(c->bb_pl->inv.items[i].data_l[0]),
                  LE32(c->bb_pl->inv.items[i].data_l[1]),
                  LE32(c->bb_pl->inv.items[i].data_l[2]),
                  LE32(c->bb_pl->inv.items[i].data2_l),
                  item_get_name(&c->bb_pl->inv.items[i], v));
            debug(DBG_LOG, "\tFlags: %08x %04x %04x\n",
                  LE32(c->bb_pl->inv.items[i].flags),
                  LE16(c->bb_pl->inv.items[i].equipped),
                  LE16(c->bb_pl->inv.items[i].tech));
        }
    }
}

/* Usage: /dumpinv [lobby/clientid/guildcard] */
static int handle_dumpinv(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;
    lobby_item_t *j;
    int do_lobby;
    uint32_t client;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    do_lobby = params && !strcmp(params, "lobby");

    /* If the arguments say "lobby", then dump the lobby's inventory. */
    if(do_lobby) {
        pthread_mutex_lock(&l->mutex);

        if(l->type != LOBBY_TYPE_GAME || l->version != CLIENT_VERSION_BB) {
            pthread_mutex_unlock(&l->mutex);
            return send_txt(c, "%s", __(c, "\tE\tC7Invalid request."));
        }

        debug(DBG_LOG, "Inventory dump for lobby %s (%" PRIu32 ")\n", l->name,
              l->lobby_id);

        TAILQ_FOREACH(j, &l->item_queue, qentry) {
            debug(DBG_LOG, "%08x: %08x %08x %08x %08x: %s\n",
                  LE32(j->d.item_id), LE32(j->d.data_l[0]),
                  LE32(j->d.data_l[1]), LE32(j->d.data_l[2]),
                  LE32(j->d.data2_l), item_get_name(&j->d, l->version));
        }

        pthread_mutex_unlock(&l->mutex);
    }
    /* If there's no arguments, then dump the caller's inventory. */
    else if(!params || params[0] == '\0') {
        dumpinv_internal(c);
    }
    /* Otherwise, try to parse the arguments. */
    else {
        /* Figure out the user requested */
        errno = 0;
        client = strtoul(params, NULL, 10);

        if(errno) {
            return send_txt(c, "%s", __(c, "\tE\tC7Invalid Target."));
        }

        /* See if we have a client ID or a guild card number... */
        if(client < 12) {
            pthread_mutex_lock(&l->mutex);

            if(client >= 4 && l->type == LOBBY_TYPE_GAME) {
                pthread_mutex_unlock(&l->mutex);
                return send_txt(c, "%s", __(c, "\tE\tC7Invalid Client ID."));
            }

            if(l->clients[client]) {
                dumpinv_internal(l->clients[client]);
            }
            else {
                pthread_mutex_unlock(&l->mutex);
                return send_txt(c, "%s", __(c, "\tE\tC7Invalid Client ID."));
            }

            pthread_mutex_unlock(&l->mutex);
        }
        /* Otherwise, assume we're looking for a guild card number. */
        else {
            ship_client_t *target = block_find_client(c->cur_block, client);

            if(!target) {
                return send_txt(c, "%s", __(c, "\tE\tC7Requested user not\n"
                                               "found."));
            }

            dumpinv_internal(target);
        }
    }

    return send_txt(c, "%s", __(c, "\tE\tC7Dumped inventory to log file."));
}

/* Usage: /showdcpc [off] */
static int handle_showdcpc(ship_client_t *c, const char *params) {
    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Check if the client is on PSOGC */
    if(c->version != CLIENT_VERSION_GC) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid on Gamecube."));
    }

    /* See if we're turning the flag off. */
    if(!strcmp(params, "off")) {
        c->flags &= ~CLIENT_FLAG_SHOW_DCPC_ON_GC;
        return send_txt(c, "%s", __(c, "\tE\tC7DC/PC games hidden."));
    }

    /* Set the flag, and tell the client that its been set. */
    c->flags |= CLIENT_FLAG_SHOW_DCPC_ON_GC;
    return send_txt(c, "%s", __(c, "\tE\tC7DC/PC games visible."));
}

/* Usage: /allowgc [off] */
static int handle_allowgc(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;

    /* Lock the lobby mutex... we've got some work to do. */
    pthread_mutex_lock(&l->mutex);

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Only the leader may use this command."));
    }

    /* See if we're turning the flag off. */
    if(!strcmp(params, "off")) {
        l->flags &= ~LOBBY_FLAG_GC_ALLOWED;
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Gamecube disallowed."));
    }

    /* Make sure there's no conflicting flags */
    if((l->flags & LOBBY_FLAG_DCONLY) || (l->flags & LOBBY_FLAG_PCONLY) ||
       (l->flags & LOBBY_FLAG_V1ONLY)) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Game flag conflict."));
    }

    /* We passed the check, set the flag and unlock the lobby. */
    l->flags |= LOBBY_FLAG_GC_ALLOWED;
    pthread_mutex_unlock(&l->mutex);

    /* Tell the leader that the command has been activated. */
    return send_txt(c, "%s", __(c, "\tE\tC7Gamecube allowed."));
}

/* Usage /ws item1,item2,item3,item4 */
static int handle_ws(ship_client_t *c, const char *params) {
    uint32_t ws[4] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
    int count;
    uint8_t tmp[0x24];
    subcmd_pkt_t *p = (subcmd_pkt_t *)tmp;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Copy over the item data. */
    count = sscanf(params, "%x,%x,%x,%x", ws + 0, ws + 1, ws + 2, ws + 3);

    if(count == EOF || count == 0) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid WS code."));
    }

    /* Fill in the packet */
    memset(p, 0, 0x24);
    memset(&tmp[0x0C], 0xFF, 0x10);
    p->hdr.dc.pkt_type = GAME_COMMAND0_TYPE;
    p->hdr.dc.pkt_len = LE16(0x24);
    p->type = SUBCMD_WORD_SELECT;
    p->size = 0x08;

    if(c->version != CLIENT_VERSION_GC) {
        tmp[6] = c->client_id;
    }
    else {
        tmp[7] = c->client_id;
    }

    tmp[8] = count;
    tmp[10] = 1;
    tmp[12] = (uint8_t)(ws[0]);
    tmp[13] = (uint8_t)(ws[0] >> 8);
    tmp[14] = (uint8_t)(ws[1]);
    tmp[15] = (uint8_t)(ws[1] >> 8);
    tmp[16] = (uint8_t)(ws[2]);
    tmp[17] = (uint8_t)(ws[2] >> 8);
    tmp[18] = (uint8_t)(ws[3]);
    tmp[19] = (uint8_t)(ws[3] >> 8);

    /* Send the packet to everyone, including the person sending the request. */
    send_pkt_dc(c, (dc_pkt_hdr_t *)p);
    return subcmd_handle_bcast(c, p);
}

/* Usage: /ll */
static int handle_ll(ship_client_t *c, const char *params) {
    char str[512];
    lobby_t *l = c->cur_lobby;
    int i;
    ship_client_t *c2;
    size_t len;

    /* Don't allow this if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can do that."));
    }

    str[0] = '\t';
    str[1] = 'E';
    len = 2;

    if(LOCAL_GM(c)) {
        for(i = 0; i < l->max_clients; i += 2) {
            if((c2 = l->clients[i])) {
                len += snprintf(str + len, 511 - len, "%d: %s (%" PRIu32 ")   ",
                                i, c2->pl->v1.name, c2->guildcard);
            }
            else {
                len += snprintf(str + len, 511 - len, "%d: None   ", i);
            }

            if((i + 1) < l->max_clients) {
                if((c2 = l->clients[i + 1])) {
                    len += snprintf(str + len, 511 - len, "%d: %s (%" PRIu32
                                    ")\n", i + 1, c2->pl->v1.name,
                                    c2->guildcard);
                }
                else {
                    len += snprintf(str + len, 511 - len, "%d: None\n", i + 1);
                }
            }
        }
    }
    else {
        for(i = 0; i < l->max_clients; i += 2) {
            if((c2 = l->clients[i])) {
                len += snprintf(str + len, 511 - len, "%d: %s   ", i,
                                c2->pl->v1.name);
            }
            else {
                len += snprintf(str + len, 511 - len, "%d: None   ", i);
            }

            if((i + 1) < l->max_clients) {
                if((c2 = l->clients[i + 1])) {
                    len += snprintf(str + len, 511 - len, "%d: %s\n", i + 1,
                                    c2->pl->v1.name);
                }
                else {
                    len += snprintf(str + len, 511 - len, "%d: None\n", i + 1);
                }
            }
        }
    }

    /* Make sure the string is terminated properly. */
    str[511] = '\0';

    /* Send the packet away */
    return send_message_box(c, "%s", str);
}

/* Usage /npc number,client_id,follow_id */
static int handle_npc(ship_client_t *c, const char *params) {
    int count, npcnum, client_id, follow;
    uint8_t tmp[0x10];
    subcmd_pkt_t *p = (subcmd_pkt_t *)tmp;
    lobby_t *l = c->cur_lobby;

    pthread_mutex_lock(&l->mutex);

    if(l->type != LOBBY_TYPE_GAME) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* For now, limit only to lobbies with a single person in them... */
    if(l->num_clients != 1) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game with\n"
                                    "one player."));
    }

    /* Also, make sure its not a battle or challenge lobby. */
    if(l->battle || l->challenge) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid in battle or\n"
                                    "challenge modes."));
    }

    /* Make sure we're not in legit mode. */
    if((l->flags & LOBBY_FLAG_LEGIT_MODE)) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid in legit\n"
                                    "mode."));
    }

    /* Make sure we're not in a quest. */
    if((l->flags & LOBBY_FLAG_QUESTING)) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid in quests."));
    }

    /* Make sure we're on Pioneer 2. */
    if(c->cur_area != 0) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid on\n"
                                    "Pioneer 2."));
    }

    /* Figure out what we're supposed to do. */
    count = sscanf(params, "%d,%d,%d", &npcnum, &client_id, &follow);

    if(count == EOF || count == 0) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid NPC data."));
    }

    /* Fill in sane defaults. */
    if(count == 1) {
        /* Find an open slot... */
        for(client_id = l->max_clients - 1; client_id >= 0; --client_id) {
            if(!l->clients[client_id]) {
                break;
            }
        }

        follow = c->client_id;
    }
    else if(count == 2) {
        follow = c->client_id;
    }

    /* Check the validity of arguments */
    if(npcnum < 0 || npcnum > 63) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid NPC number."));
    }

    if(client_id < 0 || client_id >= l->max_clients || l->clients[client_id]) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid client ID given,\n"
                                    "or no open client slots."));
    }

    if(follow < 0 || follow >= l->max_clients || !l->clients[follow]) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid follow client given."));
    }

    /* This command, for now anyway, locks us down to one player mode. */
    l->flags |= LOBBY_FLAG_SINGLEPLAYER | LOBBY_FLAG_HAS_NPC;

    /* We're done with the lobby data now... */
    pthread_mutex_unlock(&l->mutex);

    /* Fill in the packet */
    memset(p, 0, 0x10);
    p->hdr.dc.pkt_type = GAME_COMMAND0_TYPE;
    p->hdr.dc.pkt_len = LE16(0x0010);
    p->type = SUBCMD_SPAWN_NPC;
    p->size = 0x03;

    tmp[6] = 0x01;
    tmp[7] = 0x01;
    tmp[8] = follow;
    tmp[10] = client_id;
    tmp[14] = npcnum;

    /* Send the packet to everyone, including the person sending the request. */
    send_pkt_dc(c, (dc_pkt_hdr_t *)p);
    return subcmd_handle_bcast(c, p);
}

/* Usage: /stfu guildcard */
static int handle_stfu(ship_client_t *c, const char *params) {
    uint32_t gc;
    block_t *b = c->cur_block;
    ship_client_t *i;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, NULL, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Look for the requested user and STFU them (only on this block). */
    TAILQ_FOREACH(i, b->clients, qentry) {
        if(i->guildcard == gc && i->privilege < c->privilege) {
            i->flags |= CLIENT_FLAG_STFU;
            return send_txt(c, "%s", __(c, "\tE\tC7Client STFUed."));
        }
    }

    /* The person isn't here... There's nothing left to do. */
    return send_txt(c, "%s", __(c, "\tE\tC7Guildcard not found"));
}

/* Usage: /unstfu guildcard */
static int handle_unstfu(ship_client_t *c, const char *params) {
    uint32_t gc;
    block_t *b = c->cur_block;
    ship_client_t *i;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, NULL, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Look for the requested user and un-STFU them (only on this block). */
    TAILQ_FOREACH(i, b->clients, qentry) {
        if(i->guildcard == gc) {
            i->flags &= ~CLIENT_FLAG_STFU;
            return send_txt(c, "%s", __(c, "\tE\tC7Client un-STFUed."));
        }
    }

    /* The person isn't here... There's nothing left to do. */
    return send_txt(c, "%s", __(c, "\tE\tC7Guildcard not found."));
}

/* Usage: /ignore client_id */
static int handle_ignore(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;
    int id, i;
    ship_client_t *cl;

    /* Don't allow this if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can do that."));
    }

    /* Copy over the ID. */
    i = sscanf(params, "%d", &id);

    if(i == EOF || i == 0 || id >= l->max_clients || id < 0) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Client ID."));
    }

    /* Lock the lobby so we don't mess anything up in grabbing this... */
    pthread_mutex_lock(&l->mutex);

    /* Make sure there is such a client. */
    if(!(cl = l->clients[id])) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7No such client."));
    }

    /* Find an empty spot to put this in. */
    for(i = 0; i < CLIENT_IGNORE_LIST_SIZE; ++i) {
        if(!c->ignore_list[i]) {
            c->ignore_list[i] = cl->guildcard;
            pthread_mutex_unlock(&l->mutex);
            return send_txt(c, "%s %s\n%s %d", __(c, "\tE\tC7Ignoring"),
                            cl->pl->v1.name, __(c, "Entry"), i);
        }
    }

    /* If we get here, the ignore list is full, report that to the user... */
    pthread_mutex_unlock(&l->mutex);
    return send_txt(c, "%s", __(c, "\tE\tC7Ignore list full."));
}

/* Usage: /unignore entry_number */
static int handle_unignore(ship_client_t *c, const char *params) {
    int id, i;

    /* Don't allow this if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can do that."));
    }

    /* Copy over the ID */
    i = sscanf(params, "%d", &id);

    if(i == EOF || i == 0 || id >= CLIENT_IGNORE_LIST_SIZE || id < 0) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Entry Number."));
    }

    /* Clear that entry of the ignore list */
    c->ignore_list[id] = 0;

    return send_txt(c, "%s", __(c, "\tE\tC7Ignore list entry cleared."));
}

/* Usage: /quit */
static int handle_quit(ship_client_t *c, const char *params) {
    c->flags |= CLIENT_FLAG_DISCONNECTED;
    return 0;
}

/* Usage: /gameevent number */
static int handle_gameevent(ship_client_t *c, const char *params) {
    int event;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Grab the event number */
    event = atoi(params);

    if(event < 0 || event > 6) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid event code."));
    }

    ship->game_event = event;

    return send_txt(c, "%s", __(c, "\tE\tC7Game Event set."));
}

/* Usage: /ban:d guildcard reason */
static int handle_ban_d(ship_client_t *c, const char *params) {
    uint32_t gc;
    block_t *b;
    ship_client_t *i;
    char *reason;
    int j;

    /* Make sure the requester is a local GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Set the ban in the list (86,400s = 7 days) */
    if(ban_guildcard(ship, time(NULL) + 86400, c->guildcard, gc, reason + 1)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Error setting ban."));
    }

    /* Look for the requested user and kick them if they're on the ship. */
    for(j = 0; j < ship->cfg->blocks; ++j) {
        if((b = ship->blocks[j])) {
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(i, b->clients, qentry) {
                /* Disconnect them if we find them */
                if(i->guildcard == gc) {
                    if(strlen(reason) > 1) {
                        send_message_box(i, "%s\n%s %s\n%s\n%s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship."), __(i, "Ban Length:"),
                                         __(i, "1 day"), __(i, "Reason:"),
                                         reason + 1);
                    }
                    else {
                        send_message_box(i, "%s\n%s %s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship."), __(i, "Ban Length:"),
                                         __(i, "1 day"));
                    }

                    i->flags |= CLIENT_FLAG_DISCONNECTED;
                }
            }

            pthread_rwlock_unlock(&b->lock);
        }
    }

    return send_txt(c, "%s", __(c, "\tE\tC7Successfully set ban."));
}

/* Usage: /ban:w guildcard reason */
static int handle_ban_w(ship_client_t *c, const char *params) {
    uint32_t gc;
    block_t *b;
    ship_client_t *i;
    char *reason;
    int j;

    /* Make sure the requester is a local GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Set the ban in the list (604,800s = 7 days) */
    if(ban_guildcard(ship, time(NULL) + 604800, c->guildcard, gc, reason + 1)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Error setting ban."));
    }

    /* Look for the requested user and kick them if they're on the ship. */
    for(j = 0; j < ship->cfg->blocks; ++j) {
        if((b = ship->blocks[j])) {
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(i, b->clients, qentry) {
                /* Disconnect them if we find them */
                if(i->guildcard == gc) {
                    if(strlen(reason) > 1) {
                        send_message_box(i, "%s\n%s %s\n%s\n%s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship."), __(i, "Ban Length:"),
                                         __(i, "1 week"), __(i, "Reason:"),
                                         reason + 1);
                    }
                    else {
                        send_message_box(i, "%s\n%s %s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship."), __(i, "Ban Length:"),
                                         __(i, "1 week"));
                    }

                    i->flags |= CLIENT_FLAG_DISCONNECTED;
                }
            }

            pthread_rwlock_unlock(&b->lock);
        }
    }

    return send_txt(c, "%s", __(c, "\tE\tC7Successfully set ban."));
}

/* Usage: /ban:m guildcard reason */
static int handle_ban_m(ship_client_t *c, const char *params) {
    uint32_t gc;
    block_t *b;
    ship_client_t *i;
    char *reason;
    int j;

    /* Make sure the requester is a local GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Set the ban in the list (2,592,000s = 30 days) */
    if(ban_guildcard(ship, time(NULL) + 2592000, c->guildcard, gc,
                     reason + 1)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Error setting ban."));
    }

    /* Look for the requested user and kick them if they're on the ship. */
    for(j = 0; j < ship->cfg->blocks; ++j) {
        if((b = ship->blocks[j])) {
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(i, b->clients, qentry) {
                /* Disconnect them if we find them */
                if(i->guildcard == gc) {
                    if(strlen(reason) > 1) {
                        send_message_box(i, "%s\n%s %s\n%s\n%s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship."), __(i, "Ban Length:"),
                                         __(i, "30 days"), __(i, "Reason:"),
                                         reason + 1);
                    }
                    else {
                        send_message_box(i, "%s\n%s %s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship."), __(i, "Ban Length:"),
                                         __(i, "30 days"));
                    }

                    i->flags |= CLIENT_FLAG_DISCONNECTED;
                }
            }

            pthread_rwlock_unlock(&b->lock);
        }
    }

    return send_txt(c, "%s", __(c, "\tE\tC7Successfully set ban."));
}

/* Usage: /ban:p guildcard reason */
static int handle_ban_p(ship_client_t *c, const char *params) {
    uint32_t gc;
    block_t *b;
    ship_client_t *i;
    int j;
    char *reason;

    /* Make sure the requester is a local GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Set the ban in the list. An end time of -1 = forever */
    if(ban_guildcard(ship, (time_t)-1, c->guildcard, gc, reason + 1)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Error setting ban."));
    }

    /* Look for the requested user and kick them if they're on the ship. */
    for(j = 0; j < ship->cfg->blocks; ++j) {
        if((b = ship->blocks[j])) {
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(i, b->clients, qentry) {
                /* Disconnect them if we find them */
                if(i->guildcard == gc) {
                    if(strlen(reason) > 1) {
                        send_message_box(i, "%s\n%s %s\n%s\n%s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship."), __(i, "Ban Length:"),
                                         __(i, "Forever"), __(i, "Reason:"),
                                         reason + 1);
                    }
                    else {
                        send_message_box(i, "%s\n%s %s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship."), __(i, "Ban Length:"),
                                         __(i, "Forever"));
                    }

                    i->flags |= CLIENT_FLAG_DISCONNECTED;
                }
            }

            pthread_rwlock_unlock(&b->lock);
        }
    }

    return send_txt(c, "%s", __(c, "\tE\tC7Successfully set ban."));
}

/* Usage: /unban guildcard */
static int handle_unban(ship_client_t *c, const char *params) {
    uint32_t gc;
    int rv;

    /* Make sure the requester is a local GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, NULL, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Attempt to lift the ban */
    rv = ban_lift_guildcard_ban(ship, gc);

    /* Did we succeed? */
    if(!rv) {
        return send_txt(c, "%s", __(c, "\tE\tC7Lifted ban."));
    }
    else if(rv == -1) {
        return send_txt(c, "%s", __(c, "\tE\tC7User not banned."));
    }
    else {
        return send_txt(c, "%s", __(c, "\tE\tC7Error lifting ban."));
    }
}

/* Usage: /cc [any ascii char] or /cc off */
static int handle_cc(ship_client_t *c, const char *params) {
    /* Don't allow this if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can do that."));
    }

    /* Are we turning it off? */
    if(!strcmp(params, "off")) {
        c->cc_char = 0;
        return send_txt(c, "%s", __(c, "\tE\tC7Color Chat off."));
    }

    /* Make sure they only gave one character */
    if(strlen(params) != 1) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid trigger character."));
    }

    /* Set the char in the client struct */
    c->cc_char = params[0];
    return send_txt(c, "%s", __(c, "\tE\tC7Color Chat on."));
}

/* Usage: /qlang [2 character language code] */
static int handle_qlang(ship_client_t *c, const char *params) {
    int i;

    /* Don't allow this if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can do that."));
    }

    /* Make sure they only gave one character */
    if(strlen(params) != 2) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid language code."));
    }

    /* Look for the specified language code */
    for(i = 0; i < CLIENT_LANG_COUNT; ++i) {
        if(!strcmp(language_codes[i], params)) {
            c->q_lang = i;

            shipgate_send_user_opt(&ship->sg, c->guildcard, c->cur_block->b,
                                   USER_OPT_QUEST_LANG, 1, &c->q_lang);

            return send_txt(c, "%s", __(c, "\tE\tC7Quest language set."));
        }
    }

    return send_txt(c, "%s", __(c, "\tE\tC7Invalid language code."));
}

/* Usage: /friends page */
static int handle_friends(ship_client_t *c, const char *params) {
    block_t *b = c->cur_block;
    uint32_t page;

    /* Don't allow this if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can do that."));
    }

    /* Figure out the user requested */
    errno = 0;
    page = (uint32_t)strtoul(params, NULL, 10);

    if(errno != 0) {
        /* Send a message saying invalid page number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Page."));
    }

    /* Send the request to the shipgate */
    return shipgate_send_frlist_req(&ship->sg, c->guildcard, b->b, page * 5);
}

/* Usage: /gbc message */
static int handle_gbc(ship_client_t *c, const char *params) {
    /* Make sure the requester is a Global GM. */
    if(!GLOBAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Make sure there's a message to send */
    if(!strlen(params)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Forget something?"));
    }

    return shipgate_send_global_msg(&ship->sg, c->guildcard, params);
}

/* Usage: /logout */
static int handle_logout(ship_client_t *c, const char *params) {
    /* See if they're logged in first */
    if(!(c->flags & CLIENT_FLAG_LOGGED_IN)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Not logged in."));
    }

    /* Clear the logged in status. */
    c->flags &= ~(CLIENT_FLAG_LOGGED_IN | CLIENT_FLAG_OVERRIDE_GAME);
    c->privilege &= ~(CLIENT_PRIV_LOCAL_GM | CLIENT_PRIV_LOCAL_ROOT);

    return send_txt(c, "%s", __(c, "\tE\tC7Logged out."));
}

/* Usage: /override */
static int handle_override(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;

    /* Make sure the requester is a GM */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Make sure that the requester is in a lobby, not a team */
    if(l->type != LOBBY_TYPE_DEFAULT) {
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid in a game."));
    }

    /* Set the flag so we know when they join a lobby */
    c->flags |= CLIENT_FLAG_OVERRIDE_GAME;

    return send_txt(c, "%s", __(c, "\tE\tC7Lobby restriction override on."));
}

/* Usage: /ver */
static int handle_ver(ship_client_t *c, const char *params) {
    return send_txt(c, "%s: %s\n%s: %s", __(c, "\tE\tC7Git Build"),
                    GIT_BUILD, __(c, "Changeset"), GIT_SHAID_SHORT);
}

/* Usage: /restart minutes */
static int handle_restart(ship_client_t *c, const char *params) {
    uint32_t when;

    /* Make sure the requester is a local root. */
    if(!LOCAL_ROOT(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out when we're supposed to shut down. */
    errno = 0;
    when = (uint32_t)strtoul(params, NULL, 10);

    if(errno != 0) {
        /* Send a message saying invalid time */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid time."));
    }

    /* Give everyone at least a minute */
    if(when < 1) {
        when = 1;
    }

    return schedule_shutdown(c, when, 1, send_txt);
}

/* Usage: /search guildcard */
static int handle_search(ship_client_t *c, const char *params) {
    uint32_t gc;
    dc_guild_search_pkt dc;
    bb_guild_search_pkt bb;

    /* Don't allow this if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can do that."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, NULL, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card."));
    }

    /* Hacky? Maybe a bit, but this will work just fine. */
    if(c->version == CLIENT_VERSION_BB) {
        bb.hdr.pkt_len = LE16(0x14);
        bb.hdr.pkt_type = LE16(GUILD_SEARCH_TYPE);
        bb.hdr.flags = 0;
        bb.tag = LE32(0x00010000);
        bb.gc_search = LE32(c->guildcard);
        bb.gc_target = LE32(gc);

        return block_process_pkt(c, (uint8_t *)&bb);
    }
    else if(c->version == CLIENT_VERSION_PC) {
        dc.hdr.pc.pkt_len = LE16(0x10);
        dc.hdr.pc.pkt_type = GUILD_SEARCH_TYPE;
        dc.hdr.pc.flags = 0;
        dc.tag = LE32(0x00010000);
        dc.gc_search = LE32(c->guildcard);
        dc.gc_target = LE32(gc);

        return block_process_pkt(c, (uint8_t *)&dc);
    }
    else {
        dc.hdr.dc.pkt_len = LE16(0x10);
        dc.hdr.dc.pkt_type = GUILD_SEARCH_TYPE;
        dc.hdr.dc.flags = 0;
        dc.tag = LE32(0x00010000);
        dc.gc_search = LE32(c->guildcard);
        dc.gc_target = LE32(gc);

        return block_process_pkt(c, (uint8_t *)&dc);
    }
}

/* Usage: /gm */
static int handle_gm(ship_client_t *c, const char *params) {
    /* Make sure the requester is a local GM, at least. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    return send_gm_menu(c, MENU_ID_GM);
}

/* Usage: /maps [numeric string] */
static int handle_maps(ship_client_t *c, const char *params) {
    uint32_t maps[32] = { 0 };
    int i = 0;

    /* Make sure the requester is a local GM, at least. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Read in the maps string, one character at a time. Any undefined entries
       at the end will give you a 0 in their place. */
    while(*params) {
        if(!isdigit(*params)) {
            return send_txt(c, "%s", __(c, "\tE\tC7Invalid map entry."));
        }
        else if(i > 31) {
            return send_txt(c, "%s", __(c, "\tE\tC7Too many entries."));
        }

        maps[i++] = *params - '0';
        params++;
    }

    /* Free any old set, if there is one. */
    if(c->next_maps) {
        free(c->next_maps);
    }

    /* Save the maps string into the client's struct. */
    c->next_maps = (uint32_t *)malloc(sizeof(uint32_t) * 32);
    if(!c->next_maps) {
        return send_txt(c, "%s", __(c, "\tE\tC7Unknown error."));
    }

    memcpy(c->next_maps, maps, sizeof(uint32_t) * 32);

    /* We're done. */
    return send_txt(c, "%s", __(c, "\tE\tC7Set maps for next team."));
}

/* Usage: /showmaps */
static int handle_showmaps(ship_client_t *c, const char *params) {
    char string[33];
    int i;
    lobby_t *l= c->cur_lobby;

    pthread_mutex_lock(&l->mutex);

    if(l->type != LOBBY_TYPE_GAME) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* Build the string from the map list */
    for(i = 0; i < 32; ++i) {
        string[i] = l->maps[i] + '0';
    }

    string[32] = 0;

    pthread_mutex_unlock(&l->mutex);
    return send_txt(c, "%s\n%s", __(c, "\tE\tC7Maps in use:"), string);
}

/* Usage: /restorebk */
static int handle_restorebk(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;

    /* Don't allow this if they have the protection flag on. */
    if(c->flags & CLIENT_FLAG_GC_PROTECT) {
        return send_txt(c, __(c, "\tE\tC7You must login before\n"
                              "you can do that."));
    }

    /* Make sure that the requester is in a lobby, not a team */
    if(l->type != LOBBY_TYPE_DEFAULT) {
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid in a game."));
    }

    /* Not valid for Blue Burst clients */
    if(c->version == CLIENT_VERSION_BB) {
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid on Blue Burst."));
    }

    /* Send the request to the shipgate. */
    if(shipgate_send_cbkup_req(&ship->sg, c->guildcard, c->cur_block->b,
                               c->pl->v1.name)) {
        /* Send a message saying we couldn't request */
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Couldn't request character data."));
    }

    return 0;
}

/* Usage /enablebk */
static int handle_enablebk(ship_client_t *c, const char *params) {
    uint8_t enable = 1;

    /* Make sure the user is logged in */
    if(!(c->flags & CLIENT_FLAG_LOGGED_IN)) {
        return send_txt(c, "%s", __(c, "\tE\tC7You must be logged in to "
                                    "use this command."));
    }

    /* Send the message to the shipgate */
    shipgate_send_user_opt(&ship->sg, c->guildcard, c->cur_block->b,
                           USER_OPT_ENABLE_BACKUP, 1, &enable);
    c->flags |= CLIENT_FLAG_AUTO_BACKUP;
    return send_txt(c, "%s", __(c, "\tE\tC7Character backups enabled."));
}

/* Usage /disablebk */
static int handle_disablebk(ship_client_t *c, const char *params) {
    uint8_t enable = 0;

    /* Make sure the user is logged in */
    if(!(c->flags & CLIENT_FLAG_LOGGED_IN)) {
        return send_txt(c, "%s", __(c, "\tE\tC7You must be logged in to "
                                    "use this command."));
    }

    /* Send the message to the shipgate */
    shipgate_send_user_opt(&ship->sg, c->guildcard, c->cur_block->b,
                           USER_OPT_ENABLE_BACKUP, 1, &enable);
    c->flags &= ~(CLIENT_FLAG_AUTO_BACKUP);
    return send_txt(c, "%s", __(c, "\tE\tC7Character backups disabled."));
}

/* Usage: /exp amount */
static int handle_exp(ship_client_t *c, const char *params) {
    uint32_t amt;
    lobby_t *l = c->cur_lobby;

    /* Make sure the requester is a local GM, at least. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Make sure that the requester is in a team, not a lobby */
    if(l->type != LOBBY_TYPE_GAME) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
    }

    /* Make sure the requester is on Blue Burst */
    if(c->version != CLIENT_VERSION_BB) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid on Blue Burst."));
    }

    /* Figure out the amount requested */
    errno = 0;
    amt = (uint32_t)strtoul(params, NULL, 10);

    if(errno != 0) {
        /* Send a message saying invalid amount */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid amount."));
    }

    return client_give_exp(c, amt);
}

/* Usage: /level [destination level, or blank for one level up] */
static int handle_level(ship_client_t *c, const char *params) {
    uint32_t amt;
    lobby_t *l = c->cur_lobby;

    /* Make sure the requester is a local GM, at least. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    if(c->version == CLIENT_VERSION_BB) {
        /* Make sure that the requester is in a team, not a lobby */
        if(l->type != LOBBY_TYPE_GAME) {
            return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));
        }

        /* Figure out the level requested */
        if(params && strlen(params)) {
            errno = 0;
            amt = (uint32_t)strtoul(params, NULL, 10);

            if(errno != 0) {
                /* Send a message saying invalid amount */
                return send_txt(c, "%s", __(c, "\tE\tC7Invalid level."));
            }

            amt -= 1;
        }
        else {
            amt = LE32(c->bb_pl->character.level) + 1;
        }

        /* If the level is too high, let them know. */
        if(amt > 199) {
            return send_txt(c, "%s", __(c, "\tE\tC7Invalid level."));
        }

        return client_give_level(c, amt);
    }
    else if(c->version == CLIENT_VERSION_DCV2 ||
            c->version == CLIENT_VERSION_PC) {
        /* Make sure that the requester is in a lobby, not a team */
        if(l->type == LOBBY_TYPE_GAME) {
            return send_txt(c, "%s", __(c, "\tE\tC7Not valid in a team."));
        }

        /* Figure out the level requested */
        if(params && strlen(params)) {
            errno = 0;
            amt = (uint32_t)strtoul(params, NULL, 10);

            if(errno != 0) {
                /* Send a message saying invalid amount */
                return send_txt(c, "%s", __(c, "\tE\tC7Invalid level."));
            }

            amt -= 1;
        }
        else {
            amt = LE32(c->pl->v1.level) + 1;
        }

        /* If the level is too high, let them know. */
        if(amt > 199) {
            return send_txt(c, "%s", __(c, "\tE\tC7Invalid level."));
        }

        return client_give_level_v2(c, amt);
    }
    else {
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid on your version."));
    }
}

/* Usage: /sdrops [off] */
static int handle_sdrops(ship_client_t *c, const char *params) {
    /* See if we can enable server-side drops or not on this ship. */
    switch(c->version) {
        case CLIENT_VERSION_DCV1:
        case CLIENT_VERSION_DCV2:
        case CLIENT_VERSION_PC:
            if(!pt_v2_enabled() || !map_have_v2_maps() || !pmt_v2_enabled() ||
               !rt_v2_enabled())
                return send_txt(c, "%s", __(c, "\tE\tC7Server-side drops not\n"
                                            "suported on this ship for\n"
                                            "this client version."));
            break;

        case CLIENT_VERSION_GC:
        case CLIENT_VERSION_XBOX:
            /* XXXX: GM-only until they're fixed... */
            if(!pt_gc_enabled() || !map_have_gc_maps() || !pmt_gc_enabled() ||
               !rt_gc_enabled() || !LOCAL_GM(c))
                return send_txt(c, "%s", __(c, "\tE\tC7Server-side drops not\n"
                                            "suported on this ship for\n"
                                            "this client version."));
            break;

        case CLIENT_VERSION_EP3:
            return send_txt(c, "%s", __(c, "\tE\tC7Not valid on Episode 3."));

        case CLIENT_VERSION_BB:
            /* Not valid for Blue Burst (they always have server-side drops) */
            return send_txt(c, "%s", __(c, "\tE\tC7Not valid on Blue Burst."));
    }

    /* See if we're turning the flag off. */
    if(!strcmp(params, "off")) {
        c->flags &= ~CLIENT_FLAG_SERVER_DROPS;
        return send_txt(c, "%s", __(c, "\tE\tC7Server drops off\n"
                                    "for any new teams."));
    }

    c->flags |= CLIENT_FLAG_SERVER_DROPS;
    return send_txt(c, "%s", __(c, "\tE\tC7Server drops on\n"
                                "for any new teams."));
}

/* Usage: /gcprotect [off] */
static int handle_gcprotect(ship_client_t *c, const char *params) {
    uint8_t enable = 1;

    /* Make sure they're logged in */
    if(!(c->flags & CLIENT_FLAG_LOGGED_IN)) {
        return send_txt(c, "%s", __(c, "\tE\tC7You must be logged in to "
                                    "use this command."));
    }

    /* See if we're turning the flag off. */
    if(!strcmp(params, "off")) {
        enable = 0;
        shipgate_send_user_opt(&ship->sg, c->guildcard, c->cur_block->b,
                               USER_OPT_GC_PROTECT, 1, &enable);
        return send_txt(c, "%s", __(c, "\tE\tC7Guildcard protection "
                                    "disabled."));
    }

    /* Send the message to the shipgate */
    shipgate_send_user_opt(&ship->sg, c->guildcard, c->cur_block->b,
                           USER_OPT_GC_PROTECT, 1, &enable);
    return send_txt(c, "%s", __(c, "\tE\tC7Guildcard protection enabled."));
}

/* Usage: /trackinv */
static int handle_trackinv(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;

    /* Make sure the requester is a local GM, at least. */
    if(!LOCAL_GM(c))
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));

    /* Make sure that the requester is in a plain old lobby. */
    if(l->type != LOBBY_TYPE_DEFAULT)
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid in a game."));

    /* Set the flag... */
    c->flags |= CLIENT_FLAG_TRACK_INVENTORY;

    return send_txt(c, "%s", __(c, "\tE\tC7Flag set."));
}

/* Usage /trackkill [off] */
static int handle_trackkill(ship_client_t *c, const char *params) {
    uint8_t enable = 1;

    /* Make sure they're logged in */
    if(!(c->flags & CLIENT_FLAG_LOGGED_IN)) {
        return send_txt(c, "%s", __(c, "\tE\tC7You must be logged in to "
                                       "use this command."));
    }

    /* See if we're turning the flag off. */
    if(!strcmp(params, "off")) {
        enable = 0;
        shipgate_send_user_opt(&ship->sg, c->guildcard, c->cur_block->b,
                               USER_OPT_TRACK_KILLS, 1, &enable);
        c->flags &= ~CLIENT_FLAG_TRACK_KILLS;
        return send_txt(c, "%s", __(c, "\tE\tC7Kill tracking disabled."));
    }

    /* Send the message to the shipgate */
    shipgate_send_user_opt(&ship->sg, c->guildcard, c->cur_block->b,
                           USER_OPT_TRACK_KILLS, 1, &enable);
    c->flags |= CLIENT_FLAG_TRACK_KILLS;
    return send_txt(c, "%s", __(c, "\tE\tC7Kill tracking enabled."));
}

/* Usage: /ep3music value */
static int handle_ep3music(ship_client_t *c, const char *params) {
    uint32_t song;
    lobby_t *l = c->cur_lobby;
    uint8_t rawpkt[12] = { 0 };
    subcmd_pkt_t *pkt = (subcmd_pkt_t *)rawpkt;

    /* Make sure the requester is a local GM, at least. */
    if(!LOCAL_GM(c))
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));

    /* Make sure that the requester is in a lobby, not a team */
    if(l->type != LOBBY_TYPE_DEFAULT)
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid in a game."));

    /* Figure out the level requested */
    if(!params || !strlen(params))
        return send_txt(c, "%s", __(c, "\tE\tC7Must specify song."));

    errno = 0;
    song = (uint32_t)strtoul(params, NULL, 10);

    if(errno != 0)
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid song."));

    /* Prepare the packet. */
    pkt->hdr.dc.pkt_type = GAME_COMMAND0_TYPE;
    pkt->hdr.dc.flags = 0;
    pkt->hdr.dc.pkt_len = LE16(0x000C);
    pkt->type = SUBCMD_JUKEBOX;
    pkt->size = 2;
    pkt->data[2] = (uint8_t)song;

    /* Send it. */
    subcmd_send_lobby_dc(l, NULL, pkt, 0);
    return 0;
}

/* Usage: /tlogin username token */
static int handle_tlogin(ship_client_t *c, const char *params) {
    char username[32], token[32];
    int len = 0;
    const char *ch = params;

    /* Make sure the user isn't doing something stupid. */
    if(!*params) {
        return send_txt(c, "%s", __(c, "\tE\tC7You must specify\n"
                                    "your username and\n"
                                    "website token."));
    }

    /* Copy over the username/password. */
    while(*ch != ' ' && len < 32) {
        username[len++] = *ch++;
    }

    if(len == 32)
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid username."));

    username[len] = '\0';

    len = 0;
    ++ch;

    while(*ch != ' ' && *ch != '\0' && len < 32) {
        token[len++] = *ch++;
    }

    if(len == 32)
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid token."));

    token[len] = '\0';

    /* We'll get success/failure later from the shipgate. */
    return shipgate_send_usrlogin(&ship->sg, c->guildcard, c->cur_block->b,
                                  username, token, 1);
}

/* Usage: /dsdrops version difficulty section episode */
static int handle_dsdrops(ship_client_t *c, const char *params) {
#ifdef DEBUG
    uint8_t ver, diff, section, ep;
    char *sver, *sdiff, *ssection, *sep;
    char *tok, *str;
#endif

    /* Make sure the requester is a local GM, at least. */
    if(!LOCAL_GM(c))
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));

    if(c->version == CLIENT_VERSION_BB)
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid on Blue Burst."));

#ifndef DEBUG
    return send_txt(c, "%s", __(c, "\tE\tC7Debug support not compiled in."));
#else
    if(!(str = strdup(params)))
        return send_txt(c, "%s", __(c, "\tE\tC7Internal server error."));

    /* Grab each token we're expecting... */
    sver = strtok_r(str, " ,", &tok);
    sdiff = strtok_r(NULL, " ,", &tok);
    ssection = strtok_r(NULL, " ,", &tok);
    sep = strtok_r(NULL, " ,", &tok);

    if(!ssection || !sdiff || !sver || !sep) {
        free(str);
        return send_txt(c, "%s", __(c, "\tE\tC7Missing argument."));
    }

    /* Parse the arguments */
    if(!strcmp(sver, "v2")) {
        if(!pt_v2_enabled() || !map_have_v2_maps() || !pmt_v2_enabled() ||
           !rt_v2_enabled()) {
            free(str);
            return send_txt(c, "%s", __(c, "\tE\tC7Server-side drops not\n"
                                        "suported on this ship for\n"
                                        "this client version."));
        }

        ver = CLIENT_VERSION_DCV2;
    }
    else if(!strcmp(sver, "gc")) {
        if(!pt_gc_enabled() || !map_have_gc_maps() || !pmt_gc_enabled() ||
           !rt_gc_enabled()) {
            free(str);
            return send_txt(c, "%s", __(c, "\tE\tC7Server-side drops not\n"
                                        "suported on this ship for\n"
                                        "this client version."));
        }
        ver = CLIENT_VERSION_GC;
    }
    else {
        free(str);
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid version."));
    }

    errno = 0;
    diff = (uint8_t)strtoul(sdiff, NULL, 10);

    if(errno || diff > 3) {
        free(str);
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid difficulty."));
    }

    section = (uint8_t)strtoul(ssection, NULL, 10);

    if(errno || section > 9) {
        free(str);
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid section ID."));
    }

    ep = (uint8_t)strtoul(sep, NULL, 10);

    if(errno || (ep != 1 && ep != 2) ||
       (ver == CLIENT_VERSION_DCV2 && ep != 1)) {
        free(str);
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid episode."));
    }

    /* We got everything, save it in the client structure and we're done. */
    c->sdrops_ver = ver;
    c->sdrops_diff = diff;
    c->sdrops_section = section;
    c->sdrops_ep = ep;
    c->flags |= CLIENT_FLAG_DBG_SDROPS;
    free(str);

    return send_txt(c, "%s", __(c, "\tE\tC7Enabled server-side drop debug."));
#endif
}

/* Usage: /noevent */
static int handle_noevent(ship_client_t *c, const char *params) {
    if(c->version < CLIENT_VERSION_GC)
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid on this version."));

    /* Make sure that the requester is in a lobby, not a game */
    if(c->cur_lobby->type != LOBBY_TYPE_DEFAULT)
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid in a game."));

    return send_simple(c, LOBBY_EVENT_TYPE, LOBBY_EVENT_NONE);
}

/* Usage: /lflags */
static int handle_lflags(ship_client_t *c, const char *params) {
    if(!LOCAL_GM(c))
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));

    return send_txt(c, "\tE\tC7%08x", c->cur_lobby->flags);
}

/* Usage: /cflags */
static int handle_cflags(ship_client_t *c, const char *params) {
    if(!LOCAL_GM(c))
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));

    return send_txt(c, "\tE\tC7%08x", c->flags);
}

/* Usage: /showpos */
static int handle_showpos(ship_client_t *c, const char *params) {
    return send_txt(c, "\tE\tC7(%f, %f, %f)", c->x, c->y, c->z);
}

/* Usage: /t x, y, z */
static int handle_t(ship_client_t *c, const char *params) {
    char *str, *tok, *xs, *ys, *zs;
    float x, y, z;
    subcmd_teleport_t p2;
    lobby_t *l = c->cur_lobby;

    if(!LOCAL_GM(c))
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));

    if(!(str = strdup(params)))
        return send_txt(c, "%s", __(c, "\tE\tC7Internal server error."));

    /* Grab each token we're expecting... */
    xs = strtok_r(str, " ,", &tok);
    ys = strtok_r(NULL, " ,", &tok);
    zs = strtok_r(NULL, " ,", &tok);

    if(!xs || !ys || !zs) {
        free(str);
        return send_txt(c, "%s", __(c, "\tE\tC7Missing argument."));
    }

    /* Parse out the numerical values... */
    errno = 0;
    x = strtof(xs, NULL);
    y = strtof(ys, NULL);
    z = strtof(zs, NULL);
    free(str);

    /* Did they all parse ok? */
    if(errno)
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid coordinate."));

    /* Hopefully they know what they're doing... Send the packet. */
    p2.hdr.pkt_type = GAME_COMMAND0_TYPE;
    p2.hdr.pkt_len = sizeof(subcmd_teleport_t);
    p2.hdr.flags = 0;
    p2.type = SUBCMD_TELEPORT;
    p2.size = 5;
    p2.client_id = c->client_id;
    p2.unused = 0;
    p2.x = x;
    p2.y = y;
    p2.z = z;
    p2.w = c->w;

    c->x = x;
    c->y = y;
    c->z = z;

    /* Send the packet to everyone in the lobby */
    return lobby_send_pkt_dc(l, NULL, (dc_pkt_hdr_t *)&p2, 0);
}

/* Usage: /info */
static int handle_info(ship_client_t *c, const char *params) {
    /* Don't let certain versions even try... */
    if(c->version == CLIENT_VERSION_EP3)
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid on Episode III."));
    else if(c->version == CLIENT_VERSION_BB)
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid on Blue Burst."));
    else if(c->version == CLIENT_VERSION_DCV1 &&
            (c->flags & CLIENT_FLAG_IS_NTE))
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid on DC NTE."));
    else if((c->version == CLIENT_VERSION_GC ||
             c->version == CLIENT_VERSION_XBOX) &&
            !(c->flags & CLIENT_FLAG_GC_MSG_BOXES))
        return send_txt(c, "%s", __(c, "\tE\tC7Not valid on this version."));

    return send_info_list(c, ship);
}

/* Usage: /quest id */
static int handle_quest(ship_client_t *c, const char *params) {
    char *str, *tok, *qid;
    lobby_t *l = c->cur_lobby;
    uint32_t quest_id;
    int rv;

    if(!LOCAL_GM(c))
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type != LOBBY_TYPE_GAME)
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game."));

    if(l->flags & LOBBY_FLAG_BURSTING)
        return send_txt(c, "%s", __(c, "\tE\tC4Please wait a moment."));

    if(!(str = strdup(params)))
        return send_txt(c, "%s", __(c, "\tE\tC7Internal server error."));

    /* Grab each token we're expecting... */
    qid = strtok_r(str, " ,", &tok);

    /* Attempt to parse out the quest id... */
    errno = 0;
    quest_id = (uint32_t)strtoul(qid, NULL, 0);

    if(errno) {
        free(str);
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid quest id."));
    }

    /* Done with this... */
    free(str);

    pthread_rwlock_rdlock(&ship->qlock);

    /* Do we have quests configured? */
    if(!TAILQ_EMPTY(&ship->qmap)) {
        /* Find the quest first, since someone might be doing something
           stupid... */
           quest_map_elem_t *e = quest_lookup(&ship->qmap, quest_id);

           /* If the quest isn't found, bail out now. */
           if(!e) {
               rv = send_txt(c, __(c, "\tE\tC7Invalid quest id."));
               pthread_rwlock_unlock(&ship->qlock);
               return rv;
           }

           rv = lobby_setup_quest(l, c, quest_id, CLIENT_LANG_ENGLISH);
    }
    else {
        rv = send_txt(c, "%s", __(c, "\tE\tC4Quests not\nconfigured."));
    }

    pthread_rwlock_unlock(&ship->qlock);

    return rv;
}

/* Usage /autolegit [off] */
static int handle_autolegit(ship_client_t *c, const char *params) {
    uint8_t enable = 1;
    sylverant_limits_t *limits;

    /* Make sure they're logged in */
    if(!(c->flags & CLIENT_FLAG_LOGGED_IN)) {
        return send_txt(c, "%s", __(c, "\tE\tC7You must be logged in to "
                                       "use this command."));
    }

    /* See if we're turning the flag off. */
    if(!strcmp(params, "off")) {
        enable = 0;
        shipgate_send_user_opt(&ship->sg, c->guildcard, c->cur_block->b,
                               USER_OPT_LEGIT_ALWAYS, 1, &enable);
        c->flags &= ~CLIENT_FLAG_ALWAYS_LEGIT;
        return send_txt(c, "%s", __(c, "\tE\tC7Automatic legit\n"
                                       "mode disabled."));
    }

    /* Send the message to the shipgate */
    shipgate_send_user_opt(&ship->sg, c->guildcard, c->cur_block->b,
                           USER_OPT_LEGIT_ALWAYS, 1, &enable);
    c->flags |= CLIENT_FLAG_ALWAYS_LEGIT;
    send_txt(c, "%s", __(c, "\tE\tC7Automatic legit\nmode enabled."));

    /* Check them now, since they shouldn't have to re-connect to get the
       flag set... */
    pthread_rwlock_rdlock(&ship->llock);
    if(!ship->def_limits) {
        pthread_rwlock_unlock(&ship->llock);
        c->flags &= ~CLIENT_FLAG_ALWAYS_LEGIT;
        send_txt(c, "%s", __(c, "\tE\tC7Legit mode not\n"
                                "available on this\n"
                                "ship."));
        return 0;
    }

    limits = ship->def_limits;

    if(client_legit_check(c, limits)) {
        c->flags &= ~CLIENT_FLAG_ALWAYS_LEGIT;
        send_txt(c, "%s", __(c, "\tE\tC7You failed the legit "
                                "check."));
    }
    else {
        /* Set the flag and retain the limits list on the client. */
        c->flags |= CLIENT_FLAG_LEGIT;
        c->limits = retain(limits);
        send_txt(c, "%s", __(c, "\tE\tC7Legit check passed."));
    }

    pthread_rwlock_unlock(&ship->llock);
    return 0;
}

/* Usage: /censor [off] */
static int handle_censor(ship_client_t *c, const char *params) {
    uint8_t enable = 1;
    pthread_mutex_lock(&c->mutex);

    /* See if we're turning the flag off. */
    if(!strcmp(params, "off")) {
        c->flags &= ~CLIENT_FLAG_WORD_CENSOR;
        enable = 0;
        shipgate_send_user_opt(&ship->sg, c->guildcard, c->cur_block->b,
                               USER_OPT_WORD_CENSOR, 1, &enable);

        pthread_mutex_unlock(&c->mutex);

        return send_txt(c, "%s", __(c, "\tE\tC7Word censor off."));
    }

    /* Set the flag since we're turning it on. */
    c->flags |= CLIENT_FLAG_WORD_CENSOR;

    /* Send the message to the shipgate */
    shipgate_send_user_opt(&ship->sg, c->guildcard, c->cur_block->b,
                           USER_OPT_WORD_CENSOR, 1, &enable);

    pthread_mutex_unlock(&c->mutex);
    return send_txt(c, "%s", __(c, "\tE\tC7Word censor on."));
}

/* Usage: /teamlog */
static int handle_teamlog(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;
    int rv;

    /* Make sure the requester is a local GM, at least. */
    if(!LOCAL_GM(c))
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type != LOBBY_TYPE_GAME)
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a team."));

    rv = team_log_start(l);

    if(!rv) {
        return send_txt(c, "%s", __(c, "\tE\tC7Logging started."));
    }
    else if(rv == -1) {
        return send_txt(c, "%s", __(c, "\tE\tC7The team is already\n"
                                    "being logged."));
    }
    else {
        return send_txt(c, "%s", __(c, "\tE\tC7Cannot create log file."));
    }
}

/* Usage: /eteamlog */
static int handle_eteamlog(ship_client_t *c, const char *params) {
    lobby_t *l = c->cur_lobby;
    int rv;

    /* Make sure the requester is a local GM, at least. */
    if(!LOCAL_GM(c))
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));

    /* Make sure that the requester is in a team, not a lobby. */
    if(l->type != LOBBY_TYPE_GAME)
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a team."));

    rv = team_log_stop(l);

    if(!rv) {
        return send_txt(c, "%s", __(c, "\tE\tC7Logging ended."));
    }
    else {
        return send_txt(c, "%s", __(c,"\tE\tC7The team is not\n"
                                    "being logged."));
    }
}

/* Usage: /ib days ip reason */
static int handle_ib(ship_client_t *c, const char *params) {
    struct sockaddr_storage addr, netmask;
    struct sockaddr_in *ip = (struct sockaddr_in *)&addr;
    struct sockaddr_in *nm = (struct sockaddr_in *)&netmask;
    block_t *b;
    ship_client_t *i;
    char *slen, *sip, *reason;
    char *str, *tok;
    int j;
    uint32_t len;

    /* Make sure the requester is a local GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    if(!(str = strdup(params))) {
        return send_txt(c, "%s", __(c, "\tE\tC7Internal server error."));
    }

    /* Grab each token we're expecting... */
    slen = strtok_r(str, " ,", &tok);
    sip = strtok_r(NULL, " ,", &tok);
    reason = strtok_r(NULL, "", &tok);

    /* Figure out the user requested */
    errno = 0;
    len = (uint32_t)strtoul(slen, NULL, 10);

    if(errno != 0) {
        /* Send a message saying invalid length */
        free(str);
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid ban length."));
    }

    /* Parse the IP address. We only support IPv4 here. */
    if(!my_pton(AF_INET, sip, &addr)) {
        free(str);
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid IP address."));
    }

    nm->sin_addr.s_addr = 0xFFFFFFFF;
    addr.ss_family = netmask.ss_family = AF_INET;

    /* Set the ban in the list (86,400s = 1 day) */
    if(ban_ip(ship, time(NULL) + 86400 * len, c->guildcard, &addr, &netmask,
              reason)) {
        free(str);
        return send_txt(c, "%s", __(c, "\tE\tC7Error setting ban."));
    }

    /* Look for the requested user and kick them if they're on the ship. */
    for(j = 0; j < ship->cfg->blocks; ++j) {
        if((b = ship->blocks[j])) {
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(i, b->clients, qentry) {
                /* Disconnect them if we find them */
                nm = (struct sockaddr_in *)&i->ip_addr;
                if(nm->sin_family == AF_INET &&
                   ip->sin_addr.s_addr == nm->sin_addr.s_addr) {
                    if(reason && strlen(reason)) {
                        send_message_box(i, "%s\n%s\n%s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship."), __(i, "Reason:"),
                                         reason);
                    }
                    else {
                        send_message_box(i, "%s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship."));
                    }

                    i->flags |= CLIENT_FLAG_DISCONNECTED;
                }
            }

            pthread_rwlock_unlock(&b->lock);
        }
    }

    free(str);
    return send_txt(c, "%s", __(c, "\tE\tC7Successfully set ban."));
}

static command_t cmds[] = {
    { "warp"     , handle_warp      },
    { "kill"     , handle_kill      },
    { "minlvl"   , handle_min_level },
    { "maxlvl"   , handle_max_level },
    { "refresh"  , handle_refresh   },
    { "save"     , handle_save      },
    { "restore"  , handle_restore   },
    { "bstat"    , handle_bstat     },
    { "bcast"    , handle_bcast     },
    { "arrow"    , handle_arrow     },
    { "login"    , handle_login     },
    { "item"     , handle_item      },
    { "item4"    , handle_item4     },
    { "event"    , handle_event     },
    { "passwd"   , handle_passwd    },
    { "lname"    , handle_lname     },
    { "warpall"  , handle_warpall   },
    { "bug"      , handle_bug       },
    { "clinfo"   , handle_clinfo    },
    { "gban:d"   , handle_gban_d    },
    { "gban:w"   , handle_gban_w    },
    { "gban:m"   , handle_gban_m    },
    { "gban:p"   , handle_gban_p    },
    { "list"     , handle_list      },
    { "legit"    , handle_legit     },
    { "normal"   , handle_normal    },
    { "shutdown" , handle_shutdown  },
    { "log"      , handle_log       },
    { "endlog"   , handle_endlog    },
    { "motd"     , handle_motd      },
    { "friendadd", handle_friendadd },
    { "frienddel", handle_frienddel },
    { "dconly"   , handle_dconly    },
    { "v1only"   , handle_v1only    },
    { "forgegc"  , handle_forgegc   },
    { "invuln"   , handle_invuln    },
    { "inftp"    , handle_inftp     },
    { "smite"    , handle_smite     },
    { "makeitem" , handle_makeitem  },
    { "teleport" , handle_teleport  },
    { "dumpinv"  , handle_dumpinv   },
    { "showdcpc" , handle_showdcpc  },
    { "allowgc"  , handle_allowgc   },
    { "ws"       , handle_ws        },
    { "ll"       , handle_ll        },
    { "npc"      , handle_npc       },
    { "stfu"     , handle_stfu      },
    { "unstfu"   , handle_unstfu    },
    { "ignore"   , handle_ignore    },
    { "unignore" , handle_unignore  },
    { "quit"     , handle_quit      },
    { "gameevent", handle_gameevent },
    { "ban:d"    , handle_ban_d     },
    { "ban:w"    , handle_ban_w     },
    { "ban:m"    , handle_ban_m     },
    { "ban:p"    , handle_ban_p     },
    { "unban"    , handle_unban     },
    { "cc"       , handle_cc        },
    { "qlang"    , handle_qlang     },
    { "friends"  , handle_friends   },
    { "gbc"      , handle_gbc       },
    { "logout"   , handle_logout    },
    { "override" , handle_override  },
    { "ver"      , handle_ver       },
    { "restart"  , handle_restart   },
    { "search"   , handle_search    },
    { "gm"       , handle_gm        },
    { "maps"     , handle_maps      },
    { "showmaps" , handle_showmaps  },
    { "restorebk", handle_restorebk },
    { "enablebk" , handle_enablebk  },
    { "disablebk", handle_disablebk },
    { "exp"      , handle_exp       },
    { "level"    , handle_level     },
    { "sdrops"   , handle_sdrops    },
    { "gcprotect", handle_gcprotect },
    { "trackinv" , handle_trackinv  },
    { "trackkill", handle_trackkill },
    { "ep3music" , handle_ep3music  },
    { "tlogin"   , handle_tlogin    },
    { "dsdrops"  , handle_dsdrops   },
    { "noevent"  , handle_noevent   },
    { "lflags"   , handle_lflags    },
    { "cflags"   , handle_cflags    },
    { "stalk"    , handle_teleport  },    /* Happy, Aleron Ives? */
    { "showpos"  , handle_showpos   },
    { "t"        , handle_t         },    /* Short command = more precision. */
    { "info"     , handle_info      },
    { "quest"    , handle_quest     },
    { "autolegit", handle_autolegit },
    { "censor"   , handle_censor    },
    { "teamlog"  , handle_teamlog   },
    { "eteamlog" , handle_eteamlog  },
    { "ib"       , handle_ib        },
    { ""         , NULL             }     /* End marker -- DO NOT DELETE */
};

static int command_call(ship_client_t *c, const char *txt, size_t len) {
    command_t *i = &cmds[0];
    char cmd[10], params[len];
    const char *ch = txt + 3;           /* Skip the language code and '/'. */
    int clen = 0;

    /* Figure out what the command the user has requested is */
    while(*ch != ' ' && clen < 9 && *ch) {
        cmd[clen++] = *ch++;
    }

    cmd[clen] = '\0';

    /* Copy the params out for safety... */
    if(!*ch) {
        memset(params, 0, len);
    }
    else {
        strcpy(params, ch + 1);
    }

    /* Look through the list for the one we want */
    while(i->hnd) {
        /* If this is it, go ahead and handle it */
        if(!strcmp(cmd, i->trigger)) {
            return i->hnd(c, params);
        }

        i++;
    }

    /* Make sure a script isn't set up to respond to the user's command... */
    if(!script_execute(ScriptActionUnknownCommand, c, SCRIPT_ARG_PTR, c,
                       SCRIPT_ARG_CSTRING, cmd, SCRIPT_ARG_CSTRING, params,
                       SCRIPT_ARG_END)) {
        /* Send the user a message saying invalid command. */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Command."));
    }

    return 0;
}

int command_parse(ship_client_t *c, dc_chat_pkt *pkt) {
    int len = LE16(pkt->hdr.dc.pkt_len), tlen = len - 12;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;
    char buf[tlen * 2];

    /* Convert the text to UTF-8. */
    in = tlen;
    out = tlen * 2;
    inptr = (ICONV_CONST char *)pkt->msg;
    outptr = buf;

    if(pkt->msg[0] == '\t' && pkt->msg[1] == 'J') {
        iconv(ic_sjis_to_utf8, &inptr, &in, &outptr, &out);
    }
    else {
        iconv(ic_8859_to_utf8, &inptr, &in, &outptr, &out);
    }

    /* Handle the command... */
    return command_call(c, buf, (tlen * 2) - out);
}

int wcommand_parse(ship_client_t *c, dc_chat_pkt *pkt) {
    int len = LE16(pkt->hdr.dc.pkt_len), tlen = len - 12;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;
    char buf[tlen * 2];

    /* Convert the text to UTF-8. */
    in = tlen;
    out = tlen * 2;
    inptr = (ICONV_CONST char *)pkt->msg;
    outptr = buf;
    iconv(ic_utf16_to_utf8, &inptr, &in, &outptr, &out);

    /* Handle the command... */
    return command_call(c, buf, (tlen * 2) - out);
}

int bbcommand_parse(ship_client_t *c, bb_chat_pkt *pkt) {
    int len = LE16(pkt->hdr.pkt_len), tlen = len - 16;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;
    char buf[tlen * 2];

    /* Convert the text to UTF-8. */
    in = tlen;
    out = tlen * 2;
    inptr = (ICONV_CONST char *)pkt->msg;
    outptr = buf;
    iconv(ic_utf16_to_utf8, &inptr, &in, &outptr, &out);

    /* Handle the command... */
    return command_call(c, buf, (tlen * 2) - out);
}
