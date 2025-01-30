/****************************************************************************
 *  program: uunirmx - unpack irmx disk images                              *
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
/*
DESCRIPTION
    Unpacks an isis .imd or .img file into the individual files.
    Supports ISIS I/II/III SD & DD, ISIS PDS and ISIS IV
    For ISIS I/II/III and ISIS PDS a recipe file is generated to allow a sister
    application mkidsk to reconstruct the .imd or .img file
    Portions of the code are based on Dave Duffield's imageDisk sources

MODIFICATION HISTORY
    17 Aug 2018 -- original release as unidsk onto github
    18 Aug 2018 -- added attempted extraction of deleted files for ISIS II/III
    19 Aug 2018 -- Changed to use environment variable IFILEREPO for location of
                   file repository. Changed location name to use ^ prefix for
                   repository based files, removing need for ./ prefix for
                   local files
    21 Aug 2018 -- Added support for auto looking up files in the repository
                   the new option -l or -L supresses the lookup i.e. local fiiles

NOTES
    This version relies on visual studio's pack pragma to force structures to be byte
    aligned.
    An alternative would be to use byte arrays and index into these to get the relevant
    data via macros or simple function. This approach would also support big edian data

TODO
    Review the information generated for an ISIS IV disk to see if it is sufficient
    to allow recreation of the original .imd or .img file
*/

#include <assert.h>
#include <direct.h>
#include <malloc.h>
#include <memory.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "utility.h"

// volume header
#define VOLHEADER_SIZE 3328
// ISO$VOL$LABEL offsets
#define LABEL_ID       0  // LABEL$ID(3) BYTE
#define RESERVED_A     3  // RESERVED$A BYTE
#define ISO_VOL_NAME   4  // VOL$NAME(6) BYTE
#define VOL_STRUC      10 // VOL$STRUC BYTE
#define RESERVED_B     11 // RESERVED$B(60) BYTE
#define REC_SIDE       71 // REC$SIDE BYTE
#define RESERVED_C     72 // RESERVED$C(4) BYTE
#define ILEAVE         76 // ILEAVE(2) BYTE
#define RESERVED_D     78 // RESERVED$D BYTE
#define ISO_VERSION    79 // ISO$VERSION BYTE
#define RESERVED_E     80 // RESERVED$E(48) BYTE

// RMX$VOLUME$INFORMATION offsets
#define IRMX_VOL_NAME  0  // VOL$NAME(10) BYTE
#define FILL           10 // FILE BYTE
#define FILE_DRIVER    11 // FILE$DRIVER BYTE
#define VOL_GRAN       12 // VOL$GRAN WORD
#define VOL_SIZE       14 // VOL$SIZE DWORD
#define MAX_FNODE      18 // MAX$FNODE WORD
#define FNODE_START    20 // FNODE$START DWORD
#define FNODE_SIZE     24 // FNODE$SIZE WORD
#define ROOT_FNODE     26 // ROOT$FNODE WORD

// FNODE offsets
#define FLAGS          0  // FLAGS WORD
#define TYPE           2  // TYPE BYTE
#define GRAN           3  // GRAN BYTE
#define OWNER          4  // OWNER WORD
#define CRTIME         6  // CR$TIME DWORD
#define ACCESSTIME     10 // ACCESS$TIME DWORD
#define MODTIME        14 // MOD$TIME DWORD
#define TOTALSIZE      18 // TOTAL$SIZE DWORD
#define TOTALBLKS      22 // TOTAL$BLKS DWORD
#define PTR            26 // PTR(8) STRUCTURE (NUM$BLOCKS WORD, BLK$PTR(3) BYTE)
#define IDCOUNT        74 // IDCOUNT WORD
#define ACCESSOR       76 // ACCESSOR(3) STRUCTURE(ACCESS BYTE, ID WORD)

#define IRMXDATEOFFSET 252460800 // offset in seconds

typedef struct {
    uint16_t nodeId;
    uint16_t flags;
    uint8_t type;
    uint8_t gran;
    uint16_t owner;
    uint32_t crTime;
    uint32_t accessTime;
    uint32_t modTime;
    uint32_t totalSize;
    uint32_t totalBlks;
    struct {
        uint16_t numBlocks;
        uint32_t blkPtr;
    } ptr[8];
    uint16_t idCount;
    struct {
        uint8_t access;
        uint16_t id;
    } accessor[3];
} fnode_t;

bool accountShown;

char *nodeTypes[]    = { "fnode", "volMap", "fnodeMap", "account", "badBlock",
                         "?5?",   "dir",    "?7?",      "data",    "volLabel" };

char *specialFiles[] = { "__fnodes__",     "__volMap__",    "__fnodesMap__",
                         "__accounting__", "__badBlocks__", "__volLabel__" };

char targetPath[_MAX_PATH]; // file/directory name being processed
char irmxPath[_MAX_PATH];   // raw iRMX path name
char *rootPath;             // point where relative root starts in targetPath
uint32_t appendPos;         // offset into irmxPath or rootPath for current file/dir name
char *irmxDir;              // point in irmxPath of current component
char *targetDir;            // equivalent point in targetPath
unsigned pathInvalidChCnt;  // count of mapped chars in irmxPath
unsigned fileInvalidChCnt;  // count of mapped chars in irmx filename

FILE *srcFp;
FILE *dstFp;

uint8_t *rawFnode;

bool debug = false;
uint_fast16_t volGran, maxFnode, fnodeSize;
uint_fast32_t volSize, maxFnode, fnodeStart, rootFnode;
bool shortTrack0;
#define T0HOLE     (15 * 128) // for old floppies with 15 x 128 byte sectors on track 0
#define T0SIZE     (16 * 128) // for old floppies iRMX assumed T0 size for block calculations
#define MINCOPYBUF 0x10000
size_t sizeCopyBuf;
uint8_t *copyBuf;

void enterDir(char const *component);
void exitDir();
void setFileTime(char const *path, fnode_t *node);

void appendComponent(char const *component);

char const help[] = "Usage: %s [-d] file\n"
                    "Where -d shows disk configuration information";

inline bool invalidCh(int c) {
    return c == '?';
    // return strchr("?...", c);
}

/*
    file extract operations reuse a buffer to avoid too many malloc & free cycles
    the buffer grows with each bigger file, otherwise it is reused
    it also has a minimum size MINCOPYBUF (currently 64k)
*/
uint8_t *getCopyBuf(size_t size) {
    if (size > sizeCopyBuf) {
        if (size < MINCOPYBUF) // make sure minimum size
            size = MINCOPYBUF;
        else
            size = (size + volGran - 1) & ~(volGran - 1); // round up to multiple of volGran
        free(copyBuf);
        sizeCopyBuf = size;
        copyBuf     = safeMalloc(sizeCopyBuf);
    }
    return copyBuf;
}

/* routines to extract values from a buffer */
/* expect the compiler to inline these */

uint_fast8_t getByte(uint8_t *buf) {
    return *buf;
}
uint_fast16_t getWord(uint8_t *buf) {
    return buf[0] + (buf[1] << 8);
}

uint_fast32_t getBlkPtr(uint8_t *buf) { // 3 byte value
    return buf[0] + (buf[1] << 8) + (buf[2] << 16);
}

uint_fast32_t getDWord(uint8_t *buf) {
    return buf[0] + (buf[1] << 8) + (buf[2] << 16) + (buf[3] << 24);
}

/*
   convert a blk number into location in the file
   note the handling of a missing 128 byte sector for some floppies is handled in readBlk below
   expected to be inlined
*/
uint32_t blk2Loc(uint32_t blk) {
    return blk * volGran;
}

// it is assumed loc is on a 128 byte boundary
void readBlk(uint8_t *addPt, uint32_t loc, uint32_t len) {
    if (!len)
        return;

    /* the handling of a short track 0 adds some complexity here
       the complex case is where an attempt is made to read data from the
       "hole", which is basically an error, but here it is assumed to read 0
       Due to the packing on the disk, the operation is split into 3 parts
       reading up to the hole, filling with 0 for data from the hole and finally reading
       any data beyond the hole.
    */
    if (shortTrack0 && loc < T0SIZE && loc + len > T0HOLE) {
        warn("Reading from loc %d for %d bytes includes missing track 0 sector 16\n", loc, len);
        uint32_t preHoleSize = loc < T0HOLE ? T0HOLE - loc : 0;
        len -= preHoleSize;
        uint32_t holeSize = len > 128 ? 128 : len;
        len -= holeSize;

        readBlk(addPt, loc, preHoleSize); // read before the hole (uses the code in else block)
        memset(addPt + preHoleSize, 0, holeSize);
        readBlk(addPt + preHoleSize + holeSize, T0SIZE,
                len); // read after the hole (as per pre hole)
    } else {
        /* the simpler case is just to read the data, adjusting for a short track 0 if required */
        uint32_t actual = 0;
        if (shortTrack0 && loc >= T0SIZE)
            loc -= 128;
        if (fseek(srcFp, loc, SEEK_SET))
            warn("cannot access location %d\n", loc);
        else if ((actual = (uint32_t)fread(addPt, 1, len, srcFp)) != len)
            warn("Read error, reading %d bytes at location\n", len, loc);
        if (actual != len)
            memset(addPt + actual, 0, len - actual);
    }
}

#define CHUNK 128

void readBlkIndirect(uint8_t *addPt, uint32_t loc, uint32_t len) {
    uint8_t indirect[CHUNK];
    uint32_t offset = 0;

    while (len) {
        readBlk(indirect, loc, CHUNK);
        for (uint8_t *p = indirect; len && p < indirect + CHUNK; p += 4) {
            uint32_t toRead = getByte(p) * volGran;
            if (toRead == 0) {
                warn("Missing indirect block info for long file, while reading at location %d\n",
                     loc);
                memset(addPt + offset, 0, len);
                len = 0;
            } else {
                if (toRead > len)
                    toRead = len;
                readBlk(addPt + offset, blk2Loc(getBlkPtr(p + 1)), toRead);
                offset += toRead;
                len -= toRead;
            }
        }
        loc += CHUNK;
    }
}

uint8_t *loadFile(fnode_t *fnode, uint8_t *content) {
    if (!content)
        content = safeMalloc(fnode->totalSize);
    uint32_t remaining = fnode->totalSize;
    uint8_t *insertPt  = content;
    for (int i = 0; remaining && i < 8; i++) {
        unsigned numBlks = fnode->ptr[i].numBlocks;

        if (numBlks) {
            uint32_t toRead = numBlks * volGran;
            if (toRead > remaining)
                toRead = remaining;
            uint32_t loc = blk2Loc(fnode->ptr[i].blkPtr);

            if (fnode->flags & 2)
                readBlkIndirect(insertPt, loc, toRead);
            else
                readBlk(insertPt, loc, toRead);
            insertPt += toRead;
            remaining -= toRead;
        }
    }
    return content;
}

char *getUser(uint16_t id) {
    if (id == 0xffff)
        return "world";
    static char user[6];
    sprintf(user, "u%04X", id);
    return user;
}

char *getAccess(uint8_t access) {
    static char keys[5];
    keys[0] = access & 8 ? 'u' : '-';
    keys[1] = access & 4 ? 'a' : '-';
    keys[2] = access & 2 ? 'r' : '-';
    keys[3] = access & 1 ? 'd' : '-';
    keys[4] = 0;
    return keys;
}
void logFnode(fnode_t *fnode) {
    static bool needHeading = true;

    if (needHeading) {
        needHeading = false;
        if (debug)
            logMsg("fnode ");
        logMsg("%-9s %8s ml user  perm user  perm user  perm irmx-path (-> local-path)\n", "type",
                 "size");
    }
    if (debug)
        logMsg("%-5d ", fnode->nodeId);
    if (fnode->type <= 9)
        logMsg("%-9s", nodeTypes[fnode->type]);
    else
        logMsg("?%-8d", fnode->type);
    logMsg(" %8d %c%c", fnode->totalSize, fnode->flags & 0x20 ? 'm' : ' ',
             fnode->flags & 2 ? 'l' : ' ');

    for (int i = 0; i < 3; i++)
        if (i < fnode->idCount)
            logMsg(" %s %s", getUser(fnode->accessor[i].id),
                     getAccess(fnode->accessor[i].access));
        else
            logMsg("%*s", 11, "");

    if (strchr(irmxPath, '?'))
        logMsg(" %s -> %s\n", irmxPath, rootPath);
    else
        logMsg(" %s\n", irmxPath);
}

void saveFile(char *fname, uint8_t *content, uint32_t len) {
    FILE *fp;
    if (!(fp = fopen(fname, "wb")))
        warn("could not create %s\n", fname);
    else {
        if (fwrite(content, 1, len, fp) != len)
            warn("write failure for %s\n", fname);
        fclose(fp);
    }
}

#ifdef _WIN32
#define WIN32LEAN_AND_MEAN
#include <windows.h>
// unfortunately utime does not appear to work with directories in windows
// so using Windows API instead, also allows create time to be set
uint64_t const unixDay0 = 116444736000000000; // 1-Jan-1970 in FILETIME format
void setFileTime(char const *path, fnode_t *node) {

    ULARGE_INTEGER hiresTime = { .QuadPart = unixDay0 + (uint64_t)(node->crTime + IRMXDATEOFFSET) *
                                                            10000000ULL };
    FILETIME cfiletm         = { hiresTime.LowPart, hiresTime.HighPart };
    hiresTime.QuadPart = unixDay0 + (uint64_t)(node->accessTime + IRMXDATEOFFSET) * 10000000ULL;
    FILETIME afiletm   = { hiresTime.LowPart, hiresTime.HighPart };
    hiresTime.QuadPart = unixDay0 + (uint64_t)(node->modTime + IRMXDATEOFFSET) * 10000000ULL;
    FILETIME mfiletm   = { hiresTime.LowPart, hiresTime.HighPart };

    // CreateFile needs wchar path name so convert
    int wsize      = MultiByteToWideChar(CP_ACP, 0, path, -1, NULL, 0);
    wchar_t *wPath = safeMalloc(wsize * sizeof(wchar_t));
    MultiByteToWideChar(CP_ACP, 0, path, -1, wPath, wsize);

    // open the  file to allow update of the attributes
    HANDLE hFile =
        CreateFile(wPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                   OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wPath);
    if (hFile == INVALID_HANDLE_VALUE) { // quietly ignore if we could not open
        return;
    }
    SetFileTime(hFile, &cfiletm, &afiletm, &mfiletm); // set time, quietly ignore errors
    CloseHandle(hFile);
}
#else
// simple version for none windows systems
#include <sys/utime.h>
void setFileTime(char const *path, fnode_t *node) {

    struct utimbuf times = { node->accessTime + IRMXDATEOFFSET, node->modTime + IRMXDATEOFFSET };
    utime(path, &times);
}
#endif

void extractFile(fnode_t *cnode, char *name) {
    appendComponent(name);
    logFnode(cnode);

    saveFile(targetPath, loadFile(cnode, getCopyBuf(cnode->totalSize)), cnode->totalSize);
    setFileTime(targetPath, cnode);
}

bool readFnode(uint_fast16_t nodeId, fnode_t *fnode) {

    if (!rawFnode) {
        if (shortTrack0 && fnodeStart > T0SIZE)
            fnodeStart -= 128;
        rawFnode = safeMalloc(fnodeSize * maxFnode);
        if (fseek(srcFp, fnodeStart, SEEK_SET) ||
            fread(rawFnode, fnodeSize, maxFnode, srcFp) != maxFnode)
            fatal("couldn't load fnodes\n");
        for (uint32_t i = 0; i < maxFnode; i++)
            rawFnode[i * fnodeSize] &=
                0xe7; // make sure bits 3,4 of flag are clear so we can use them
    }

    if (nodeId >= maxFnode) {
        warn("Failed to read fnode %d\n", nodeId);
        return false;
    }
    uint8_t *p    = rawFnode + nodeId * fnodeSize;
    fnode->nodeId = nodeId;
    fnode->flags  = getWord(p + FLAGS);
    p[FLAGS] |= 0x8; // flag as processed
    fnode->type       = getByte(p + TYPE);
    fnode->gran       = getByte(p + GRAN);
    fnode->owner      = getWord(p + OWNER);
    fnode->crTime     = getDWord(p + CRTIME);
    fnode->accessTime = getDWord(p + ACCESSTIME);
    fnode->modTime    = getDWord(p + MODTIME);
    fnode->totalSize  = getDWord(p + TOTALSIZE);
    fnode->totalBlks  = getDWord(p + TOTALBLKS);
    for (int i = 0; i < 8; i++) {
        fnode->ptr[i].numBlocks = getWord(p + PTR + i * 5);
        fnode->ptr[i].blkPtr    = getBlkPtr(p + PTR + 2 + i * 5);
    }
    fnode->idCount = getWord(p + IDCOUNT);
    for (int i = 0; i < 3; i++) {
        fnode->accessor[i].access = getByte(p + ACCESSOR + i * 3);
        fnode->accessor[i].id     = getWord(p + ACCESSOR + 1 + i * 3);
    }

    /*
       Note the fnode gran size is allowed to be any value but defaults to 1
       In the implementation I have assumed that the ptr info uses the volGran and the
       fnode gran is more of user policy, possibly related to totalBlocks/thisSize usage or
       preallocation strategies  for realtime data handling.
       Without samples I not able to verify this
    */
    if (fnode->gran != 1)
        warn("Unverified behaviour as fnode Gran size is not 1\n");
    if (!(fnode->flags & 1))
        warn("Fnode %d referenced but it is marked as unallocated\n", nodeId);
    return true;
}

void irmxDirectory(fnode_t *dnode, char const *dirPath) {
    enterDir(dirPath);

    logFnode(dnode);

    uint8_t *directory = loadFile(dnode, NULL);
    appendComponent("__dir__"); /* save the raw directory info */
    saveFile(targetPath, directory, dnode->totalSize);
    setFileTime(targetPath, dnode);

    for (uint8_t *p = directory; p < directory + dnode->totalSize; p += 16) {
        uint_fast16_t node = getWord(p);
        if (node) {
            char component[15];
            strncpy(component, p + 2, 14);
            component[14] = '\0';

            fnode_t cnode;
            if (!readFnode(node, &cnode))
                warn("failed to read component %s fnode %d\n", component, cnode);
            else {
                if (cnode.type == 6)
                    irmxDirectory(&cnode, component);
                else
                    extractFile(&cnode, component);
            }
        }
    }
    appendComponent(".");
    setFileTime(targetPath, dnode);
    exitDir();
    free(directory);
}

bool nonBlank(uint8_t *p, unsigned len) {
    while (len--)
        if (*p++ != ' ')
            return true;
    return false;
}

void irmxConfig(FILE *fp) {
    uint8_t isoLabel[128];
    uint8_t irmxLabel[128];

    fseek(fp, 0, SEEK_END);

    uint_fast32_t fileSize = ftell(fp);

    if (fseek(fp, 768, SEEK_SET) != 0 || fread(isoLabel, 1, 128, fp) != 128 ||
        fseek(fp, 384, SEEK_SET) != 0 || fread(irmxLabel, 1, 128, fp) != 128)
        fatal("Failed to read ISO and iRMX labels\n");

    volGran     = getWord(irmxLabel + VOL_GRAN);
    volSize     = getDWord(irmxLabel + VOL_SIZE);
    maxFnode    = getWord(irmxLabel + MAX_FNODE);
    fnodeStart  = getDWord(irmxLabel + FNODE_START);
    fnodeSize   = getWord(irmxLabel + FNODE_SIZE);
    rootFnode   = getWord(irmxLabel + ROOT_FNODE);
    shortTrack0 = volSize == fileSize + 128; // floppy disk may have track 0 with 15, 128 byte
                                             // sectors leading to a short block on track 0

    if (debug) {
        printf("ISO: ");
        if (memcmp(isoLabel, "VOL", 3) != 0)
            printf(" label$id=%.3s", isoLabel);
        if (isoLabel[RESERVED_A] != '1')
            printf(" reserved$A=%c", isoLabel[RESERVED_A]);
        if (nonBlank(isoLabel + ISO_VOL_NAME, 6))
            printf(" vol$name='%.6s'", isoLabel + ISO_VOL_NAME);
        if (isoLabel[VOL_STRUC] != 'N')
            printf(" vol$struc=%c", isoLabel[VOL_STRUC]);
        if (nonBlank(isoLabel + RESERVED_B, 60))
            printf(" reserved$B non-blank");
        if (isoLabel[REC_SIDE] != '1')
            printf(" rec$side=%c", isoLabel[REC_SIDE]);
        if (nonBlank(isoLabel + RESERVED_C, 4))
            printf(" reserved$C='%.4s'", isoLabel + RESERVED_C);
        printf(" ileave=%.2s", isoLabel + ILEAVE);
        if (isoLabel[RESERVED_D] != ' ')
            printf(" reserved$D='%c'", isoLabel[RESERVED_D]);
        if (isoLabel[ISO_VERSION] != '1')
            printf(" iso$version=%c", isoLabel[ISO_VERSION]);
        if (nonBlank(isoLabel + RESERVED_E, 48))
            printf(" reserved$E non-blank");

        printf("\niRMX:");
        printf(" volName='%.10s'", irmxLabel + IRMX_VOL_NAME);
        if (irmxLabel[FILL])
            printf(" fill=%d", irmxLabel[FILL]);
        if (irmxLabel[FILE_DRIVER] != 4)
            printf(" file$driver=%d", irmxLabel[FILE_DRIVER]);
        printf(" volGran=%d", volGran);
        printf(" volSize=%d", volSize);
        printf("\n      maxFnode=%d", maxFnode);
        printf(" fnodeStart=%d", fnodeStart);
        printf(" fnodeSize=%d", fnodeSize);
        printf(" rootFnode=%d\n", rootFnode);
        if (shortTrack0)
            printf("Short track 0\n");
    } else {
        if (nonBlank(isoLabel + ISO_VOL_NAME, 6))
            printf("ISO volName='%.6s', ", isoLabel + ISO_VOL_NAME);
        printf("iRMX volName='%.10s'\n", irmxLabel + IRMX_VOL_NAME);
    }
    if (memcmp(isoLabel, "VOL1", 4) != 0 || isoLabel[VOL_STRUC] != 'N' ||
        isoLabel[ISO_VERSION] != '1')
        fatal("bad ISO Label\n");

    if (irmxLabel[FILE_DRIVER] != 4 || (!shortTrack0 && volSize != fileSize) || volGran == 0 ||
        (volGran & (volGran - 1)) != 0 || rootFnode >= maxFnode || fnodeStart >= volSize)
        fatal("bad iRMX label\n");
}

/*
    dump reserved sections
*/
void dumpReserved(FILE *fp) {
    uint8_t tmp[VOLHEADER_SIZE];
    if (fseek(fp, 0L, SEEK_SET) == 0 && fread(tmp, 1, VOLHEADER_SIZE, fp) == VOLHEADER_SIZE) {
        appendComponent("__boot__");
        saveFile(targetPath, tmp, VOLHEADER_SIZE);
        logMsg("-     %-9s %8d    ----- ----%22s __boot__\n", "system", VOLHEADER_SIZE, "");
    } else
        fprintf(stderr, "can't read volume header\n");
}

/*
    create the directory for the root of the extracted files
    note filename will already have been opened so is a file not a directory
*/
void mkRoot(char const *fileName) {
    strcpy(targetPath, fileName);

    char *s;
    struct stat sbuf;
    // remove any extent
    if (!(s = strrchr(targetPath, '.')) || strpbrk(s, "/\\"))
        s = strchr(targetPath, '\0');

    *s          = '\0';
    char ext[3] = "";
    for (;;) {
        strcpy(s, ext);
        if (mkdir(targetPath) == 0 ||
            (errno == EEXIST && stat(targetPath, &sbuf) == 0 && (sbuf.st_mode & S_IFDIR)))
            break;
        if (!*ext)
            strcpy(ext, "-a");
        else if (++ext[1] > 'z') {
            *s = '\0';
            fatal("can't create a root directory based on %s\n", targetPath);
        }
    }
    rootPath = strchr(s, '\0');
    strcpy(rootPath, "/");
    strcpy(irmxPath, "/");
    appendPos = 1;
}

void appendComponent(char const *component) {
    strcpy(irmxPath + appendPos, component);
    strcpy(rootPath + appendPos, component);
    for (char *s = rootPath + appendPos; s = strchr(s, '?'); s++)
        *s = '!';
}

void enterDir(char const *component) {
    if (*component) { /* note root directory already exists*/
        appendComponent(component);
        if (mkdir(targetPath) != 0 && errno != EEXIST)
            fatal("failed to create directory %s\n", targetPath);
        strcat(irmxPath + appendPos, "/");
        strcat(rootPath + appendPos, "/");
        appendPos = (uint32_t)strlen(irmxPath);
    } else {
        irmxPath[appendPos] = '\0';
        rootPath[appendPos] = '\0';
    }
}

void exitDir() {
    if (appendPos > 1) {
        irmxPath[--appendPos] = '\0';   // remove directory /
        appendPos             = (uint32_t)(strrchr(irmxPath, '/') - irmxPath + 1);
    }
    irmxPath[appendPos] = '\0';
    rootPath[appendPos]   = '\0';
}

void main(int argc, char **argv) {

    while (getopt(argc, argv, "d") != EOF) {
        if (optopt == 'd')
            debug = true;
    }

    if (optind != argc - 1)
        usage("Missing file to process");

    if (!(srcFp = fopen(argv[optind], "rb")))
        fatal("can't open file %s\n", argv[optind]);

    irmxConfig(srcFp);

    /* make a directory path from the name*/
    mkRoot(argv[optind]);
    printf("Extracted files can be found under %s\n\n", targetPath);

    appendComponent("__log__");
    openLog(targetPath);

    fnode_t aNode;
    if (!readFnode(rootFnode, &aNode))
        fatal("Cannot read root node %d\n", rootFnode);

    irmxDirectory(&aNode, "");
    logMsg("Unattached files\n");
    for (uint32_t i = 0; i < maxFnode; i++)
        if ((rawFnode[i * fnodeSize] & 9) == 1 && readFnode(i, &aNode)) {
            if (i < 6)
                extractFile(&aNode, specialFiles[i]);
            else {
                char tmp[14];
                sprintf(tmp, "__fnode%d__", i);
                extractFile(&aNode, tmp);
            }
        }
    dumpReserved(srcFp);
    fclose(srcFp);
    closeLog();
}
