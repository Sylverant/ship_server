/*
    Sylverant Ship Server
    Copyright (C) 2019 Lawrence Sebald

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

#ifndef SMUTDATA_H
#define SMUTDATA_H

int smutdata_read(const char *fn);
void smutdata_cleanup(void);

#define SMUTDATA_WEST   (1 << 0)
#define SMUTDATA_EAST   (1 << 1)
#define SMUTDATA_BOTH   (SMUTDATA_WEST | SMUTDATA_EAST)

/* Check a string for violations of the word censor. This function does no
   replacement on the string -- it simply checks if the string contains
   something that should be censored. The input string *must* be in UTF-8. */
int smutdata_check_string(const char *str, int which);

/* Censor a given string against the word censor. The input string *must* be in
   UTF-8 already. If any replacements have been made, str will be updated to
   point to the censored string. */
char *smutdata_censor_string(const char *str, int which);

int smutdata_enabled(void);

#endif /* !SMUTDATA_H */
