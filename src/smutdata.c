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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <iconv.h>
#include <wchar.h>
#include <wctype.h>

#include <sylverant/debug.h>

#include <psoarchive/PRS.h>

#include "smutdata.h"
#include "utils.h"

#define debug(x, ...) printf(__VA_ARGS__)

static uint32_t smutdata_west_count = 0;
static wchar_t **smutdata_west = NULL;

static uint32_t smutdata_east_count = 0;
static wchar_t **smutdata_east = NULL;

#define LE16_AT_OFFSET(buf, offset) \
    buf[offset] | (buf[offset + 1] << 8)

#define LE32_AT_OFFSET(buf, offset) \
    buf[offset] | (buf[offset + 1] << 8) | \
        (buf[offset + 2] << 16) | (buf[offset + 3] << 24)

int smutdata_read(const char *fn) {
    uint32_t entries1, entries2, i, j, off1, off2;
    uint16_t wordbuf[32];
    char convbuf[128];
    size_t inb, outb;
    ICONV_CONST char *inptr;
    char *outptr;
    int ucsz;
    uint8_t *ucbuf;
    mbstate_t state;

    /* Read in the file and decompress it. */
    if((ucsz = pso_prs_decompress_file(fn, &ucbuf)) < 0) {
        debug(DBG_ERROR, "Cannot read smutdata file %s: %s\n", fn,
              strerror(-ucsz));
        return -1;
    }

    i = LE32_AT_OFFSET(ucbuf, 0);
    if(i != 2) {
        debug(DBG_WARN, "Smutdata header has invalid number of entries: %d\n", (int)i);
        free(ucbuf);
        return -2;
    }

    /* Read the number of elements in the file. */
    entries1 = LE32_AT_OFFSET(ucbuf, 4);
    entries2 = LE32_AT_OFFSET(ucbuf, 8);

    /* Sanity check before we go any farther... */
    if(((entries1 + entries2 + 3) << 2) > (uint32_t)ucsz) {
        debug(DBG_WARN, "Smutdata file is too short reading headers!\n");
        free(ucbuf);
        return -3;
    }

    /* Allocate the arrays... */
    if(!(smutdata_west = (wchar_t **)malloc(sizeof(wchar_t *) * entries1))) {
        debug(DBG_WARN, "Error allocating smutdata array\n");
        free(ucbuf);
        return -4;
    }

    if(!(smutdata_east = (wchar_t **)malloc(sizeof(wchar_t *) * entries2))) {
        debug(DBG_WARN, "Error allocating smutdata array\n");
        free(smutdata_west);
        free(ucbuf);
        return -5;
    }

    /* Clear these just in case something goes wrong below, so we can call the
       normal cleanup function any time after this... */
    memset(smutdata_west, 0, sizeof(wchar_t *) * entries1);
    memset(smutdata_east, 0, sizeof(wchar_t *) * entries2);

    /* The last entry in each is always blank, so ignore it. */
    smutdata_west_count = entries1 - 1;
    smutdata_east_count = entries2 - 1;

    off1 = 12;

    /* Read each entry in */
    for(i = 0; i < smutdata_west_count; ++i) {
        /* Read the pointer in... */
        off2 = LE32_AT_OFFSET(ucbuf, off1);
        off1 = off1 + 4;

        /* Read each letter of the string in. */
        for(j = 0; j < 32; ++j) {
            if(off2 + 2 > (uint32_t)ucsz) {
                debug(DBG_WARN, "Smutdata file is too short reading word!\n");
                free(ucbuf);
                smutdata_cleanup();
                return -6;
            }

            wordbuf[j] = LE16_AT_OFFSET(ucbuf, off2);
            off2 += 2;

            /* Have we reached the end of the string? */
            if(wordbuf[j] == 0)
                break;
        }

        /* Convert the string to UTF-8 */
        inb = (j << 1) + 2;
        outb = 128;
        inptr = (ICONV_CONST char *)wordbuf;
        outptr = convbuf;
        if(iconv(ic_utf16_to_utf8, &inptr, &inb, &outptr,
                 &outb) == (size_t)-1) {
            debug(DBG_WARN, "Error converting smutdata string: %s\n",
                  strerror(errno));
            continue;
        }

        /* Copy it over to the main array. */
        outb = 128 - outb;
        smutdata_west[i] = (wchar_t *)malloc(outb * sizeof(wchar_t));
        if(!smutdata_west[i]) {
            debug(DBG_WARN, "Error allocating smutdata string\n");
            free(ucbuf);
            smutdata_cleanup();
            return -7;
        }

        memset(&state, 0, sizeof(mbstate_t));
        outptr = convbuf;
        mbsnrtowcs(smutdata_west[i], (const char **)&outptr, outb, outb,
                   &state);
    }

    /* Skip the blank entry from the end of the western list before we start
       on the eastern one... */
    off1 += 4;

    for(i = 0; i < smutdata_east_count; ++i) {
        /* Read the pointer in... */
        off2 = LE32_AT_OFFSET(ucbuf, off1);
        off1 = off1 + 4;

        /* Read each letter of the string in. */
        for(j = 0; j < 32; ++j) {
            if(off2 + 2 > (uint32_t)ucsz) {
                debug(DBG_WARN, "Smutdata file is too short reading word!\n");
                free(ucbuf);
                smutdata_cleanup();
                return -8;
            }

            wordbuf[j] = LE16_AT_OFFSET(ucbuf, off2);
            off2 += 2;

            /* Have we reached the end of the string? */
            if(wordbuf[j] == 0)
                break;
        }

        /* Convert the string to UTF-8 */
        inb = (j << 1) + 2;
        outb = 128;
        inptr = (ICONV_CONST char *)wordbuf;
        outptr = convbuf;
        if(iconv(ic_utf16_to_utf8, &inptr, &inb, &outptr,
                 &outb) == (size_t)-1) {
            debug(DBG_WARN, "Error converting smutdata string: %s\n",
                  strerror(errno));
            continue;
        }

        /* Copy it over to the main array. */
        outb = 128 - outb;
        smutdata_east[i] = (wchar_t *)malloc(outb * sizeof(wchar_t));
        if(!smutdata_east[i]) {
            debug(DBG_WARN, "Error allocating smutdata string\n");
            free(ucbuf);
            smutdata_cleanup();
            return -7;
        }

        memset(&state, 0, sizeof(mbstate_t));
        outptr = convbuf;
        mbsnrtowcs(smutdata_east[i], (const char **)&outptr, outb, outb,
                   &state);
    }

    /* Clean up... */
    free(ucbuf);

    return 0;
}

void smutdata_cleanup(void) {
    uint32_t i;

    if(!smutdata_west)
        return;

    for(i = 0; i < smutdata_west_count; ++i) {
        free(smutdata_west[i]);
    }

    free(smutdata_west);
    smutdata_west_count = 0;
    smutdata_west = NULL;

    for(i = 0; i < smutdata_east_count; ++i) {
        free(smutdata_east[i]);
    }

    free(smutdata_east);
    smutdata_east_count = 0;
    smutdata_east = NULL;
}

int smutdata_check_string(const char *str, int which) {
    size_t len = strlen(str);
    uint32_t i, k;
    size_t j, len2;
    int matched = 1;
    wchar_t *real_wstr, *cmp, *wstr;
    mbstate_t state;
    const char *tmp;
    wchar_t tc;

    /* If we don't have the censor loaded, then there's nothing to do. */
    if(!smutdata_west)
        return 0;

    /* Sanity check... */
    if(!(which & SMUTDATA_BOTH))
        return 0;

    /* Convert the input string to a string of wchar_t. */
    if(!(real_wstr = (wchar_t *)malloc((len + 1) * sizeof(wchar_t))))
        return 0;

    memset(&state, 0, sizeof(mbstate_t));
    tmp = str;
    mbsrtowcs(real_wstr, &tmp, len + 1, &state);

    /* Does this string start with a language marker? If so, ignore it. */
    if(len >= 2 && real_wstr[0] == L'\t' &&
       (real_wstr[1] == L'J' || real_wstr[1] == L'E')) {
        len -= 2;
        wstr = real_wstr + 2;
    }
    else {
        wstr = real_wstr;
    }


    /* Check the western language list. */
    if((which & SMUTDATA_WEST)) {
        for(j = 0; j < len; ++j) {
            for(i = 0; i < smutdata_west_count; ++i) {
                len2 = wcslen(smutdata_west[i]);
                cmp = smutdata_west[i];
                matched = 1;

                /* If we're at the beginning of the string and the word we're
                   checking starts with a space, ignore it. */
                if(j == 0 && cmp[0] == L' ') {
                    ++cmp;
                    --len2;
                }

                for(k = 0; k < len - j && k < len2; ++k) {
                    /* Check if we have a match. Note that a tab character
                       matches 'l', '1', and '|'. */
                    tc = towlower(wstr[j + k]);
                    if(cmp[k] != tc) {
                        if(cmp[k] != L'\t' ||
                           (tc != L'l' && tc != L'1' && tc != L'|')) {
                            matched = 0;
                            break;
                        }
                    }
                }

                /* If we appear to have something matched right now, check if
                   it is only set because we ran out of stuff to compare or if
                   there's actually a match. */
                if(matched && !wstr[j + k]) {
                    /* We're at the end of the string... Did we have a real
                       match (or one sans an ending space at most)? */
                    if(len2 > k) {
                        if(cmp[k] != L' ')
                            matched = 0;

                        if(len2 != k + 1)
                            matched = 0;
                    }
                }

                /* If we've still got a match, then return that we matched
                   something... */
                if(matched) {
                    free(real_wstr);
                    return 1;
                }
            }
        }
    }

    /* Check the eastern language list. */
    if((which & SMUTDATA_EAST)) {
        for(j = 0; j < len; ++j) {
            for(i = 0; i < smutdata_east_count; ++i) {
                len2 = wcslen(smutdata_east[i]);
                cmp = smutdata_east[i];
                matched = 1;

                /* If we're at the beginning of the string and the word we're
                   checking starts with a space, ignore it. */
                if(j == 0 && cmp[0] == L' ') {
                    ++cmp;
                    --len2;
                }

                for(k = 0; k < len - j && k < len2; ++k) {
                    /* Check if we have a match. */
                    if(cmp[k] != wstr[j + k]) {
                        matched = 0;
                        break;
                    }
                }

                /* If we appear to have something matched right now, check if
                   it is only set because we ran out of stuff to compare or if
                   there's actually a match. */
                if(matched && !wstr[j + k]) {
                    /* We're at the end of the string... Did we have a real
                       match (or one sans an ending space at most)? */
                    if(len2 > k) {
                        if(cmp[k] != L' ')
                            matched = 0;

                        if(len2 != k + 1)
                            matched = 0;
                    }
                }

                /* If we've still got a match, then return that we matched
                   something... */
                if(matched) {
                    free(real_wstr);
                    return 1;
                }
            }
        }
    }

    free(real_wstr);
    return 0;
}

static const char censor_str[] = "#!@%";

char *smutdata_censor_string(const char *str, int which) {
    size_t len = strlen(str);
    uint32_t i, k;
    size_t j, len2;
    int matched = 0, skip = 0;
    wchar_t *wstr, *cmp, *real_wstr;
    const wchar_t *tmp2;
    mbstate_t state;
    const char *tmp;
    char *rv;
    wchar_t tc;

    /* Convert the input string to a string of wchar_t. */
    if(!(real_wstr = (wchar_t *)malloc((len + 1) * sizeof(wchar_t))))
        return NULL;

    memset(&state, 0, sizeof(mbstate_t));
    tmp = str;
    len = mbsrtowcs(real_wstr, &tmp, len + 1, &state);

    /* Sanity check... */
    if(!smutdata_west)
        goto out;

    if(!(which & SMUTDATA_BOTH))
        goto out;

    /* Does this string start with a language marker? If so, ignore it. */
    if(len >= 2 && real_wstr[0] == L'\t' &&
       (real_wstr[1] == L'J' || real_wstr[1] == L'E')) {
        len -= 2;
        wstr = real_wstr + 2;
    }
    else {
        wstr = real_wstr;
    }

    /* Check the western language list. */
    if((which & SMUTDATA_WEST)) {
        for(j = 0; j < len; ++j) {
            for(i = 0; i < smutdata_west_count; ++i) {
                len2 = wcslen(smutdata_west[i]);
                cmp = smutdata_west[i];
                matched = 1;
                skip = 0;

                /* If we're at the beginning of the string and the word we're
                   checking starts with a space, ignore it. */
                if(j == 0 && cmp[0] == L' ') {
                    ++cmp;
                    --len2;
                    skip = 1;
                }

                for(k = 0; k < len - j && k < len2; ++k) {
                    /* Check if we have a match. Note that a tab character
                       matches 'l', '1', and '|'. */
                    tc = towlower(wstr[j + k]);
                    if(cmp[k] != tc) {
                        if(cmp[k] != L'\t' ||
                           (tc != L'l' && tc != L'1' && tc != L'|')) {
                            matched = 0;
                            break;
                        }
                    }
                }

                /* If we appear to have something matched right now, check if
                   it is only set because we ran out of stuff to compare or if
                   there's actually a match. */
                if(matched && !wstr[j + k]) {
                    /* We're at the end of the string... Did we have a real
                       match (or one sans an ending space at most)? */
                    if(len2 > k) {
                        if(cmp[k] != L' ')
                            matched = 0;

                        if(len2 != k + 1)
                            matched = 0;
                    }
                }

                /* If we've still got a match, then break out, so we can do the
                   censoring...*/
                if(matched) {
                    break;
                }
            }

            /* Do we have something to censor? If so, do it and move on to the
               next character after what we censored. */
            if(matched) {
                for(k = 0; k < len2; ++k) {
                    /* Don't censor spaces. */
                    if(wstr[j + k] && wstr[j + k] != L' ') {
                        wstr[j + k] = censor_str[(k + skip) & 0x03];
                    }
                }

                j += len2 - 1;

                if(cmp[len2 - 1] == L' ')
                    --j;
            }
        }
    }

    /* Check the eastern language list. */
    if((which & SMUTDATA_EAST)) {
        for(j = 0; j < len; ++j) {
            for(i = 0; i < smutdata_east_count; ++i) {
                len2 = wcslen(smutdata_east[i]);
                cmp = smutdata_east[i];
                matched = 1;
                skip = 0;

                /* If we're at the beginning of the string and the word we're
                   checking starts with a space, ignore it. */
                if(j == 0 && cmp[0] == L' ') {
                    ++cmp;
                    --len2;
                    skip = 1;
                }

                for(k = 0; k < len - j && k < len2; ++k) {
                    /* Check if we have a match. */
                    if(cmp[k] != wstr[j + k]) {
                        matched = 0;
                        break;
                    }
                }

                /* If we appear to have something matched right now, check if
                   it is only set because we ran out of stuff to compare or if
                   there's actually a match. */
                if(matched && !wstr[j + k]) {
                    /* We're at the end of the string... Did we have a real
                       match (or one sans an ending space at most)? */
                    if(len2 > k) {
                        if(cmp[k] != L' ')
                            matched = 0;

                        if(len2 != k + 1)
                            matched = 0;
                    }
                }

                /* If we've still got a match, then break out, so we can do the
                   censoring...*/
                if(matched) {
                    break;
                }
            }

            /* Do we have something to censor? If so, do it and move on to the
               next character after what we censored. */
            if(matched) {
                for(k = 0; k < len2; ++k) {
                    /* Don't censor spaces. */
                    if(wstr[j + k] && wstr[j + k] != L' ') {
                        wstr[j + k] = censor_str[(k + skip) & 0x03];
                    }
                }

                j += len2 - 1;

                if(cmp[len2 - 1] == L' ')
                    --j;
            }
        }
    }

    /* Copy over the output. */
out:
    memset(&state, 0, sizeof(mbstate_t));
    tmp2 = real_wstr;
    len2 = wcsrtombs(NULL, &tmp2, 0, &state);

    if(!(rv = (char *)malloc(len2 + 1))) {
        free(real_wstr);
        return NULL;
    }

    memset(&state, 0, sizeof(mbstate_t));
    tmp2 = real_wstr;
    wcsrtombs(rv, &tmp2, len2 + 1, &state);
    free(real_wstr);

    return rv;
}

int smutdata_enabled(void) {
    return !!smutdata_west;
}
