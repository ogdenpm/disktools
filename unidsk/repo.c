/*
    repo.c     (c) by Mark Ogden 2018

DESCRIPTION
    Handles file lookup in the file repository
    Part of unidsk

MODIFICATION HISTORY
    21 Aug 2018 -- original release as unidsk onto github
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "unidsk.h"


typedef unsigned char byte;
bool cacheValid = false;

#define HashSize    4409        // 600th prime




typedef struct _loc {           // cut down version from irepo as don't hash on loc
    struct _loc *nchain;
    char loc[1];
} loc_t;




const char *fileName(const char *loc) {
    const char *s = strrchr(loc, '/');
    if (s)
        loc = s + 1;
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
        return strlen(fileName(loc)) - strlen(iname) + 1;
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


