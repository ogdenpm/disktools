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

sizing_t sizing[2][SUSPECT + 1][2]; // [encoding][sSize][head]

track_t disk[MAXCYLINDER][MAXHEAD];

uint8_t nCylinder;
uint8_t nHead;
uint8_t headsUsed;

FILE *fpout;
char *invokeName;
char *srcFile;

/// <summary>
/// emit Fatal error message with '\n' appended to stderr and exit
/// </summary>
/// <param name="fmt">printf compatible format string</param>
/// <param name="">optional parameters</param>
/// <returns>exits with 1</returns>
_Noreturn void fatal(char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fputs("Fatal error: ", stderr);
    vfprintf(stderr, fmt, args);
    va_end(args);
    putc('\n', stderr);
    exit(1);
}

/// <summary>
/// emit warning message with '\n' appended to stdout
/// </summary>
/// <param name="fmt">printf compatible format string</param>
/// <param name="">optional parameters</param>
void warn(char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fputs("Warning: ", stdout);
    vfprintf(stdout, fmt, args);
    va_end(args);
    putc('\n', stdout);
}

#ifdef _MSC_VER
/// <summary>
/// Equivalent to POSIX basename for visual C
/// </summary>
/// <param name="path">full path to file</param>
/// <returns>pointer filename only</returns>

char *basename(char *path) {
    char *s;
    if (path[0] && path[1] == ':') // strip any device
        path += 2;
    while ((s = strpbrk(path, "/\\"))) // allow / or \ directory separators
        path = s + 1;
    return path;
}
#endif

/// <summary>
/// generated a new file name by replacing the ext (if present)
/// </summary>
/// <param name="path">base filename including path</param>
/// <param name="ext">new extent including '.' to append</param>
/// <returns></returns>
char *makeFilename(char *path, char *ext) {
    char *s       = strrchr(basename(path), '.');
    int prefixLen = (int)(s ? s - path : strlen(path));
    char *newFile = safeMalloc(prefixLen + strlen(ext) + 1);
    strncpy(newFile, path, prefixLen);
    strcpy(newFile + prefixLen, ext);
    return newFile;
}

/// <summary>
/// Check if sectors match.
/// </summary>
/// <param name="pSector1">pointer to first sector</param>
/// <param name="pSector2">pointer to second sector</param>
/// <returns>true if cylinder, head, sector Id, sector size and sector content are the
/// same</returns>
bool sameSector(sector_t *pSector1, sector_t *pSector2) {
    return memcmp(pSector1, pSector2, 4) == 0 &&
           pSector1->sSize < 8 && // cyl, head, sec, sSize same
           memcmp(pSector1->data, pSector2->data, 128 << pSector1->sSize) == 0;
}

/// <summary>
/// Check whether two tracks have the same content
/// Sector ordering is ignored and only valid sectors are checked
/// </summary>
/// <param name="pTrack1">pointer to first track</param>
/// <param name="pTrack2">pointer to second track</param>
/// <returns>true if tracks are the same. Note must have at least one valid sector in
/// common</returns>
bool sameTrack(track_t *pTrack1, track_t *pTrack2) {

    if (pTrack1->nUsable == 0 || pTrack2->nUsable == 0)
        return pTrack1->nUsable + pTrack2->nUsable == 0;

    uint16_t good2[256];
    // populate the good sectors on track2
    for (int i = 0; i < 256; i++)
        good2[i] = UNALLOCATED;

    sector_t *ps = pTrack2->sectors;
    for (int i = 0; i < pTrack2->nSec; i++, ps++)
        if ((ps->extraFlag & F_USED) && (ps->flags & ~(SEC_DAM | SEC_DUP)) == 0)
            good2[ps->sec] = i;

    // scan for matching sectors
    bool match = false;
    ps         = pTrack1->sectors;
    for (int i = 0; i < pTrack1->nSec; i++, ps++) {
        if ((ps->extraFlag & F_USED) && (ps->flags & ~(SEC_DAM | SEC_DUP)) == 0 &&
            good2[ps->sec] != UNALLOCATED) {
            if (!sameSector(ps, pTrack2->sectors + good2[ps->sec]))
                return false;
            match = true;
        }
    }
    return match;
}

/// <summary>
/// generate a score from the flags to determine a preferred version of any sector
/// normal sector            16
///      duplicate           15
///      with CRC            14
///      duplicate with CRC  13
/// deleted data sector      12
///      duplicate           11
///      with CRC            10
///      duplicate with CRC  09
/// Skipped sector           08
///      duplicate           07
/// No data                  04
///      duplicate           03
///
/// Could probably be simplified as duplicate does not appear to be used
/// and the only examples of deleted data sectors don't have CRC errors or
/// conflicting normal sectors
/// </summary>
/// <param name="flags">sector flags used in calculation</param>
/// <returns>score value</returns>
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

/// <summary>
/// Analyse the .TD0 sector data
/// Duplicates are removed
/// Inter sector spacing is determined
/// check for CRC errors on first/last sector id
/// check for mixed size sectors
/// check for intra track cylinder/head changes
/// </summary>
/// <param name="pTrack">pointer to the track structure to analyse</param>
void analyseTrack(track_t *pTrack) {
    uint8_t sectorMap[MAXIDAMS]; // list of sectors used. index is  order on track
                                 // value is the index of the corresponding .TD0 sector
    uint8_t secIdUsed[256];      // used to support duplicates, indexed by sector id.
                                 // Value is .TD0 sector index
    memset(secIdUsed, MAXIDAMS, sizeof(secIdUsed));

    // build sectorMap
    sector_t *pIn = pTrack->sectors;
    // there are some rare occasions where the auto sector isn't tagged so fix it
    if (!(pIn->flags & SEC_AUTO) && (pIn->sec == 100 + pTrack->nSec)) {
        pIn->flags |= SEC_AUTO;
        warn("Fixed auto sector tag");
    }
    // first sector may be auto generated if so update track flags
    if (pIn->flags & SEC_AUTO)
        pTrack->tFlags |= TRK_HASAUTO;
    // process each of the .TD0 sectors
    for (uint8_t i = 0; i < pTrack->nSec; i++, pIn++) {
        // process sectors normal sectors only
        if (!(pIn->flags & SEC_AUTO) && pIn->sSize < 8) {
            if (secIdUsed[pIn->sec] == MAXIDAMS) { // first sight of sector id so use it
                sectorMap[pTrack->nUsable] = i;
                secIdUsed[pIn->sec]        = pTrack->nUsable++;
                pIn->extraFlag |= F_USED;
            } else {                                      // we have some form of duplicate
                uint16_t slot      = secIdUsed[pIn->sec]; // position on track
                sector_t *pPrev    = &pTrack->sectors[sectorMap[slot]]; // current .TD0 sector used
                uint8_t nFlags     = pIn->flags;                        // flags of new sector
                uint8_t cFlags     = pPrev->flags;                      // flags of current sector
                bool bothValidData = ((nFlags | cFlags) & ~(SEC_DUP | SEC_DAM)) == 0;

                if (sectorScore(nFlags) > sectorScore(cFlags)) { // better?
                    sectorMap[slot] = i;
                    pIn->extraFlag |= F_USED;
                    pPrev->extraFlag &= ~F_USED;
                    if (bothValidData && !sameSector(pPrev, pIn))
                        pPrev->extraFlag |= F_DUP; // problem duplicate
                } else if (bothValidData == 0 && !sameSector(pPrev, pIn))
                    pIn->flags |= F_DUP; // problem duplicate
            }
        }
    }
    // set track flags if we have some usable sectors
    if (pTrack->nUsable) {
        if (pTrack->tFlags & TRK_HASAUTO)
            sectorMap[pTrack->nUsable++] = 0; // add Auto sector to end of list in case needed
        pTrack->sectorMap = safeMalloc(pTrack->nUsable); // save the sectorMap
        memcpy(pTrack->sectorMap, sectorMap, pTrack->nUsable);
        if (pTrack->tFlags & TRK_HASAUTO)
            pTrack->nUsable--; // revert to exclude the auto sector

        // find first/last sectors and inter sector spacing
        pTrack->first = 255;
        for (int i = 0; i < 256; i++) {
            if (secIdUsed[i] != MAXIDAMS) {
                if (pTrack->first == 255) { // first so initialise
                    pTrack->first   = i;
                    pTrack->spacing = 0;
                } else
                    // check for new spacing, accounting for potential missing IDAMs
                    if (pTrack->spacing == 0 || pTrack->spacing > i - pTrack->last)
                        pTrack->spacing =
                            pTrack->spacing % (i - pTrack->last) == 0 ? i - pTrack->last : 1;
                pTrack->last = i;
            }
        }
        if (pTrack->spacing == 0)
            pTrack->spacing = 1; // avoids / 0 error later

        // set the track flags
        if ((pTrack->last - pTrack->first) / pTrack->spacing + 1 > pTrack->nUsable)
            pTrack->tFlags |= TRK_HASGAP;

        pTrack->trackSize = 0;
        sector_t *ps      = pTrack->sectors;
        sector_t *fs      = NULL; // first used sector
        for (int i = 0; i < pTrack->nSec; i++, ps++) {
            if (ps->extraFlag & F_USED) {
                if (!fs) {
                    fs            = ps;
                    pTrack->sSize = fs->sSize;
                }
                if (ps->cyl != fs->cyl || ps->head != fs->head)
                    pTrack->tFlags |= TRK_NONSTD;
                if ((ps->flags & SEC_CRC) && (ps->sec == pTrack->first || ps->sec == pTrack->last))
                    pTrack->tFlags |= TRK_HASBAD;
                if (fs->sSize != ps->sSize)
                    pTrack->sSize = MIXEDSIZES;
                pTrack->trackSize += 128 << ps->sSize;
            }
        }
        if (pTrack->tFlags & TRK_NONSTD) // Treat non-standard tracks separately for sizing
            pTrack->sSize = SUSPECT;
        else if (pTrack->sSize == MIXEDSIZES) // don't fix mixed size tracks
            pTrack->tFlags |= TRK_NONSTD;
    }
}

/// <summary>
/// Display a track and it's sectors in the order .TD0 saved them
/// Ancillary information is also shown
/// Cylinder/Head encoding real_sector_count [/.TD0_sector_count] '[' sector_size ']' followed by
/// each sector prefixed by # if not used and suffixed by a code if not a normal good sector
/// changes to sector size are indicated by '[' new_size ']'
/// changes to cylinder/hear are indicated by {new_cylinder, new_head}
/// If in showing the sector usage it appears that the track is invalid
/// then it sets the usable sector count to 0. Option -kt overrides this
/// </summary>
/// <param name="pTrack">pointer to the track structure to analyse</param>
/// <param name="pTd0">pointer to the raw .TD0 track structure</param>
/// <param name="options">user specified options</param>
void showSectorUsage(track_t *pTrack, td0Track_t *pTd0, uint16_t options) {
    sector_t *pIn        = pTrack->sectors;
    uint8_t sSize        = pIn->sSize;
    uint8_t cyl          = pTd0->cyl;
    uint8_t cylChangeCnt = 0;
    uint8_t head         = pTd0->head;
    uint8_t col          = 5; // current output column "%2d/%d " already emitted
    uint8_t sCol         = 5; // sector restart col
    uint8_t nValid       = 0;

    col += printf("%s ", pTrack->encoding ? "FM " : "MFM"); // encoding
    if (pTrack->nUsable == pTrack->nSec) // usable sector count [ / td0 sector count]
        col += printf("%3d", pTrack->nSec);
    else
        col += printf("%d/%d", pTrack->nUsable, pTrack->nSec);

    if (sSize < 8) // Only display initial sector size if valid
        col += printf("%*s[%d]", 15 - col, "", 128 << sSize); // Initial sector size
    sCol = col;

    // show {cylinder, head} if the first sector is non standard
    if (pTrack->sectors[0].cyl != pTd0->cyl || pTrack->sectors[0].head != pTd0->head) {
        cyl  = pTrack->sectors[0].cyl;
        head = pTrack->sectors[0].head;
        printf(" {%d/%d}", cyl, head);
    }

    // show all of the sectors from the .TD0 file
    for (uint8_t i = 0; i < pTrack->nSec; i++, pIn++) {
        char info[64]; // single sector info string used to support multi line formatting
        char *s = info;

        // check for sector size changes or abnormal sector sizes
        if (pIn->sSize >= 8) // If sector size is non standard emit the associated code
            s += sprintf(s, " [0x%x] ", pIn->sSize);
        else if (sSize != pIn->sSize) // print sector size changes
            s += sprintf(s, " [%d]", 128 << (sSize = pIn->sSize));

        // check for cylinder or head changes
        if (pIn->cyl != cyl || pIn->head != head) {
            cyl  = pIn->cyl;
            head = pIn->head;
            s += sprintf(s, " {%d/%d}", cyl, head);
            cylChangeCnt++;
        }

        // for used sectors, update the trackSize and the valid sector count
        if (pIn->extraFlag & F_USED) {
            s += sprintf(s, " %2d", pIn->sec);
            if ((pIn->flags & ~(SEC_DAM | SEC_DUP | SEC_DOS)) == 0)
                nValid++;
        } else
            s += sprintf(s, " #%d", pIn->sec); // mark unused sectors

        // print any suffix code associated with the sector
        if (pIn->extraFlag & F_DUP)         // was a duplicate sector with different content!!
            *s++ = 'P';                     // problem sector
        if (pIn->flags) {                   // record the code associated with the flags
            for (uint8_t i = 0; i < 7; i++) // 8 & 0x80 not used
                // SEC_DUP   -> 'D'
                // SEC_CRC   -> 'c'
                // SEC_DAM   -> 'd'
                // SEC_DOS   -> 'u'  unused
                // SEC_NODAT -> 'n'  no data
                // SEC_AUTO  -> '?'  sector id was auto generated
                if (pIn->flags & (1 << i))
                    *s++ = "Dcd.un?"[i];
        }
        *s = '\0';
        // see if the sector string fits on line if not start new line
        if (col + (s - info) > 100) {
            printf("\n%*s", sCol, "");
            col = sCol;
        }
        col += printf("%s", info);
    }
    // check whether the track was actually valid
    char *reason = NULL;
    // due to a bug in Teledisk, if there are no IDAMs it may generate a dummy IDAM
    // using some data from the previous track access
    if (pTrack->nUsable == 0) {
        reason = "no real Address Markers";
        options &= ~K_TRACK; // always spurious
    } else if (nValid == 0)
        reason = "no good sectors on track";
    else if (pTrack->nUsable < 3 &&
             pTrack->trackSize < 8192) // size check is because 2 8192 sectors is valid
        reason = "< 3 Address Markers";
    else if (cylChangeCnt == 0 && cyl != pTd0->cyl && sameTrack(pTrack, &disk[cyl][pTd0->head]))
        reason = "duplicate track";

    if (reason) { // if invalid, show the problem. -kt allows it anyway!!
        char *prefix = (options & K_TRACK) ? "Warning " : "Spurious";
        if (col + strlen(prefix) + strlen(reason) + 4 > 100)
            printf("\n%*s", sCol, "");
        printf(" - %s %s", prefix, reason);
        if (!(options & K_TRACK))
            pTrack->nUsable = 0; // will not be used
        else
            pTrack->sSize = SUSPECT; // lump with with other suspect tracks for sizing purposes
    }
    putchar('\n');
}

/// <summary>
/// Load the tracks/sectors from a .TD0 file
/// The core work of reading the file is done via readTrack
/// Here any processing of the track is invoked and the
/// sector layout/usage is shown
/// For tracks with all good sectors the data to help determine
/// sector range and spacing is updated
/// </summary>
/// <param name="options"></param>
void loadDisk(uint16_t options) {

    td0Track_t *pTd0;
    while ((pTd0 = readTrack())) {
        uint8_t encoding = pTd0->head >> 7; // 1 = FM, 0 = MFM
        pTd0->head &= 0x7f;
        if (pTd0->cyl >= MAXCYLINDER || pTd0->head >= MAXHEAD)
            fatal("Track %d/%d beyond supported limits", pTd0->cyl, pTd0->head);

        track_t *pTrack = &disk[pTd0->cyl][pTd0->head];
        if (pTrack->sectors)
            warn("Track %d/%d - skipping duplicate track", pTd0->cyl, pTd0->head);
        else if (pTd0->sectors == NULL)
            warn("Track %d/%d - no sectors", pTd0->cyl, pTd0->head);
        else {
            pTrack->nSec     = pTd0->nSec;
            pTrack->sectors  = pTd0->sectors;
            pTrack->encoding = encoding;

            analyseTrack(pTrack);

            printf("%2d/%d ", pTd0->cyl, pTd0->head);
            if (pTrack->nSec) {
                showSectorUsage(pTrack, pTd0, options);
                if (pTrack->nUsable) { // showSector usage updates nUsable for spurious data
                    pTd0->sectors = 0; // claim onwership of memory

                    if ((pTrack->tFlags & ~TRK_HASAUTO) ==
                        0) { // only good tracks are used for geometry guess
                        sizing_t *si = &sizing[encoding][pTrack->sSize][pTd0->head];
                        si->count++;
                        si->first += pTrack->first;
                        si->last += pTrack->last;
                        si->spacing += pTrack->spacing;
                    }
                    if (pTd0->cyl >= nCylinder)
                        nCylinder = pTd0->cyl + 1;
                    if (pTd0->head >= nHead)
                        nHead = pTd0->head + 1;
                    headsUsed |= 1 << pTd0->head; // used to catch all of head 0 being invalid
                }
            } else
                printf("No usable sectors\n");
        }
    }
}

/// <summary>
/// Processes a track to fix the sector range and inter sector spacing if needed
/// Will not fix if the read sector range falls outside the range determined by fixSizing()
/// If option -ka or -KA is specified and there is one misssing IDAM and an auto generated
/// sector is present, it uses it to replace the missign IDAM
/// </summary>
/// <param name="cylinder">Cylinder to process</param>
/// <param name="head">Head to process</param>
/// <param name="options">options to consider</param>
void fixRange(uint8_t cylinder, uint8_t head, uint16_t options) {

    track_t *pTrack = &disk[cylinder][head];
    if (pTrack->nSec < 3)
        return;
    sizing_t si     = sizing[pTrack->encoding][pTrack->sSize][head]; // expected layout

    uint8_t first   = (uint8_t)si.first;
    uint8_t last    = (uint8_t)si.last;
    uint8_t spacing = (uint8_t)si.spacing;
    uint8_t nSec    = (last - first) / spacing + 1;

    if (pTrack->first < first || pTrack->last > last)
        pTrack->sSize = SUSPECT;

    if ((pTrack->tFlags & TRK_NONSTD) || pTrack->first < first || pTrack->last > last ||
        pTrack->spacing != spacing) {
        first   = pTrack->first;
        last    = pTrack->last;
        spacing = pTrack->spacing;
        nSec    = (last - first) / pTrack->spacing + 1;
        if ((pTrack->tFlags & TRK_NONSTD) || nSec - pTrack->nUsable > 2) {
            warn("%d/%d: Non standard track using sector range %d-%d%s", cylinder, head, first,
                 last, pTrack->nUsable == nSec ? "" : " Possible missing IDAMs");

            if ((pTrack->tFlags & TRK_HASGAP))
                pTrack->sSize = SUSPECT;

            pTrack->tFlags |= TRK_NONSTD;
        } else
            warn("%d/%d: Non standard track using sector range %d-%d", cylinder, head, first, last);
    }
    if ((pTrack->tFlags & TRK_HASAUTO) && (options & K_AUTO) && !(pTrack->tFlags & TRK_NONSTD)) {
        if (nSec - pTrack->nUsable != 1)
            warn("%d/%d: Cannot identify sector id for auto sector", cylinder, head);
        else {
            bool found = true;
            for (int sec = first; sec <= last; sec += spacing) {
                found = false;
                for (int slot = 0; !found && slot < pTrack->nUsable; slot++)
                    found = pTrack->sectors[pTrack->sectorMap[slot]].sec == sec;
                if (!found) {
                    pTrack->nUsable++;            // we reserved the slot map entry in analyseSector
                    pTrack->sectors[0].sec = sec; // give the auto sector a valid sector Id
                    pTrack->trackSize += 128 << pTrack->sectors[0].sSize;
                    warn("%d/%d: Using auto generated sector as sector %d", cylinder, head, sec);
                    break;
                }
            }
        }
    }
    if (options & K_GAP)
        pTrack->tFlags |= TRK_NONSTD;
    else if (!(pTrack->tFlags & TRK_NONSTD) && nSec - pTrack->nUsable != 0)
        warn("%d/%d: %d missing IDAMs", cylinder, head, nSec - pTrack->nUsable);
    pTrack->first = first;
    pTrack->last  = last;
}

/// <summary>
/// Works out the most likely sector range and inter sector spacing based on
/// information collected from good tracks
/// It displays any track formats it derives
/// </summary>
void fixSizing() {
    printf("Assumed standard track format(s)\n");
    for (int sSize = 0; sSize <= SUSPECT; sSize++) {
        for (int enc = 0; enc <= 1; enc++) {
            for (int head = 0; head < nHead; head++) {
                sizing_t *ps = &sizing[enc][sSize][head];
                if (ps->count) {
                    ps->first   = (ps->first + ps->count / 2) / ps->count;
                    ps->last    = (ps->last + ps->count / 2) / ps->count;
                    ps->spacing = (ps->spacing + ps->count / 2) / ps->count;
                    printf("side %d: %s sector size %d range %d-%d", head, enc ? "FM" : "MFM",
                           128 << sSize, ps->first, ps->last);
                    if (ps->spacing > 1)
                        printf("/%d", ps->spacing);
                    putchar('\n');
                    ps->count = 0;
                }
                if (ps->spacing == 0)
                    ps->spacing = 1;
            }
        }
    }
}

/// <summary>
/// Shows a guess at the underlying disk format and sizing
/// </summary>
/// <returns>returns true if the disk size is > 0</returns>
bool sizeDisk() {
    uint32_t diskSize = 0;
    printf("Appears to be");

    uint8_t head = 0;
    for (track_t *p = &disk[0][0]; p < &disk[nCylinder][nHead]; p++, head = !head) {
        if (p->nUsable)
            sizing[p->encoding][p->sSize][head].count++;
    }
    for (uint8_t i = 0; i <= SUSPECT; i++) {
        for (uint8_t enc = 0; enc < 2; enc++) { // 0->MFM, 1->FM
            char *encoding = enc ? "FM" : "MFM";
            int partSize   = 0;
            sizing_t *si   = sizing[enc][i];
            if (si[0].count || si[1].count) {
                if (i < MIXEDSIZES) {
                    uint8_t nSec0 =
                        si[0].count ? (si[0].last - si[0].first) / si[0].spacing + 1 : 0;
                    uint8_t nSec1 =
                        si[1].count ? (si[1].last - si[1].first) / si[1].spacing + 1 : 0;
                    if (si[0].count == si[1].count && nSec0 == nSec1)
                        printf(" [%dx2x%d", si[0].count, nSec0);
                    else if (si[1].count == 0)
                        printf(" [%s%dx%d", nHead == 2 ? "0:" : "", si[0].count, nSec0);
                    else if (si[0].count == 0)
                        printf(" [1:%dx%d", si[1].count, nSec1);
                    else
                        printf(" [(0:%dx%d 1:%dx%d)", si[0].count, nSec0, si[1].count, nSec1);
                    partSize = (si[0].count * nSec0 + si[1].count * nSec1) * (128 << i);
                    printf(" %d", 128 << i);

                } else {
                    for (track_t *p = &disk[0][0]; p < &disk[nCylinder][nHead]; p++)
                        if (p->nUsable && p->encoding == enc && p->sSize == i)
                            partSize += p->trackSize;

                    if (si[1].count == 0)
                        printf(" [%s%d", nHead == 2 ? "0:": "", si[0].count);
                    else if (si[0].count == 0)
                        printf(" [1:%d", si[1].count);
                    if (si[0].count && si[1].count)
                        printf(" 1:%d", si[1].count);
                    printf(" %s", i == MIXEDSIZES ? "mixed size" : "non standard");
                }
                diskSize += partSize;
                printf(" %s sector %dK]", encoding, partSize / 1024);
            }
        }
    }

    printf(" %dK\n\n", diskSize / 1024);
    return diskSize > 0;
}

/// <summary>
/// Display the key information from the TD0 header in line with TD0CHECK v2.23
/// Also compacts any commend to remove trailing newlines / spaces
/// </summary>
/// <param name="pHead">pointer to the header content</param>
void showHeader(td0Header_t *pHead) {
    static char *densities[] = { "250kbps low", "300kbps low", "high",    "high",
                                 "extended",    "extended",    "unknown", "unknown" };

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
    uint8_t density = pHead->density & 6;
    printf("Source was %s %s-density %s\n", source, densities[density],
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

/// <summary>
/// Display command line usage.
/// Optionally display a message
/// </summary>
/// <param name="fmt">Message to display before usage with usual printf options. NULL if there is no
/// message</param> <param name="">Optional arguments driven by printf formatting options in
/// fmt</param> <returns>Always exits. If fmt not NULL exits with 1 else exits with 0</returns>
_Noreturn void usage(char const *fmt, ...) {
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        putchar('\n');
    }
    printf(
        "Usage: %s -v | -V | [options] file.td0 [outfile|.ext]\n"
        "Where -v shows version info and -V shows extended version info\n"
        "If present outfile or .ext are used to specify the name of the converted file\n"
        "The .ext option is a short hand for using the input file with the .td0 replaced by .ext\n"
        "If the outfile ends in .imd, the IMD file format is used else a binary image is created\n"
        "Options are\n"
        " -c dd       Only cylinders before the number dd are written\n"
        " -k[aAcdgt]* Keep spurious information\n"
        "    a        Attempt to place auto generated sectors - use with caution\n"
        "     A       As 'a' but also tag with CRC error in IMD images\n"
        "      c      Use data from a bad CRC sectors - implied for IMD\n"
        "       d     Use data from deleted data sectors - implied for IMD\n"
        "         g   Keep sector gaps - implied for IMD\n"
        "          t  Use spurious tracks\n"
        " -to         Process side 1 tracks then side 2 tracks both in ascending order (Out-Out)\n"
        " -tb         As -to but side 2 tracks are processed in descending order (Out-Back)\n"
        "Track sorting defaults to cylinders in ascending order, alternating head\n"
        "Skipped and missing data sectors are always replaced with dummy sectors\n"
        "Duplicates of valid sectors are ignored, any with differing content are noted\n",
        invokeName);
    exit(fmt ? 0 : 1);
}

/// <summary>
/// parse the command line and orchestrate the load, analysis and file conversion
/// </summary>
/// <param name="argc">standard argc</param>
/// <param name="argv">standard argv</param>
/// <returns>0</returns>
int main(int argc, char *argv[]) {

    uint16_t options  = 0;
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
    while ((c = getopt(argc, argv, "k:t:c:")) != EOF) {
        switch (c) {
        case 'k':
            while (*optarg) {
                switch (*optarg++) {
                case 'c':
                    options |= K_CRC;
                    break;
                case 'd':
                    options |= K_DAM;
                    break;
                case 'A':
                    options |= K_AUTOCRC;
                case 'a':
                    options |= K_AUTO;
                    break;
                case 't':
                    options |= K_TRACK;
                    break;
                }
            }
            break;
        case 'c':
            cylinderLimit = atoi(optarg);
            break;
        case 't':
            if (*optarg == 'o')
                options = (options & ~S_OB) | S_OO;
            else if (*optarg == 'b')
                options = (options & ~S_OO) | S_OB;
            else
                usage("Unknown option -t%s", optarg);
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

    if (argc - optind > 1) {
        if (argv[optind + 1][0] == '.' && argv[optind + 1] == basename(argv[optind + 1]))
            outFile = makeFilename(argv[optind], argv[optind + 1]);
        else
            outFile = argv[optind + 1];
    }

    showHeader(pHead);

    loadDisk(options); // build the internal track structure from the .TD0 file
    fixSizing();       // determine likely track formats

    if (cylinderLimit > 0 && cylinderLimit < nCylinder) {
        printf("Limiting to first %d cylinders\n", cylinderLimit);
        nCylinder = cylinderLimit; // 0 based cylinders
    }
    if (!(headsUsed & 1))
        options |= NOHEAD0;
    // fixup missing IDAMs if possible
    for (int cylinder = 0; cylinder < nCylinder; cylinder++)
        for (int head = 0; head < nHead; head++)
            fixRange(cylinder, head, options);

    // show the expected disk size and save it if requested
    if (sizeDisk() && outFile && nCylinder != 0) {
        saveImage(outFile, pHead, options);
        printf("Produced %s\n", basename(outFile));
    } else
        printf("No file produced\n");

    closeTd0();
    return 0;
}
