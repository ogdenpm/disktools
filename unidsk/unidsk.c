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
#include <ctype.h>
#include <malloc.h>
#include <memory.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#include <direct.h>
#define mkdir(p, mode) _mkdir(p)
#else
#include <errno.h>
#include <sys/stat.h>
int unlink(const char *pathname);   // unistd.h conflicts with my getopt
int chdir(const char *path);
#endif
#include "unidsk.h"
#include "utility.h"
#include <stdbool.h>
#include <stdint.h>


char const help[] = { "Usage: %s [-l] [-d] file\n"
                      "Where\n"
                      "  -l  don't lookup in the repository\n"
                      "  -d  debug - shows link info\n" };

char targetPath[_MAX_PATH];

bool isDOS = false; // set to true if ISIS named DOS

isisDir_t isisDir[MAXDIR];
int dirIdx;
int osIdx = -1;
char comment[MAXCOMMENT];

int diskType;
FILE *logfp;
bool showRdError   = true;
int rdErrorCnt     = 0;
int recoveredFiles = 0;

bool debug         = false;
bool pad           = false;

#define NIFILES 10

typedef struct {
    unsigned char type;
    union {
        unsigned char fillCh;
        unsigned char *data;
    };
} SECTOR;

typedef struct {
    unsigned char Mode,                // Data rate/density
        Cyl,                           // Current cylinder
        Head,                          // Current head
        Nsec;                          // Number of sectors
    unsigned Size;                     // Sector size
    unsigned char startSec,            // starting sector
        Hflag;                         // extra bits embedded in Head
    unsigned char Smap[MAXSECTOR + 1]; // Sector numbering map
    unsigned char Cmap[MAXSECTOR + 1]; // Cylinder numbering map
    unsigned char Hmap[MAXSECTOR + 1]; // Head numbering map
    uint64_t Smissing;                 // bit set if sector is missing
    uint8_t *buf;                      // de interlaced track buffer
} TRACK;                               // Sector type flags

int maxCylinder;

TRACK *disk[MAXCYLINDER][MAXHEAD];
uint64_t sectorMissing[MAXCYLINDER][MAXHEAD]; // bit set if sector is missing

// Mode (data rate) index to value conversion table
unsigned Mtext[] = { 500, 300, 250 };

// Encoded sector size to actual size conversion table
unsigned xsize[] = { 128, 256, 512, 1024, 2048, 4096, 8192 };
uint8_t transferBuf[8192]; // for imd data is loaded here first incase sector id is bad

// Table for mode translation
unsigned char Tmode[] = { 0, 1, 2, 3, 4, 5 };

unsigned char Fill,
    Dam   = 255, // Deleted address marks
    Bad   = 255; // Convert bad sectors

int heads = 1;

#pragma pack(push, 1)
typedef struct {
    uint16_t flags;
    uint8_t type;
    uint8_t gran;
    uint16_t owner;
    uint32_t crTime;
    uint32_t accessTime;
    uint32_t modTime;
    uint32_t totalSize;
    uint32_t totalBlocks;
    struct {
        uint16_t numBlocks;
        uint8_t blkPtr[3];
    } ptr[8];
    uint32_t thisSize;
    uint16_t reserverdA;
    uint16_t reserverdB;
    uint16_t idCount;
    struct {
        uint8_t access;
        uint16_t id;
    } accessor[3];
    uint16_t parent;
    uint8_t aux[3];
} fnode_t;

typedef struct {
    uint8_t status;
    char name[6];
    char ext[3];
    uint8_t attributes;
    uint8_t lastblksize;
    uint16_t blocks;
    uint8_t sector;
    uint8_t track;
} dir_t;

typedef struct {
    uint8_t sector;
    uint8_t track;
} linkage_t;

typedef struct {
    linkage_t prev, next, pointers[1];
} isisLinkage_t;

#pragma pack(pop)

struct {
    int inuse;
    fnode_t *fn;
} ifiles[NIFILES];

/*
 * for Isis Read errors display error message if enabled
 * and keep running count of errors
 */
void rdError(char *fmt, ...) {
    if (showRdError) {
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
    }
    rdErrorCnt++;
}

int volGran, volSize, maxFnode, fnodeStart, fnodeSize, rootFnode;
/*
 * High - speed test for compressable sector(all bytes same value)
 */
int issame(unsigned char *buf, int size) {
    for (int i = 1; i < size; i++)
        if (buf[i] != buf[0])
            return 0;
    return 1;
}

void free_track(TRACK *t) {
    if (t->buf)
        free(t->buf);
    free(t);
}

// detect duplicate tracks which occasionally occurs on 80 track drives reading a 40 track disk
int tracksSame(TRACK *t1, TRACK *t2) {
    if (t1->Cyl != t2->Cyl || t1->Head != t2->Head || t1->Nsec != t2->Nsec)
        return 0;
    return memcmp(t1->Smap, t2->Smap, t1->Nsec) == 0 && memcmp(t1->Cmap, t2->Cmap, t1->Nsec) == 0 &&
           memcmp(t1->Hmap, t2->Hmap, t1->Nsec) == 0 &&
           memcmp(t1->buf, t2->buf, t1->Nsec * t1->Size) == 0;
}

/*
 * Load a track from an image into memory
 */
TRACK *load_track(FILE *fp) {
    int c, i, m, h, n, s;
    TRACK *t;

    if ((m = getc(fp)) == EOF)
        return NULL;

    t       = (TRACK *)calloc(1, sizeof(TRACK));
    t->Mode = m;
    if ((c = getc(fp)) == EOF)
        fatal("EOF at Cylinder");
    t->Cyl = c;
    if ((h = getc(fp)) == EOF)
        fatal("EOF at Head");
    t->Head = h & 0x0F;
    if (t->Head == 1)
        heads = 2;
    t->Hflag = (h & 0xF0);
    if ((n = getc(fp)) == EOF)
        fatal("EOF at Nsec");
    t->Nsec = n;
    if ((s = getc(fp)) == EOF)
        fatal("EOF at Size");
    t->Size = s;
    if (t->Cyl >= MAXCYLINDER)
        fatal("Cylinder value %d out of range 0-%d\n", t->Cyl, MAXCYLINDER - 1);

    if (m > 5)
        fatal("Mode value %d out of range 0-5\n", m);

    if ((h & 0x0F) >= MAXHEAD)
        fatal("Head value %d out of range 0-%d\n", h, MAXHEAD - 1);

    if (s > 6) {
        fatal("Size value %d out of range 0-6\n", s);
        t->Size = 0;
    }

    t->Size = s = xsize[s];

    if (n) {
        if ((int)fread(t->Smap, 1, n, fp) != n)
            fatal("EOF in Sector Map");
        t->startSec = 255;
        for (i = 0; i < n; i++)
            if (t->Smap[i] < t->startSec)
                t->startSec = t->Smap[i];

        if (h & 0x80) {
            if ((int)fread(t->Cmap, 1, n, fp) != n)
                fatal("EOF in Cylinder Map");
            if (!issame(t->Cmap, n)) {
                for (int i = 0; i < n; i++)
                    if (t->Cmap[i]) {
                        t->Cmap[0] = t->Cmap[i];
                        break;
                    }
                fprintf(stderr,
                        "WARNING: mixed cylinder head mapping not supported. Mapping all of "
                        "cylinder %d to %d\n",
                        t->Cyl, t->Cmap[0]);
            }
            t->Cyl = t->Cmap[0];
            t->Hflag &= 0x7f;
            h &= 0x7f;
        } else
            memset(t->Cmap, t->Cyl, n);

        if (h & 0x40) {
            if ((int)fread(t->Hmap, 1, n, fp) != n)
                fatal("EOF in Head Map");
        } else
            memset(t->Hmap, h, n);
    }
    assert(t != NULL);
    if ((t->buf = (uint8_t *)calloc(s * t->Nsec, 1)) == NULL) {
        fprintf(stderr, "alloc track buf - out of memory\n");
        exit(1);
    }

    // Decode sector data blocks
    t->Smissing = ~0ULL;
    for (i = 0; i < n; ++i) {
        if ((c = getc(fp)) == EOF)
            fatal("EOF at sector %d/%d flag", i, t->Smap[i]);

        if (c & 1) {
            if ((int)fread(transferBuf, 1, s, fp) != s)
                fatal("EOF in sector %d/%d data", i, t->Smap[i]);
        } else if (c) {
            if ((h = getc(fp)) == EOF)
                fatal("EOF in sector %d/%d compress data", i, t->Smap[i]);
            memset(transferBuf, h, s);
        }
        if (c && (t->startSec > t->Smap[i] || t->Smap[i] >= n + t->startSec))
            fprintf(stderr, "Warning: Track %d/%d - Invalid sector %d\n", t->Cyl, t->Head,
                    t->Smap[i]);
        else if (c == 1 || c == 2) {
            memcpy(t->buf + s * (t->Smap[i] - t->startSec), transferBuf, s);
            t->Smissing &= ~(1ULL << (t->Smap[i] - t->startSec));
        }
    }

    if (t->Cyl > maxCylinder)
        maxCylinder = t->Cyl;
    return t;
}

void load_imd(FILE *fp) {
    TRACK *t;
    int c;
    int i = 0;

    while ((c = getc(fp)) != 0x1A) {
        if (c == EOF) {
            fprintf(stderr, "EOF in comment\n");
            exit(1);
        } else if (c != '\r') {
            putchar(c);
            if (i < MAXCOMMENT)
                comment[i++] = c;
        }
    }
    while ((t = load_track(fp))) {
        if (t->Cyl > MAXCYLINDER || t->Head > MAXHEAD) {
            fprintf(stderr, "Cyl %d, Head %d not supported\n", t->Cyl, t->Head);
            free(t);
            continue;
        }

        if (disk[t->Cyl][t->Head]) {
            if (tracksSame(disk[t->Cyl][t->Head], t))
                printf("track duplicate %d/%d - info same\n", t->Cyl, t->Head);
            else
                printf("track duplicate %d/%d - info different\n", t->Cyl, t->Head);
            free_track(t);
            continue;
        } else
            disk[t->Cyl][t->Head] = t;
    }

    if (disk[1][0] == NULL && disk[2][0] == NULL)
        diskType = UNKNOWN;
    else
        switch (disk[1][0] ? disk[1][0]->Nsec : disk[2][0]->Nsec) {
        case 26:
            diskType = ISIS_SD;
            break;
        case 52:
            diskType = ISIS_DD;
            break;
        case 16:
            diskType = disk[0][0] && disk[0][0]->Size == 256 ? ISIS_DOS : ISIS_PDS;
            break;
        case 8:
            diskType = ISIS_IV;
            break;
        default:
            diskType = UNKNOWN;
        }
}

struct {
    int size;
    uint8_t diskType;
    uint8_t tracks;
    uint8_t heads;
    uint8_t t0spt;
    int t0SSize;
    uint8_t spt;
    int sSize;
} diskSizes[] = {
    // clang-format off
    { 256256, ISIS_SD, 77, 1, 26, 128, 26, 128 },
    { 512512, ISIS_DD, 77, 1, 52, 128, 52, 128 },
    { 653312, ISIS_PDS, 80, 2, 16, 128, 16, 256 },
    { 327680, ISIS_DOS, 80, 1, 16, 256, 16, 256 }, // requires img in true linear order
    { 653184, ISIS_IV, 80, 2, 15, 128, 8, 512 }    // currently unreliable
    // clang-format on
};
#define DISKOPT (int)(sizeof(diskSizes) / sizeof(diskSizes[0]))

void load_img(FILE *fp) {
    long fileSize;
    int spt;
    int track;
    TRACK *t;
    int diskopt;
    int sSize;

    fseek(fp, 0, SEEK_END);
    fileSize = ftell(fp);
    rewind(fp);

    for (diskopt = 0; diskopt < DISKOPT; diskopt++)
        if (fileSize == diskSizes[diskopt].size)
            break;
    if (diskopt >= DISKOPT) {
        fprintf(stderr, "Unknown disk image\n");
        exit(1);
    }
    diskType = diskSizes[diskopt].diskType;
    heads    = diskSizes[diskopt].heads;

    spt      = diskSizes[diskopt].t0spt;
    sSize    = diskSizes[diskopt].t0SSize;
    for (track = 0; track < diskSizes[diskopt].tracks; track++) {
        for (int head = 0; head < diskSizes[diskopt].heads; head++) {
            if ((t = (TRACK *)calloc(1, sizeof(TRACK))) && (t->buf = malloc(spt * sSize))) {
                t->Cyl      = track;
                t->Head     = head;
                t->Nsec     = spt;
                t->Size     = sSize;
                t->startSec = 1;
                if ((int)fread(t->buf, sSize, spt, fp) != spt) {
                    fprintf(stderr, "read error\n");
                    exit(1);
                }
                disk[track][head] = t;
            } else {
                fprintf(stderr, "out of memory\n");
                exit(1);
            }
            sSize = diskSizes[diskopt].sSize; // rest of disk takes geometry
            spt   = diskSizes[diskopt].spt;
        }
    }
    maxCylinder = diskSizes[diskopt].tracks - 1;
}

uint8_t *getTS(int track, int sector) {
    TRACK *p;
    int head = 0;
    int spt;
    int offset = 0; // for ISIS-DOS, 256 byte sectors treated as 2x128 byte sectors, offset = 128
                    // for 2nd sector
    sector--;       // 0 base sector
    if (diskType == ISIS_DOS) {
        offset = (sector % 2 * 128);
        sector /= 2;
    }
    if (track > maxCylinder) {
        rdError("track %d > maxCylinder %d\n", track, maxCylinder);
        return NULL;
    }
    if (disk[track][0])
        spt = disk[track][0]->Nsec;
    else if (track != 0 || !disk[track][1] ||
             diskType == UNKNOWN) // special handling only for track 0 of ISIS_PDS and ISIS_IV disks
        return NULL;
    else
        spt = diskType == ISIS_PDS ? 16 : 15;
    if (sector >= spt && heads > 0) { // overflow onto other head ?
        head++;
        sector -= spt;
    }

    if (!(p = disk[track][head])) // sector is still missing track
        return NULL;
    if (sector >= p->Nsec) {
        rdError("sector %d > max sector %d for cylinder %d head %d\n", sector + 1, p->Nsec, head,
                track);
        return NULL;
    }

    if (p->Smissing & (1ULL << sector))
        return NULL;
    return p->buf + sector * p->Size + offset;
}

uint8_t *getBlock(int blk) {
    int logicalCylinder, offset;
    int t00cnt, t10cnt;

    if (disk[0][0])
        t00cnt = (disk[0][0]->Nsec * disk[0][0]->Size + volGran - 1) / volGran;
    else // special handling if t00 is missing
        t00cnt = 4;
    t10cnt = (disk[1][0]->Nsec * disk[1][0]->Size + volGran - 1) / volGran;

    if (blk < t00cnt) { // track 0, head 0
        logicalCylinder = 0;
        offset          = (blk * volGran);
    } else {
        blk += (t10cnt - t00cnt); // trick to assume track 0, head 0 is same as others
        logicalCylinder = blk / t10cnt;
        offset          = blk % t10cnt * volGran;
    }
    if (logicalCylinder / heads <= MAXCYLINDER &&
        disk[logicalCylinder / heads][logicalCylinder % heads])
        return disk[logicalCylinder / heads][logicalCylinder % heads]->buf + offset;
    else
        return NULL;
}

int readWord(uint8_t *p) {
    return p[0] + p[1] * 256;
}

int readBlockNumber(uint8_t *p) {
    return p[0] + p[1] * 0x100 + p[2] * 0x10000;
}

int readDWord(uint8_t *p) {
    return readWord(p) + readWord(p + 2) * 0x10000;
}

uint8_t *getFileLoc(fnode_t *fn, unsigned pos) {
    int i, n;
    uint8_t *p;

#pragma pack(push, 1)
    typedef struct {
        uint8_t nblocks;
        uint8_t blockNum[3];
    } indirect_t;
#pragma pack(pop)
    indirect_t *indirect;

    if (pos >= fn->totalSize)
        return NULL;

    n = pos / volGran;

    for (i = 0; i < 8 && fn->ptr[i].numBlocks <= n; i++)
        n -= fn->ptr[i].numBlocks;
    if (i == 8)
        return NULL;
    if (fn->flags & 2) { // long file
        if ((p = getBlock(readBlockNumber(fn->ptr[i].blkPtr))) == NULL)
            return NULL;
        indirect = (indirect_t *)p;
        while (indirect->nblocks <= n)
            n -= (indirect++)->nblocks;
        p = getBlock(readBlockNumber(indirect->blockNum) + n);
    } else
        p = getBlock(readBlockNumber(fn->ptr[i].blkPtr) + n);

    return p + pos % volGran;
}

int iRead(int fd, uint8_t *buf, int pos, int count);

int iOpen(int node) {
    if (!ifiles[0].inuse) {
        ifiles[0].inuse = 1;
        ifiles[0].fn    = (fnode_t *)getBlock(fnodeStart / volGran);
    }

    for (int i = 1; i < NIFILES; i++) {
        if (!ifiles[i].inuse) {
            if (!ifiles[i].fn)
                ifiles[i].fn = (fnode_t *)malloc(fnodeSize);
            iRead(0, (uint8_t *)ifiles[i].fn, node * fnodeSize, fnodeSize);
            ifiles[i].inuse = 1;
            return i;
        }
    }
    return -1;
}

int iRead(int fd, uint8_t *buf, int pos, int count) {
    fnode_t *fn;
    int n = 0;
    int delta;
    uint8_t *p;

    if (fd < 0 || NIFILES <= fd || !ifiles[fd].inuse)
        return 0;
    fn = ifiles[fd].fn;

    if ((int)fn->totalSize - pos < count)
        count = fn->totalSize - pos;

    while (count > 0) {
        delta = volGran - pos % volGran;
        if (delta > count)
            delta = count;
        if ((p = getFileLoc(fn, pos)) == NULL) {
            fprintf(logfp, ">>>read of non existant data at offset %d\n", pos);
            return n;
        }
        memcpy(buf + n, p, delta);
        n += delta;
        count -= delta;
        pos += delta;
    }

    return n;
}

void iClose(int fd) {
    if (0 <= fd && fd < NIFILES)
        ifiles[fd].inuse = 0;
}

#define READCHUNK 2048
void dumpfile(int fd, char *filename) {
    FILE *fout;
    int cnt;
    int pos = 0;
    uint8_t buf[READCHUNK];

    if ((fout = fopen(filename, "wb")) == NULL) {
        fprintf(stderr, "can't create %s\n", filename);
        return;
    }

    while (1) {
        cnt = iRead(fd, buf, pos, READCHUNK);
        if (cnt != 0)
            fwrite(buf, cnt, 1, fout);
        pos += cnt;
        if (cnt != READCHUNK)
            break;
    }
    fclose(fout);
}

void dumpdirectory(int directory, char *dirPath) // note recover not yet implemented
{
    char path[_MAX_PATH];
    fnode_t *fn;
    char *s;
    int pos = 0;
    int fd;
#pragma pack(push, 1)
    struct {
        uint16_t fnode;
        char name[14];
    } dentry;
#pragma pack(pop)

    while (iRead(directory, (uint8_t *)&dentry, pos, sizeof(dentry)) == sizeof(dentry)) {
        if (dentry.fnode != 0) {
            strcpy(path, dirPath);
            if (strlen(dirPath) + 14 >= sizeof(path))
                fatal("Name too long %s%.14s", dirPath, dentry.name);
            strncat(path, dentry.name, 14);
            fd = iOpen(dentry.fnode);
            if (fd < 0)
                fprintf(stderr, "can't read fnode %d - %s\n", dentry.fnode, path);
            else {
                fn = ifiles[fd].fn;
                fprintf(logfp, "fnode = %d, flags = %02X ", dentry.fnode, fn->flags);
                switch (fn->type) {
                case 0:
                    fprintf(logfp, "%s -> fnode file - size %d\n", path, fn->totalSize);
                    break;
                case 1:
                    fprintf(logfp, "%s -> volume free space map - size %d\n", path, fn->totalSize);
                    break;
                case 2:
                    fprintf(logfp, "%s -> free nodes map - size %d\n", path, fn->totalSize);
                    break;
                case 3:
                    fprintf(logfp, "%s -> space accounting file - size %d\n", path, fn->totalSize);
                    break;
                case 4:
                    fprintf(logfp, "%s -> bad device blocks file - size %d\n", path, fn->totalSize);
                    break;
                case 6:
                    fprintf(logfp, "%s -> directory file - size %d\n", path, fn->totalSize);
                    break;
                case 8:
                    fprintf(logfp, "%s -> data file - size %d\n", path, fn->totalSize);
                    break;
                case 9:
                    fprintf(logfp, "%s -> volume label file - size %d\n", path, fn->totalSize);
                    break;
                default:
                    fprintf(logfp, "%s -> unknown file format %d - size %d\n", path, fn->type,
                            fn->totalSize);
                    break;
                }

                s = path;
                while ((s = strchr(s, '?')))
                    *s = '_';
                if (fn->type == 6) {
                    if (mkdir(path, 0666) == -1 && errno != EEXIST) {
                        fprintf(stderr, "can't create directory %s\n", path);
                    } else {
                        strcat(path, "/");
                        dumpdirectory(fd, path);
                    }
                } else
                    dumpfile(fd, path);
                iClose(fd);
            }
        }
        pos += sizeof(dentry);
    }
}

uint8_t fnode0[16] = { 5, 0, 0, 1 };
int fnode0Loc[]    = { 0x30a00, 0x40000, 0 };

void isis4() {
    uint8_t *p;
    int fd;
    //	fnode_t *fn;
    //	int offset;
    int *probe;

    if ((logfp = fopen("__log__", "wt")) == NULL) {
        fprintf(stderr, "can't create __log__ file\n");
        exit(1);
    }

    if (disk[0][0] == NULL) {
        fprintf(logfp, "Warning missing ISIS IV volume label sector\n");
        volGran   = disk[1][0]->Size;
        fnodeSize = 128; // It may be different on iRMX!!
        /* probe for the fnode entry */
        for (probe = fnode0Loc; *probe; probe++) {
            p = getBlock(*probe / volGran);
            if (memcmp(p + *probe % volGran, fnode0, 16) == 0) {
                fnodeStart = *probe;
                rootFnode  = 0;
                for (int i = 2; i < 10; i++) {
                    p = getBlock((*probe + i * fnodeSize) / volGran);
                    if (p[(*probe + i * fnodeSize) % volGran + 2] == 6) {
                        rootFnode = i;
                        break;
                    }
                }
                break;
            }
        }
        if (*probe == 0) {
            fprintf(stderr, "can't find fnodes\n");
            return;
        }
        if (rootFnode == 0) {
            fprintf(stderr, "can't find directory\n");
            return;
        }
    } else {
        p = disk[0][0]->buf;
        p += 384;
        volGran = readWord(p + 12);

        unsigned i;
        for (i = volGran; i && (i & 1) == 0; i >>= 1) // vol should be 2^n
            ;
        if (i != 1) {
            fprintf(stderr, "invalid volGran\n");
            return;
        }
        volSize = readDWord(p + 14);
        if (volSize < 0 || volSize > (160 * 18 * 512)) { // max 1.44Mb diskette
            fprintf(stderr, "invalid volSize\n");
            return;
        }
        maxFnode   = readWord(p + 18);
        fnodeStart = readDWord(p + 20);
        fnodeSize  = readWord(p + 24);
        rootFnode  = readWord(p + 26);
    }
    printf("Assuming ISIS IV disk\n");
    fprintf(logfp,
            "volGran = %d, volSize = %d, maxFnode = %d, fnodeStart = 0x%X, fnodeSize = %d, "
            "rootFnode = %d\n",
            volGran, volSize, maxFnode, fnodeStart, fnodeSize, rootFnode);
    fd = iOpen(rootFnode);
    dumpdirectory(fd, "");
    iClose(fd);
    dumpfile(0, "__fnodes__");
    fclose(logfp);

    // for (int i = 0; i < maxFnode; i++) {
    //	fn = (fnode_t *)getFnode(i);
    //	printf("fnode %d:\n", i);
    //	printf("\tflags = %04X, type = ", fn->flags);
    //	switch (fn->type) {
    //	case 0: printf("fnode file"); break;
    //	case 1: printf("volume free space map"); break;
    //	case 2: printf("free fnodes map"); break;
    //	case 3: printf("space accounting file"); break;
    //	case 4: printf("bad device blocks file"); break;
    //	case 6: printf("directory file"); break;
    //	case 8: printf("data file"); break;
    //	default: printf("unknown %d", fn->type); break;
    //	}
    //	printf(", gran = %d, owner = %d\n", fn->gran, fn->owner);
    //	printf("\tcr$time = %d, access$time = %d, mod$time = %d", fn->crTime, fn->accessTime,
    //fn->modTime); 	printf(", total$size = %d, total$blocks = %d\n", fn->totalSize,
    //fn->totalBlocks); 	for (int j = 0; j < 8; j++) { 		printf("\tptr[%d], num$blocks = %d, blk$ptr =
    //%d\n", j, fn->ptr[j].numBlocks, fn->ptr[j].blkPtr[0] + fn->ptr[j].blkPtr[1] * 256 +
    //fn->ptr[j].blkPtr[2] * 0x10000);
    //	}
    //	printf("\tthis$size = %d, reserved$a = %d, reserved$b = %d, id$count = %d\n", fn->thisSize,
    //fn->reserverdA, fn->reserverdB, fn->idCount); 	for (int j = 0; j < 3 && j < fn->idCount; j++)
    //		printf("\taccessor[%d], access = %02X, id = %d\n", j, fn->accessor[j].access,
    //fn->accessor[j].id); 	printf("\tparent = %d\n", fn->parent);

    //}
}

bool extractFile(dir_t *dptr, int dirSlot) {
    char filename[16];
    char isisName[11];
    FILE *fout;
    uint8_t *p;
    int blk, blkIdx;
    isisLinkage_t *links =
        (isisLinkage_t *)&(dptr->blocks); // trick to pick next block from directory info
    int prevTrk, prevSec;
    int sectorSize = diskType == ISIS_PDS ? 256 : 128;

    if (*dptr->ext)
        sprintf(isisName, "%.6s.%.3s", dptr->name, dptr->ext);
    else
        sprintf(isisName, "%.6s", dptr->name);

    if (!*isisName) {
        if (dptr->status != 0) // ignore missing name if deleted file
            return true;
        else {
            fprintf(stderr, "Missing file name for ISIS.DIR entry %d\n", dirSlot);
            return false;
        }
    }

    if (dptr->status != 0)
        sprintf(filename, "#%d_%s", dirSlot, isisName); // mark recovered file
    else
        strcpy(filename, isisName);

    for (char *s = filename; *s; s++)
        *s = tolower(*s);

    if ((fout = fopen(filename, "wb")) == NULL) {
        fprintf(stderr, "can't create %s\n", filename);
        return false;
    }
    prevTrk = prevSec = 0;

    showRdError       = dptr->status == 0;
    rdErrorCnt        = 0;

    if (debug)
        printf("%s:", filename);
    for (blk = 0; blk < dptr->blocks; blk++) {
        if (blk == dptr->blocks - 1)
            sectorSize = diskType == ISIS_PDS ? dptr->lastblksize + 1 : dptr->lastblksize;

        blkIdx = blk % (diskType == ISIS_PDS ? 123 : 62);
        if (debug && blkIdx && blkIdx % 16 == 0)
            printf("\n%10c", ' ');
        if (blkIdx == 0) {
            if (debug)
                printf("\n  %02d:%02d -> ", links->next.track, links->next.sector);
            isisLinkage_t *curLinks = links;
            if ((links = (isisLinkage_t *)getTS(links->next.track, links->next.sector)) == NULL) {
                rdError("%s block %d bad linkage block t=%d s=%d\n", isisName, blk,
                        curLinks->next.track, curLinks->next.sector);
                rdErrorCnt++;
                break;
            }
            if (prevSec != links->prev.sector && prevTrk != links->prev.track) {
                rdError("%s block %d corrupt linkage block t=%d s=%d\n", isisName, blk,
                        curLinks->next.track, curLinks->next.sector);
                rdErrorCnt++;
                break;
            }
            prevTrk = curLinks->next.track;
            prevSec = curLinks->next.sector;
            if (debug)
                printf("(%02d:%02d %02d:%02d)", links->prev.track, links->prev.sector,
                       links->next.track, links->next.sector);
        }
        if (debug)
            printf(" %02d:%02d", links->pointers[blkIdx].track, links->pointers[blkIdx].sector);
        if (links->pointers[blkIdx].sector == 0 ||
            (p = getTS(links->pointers[blkIdx].track, links->pointers[blkIdx].sector)) == NULL) {
            rdError("%s block %d missing t=%d s=%d\n", filename, blk, links->pointers[blkIdx].track,
                    links->pointers[blkIdx].sector);
            rdErrorCnt++;

            for (int i = 0; i < sectorSize; i++)
                putc(0xc7, fout);
        } else
            fwrite(p, 1, sectorSize, fout);
    }
    if (debug && blk != 0) {
        while (++blkIdx < (diskType == ISIS_PDS ? 123 : 62) &&
               (links->pointers[blkIdx].track || links->pointers[blkIdx].sector))
            printf(" [%02d:%02d]", links->pointers[blkIdx].track, links->pointers[blkIdx].sector);
    }
    if (debug)
        putchar('\n');

    fclose(fout);
    // update the recipe info
    strcpy(isisDir[dirIdx].name, dptr->status != 0 ? "#" : "");
    strcat(isisDir[dirIdx].name, isisName);
    strcpy(isisDir[dirIdx].fname, filename);
    if (dptr->blocks)
        isisDir[dirIdx].dirLen =
            (dptr->blocks - 1) * (diskType == ISIS_PDS ? 256 : 128) + sectorSize;
    else
        isisDir[dirIdx].dirLen = -dptr->lastblksize;
    int size = isisDir[dirIdx].actLen = getFileKey(filename, isisDir[dirIdx].key);
    isisDir[dirIdx].attrib            = dptr->attributes;
    isisDir[dirIdx].errors            = rdErrorCnt;
    if (osIdx < 0 && dptr->status == 0 &&
        (strcmp(isisName, "ISIS.BIN") == 0 || strcmp(isisName, "ISIS.PDS") == 0))
        osIdx = dirIdx;

    dirIdx++;
    if (dptr->status != 0) { // deleted file
        if (rdErrorCnt || size == 0)
            unlink(filename);
        else
            recoveredFiles++;
        return true;
    }
    return rdErrorCnt == 0;
}

bool isis2_3(dir_t *dptr) {
    FILE *fp;
    dir_t dentry;
    int dirSlot;
    bool ok        = true;
    char *isisName = isDOS ? "DOS.DIR" : "ISIS.DIR";

    printf("Looks like an ISIS %s disk\n", diskType == ISIS_PDS ? "PDS" : "I / II / III");

    ok &= extractFile(dptr, 0); // get the ISIS.DIR file, leaves filename in targetPath
    /* although processing of the header file could be done here
       it is simpler to re-read the ISIS.DIR file that has just been saved
    */
    if ((fp = fopen(isisName, "rb")) == NULL) {
        fprintf(stderr, "can't read %s file\n", isisName);
        return false;
    }
    dirSlot = 0;
    while (fread(&dentry, sizeof(dir_t), 1, fp) == 1 && dentry.status != 0x7f) {
        dirSlot++;
        if (memcmp(dentry.name, isDOS ? "DOS\0\0\0DIR" : "ISIS\0\0DIR", 9) == 0)
            continue;
        if (dentry.status == 0 || dentry.status == 0xff)
            ok &= extractFile(&dentry, dirSlot);
        else {
            fprintf(stderr, "corrupt directory - status byte %02X\n", dentry.status);
            break;
        }
    }
    fclose(fp);
    if (recoveredFiles)
        printf("%d deleted files recovered for checking\n", recoveredFiles);
    return ok;
}

uint8_t *chkIsisDir(uint8_t *p) {

    for (int i = 0; i < 4; i++, p += sizeof(dir_t))
        if (memcmp(p, "\0ISIS\0\0DIR", 10) == 0)
            return p;
        else if (memcmp(p, "\0DOS\0\0\0DIR", 10) == 0) {
            isDOS = true;
            return p;
        }
    return NULL;
}

int main(int argc, char **argv) {
    uint8_t *p;
    FILE *fp;
    char targetPath[_MAX_PATH];
    bool local = false;
    bool ok;

    while (getopt(argc, argv, "ld") != EOF) {
        if (optopt == 'l')
            local = true;
        else if (optopt == 'd')
            debug = true;
    }

    if (optind >= argc)
        usage("Missing file to process");

    if (optind < argc - 1)
        warn("Ignoring files after %s", argv[optind]);

    if ((fp = fopen(argv[optind], "rb")) == NULL)
        fatal("Cannot open %s", argv[optind]);

    if (!local)
        openDb();

    strcpy(targetPath, argv[optind]);
    char const *fileName = basename(targetPath);
    char *ext            = strrchr(fileName, '.');
    if (ext && stricmp(ext, ".imd") == 0)
        load_imd(fp);
    else
        load_img(fp);
    fclose(fp);
    isDOS = diskType == ISIS_DOS;

    /* make a directory path from the name*/
    *ext = '\0';
    mkdir(targetPath, 0666);
    if (chdir(targetPath)) // move to new directory for simple file creation
        fatal("Can't write data to %s\n", targetPath);

    // because there are example disks (i.e. I have seen one) where ISIS.DIR is not in
    // the correct place, look through 4 entries to see if it exists

    if ((p = getTS(1, 2)) && (p = chkIsisDir(p))) { // ISIS-II
        ok = isis2_3((dir_t *)p);
        mkRecipe(basename(*argv), isisDir, comment, diskType, ok);
    } else if ((p = getTS(0x27, 2)) && (p = chkIsisDir(p))) { // ISIS-PDS
        ok = isis2_3((dir_t *)p);
        mkRecipe(basename(*argv), isisDir, comment, diskType, ok);
    } else
        isis4();
    if (!local)
        closeDb();
}
