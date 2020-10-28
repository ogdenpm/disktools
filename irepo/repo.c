#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "irepo.h"

bool isSpecial(const char *name) {
    static char *special[] = { "AUTO", "ZERO", "ZEROHDR", NULL };
    for (int i = 0; special[i]; i++)
        if (strcmp(name, special[i]) == 0)
            return true;
    return false;
}


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



const char *fileName(const char *loc) {
    const char *s = strrchr(loc, '/');
    if (s)
        loc =  s + 1;
    if (s = strrchr(loc, '\\'))
        return s + 1;
    return loc;
}


// return the ISIS compatible prefix of the filename part of the loc path
// i.e. upto 6.3 alphanumeric name, coverted to uppercase
char *mkIname(const char *loc) {
    static char iname[11];
    loc = fileName(loc);
    char *s = iname;

    for (int i = 0; i < 6 && isalnum(*loc);)
        *s++ = toupper(*loc++);
    if (*loc++ == '.' && isalnum(*loc)) {
        *s++ = '.';
        for (int i = 0; i < 3 && isalnum(*loc);)
            *s++ = toupper(*loc++);
    }
    *s = 0;
    return iname;
}


// compare the location and iname
// only the ISIS compatible prefix of the location filename is used see mkIname

int cmpIname(const char *loc, const char *iname) {
    const char *fname = mkIname(loc);

    if (_stricmp(iname, fname) == 0)
        return (int)(strlen(fileName(loc)) - strlen(iname) + 1);
    return 0;
}

// finds a location with a filename part match to iname, or any name if iname is NULL
// exact match will return immediately
// if not the match with the shortest filename (then shortest path) is chosen 
char *findMatch(const KeyPtr key, const char *iname) {
    char *loc = firstLoc(key);

    char *candidate = NULL;
    int clen;

    while (loc) {
        int cmp;
        if (!iname)
            cmp = (int)strlen(fileName(loc));
        else if ((cmp = cmpIname(loc, iname)) == 1)
            return loc;

        if (cmp && (!candidate || cmp < clen || (cmp == clen && strlen(loc) < strlen(candidate)))) {
            candidate = loc;
            clen = cmp;
        }
        loc = nextLoc();
    }

    return candidate;
}

bool isAlt(const char *loc, const char *iname) {
    return !iname || cmpIname(loc, iname) > 0;

}


// print entry, returns true if changed
bool printEntry(FILE *fp, const char *iname, const char *attrib, KeyPtr key,  const char *loc, bool showAlt) {
    Key newkey;
    bool inRepo = false;
    bool changed = false;
    const char *altname = iname;
    const char *newloc = NULL;

    if (isSpecial(loc)) {    // special options ingore checksum etc
        showAlt = false;
        if (*key && *key != '*') {
            key = "";
            changed++;
        }
    } else {
        const char *lp = NULL;
        if (*loc == '^')
            if (lp = getDbLoc(++loc)) {       // if invalid repo ref force recalc
                inRepo = true;
                loc = lp;
            } else {
                fprintf(fp, "# Next entry: ^%s moved or no longer in repository\n", loc);
                fprintf(stderr, "Warning: ^%s moved or no longer in repository\n", loc);
                changed = true;
                loc = iname;
            }
        if (isValidKey(key)) {                                  // key is most significant indicator
            if (!inRepo || !isValidPair(key, loc)) {            // not valid repo entry
                inRepo = (newloc = findMatch(key, altname)) ||       // candidate found (iname match or any name match)
                    (newloc = findMatch(key, altname = NULL));
                if (inRepo) {
                    loc = newloc;
                    changed = true;
                }
            }
        } else if (inRepo) {                            // if we have file in repo get it's checksum
            key = getDbLocKey(loc);
            changed = true;
        }
        
        if (!inRepo) {                                  // if not in repo and file exists use it information to override key
            if (*loc == '*')                            // old comment format, replace with name
                loc = iname;
            if (*key != '*') {
                switch (getFileKey(loc, newkey)) {
                case -1:
                    key = "* file not found *";
                    fprintf(fp, "# Next entry: no match to key: %s or location %s\n", key, loc);
                    fprintf(stderr, "Warning: no match to key: %s or location %s\n", key, loc);
                    break;
                case 0:
                    loc = "ZERO";
                    key = "";
                    break;
                default:
                    changed |= strcmp(key, newkey) != 0;
                    key = newkey;
                }
            }
        }

    }


    fprintf(fp, "%s,%s,%s,%s%s\n", iname, attrib, key, inRepo ? "^" : "", loc);

    if (showAlt && inRepo) {
        for (char *lp = firstLoc(key); lp; lp = nextLoc())
            if (lp != loc && isAlt(lp, altname)) {
                fprintf(fp, "# also ^%s\n", lp);
                changed = true;
            }
    }
    return changed;
}