/*
    Sylverant Ship Server
    Copyright (C) 2011 Lawrence Sebald

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

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

#include <sylverant/debug.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "bans.h"
#include "ship.h"

#ifndef LIBXML_TREE_ENABLED
#error You must have libxml2 with tree support built-in.
#endif

#define XC (const xmlChar *)

static int write_bans_list(ship_t *s) {
    xmlDoc *doc;
    xmlNode *root;
    xmlDtd *dtd;
    xmlNode *node;
    guildcard_ban_t *i;
    int rv = 0;
    time_t now = time(NULL);
    char tmp_str[64];

    /* Make sure the file exists and can be read, otherwise quietly bail out */
    if(!s->cfg->bans_file[0]) {
        return -1;
    }

    /* Create the new document */
    doc = xmlNewDoc(XC"1.0");
    if(!doc) {
        return -2;
    }

    root = xmlNewNode(NULL, XC"bans");
    if(!root) {
        rv = -3;
        goto err_doc;
    }

    /* Set the bans node as the root */
    xmlDocSetRootElement(doc, root);

    /* Create the DTD declaration we need */
    dtd = xmlCreateIntSubset(doc, XC"bans",
                             XC"-//Sylverant//DTD Ban Configuration 1.0//EN",
                             XC"http://sylverant.net/dtd/bans1/bans.dtd");
    if(!dtd) {
        rv = -4;
        goto err_doc;
    }

    /* Add in all the elements we need as we go through the list */
    pthread_rwlock_rdlock(&s->banlock);

    TAILQ_FOREACH(i, &s->guildcard_bans, qentry) {
        /* Ignore bans that are over already */
        if(i->end_time != -1 && i->end_time < now) {
            continue;
        }

        /* Create the node for this entry, and fill it in. */
        node = xmlNewChild(root, NULL, XC"ban", NULL);
        if(!node) {
            rv = -5;
            goto err_doc;
        }

        sprintf(tmp_str, "%lu", (unsigned long)i->set_by);
        xmlNewProp(node, XC"set_by", XC tmp_str);

        sprintf(tmp_str, "%lu", (unsigned long)i->banned_gc);
        xmlNewProp(node, XC"guildcard", XC tmp_str);

        sprintf(tmp_str, "%lld", (long long)i->start_time);
        xmlNewProp(node, XC"start", XC tmp_str);

        sprintf(tmp_str, "%lld", (long long)i->end_time);
        xmlNewProp(node, XC"end", XC tmp_str);

        xmlNewProp(node, XC"reason", XC i->reason);
    }

    pthread_rwlock_unlock(&s->banlock);

    /* Save the file out */
    xmlSaveFormatFileEnc(s->cfg->bans_file, doc, "UTF-8", 1);

err_doc:
    free(doc);
    
    return rv;
}

static int ban_gc_int(ship_t *s, time_t end_time, time_t start_time,
                      uint32_t set_by, uint32_t guildcard, const char *reason) {
    guildcard_ban_t *ban;
    int len = reason ? strlen(reason) + 1 : 1;

    /* Allocate space for the new ban... */
    ban = (guildcard_ban_t *)malloc(sizeof(guildcard_ban_t));
    if(!ban) {
        debug(DBG_WARN, "Can't allocate space for new guildcard ban\n");
        perror("malloc");
        return -1;
    }

    /* Allocate space for the string and copy it over */
    ban->reason = (char *)malloc(len);
    if(!ban->reason) {
        debug(DBG_WARN, "Can't allocate space for new gc ban reason\n");
        perror("malloc");
        free(ban);
        return -1;
    }

    if(len > 1) {
        strcpy(ban->reason, reason);
    }
    else {
        ban->reason[0] = '\0';
    }

    /* Fill in the struct */
    ban->start_time = start_time;
    ban->end_time = end_time;
    ban->set_by = set_by;
    ban->banned_gc = guildcard;

    /* Now that that's done, we need to add it to the list... */
    pthread_rwlock_wrlock(&s->banlock);
    TAILQ_INSERT_TAIL(&s->guildcard_bans, ban, qentry);
    pthread_rwlock_unlock(&s->banlock);

    return 0;
}

int ban_guildcard(ship_t *s, time_t end_time, uint32_t set_by,
                  uint32_t guildcard, const char *reason) {
    /* Add the ban to the list... */
    if(ban_gc_int(s, end_time, time(NULL), set_by, guildcard, reason)) {
        return -1;
    }

    /* Save the file */
    if(write_bans_list(s)) {
        debug(DBG_WARN, "Couldn't save bans list\n");
        return -2;
    }

    return 0;
}

int ban_lift_guildcard_ban(ship_t *s, uint32_t guildcard) {
    guildcard_ban_t *i, *tmp;
    int num_lifted = 0, num_matching = 0;
    time_t now = time(NULL);

    /* This involves writing to the ban list, in general. So, we have to lock
       for writing, unfortunately... */
    pthread_rwlock_wrlock(&s->banlock);

    /* Look for any matching entries, and remove all of them. */
    i = TAILQ_FIRST(&s->guildcard_bans);
    while(i) {
        tmp = TAILQ_NEXT(i, qentry);

        /* Did we find a match? */
        if(i->banned_gc == guildcard) {
            TAILQ_REMOVE(&s->guildcard_bans, i, qentry);
            free(i->reason);
            free(i);
            ++num_lifted;
            ++num_matching;
        }

        /* While we're at it, remove any stale bans */
        if(i->end_time != (time_t)-1 && i->end_time < now) {
            TAILQ_REMOVE(&s->guildcard_bans, i, qentry);
            free(i->reason);
            free(i);
            ++num_lifted;
        }

        i = tmp;
    }

    /* We're done with writing to the list, unlock this now... */
    pthread_rwlock_unlock(&s->banlock);

    if(num_lifted) {
        /* Save the file */
        if(write_bans_list(s)) {
            debug(DBG_WARN, "Couldn't save bans list\n");
            return -2;
        }

        return num_matching ? 0 : -1;
    }

    /* Didn't find anything if we get here, return failure. */
    return -1;
}

int ban_sweep_guildcards(ship_t *s) {
    guildcard_ban_t *i, *tmp;
    int num_lifted = 0;
    time_t now = time(NULL);

    /* This involves writing to the ban list, in general. So, we have to lock
       for writing, unfortunately... */
    pthread_rwlock_wrlock(&s->banlock);

    /* Look for any expired entries, and remove all of them. */
    i = TAILQ_FIRST(&s->guildcard_bans);
    while(i) {
        tmp = TAILQ_NEXT(i, qentry);

        if(i->end_time != (time_t)-1 && i->end_time < now) {
            TAILQ_REMOVE(&s->guildcard_bans, i, qentry);
            free(i->reason);
            free(i);
            ++num_lifted;
        }

        i = tmp;
    }

    /* We're done with writing to the list, unlock this now... */
    pthread_rwlock_unlock(&s->banlock);

    if(num_lifted) {
        /* Save the file */
        if(write_bans_list(s)) {
            debug(DBG_WARN, "Couldn't save bans list\n");
            return -1;
        }
    }

    return 0;
}

int is_guildcard_banned(ship_t *s, uint32_t guildcard, char **reason,
                        time_t *until) {
    time_t now = time(NULL);
    guildcard_ban_t *i;
    int banned = 0;

    /* Lock the ban lock so nothing changes under us */
    pthread_rwlock_rdlock(&s->banlock);

    /* Look for the user with any bans that haven't expired */
    TAILQ_FOREACH(i, &s->guildcard_bans, qentry) {
        if(i->banned_gc == guildcard) {
            if(i->end_time >= now || i->end_time == (time_t)-1) {
                banned = 1;
                *reason = i->reason;
                *until = i->end_time;
                break;
            }
        }
    }

    pthread_rwlock_unlock(&s->banlock);

    return banned;
}

int ban_list_read(const char *fn, ship_t *s) {
    xmlParserCtxtPtr cxt;
    xmlDoc *doc;
    xmlNode *n;
    xmlChar *set_by, *banned_gc, *start_time, *end_time, *reason;
    uint32_t set_gc, ban_gc;
    time_t s_time, e_time, now = time(NULL);
    int rv = 0, num_bans = 0;

    if(!TAILQ_EMPTY(&s->guildcard_bans)) {
        debug(DBG_WARN, "Cannot read guildcard bans multiple times!\n");
        return -1;
    }

    /* Make sure the file exists and can be read, otherwise quietly bail out */
    if(access(fn, R_OK)) {
        return -1;
    }

    /* Create an XML Parsing context */
    cxt = xmlNewParserCtxt();
    if(!cxt) {
        debug(DBG_ERROR, "Couldn't create XML parsing context for ban List\n");
        rv = -1;
        goto err;
    }

    /* Open the GM list XML file for reading. */
    doc = xmlReadFile(fn, NULL, XML_PARSE_DTDVALID);
    if(!doc) {
        xmlParserError(cxt, "Error in parsing ban List");
        rv = -2;
        goto err_cxt;
    }

    /* Make sure the document validated properly. */
    if(!cxt->valid) {
        xmlParserValidityError(cxt, "Validity Error parsing ban List");
        rv = -3;
        goto err_doc;
    }

    /* If we've gotten this far, we have a valid document. Make sure its not
       empty... */
    n = xmlDocGetRootElement(doc);

    if(!n) {
        debug(DBG_WARN, "Empty ban List document\n");
        rv = -4;
        goto err_doc;
    }

    /* Make sure the list looks sane. */
    if(xmlStrcmp(n->name, XC"bans")) {
        debug(DBG_WARN, "Ban List does not appear to be of the right type\n");
        rv = -5;
        goto err_doc;
    }

    n = n->children;
    while(n) {
        if(n->type != XML_ELEMENT_NODE) {
            /* Ignore non-elements. */
            n = n->next;
            continue;
        }
        else if(xmlStrcmp(n->name, XC"ban")) {
            debug(DBG_WARN, "Invalid Tag %s on line %hu\n", n->name, n->line);
        }
        else {
            /* We've got the right tag, see if we have all the attributes... */
            set_by = xmlGetProp(n, XC"set_by");
            banned_gc = xmlGetProp(n, XC"guildcard");
            start_time = xmlGetProp(n, XC"start");
            end_time = xmlGetProp(n, XC"end");
            reason = xmlGetProp(n, XC"reason");

            if(!set_by || !banned_gc || !start_time || !end_time || !reason) {
                debug(DBG_WARN, "Incomplete ban entry on line %hu\n", n->line);
                goto next;
            }

            errno = 0;
            set_gc = (uint32_t)strtoul((char *)set_by, NULL, 0);

            if(errno) {
                debug(DBG_WARN, "Invalid ban setter on line %hu: %s\n", n->line,
                      set_by);
                goto next;
            }

            ban_gc = (uint32_t)strtoul((char *)banned_gc, NULL, 0);
            if(errno) {
                debug(DBG_WARN, "Invalid banned GC on line %hu: %s\n", n->line,
                      banned_gc);
                goto next;
            }

            s_time = (time_t)strtoll((char *)start_time, NULL, 0);
            if(errno) {
                debug(DBG_WARN, "Invalid start time on line %hu: %s\n", n->line,
                      start_time);
                goto next;
            }

            e_time = (time_t)strtoll((char *)end_time, NULL, 0);
            if(errno) {
                debug(DBG_WARN, "Invalid end time on line %hu: %s\n", n->line,
                      end_time);
                goto next;
            }

            /* Add the ban to the list, if its not expired already */
            if(e_time == -1 || e_time > now) {
                ban_gc_int(s, e_time, s_time, set_gc, ban_gc, (char *)reason);
                ++num_bans;
            }

next:
            /* Free the memory we allocated here... */
            xmlFree(set_by);
            xmlFree(banned_gc);
            xmlFree(start_time);
            xmlFree(end_time);
            xmlFree(reason);
        }

        n = n->next;
    }

    debug(DBG_LOG, "Read %d current guildcard bans\n", num_bans);

    /* Cleanup/error handling below... */
err_doc:
    xmlFreeDoc(doc);
err_cxt:
    xmlFreeParserCtxt(cxt);
err:

    return rv;
}

void ban_list_clear(ship_t *s) {
    guildcard_ban_t *i, *tmp;

    pthread_rwlock_wrlock(&s->banlock);

    i = TAILQ_FIRST(&s->guildcard_bans);
    while(i) {
        tmp = TAILQ_NEXT(i, qentry);

        TAILQ_REMOVE(&s->guildcard_bans, i, qentry);
        free(i->reason);
        free(i);

        i = tmp;
    }

    TAILQ_INIT(&s->guildcard_bans);

    pthread_rwlock_unlock(&s->banlock);
}
