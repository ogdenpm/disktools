/****************************************************************************
 *  program: unidsk - unpack intel ISIS IMD/IMG images                      *
 *  Copyright (C) 2020 Mark Ogden <mark.pm.ogden@btinternet.com>            *
 *                                                                          *
 *  This program is free software; you can redistribute it and/or           *
 *  modify it under the terms of the GNU General Public License             *
 *  as published by the Free Software Foundation; either version 2          *
 *  of the License, or (at your option) any later version.                  *
 *                                                                          *
 *  This program is distributed in the hope that it will be useful,         *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU General Public License for more details.                            *
 *                                                                          *
 *  You should have received a copy of the GNU General Public License       *
 *  along with this program; if not, write to the Free Software             *
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,              *
 *  MA  02110-1301, USA.                                                    *
 *                                                                          *
 ****************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <direct.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#include "unidsk.h"

struct {
    char label[(6 + 3) * 3 + 2];    // allow for encoded numbers
    char suffix[4];
    char version[(2 * 3) + 1];      // allow for encoded numbers
    char crlf[2 * 3 + 1];           // allow for encoded numbers
    char *format;
    char interleave[4];
    char *os;
} recipeInfo;


struct {
    KeyPtr key;
    char *os;
} osMap[] = {
    "6SLy1sgk5/mEkePx2RtE6sntnQ8", "ISIS I 1.1",
    "/YmatRu9vhzEjMhDylDjhykvqZo", "ISIS II 2.2",
    "CDiOIW5+mmg+lMLUzBiKotg1Q58", "ISIS II 3.4",
    "sRiKeMMS6BzoByW8JMfR602LkRc", "ISIS II 4.0",
    "o3AUw0iiH+s7gzpbQng+QpJOpUU", "ISIS II 4.1",
    "sWQzN17YlQdDPy7hXQc+2Htq9lA", "ISIS II 4.2",
    "u/pkKs6Q7J2EBWs9znd6aTF7j3o", "ISIS II 4.2w",
    "IT/NSStKstpz2wMCcHbGrEj0GJM", "ISIS II 4.3",
    "oQXgd+7fmrQ/wH/Wfr9ptN4yK2s", "ISIS II 4.3w",
    "6zffO/tlxHVojxlqs2BQvu31814", "PDS 1.0",
    "kr87fI+DkuGXQec3i2IG7S+8usI", "PDS 1.1",
    "DJ5TiAs5F4yNGlp7fzUjT/k5Zsk", "TEST 1.0",
    "7SzuQtZxju9XU/+ehbFOQ7W0tz8", "TEST 1.1",
    "yOeRj3n6yo8SYlu3Ne4L8Ci52BI", "OSIRIS 3.0",
    "JnbT/FfQLj4sVO/yJLIL8tyoGUU", "ISIS III(N) 2.0",
    0, "NONE"
};

char *osFormat[] = { "UNKNOWN", "ISIS II SD", "ISIS II DD", "ISIS PDS", "ISIS IV", "ISIS-DOS" };
char *suffixFormat = "-SDP4";


void EncodePair(FILE *fp, unsigned char *s) {
    putc(' ', fp);
    for (int i = 0; i < 2; i++)
        if (isupper(*s) || isdigit(*s) || *s == '.')
            putc(*s++, fp);
        else
            fprintf(fp, "#%02X", *s++);
}

void StrEncodeNCat(char *dst, uint8_t const *src, int len) {
    static char *hexDigits = "0123456789ABCDEF";
    dst = strchr(dst, '\0');        // point to end of dst string

    while (len-- > 0 && *src) {
        if (isupper(*src) || isdigit(*src) || *src == '.')
            *dst++ = *src++;
        else {
            *dst++ = '#';
            *dst++ = hexDigits[*src / 16];
            *dst++ = hexDigits[*src++ % 16];
        }
    }
    *dst = 0;
}


void getRecipeInfo(isisDir_t *isisDir, int diskType, bool isOK) {
    FILE *lab;
    label_t label;

    // get the none label related info
    recipeInfo.format = osFormat[diskType];
    recipeInfo.suffix[0] = suffixFormat[diskType];

    if (osIdx >= 0) {
        if (isisDir[osIdx].actLen != isisDir[osIdx].dirLen || isisDir[osIdx].errors) {
            recipeInfo.os = "CORRUPT";
        } else {
            recipeInfo.os = "UNKNOWN";       // default to unknown os
            recipeInfo.suffix[1] = 'b';      // small b if os but unknown
            for (int i = 0; osMap[i].key; i++)
                if (strcmp(osMap[i].key, isisDir[osIdx].key) == 0) {
                    recipeInfo.os = osMap[i].os;
                    recipeInfo.suffix[1] = 'B'; // yes it is bootable
                    if (i == 0)
                        recipeInfo.suffix[0] = '1';     // special for ISIS I
                    break;
                }
        }
    } else
        recipeInfo.os = "NONE";
    if (!isOK)
        strcat(recipeInfo.suffix, "E");

    // now get the info from isis.lab
    memset(&label, 0, sizeof(label));              // just in case of partial read
    if (!(lab = fopen("isis.lab", "rb")) ||
          fread(&label, 1, sizeof(label), lab) <= sizeof(label) -74 ||          // only need first 3 bytes of fmtTable
          label.name[0] == 0xc7)
        memset(&label, 0, sizeof(label));      // invalid label or uninitialised sector
    if (lab)
        fclose(lab);

    if (label.name[0]) {
        StrEncodeNCat(recipeInfo.label, label.name, 6);        // relies on labelInfo array being 0;
        if (label.name[6]) {                                   // copy -ext if present
            strcat(recipeInfo.label, "-");
            StrEncodeNCat(recipeInfo.label, &label.name[6], 3);
        }
    } else
        strcpy(recipeInfo.label, "broken");

    if (label.version)
        StrEncodeNCat(recipeInfo.version, label.version, 2);
    if (diskType == ISIS_PDS && (label.crlf[0] != '\r' || label.crlf[1] != '\n'))
        StrEncodeNCat(recipeInfo.crlf, label.crlf, 2);

    if (*label.fmtTable)
        if (diskType == ISIS_SD && strncmp(label.fmtTable, "1<6", 3) ||  // nonstandard interleave suffix
            diskType == ISIS_DD && strncmp(label.fmtTable, "145", 3))
            strncpy(recipeInfo.interleave, label.fmtTable, 3);

}



void mkRecipe(char const *name, isisDir_t *isisDir, char *comment, int diskType, bool isOK)
{
    FILE *fp;
    char recipeName[(6+3) * 3 + 1 + 4 + 1];     // 6-3 possibly encoded - suffix '\0'
    char *dbPath;
    isisDir_t *dentry;
    char *altname;

    getRecipeInfo(isisDir, diskType, isOK);

    sprintf(recipeName, "@II-%s-%s", recipeInfo.label, recipeInfo.suffix);

    if ((fp = fopen(recipeName, "wt")) == NULL) {
        fprintf(stderr, "can't create recipe file\n");
        return;
    }


    fprintf(fp, "# %s\n", recipeName + 1);


    fprintf(fp, "label: %s\n", recipeInfo.label);
    if (*recipeInfo.version)
        fprintf(fp, "version: %s\n", recipeInfo.version);

    fprintf(fp, "format: %s\n", recipeInfo.format);
    fprintf(fp, "os: %s\n", recipeInfo.os);

    if (*recipeInfo.interleave)
        fprintf(fp, "interleave: %s\n", recipeInfo.interleave);
    if (*recipeInfo.crlf)
    fprintf(fp, "crlf: %s\n", recipeInfo.crlf);
    fprintf(fp, "source: %s\n", name);

    if (*comment) {
        while (*comment) {
            fputs("# ", fp);
            while (*comment && *comment != '\n')
                putc(*comment++, fp);
            if (*comment)
                putc(*comment++, fp);
        }
    }


    fprintf(fp, "Files:\n");

    for (dentry = isisDir; dentry < &isisDir[MAXDIR] && dentry->name[0]; dentry++) {
        if (dentry->errors || (dentry->name[0] == '#' && dentry->dirLen <= 0)) {
            if (dentry->name[0] == '#') {           // if the file was deleted and has errors note it and skip
                fprintf(fp, "# file %s could not be recovered\n", dentry->name + 1);
                continue;
            }
        }

        fprintf(fp, "%s,", dentry->name);
        if (dentry->attrib & 0x80) putc('F', fp);
        if (dentry->attrib & 4) putc('W', fp);
        if (dentry->attrib & 2) putc('S', fp);
        if (dentry->attrib & 1) putc('I', fp);

        bool inRepo = false;
        if (dentry->dirLen < 0)
            sprintf(dentry->key, "* EOF count %d *", -dentry->dirLen);
        else if (dentry->dirLen == 0)
            strcpy(dentry->key, "");
        else if (dentry->actLen != dentry->dirLen)
            sprintf(dentry->key, "* size %d expected %d *", dentry->actLen, dentry->dirLen);
        else if (dentry->errors)
            strcpy(dentry->key, "* corrupt *");

        bool na = false;
        if (strcmp(dentry->name, "ISIS.DIR") == 0 ||
            strcmp(dentry->name, "ISIS.LAB") == 0 ||
            strcmp(dentry->name, "ISIS.MAP") == 0 ||
            strcmp(dentry->name, "ISIS.FRE") == 0) {
            dbPath = "AUTO";
            na = true;
        } else if (dentry->dirLen == 0) {
            dbPath = "ZERO";
            na = true;
        } else if (dentry->dirLen < 0) {
            dbPath = "ZEROHDR";        // zero but link block allocated
            na = true;
        } else if ((dbPath = findMatch(dentry->key, altname = dentry->name)) ||
            (dbPath = findMatch(dentry->key, altname = NULL)))
            inRepo = true;
        else
            dbPath = dentry->fname;
        if (dentry->key[0] != '*' && na)
            dentry->key[0] = 0;

        fprintf(fp, ",%s,%s%s\n", dentry->key, inRepo ? "^" : "", dbPath);
        // print the alternative names if available
        if (inRepo) {
            for (char *lp = firstLoc(dentry->key); lp; lp = nextLoc())
                if (lp != dbPath && isAlt(lp, altname))
                    fprintf(fp, "# also ^%s\n", lp);
        }
    }
    fclose(fp);
}