/*
    Sylverant Ship Server
    Copyright (C) 2010, 2011 Lawrence Sebald

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
#include <regex.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "clients.h"
#include "lobby.h"
#include "block.h"
#include "ship.h"
#include "ship_packets.h"
#include "utils.h"

/* Search domains. */
#define DOMAIN_SHIP     0
#define DOMAIN_BLOCK    1
#define DOMAIN_LOBBY    2

static int pllist_ship(ship_client_t *c, const char *name, int first,
                       int minlvl, int maxlvl, int ch_class) {
    block_t *b;
    ship_client_t *c2;
    int i, count = 0, len = 2;
    char str[512];
    regex_t re;
    char ip[INET6_ADDRSTRLEN];

    /* Compile the regular expression if the user asked to search by name */
    if(name) {
        if(regcomp(&re, name, REG_EXTENDED | REG_ICASE | REG_NOSUB)) {
            return send_txt(c, "%s", __(c, "\tE\tC7Invalid name given"));
        }
    }

    strcpy(str, "\tE");

    /* Look through all blocks on the ship. */
    for(i = 0; i < ship->cfg->blocks && count < first + 4; ++i) {
        if(!ship->blocks[i]) {
            continue;
        }

        b = ship->blocks[i];
        pthread_mutex_lock(&b->mutex);

        /* Go through everyone in the block looking for any matches. */
        TAILQ_FOREACH(c2, b->clients, qentry) {
            pthread_mutex_lock(&c2->mutex);

            /* Make sure the level is in range */
            if(c2->pl->v1.level < minlvl || c2->pl->v1.level > maxlvl) {
                pthread_mutex_unlock(&c2->mutex);
                continue;
            }

            /* Next check if the class matches the request */
            if(ch_class != -1 && c2->pl->v1.ch_class != ch_class) {
                pthread_mutex_unlock(&c2->mutex);
                continue;
            }

            /* Check the name filter, if given */
            if(name && regexec(&re, c2->pl->v1.name, 0, NULL, 0)) {
                pthread_mutex_unlock(&c2->mutex);
                continue;
            }

            /* If we've gotten here, then the user matches */
            ++count;

            /* If we're on the right page, add the client to the message */
            if(count >= first) {
                my_ntop(&c2->ip_addr, ip);

                if(c2->cur_lobby) {
                    sprintf(&str[len], "%s  %s  Lv.%d  GC: %d\n"
                            "B: %d  IP: %s  Lobby: %s\n", c2->pl->v1.name,
                            classes[c2->pl->v1.ch_class], c2->pl->v1.level + 1,
                            c2->guildcard, c2->cur_block->b, ip,
                            c2->cur_lobby->name);
                }
                else {
                    sprintf(&str[len], "%s  %s  Lv.%d  GC: %d\n"
                            "B: %d  IP: %s  Lobby: ----\n", c2->pl->v1.name,
                            classes[c2->pl->v1.ch_class], c2->pl->v1.level + 1,
                            c2->guildcard, c2->cur_block->b, ip);
                }

                len = strlen(str);
            }

            pthread_mutex_unlock(&c2->mutex);

            if(count >= first + 4) {
                break;
            }
        }

        pthread_mutex_unlock(&b->mutex);
    }

    /* Clean up the regular expression */
    if(name) {
        regfree(&re);
    }

    /* If we have no results, then tell the user that */
    if(len == 2) {
        strcpy(str, __(c, "\tENo matches found"));
    }

    /* Send the message away */
    return send_message_box(c, "%s", str);
}

static int pllist_block(ship_client_t *c, const char *name, int first,
                        int minlvl, int maxlvl, int ch_class) {
    block_t *b = c->cur_block;
    ship_client_t *c2;
    int count = 0, len = 2;
    char str[512];
    regex_t re;
    char ip[INET6_ADDRSTRLEN];

    /* Compile the regular expression if the user asked to search by name */
    if(name) {
        if(regcomp(&re, name, REG_EXTENDED | REG_ICASE | REG_NOSUB)) {
            return send_txt(c, "%s", __(c, "\tE\tC7Invalid name given"));
        }
    }

    strcpy(str, "\tE");

    /* Go through everyone in the block looking for any matches. */
    TAILQ_FOREACH(c2, b->clients, qentry) {
        pthread_mutex_lock(&c2->mutex);

        /* Make sure the level is in range */
        if(c2->pl->v1.level < minlvl || c2->pl->v1.level > maxlvl) {
            pthread_mutex_unlock(&c2->mutex);
            continue;
        }

        /* Next check if the class matches the request */
        if(ch_class != -1 && c2->pl->v1.ch_class != ch_class) {
            pthread_mutex_unlock(&c2->mutex);
            continue;
        }

        /* Check the name filter, if given */
        if(name && regexec(&re, c2->pl->v1.name, 0, NULL, 0)) {
            pthread_mutex_unlock(&c2->mutex);
            continue;
        }

        /* If we've gotten here, then the user matches */
        ++count;

        /* If we're on the right page, add the client to the message */
        if(count >= first) {
            my_ntop(&c2->ip_addr, ip);

            if(c2->cur_lobby) {
                sprintf(&str[len], "%s  %s  Lv.%d  GC: %d\n"
                        "B: %d  IP: %s  Lobby: %s\n", c2->pl->v1.name,
                        classes[c2->pl->v1.ch_class], c2->pl->v1.level + 1,
                        c2->guildcard, c2->cur_block->b, ip,
                        c2->cur_lobby->name);
            }
            else {
                sprintf(&str[len], "%s  %s  Lv.%d  GC: %d\n"
                        "B: %d  IP: %s  Lobby: ----\n", c2->pl->v1.name,
                        classes[c2->pl->v1.ch_class], c2->pl->v1.level + 1,
                        c2->guildcard, c2->cur_block->b, ip);
            }

            len = strlen(str);
        }

        pthread_mutex_unlock(&c2->mutex);

        if(count >= first + 4) {
            break;
        }
    }

    /* Clean up the regular expression */
    if(name) {
        regfree(&re);
    }

    /* If we have no results, then tell the user that */
    if(len == 2) {
        strcpy(str, __(c, "\tENo matches found"));
    }

    /* Send the message away */
    return send_message_box(c, "%s", str);
}

static int pllist_lobby(ship_client_t *c, const char *name, int first,
                        int minlvl, int maxlvl, int ch_class) {
    lobby_t *l = c->cur_lobby;
    ship_client_t *c2;
    int i, count = 0, len = 2;
    char str[512];
    regex_t re;
    char ip[INET6_ADDRSTRLEN];

    /* Compile the regular expression if the user asked to search by name */
    if(name) {
        if(regcomp(&re, name, REG_EXTENDED | REG_ICASE | REG_NOSUB)) {
            return send_txt(c, "%s", __(c, "\tE\tC7Invalid name given"));
        }
    }

    strcpy(str, "\tE");

    /* Look through all the clients in this lobby for one matching the request
       that was sent */
    for(i = 0; i < l->max_clients && count < first + 4; ++i) {
        if((c2 = l->clients[i])) {
            pthread_mutex_lock(&c2->mutex);

            /* Make sure the level is in range */
            if(c2->pl->v1.level < minlvl || c2->pl->v1.level > maxlvl) {
                pthread_mutex_unlock(&c2->mutex);
                continue;
            }

            /* Next check if the class matches the request */
            if(ch_class != -1 && c2->pl->v1.ch_class != ch_class) {
                pthread_mutex_unlock(&c2->mutex);
                continue;
            }

            /* Check the name filter, if given */
            if(name && regexec(&re, c2->pl->v1.name, 0, NULL, 0)) {
                pthread_mutex_unlock(&c2->mutex);
                continue;
            }

            /* If we've gotten here, then the user matches */
            ++count;

            /* If we're on the right page, add the client to the message */
            if(count >= first) {
                my_ntop(&c2->ip_addr, ip);

                if(c2->cur_lobby) {
                    sprintf(&str[len], "%s  %s  Lv.%d  GC: %d\n"
                            "B: %d  IP: %s  Lobby: %s\n", c2->pl->v1.name,
                            classes[c2->pl->v1.ch_class], c2->pl->v1.level + 1,
                            c2->guildcard, c2->cur_block->b, ip,
                            c2->cur_lobby->name);
                }
                else {
                    sprintf(&str[len], "%s  %s  Lv.%d  GC: %d\n"
                            "B: %d  IP: %s  Lobby: ----\n", c2->pl->v1.name,
                            classes[c2->pl->v1.ch_class], c2->pl->v1.level + 1,
                            c2->guildcard, c2->cur_block->b, ip);
                }

                len = strlen(str);
            }

            pthread_mutex_unlock(&c2->mutex);
        }
    }

    /* Clean up the regular expression */
    if(name) {
        regfree(&re);
    }

    /* If we have no results, then tell the user that */
    if(len == 2) {
        strcpy(str, __(c, "\tENo matches found"));
    }

    /* Send the message away */
    return send_message_box(c, "%s", str);
}

int send_player_list(ship_client_t *c, char *params) {
    char *lasts = NULL, *tok, *name = NULL;
    int dom, first = 0, minlvl = 0, maxlvl = 200, ch_class = -1;

    /* Set up for string tokenization... */
    tok = strtok_r(params, " ", &lasts);

    /* Figure out what domain we're looking in first. */
    if(!tok) {
        return send_txt(c, "%s", __(c, "\tE\tC7Missing search domain"));
    }
    else if(!strcmp(tok, "s")) {
        dom = DOMAIN_SHIP;
    }
    else if(!strcmp(tok, "b")) {
        dom = DOMAIN_BLOCK;
    }
    else if(!strcmp(tok, "l")) {
        dom = DOMAIN_LOBBY;
    }
    else {
        return send_txt(c, "%s",
                        __(c, "\tE\tC7Invalid or missing search domain"));
    }

    /* Look at whatever we have left.... */
    while((tok = strtok_r(NULL, " ", &lasts))) {
        /* If its a page number, then handle that. */
        if(!strcmp(tok, "p")) {
            tok = strtok_r(NULL, " ", &lasts);

            errno = 0;
            first = (int)strtol(tok, NULL, 0);
            if(errno) {
                return send_txt(c, "%s", __(c, "\tE\tC7Invalid page given"));
            }

            first = (first - 1) * 4;
        }
        else if(!strcmp(tok, "n")) {
            name = strtok_r(NULL, " ", &lasts);

            if(!name) {
                return send_txt(c, "%s",
                                __(c, "\tE\tC7Name requires an argument"));
            }
        }
        else if(!strcmp(tok, "mnlv")) {
            tok = strtok_r(NULL, " ", &lasts);

            errno = 0;
            minlvl = (int)strtol(tok, NULL, 0) - 1;
            if(errno) {
                return send_txt(c, "%s",
                                __(c, "\tE\tC7Invalid min level given"));
            }
        }
        else if(!strcmp(tok, "mxlv")) {
            tok = strtok_r(NULL, " ", &lasts);

            errno = 0;
            maxlvl = (int)strtol(tok, NULL, 0) - 1;
            if(errno) {
                return send_txt(c, "%s",
                                __(c, "\tE\tC7Invalid max level given"));
            }
        }
        else if(!strcmp(tok, "lv")) {
            tok = strtok_r(NULL, " ", &lasts);

            errno = 0;
            minlvl = maxlvl = (int)strtol(tok, NULL, 0) - 1;
            if(errno) {
                return send_txt(c, "%s", __(c, "\tE\tC7Invalid level given"));
            }
        }
        else if(!strcmp(tok, "c")) {
            int i, len;

            tok = strtok_r(NULL, " ", &lasts);

            if(!tok) {
                return send_txt(c, "%s",
                                __(c, "\tE\tC7Class requires an argument"));
            }

            /* Grab the length. */
            len = strlen(tok);

            if(len < 5) {
                return send_txt(c, "%s", __(c, "\tE\tC7Invalid class"));
            }

            /* Format the class name correctly */
            tok[0] = (char)toupper(tok[0]);
            tok[1] = (char)toupper(tok[1]);

            for(i = 2; i < len; ++i) {
                tok[i] = (char)tolower(tok[i]);
            }

            /* Figure out which class number that is */
            for(ch_class = 0; ch_class < 12; ++ch_class) {
                if(!strcmp(tok, classes[ch_class])) {
                    break;
                }
            }

            /* Make sure its valid */
            if(ch_class == 12) {
                return send_txt(c, "%s", __(c, "\tE\tC7Invalid class"));
            }
        }
        else {
            /* Everything comes in pairs, so if we have something we don't
               recognize, skip the next thing too... */
            tok = strtok_r(NULL, " ", &lasts);
        }
    }

    /* Do the search */
    switch(dom) {
        case DOMAIN_SHIP:
            return pllist_ship(c, name, first, minlvl, maxlvl, ch_class);

        case DOMAIN_BLOCK:
            return pllist_block(c, name, first, minlvl, maxlvl, ch_class);

        case DOMAIN_LOBBY:
            return pllist_lobby(c, name, first, minlvl, maxlvl, ch_class);
    }

    return -1;
}
