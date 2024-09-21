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

sizing_t sizing[2][9][2]; // [encoding][sSize][head]

track_t tracks[MAXCYLINDER][MAXHEAD];

uint8_t nCylinder;
uint8_t nHead;

FILE *fpin, *fpout;
char *invokeName;
patBuf_t patBuf;
comment_t cHead;

fileHdr_t fHead;
char *td0File;

unsigned char docomp = 0; // global stepping set to 1 if advanced compression

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

#ifdef _MSC_VER
char *basename(char *path) {
    char *s;

    if (path[0] && path[1] == ':')
        path += 2;
    while ((s = strpbrk(path, "/\\")))
        path = s + 1;
    return path;
}
#endif

// auto expanding allocation for i block

bool cread(void *buf, int len) {
    return docomp ? fHead.ver > 19 ? decodeNew(buf, len) : decodeOld(buf, len)
                  : readIn(buf, len);
}

char *months[] = { "January", "February", "March",     "April",   "May",      "June",
                   "July",    "August",   "September", "October", "November", "December" };

void doComment() {

    if (!cread(&cHead, sizeof(struct commentHdr_t)))
        fatal("Failed to read comment header");
    uint16_t len = WORD(cHead.len); // portability
    if (len > MAXCOMMENT || !cread(cHead.comment, len))
        fatal("Failed to read comment text");

    // check all but 1st 2 bytes of comment region
    uint16_t crc = do_crc(cHead.len, 8, 0);         // header
    crc          = do_crc(cHead.comment, len, crc); // comment data
    if (WORD(cHead.crc) != crc)
        warn("Crc error in comment header");

    printf("Created on %s %2d, %d at %2d:%02d:%02d\n", months[cHead.mon], cHead.day,
           cHead.yr + 1900, cHead.hr, cHead.min, cHead.sec);

    // trim trailing newlines and spaces
    cHead.comment[len] = '\0'; // make sure we terminate
    while (len && (cHead.comment[len - 1] == ' ' || cHead.comment[len - 1] == '\0'))
        len--;
    if (len == 0)
        strcpy(cHead.comment, "Comment text is blank\n");
    else {
        for (uint16_t i = 0; i < len; i++)
            if (!cHead.comment[i])
                cHead.comment[i] = '\n';
        strcpy(cHead.comment + len, "\n");
    }
}

void doSector(sector_t *sector) {
    static uint8_t sectorBuf[MAXPATTERN * 2];
    uint8_t *ptr = sectorBuf;

    if (!cread(&sector->hdr, sizeof(sector->hdr)))
        fatal("Premature EOF reading sector header\n");

    sector->flags = 0;
    if ((sector->hdr.sSize & 0xf8) || (sector->hdr.sFlags & 0x30)) {
        sector->data = NULL; // no data available
        if ((do_crc(&sector->hdr, 5, 0) & 0xff) != sector->hdr.crc)
            warn("%d/%d/%d: Sector header CRC error", sector->hdr.trk, sector->hdr.head,
                 sector->hdr.sec);
    } else {
        if (!cread(&patBuf.len, sizeof(patBuf.len)))
            fatal("Premature EOF reading pattern length\n");
        int16_t len = WORD(patBuf.len);
        if (len == 0)
            fatal("Corrupt file: no pattern type");
        if (!cread(&patBuf.pattern, len))
            fatal("Premature EOF reading pattern");
        switch (patBuf.pattern[0]) {
        case COPY:
            if (--len)
                memcpy(ptr, &patBuf.pattern[1], len);
            ptr += len;
            break;
        case REPEAT:
            if (len < 5)
                fatal("Corrupt file: short Repeat pattern");
            int16_t rept = patBuf.pattern[1] + (patBuf.pattern[2] << 8);
            if (rept < 0)
                fatal("Corrupt file: bad repeat count in Repeat pattern");

            while (rept-- > 0) {
                *ptr++ = patBuf.pattern[3];
                *ptr++ = patBuf.pattern[4];
            }
            break;
        case RLE:
            uint16_t step;
            for (uint16_t i = 1; i < len; i += step) {
                switch (patBuf.pattern[i]) {
                case COPY:
                    if (i + 1 > len || patBuf.pattern[i + 1] + 2 > len)
                        fatal("Corrupt file: short RLE-Copy pattern");
                    memcpy(ptr, &patBuf.pattern[i + 2], patBuf.pattern[i + 1]);
                    ptr += patBuf.pattern[i + 1];
                    step = patBuf.pattern[i + 1] + 2;
                    break;
                case REPEAT:
                    if (i + 3 > len)
                        fatal("Corrupt file: short RLE-Repeat pattern");
                    for (int j = 0; j < patBuf.pattern[i + 1]; j++) {
                        *ptr++ = patBuf.pattern[i + 2];
                        *ptr++ = patBuf.pattern[i + 3];
                    }
                    step = 4;
                    break;
                default:
                    fatal("Corrupt file: unknown RLE subtype %d", patBuf.pattern[i]);
                }
            }
            break;
        default:
            fatal("Corrupt file: unknown pattern type %d", patBuf.pattern[0]);
            break;
        }

        int16_t dlen = (int)(ptr - sectorBuf);

        if ((do_crc(sectorBuf, dlen, 0) & 0xff) != sector->hdr.crc) {
            warn("%d/%d/%d: Sector header CRC error", sector->hdr.trk, sector->hdr.head,
                 sector->hdr.sec);
        }

        int sSize = 128 << sector->hdr.sSize;
        if (dlen != sSize)
            fatal("Corrupt file: Sector content length %d, expected %d", dlen, sSize);

        sector->data = malloc(sSize);
        memcpy(sector->data, sectorBuf, dlen);
    }
}

bool sameSector(sector_t *sector1, sector_t *sector2) {
    if (sector1->hdr.trk != sector2->hdr.trk || sector1->hdr.head != sector2->hdr.head ||
        sector1->hdr.sSize != sector2->hdr.sSize)
        return false;
    if ((sector1->data && !sector2->data) || !sector1->data && sector2->data)
        return false;
    return sector1->data == NULL ||
           memcmp(sector1->data, sector2->data, 128 << sector1->hdr.sSize) == 0;
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

void buildSectorMap(track_t *track, uint8_t nsec) {
    uint8_t sectorMap[256];
    uint8_t nReal = 0;  // number of real sectors recorded
    uint16_t used[256]; // used to support duplicates
    for (int i = 0; i < 256; i++)
        used[i] = UNALLOCATED;

    // build sectorMap
    sector_t *pIn = track->sectors;
    for (uint8_t i = 0; i < nsec; i++, pIn++) {
        // process sectors with valid IDAMs
        if ((pIn->hdr.sFlags & SEC_NOID) || (pIn->hdr.sSize & 0xf8) != 0)
            ;                                         // ignore if no IDAM, unknown size
        else if (used[pIn->hdr.sec] == UNALLOCATED) { // first sight of sector id so use it
            if (nReal == 0)
                track->first = track->last = pIn->hdr.sec;
            else if (pIn->hdr.sec < track->first)
                track->first = pIn->hdr.sec;
            else if (pIn->hdr.sec > track->last)
                track->last = pIn->hdr.sec;
            sectorMap[nReal]   = i;
            used[pIn->hdr.sec] = nReal++;
        } else {
            uint16_t prevId = used[pIn->hdr.sec];
            sector_t *pRef  = &track->sectors[sectorMap[prevId]];
            uint8_t rScore  = sectorScore(pRef->hdr.sFlags);
            uint8_t iScore  = sectorScore(pIn->hdr.sFlags);
            if (iScore > rScore) { // better?
                track->sectorMap[prevId] = i;
                if ((iScore == 16 || iScore == 12) && rScore + 1 == iScore &&
                    !sameSector(pRef, pIn))
                    pRef->flags |= F_DUP; // problem duplicate
            } else if ((rScore == 16 || rScore == 12) &&
                       (rScore == iScore || rScore == iScore + 1) && !sameSector(pRef, pIn))
                pIn->flags |= F_DUP; // problem duplicate
        }
    }
    // allocate sectorMap
    track->mapLen    = nReal;
    track->sectorMap = malloc(nReal);
    memcpy(track->sectorMap, sectorMap, nReal);

    pIn = track->sectors;

    printf("%s ", track->encoding == FM ? "FM " : "MFM");

    track->sSize     = 0xff;
    track->trackSize = 0;
    uint8_t sSize    = 0xff;
    for (uint8_t i = 0; i < nsec; i++, pIn++) {
        if (i && i % 26 == 0)
            printf("\n                ");
        uint8_t flags = pIn->hdr.sFlags;
        if ((flags & SEC_NOID))
            printf(" [??]");
        else {
            bool isUsed = used[pIn->hdr.sec] != UNALLOCATED && sectorMap[used[pIn->hdr.sec]] == i;
            if (pIn->hdr.sSize & 0xf8)
                printf(" (0x%x) ", pIn->hdr.sSize);
            else if (sSize != pIn->hdr.sSize)
                printf(" (%d) ", 128 << (sSize = pIn->hdr.sSize));

            if (isUsed) {
                printf(" %d", pIn->hdr.sec);
                if (track->sSize == 0xff)
                    track->sSize = pIn->hdr.sSize;
                else if (track->sSize != pIn->hdr.sSize)
                    track->sSize = MIXEDSIZES;
                track->trackSize += 128 << sSize;
            } else
                printf(" [%d", pIn->hdr.sec);
            if (pIn->flags & F_DUP)
                putchar('x');
            for (uint8_t i = 0; i < 6; i++) // 0x80 not used and 0x40 handled as SEC_NOID
                if (flags & (1 << i))
                    putchar("dcr?sm"[i]);
            if (!isUsed)
                putchar(']');
        }
    }

    putchar('\n');
}

bool doTrack() {
    trkHdr_t trk;
    track_t *track;
    trk.nsec = 0;       // partial read will overwrite

    bool ok = cread(&trk, sizeof(trk));

    if (trk.nsec == 0xff)
        return false;

    if (!ok)
        fatal("Corrupt file, failed to read track header");

    if (trk.crc != (do_crc(&trk, 3, 0) & 0xff))
        warn("crc error in track record %d", trk.trk);

    if (trk.trk >= MAXCYLINDER)
        fatal("Cylinder number %d too large", trk.trk);
    uint8_t encoding = trk.head & 0x80 ? FM : MFM;
    trk.head &= 0x7f;
    if ((trk.head) >= MAXHEAD)
        fatal("For track %d head %d too large", trk.trk, trk.head);
    if (tracks[trk.trk][trk.head].sectors) {
        warn("Overwriting duplicate track %d head %d", trk.trk, trk.head);
        free(tracks[trk.trk][trk.head].sectors);
    }

    track = &tracks[trk.trk][trk.head];

    printf("%2d/%d ", trk.trk, trk.head);

    // track->sectorMap = NULL; already initialised to NULL
    if (trk.nsec == 0)
        printf("No sectors\n");
    else {
        bool haveSector = false;
        track->sectors  = malloc(trk.nsec * sizeof(sector_t));
        // read in all of the sectors
        sector_t *pIn = track->sectors;
        for (uint8_t i = 0; i < trk.nsec; i++, pIn++) {
            doSector(&track->sectors[i]);
            // find sector Id bounds, ingore invalid IDAMs
            if ((pIn->hdr.sFlags & SEC_NOID) == 0)
                haveSector = true;
        }
        // allocate sectorMap
        if (haveSector) {
            track->encoding = encoding;
            buildSectorMap(track, trk.nsec);

            sizingsUsed |= (1 << track->sSize);
            encodingsUsed |= encoding;
            sizing_t *si = &sizing[encoding - 1][track->sSize][trk.head];
            if (si->count++ == 0 || si->first > track->first) {
                si->first = track->first;
                si->nSec  = track->mapLen;
            }
            if (si->nSec < track->mapLen)
                si->nSec = track->mapLen;
            if (trk.trk >= nCylinder)
                nCylinder = trk.trk + 1;
            if (trk.head >= nHead)
                nHead = trk.head + 1;
        } else
            printf("No valid sectors\n");
    }
    return true;
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
        (nHead == 1 || sizing[encodingsUsed - 1][firstSizing][0].nSec ==
                           sizing[encodingsUsed - 1][firstSizing][1].nSec)) {
        // we have a disk with a simple disk
        sizing_t *si = &sizing[encodingsUsed - 1][firstSizing][0];

        printf("Appears to be %d%sx%d %d %s sector", nCylinder, nHead == 2 ? "x2" : "", si->nSec,
               128 << firstSizing, encodingsUsed == MFM ? "MFM" : "FM");
        diskSize = nCylinder * nHead * si->nSec * (128 << firstSizing);
    } else { // complex disk format
        // we have a complex disk layout
        diskSize = 0;
        printf("Appears to be");
        for (uint8_t enc = FM; enc >= MFM; enc--) {
            char *encoding = enc == MFM ? "MFM" : "FM";

            for (uint8_t i = 0; i <= MIXEDSIZES; i++) {
                sizing_t *si = sizing[enc - 1][i];
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
                        for (track_t *p = &tracks[0][0]; p < &tracks[nCylinder][nHead]; p++)
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

void showHeader(fileHdr_t *head) {
    static char *densities[] = { "low", "high", "extended", "unknown" };

    printf("Produced with version %d.%d\n", head->ver / 10, head->ver % 10);
    printf("%s data compression was used\n", head->sig[0] == 'T' ? "Normal"
                                             : head->ver > 19    ? "New Advanced"
                                                                 : "Old Advanced");
    printf("%s sectors were copied\n", head->dosMode ? "DOS Allocated" : "All");
    printf("%s checked\n", head->surfaces == 1 ? "One side was" : "Both sides were");

    char *source = "unknown type";
    switch (head->driveType) {
    case 0:
        source = (head->stepping & 2) ? "5 1/4\"" : "5 1/4\" 96 tpi in a 48 tpi";
        break;
    case 1:
        source = "5 1/4\"";
        break;
    case 2:
        source = (head->dataRate & 6)   ? "5 1/4\""
                 : (head->stepping & 1) ? "5 1/4\" 48 tpi"
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

    if (head->stepping & HAS_COMMENT)
        doComment();
    else
        strcpy(cHead.comment, "No comment information\n");

    printf("Type = %d, stepping = %02X, dataRate = %02X\n", head->driveType, head->stepping,
           head->dataRate);
    printf("Source was %s %s-density %s\n", source, densities[(head->dataRate & 6) >> 1],
           (head->dataRate & 0x80) ? "FM" : "MFM");
    printf("\n%s\n", cHead.comment);
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
           " -k[cdm]* Keep errored sector types rather than generate dummy sectors\n"
           "    c     Use data from CRC errored sector\n"
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

    uint8_t keep        = 0;
    uint8_t layout      = ALT;
    char const *outFile = NULL;
    int cylinderLimit   = -1;

    invokeName          = basename(argv[0]);

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

    td0File = argv[optind];
    if ((fpin = fopen(td0File, "rb")) == NULL)
        fatal("Could not open %s", td0File);


    printf("Processing %s\n", basename(td0File));

    if (fread(&fHead, 1, sizeof(fHead), fpin) != sizeof(fHead) || fHead.sig[2] != '\0' ||
        (strcmp(fHead.sig, "TD") != 0 && strcmp(fHead.sig, "td") != 0))
        fatal("Not a TD0 file");

    if (WORD(fHead.crc) != do_crc(&fHead, 10, 0)) // check for corruption
        warn("CRC error in file header");

    // check for advanced compression
    if (fHead.sig[0] == 't') {
        docomp = 1;                                      // set global stepping to redirect read()
        fHead.ver > 19 ? initNew() : initOld(); // initialize decompression routine
    }

    showHeader(&fHead);

    while (doTrack())
        ;

    if (sizeDisk() && outFile && cylinderLimit != 0) {
        if (cylinderLimit > 0 && cylinderLimit < nCylinder) {
            printf("Saving first %d cylinders only\n", cylinderLimit);
            nCylinder = cylinderLimit; // 0 based cylinders
        }
        saveImage(outFile, keep, layout);

    } else
        printf("No file produced\n");

    if (fpin)   // may have already been closed during chaining attempt
        fclose(fpin);
    return 0;
}
