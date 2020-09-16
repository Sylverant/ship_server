/*
    Sylverant Ship Server
    Copyright (C) 2011, 2016, 2018, 2019, 2020 Lawrence Sebald

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
#include <unistd.h>
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

#ifdef ENABLE_LUA

static pthread_mutex_t script_mutex = PTHREAD_MUTEX_INITIALIZER;
static lua_State *lstate;
static int scripts_ref = 0;

static int script_ids[ScriptActionCount] = { 0 };
static int script_ids_gate[ScriptActionCount] = { 0 };

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
    XC"TEAM_CREATE",
    XC"TEAM_DESTROY",
    XC"TEAM_JOIN",
    XC"TEAM_LEAVE",
    XC"ENEMY_KILL",
    XC"ENEMY_HIT",
    XC"BOX_BREAK",
    XC"UNK_COMMAND",
    XC"SDATA",
    XC"UNK_MENU",
    XC"BANK_ACTION",
    XC"CHANGE_AREA",
    XC"QUEST_SYNCREG",
};

/* Figure out what index a given script action sits at */
static inline script_action_t script_action_to_index(xmlChar *str) {
    int i;

    for(i = 0; i < ScriptActionCount; ++i) {
        if(!xmlStrcmp(script_action_text[i], str)) {
            return (script_action_t)i;
        }
    }

    return ScriptActionInvalid;
}

int script_add(script_action_t action, const char *filename) {
    char realfn[64];
    int len;

    /* Can't do anything if we don't have any scripts loaded. */
    if(!scripts_ref)
        return 0;

    /* Make the real filename we'll try to load from... */
    len = snprintf(realfn, 64, "scripts/%s", filename);
    if(len >= 64) {
        debug(DBG_WARN, "Attempt to add script with long filename\n");
        return -1;
    }

    pthread_mutex_lock(&script_mutex);

    /* Pull the scripts table out to the top of the stack. */
    lua_rawgeti(lstate, LUA_REGISTRYINDEX, scripts_ref);

    /* Attempt to read in the script. */
    if(luaL_loadfile(lstate, realfn) != LUA_OK) {
        debug(DBG_WARN, "Couldn't load script \"%s\"\n", filename);
        lua_pop(lstate, 1);
        pthread_mutex_unlock(&script_mutex);
        return -1;
    }

    /* Issue a warning if we're redefining something before doing it. */
    if(script_ids_gate[action]) {
        debug(DBG_WARN, "Redefining script event %d\n", (int)action);
        luaL_unref(lstate, -2, script_ids_gate[action]);
    }

    /* Add the script to the Lua table. */
    script_ids_gate[action] = luaL_ref(lstate, -2);
    debug(DBG_LOG, "Script for type %d added as ID %d\n", (int)action,
          script_ids_gate[action]);

    /* Pop off the scripts table and unlock the mutex to clean up. */
    lua_pop(lstate, 1);
    pthread_mutex_unlock(&script_mutex);

    return 0;
}

int script_add_lobby_locked(lobby_t *l, script_action_t action) {
    /* Can't do anything if we don't have any scripts loaded. */
    if(!scripts_ref)
        return 0;

    /* Pull the scripts table out to the top of the stack. */
    lua_rawgeti(lstate, LUA_REGISTRYINDEX, scripts_ref);

    /* Issue a warning if we're redefining something before doing it. */
    if(l->script_ids[action]) {
        debug(DBG_WARN, "Redefining lobby event %d for lobby %" PRIu32 "\n",
              (int)action, l->lobby_id);
        luaL_unref(lstate, -1, l->script_ids[action]);
    }

    /* Pull the function out to the top of the stack. */
    lua_pushvalue(lstate, -2);

    /* Add the script to the Lua table. */
    l->script_ids[action] = luaL_ref(lstate, -2);
    debug(DBG_LOG, "Lobby callback for type %d added as ID %d\n", (int)action,
          l->script_ids[action]);

    /* Pop off the scripts table and the function to clean up. */
    lua_pop(lstate, 2);

    return 0;
}

int script_remove(script_action_t action) {
    /* Can't do anything if we don't have any scripts loaded. */
    if(!scripts_ref)
        return 0;

    pthread_mutex_lock(&script_mutex);

    /* Make sure there's actually something registered. */
    if(!script_ids_gate[action]) {
        debug(DBG_WARN, "Attempt to unregister script for event %d that does "
              "not exist.\n", (int)action);
        pthread_mutex_unlock(&script_mutex);
        return -1;
    }

    /* Pull the scripts table out to the top of the stack and remove the
       script reference from it. */
    lua_rawgeti(lstate, LUA_REGISTRYINDEX, scripts_ref);
    luaL_unref(lstate, -2, script_ids_gate[action]);

    /* Pop off the scripts table, clear out the id stored in the script_ids_gate
       array, and unlock the mutex to finish up. */
    lua_pop(lstate, 1);
    script_ids_gate[action] = 0;
    pthread_mutex_unlock(&script_mutex);

    return 0;
}

int script_remove_lobby_locked(lobby_t *l, script_action_t action) {
    /* Can't do anything if we don't have any scripts loaded. */
    if(!scripts_ref)
        return 0;

    /* Make sure there's actually something registered. */
    if(!l->script_ids[action]) {
        debug(DBG_WARN, "Attempt to unregister lobby %" PRIu32 " script for "
              "event %d that does not exist.\n", l->lobby_id, (int)action);
        return -1;
    }

    /* Pull the scripts table out to the top of the stack and remove the
       script reference from it. */
    lua_rawgeti(lstate, LUA_REGISTRYINDEX, scripts_ref);
    luaL_unref(lstate, -2, l->script_ids[action]);

    /* Pop off the scripts table and clear out the id stored in the lobby's
       script_ids array to finish up. */
    lua_pop(lstate, 1);
    l->script_ids[action] = 0;

    return 0;
}

int script_update_module(const char *filename) {
    char *script;
    size_t size;
    char *modname, *tmp;

    /* Can't do anything if we don't have any scripts loaded. */
    if(!scripts_ref)
        return 0;

    /* Chop off the extension of the filename. */
    if(!(modname = strdup(filename)))
        return -1;

    tmp = strrchr(modname, '.');
    if(tmp)
        *tmp = '\0';

    size = strlen(modname);

    if(!(script = (char *)malloc(size + 100)))
        return -1;

    snprintf(script, size + 100, "package.loaded['%s'] = nil", modname);

    /* Set the module search path to include the scripts/modules dir. */
    pthread_mutex_lock(&script_mutex);
    (void)luaL_dostring(lstate, script);
    pthread_mutex_unlock(&script_mutex);
    free(script);
    free(modname);

    return 0;
}

/* Parse the XML for the script definitions */
int script_eventlist_read(const char *fn) {
    xmlParserCtxtPtr cxt;
    xmlDoc *doc;
    xmlNode *n;
    xmlChar *file, *event;
    int rv = 0;
    script_action_t idx;

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
        xmlParserError(cxt, "Error in parsing script list\n");
        rv = -2;
        goto err_cxt;
    }

    /* Make sure the document validated properly. */
    if(!cxt->valid) {
        xmlParserValidityError(cxt, "Validity Error parsing script list\n");
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
                lua_pop(lstate, 1);
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
    long size = pathconf(".", _PC_PATH_MAX);
    char *path_str, *script;

    if(!(path_str = (char *)malloc(size))) {
        debug(DBG_WARN, "Out of memory, bailing out!\n");
        return;
    }
    else if(!getcwd(path_str, size)) {
        debug(DBG_WARN, "Cannot save path, local packages will not work!\n");
    }

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

    if(path_str) {
        size = strlen(path_str) + 100;

        if(!(script = (char *)malloc(size)))
            debug(DBG_WARN, "Cannot save path in scripts!\n");

        if(script) {
            snprintf(script, size, "package.path = package.path .. "
                     "\";%s/scripts/modules/?.lua\"", path_str);

            /* Set the module search path to include the scripts/modules dir. */
            (void)luaL_dostring(lstate, script);
            free(script);
        }

        free(path_str);
    }

    /* Read in the configuration into our script table */
    if(script_eventlist_read(s->cfg->scripts_file)) {
        debug(DBG_WARN, "Couldn't load scripts configuration!\n");

        /* Make a scripts table, in case the gate sends us some later. */
        lua_newtable(lstate);
        scripts_ref = luaL_ref(lstate, LUA_REGISTRYINDEX);
    }
    else {
        debug(DBG_LOG, "Read script configuration\n");
    }

    s->lstate = lstate;
}

void cleanup_scripts(ship_t *s) {
    int i;

    if(lstate) {
        /* For good measure, remove the scripts table from the registry. This
           should garbage collect everything in it, I hope. */
        if(scripts_ref)
            luaL_unref(lstate, LUA_REGISTRYINDEX, scripts_ref);

        lua_close(lstate);

        /* Clean everything back to a sensible state. */
        lstate = NULL;
        scripts_ref = 0;
        for(i = 0; i < ScriptActionCount; ++i) {
            script_ids[i] = 0;
            script_ids_gate[i] = 0;
        }

        s->lstate = NULL;
    }
}

static lua_Integer exec_pkt(int scr, script_action_t event, ship_client_t *c,
                            const void *pkt, uint16_t len) {
    lua_Integer rv = 0;
    int err;

    /* There is an script defined, grab it from the table. */
    lua_rawgeti(lstate, -1, scr);

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
    if(!err) {
        debug(DBG_ERROR, "Script for event %d didn't return int\n", (int)event);
    }

    /* Pop off the return value. */
    lua_pop(lstate, 1);

out:
    return rv;
}

int script_execute_pkt(script_action_t event, ship_client_t *c, const void *pkt,
                       uint16_t len) {
    lua_Integer grv = 0, lrv = 0;

    /* Can't do anything if we don't have any scripts loaded. */
    if(!scripts_ref)
        return 0;

    pthread_mutex_lock(&script_mutex);

    /* Pull the scripts table out to the top of the stack. */
    lua_rawgeti(lstate, LUA_REGISTRYINDEX, scripts_ref);

    /* See if there's a script event defined by the shipgate. */
    if(script_ids_gate[event])
        grv = exec_pkt(script_ids_gate[event], event, c, pkt, len);

    /* See if there's a script event defined locally */
    if(script_ids[event])
        lrv = exec_pkt(script_ids[event], event, c, pkt, len);

    /* Pop off the table reference that we pushed up above. */
    lua_pop(lstate, 1);
    pthread_mutex_unlock(&script_mutex);

    /* Return success if either script ran and returned success. */
    return (int)(grv | lrv);
}

static lua_Integer push_args_and_exec(int scr, script_action_t event,
                                      va_list ap) {
    lua_Integer rv = 0;
    int err = 0, argtype, argcount = 0;

    /* Push the script that we're looking at onto the stack. */
    lua_rawgeti(lstate, -1, scr);

    /* Now, push the arguments onto the stack. */
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

            case SCRIPT_ARG_STRING:
            {
                size_t len = va_arg(ap, size_t);
                char *str = va_arg(ap, char *);
                lua_pushlstring(lstate, str, len);
                break;
            }

            case SCRIPT_ARG_CSTRING:
            {
                char *str = va_arg(ap, char *);
                lua_pushstring(lstate, str);
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

    /* Done with that, call the function. */
    if(lua_pcall(lstate, argcount, 1, 0) != LUA_OK) {
        debug(DBG_ERROR, "Error running Lua script for event %d\n", (int)event);
        lua_pop(lstate, 1);
        goto out;
    }

    /* Grab the return value from the lua function (it should be of type
       integer). */
    rv = lua_tointegerx(lstate, -1, &err);
    if(!err) {
        debug(DBG_ERROR, "Script for event %d didn't return int\n", (int)event);
    }

    /* Pop off the return value. */
    lua_pop(lstate, 1);

out:
    return rv;
}

int script_execute(script_action_t event, ship_client_t *c, ...) {
    lua_Integer llrv = 0, lrv = 0, grv = 0;
    va_list ap;

    /* Can't do anything if we don't have any scripts loaded. */
    if(!scripts_ref)
        return 0;

    pthread_mutex_lock(&script_mutex);

    /* Pull the scripts table out to the top of the stack. */
    lua_rawgeti(lstate, LUA_REGISTRYINDEX, scripts_ref);

    /* See if there's a script event defined by the gate */
    if(script_ids_gate[event]) {
        va_start(ap, c);
        grv = push_args_and_exec(script_ids_gate[event], event, ap);
        va_end(ap);
    }

    /* See if there's a script event defined locally */
    if(script_ids[event]) {
        va_start(ap, c);
        lrv = push_args_and_exec(script_ids[event], event, ap);
        va_end(ap);
    }

    /* See if there is a team-defined event. */
    if(c && c->cur_lobby && c->cur_lobby->script_ids) {
        if(c->cur_lobby->script_ids[event]) {
            va_start(ap, c);
            llrv = push_args_and_exec(c->cur_lobby->script_ids[event], event,
                                      ap);
            va_end(ap);
        }
    }

    /* Pop off the table reference that we pushed up above. */
    lua_pop(lstate, 1);
    pthread_mutex_unlock(&script_mutex);
    return (int)(llrv | lrv | grv);
}

int script_execute_file(const char *fn, lobby_t *l) {
    lua_Integer rv;
    int err;

    /* Can't do anything if we can't run scripts. */
    if(!scripts_ref)
        return -1;

    pthread_mutex_lock(&script_mutex);

    /* Attempt to read in the script. */
    if(luaL_loadfile(lstate, (const char *)fn) != LUA_OK) {
        debug(DBG_WARN, "Couldn't load script '%s'\n", fn);
        lua_pop(lstate, 1);
        return -1;
    }

    /* Push the lobby structure for the team to the stack. */
    lua_pushlightuserdata(lstate, l);

    /* Run the script. */
    if(lua_pcall(lstate, 1, 1, 0) != LUA_OK) {
        debug(DBG_ERROR, "Error running Lua script '%s'\n", fn);
        lua_pop(lstate, 1);
        return -1;
    }

    /* Grab the return value from the lua function (it should be of type
       integer). */
    rv = lua_tointegerx(lstate, -1, &err);
    if(!err) {
        debug(DBG_ERROR, "Script '%s' didn't return int\n", fn);
    }

    /* Pop off the return value. */
    lua_pop(lstate, 1);

    return (int)rv;
}

#else

void init_scripts(ship_t *s) {
    (void)s;
}

void cleanup_scripts(ship_t *s) {
    (void)s;
}

int script_execute_pkt(script_action_t event, ship_client_t *c, const void *pkt,
                       uint16_t len) {
    (void)event;
    (void)c;
    (void)pkt;
    (void)len;
    return 0;
}

int script_execute(script_action_t event, ship_client_t *c, ...) {
    (void)event;
    (void)c;
    return 0;
}

int script_add(script_action_t event, const char *filename) {
    (void)event;
    (void)filename;
    return 0;
}

int script_remove(script_action_t event) {
    (void)event;
    return 0;
}

int script_update_module(const char *modname) {
    (void)modname;
    return 0;
}

int script_add_lobby_locked(lobby_t *l, script_action_t action) {
    (void)l;
    (void)action;
    return 0;
}

int script_remove_lobby_locked(lobby_t *l, script_action_t action) {
    (void)l;
    (void)action;
    return 0;
}

int script_execute_file(const char *fn, lobby_t *l) {
    (void)fn;
    (void)l;
    return 0;
}

#endif /* ENABLE_LUA */
