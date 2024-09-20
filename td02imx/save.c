#include "td02imx.h"
#include <time.h>

static int16_t imdSectorMap[256];  // order indexed into sectors array, > 0xff then missing IDAM
static int8_t imdCylinderMap[256]; // IMD cylinder Map
static int8_t imdHeadMap[256];     // IMD head map
static int8_t imdHead;             // head with bits set if cylinder or head map required


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

void saveIMDHdr(FILE *fp) {
    fprintf(fp, "IMD 1.20: ");
    if (fHead.stepping & HAS_COMMENT) // had a comment so use time from original td0 file
        fprintf(fp, "%02d/%02d/%04d %02d:%02d:%02d\r\n", cHead.day, cHead.mon + 1, cHead.yr + 1900,
                cHead.hr, cHead.min, cHead.sec);
    else {
        time_t nowTime;
        struct tm *now;
        time(&nowTime);
        now = localtime(&nowTime);

        fprintf(fp, "%02d/%02d/%04d %02d:%02d:%02d\r\n", now->tm_mday,
                                    now->tm_mon + 1, now->tm_year + 1900, now->tm_hour, now->tm_min, now->tm_sec);
    }

    fprintf(fp, "Converted from %s\r\n", td0File);

    for (char *s = cHead.comment; *s; s++) {
        if (*s == '\n')
            putc('\r', fp);
        putc(*s, fp);
    }
    putc(0x1a, fp);
}

bool sameValues(uint8_t *buf, uint16_t len) {
    for (int i = 1; i < len; i++)
        if (buf[i] != buf[0])
            return false;
    return true;
}

// pSector == NULL for SEC_NOID
void saveIMDSector(FILE *fp, uint8_t cylinder, uint8_t head, uint16_t slot, uint8_t keep) {
    track_t *pTrack   = &tracks[cylinder][head];
    uint16_t sSize    = 128 << pTrack->sSize;
    sector_t *pSector = slot < UNALLOCATED ? &pTrack->sectors[imdSectorMap[slot]] : NULL;

    if (!pSector || (pSector->flags & SEC_NODAT))
        putc(0, fp);
    else if (pSector->flags & SEC_DOS) {
        putc(1, fp);
        putc(0xe5, fp);
    } else {
        uint8_t type   = pSector->flags & SEC_DAM ? 3 : 1;
        uint16_t sSize = 128 << pSector->hdr.sSize;
        if (pSector->flags & SEC_CRC)
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





void saveIMDTrack(FILE *fp, uint8_t cylinder, uint8_t head, uint8_t keep) {
    track_t *pTrack = &tracks[cylinder][head];
    if (!pTrack->sectors) {
        warn("%d/%d: track missing", cylinder, head);
        return;
    }
    if (pTrack->sSize == MIXEDSIZES) {
        warn("%d/%d: skipping - cannot write track with mixed size sectors");
        return;
    }

    // check if track has expected geometry
    sizing_t *si = &sizing[pTrack->encoding - 1][pTrack->sSize][head]; // expected layout

    if (si->nSec != pTrack->mapLen || si->first != pTrack->first ||
        (si->first + si->nSec - 1) != pTrack->last)
        warn("%d/%d: Possible missing IDAMS, using sector range %d-%d", cylinder, head,
             pTrack->first, pTrack->last);

    // build cylinder & head maps and mark if needed
    uint8_t cylinderMap[256];
    uint8_t headMap[256];
    uint8_t imdHead = head;

    for (int slot = 0; slot < pTrack->mapLen; slot++) {
        cylinderMap[slot] = pTrack->sectors[pTrack->sectorMap[slot]].hdr.trk;
        headMap[slot]     = pTrack->sectors[pTrack->sectorMap[slot]].hdr.head;
        if (cylinderMap[slot] != cylinder)
            imdHead |= 0x80;
        if (headMap[slot] != head)
            imdHead |= 0x40;
    }

    uint8_t mode = ((fHead.dataRate & 7) > 2 ? 0 : (fHead.dataRate & 1) ? 1 : 2);
    if (pTrack->encoding == MFM)
        mode += 3;

    putc(mode, fp);           // IMD mode
    putc(cylinder, fp);       // IMD cylinder
    putc(imdHead, fp);        // IMD head
    putc(pTrack->mapLen, fp); // IMD number of sectors
    putc(pTrack->sSize, fp);  // IMD Sector Size
    for (int slot = 0; slot < pTrack->mapLen; slot++)
        putc(pTrack->sectors[pTrack->sectorMap[slot]].hdr.sec, fp);

    if ((imdHead & 0x80) && fwrite(imdCylinderMap, 1, pTrack->mapLen, fp) != pTrack->mapLen)
        warn("Write error: cylinder map");
    if ((imdHead & 0x40) && fwrite(imdHeadMap, 1, pTrack->mapLen, fp) != pTrack->mapLen)
        warn("Write error: head map");

    for (int slot = 0; slot < pTrack->mapLen; slot++)
        saveIMDSector(fp, cylinder, head, slot, keep);
}

void saveIMGSector(FILE *fp, uint8_t cylinder, uint8_t head, uint16_t slot, uint8_t keep) {
    track_t *pTrack = &tracks[cylinder][head];
    uint16_t sSize  = 128 << pTrack->sSize;
    if (slot >= UNALLOCATED) {
        if ((keep & SEC_NOID) || pTrack->sSize == MIXEDSIZES) {
            warn("%d/%d: omitting sector at slot %d", cylinder, head, slot & 0xff);
        } else {
            warn("%d/%d: filling missing sector at slot %d", cylinder, head, slot & 0xff);
            writeBadSector(fp, "** missing **", sSize);
        }
    } else {
        sector_t *pSector = &pTrack->sectors[imdSectorMap[slot]];
        uint8_t sectorId  = pSector->hdr.sec;
        uint8_t sFlags    = pSector->hdr.sFlags & ~SEC_DUP;
        char *fill        = NULL;
        if (sFlags & SEC_CRC) {
            warn("%d/%d: sector %d CRC error", cylinder, head, sectorId);
            if (!(keep & SEC_CRC))
                fill = "** bad crc **";
        } else if (sFlags & SEC_DOS) {
            warn("%d/%d: sector %d unused", cylinder, head, sectorId);
            fill = "** unused **";
        } else if (sFlags & SEC_DAM) {
            warn("%d/%d: sector %d deleted data", cylinder, head, sectorId);
            if (!(keep & SEC_DAM))
                fill = "** deleted **";
        } else if (sFlags) {
            warn("%d/%d: sector %d data missing", cylinder, head, sectorId);
            fill = "** missing **";
        }
        if (fill)
            writeBadSector(fp, fill, sSize);
        else if (fwrite(pSector->data, 1, sSize, fp) != sSize)
            warn("Write error");
    }
}



void saveIMGTrack(FILE *fp, uint8_t cylinder, uint8_t head, uint8_t keep) {
    uint16_t sectorMap[256];
    track_t *pTrack = &tracks[cylinder][head];
    sizing_t *si    = &sizing[pTrack->encoding - 1][pTrack->sSize][head]; // expected layout
    uint8_t first   = si->first;
    uint8_t last    = first + si->nSec - 1;

    if (pTrack->sectors) {
        // check if missing IDAMs need to be flagged

        if ((keep && SEC_NOID) && (si->nSec != pTrack->mapLen || first != pTrack->first || last != pTrack->last)) {
            warn("%d/%d: Possible missing IDAMS, using sector range %d-%d", cylinder, head,
                 pTrack->first, pTrack->last);
            first = pTrack->first;
            last  = pTrack->last;
        }

        // initialise sector map, assuming all missing (lower byte is sector number)
        for (int slot = 0; slot < 256; slot++)
            sectorMap[slot] = UNALLOCATED + slot;

        // populate the known entries
        for (int mapIdx = 0; mapIdx < pTrack->mapLen; mapIdx++) {
            uint8_t sectorIdx                                = pTrack->sectorMap[mapIdx];
            sectorMap[pTrack->sectors[sectorIdx].hdr.sec] = sectorIdx;
        }

        // create the final sectorMap
        uint8_t nSec = 0;
        for (int inSlot = first; inSlot <= last; inSlot++)
            if (!(keep & SEC_NOID) || sectorMap[inSlot] < UNALLOCATED)
                sectorMap[nSec++] = sectorMap[inSlot];

        for (int slot = 0; slot < nSec; slot++)
            saveIMGSector(fp, cylinder, head, slot, keep);
    }

    else
        warn("%d/%d - track missing - use IMD format", cylinder, head);
}

void saveImage(char const *outFile, uint8_t keep, uint8_t layout) {
    FILE *fpout;
    if (!(fpout = fopen(outFile, "wb")))
        fatal("Cannot create output file %s", outFile);

    char *s = strrchr(basename((char *)outFile), '.');

    if (s && stricmp(s, ".imd") == 0) {
        saveIMDHdr(fpout);
        for (int cylinder = 0; cylinder < nCylinder; cylinder++)
            for (int head = 0; head < nHead; head++)
                saveIMDTrack(fpout, cylinder, head, keep);

    } else if (layout == ALT || nHead == 1)
        for (int cylinder = 0; cylinder < nCylinder; cylinder++)
            for (int head = 0; head < nHead; head++)
                saveIMGTrack(fpout, cylinder, head, keep);
    else if (layout == OUTOUT)
        for (int head = 0; head < nHead; head++)
            for (int cylinder = 0; cylinder < nCylinder; cylinder++)
                saveIMGTrack(fpout, cylinder, head, keep);
    else {
        for (int cylinder = 0; cylinder < nCylinder; cylinder++)
            saveIMGTrack(fpout, cylinder, 0, keep);
        for (int cylinder = nCylinder - 1; cylinder >= 0; cylinder--)
            saveIMGTrack(fpout, cylinder, 1, keep);
    }

    fclose(fpout);
}