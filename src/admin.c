/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2012 Lawrence Sebald

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

#include <stdint.h>
#include <pthread.h>

#include <sylverant/debug.h>

#include "admin.h"
#include "block.h"
#include "clients.h"
#include "ship.h"
#include "ship_packets.h"
#include "utils.h"

int kill_guildcard(ship_client_t *c, uint32_t gc, const char *reason) {
    block_t *b;
    ship_client_t *i;
    int j;

    /* Make sure we don't have anyone trying to escalate their privileges. */
    if(!LOCAL_GM(c)) {
        return -1;
    }

    /* Look through all the blocks for the requested user, and kick the first
       instance we happen to find (there shouldn't be more than one). */
    for(j = 0; j < ship->cfg->blocks; ++j) {
        b = ship->blocks[j];

        if(b && b->run) {
            pthread_rwlock_rdlock(&b->lock);

            /* Look for the requested user */
            TAILQ_FOREACH(i, b->clients, qentry) {
                pthread_mutex_lock(&i->mutex);

                /* Disconnect them if we find them */
                if(i->guildcard == gc) {
                    if(c->privilege <= i->privilege) {
                        pthread_mutex_unlock(&i->mutex);
                        pthread_rwlock_unlock(&b->lock);
                        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
                    }

                    if(reason) {
                        send_message_box(i, "%s\n\n%s\n%s",
                                         __(i, "\tEYou have been kicked by a "
                                            "GM."),
                                         __(i, "Reason:"), reason);
                    }
                    else {
                        send_message_box(i, "%s",
                                         __(i, "\tEYou have been kicked by a "
                                            "GM."));
                    }

                    i->flags |= CLIENT_FLAG_DISCONNECTED;
                    pthread_mutex_unlock(&i->mutex);
                    pthread_rwlock_unlock(&b->lock);
                    return 0;
                }

                pthread_mutex_unlock(&i->mutex);
            }

            pthread_rwlock_unlock(&b->lock);
        }
    }

    /* If the requester is a global GM, forward the request to the shipgate,
       since it wasn't able to be done on this ship. */
    if(GLOBAL_GM(c)) {
        shipgate_send_kick(&ship->sg, c->guildcard, gc, reason);
    }

    return 0;
}

int refresh_quests(ship_client_t *c, msgfunc f) {
    sylverant_quest_list_t qlist[CLIENT_VERSION_COUNT][CLIENT_LANG_COUNT];
    quest_map_t qmap;
    int i, j;
    char fn[512];

    /* Make sure we don't have anyone trying to escalate their privileges. */
    if(!LOCAL_GM(c)) {
        return -1;
    }

    if(ship->cfg->quests_dir && ship->cfg->quests_dir[0]) {
        /* Read in the new quests first */
        TAILQ_INIT(&qmap);

        for(i = 0; i < CLIENT_VERSION_COUNT; ++i) {
            for(j = 0; j < CLIENT_LANG_COUNT; ++j) {
                sprintf(fn, "%s/%s-%s/quests.xml", ship->cfg->quests_dir,
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
        pthread_rwlock_wrlock(&ship->qlock);

        /* Out with the old, and in with the new. */
        for(i = 0; i < CLIENT_VERSION_COUNT; ++i) {
            for(j = 0; j < CLIENT_LANG_COUNT; ++j) {
                sylverant_quests_destroy(&ship->qlist[i][j]);
                ship->qlist[i][j] = qlist[i][j];
            }
        }

        quest_cleanup(&ship->qmap);
        ship->qmap = qmap;

        /* Unlock the lock, we're done. */
        pthread_rwlock_unlock(&ship->qlock);
        return f(c, "%s", __(c, "\tE\tC7Updated quest list."));
    }
    else {
        return f(c, "%s", __(c, "\tE\tC7No quest list configured."));
    }
}

int refresh_gms(ship_client_t *c, msgfunc f) {
    /* Make sure we don't have anyone trying to escalate their privileges. */
    if(!LOCAL_ROOT(c)) {
        return -1;
    }

    if(ship->cfg->gm_file && ship->cfg->gm_file[0]) {
        /* Try to read the GM file. This will clean out the old list as
         well, if needed. */
        if(gm_list_read(ship->cfg->gm_file, ship)) {
            return f(c, "%s", __(c, "\tE\tC7Couldn't read GM list."));
        }

        return f(c, "%s", __(c, "\tE\tC7Updated GM list."));
    }
    else {
        return f(c, "%s", __(c, "\tE\tC7No GM list configured."));
    }
}

int refresh_limits(ship_client_t *c, msgfunc f) {
    sylverant_limits_t *limits, *tmplimits;

    /* Make sure we don't have anyone trying to escalate their privileges. */
    if(!LOCAL_GM(c)) {
        return -1;
    }

    if(ship->cfg->limits_file && ship->cfg->limits_file[0]) {
        if(sylverant_read_limits(ship->cfg->limits_file, &limits)) {
            return f(c, "%s", __(c, "\tE\tC7Couldn't read limits."));
        }

        pthread_rwlock_wrlock(&ship->llock);
        tmplimits = ship->limits;
        ship->limits = limits;
        pthread_rwlock_unlock(&ship->llock);

        sylverant_free_limits(tmplimits);

        return f(c, "%s", __(c, "\tE\tC7Updated limits."));
    }
    else {
        return f(c, "%s", __(c, "\tE\tC7No configured limits."));
    }
}

int broadcast_message(ship_client_t *c, const char *message, int prefix) {
    block_t *b;
    int i;
    ship_client_t *i2;

    /* Make sure we don't have anyone trying to escalate their privileges. */
    if(c && !LOCAL_GM(c)) {
        return -1;
    }

    /* Go through each block and send the message to anyone that is alive. */
    for(i = 0; i < ship->cfg->blocks; ++i) {
        b = ship->blocks[i];

        if(b && b->run) {
            pthread_rwlock_rdlock(&b->lock);

            /* Send the message to each player. */
            TAILQ_FOREACH(i2, b->clients, qentry) {
                pthread_mutex_lock(&i2->mutex);

                if(i2->pl) {
                    if(prefix) {
                        send_txt(i2, "%s", __(i2, "\tE\tC7Global Message:"));
                    }

                    send_txt(i2, "%s", message);
                }

                pthread_mutex_unlock(&i2->mutex);
            }

            pthread_rwlock_unlock(&b->lock);
        }
    }

    return 0;
}

int schedule_shutdown(ship_client_t *c, uint32_t when, int restart, msgfunc f) {
    ship_client_t *i2;
    block_t *b;
    int i;
    extern int restart_on_shutdown;     /* in ship_server.c */

    /* Make sure we don't have anyone trying to escalate their privileges. */
    if(!LOCAL_ROOT(c)) {
        return -1;
    }

    /* Go through each block and send a notification to everyone. */
    for(i = 0; i < ship->cfg->blocks; ++i) {
        b = ship->blocks[i];

        if(b && b->run) {
            pthread_rwlock_rdlock(&b->lock);

            /* Send the message to each player. */
            TAILQ_FOREACH(i2, b->clients, qentry) {
                pthread_mutex_lock(&i2->mutex);

                if(i2->pl) {
                    if(i2 != c) {
                        if(restart) {
                            send_txt(i2, "%s %" PRIu32 " %s",
                                     __(i2, "\tE\tC7Ship is going down for\n"
                                        "restart in"), when,
                                     __(i2, "minutes."));
                        }
                        else {
                            send_txt(i2, "%s %" PRIu32 " %s",
                                     __(i2, "\tE\tC7Ship is going down for\n"
                                        "shutdown in"), when,
                                     __(i2, "minutes."));
                        }
                    }
                    else {
                        if(restart) {
                            f(i2, "%s %" PRIu32 " %s",
                              __(i2, "\tE\tC7Ship is going down for\n"
                                 "restart in"), when, __(i2, "minutes."));
                        }
                        else {
                            f(i2, "%s %" PRIu32 " %s",
                              __(i2, "\tE\tC7Ship is going down for\n"
                              "shutdown in"), when, __(i2, "minutes."));
                        }
                    }
                }

                pthread_mutex_unlock(&i2->mutex);
            }

            pthread_rwlock_unlock(&b->lock);
        }
    }

    /* Log the event to the log file */
    debug(DBG_LOG, "Ship server %s scheduled for %" PRIu32 " minutes by %u\n",
          restart ? "restart" : "shutdown", when, c->guildcard);

    restart_on_shutdown = restart;
    ship_server_shutdown(ship, time(NULL) + (when * 60));

    return 0;
}

int global_ban(ship_client_t *c, uint32_t gc, uint32_t l, const char *reason) {
    const char *len = NULL;
    block_t *b;
    ship_client_t *i;
    int j;

    /* Make sure we don't have anyone trying to escalate their privileges. */
    if(!GLOBAL_GM(c)) {
        return -1;
    }

    /* Set the ban with the shipgate first. */
    if(shipgate_send_ban(&ship->sg, SHDR_TYPE_GCBAN, c->guildcard, gc, l,
                         reason)) {
        return send_txt(c, "%s", __(c, "\tE\tC7Error setting ban."));
    }

    /* Look through all the blocks for the requested user, and kick the first
       instance we happen to find, if any (there shouldn't be more than one). */
    for(j = 0; j < ship->cfg->blocks; ++j) {
        b = ship->blocks[j];

        if(b && b->run) {
            pthread_rwlock_rdlock(&b->lock);

            TAILQ_FOREACH(i, b->clients, qentry) {
                /* Disconnect them if we find them */
                if(i->guildcard == gc) {
                    pthread_mutex_lock(&i->mutex);

                    /* Make sure we're not trying something dirty (the gate
                       should also have blocked the ban if this happens, in
                       most cases anyway) */
                    if(c->privilege <= i->privilege) {
                        pthread_mutex_unlock(&i->mutex);
                        pthread_rwlock_unlock(&b->lock);
                        return send_txt(c, "%s", __(c, "\tE\tC7Nice try."));
                    }

                    /* Handle the common cases... */
                    switch(l) {
                        case 0xFFFFFFFF:
                            len = __(i, "Forever");
                            break;

                        case 2592000:
                            len = __(i, "30 days");
                            break;

                        case 604800:
                            len = __(i, "1 week");
                            break;

                        case 86400:
                            len = __(i, "1 day");
                            break;

                        /* Other cases just don't have a length on them... */
                    }

                    /* Send the user a message telling them they're banned. */
                    if(reason && len) {
                        send_message_box(i, "%s\n%s %s\n%s\n%s",
                                         __(i, "\tEYou have been banned by a "
                                            "GM."), __(i, "Ban Length:"),
                                         len, __(i, "Reason:"), reason);
                    }
                    else if(len) {
                        send_message_box(i, "%s\n%s %s",
                                         __(i, "\tEYou have been banned by a "
                                            "GM."), __(i, "Ban Length:"),
                                         len);
                    }
                    else if(reason) {
                        send_message_box(i, "%s\n%s\n%s",
                                         __(i, "\tEYou have been banned by a "
                                            "GM."), __(i, "Reason:"), reason);
                    }
                    else {
                        send_message_box(i, "%s", __(i, "\tEYou have been "
                                                     "banned by a GM."));
                    }

                    i->flags |= CLIENT_FLAG_DISCONNECTED;

                    /* The ban setter will get a message telling them the ban has been
                       set (or an error happened). */
                    pthread_mutex_unlock(&i->mutex);
                    pthread_rwlock_unlock(&b->lock);
                    return 0;
                }

                pthread_mutex_unlock(&i->mutex);
            }

            pthread_rwlock_unlock(&b->lock);
        }
    }

    /* Since the requester is a global GM, forward the kick request to the
       shipgate, since it wasn't able to be done on this ship. */
    if(GLOBAL_GM(c)) {
        shipgate_send_kick(&ship->sg, c->guildcard, gc, reason);
    }

    return 0;
}
