
#include "td02imx.h"
#include <time.h>

typedef struct {
    uint8_t first;
    uint8_t last;
    bool autoGen;
    bool noUnallocated;
} range_t;

/// <summary>
/// Writes a dummy sector filled with a repeated copy of the message supplied
/// </summary>
/// <param name="fp">The FILE stream to write to</param>
/// <param name="msg">The repeated message to write. It is padded to 16 chars</param>
/// <param name="size">The sector size to write</param>
void writeBadSector(FILE *fp, char *msg, uint16_t size) {
    char buf[16];
    memset(buf, ' ', sizeof(buf));
    memcpy(buf, msg, strlen(msg));
    while (size) {
        uint16_t len = size > sizeof(buf) ? sizeof(buf) : size;
        if (fwrite(buf, 1, len, fp) != len)
            fatal("Write error");
        size -= len;
    }
}

/// <summary>
/// writes the IMD header
/// </summary>
/// <param name="fp">Output file steam</param>
/// <param name="pHead">Pointer to the td0 header information</param>
void saveIMDHdr(FILE *fp, td0Header_t *pHead) {
    fprintf(fp, "IMD 1.20: ");
    if (pHead->stepping & HAS_COMMENT) // had a comment so use time from original td0 file
        fprintf(fp, "%02d/%02d/%04d %02d:%02d:%02d\r\n", pHead->day, pHead->mon + 1,
                pHead->yr + 1900, pHead->hr, pHead->min, pHead->sec);
    else {
        time_t nowTime;
        struct tm *now;
        time(&nowTime);
        now = localtime(&nowTime);

        fprintf(fp, "%02d/%02d/%04d %02d:%02d:%02d\r\n", now->tm_mday, now->tm_mon + 1,
                now->tm_year + 1900, now->tm_hour, now->tm_min, now->tm_sec);
    }

    fprintf(fp, "Converted from %s\r\n", srcFile);

    for (char *s = pHead->comment; *s; s++) {
        if (*s == '\n')
            putc('\r', fp);
        putc(*s, fp);
    }
    putc(0x1a, fp);
}

/// <summary>
/// Sames the values in a sector for IMD compression
/// </summary>
/// <param name="buf">pointer to data</param>
/// <param name="len">length of data</param>
/// <returns>true if all bytes are same</returns>
bool sameValues(uint8_t *buf, uint16_t len) {
    for (int i = 1; i < len; i++)
        if (buf[i] != buf[0])
            return false;
    return true;
}

/// <summary>
/// Saves an IMD sector tpye and content.
/// </summary>
/// <param name="fp">file stream to write to</param>
/// <param name="pSector">pointer to the sector information</param>
/// <param name="options">The user specified options</param>
void saveIMDSector(FILE *fp, sector_t *pSector, uint16_t options) {

    if (!pSector || (pSector->flags & SEC_NODAT)) // no data
        putc(0, fp);
    else if (pSector->flags & SEC_DOS) { // for DOS generate a sector of all 0xe5
        putc(2, fp);
        putc(0xe5, fp);
    } else { // generate the sector type and its content
        uint8_t type   = pSector->flags & SEC_DAM ? 3 : 1;
        uint16_t sSize = 128 << pSector->sSize;
        if ((pSector->flags & SEC_CRC) || ((pSector->flags & SEC_AUTO) && (options & K_AUTOCRC)))
            type += 4;
        if (sameValues(pSector->data, sSize)) {
            putc(type + 1, fp);
            putc(pSector->data[0], fp);
        } else {
            putc(type, fp);
            if (fwrite(pSector->data, 1, sSize, fp) != sSize)
                warn("Write error");
        }
    }
}

/// <summary>
/// Saves an IMD track and its sectors
/// </summary>
/// <param name="fp">file stream to write to</param>
/// <param name="pHead">pointer to the td0 header for density info</param>
/// <param name="cylinder">The cylinder to write</param>
/// <param name="head">The head to use</param>
/// <param name="options">The user options</param>
void saveIMDTrack(FILE *fp, td0Header_t *pHead, uint8_t cylinder, uint8_t head, uint16_t options) {

    track_t *pTrack = &disk[cylinder][head];
    if (!pTrack->sectors) {
        warn("%d/%d: track missing", cylinder, head);
        return;
    }
    if (pTrack->sSize == MIXEDSIZES) {
        warn("%d/%d: skipping - cannot write track with mixed size sectors");
        return;
    }

    // build cylinder & head maps and mark if needed
    uint8_t sectorOrder[256];
    uint8_t cylinderMap[256];
    uint8_t headMap[256];
    uint8_t imdHead = head;
    uint8_t nUsable = pTrack->nUsable;
    for (int slot = 0; slot < nUsable; slot++) {
        sectorOrder[slot] = pTrack->sectors[pTrack->sectorMap[slot]].sec;
        cylinderMap[slot] = pTrack->sectors[pTrack->sectorMap[slot]].cyl;
        headMap[slot]     = pTrack->sectors[pTrack->sectorMap[slot]].head;
        if (cylinderMap[slot] != cylinder)
            imdHead |= 0x80;
        if (headMap[slot] != head)
            imdHead |= 0x40;
    }

    uint8_t mode = ((pHead->density & 7) > 2 ? 0 : (pHead->density & 1) ? 1 : 2);
    if (pTrack->encoding == 0)
        mode += 3;

    putc(mode, fp);          // IMD mode
    putc(cylinder, fp);      // IMD cylinder
    putc(imdHead, fp);       // IMD head
    putc(nUsable, fp);       // IMD number of sectors
    putc(pTrack->sSize, fp); // IMD Sector Size
    if (fwrite(sectorOrder, 1, nUsable, fp) != nUsable)
        warn("Write error: sector map");

    if ((imdHead & 0x80) && fwrite(cylinderMap, 1, nUsable, fp) != nUsable)
        warn("Write error: cylinder map");
    if ((imdHead & 0x40) && fwrite(headMap, 1, nUsable, fp) != nUsable)
        warn("Write error: head map");

    for (int slot = 0; slot < pTrack->nUsable; slot++)
        saveIMDSector(fp, &pTrack->sectors[pTrack->sectorMap[slot]], options);
}

/// <summary>
/// write the sector image. Creating dummy sectors if needed
/// </summary>
/// <param name="fp">file stream to write to</param>
/// <param name="cylinder">The cylinder to use</param>
/// <param name="head">The head to use</param>
/// <param name="slot">The index into the td0 sectors to use or a dummy sector Id</param>
/// <param name="options">The user options</param>
void saveIMGSector(FILE *fp, uint8_t cylinder, uint8_t head, uint16_t slot, uint16_t options) {
    track_t *pTrack = &disk[cylinder][head];
    uint16_t sSize  = 128 << pTrack->sSize;
    if (slot >= UNALLOCATED) { // we were passed a dummy sector id
        if (pTrack->sSize == MIXEDSIZES) {
            warn("%d/%d: omitting sector %d", cylinder, head, slot & 0xff);
        } else {
            warn("%d/%d: filling missing sector %d", cylinder, head, slot & 0xff);
            writeBadSector(fp, "** missing **", sSize);
        }
    } else { // real sector but may require dummy data to be used dependent on options
        sector_t *pSector = &pTrack->sectors[slot];
        uint8_t sectorId  = pSector->sec;
        uint8_t sFlags    = pSector->flags & ~SEC_DUP;
        char *fill        = NULL;
        if (sFlags) {
            if (sFlags & SEC_CRC) {
                warn("%d/%d: sector %d CRC error", cylinder, head, sectorId);
                if (!(options & K_CRC))
                    fill = "** bad crc **";
            } else if (sFlags & SEC_DOS) {
                warn("%d/%d: sector %d unused", cylinder, head, sectorId);
                fill = "** unused **";
            } else if (sFlags & SEC_DAM) {
                warn("%d/%d: sector %d deleted data", cylinder, head, sectorId);
                if (!(options & K_DAM))
                    fill = "** deleted **";
            } else if (sFlags & SEC_NODAT) {
                warn("%d/%d: sector %d data missing", cylinder, head, sectorId);
                fill = "** missing **";
            }
        }
        if (fill)
            writeBadSector(fp, fill, sSize); // write a dummy sector
        else if (fwrite(pSector->data, 1, sSize, fp) != sSize)
            warn("Write error"); //  write the sector data
    }
}

/// <summary>
/// Saves the binary image of a track.
/// </summary>
/// <param name="fp">The fp.</param>
/// <param name="cylinder">The cylinder.</param>
/// <param name="head">The head.</param>
/// <param name="options">The options.</param>
void saveIMGTrack(FILE *fp, uint8_t cylinder, uint8_t head, uint16_t options) {
    uint16_t sectorMap[256];

    track_t *pTrack = &disk[cylinder][head];

    if (pTrack->nUsable) {
        // initialise sector map, assuming all missing (lower byte is sector number)
        for (int slot = 0; slot < 256; slot++)
            sectorMap[slot] = UNALLOCATED + slot;

        // populate the known entries
        for (int mapIdx = 0; mapIdx < pTrack->nUsable; mapIdx++) {
            uint8_t sectorIdx                         = pTrack->sectorMap[mapIdx];
            sectorMap[pTrack->sectors[sectorIdx].sec] = sectorIdx;
        }

        // create the final sectorMap
        uint8_t nSec = 0;
        if (options & K_GAP)
            pTrack->tFlags |= TRK_NONSTD;
        for (int inSlot = pTrack->first; inSlot <= pTrack->last; inSlot += pTrack->spacing)
            if (sectorMap[inSlot] < UNALLOCATED || !(pTrack->tFlags & TRK_NONSTD))
                sectorMap[nSec++] = sectorMap[inSlot];

        for (int slot = 0; slot < nSec; slot++)
            saveIMGSector(fp, cylinder, head, sectorMap[slot], options);
    }

    else
        warn("%d/%d - track missing - use IMD format", cylinder, head);
}



/// <summary>
/// writes an IMD or binary image based on file name extension
/// for binary image, the track sort options are applied
/// </summary>
/// <param name="outFile">The output file</param>
/// <param name="pHead">pointer the td0 header, IMD requires the density info</param>
/// <param name="options">The user options</param>
void saveImage(char const *outFile, td0Header_t *pHead, uint16_t options) {
    FILE *fpout;
    if (!(fpout = fopen(outFile, "wb")))
        fatal("Cannot create output file %s", outFile);

    char *s       = strrchr(basename((char *)outFile), '.');

    int firstHead = 0;
    if (options & NOHEAD0) {    // pseudo option used when no data for side 1 (head 0)
        warn("All of side 1 (head 0) is unusable");
        firstHead = 1;
    }
    if (s && strcasecmp(s, ".imd") == 0) {
        saveIMDHdr(fpout, pHead);
        for (int cylinder = 0; cylinder < nCylinder; cylinder++)
            for (int head = firstHead; head < nHead; head++)
                saveIMDTrack(fpout, pHead, cylinder, head, options);

    } else {
        if (!(options & (S_OO | S_OB)) || nHead == 1)
            for (int cylinder = 0; cylinder < nCylinder; cylinder++)
                for (int head = firstHead; head < nHead; head++)
                    saveIMGTrack(fpout, cylinder, head, options);
        else if (options & S_OO)
            for (int head = firstHead; head < nHead; head++)
                for (int cylinder = 0; cylinder < nCylinder; cylinder++)
                    saveIMGTrack(fpout, cylinder, head, options);
        else {
            if (firstHead == 0) {
                for (int cylinder = 0; cylinder < nCylinder; cylinder++)
                    saveIMGTrack(fpout, cylinder, 0, options);
            }
            for (int cylinder = nCylinder - 1; cylinder >= 0; cylinder--)
                saveIMGTrack(fpout, cylinder, 1, options);
        }
    }
    fclose(fpout);
}