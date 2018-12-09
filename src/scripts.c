/*
    Sylverant Ship Server
    Copyright (C) 2011, 2016, 2018 Lawrence Sebald

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
#include <stdarg.h>
#include <sys/queue.h>

#include <sylverant/debug.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "scripts.h"
#include "utils.h"
#include "clients.h"

#ifdef ENABLE_LUA
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#endif

#ifndef LIBXML_TREE_ENABLED
#error You must have libxml2 with tree support built-in.
#endif

#define XC (const xmlChar *)

#if 0

script_entry_t *script_add(const char *filename) {
    uint32_t hashval;
    script_entry_t *entry;
    PyObject *name;

    /* See if its already in the table */
    if((entry = script_lookup(filename, &hashval))) {
        return entry;
    }

    /* Allocate space */
    entry = (script_entry_t *)xmalloc(sizeof(script_entry_t));
    entry->filename = (char *)xmalloc(strlen(filename) + 1);

    /* Set the data as needed */
    strcpy(entry->filename, filename);

    /* Read in the file */
    name = PyString_FromString(filename);
    if(!name) {
        goto err;
    }

    entry->module = PyImport_Import(name);
    Py_DECREF(name);

    if(!entry->module) {
        debug(DBG_WARN, "Couldn't load script file %s\n", filename);
        PyErr_Print();
        goto err;
    }

    /* Add it to the list where it belongs, and we're done! */
    TAILQ_INSERT_TAIL(&scripts[hashval], entry, qentry);
    return entry;

err:
    free(entry->filename);
    free(entry);
    return NULL;
}

void script_remove_entry(script_entry_t *entry) {
    uint32_t hashval;

    /* Figure out where the entry is */
    hashval = hash(entry->filename, strlen(entry->filename), 0);

    /* Remove it */
    TAILQ_REMOVE(&scripts[hashval % SCRIPT_HASH_ENTRIES], entry, qentry);
    Py_DECREF(entry->module);
    free(entry->filename);
    free(entry);
}

void script_remove(const char *filename) {
    script_entry_t *entry;
    uint32_t hashval;

    /* Find the entry */
    entry = script_lookup(filename, &hashval);

    /* Remove it */
    if(entry) {
        TAILQ_REMOVE(&scripts[hashval], entry, qentry);
        Py_DECREF(entry->module);
        free(entry->filename);
        free(entry);
    }
}

#elif defined(ENABLE_LUA)

static pthread_mutex_t script_mutex = PTHREAD_MUTEX_INITIALIZER;
static lua_State *lstate;
static int scripts_ref = 0;

static int script_ids[ScriptActionCount] = { 0 };

/* Text versions of the script actions. This must match the list in the
   script_action_t enum in scripts.h. */
static const xmlChar *script_action_text[] = {
    XC"STARTUP",
    XC"SHUTDOWN",
    XC"SHIP_LOGIN",
    XC"SHIP_LOGOUT",
    XC"BLOCK_LOGIN",
    XC"BLOCK_LOGOUT",
    XC"UNK_SHIP_PKT",
    XC"UNK_BLOCK_PKT",
    XC"UNK_EP3_PKT",
    XC"ENEMY_KILL",
    XC"ENEMY_HIT",
    XC"TEAM_CREATE",
    XC"TEAM_DESTROY",
    XC"TEAM_JOIN",
    XC"TEAM_LEAVE",
};

/* Figure out what index a given script action sits at */
static inline int script_action_to_index(xmlChar *str) {
    int i;

    for(i = 0; i < ScriptActionCount; ++i) {
        if(!xmlStrcmp(script_action_text[i], str)) {
            return i;
        }
    }

    return ScriptActionInvalid;
}

/* Parse the XML for the script definitions */
int script_eventlist_read(const char *fn) {
    xmlParserCtxtPtr cxt;
    xmlDoc *doc;
    xmlNode *n;
    xmlChar *file, *event;
    int rv = 0, idx;

    /* If we're reloading, kill the old list. */
    if(scripts_ref) {
        luaL_unref(lstate, LUA_REGISTRYINDEX, scripts_ref);
    }

    /* Create an XML Parsing context */
    cxt = xmlNewParserCtxt();
    if(!cxt) {
        debug(DBG_ERROR, "Couldn't create XML parsing context for scripts\n");
        rv = -1;
        goto err;
    }

    /* Open the script list XML file for reading. */
    doc = xmlReadFile(fn, NULL, 0);
    if(!doc) {
        xmlParserError(cxt, "Error in parsing script List");
        rv = -2;
        goto err_cxt;
    }

    /* Make sure the document validated properly. */
    if(!cxt->valid) {
        xmlParserValidityError(cxt, "Validity Error parsing script List");
        rv = -3;
        goto err_doc;
    }

    /* If we've gotten this far, we have a valid document, now go through and
       add in entries for everything... */
    n = xmlDocGetRootElement(doc);

    if(!n) {
        debug(DBG_WARN, "Empty script List document\n");
        rv = -4;
        goto err_doc;
    }

    /* Make sure the list looks sane. */
    if(xmlStrcmp(n->name, XC"scripts")) {
        debug(DBG_WARN, "Script list does not appear to be the right type\n");
        rv = -5;
        goto err_doc;
    }

    /* Create a table for storing our pre-parsed scripts in... */
    lua_newtable(lstate);

    n = n->children;
    while(n) {
        if(n->type != XML_ELEMENT_NODE) {
            /* Ignore non-elements. */
            n = n->next;
            continue;
        }
        else if(xmlStrcmp(n->name, XC"script")) {
            debug(DBG_WARN, "Invalid Tag %s on line %hu\n", n->name, n->line);
        }
        else {
            /* We've got the right tag, see if we have all the attributes... */
            event = xmlGetProp(n, XC"event");
            file = xmlGetProp(n, XC"file");

            if(!event || !file) {
                debug(DBG_WARN, "Incomplete script entry on line %hu\n",
                      n->line);
                goto next;
            }

            /* Figure out the entry we're looking at */
            idx = script_action_to_index(event);

            if(idx == ScriptActionInvalid) {
                debug(DBG_WARN, "Ignoring unknown event (%s) on line %hu\n",
                      (char *)event, n->line);
                goto next;
            }

            /* Issue a warning if we're redefining something */
            if(script_ids[idx]) {
                debug(DBG_WARN, "Redefining event \"%s\" on line %hu\n",
                      (char *)event, n->line);
            }

            /* Attempt to read in the script. */
            if(luaL_loadfile(lstate, (const char *)file) != LUA_OK) {
                debug(DBG_WARN, "Couldn't load script \"%s\" on line %hu\n",
                      (char *)file, n->line);
                goto next;
            }

            /* Add the script to the Lua table. */
            script_ids[idx] = luaL_ref(lstate, -2);
            debug(DBG_LOG, "Script for type %s added as ID %d\n", event,
                  script_ids[idx]);

        next:
            /* Free the memory we allocated here... */
            xmlFree(event);
            xmlFree(file);
        }

        n = n->next;
    }

    /* Store the table of scripts to the registry for later use. */
    scripts_ref = luaL_ref(lstate, LUA_REGISTRYINDEX);

    /* Cleanup/error handling below... */
err_doc:
    xmlFreeDoc(doc);
err_cxt:
    xmlFreeParserCtxt(cxt);
err:

    return rv;
}

void init_scripts(ship_t *s) {
    /* Not that this should happen, but just in case... */
    if(lstate) {
        debug(DBG_WARN, "Attempt to initialize scripting twice!\n");
        return;
    }

    /* Initialize the Lua interpreter */
    debug(DBG_LOG, "Initializing scripting support...\n");
    if(!(lstate = luaL_newstate())) {
        debug(DBG_ERROR, "Cannot initialize Lua!\n");
        return;
    }

    /* Load up the standard libraries. */
    luaL_openlibs(lstate);

    /* Register various scripting libraries. */
    luaL_requiref(lstate, "ship", ship_register_lua, 1);
    lua_pop(lstate, 1);
    luaL_requiref(lstate, "client", client_register_lua, 1);
    lua_pop(lstate, 1);
    luaL_requiref(lstate, "lobby", lobby_register_lua, 1);
    lua_pop(lstate, 1);

    /* Read in the configuration into our script table */
    if(script_eventlist_read(s->cfg->scripts_file)) {
        debug(DBG_WARN, "Couldn't load scripts configuration!\n");

    }
    else {
        debug(DBG_LOG, "Read script configuration\n");
    }
}

void cleanup_scripts(ship_t *s) {
    if(lstate) {
        /* For good measure, remove the scripts table from the registry. This
           should garbage collect everything in it, I hope. */
        if(scripts_ref)
            luaL_unref(lstate, LUA_REGISTRYINDEX, scripts_ref);

        lua_close(lstate);
    }
}

int script_execute_pkt(script_action_t event, ship_client_t *c, const void *pkt,
                       uint16_t len) {
    lua_Integer rv = 0;
    int err = 0;

    /* Can't do anything if we don't have any scripts loaded. */
    if(!scripts_ref)
        return 0;

    pthread_mutex_lock(&script_mutex);

    /* Pull the scripts table out to the top of the stack. */
    lua_rawgeti(lstate, LUA_REGISTRYINDEX, scripts_ref);

    /* See if there's a script event defined */
    if(!script_ids[event])
        goto out;

    /* There is an script defined, grab it from the table. */
    lua_rawgeti(lstate, -1, script_ids[event]);

    /* Now, push the arguments onto the stack. First up is a light userdata
       for the client object. */
    lua_pushlightuserdata(lstate, c);

    /* Next is a string of the packet itself. */
    lua_pushlstring(lstate, (const char *)pkt, (size_t)len);

    /* Done with that, call the function. */
    if(lua_pcall(lstate, 2, 1, 0) != LUA_OK) {
        debug(DBG_ERROR, "Error running Lua script for event %d\n", (int)event);
        lua_pop(lstate, 1);
        goto out;
    }

    /* Grab the return value from the lua function (it should be of type
       integer). */
    rv = lua_tointegerx(lstate, -1, &err);
    if(err) {
        debug(DBG_ERROR, "Script for event %d didn't return int\n",(int)event);
    }

    /* Pop off the return value. */
    lua_pop(lstate, 1);

out:
    /* Pop off the table reference that we pushed up above. */
    lua_pop(lstate, 1);
    pthread_mutex_unlock(&script_mutex);
    return (int)rv;
}

int script_execute(script_action_t event, ...) {
    lua_Integer rv = 0;
    int err = 0, argtype, argcount = 0;
    va_list ap;

    /* Can't do anything if we don't have any scripts loaded. */
    if(!scripts_ref)
        return 0;

    pthread_mutex_lock(&script_mutex);

    /* Pull the scripts table out to the top of the stack. */
    lua_rawgeti(lstate, LUA_REGISTRYINDEX, scripts_ref);

    /* See if there's a script event defined */
    if(!script_ids[event])
        goto out;

    /* There is an script defined, grab it from the table. */
    lua_rawgeti(lstate, -1, script_ids[event]);

    /* Now, push the arguments onto the stack. */
    va_start(ap, event);
    while((argtype = va_arg(ap, int))) {
        switch(argtype) {
            case SCRIPT_ARG_INT:
            {
                int arg = va_arg(ap, int);
                lua_Integer larg = (lua_Integer)arg;
                lua_pushinteger(lstate, larg);
                break;
            }

            case SCRIPT_ARG_UINT8:
            {
                uint8_t arg = (uint8_t)va_arg(ap, int);
                lua_Integer larg = (lua_Integer)arg;
                lua_pushinteger(lstate, larg);
                break;
            }

            case SCRIPT_ARG_UINT16:
            {
                uint16_t arg = (uint16_t)va_arg(ap, int);
                lua_Integer larg = (lua_Integer)arg;
                lua_pushinteger(lstate, larg);
                break;
            }

            case SCRIPT_ARG_UINT32:
            {
                uint32_t arg = va_arg(ap, uint32_t);
                lua_Integer larg = (lua_Integer)arg;
                lua_pushinteger(lstate, larg);
                break;
            }

            case SCRIPT_ARG_FLOAT:
            {
                double arg = va_arg(ap, double);
                lua_Number larg = (lua_Number)arg;
                lua_pushnumber(lstate, larg);
                break;
            }

            case SCRIPT_ARG_PTR:
            {
                void *arg = va_arg(ap, void *);
                lua_pushlightuserdata(lstate, arg);
                break;
            }

            default:
                /* Fix the stack and stop trying to parse now... */
                debug(DBG_WARN, "Invalid script argument type: %d\n", argtype);
                lua_pop(lstate, argcount);
                rv = 0;
                goto out;
        }

        ++argcount;
    }
    va_end(ap);

    /* Done with that, call the function. */
    if(lua_pcall(lstate, argcount, 1, 0) != LUA_OK) {
        debug(DBG_ERROR, "Error running Lua script for event %d\n", (int)event);
        lua_pop(lstate, 1);
        goto out;
    }

    /* Grab the return value from the lua function (it should be of type
       integer). */
    rv = lua_tointegerx(lstate, -1, &err);
    if(err) {
        debug(DBG_ERROR, "Script for event %d didn't return int\n",(int)event);
    }

    /* Pop off the return value. */
    lua_pop(lstate, 1);

out:
    /* Pop off the table reference that we pushed up above. */
    lua_pop(lstate, 1);
    pthread_mutex_unlock(&script_mutex);
    return (int)rv;
}

#else

void init_scripts(ship_t *s) {
}

void cleanup_scripts(ship_t *s) {
}

int script_execute_pkt(script_action_t event, ship_client_t *c, const void *pkt,
                       uint16_t len) {
    return 0;
}

int script_execute(script_action_t event, ...) {
    return 0;
}

#endif /* 0 */
