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

#include "sha1.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _MSC_VER
#include "_dirent.h"
#else
#include <dirent.h>
#include <limits.h>
#define _MAX_PATH   PATH_MAX
#endif

#include "db.h"
#include "utility.h"

#define CATLOGNAME "ncatalog.db"

#define HashSize   7793 // largest prime under 8,000
#define MAXLINE    256  // split very long lines

// database is a text file stored as
// sha1key:location[:location]*
// very long lines are split, each line starting with the sha1key
// Note. locations are relative to path specified in IFILEREPO environment var

typedef struct _key {
    struct _key *hchain;
    struct _loc *nchain; // start of chain of locs with same key
    Key key;
} dbkey_t;

typedef struct _loc {
    struct _loc *hchain;
    struct _loc *nchain; // next loc with same key
    dbkey_t *pKey;         // points to key entry for this location
    char loc[1];
} loc_t;

static char repoPath[_MAX_PATH]; // contains the path of the current file being processed
const char *repoFile;
bool dbIsOpen = false;

static dbkey_t *keyHashTable[HashSize];
static loc_t *locHashTable[HashSize];

static void loadDb(char *fpart);
static dbkey_t *lookupKey(const KeyPtr key);
static loc_t *lookupLoc(const char *loc);

static void rebuildDb(char *fpart, ...);
static void rebuildTree(char *fpart);
static void saveDb(char *fpart);


#define BLOCKSIZE 0x10000 // chunk size to load files in for sha1 calculation

static int keyIterI = -1;
static dbkey_t *keyIter;
static loc_t *locIter;

// public functions

// free up memory used for the database
void closeDb() {
    for (unsigned i = 0; i < HashSize; i++) {
        loc_t *lq;
        for (loc_t *lp = locHashTable[i]; lp; lp = lq) {
            lq = lp->hchain;
            free(lp);
        }
        dbkey_t *cq;
        for (dbkey_t *cp = keyHashTable[i]; cp; cp = cq) {
            cq = cp->hchain;
            free(cp);
        }
    }
    memset(keyHashTable, 0, sizeof(keyHashTable));
    memset(locHashTable, 0, sizeof(locHashTable));
    dbIsOpen = false;
}

KeyPtr firstKey() {
    for (keyIterI = 0; keyIterI < HashSize; keyIterI++)
        if ((keyIter = keyHashTable[keyIterI]))
            return keyIter->key;
    keyIterI = -1;
    return NULL;
}

char *firstLoc(KeyPtr key) {
    dbkey_t *kp;
    if (!key || !(kp = lookupKey(key)))
        return NULL;
    locIter = kp->nchain;
    return locIter->loc;
}

// calculate the key for a file
// returns length of file or -1 if not found
int getFileKey(const char *fname, KeyPtr key) {
    static uint8_t fileblock[BLOCKSIZE]; // buffer used to load file for key calculation
    static char enc[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    FILE *fp;
    SHA1Context ctx;
    int flen = 0;
    if ((fp = fopen(fname, "rb"))) {
        int len = BLOCKSIZE;
        // generate the SHA1 key
        SHA1Reset(&ctx);
        while (len == BLOCKSIZE) {
            flen += len = (int)fread(fileblock, 1, len, fp);
            if (len > 0)
                SHA1Input(&ctx, fileblock, len);
        }
        fclose(fp);

        uint8_t sha1[SHA1HashSize + 1];

        SHA1Result(&ctx, sha1);
        sha1[SHA1HashSize] = 0; // 0 pad to triple boundary

        // convert to enc64 format
        for (int i = 0; i < SHA1HashSize; i += 3) {
            int triple = (sha1[i] << 16) + (sha1[i + 1] << 8) + sha1[i + 2];
            for (int j = 0; j < 4; j++)
                *key++ = enc[(triple >> (3 - j) * 6) & 0x3f];
        }
        *--key = 0; // replace 28th char with 0;

        return flen;
    }
    strcpy(key, "* file not found *");
    return -1;
}

const char *getDbLoc(const char *loc) {
    loc_t *lp = lookupLoc(loc);
    return lp ? lp->loc : NULL;
}

KeyPtr getDbLocKey(const char *loc) {
    loc_t *lp = lookupLoc(loc);
    return lp ? lp->pKey->key : NULL;
}

bool isDir(const char *fname) {
    struct stat _stat;
    return stat(fname, &_stat) == 0 && (_stat.st_mode & S_IFDIR);
}

bool isFile(const char *fname) {
    struct stat _stat;
    return stat(fname, &_stat) == 0 && (_stat.st_mode & S_IFREG);
}

// checks whether key looks valid
// check ends on end of string or colon/comma separator
bool isValidKey(const KeyPtr key) {
    const char *s;
    for (s = key; isalnum(*s) || *s == '+' || *s == '/'; s++)
        ;
    return (*s == 0 || *s == ':' || *s == ',') && s - key == KEYSIZE;
}

bool keyExists(const KeyPtr key) {
    return lookupKey(key) != NULL;
}

bool isValidPair(const KeyPtr key, const char *loc) {
    loc_t *lp = lookupLoc(loc);
    return strcmp(lp->pKey->key, key) == 0;
}

KeyPtr nextKey() {
    if (keyIter && (keyIter = keyIter->hchain))
        return keyIter->key;
    if (keyIterI < 0)
        return NULL;
    for (keyIterI++; keyIterI < HashSize; keyIterI++)
        if ((keyIter = keyHashTable[keyIterI]))
            return keyIter->key;
    keyIterI = -1;
    return NULL;
}

char *nextLoc() {
    if (locIter && (locIter = locIter->nchain))
        return locIter->loc;
    return NULL;
}

// load in the database, if rebuild is true regenerate
// will abort if IFILEREPO is not defined called functions will abort if it is invalid
void openDb(bool rebuild) {
    if (dbIsOpen)
        closeDb();

    char *s = getenv("IFILEREPO");
    if (!s)
        fatal("IFILEREPO not defined");
    strcpy(repoPath, s);
#ifdef _WIN32
    for (char *t = repoPath; (t = strchr(t, '\\'));t++) // convert to common format
        *t = '/';
#endif
    char *fpart = strchr(repoPath, '\0');         // fpart is where file name is added onto repoPath
    while (fpart != repoPath && fpart[-1] == '/') // remove trailing /
        *--fpart = 0;

    if (rebuild)
        rebuildDb(fpart, "Intel80", "Intel86", NULL);
    else
        loadDb(fpart);
}

unsigned hash(const char *key) { // generate hash value from string

   uint32_t h = 3323198485ul;
    for (; *key; ++key) {
        h ^= *key;
        h *= 0x5bd1e995;
        h ^= h >> 15;
    }
    return h % HashSize;
}

static dbkey_t *lookupKey(const KeyPtr key) {
    for (dbkey_t *hp = keyHashTable[hash(key)]; hp; hp = hp->hchain)
        if (strcmp(hp->key, key) == 0)
            return hp;
    return NULL;
}

static loc_t *lookupLoc(const char *loc) {
    for (loc_t *hp = locHashTable[hash(loc)]; hp; hp = hp->hchain) {
        if (strcmp(hp->loc, loc) == 0)
            return hp;
    }
    return NULL;
}

void insertItem(const KeyPtr key, const char *loc) {
    // check for basic insert error
    loc_t *lp = lookupLoc(loc); // should not find
    if (lp) {                   // but if it does report if different, else ignore
        if (strcmp(lp->pKey->key, key) != 0)
            warn("InsertItem, loc (%s) with different keys (%s) (%s)", loc, lp->pKey->key, key);
        return;
    }
    // first install in the checksum based hash lists
    dbkey_t *kp = lookupKey(key);
    if (!kp) {
        kp = safeMalloc(sizeof(dbkey_t));
        strcpy(kp->key, key);
        unsigned hval      = hash(key);
        kp->hchain         = keyHashTable[hval]; // install in hash table
        keyHashTable[hval] = kp;
        kp->nchain         = NULL;
    }
    // now install in the loc based hash lists
    unsigned hval      = hash(loc);
    lp                 = safeMalloc(sizeof(loc_t) + strlen(loc));
    lp->hchain         = locHashTable[hval]; // insert in loc hash chain
    locHashTable[hval] = lp;
    lp->nchain         = kp->nchain; // put at head of location name chain
    kp->nchain         = lp;
    lp->pKey           = kp; // insert ref back to key
    strcpy(lp->loc, loc);    // insert the name
}

// the database load/rebuild/save routines implementations
// load from file
static void loadDb(char *fpart) {
    FILE *fp;
    strcat(fpart, "/" CATLOGNAME);

    if ((fp = fopen(repoPath, "rt")) == NULL)
        fatal("Cannot locate catalog at %s", repoPath);

    char line[MAXLINE];
    char *key;
    char *loc;

    while (fgets(line, MAXLINE, fp)) {
        // check for full line
        char *s = strchr(line, '\n');
        if (!s) {
            warn("Corrupt catalog, truncated line %s", line);
            int c;
            while ((c = getc(fp)) != '\n' && c != EOF)
                ;
            continue;
        }
        *s = ':'; // replace '\n' with :
        // check key is reasonable
        key = line;
        if (!isValidKey(key) || !(s = strchr(key, ':'))) {
            warn("Corrupt catalog line %s", line);
            continue;
        }
        *s = 0;
        for (loc = s + 1; (s = strchr(loc, ':')); loc = s + 1) {
            if (s != loc) { // ignore blank entries
                *s = 0;
                insertItem(key, loc);
            }
        }
    }
    fclose(fp);
    dbIsOpen = true;
}

// rebuild top level allowing multiple trees to be scanned
static void rebuildDb(char *fpart, ...) {
    va_list dirs;
    va_start(dirs, fpart);
    char *dname;
    *fpart = '/'; // append the directory separator
    repoFile = fpart + 1;
    while ((dname = va_arg(dirs, char *))) {
        strcpy(fpart + 1, dname);
        rebuildTree(strchr(fpart, '\0'));
    }
    saveDb(fpart);
}

// the recursive routine that walks the tree and loads the data
static void rebuildTree(char *fpart) {

    DIR *curdir = opendir(repoPath);
    if (!curdir) {
        warn("Couldn't open directory %s", repoPath);
        return;
    }
    printf("%s\n", repoFile); // for progress reporting

    *fpart = '/';

    struct dirent *dentry;

    while ((dentry = readdir(curdir))) {
        if (strcmp(dentry->d_name, ".") == 0 || strcmp(dentry->d_name, "..") == 0)
            continue;
        strcpy(fpart + 1, dentry->d_name);
        if (isDir(repoPath))
            rebuildTree(strchr(fpart, '\0'));
        else {
            Key key;
            if (getFileKey(repoPath, key) > 0)
                insertItem(key, repoFile);
        }
    }

    closedir(curdir);
}

// routine to save the rebuilt database
// as this is private don't use the public iterators
static void saveDb(char *fpart) {
    FILE *fp;
    int uniqueCnt = 0;
    int fileCnt   = 0;
    strcpy(fpart, "/" CATLOGNAME);

    if ((fp = fopen(repoPath, "wt")) == NULL)
        fatal("Cannot create catalog at %s", repoPath);

    for (unsigned i = 0; i < HashSize; i++) {
        for (dbkey_t *kp = keyHashTable[i]; kp; kp = kp->hchain) {
            uniqueCnt++;
            int linelen = fprintf(fp, "%s", kp->key);
            for (loc_t *np = kp->nchain; np; np = np->nchain) {
                if (linelen + strlen(np->loc) + 2 >= MAXLINE) {
                    putc('\n', fp);
                    linelen = fprintf(fp, "%s", kp->key);
                }
                linelen += fprintf(fp, ":%s", np->loc);
                fileCnt++;
            }
            putc('\n', fp);
        }
    }
    fclose(fp);
    printf("Catalog updated: %d files, %d unique keys\n\n", fileCnt, uniqueCnt);
}

