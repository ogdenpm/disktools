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
#include "unidsk.h"

#define CATLOGNAME  "catalog.db"

typedef unsigned char byte;
bool cacheValid = false;

#define HashSize    4409        // 600th prime
#define ChecksumSize    27      // base 64 SHA1 hash length
#define PERLSEP 0x1c            // used to separate isisname and checksum in the repository

typedef struct _locInfo {
    struct _locInfo *link;
    char *isisName;          // 6.3
    char locations[2];          // string, 0, 0
} locInfo_t;

typedef struct _hlist {
    struct _hlist *link;
    byte checksum[ChecksumSize];
    locInfo_t *nameLocs;
} hlist_t;

hlist_t *hashTable[HashSize];       // will init to 0

unsigned hash(char *s) {
    unsigned val = 0;
    while (*s)
        val = val * 33 + *s++;
    return val % HashSize;
}

void AddLocInfo(char *isisName, char *checksum, char *locations) {
    unsigned hval = hash(checksum);
    hlist_t *hp;
    locInfo_t *lp;
    char *s;

    // see if checksum entry exists
    for (hp = hashTable[hval]; hp; hp = hp->link)
        if (memcmp(hp->checksum, checksum, ChecksumSize) == 0)
            break;
    if (!hp) {            // not found so insert at front of chain
        if (!(hp = (hlist_t *)malloc(sizeof(hlist_t)))) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        hp->link = hashTable[hval];
        hashTable[hval] = hp;
        memcpy(hp->checksum, checksum, ChecksumSize);
        hp->nameLocs = NULL;
    }
    if (!(lp = (locInfo_t *)malloc(sizeof(locInfo_t) + strlen(locations)))) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    lp->isisName = _strdup(isisName);
    strcpy(lp->locations, locations);
    *(strchr(lp->locations, 0) + 1) = 0;        // append an extra 0
    s = lp->locations;
    while (s = strchr(s, ':'))      // replace the : with 0 throughout
        *s++ = 0;
    lp->link = hp->nameLocs;
    hp->nameLocs = lp;
}

void loadCache() {
    FILE *fp;
    char line[MAXINLINE];
    char catalog[_MAX_PATH];
    char *name, *checksum, *locations;
    char *s = getenv("IFILEREPO");

    if (!s)
        s = "";
    strcpy(catalog, s);
    s = strchr(catalog, 0);   // s -> end of string
    // if cache name not blank make sure it has trailing directory separator
    if (s != catalog && s[-1] != '/' && s[-1] != '\\')
        strcat(s, "/");

    strcat(catalog, CATLOGNAME);
    if ((fp = fopen(catalog, "rt")) == NULL) {
        fprintf(stderr, "Warning catalog database not found, check IFILEREPO is set or use -l option\n");
        return;
    }
    while (fgets(line, MAXINLINE, fp)) {
        if (s = strchr(line, '\n'))
            *s = 0;
        else {
            int c;
            fprintf(stderr, "Warning line too long, truncating");
            while ((c = getc(fp)) != EOF && c != '\n')
                ;
        }
        name = line;
        locations = NULL;
        if ((checksum = strchr(line, PERLSEP)) && checksum != line)
            locations = strchr(checksum + 1, ':');
        if (!locations || locations - 1 - checksum != ChecksumSize) {
            fprintf(stderr, "Ignoring invalid line %s\n", line);
            continue;
        }
        *checksum++ = 0;      // remove PERLSEP;
        *locations++ = 0;     // and leading :
//        if (strlen(name) <= 10)     // if not bothered with intel86 names
        AddLocInfo(name, checksum, locations);
    }
    cacheValid = true;
}

locInfo_t *Lookup(char *name, char *checksum) {
    unsigned hval = hash(checksum);
    hlist_t *hp;
    locInfo_t *lp;

    for (hp = hashTable[hval]; hp; hp = hp->link) {
        if (memcmp(hp->checksum, checksum, ChecksumSize) == 0)
            break;
    }
    if (hp == NULL)
        return NULL;
    if (name == NULL)
        return hp->nameLocs;

    for (lp = hp->nameLocs; lp; lp = lp->link) {
        if (_stricmp(lp->isisName, name) == 0)
            return lp;
    }
    return NULL;
}

/*
    looks up the directory entry in the repo
    returns a pointer to the location or NULL if no location
    entry is the pointer to the directory information
    if NULL the location is the next aliased path if available
    once all aliases have been returned NULL is returned
*/
char *Dblookup(isisDir_t *entry) {
    static locInfo_t *lp;
    static bool all;        // true if we return all aliases even with different isis names
    static char *lastLoc;

    if (!cacheValid)
        return NULL;
    if (entry) {
        all = false;
        if (lp = Lookup(entry->name, entry->checksum))
            return lastLoc = lp->locations;
        else if ((lp = Lookup(NULL, entry->checksum))) {    // we didn't find so see if a file matches anywhere
            all = true;;
            return lastLoc = lp->locations;
        }
        else
            return lastLoc = NULL;
    }
    if (!lp || !lastLoc || *lastLoc == 0)
        return NULL;
    lastLoc = strchr(lastLoc, 0) + 1;               // start of next string
    if (*lastLoc || !all)
        return lastLoc;
    lp = lp->link;
    lastLoc = lp ? lp->locations : NULL;
    return lastLoc;
}