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

#ifndef SCRIPTS_H
#define SCRIPTS_H

#ifdef HAVE_PYTHON
#include <Python.h>
#endif

#include <stdint.h>
#include <sys/queue.h>

/* Hash functions from lookup3.c */
uint32_t hashword(const uint32_t *k, size_t length, uint32_t initval);
void hashword2(const uint32_t *k, size_t length, uint32_t *pc, uint32_t *pb);
uint32_t hashlittle(const void *key, size_t length, uint32_t initval);
void hashlittle2(const void *key, size_t length, uint32_t *pc, uint32_t *pb);
uint32_t hashbig(const void *key, size_t length, uint32_t initval);

#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__)
#define hash(k, l, i) hashbig(k, l, i)
#else
#define hash(k, l, i) hashlittle(k, l, i)
#endif

#ifdef HAVE_PYTHON

/* Scripting stuff in scripts.c */
typedef struct script_entry {
    TAILQ_ENTRY(script_entry) qentry;
    
    char *filename;
    PyObject *module;
} script_entry_t;

script_entry_t *script_lookup(const char *filename, uint32_t *hashv);
script_entry_t *script_add(const char *filename);
void script_remove_entry(script_entry_t *entry);
void script_remove(const char *filename);
void script_hash_cleanup(void);

#endif

void init_scripts(void);
void cleanup_scripts(void);


#endif /* !SCRIPTS_H */
