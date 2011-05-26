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

#include <sys/queue.h>

#include <sylverant/debug.h>

#include "scripts.h"
#include "utils.h"
#include "clients.h"

#ifdef HAVE_PYTHON

#define SCRIPT_HASH_ENTRIES 24

TAILQ_HEAD(script_queue, script_entry);
static struct script_queue scripts[SCRIPT_HASH_ENTRIES];

static PyMethodDef sylverant_methods[] = {
    { NULL }
};

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
}

void cleanup_scripts(void) {
    script_hash_cleanup();
    Py_Finalize();
}

#else

void init_scripts(void) {
}

void cleanup_scripts(void) {
}

#endif /* HAVE_PYTHON */
