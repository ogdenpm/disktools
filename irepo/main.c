/****************************************************************************
 *  program: irepo - Intel Repository Tool                                  *
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

#include "irepo.h"
#include "utility.h"
#include <ctype.h>
#ifdef _MSC_VER
#include <io.h>
#else
#define getopt  hide_getopt
#define optarg hide_optarg
#include <unistd.h>
#undef getopt
#undef optarg
#define stricmp strcasecmp
#define strnicmp strncasecmp

#endif
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

char const help[] = 
        "Usage: %s [options] [--] file*\n"
        "Where options are:\n"
        "   -R    rebuild catalog\n"
        "   -D    show files with the same hash value in the catalog\n"
        "   -u    update recipe @files, default is they are skipped\n"
        "   -a    show alternatives in @files even for matched entries\n"
        "   --    terminates option processing allows files starting with -\n"
        " file    show repo match for file. Recipe @files are skipped unless -u\n"
        "         OS filename wildcard characters are supported\n"
        "\n"
        "NOTE: the environment variable IFILEREPO must be set to the location of the repository\n"
        "where ncatalog.db and the directory trees intel80 and intel86 are located\n";

void showDuplicates() {
    KeyPtr key;
    key = firstKey();
    while (key) {
        char *floc, *loc;
        if ((floc = firstLoc(key)) && (loc = nextLoc())) {
            printf("%s\n", floc);
            while (loc) {
                printf("%s\n", loc);
                loc = nextLoc();
            }
            putchar('\n');
        }
        key = nextKey();
    }
}

char *trim(char *s) {
    char *t = strchr(s, '\0');
    while (t > s && isblank(t[-1]))
        t--;
    *t = 0;
    while (isblank(*s))
        s++;
    return s;
}

void reIndexFile(const char *path, bool showAlt) {
    char tmpFile[10];
    const char *fname = basename(path);
    char line[MAXLINE];
    char newtag[MAXLINE];

    enum { FIRSTLINE, HEADER, FILES } state = FIRSTLINE;

    FILE *fpin, *fpout;
    int changes        = 0;
    int unresolved     = 0;
    bool recipeChanged = false; // used to track single change for a recipe & alts
    recipe_t recipe;

    if ((fpin = fopen(path, "rt")) == NULL) {
        warn("Can't open %s", path);
        return;
    }
    int i;
    for (i = 0; i < 20; i++) { // look for an available filename
        sprintf(tmpFile, "irepo.$%02d", i);
        if (access(tmpFile, 0) == -1 && errno == ENOENT)
            break;
    }
    if ((fpout = fopen(tmpFile, "wt")) == NULL) {
        warn("Can't create tmp file %s", tmpFile);
        fclose(fpin);
        return;
    }
    sprintf(newtag, "# %s", fname + 1);

    while (fgets(line, MAXLINE, fpin)) {
        char *s;
        if ((s = strchr(line, '\n')))
            *s = 0;
        else {
            int c;
            warn("Line truncated: %s", line);
            while ((c = getc(fpin)) != '\n' && c != EOF)
                ;
        }

        s = trim(line);
        if (state == FILES) {
            if (*s == '#') {
                if (strnicmp(line, "# also ^", 8) ==
                    0) // # also only count as single change (with recipe itself & any added #also
                    recipeChanged = true;
                else
                    fprintf(fpout, "%s\n", line);
            } else {
                if (recipeChanged) {
                    changes++;
                    recipeChanged = false;
                }
                if (!parseRecipe(s, &recipe))
                    fprintf(fpout, "%s\n", line);
                else {
                    recipeChanged = recipe.changed | updateRecipe(&recipe);
                    recipeChanged |= printRecipe(fpout, &recipe, showAlt | recipeChanged);
                    if (!recipe.inRepo && !chkSpecial(&recipe))
                        unresolved++;
                }
            }
        } else if (state == HEADER) {
            fprintf(fpout, "%s\n", line);
            if (strnicmp(line, "Files:", 6) == 0)
                state = FILES;
        } else {
            fprintf(fpout, "%s\n", newtag);
            if (strcmp(line, newtag) != 0) {
                changes++;
                if (*line != '#')
                    fprintf(fpout, "%s\n", line);
                else if (strncmp(line, newtag, 5) != 0) {
                    warn("Keeping %s", line);
                    fprintf(fpout, "%s\n", line);
                }
            }
            state = HEADER;
        }
    }

    fclose(fpin);
    fclose(fpout);

    if (changes += recipeChanged) {
        printf("%s: %d Updates & %d files not in repository\n", fname, changes, unresolved);
        unlink(path);
        if (rename(tmpFile, path))
            warn("Cannot rename %s to %s", tmpFile, path);
    } else {
        if (unresolved)
            printf("%s: No Updates & %d files not in repository\n", fname, unresolved);
        else
            printf("%s: No Updates\n", fname);
        unlink(tmpFile);
    }
}

void printFileInfo(const char *fname, bool showKey) {
    Key key;
    char iname[11];
    char *loc;
    char *altname;
    const char *fpart = basename(fname);
    strcpy(iname, mkIname(fname));
    int fsize   = getFileKey(fname, key);
    bool inRepo = (loc = findMatch(key, altname = iname)) || (loc = findMatch(key, altname = NULL));

    if (fsize == 0) // zero length file
        printf("%-11s -> ZERO\n", fname);
    else if (fsize < 0)
        printf("%-11s -> file not found\n", fname);
    else if (stricmp(fpart, "isis.dir") == 0 || stricmp(fpart, "isis.map") == 0 ||
             stricmp(fpart, "isis.lab") == 0 || stricmp(fpart, "isis.fre") == 0) {
        if (showKey)
            printf("%-11s -> AUTO [%s]\n", fname, key);
        else
            printf("%-11s -> AUTO\n", fname);
    } else if (inRepo) {
        if (showKey)
            printf("%-11s -> ^%s [%s]\n", fname, loc, key);
        else
            printf("%-11s -> ^%s\n", fname, loc);
        for (char *lp = firstLoc(getDbLocKey(loc)); lp; lp = nextLoc())
            if (lp != loc && isAlt(lp, altname))
                printf("%14c ^%s\n", '+', lp);
    } else if (showKey)
        printf("%-11s -> Not in repository [%s]\n", fname, key);
    else
        printf("%-11s -> Not in repository\n", fname);
}

int main(int argc, char **argv) {
    bool rebuild         = false;
    bool showAlt         = false;
    bool catDup          = false;
    bool updateRecipe    = false;
    bool showKey         = false;

    while (getopt(argc, argv, "RaDuk") != EOF) {
        switch (optopt) {
        case 'R':
            rebuild = true;
            break;
        case 'a':
            showAlt = true;
            break;
        case 'D':
            catDup = true;
            break;
        case 'u':
            updateRecipe = true;
            break;
        case 'k':
            showKey = true;
            break;
        }
    }

    if (argc == optind && !rebuild && !catDup)
        usage("File(s) required except for -R and -D");

    openDb(rebuild);

    if (catDup)
        showDuplicates();
    while (argc != optind) {
        if (!isFile(argv[optind]))
            ;
        else if (argv[optind][0] == '@') {
            if (updateRecipe)
                reIndexFile(argv[optind], showAlt);
        } else
            printFileInfo(argv[optind], showKey);
        optind++;
    }
    closeDb();
}
