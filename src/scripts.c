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

#ifdef HAVE_PYTHON
#include <Python.h>
#endif

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

#ifndef LIBXML_TREE_ENABLED
#error You must have libxml2 with tree support built-in.
#endif

#define XC (const xmlChar *)

#ifdef HAVE_PYTHON

/* Text versions of the script actions. This must match the list in the
   script_action_t enum in scripts.h. */
static const xmlChar *script_action_text[] = {
    XC"client login",
    XC"client logout"
};

#define SCRIPT_HASH_ENTRIES 24

/* Set up the hash table of script files */
TAILQ_HEAD(script_queue, script_entry);
static struct script_queue scripts[SCRIPT_HASH_ENTRIES];

/* List of scripts defined */
static script_event_t scriptevents[ScriptActionCount];
static int scripts_initialized = 0;

/* Method list in the sylverant package in Python (currently none) */
static PyMethodDef sylverant_methods[] = {
    { NULL }
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

/* Clean up the whole list of scripts */
void script_eventlist_clear() {
    int i;

    for(i = 0; i < ScriptActionCount; ++i) {
        if(scriptevents[i].function) {
            Py_DECREF(scriptevents[i].function);
            scriptevents[i].function = NULL;
            scriptevents[i].module = NULL;
        }
    }
}

/* Parse the XML for the script definitions */
int script_eventlist_read(const char *fn) {
    xmlParserCtxtPtr cxt;
    xmlDoc *doc;
    xmlNode *n;
    xmlChar *module, *function, *event;
    script_entry_t *mod_entry;
    PyObject *func_entry;
    int rv = 0, idx;

    /* If we're reloading, kill the old list. */
    if(scripts_initialized) {
        script_eventlist_clear();
    }

    /* Create an XML Parsing context */
    cxt = xmlNewParserCtxt();
    if(!cxt) {
        debug(DBG_ERROR, "Couldn't create XML parsing context for scripts\n");
        rv = -1;
        goto err;
    }

    /* Open the script list XML file for reading. */
    doc = xmlReadFile(fn, NULL, 0 /* XML_PARSE_DTDVALID */);
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
            module = xmlGetProp(n, XC"module");
            function = xmlGetProp(n, XC"function");

            if(!event || !module || !function) {
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
            if(scriptevents[idx].function) {
                debug(DBG_WARN, "Redefining event \"%s\" on line %hu\n",
                      (char *)event, n->line);
                Py_DECREF(scriptevents[idx].function);
                scriptevents[idx].function = NULL;
                scriptevents[idx].module = NULL;
            }

            /* Try to grab the module */
            mod_entry = script_add((const char *)module);
            if(!mod_entry) {
                debug(DBG_WARN, "Couldn't load module \"%s\" on line %hu\n",
                      (char *)module, n->line);
                goto next;
            }

            /* Ok, got the module, now look for the function */
            func_entry = PyObject_GetAttrString(mod_entry->module,
                                                (char *)function);
            if(!func_entry || !PyCallable_Check(func_entry)) {
                debug(DBG_WARN, "Function %s does not exist in module %s (line "
                      "%hu)\n", (char *)function, (char *)module, n->line);
                Py_XDECREF(func_entry);
                goto next;
            }

            /* Everything's set up, now all we have to do is fill in the entry
               in the list */
            scriptevents[idx].module = mod_entry;
            scriptevents[idx].function = func_entry;

        next:
            /* Free the memory we allocated here... */
            xmlFree(event);
            xmlFree(module);
            xmlFree(function);
        }

        n = n->next;
    }

    /* Cleanup/error handling below... */
err_doc:
    xmlFreeDoc(doc);
err_cxt:
    xmlFreeParserCtxt(cxt);
err:

    return rv;
}

/* Call the script function for the given event with the args listed */
int script_execute(script_action_t event, ...) {
    va_list ap;
    int argcount = 0, i = 0;
    PyObject *args, *obj, *rv;

    /* Make sure the event is sane */
    if(event < ScriptActionFirst || event >= ScriptActionCount) {
        return -1;
    }

    /* Short circuit if the event isn't defined */
    if(!scriptevents[event].function) {
        return 0;
    }

    /* Figure out how many arguments there are */
    va_start(ap, event);
    while(va_arg(ap, void *)) {
        ++argcount;
    }
    va_end(ap);

    /* Build up the tuple of arguments */
    args = PyTuple_New(argcount);
    if(!args) {
        return -2;
    }

    va_start(ap, event);
    while((obj = va_arg(ap, PyObject *))) {
        /* PyTuple_SetItem() steals a reference, so increment the refcount */
        Py_INCREF(obj);
        PyTuple_SetItem(args, i++, obj);
    }
    va_end(ap);

    /* Attempt to call the function */
    rv = PyObject_CallObject(scriptevents[event].function, args);
    Py_DECREF(args);

    if(!rv) {
        debug(DBG_WARN, "Error calling function for event \"%s\":\n",
              script_action_text[event]);
        PyErr_Print();
        return -3;
    }

    /* Ignore the return value from Python, and return success. */
    Py_DECREF(rv);

    return 0;
}

/* Look for an entry with the given filename */
script_entry_t *script_lookup(const char *filename, uint32_t *hashv) {
    uint32_t hashval;
    script_entry_t *i;

    /* First look to see if its already there */
    hashval = hash(filename, strlen(filename), 0);

    if(hashv) {
        *hashv = hashval % SCRIPT_HASH_ENTRIES;
    }

    TAILQ_FOREACH(i, &scripts[hashval % SCRIPT_HASH_ENTRIES], qentry) {
        if(!strcmp(filename, i->filename)) {
            return i;
        }
    }

    return NULL;
}

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

void script_hash_cleanup(void) {
    int i;
    script_entry_t *j, *tmp;

    for(i = 0; i < SCRIPT_HASH_ENTRIES; ++i) {
        j = TAILQ_FIRST(&scripts[i]);

        while(j) {
            tmp = TAILQ_NEXT(j, qentry);
            TAILQ_REMOVE(&scripts[i], j, qentry);

            Py_DECREF(j->module);
            free(j->filename);
            free(j);
            j = tmp;
        }

        TAILQ_INIT(&scripts[i]);
    }
}

void init_scripts(void) {
    PyObject* m;
    int i;
    char *origpath;
    char *scriptdir;

    Py_InitializeEx(0);

    /* Add the scripts directory to the path */
    origpath = Py_GetPath();
    scriptdir = (char *)malloc(strlen(origpath) + 1 +
                               strlen(sylverant_directory) + 8);

#ifndef _WIN32
    sprintf(scriptdir, "%s:%s/scripts", origpath, sylverant_directory);
#else
    sprintf(scriptdir, "%s;%s/scripts", origpath, sylverant_directory);
#endif

    PySys_SetPath(scriptdir);
    free(scriptdir);

    m = Py_InitModule3("sylverant", sylverant_methods, "Sylveant module.");

    /* Init each class here */
    client_init_scripting(m);

    /* Clean up the scripting hash entries */
    for(i = 0; i < SCRIPT_HASH_ENTRIES; ++i) {
        TAILQ_INIT(&scripts[i]);
    }

    memset(scriptevents, 0, sizeof(script_event_t) * ScriptActionCount);

    /* XXXX: Read in the configuration (hard-coded file for now, fix later) */
    if(script_eventlist_read("config/scripts.xml")) {
        debug(DBG_WARN, "Couldn't load scripts configuration!\n");
    }
    else {
        debug(DBG_LOG, "Read script configuration\n");
    }
}

void cleanup_scripts(void) {
    script_eventlist_clear();
    script_hash_cleanup();
    Py_Finalize();
}

#else

void init_scripts(void) {
}

void cleanup_scripts(void) {
}

#endif /* HAVE_PYTHON */
