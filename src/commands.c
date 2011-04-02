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
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <iconv.h>
#include <time.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <sylverant/debug.h>

#include "ship_packets.h"
#include "lobby.h"
#include "subcmd.h"
#include "utils.h"
#include "shipgate.h"
#include "items.h"
#include "bans.h"

/* Some macros for commonly used privilege checks. */
#define LOCAL_GM(c) \
    ((c->privilege & CLIENT_PRIV_LOCAL_GM) && \
     (c->flags & CLIENT_FLAG_LOGGED_IN))

#define GLOBAL_GM(c) \
    ((c->privilege & CLIENT_PRIV_GLOBAL_GM) && \
     (c->flags & CLIENT_FLAG_LOGGED_IN))

#define LOCAL_ROOT(c) \
    ((c->privilege & CLIENT_PRIV_LOCAL_ROOT) && \
     (c->flags & CLIENT_FLAG_LOGGED_IN))

#define GLOBAL_ROOT(c) \
    ((c->privilege & CLIENT_PRIV_GLOBAL_ROOT) && \
     (c->flags & CLIENT_FLAG_LOGGED_IN))

extern int handle_dc_gcsend(ship_client_t *d, subcmd_dc_gcsend_t *pkt);

typedef struct command {
    char trigger[10];
    int (*hnd)(ship_client_t *c, dc_chat_pkt *pkt, char *params);
} command_t;

/* Usage: /warp area */
static int handle_warp(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    unsigned long area;
    lobby_t *l = c->cur_lobby;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game lobby."));
    }

    /* Figure out the floor requested */
    errno = 0;
    area = strtoul(params, NULL, 10);

    if(errno) {
        /* Send a message saying invalid area */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid area!"));
    }

    if(area > 17) {
        /* Area too large, give up */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid area!"));
    }

    /* Send the person to the requested place */
    return send_warp(c, (uint8_t)area);
}

/* Usage: /warpall area */
static int handle_warpall(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    unsigned long area;
    lobby_t *l = c->cur_lobby;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game lobby."));
    }

    /* Figure out the floor requested */
    errno = 0;
    area = strtoul(params, NULL, 10);

    if(errno) {
        /* Send a message saying invalid area */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid area!"));
    }

    if(area > 17) {
        /* Area too large, give up */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid area!"));
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

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Look for the requested user (only on this block) */
    TAILQ_FOREACH(i, b->clients, qentry) {
        /* Disconnect them if we find them */
        if(i->guildcard == gc) {
            if(c->privilege <= i->privilege) {
                return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
            }

            if(strlen(reason) > 1) {
                send_message_box(i, "%s\n\n%s\n%s",
                                 __(i, "\tEYou have been kicked by a GM."),
                                 __(i, "Reason:"), reason + 1);
            }
            else {
                send_message_box(i, "%s",
                                 __(i, "\tEYou have been kicked by a GM."));
            }

            i->flags |= CLIENT_FLAG_DISCONNECTED;
            return 0;
        }
    }

    /* If the requester is a global GM, forward the request to the shipgate,
       since it wasn't able to be done on this block (the shipgate may well
       send the packet back to us if the person is on another block on this
       ship). */
    if(GLOBAL_GM(c)) {
        if(strlen(reason) > 1) {
            shipgate_send_kick(&c->cur_ship->sg, c->guildcard, gc, reason + 1);
        }
        else {
            shipgate_send_kick(&c->cur_ship->sg, c->guildcard, gc, NULL);
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
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game lobby."));
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid level value"));
    }

    /* Make sure the requested level is greater than or equal to the value for
       the game's difficulty. */
    if(lvl < game_required_level[l->difficulty]) {
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Invalid level for this difficulty."));
    }

    /* Make sure the requested level is less than or equal to the game's maximum
       level. */
    if(lvl > l->max_level + 1) {
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Minimum level must be <= maximum."));
    }

    /* Set the value in the structure, and be on our way. */
    l->min_level = lvl - 1;

    return send_txt(c, "%s", __(c, "\tE\tC7Minimum level set."));
}

/* Usage: /maxlvl level */
static int handle_max_level(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    int lvl;
    lobby_t *l = c->cur_lobby;

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game lobby."));
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid level value"));
    }

    /* Make sure the requested level is greater than or equal to the value for
       the game's minimum level. */
    if(lvl < l->min_level + 1) {
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Maximum level must be >= minimum."));
    }
    
    /* Set the value in the structure, and be on our way. */
    l->max_level = lvl - 1;

    return send_txt(c, "%s", __(c, "\tE\tC7Maximum level set."));
}

/* Usage: /refresh [quests or gms] */
static int handle_refresh(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    ship_t *s = c->cur_ship;
    sylverant_quest_list_t quests;
    sylverant_quest_list_t qlist[CLIENT_VERSION_COUNT][CLIENT_LANG_COUNT];
    quest_map_t qmap;
    int i, j;
    char fn[512];

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    if(!strcmp(params, "quests")) {
        if(s->cfg->quests_file && s->cfg->quests_file[0]) {
            if(sylverant_quests_read(s->cfg->quests_file, &quests)) {
                debug(DBG_ERROR, "%s: Couldn't read quests file!\n",
                      s->cfg->name);
                return send_txt(c, "%s",
                                __(c, "\tE\tC7Couldn't read quests file!"));
            }

            /* Lock the mutex to prevent anyone from trying anything funny. */
            pthread_rwlock_wrlock(&s->qlock);

            /* Out with the old, and in with the new. */
            sylverant_quests_destroy(&s->quests);
            s->quests = quests;

            /* Unlock the lock, we're done. */
            pthread_rwlock_unlock(&s->qlock);
            return send_txt(c, "%s", __(c, "\tE\tC7Updated quest list"));
        }
        else if(s->cfg->quests_dir && s->cfg->quests_dir[0]) {
            /* Read in the new quests first */
            TAILQ_INIT(&qmap);

            for(i = 0; i < CLIENT_VERSION_COUNT; ++i) {
                for(j = 0; j < CLIENT_LANG_COUNT; ++j) {
                    sprintf(fn, "%s/%s-%s/quests.xml", s->cfg->quests_dir,
                            version_codes[i], language_codes[j]);
                    if(!sylverant_quests_read(fn, &qlist[i][j])) {
                        if(!quest_map(&qmap, &qlist[i][j], i, j)) { 
                            debug(DBG_LOG, "Read quests for %s-%s\n",
                                  version_codes[i], language_codes[j]);
                        }
                        else {
                            debug(DBG_LOG, "Unable to map quests for %s-%s\n",
                                  version_codes[i], language_codes[j]);
                            sylverant_quests_destroy(&qlist[i][j]);
                        }
                    }
                }
            }

            /* Lock the mutex to prevent anyone from trying anything funny. */
            pthread_rwlock_wrlock(&s->qlock);
            
            /* Out with the old, and in with the new. */
            for(i = 0; i < CLIENT_VERSION_COUNT; ++i) {
                for(j = 0; j < CLIENT_LANG_COUNT; ++j) {
                    sylverant_quests_destroy(&s->qlist[i][j]);
                    s->qlist[i][j] = qlist[i][j];
                }
            }

            quest_cleanup(&s->qmap);
            s->qmap = qmap;
            
            /* Unlock the lock, we're done. */
            pthread_rwlock_unlock(&s->qlock);
            return send_txt(c, "%s", __(c, "\tE\tC7Updated quest list"));
        }
        else {
            return send_txt(c, "%s",
                            __(c, "\tE\tC7No configured quests list!"));
        }
    }
    else if(!strcmp(params, "gms")) {
        /* Make sure the requester is a local root. */
        if(!LOCAL_ROOT(c)) {
            return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
        }

        if(s->cfg->gm_file[0]) {
            /* Try to read the GM file. This will clean out the old list as
               well, if needed. */
            if(gm_list_read(s->cfg->gm_file, s)) {
                return send_txt(c, "%s",
                                __(c, "\tE\tC7Couldn't read GM list!"));
            }

            return send_txt(c, "%s", __(c, "\tE\tC7Updated GMs list"));
        }
        else {
            return send_txt(c, "%s", __(c, "\tE\tC7No configured GM list!"));
        }
    }
    else {
        return send_txt(c, "%s", __(c, "\tE\tC7Unknown item to refresh"));
    }
}

/* Usage: /save slot */
static int handle_save(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;
    uint32_t slot;

    /* Make sure that the requester is in a lobby lobby, not a game lobby */
    if(l->type != LOBBY_TYPE_DEFAULT) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a "
                                    "non-game lobby."));
    }

    /* Figure out the slot requested */
    errno = 0;
    slot = (uint32_t)strtoul(params, NULL, 10);

    if(errno || slot > 4 || slot < 1) {
        /* Send a message saying invalid slot */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid slot value"));
    }

    /* Adjust so we don't go into the Blue Burst character data */
    slot += 4;

    /* Send the character data to the shipgate */
    if(shipgate_send_cdata(&c->cur_ship->sg, c->guildcard, slot, c->pl)) {
        /* Send a message saying we couldn't save */
        return send_txt(c, "%s", __(c, "\tE\tC7Couldn't save character data"));
    }

    /* An error or success message will be sent when the shipgate gets its
       response. */
    return 0;
}

/* Usage: /restore slot */
static int handle_restore(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;
    uint32_t slot;

    /* Make sure that the requester is in a lobby lobby, not a game lobby */
    if(l->type != LOBBY_TYPE_DEFAULT) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a "
                                    "non-game lobby."));
    }

    /* Figure out the slot requested */
    errno = 0;
    slot = (uint32_t)strtoul(params, NULL, 10);

    if(errno || slot > 4 || slot < 1) {
        /* Send a message saying invalid slot */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid slot value"));
    }

    /* Adjust so we don't go into the Blue Burst character data */
    slot += 4;

    /* Send the request to the shipgate. */
    if(shipgate_send_creq(&c->cur_ship->sg, c->guildcard, slot)) {
        /* Send a message saying we couldn't request */
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Couldn't request character data"));
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

        if(i->type != LOBBY_TYPE_DEFAULT) {
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
    return send_txt(c, "\tE\tC7BLOCK%02d:\n%d %s\n%d %s", b->b,
                    players, __(c, "Users"), games, __(c, "Teams"));
}

/* Usage /bcast message */
static int handle_bcast(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    ship_t *s = c->cur_ship;
    block_t *b;
    int i;
    ship_client_t *i2;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Go through each block and send the message to anyone that is alive. */
    for(i = 0; i < s->cfg->blocks; ++i) {
        b = s->blocks[i];

        if(b && b->run) {
            pthread_mutex_lock(&b->mutex);

            /* Send the message to each player. */
            TAILQ_FOREACH(i2, b->clients, qentry) {
                pthread_mutex_lock(&i2->mutex);

                if(i2->pl) {
                    send_txt(i2, "%s\n%s", __(i2, "\tE\tC7Global Message:"),
                             params);
                }

                pthread_mutex_unlock(&i2->mutex);
            }

            pthread_mutex_unlock(&b->mutex);
        }
    }

    return 0;
}

/* Usage /arrow color_number */
static int handle_arrow(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    int i;

    /* Set the arrow color and send the packet to the lobby. */
    i = atoi(params);
    c->arrow = i;

    send_txt(c, "%s", __(c, "\tE\tC7Arrow set"));

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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid request"));
    }

    username[len] = '\0';

    len = 0;
    ++ch;

    while(*ch != ' ' && *ch != '\0' && len < 32) {
        password[len++] = *ch++;
    }

    if(len == 32) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid request"));
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
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Copy over the item data. */
    count = sscanf(params, "%x,%x,%x,%x", item + 0, item + 1, item + 2,
                   item + 3);

    if(count == EOF || count == 0) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid item code"));
    }

    c->next_item[0] = item[0];
    c->next_item[1] = item[1];
    c->next_item[2] = item[2];
    c->next_item[3] = item[3];

    return send_txt(c, "%s", __(c, "\tE\tC7Next item set successfully"));
}

/* Usage /item4 item4 */
static int handle_item4(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t item;
    int count;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Copy over the item data. */
    count = sscanf(params, "%x", &item);

    if(count == EOF || count == 0) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid item code"));
    }

    c->next_item[3] = item;

    return send_txt(c, "%s", __(c, "\tE\tC7Next item set successfully"));
}

/* Usage: /event number */
static int handle_event(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l;
    ship_t *s = c->cur_ship;
    ship_client_t *c2;
    block_t *b;
    int event, i, j;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Grab the event number */
    event = atoi(params);

    if(event < 0 || event > 14) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid event code."));
    }

    s->cfg->lobby_event = event;

    /* Go through all the blocks... */
    for(i = 0; i < s->cfg->blocks; ++i) {
        b = s->blocks[i];

        if(b && b->run) {
            pthread_mutex_lock(&b->mutex);

            /* ... and set the event code on each default lobby. */
            TAILQ_FOREACH(l, &b->lobbies, qentry) {
                pthread_mutex_lock(&l->mutex);

                if(l->type == LOBBY_TYPE_DEFAULT) {
                    l->event = event;

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

    return send_txt(c, "%s", __(c, "\tE\tC7Event set."));
}

/* Usage: /passwd newpass */
static int handle_passwd(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game lobby."));
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Only the leader may use this command."));
    }

    /* Check the length of the provided password. */
    if(strlen(params) > 16) {
        return send_txt(c, "%s", __(c, "\tE\tC7Password too long."));
    }

    pthread_mutex_lock(&l->mutex);

    /* Copy the new password in. */
    strcpy(l->passwd, params);

    pthread_mutex_unlock(&l->mutex);

    return send_txt(c, "%s", __(c, "\tE\tC7Password set."));
}

/* Usage: /lname newname */
static int handle_lname(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(l->type == LOBBY_TYPE_DEFAULT) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game lobby."));
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
static int handle_bug(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
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
    gcpkt.padding[0] = gcpkt.padding[1] = gcpkt.padding[1] = 0;
    sprintf(gcpkt.name, __(c, "Report Bug"));
    sprintf(gcpkt.text, __(c, "Send a Simple Mail to this guildcard to report "
                           "a bug"));

    send_txt(c, "%s", __(c, "\tE\tC7Send a mail to the\n"
                         "'Report Bug' user to report\n"
                         "a bug."));

    return handle_dc_gcsend(c, &gcpkt);
}

/* Usage /clinfo client_id */
static int handle_clinfo(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;
    int id, count;
    ship_client_t *cl;
    char ip[INET_ADDRSTRLEN];

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Copy over the item data. */
    count = sscanf(params, "%d", &id);

    if(count == EOF || count == 0 || id >= l->max_clients || id < 0) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Client ID"));
    }

    /* Make sure there is such a client. */
    if(!(cl = l->clients[id])) {
        return send_txt(c, "%s", __(c, "\tE\tC7No such client"));
    }

    /* Fill in the client's info. */
    inet_ntop(AF_INET, &cl->addr, ip, INET_ADDRSTRLEN);
    return send_txt(c, "\tE\tC7Name: %s\nIP: %s\nGC: %u\n%s Lv.%d",
                    cl->pl->v1.name, ip, cl->guildcard,
                    classes[cl->pl->v1.ch_class], cl->pl->v1.level + 1);
}

/* Usage: /gban:d guildcard reason */
static int handle_gban_d(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t gc;
    block_t *b = c->cur_block;
    ship_client_t *i;
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Set the ban with the shipgate first (86400s = 1 day). */
    if(shipgate_send_ban(&c->cur_ship->sg, SHDR_TYPE_GCBAN, c->guildcard, gc,
                         time(NULL) + 86400, reason + 1)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Error setting ban!"));
    }

    /* Look for the requested user and kick them if they're currently connected
       (only on this block). */
    TAILQ_FOREACH(i, b->clients, qentry) {
        /* Disconnect them if we find them */
        if(i->guildcard == gc) {
            if(strlen(reason) > 1) {
                send_message_box(i, "%s\n%s %s\n%s\n%s",
                                 __(i, "\tEYou have been banned by a GM"),
                                 __(i, "Ban Length:"), __(i, "1 day"),
                                 __(i, "Reason:"), reason + 1);
            }
            else {
                send_message_box(i, "%s\n%s %s",
                                 __(i, "\tEYou have been banned by a GM"),
                                 __(i, "Ban Length:"), __(i, "1 day"));
            }

            i->flags |= CLIENT_FLAG_DISCONNECTED;
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

    /* Make sure the requester is a global GM. */
    if(!GLOBAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Set the ban with the shipgate first (604800s = 1 week). */
    if(shipgate_send_ban(&c->cur_ship->sg, SHDR_TYPE_GCBAN, c->guildcard, gc,
                         time(NULL) + 604800, reason + 1)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Error setting ban!"));
    }

    /* Look for the requested user and kick them if they're currently connected
       (only on this block). */
    TAILQ_FOREACH(i, b->clients, qentry) {
        /* Disconnect them if we find them */
        if(i->guildcard == gc) {
            if(strlen(reason) > 1) {
                send_message_box(i, "%s\n%s %s\n%s\n%s",
                                 __(i, "\tEYou have been banned by a GM"),
                                 __(i, "Ban Length:"), __(i, "1 week"),
                                 __(i, "Reason:"), reason + 1);
            }
            else {
                send_message_box(i, "%s\n%s %s",
                                 __(i, "\tEYou have been banned by a GM"),
                                 __(i, "Ban Length:"), __(i, "1 week"));
            }

            i->flags |= CLIENT_FLAG_DISCONNECTED;
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

    /* Make sure the requester is a global GM. */
    if(!GLOBAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Set the ban with the shipgate first (2,592,000s = 30 days). */
    if(shipgate_send_ban(&c->cur_ship->sg, SHDR_TYPE_GCBAN, c->guildcard, gc,
                         time(NULL) + 2592000, reason + 1)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Error setting ban!"));
    }

    /* Look for the requested user and kick them if they're currently connected
       (only on this block). */
    TAILQ_FOREACH(i, b->clients, qentry) {
        /* Disconnect them if we find them */
        if(i->guildcard == gc) {
            if(strlen(reason) > 1) {
                send_message_box(i, "%s\n%s %s\n%s\n%s",
                                 __(i, "\tEYou have been banned by a GM"),
                                 __(i, "Ban Length:"), __(i, "30 days"),
                                 __(i, "Reason:"), reason + 1);
            }
            else {
                send_message_box(i, "%s\n%s %s",
                                 __(i, "\tEYou have been banned by a GM"),
                                 __(i, "Ban Length:"), __(i, "30 days"));
            }

            i->flags |= CLIENT_FLAG_DISCONNECTED;
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

    /* Make sure the requester is a global GM. */
    if(!GLOBAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &reason, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Set the ban with the shipgate first (0xFFFFFFFF = forever (or close
       enough anyway)). */
    if(shipgate_send_ban(&c->cur_ship->sg, SHDR_TYPE_GCBAN, c->guildcard, gc,
                         0xFFFFFFFF, reason + 1)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Error setting ban!"));
    }

    /* Look for the requested user and kick them if they're currently connected
       (only on this block). */
    TAILQ_FOREACH(i, b->clients, qentry) {
        /* Disconnect them if we find them */
        if(i->guildcard == gc) {
            if(strlen(reason) > 1) {
                send_message_box(i, "%s\n%s %s\n%s\n%s",
                                 __(i, "\tEYou have been banned by a GM"),
                                 __(i, "Ban Length:"), __(i, "Forever"),
                                 __(i, "Reason:"), reason + 1);
            }
            else {
                send_message_box(i, "%s\n%s %s",
                                 __(i, "\tEYou have been banned by a GM"),
                                 __(i, "Ban Length:"), __(i, "Forever"));
            }

            i->flags |= CLIENT_FLAG_DISCONNECTED;

            /* The ban setter will get a message telling them the ban has been
               set (or an error happened). */
            return 0;
        }
    }

    /* The person isn't here... There's nothing left to do. */
    return 0;
}

/* Usage: /list parameters (there's too much to put here) */
static int handle_list(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    /* Make sure the requester is a local GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
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
    if(l->type != LOBBY_TYPE_GAME) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game lobby."));
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Only the leader may use this command."));
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
                l->clients[i]->flags |= CLIENT_FLAG_DISCONNECTED;
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
    if(l->type != LOBBY_TYPE_GAME) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game lobby."));
    }

    /* Make sure the requester is the leader of the team. */
    if(l->leader_id != c->client_id) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Only the leader may use this command."));
    }

    /* Can't use this while waiting on a legit check. */
    if((l->flags & LOBBY_FLAG_LEGIT_CHECK)) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Please wait a while before\n"
                                    "using this command."));
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
static int handle_shutdown(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    int i;
    ship_client_t *i2;
    uint32_t when;
    ship_t *s = c->cur_ship;
    block_t *b;

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

    /* Go through each block and send a notification to everyone. */
    for(i = 0; i < s->cfg->blocks; ++i) {
        b = s->blocks[i];

        if(b && b->run) {
            pthread_mutex_lock(&b->mutex);

            /* Send the message to each player. */
            TAILQ_FOREACH(i2, b->clients, qentry) {
                pthread_mutex_lock(&i2->mutex);

                if(i2->pl) {
                    send_txt(i2, "%s %lu %s",
                             __(i2, "\tE\tC7Ship is going down for\n"
                                "shutdown in"),
                             (unsigned long)when, __(i2, "minutes."));
                }

                pthread_mutex_unlock(&i2->mutex);
            }

            pthread_mutex_unlock(&b->mutex);
        }
    }

    ship_server_shutdown(s, time(NULL) + (when * 60));
    return 0;
}

/* Usage: /log guildcard */
static int handle_log(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Look for the requested user and start the log */
    TAILQ_FOREACH(i, b->clients, qentry) {
        /* Start logging them if we find them */
        if(i->guildcard == gc) {
            rv = pkt_log_start(i);

            if(!rv) {
                return send_txt(c, "%s", __(c, "\tE\tC7Logging started"));
            }
            else if(rv == -1) {
                return send_txt(c, "%s", __(c, "\tE\tC7The user is already\n"
                                            "being logged."));
            }
            else if(rv == -2) {
                return send_txt(c, "%s",
                                __(c, "\tE\tC7Cannot create log file"));
            }
        }
    }

    /* The person isn't here... There's nothing left to do. */
    return send_txt(c, "%s", __(c, "\tE\tC7Requested user not\nfound"));
}

/* Usage: /endlog guildcard */
static int handle_endlog(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Look for the requested user and end the log */
    TAILQ_FOREACH(i, b->clients, qentry) {
        /* Finish logging them if we find them */
        if(i->guildcard == gc) {
            rv = pkt_log_stop(i);

            if(!rv) {
                return send_txt(c, "%s", __(c, "\tE\tC7Logging ended"));
            }
            else if(rv == -1) {
                return send_txt(c, "%s", __(c,"\tE\tC7The user is not\n"
                                            "being logged."));
            }
        }
    }

    /* The person isn't here... There's nothing left to do. */
    return send_txt(c, "%s", __(c, "\tE\tC7Requested user not\nfound"));
}

/* Usage: /motd */
static int handle_motd(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    return send_message_box(c, "%s", c->cur_ship->motd);
}

/* Usage: /friendadd guildcard nickname */
static int handle_friendadd(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t gc;
    char *nick;

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, &nick, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Make sure the nickname is valid. */
    if(!nick || nick[0] != ' ' || nick[1] == '\0') {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Nickname"));
    }

    /* Send a request to the shipgate to do the rest */
    shipgate_send_friend_add(&c->cur_ship->sg, c->guildcard, gc, nick + 1);
    
    /* Any further messages will be handled by the shipgate handler */
    return 0;
}

/* Usage: /frienddel guildcard */
static int handle_frienddel(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t gc;

    /* Figure out the user requested */
    errno = 0;
    gc = (uint32_t)strtoul(params, NULL, 10);

    if(errno != 0) {
        /* Send a message saying invalid guildcard number */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Send a request to the shipgate to do the rest */
    shipgate_send_friend_del(&c->cur_ship->sg, c->guildcard, gc);

    /* Any further messages will be handled by the shipgate handler */
    return 0;
}

/* Usage: /dconly [off] */
static int handle_dconly(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;
    int i;

    /* Lock the lobby mutex... we've got some work to do. */
    pthread_mutex_lock(&l->mutex);

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game lobby."));
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
static int handle_v1only(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;
    int i;

    /* Lock the lobby mutex... we've got some work to do. */
    pthread_mutex_lock(&l->mutex);

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game lobby."));
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
static int handle_forgegc(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Make sure a name was given */
    if(!name || name[0] != ' ' || name[1] == '\0') {
        return send_txt(c, "%s", __(c, "\tE\tC7No name given"));
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
    return handle_dc_gcsend(c, &gcpkt);
}

/* Usage: /invuln [off] */
static int handle_invuln(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
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
static int handle_inftp(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
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
static int handle_smite(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;
    int count, id, hp, tp;
    ship_client_t *cl;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game lobby."));
    }

    /* Copy over the item data. */
    count = sscanf(params, "%d %d %d", &id, &hp, &tp);

    if(count == EOF || count < 3 || id >= l->max_clients || id < 0 || hp < 0 ||
       tp < 0 || hp > 2040 || tp > 2040) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Parameter"));
    }

    pthread_mutex_lock(&l->mutex);

    /* Make sure there is such a client. */
    if(!(cl = l->clients[id])) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7No such client"));
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
static int handle_makeitem(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;
    subcmd_drop_stack_t p2;
    static uint32_t itid = 0xF0000001;  /* ID of the next item generated */

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game lobby."));
    }

    /* Make sure there's something set with /item */
    if(!c->next_item[0]) {
        return send_txt(c, "%s", __(c, "\tE\tC7Need to set an item first."));
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
    p2.item_id = LE32(itid);
    p2.item2 = LE32(c->next_item[3]);
    p2.two = LE32(0x00000002);

    /* Clear the set item */
    c->next_item[0] = 0;
    c->next_item[1] = 0;
    c->next_item[2] = 0;
    c->next_item[3] = 0;

    /* Increment the next item ID. */
    itid++;

    /* Send the packet to everyone in the lobby */
    return lobby_send_pkt_dc(l, NULL, (dc_pkt_hdr_t *)&p2, 0);
}

/* Usage: /teleport client */
static int handle_teleport(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    int client;
    lobby_t *l = c->cur_lobby;
    ship_client_t *c2;
    subcmd_teleport_t p2;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game lobby."));
    }

    /* Figure out the user requested */
    errno = 0;
    client = strtoul(params, NULL, 10);

    if(errno) {
        /* Send a message saying invalid client ID */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Client ID"));
    }

    if(client > l->max_clients) {
        /* Client ID too large, give up */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Client ID"));
    }

    if(!(c2 = l->clients[client])) {
        /* Client doesn't exist */
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Client ID"));
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

        /* Send the packet to everyone in the lobby */
        return lobby_send_pkt_dc(l, NULL, (dc_pkt_hdr_t *)&p2, 0);
    }
}

static int handle_dumpinv(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    int i;

    /* Make sure the requester is a GM. */
    if(!LOCAL_GM(c)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
    }

    debug(DBG_LOG, "Inventory dump for %s (%d)\n", c->pl->v1.name,
          c->guildcard);

    for(i = 0; i < c->item_count; ++i) {
        debug(DBG_LOG, "%d (%08x): %08x %08x %08x %08x: %s\n", i, 
               LE32(c->items[i].item_id), LE32(c->items[i].data_l[0]),
               LE32(c->items[i].data_l[1]), LE32(c->items[i].data_l[2]),
               LE32(c->items[i].data2_l), item_get_name(&c->items[i]));
    }

    return 0;
}

/* Usage: /showdcpc [off] */
static int handle_showdcpc(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
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
static int handle_allowgc(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;

    /* Lock the lobby mutex... we've got some work to do. */
    pthread_mutex_lock(&l->mutex);

    /* Make sure that the requester is in a game lobby, not a lobby lobby. */
    if(l->type != LOBBY_TYPE_GAME) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game lobby."));
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
static int handle_ws(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid WS code"));
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
static int handle_ll(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    char str[512];
    lobby_t *l = c->cur_lobby;
    int i;
    ship_client_t *c2;

    strcpy(str, "\tE");

    if(LOCAL_GM(c)) {
        for(i = 0; i < l->max_clients; i += 2) {
            if((c2 = l->clients[i])) {
                snprintf(str, 511, "%s%d: %s (%d)   ", str, i, c2->pl->v1.name,
                         c2->guildcard);
            }
            else {
                snprintf(str, 511, "%s%d: None   ", str, i);
            }

            if((i + 1) < l->max_clients) {
                if((c2 = l->clients[i + 1])) {
                    snprintf(str, 511, "%s%d: %s (%d)\n", str, i + 1,
                             c2->pl->v1.name, c2->guildcard);
                }
                else {
                    snprintf(str, 511, "%s%d: None\n", str, i + 1);
                }
            }
        }
    }
    else {
        for(i = 0; i < l->max_clients; i += 2) {
            if((c2 = l->clients[i])) {
                snprintf(str, 511, "%s%d: %s   ", str, i, c2->pl->v1.name);
            }
            else {
                snprintf(str, 511, "%s%d: None   ", str, i);
            }
            
            if((i + 1) < l->max_clients) {
                if((c2 = l->clients[i + 1])) {
                    snprintf(str, 511, "%s%d: %s\n", str, i + 1,
                             c2->pl->v1.name);
                }
                else {
                    snprintf(str, 511, "%s%d: None\n", str, i + 1);
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
static int handle_npc(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    int count, npcnum, client_id, follow;
    uint8_t tmp[0x10];
    subcmd_pkt_t *p = (subcmd_pkt_t *)tmp;
    lobby_t *l = c->cur_lobby;

    pthread_mutex_lock(&l->mutex);

    if(l->type != LOBBY_TYPE_GAME) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Only valid in a game lobby."));
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid NPC data"));
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid NPC number"));
    }

    if(client_id < 0 || client_id >= l->max_clients || l->clients[client_id]) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid client ID given,\n"
                                    "or no open client slots."));
    }

    if(follow < 0 || follow >= l->max_clients || !l->clients[follow]) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid follow client given"));
    }

    /* This command, for now anyway, locks us down to one player mode. */
    l->flags |= LOBBY_FLAG_SINGLEPLAYER;

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
static int handle_stfu(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Look for the requested user and STFU them (only on this block). */
    TAILQ_FOREACH(i, b->clients, qentry) {
        if(i->guildcard == gc && i->privilege < c->privilege) {
            i->flags |= CLIENT_FLAG_STFU;
            return send_txt(c, "%s", __(c, "\tE\tC7Client STFUed"));
        }
    }

    /* The person isn't here... There's nothing left to do. */
    return send_txt(c, "%s", __(c, "\tE\tC7Guildcard not found"));
}

/* Usage: /unstfu guildcard */
static int handle_unstfu(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Look for the requested user and un-STFU them (only on this block). */
    TAILQ_FOREACH(i, b->clients, qentry) {
        if(i->guildcard == gc) {
            i->flags &= ~CLIENT_FLAG_STFU;
            return send_txt(c, "%s", __(c, "\tE\tC7Client un-STFUed"));
        }
    }

    /* The person isn't here... There's nothing left to do. */
    return send_txt(c, "%s", __(c, "\tE\tC7Guildcard not found"));
}

/* Usage: /ignore client_id */
static int handle_ignore(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    lobby_t *l = c->cur_lobby;
    int id, i;
    ship_client_t *cl;

    /* Copy over the ID. */
    i = sscanf(params, "%d", &id);

    if(i == EOF || i == 0 || id >= l->max_clients || id < 0) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Client ID"));
    }

    /* Lock the lobby so we don't mess anything up in grabbing this... */
    pthread_mutex_lock(&l->mutex);

    /* Make sure there is such a client. */
    if(!(cl = l->clients[id])) {
        pthread_mutex_unlock(&l->mutex);
        return send_txt(c, "%s", __(c, "\tE\tC7No such client"));
    }

    /* Find an empty spot to put this in. */
    for(i = 0; i < CLIENT_IGNORE_LIST_SIZE; ++i) {
        if(!c->ignore_list[i]) {
            c->ignore_list[i] = cl->guildcard;
            pthread_mutex_unlock(&l->mutex);
            return send_txt(c, "%s %s\n%s %d", __(c, "\tE\tC7Ignoring"),
                            cl->pl->v1.name, __(c, "\tE\tC7Entry"), i);
        }
    }

    /* If we get here, the ignore list is full, report that to the user... */
    pthread_mutex_unlock(&l->mutex);
    return send_txt(c, "%s", __(c, "\tE\tC7Ignore list full"));
}

/* Usage: /unignore entry_number */
static int handle_unignore(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    int id, i;

    /* Copy over the ID */
    i = sscanf(params, "%d", &id);

    if(i == EOF || i == 0 || id >= CLIENT_IGNORE_LIST_SIZE || id < 0) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Entry Number"));
    }

    /* Clear that entry of the ignore list */
    c->ignore_list[id] = 0;

    return send_txt(c, "%s", __(c, "\tE\tC7Ignore list entry cleared"));
}

/* Usage: /quit */
static int handle_quit(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    c->flags |= CLIENT_FLAG_DISCONNECTED;
    return 0;
}

/* Usage: /gameevent number */
static int handle_gameevent(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    ship_t *s = c->cur_ship;
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

    s->cfg->game_event = event;

    return send_txt(c, "%s", __(c, "\tE\tC7Game Event set."));
}

/* Usage: /ban:d guildcard reason */
static int handle_ban_d(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t gc;
    block_t *b;
    ship_t *s = c->cur_ship;
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Set the ban in the list (86,400s = 7 days) */
    if(ban_guildcard(s, time(NULL) + 86400, c->guildcard, gc, reason + 1)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Error setting ban!"));
    }

    /* Look for the requested user and kick them if they're on the ship. */
    for(j = 0; j < s->cfg->blocks; ++j) {
        if((b = s->blocks[j])) {
            pthread_mutex_lock(&b->mutex);

            TAILQ_FOREACH(i, b->clients, qentry) {
                /* Disconnect them if we find them */
                if(i->guildcard == gc) {
                    if(strlen(reason) > 1) {
                        send_message_box(i, "%s\n%s %s\n%s\n%s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship"), __(i, "Ban Length:"),
                                         __(i, "1 day"), __(i, "Reason:"),
                                         reason + 1);
                    }
                    else {
                        send_message_box(i, "%s\n%s %s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship"), __(i, "Ban Length:"),
                                         __(i, "1 day"));
                    }

                    i->flags |= CLIENT_FLAG_DISCONNECTED;
                }
            }

            pthread_mutex_unlock(&b->mutex);
        }
    }

    return send_txt(c, "%s", __(c, "\tE\tC7Successfully set ban."));
}

/* Usage: /ban:w guildcard reason */
static int handle_ban_w(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t gc;
    block_t *b;
    ship_t *s = c->cur_ship;
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Set the ban in the list (604,800s = 7 days) */
    if(ban_guildcard(s, time(NULL) + 604800, c->guildcard, gc, reason + 1)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Error setting ban!"));
    }

    /* Look for the requested user and kick them if they're on the ship. */
    for(j = 0; j < s->cfg->blocks; ++j) {
        if((b = s->blocks[j])) {
            pthread_mutex_lock(&b->mutex);

            TAILQ_FOREACH(i, b->clients, qentry) {
                /* Disconnect them if we find them */
                if(i->guildcard == gc) {
                    if(strlen(reason) > 1) {
                        send_message_box(i, "%s\n%s %s\n%s\n%s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship"), __(i, "Ban Length:"),
                                         __(i, "1 week"), __(i, "Reason:"),
                                         reason + 1);
                    }
                    else {
                        send_message_box(i, "%s\n%s %s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship"), __(i, "Ban Length:"),
                                         __(i, "1 week"));
                    }

                    i->flags |= CLIENT_FLAG_DISCONNECTED;
                }
            }

            pthread_mutex_unlock(&b->mutex);
        }
    }

    return send_txt(c, "%s", __(c, "\tE\tC7Successfully set ban."));
}

/* Usage: /ban:m guildcard reason */
static int handle_ban_m(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t gc;
    block_t *b;
    ship_t *s = c->cur_ship;
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Set the ban in the list (2,592,000s = 30 days) */
    if(ban_guildcard(s, time(NULL) + 2592000, c->guildcard, gc, reason + 1)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Error setting ban!"));
    }

    /* Look for the requested user and kick them if they're on the ship. */
    for(j = 0; j < s->cfg->blocks; ++j) {
        if((b = s->blocks[j])) {
            pthread_mutex_lock(&b->mutex);

            TAILQ_FOREACH(i, b->clients, qentry) {
                /* Disconnect them if we find them */
                if(i->guildcard == gc) {
                    if(strlen(reason) > 1) {
                        send_message_box(i, "%s\n%s %s\n%s\n%s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship"), __(i, "Ban Length:"),
                                         __(i, "30 days"), __(i, "Reason:"),
                                         reason + 1);
                    }
                    else {
                        send_message_box(i, "%s\n%s %s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship"), __(i, "Ban Length:"),
                                         __(i, "30 days"));
                    }

                    i->flags |= CLIENT_FLAG_DISCONNECTED;
                }
            }

            pthread_mutex_unlock(&b->mutex);
        }
    }

    return send_txt(c, "%s", __(c, "\tE\tC7Successfully set ban."));
}

/* Usage: /ban:p guildcard reason */
static int handle_ban_p(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t gc;
    ship_t *s = c->cur_ship;
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Set the ban in the list. An end time of -1 = forever */
    if(ban_guildcard(s, (time_t)-1, c->guildcard, gc, reason + 1)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Error setting ban!"));
    }

    /* Look for the requested user and kick them if they're on the ship. */
    for(j = 0; j < s->cfg->blocks; ++j) {
        if((b = s->blocks[j])) {
            pthread_mutex_lock(&b->mutex);

            TAILQ_FOREACH(i, b->clients, qentry) {
                /* Disconnect them if we find them */
                if(i->guildcard == gc) {
                    if(strlen(reason) > 1) {
                        send_message_box(i, "%s\n%s %s\n%s\n%s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship"), __(i, "Ban Length:"),
                                         __(i, "Forever"), __(i, "Reason:"),
                                         reason + 1);
                    }
                    else {
                        send_message_box(i, "%s\n%s %s",
                                         __(i, "\tEYou have been banned from "
                                            "this ship"), __(i, "Ban Length:"),
                                         __(i, "Forever"));
                    }

                    i->flags |= CLIENT_FLAG_DISCONNECTED;
                }
            }

            pthread_mutex_unlock(&b->mutex);
        }
    }

    return send_txt(c, "%s", __(c, "\tE\tC7Successfully set ban."));
}

/* Usage: /unban guildcard */
static int handle_unban(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    uint32_t gc;
    ship_t *s = c->cur_ship;
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
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid Guild Card"));
    }

    /* Attempt to lift the ban */
    rv = ban_lift_guildcard_ban(s, gc);

    /* Did we succeed? */
    if(!rv) {
        return send_txt(c, "%s", __(c, "\tE\tC7Lifted ban"));
    }
    else if(rv == -1) {
        return send_txt(c, "%s", __(c, "\tE\tC7User not banned"));
    }
    else {
        return send_txt(c, "%s", __(c, "\tE\tC7Error lifting ban"));
    }
}

/* Usage: /cc [any ascii char] or /cc off */
static int handle_cc(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    /* Are we turning it off? */
    if(!strcmp(params, "off")) {
        c->cc_char = 0;
        return send_txt(c, "%s", __(c, "\tE\tC7Color Chat off"));
    }

    /* Make sure they only gave one character */
    if(strlen(params) != 1) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid trigger char"));
    }

    /* Set the char in the client struct */
    c->cc_char = params[0];
    return send_txt(c, "%s", __(c, "\tE\tC7Color Chat on"));
}

/* Usage: /qlang [2 character language code] */
static int handle_qlang(ship_client_t *c, dc_chat_pkt *pkt, char *params) {
    int i;

    /* Make sure they only gave one character */
    if(strlen(params) != 2) {
        return send_txt(c, "%s", __(c, "\tE\tC7Invalid language code"));
    }

    /* Look for the specified language code */
    for(i = 0; i < CLIENT_LANG_COUNT; ++i) {
        if(!strcmp(language_codes[i], params)) {
            c->q_lang = i;
            return send_txt(c, "%s", __(c, "\tE\tC7Quest language set"));
        }
    }

    return send_txt(c, "%s", __(c, "\tE\tC7Invalid language code"));
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
    { ""         , NULL             }     /* End marker -- DO NOT DELETE */
};

int command_parse(ship_client_t *c, dc_chat_pkt *pkt) {
    command_t *i = &cmds[0];
    int plen = LE16(pkt->hdr.dc.pkt_len);
    char cmd[10], params[plen];
    char *ch;
    int len = 0;

    /* Figure out what the command the user has requested is */
    ch = pkt->msg + 3;

    while(*ch != ' ' && len < 9 && *ch) {
        cmd[len++] = *ch++;
    }

    cmd[len] = '\0';

    /* Copy the params out for safety... */
    if(!*ch) {
        memset(params, 0, plen);
    }
    else {
        strcpy(params, ch + 1);
    }

    /* Look through the list for the one we want */
    while(i->hnd) {
        /* If this is it, go ahead and handle it */
        if(!strcmp(cmd, i->trigger)) {
            return i->hnd(c, pkt, params);
        }

        i++;
    }

    /* Send the user a message saying invalid command. */
    return send_txt(c, "%s", __(c, "\tE\tC7Invalid Command!"));
}

int wcommand_parse(ship_client_t *c, dc_chat_pkt *pkt) {
    int len = LE16(pkt->hdr.dc.pkt_len), tlen = len - 12;
    iconv_t ic;
    size_t in, out;
    ICONV_CONST char *inptr;
    char *outptr;
    unsigned char buf[len];
    dc_chat_pkt *p2 = (dc_chat_pkt *)buf;

    ic = iconv_open("ISO-8859-1", "UTF-16LE");
    if(ic == (iconv_t)-1) {
        return -1;
    }

    /* Convert the text to ISO-8859-1. */
    in = out = tlen;
    inptr = pkt->msg;
    outptr = p2->msg;
    iconv(ic, &inptr, &in, &outptr, &out);
    iconv_close(ic);

    /* Fill in the rest of the packet. */
    p2->hdr.dc.pkt_type = CHAT_TYPE;
    p2->hdr.dc.flags = 0;
    p2->hdr.dc.pkt_len = LE16((12 + (tlen - out)));
    p2->padding = 0;
    p2->guildcard = pkt->guildcard;

    /* Hand off to the normal command parsing code. */
    return command_parse(c, p2);
}
