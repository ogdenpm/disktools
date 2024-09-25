/* wteledsk.c  - teledisk *.td0 file decoding

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
    http://www.gnu.org/licenses/gpl.txt

*/
#include "td02imx.h"
#include "getopt.h"
#include "showVersion.h"
#include "td0.h"

sizing_t sizing[2][9][2]; // [encoding][sSize][head]

track_t disk[MAXCYLINDER][MAXHEAD];

uint8_t nCylinder;
uint8_t nHead;

FILE *fpout;
char *invokeName;
char *srcFile;

_Noreturn void fatal(char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fputs("Fatal error: ", stderr);
    vfprintf(stderr, fmt, args);
    va_end(args);
    putc('\n', stderr);
    exit(1);
}

void warn(char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fputs("Warning: ", stdout);
    vfprintf(stdout, fmt, args);
    va_end(args);
    putc('\n', stdout);
}

char *basename(char *path) {
    char *s;
#ifdef WIN32
    if (path[0] && path[1] == ':')
        path += 2;
    while ((s = strpbrk(path, "/\\")))
#else
    while ((s = strchr(path, '/')))
#endif
        path = s + 1;
    return path;
}

bool sameSector(sector_t *sector1, sector_t *sector2) {
    if (sector1->cyl != sector2->cyl || sector1->head != sector2->head ||
        sector1->sSize != sector2->sSize)
        return false;
    if (sector1->data && sector2->data)
        return memcmp(sector1->data, sector2->data, 128 << sector1->sSize) == 0;

    return sector1->data == NULL && sector2->data == NULL;
}

// generate a score from the flags to determine a preferred version of any sector that is duplicated
// highest to lowest
// normal sector            16
//      duplicate           15
//      with CRC            14
//      duplicate with CRC  13
// deleted data sector      12
//      duplicate           11
//      with CRC            10
//      duplicate with CRC  09
// Skipped sector           08
//      duplicate           07
// No data                  04
//      duplicate           03
//
uint8_t sectorScore(uint8_t flags) {
    uint8_t score;
    if ((flags & ~(SEC_DUP | SEC_CRC)) == 0)
        score = 16;
    else if (flags & SEC_DAM)
        score = 12;
    else if (flags & SEC_DOS)
        score = 8;
    else if (flags & SEC_NODAT)
        score = 4;
    else
        return 0;
    return score - (flags & (SEC_DUP | SEC_CRC));
}

void buildSectorMap(track_t *pTrack) {
    uint8_t sectorMap[256];
    uint16_t used[256]; // used to support duplicates
    for (int i = 0; i < 256; i++)
        used[i] = UNALLOCATED;

    // build sectorMap
    sector_t *pIn = pTrack->sectors;
    for (uint8_t i = 0; i < pTrack->nSec; i++, pIn++) {
        // process sectors with valid IDAMs
        if (!(pIn->flags & SEC_NOID) && pIn->sSize < 8) { // only consider if IDAM and sSize
            if (used[pIn->sec] == UNALLOCATED) {          // first sight of sector id so use it
                if (pTrack->nUsable == 0)
                    pTrack->first = pTrack->last = pIn->sec;
                else if (pIn->sec < pTrack->first)
                    pTrack->first = pIn->sec;
                else if (pIn->sec > pTrack->last)
                    pTrack->last = pIn->sec;
                sectorMap[pTrack->nUsable] = i;
                used[pIn->sec]             = pTrack->nUsable++;
            } else { // we have some form of duplicate !!!
                uint16_t slot   = used[pIn->sec];
                sector_t *pPrev = &pTrack->sectors[sectorMap[slot]];
                uint8_t pScore  = sectorScore(pPrev->flags);
                uint8_t iScore  = sectorScore(pIn->flags);
                if (iScore > pScore) { // better?
                    sectorMap[slot] = i;
                    if ((pScore == 16 || pScore == 12) && iScore + 1 == pScore &&
                        !sameSector(pPrev, pIn))
                        pPrev->extraFlag |= F_DUP; // problem duplicate
                } else if ((pScore == 16 || pScore == 12) &&
                           (pScore == iScore || pScore == iScore + 1) && !sameSector(pPrev, pIn))
                    pIn->flags |= F_DUP; // problem duplicate
            }
        }
    }
    if (pTrack->nUsable) {
        pTrack->sectorMap = safeMalloc(pTrack->nUsable); // save the sectorMap
        memcpy(pTrack->sectorMap, sectorMap, pTrack->nUsable);
        // mark the used sectors, set sSize information and trackSize
        pTrack->sSize     = pTrack->sectors[sectorMap[0]].sSize;
        pTrack->trackSize = 0;
        for (int i = 0; i < pTrack->nUsable; i++) {
            sector_t *ps = &pTrack->sectors[sectorMap[i]];
            ps->extraFlag |= F_USED;
            if (pTrack->sSize != ps->sSize)
                pTrack->sSize = MIXEDSIZES;
            pTrack->trackSize += 128 << ps->sSize;
        }
    } else
        pTrack->sectorMap = NULL;
}

void showSectorUsage(track_t *pTrack) {
    sector_t *pIn = pTrack->sectors;
    uint8_t sSize = pIn[pTrack->sectorMap[0]].sSize;
    uint8_t col   = 5; // current output column "%2d/%d " already emitted
    uint8_t sCol  = 5; // sector restart col


    col += printf("%s ", pTrack->encoding ? "FM " : "MFM");
    if (pTrack->nUsable == pTrack->nSec)
        col += printf("%3d", pTrack->nSec);
    else
        col += printf("%d/%d", pTrack->nUsable, pTrack->nSec);

    sCol = col += printf("%*s[%d]", 15 - col, "", 128 << sSize);
    for (uint8_t i = 0; i < pTrack->nSec; i++, pIn++) {
        char info[64]; // single sector info string
        char *s = info;

        if ((pIn->flags & SEC_NOID))
            s += sprintf(s, " ??");
        else {
            if (pIn->sSize >= 8)
                s += sprintf(s, " [0x%x] ", pIn->sSize);
            else if (sSize != pIn->sSize)
                s += sprintf(s, " [%d]", 128 << (sSize = pIn->sSize));

            if (pIn->extraFlag & F_USED) {
                s += sprintf(s, " %d", pIn->sec);
                pTrack->trackSize += 128 << pIn->sSize;
            } else
                s += sprintf(s, " !%d", pIn->sec);
            if (pIn->extraFlag & F_DUP)
                *s++ = 'x';
           
            for (uint8_t i = 0; i < 6; i++) // 0x80 not used and 0x40 handled as SEC_NOID
                if (pIn->flags & (1 << i))
                    *s++ = "dcr?sm"[i];
            *s = '\0';
        }
        if (col + (s - info) > 100) {
            printf("\n%*s", sCol, "");
            col = sCol;
        }
        col += printf("%s", info);
    }

    putchar('\n');
}

void loadDisk() {

    td0Track_t *pTd0;
    while ((pTd0 = readTrack())) {
        uint8_t encoding = pTd0->head >> 7; // 1 = FM
        pTd0->head &= 0x7f;
        if (pTd0->cyl >= MAXCYLINDER || pTd0->head >= MAXHEAD)
            fatal("Physical cylinder/head %d/%d beyond supported limits", pTd0->cyl, pTd0->head);
        track_t *pTrack = &disk[pTd0->cyl][pTd0->head];
        if (pTrack->nUsable == 0) {
            pTrack->nSec     = pTd0->nSec;
            pTrack->sectors  = pTd0->sectors;
            pTrack->encoding = encoding;
            buildSectorMap(pTrack);

            printf("%2d/%d ", pTd0->cyl, pTd0->head);

            if (pTrack->nUsable) {
                showSectorUsage(pTrack);
                pTd0->sectors = 0; // claim onwership of memory
                // update information on used track formats
                sizingsUsed |= (1 << pTrack->sSize);
                encodingsUsed |= encoding;
                sizing_t *si = &sizing[encoding][pTrack->sSize][pTd0->head];
                if (si->count++ == 0 || si->first > pTrack->first) {
                    si->first = pTrack->first;
                    si->nSec  = pTrack->nUsable;
                }
                if (si->nSec < pTrack->nUsable) // relies on si->nSec initialised to 0
                    si->nSec = pTrack->nUsable;

                if (pTd0->cyl >= nCylinder)
                    nCylinder = pTd0->cyl + 1;
                if (pTd0->head >= nHead)
                    nHead = pTd0->head + 1;
            } else
                printf("No usable sectors\n");
        } else
            warn("Skipping duplicate Track %d/%d", pTd0->cyl, pTd0->head);
    }
}

bool sizeDisk() {
    // handle simple case
    uint32_t diskSize;
    uint8_t firstSizing;
    for (firstSizing = 0; firstSizing < MIXEDSIZES; firstSizing++)
        if (sizingsUsed & (1 << firstSizing))
            break;
    if (firstSizing < MIXEDSIZES && (sizingsUsed == (1 << firstSizing)) &&
        encodingsUsed != (FM | MFM) &&
        (nHead == 1 || sizing[encodingsUsed][firstSizing][0].nSec ==
                           sizing[encodingsUsed][firstSizing][1].nSec)) {
        // we have a disk with a simple disk
        sizing_t *si = &sizing[encodingsUsed][firstSizing][0];

        printf("Appears to be %d%sx%d %d %s sector", nCylinder, nHead == 2 ? "x2" : "", si->nSec,
               128 << firstSizing, encodingsUsed ? "FM" : "MFM");
        diskSize = nCylinder * nHead * si->nSec * (128 << firstSizing);
    } else { // complex disk format
        // we have a complex disk layout
        diskSize = 0;
        printf("Appears to be");
        for (uint8_t enc = 0; enc <= 1; enc++) {
            char *encoding = enc ? "FM" : "MFM";

            for (uint8_t i = 0; i <= MIXEDSIZES; i++) {
                sizing_t *si = sizing[enc][i];
                if (si[0].count || si[1].count) {
                    if (si[0].count == si[1].count && si[0].nSec == si[1].nSec)
                        printf(" [%dx2x%d", si[0].count, si[0].nSec);
                    else if (si[1].count == 0)
                        printf(" [%s%dx%d", nHead == 2 ? "1:" : "", si[0].count, si[0].nSec);
                    else if (si[0].count == 0)
                        printf(" [2:%dx%d", si[1].count, si[1].nSec);
                    else
                        printf(" [(1:%dx%d 2:%dx%d)", si[0].count, si[0].nSec, si[1].count,
                               si[1].nSec);
                    int partSize = 0;
                    if (i != MIXEDSIZES) {
                        partSize +=
                            (si[0].count * si[0].nSec + si[1].count * si[1].nSec) * (128 << i);
                        printf(" %d", 128 << i);
                    } else {
                        for (track_t *p = &disk[0][0]; p < &disk[nCylinder][nHead]; p++)
                            if (p->encoding == enc && p->sSize == MIXEDSIZES)
                                partSize += p->trackSize;
                        printf("mixed size");
                    }
                    diskSize += partSize;
                    printf(" %s sector %dK]", encoding, partSize / 1024);
                }
            }
        }
    }
    printf(" %dK\n\n", diskSize / 1024);
    return true;
}

void showHeader(td0Header_t *pHead) {
    static char *densities[] = { "low", "high", "extended", "unknown" };

    printf("Produced with version %d.%d\n", pHead->ver / 10, pHead->ver % 10);
    printf("%s data compression was used\n", pHead->sig[0] == 'T' ? "Normal"
                                             : pHead->ver > 19    ? "New Advanced"
                                                                  : "Old Advanced");
    printf("%s sectors were copied\n", pHead->dosMode ? "DOS Allocated" : "All");
    printf("%s checked\n", pHead->surfaces == 1 ? "One side was" : "Both sides were");

    char *source = "unknown type";
    switch (pHead->driveType) {
    case 0:
        source = (pHead->stepping & 2) ? "5 1/4\"" : "5 1/4\" 96 tpi in a 48 tpi";
        break;
    case 1:
        source = "5 1/4\"";
        break;
    case 2:
        source = (pHead->density & 6)    ? "5 1/4\""
                 : (pHead->stepping & 1) ? "5 1/4\" 48 tpi"
                                         : "5 1/4\" 96 tpi";
        break;
    case 3:
    case 4:
    case 6:
        source = "3 1/2\"";
        break;
    case 5:
        source = "8\"";
        break;
    }

    printf("Type = %d, stepping = %02X, density = %02X\n", pHead->driveType, pHead->stepping,
           pHead->density);
    printf("Source was %s %s-density %s\n", source, densities[(pHead->density & 6) >> 1],
           (pHead->density & 0x80) ? "FM" : "MFM");

    // display comment information
    static char *months[] = {
        "January", "February", "March",     "April",   "May",      "June",
        "July",    "August",   "September", "October", "November", "December"
    };
    if (pHead->stepping & HAS_COMMENT) {
        printf("Created on %s %2d, %d at %2d:%02d:%02d\n", months[pHead->mon], pHead->day,
               pHead->yr + 1900, pHead->hr, pHead->min, pHead->sec);

        // trim trailing newlines and spaces from comment
        char *s = strchr(pHead->comment, '\0');
        while (s > pHead->comment && (s[-1] == '\n' || s[-1] == ' '))
            s--;
        strcpy(s, s == pHead->comment ? "Comment text is blank\n" : "\n");
    } else
        strcpy(pHead->comment, "No comment information\n");
    fputs(pHead->comment, stdout);
    putchar('\n');
}

_Noreturn void usage(char const *fmt, ...) {
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        putchar('\n');
    }
    printf("Usage: %s -v | -V | [options] file.td0\n"
           "Where -v shows version info and -V shows extended version info\n"
           "\n"
           "Common options are\n"
           " -o file  Write to specified file, extension determines format\n"
           " -n dd    Only cylinders before the number dd are written\n"
           "Image file only options are\n"
           " -k[cdm]* Keep bad sector types rather than generate dummy sectors\n"
           "    c     Use data from a bad CRC sector\n"
           "     d    Use data from deleted data sector\n"
           "      m   Do not generate dummy sectors for missing IDAM entries\n"
           "          Skipped and missing data sectors are always replaced with dummy sectors\n"
           " -soo     Sort tracks in OutOut order\n"
           " -sob     Sort tracks in OutBack order\n"
           "Track sorting, defaults to cylinders in ascending order, alternating head\n"
           "Duplicates of valid sectors are ignored, any with differing content are noted\n",
           invokeName);
    exit(fmt ? 0 : 1);
}

int main(int argc, char *argv[]) {

    uint8_t keep      = 0;
    uint8_t layout    = ALT;
    char *outFile     = NULL;
    int cylinderLimit = -1;

    invokeName        = basename(argv[0]);

#ifdef _WIN32
    char *s = strrchr(invokeName, '.');
    if (s && stricmp(s, ".exe") == 0)
        *s = 0;
#endif
    CHK_SHOW_VERSION(argc, argv);

    int c;
    while ((c = getopt(argc, argv, "k:o:s:n:")) != EOF) {
        switch (c) {
        case 'k':
            while (*optarg) {
                switch (*optarg++) {
                case 'c':
                    keep |= SEC_CRC;
                    break;
                case 'd':
                    keep |= SEC_DAM;
                    break;
                case 'm':
                    keep |= SEC_NOID;
                    break;
                }
            }
            break;
        case 'n':
            cylinderLimit = atoi(optarg);
            break;
        case 's':
            if (strcmp(optarg, "oo") == 0)
                layout = OUTOUT;
            else if (strcmp(optarg, "ob") == 0)
                layout = OUTBACK;
            else
                usage("Unknown -s sort order %s", optarg);
            break;
        case 'o':
            outFile = optarg;
            break;

        default:
            usage("unknown option -%c", c);
        }
    }
    if (argc - optind < 1)
        usage("No input file specified");

    td0Header_t *pHead = openTd0(argv[optind]);
    srcFile            = basename(argv[optind]);
    printf("Processing %s\n", srcFile);

    showHeader(pHead);

    loadDisk();

    if (sizeDisk() && outFile && cylinderLimit != 0) {
        if (cylinderLimit > 0 && cylinderLimit < nCylinder) {
            printf("Saving first %d cylinders only\n", cylinderLimit);
            nCylinder = cylinderLimit; // 0 based cylinders
        }
        saveImage(outFile, pHead, keep, layout);
        printf("Produced %s\n", basename(outFile));
    } else
        printf("No file produced\n");

    closeTd0();
    return 0;
}
