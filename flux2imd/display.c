// This is an open source non-commercial project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <memory.h>
#include <string.h>
#include "flux2Imd.h"
#include "trackManager.h"
#include "util.h"

#define pOpt    1
#define gOpt    2
#define bOpt    4

static int charMask = 0xff;

// forward references
static int rowSuspectCnt(uint16_t *s, int len);
static char *sectorToString(track_t *pTrack, uint8_t slot);

// only called when there is data for the sector
// cleans up suspect flag if same byte without suspect has been seen
// in one of the copies

static void cleanUpSuspect(sectorDataList_t *pList) {
    sectorDataList_t *qList;
    uint16_t *p, *q;
    if (!pList)
        return;
    for (; pList->next; pList = pList->next)
        for (qList = pList->next; qList; qList = qList->next) {
            int len = pList->sectorData.len <= qList->sectorData.len
                ? pList->sectorData.len : qList->sectorData.len;
            p = pList->sectorData.rawData;
            q = qList->sectorData.rawData;
            for (int i = 0; i < len; i++)
                if (((p[i] ^ q[i]) & 0xff) == 0)        // data bytes same
                    p[i] = q[i] = p[i] & q[i];          // makes sure suspect is cleared if one is not set
        }
}

static void displayDataLine(uint16_t *p, int len) {
    for (int j = 0; j < len; j++)
        logBasic("%02X%c ", p[j] & 0xff, (p[j] & SUSPECT) ? '*' : ' ');

    for (int j = 0; j < 16; j++) {
        int c = p[j] & charMask;
        logBasic("%c", (' ' <= c && c <= '~') ? c : '.');
    }
    logBasic("\n");
}

static void displayExtraLine(uint16_t *p, int len) {       // len: 1 or 2 just CRC, 4 just fwd/bwd len, 8 fwd/bwd/crc & postamble
    switch (len) {
    case 1:
        logBasic("crc %02X%s\n", p[0] & 0xff, (p[0] & SUSPECT) ? "*" : "");
        break;
    case 2: // simple CRC for bad sector
        logBasic("crc %02X%s %02X%s\n", p[0] & 0xff, (p[0] & SUSPECT) ? "*" : "", p[1] & 0xff, (p[1] & SUSPECT) ? "*" : "");
        break;
    case 4:  // simple fwd / back for good ZDS sector
        logBasic("forward %d%s/%d%s ", p[3] & 0xff, (p[3] & SUSPECT) ? "*" : "", p[2] & 0xff, (p[2] & SUSPECT) ? "*" : "");
        logBasic("backward %d%s/%d%s\n", p[1] & 0xff, (p[1] & SUSPECT) ? "*" : "", p[0] & 0xff, (p[0] & SUSPECT) ? "*" : "");
        break;
    case 8:
        logBasic("forward %d%s/%d%s ", p[3] & 0xff, (p[3] & SUSPECT) ? "*" : "", p[2] & 0xff, (p[2] & SUSPECT) ? "*" : "");
        logBasic("backward %d%s/%d%s ", p[1] & 0xff, (p[1] & SUSPECT) ? "*" : "", p[0] & 0xff, (p[0] & SUSPECT) ? "*" : "");
        logBasic("crc %02X%s %02X%s ", p[4] & 0xff, (p[4] & SUSPECT) ? "*" : "", p[5] & 0xff, (p[5] & SUSPECT) ? "*" : "");
        logBasic("postamble %02X%s %02X%s\n", p[6] & 0xff, (p[6] & SUSPECT) ? "*" : "", p[7] & 0xff, (p[7] & SUSPECT) ? "*" : "");
        break;
    }
}


// to minimse the noise in the dump. If there is a row copy without suspect tags
// choose to display only it any any other non duplicate copys of the row that also have no suspect tags

static void displayLine(sector_t *pSector, int offset, int len, void (*displayFunc)(uint16_t *, int)) {
    char *marker = (pSector->status & SS_DATAGOOD) ? NULL : " ";
    bool cleanOnly = false;
    sectorDataList_t *p;
    // see if we have a line with no suspect bytes
    for (p = pSector->sectorDataList; p && rowSuspectCnt(&p->sectorData.rawData[offset], len); p = p->next)    // find row with no tags
        ;
    if (p)
        cleanOnly = true;       // got one so only clean bytes for this line
    else
        p = pSector->sectorDataList;

    for (; p; p = p->next) {        // go through each of the sectors
        if (cleanOnly && rowSuspectCnt(&p->sectorData.rawData[offset], len))     // if clean only skip bad rows
            continue;
        bool duplicate = false;                                 // check if a duplicate
        for (sectorDataList_t *q = pSector->sectorDataList; q != p && !duplicate; q = q->next)
            if (memcmp(&p->sectorData.rawData[offset], &q->sectorData.rawData[offset], len * sizeof(uint16_t)) == 0)
                duplicate = true;
        if (!duplicate) {                                       // no its new
            if (marker)
                logBasic(marker);
            displayFunc(&p->sectorData.rawData[offset], len);
            marker = "+";                                       // make sure any more have + marker
        }
    }
}


static void displaySector(track_t *pTrack, uint8_t slot, unsigned options) {
    sector_t *pSector = &pTrack->sectors[slot];

    int size = 128 << pTrack->fmt->sSize;

    if (!pSector->sectorDataList) {
        if (options & bOpt)
            logBasic("%s: No data\n", sectorToString(pTrack, slot));
        return;
    }
    if (pSector->status & SS_DATAGOOD) {
        if (!(options & gOpt))
            return;
    } else if (!(options & bOpt))
        return;


    bool isGood = (pSector->status & SS_DATAGOOD);
    if (!isGood)
        cleanUpSuspect(pSector->sectorDataList);

    logBasic("\n%s:%s\n", sectorToString(pTrack, slot), isGood ? "" : " ---- Corrupt Sector ----");

    for (int i = 0; i < size; i += 16)
        displayLine(pSector, i, size - i >= 16 ? 16 : size - i, displayDataLine);

    if (!isGood || pTrack->fmt->options == O_ZDS) {
        int cntExtra;
        switch (pTrack->fmt->options) {
        case O_NSI:
        case O_MTECH: cntExtra = 1; break;
        case O_ZDS: cntExtra = (pSector->status & SS_DATAGOOD) ? 4 : 8; break;
        default: cntExtra = 2;
        }

        displayLine(pSector, size, cntExtra, displayExtraLine);
    }
    if (!isGood)
        logBasic("       ---- End Corrupt Sector ----\n");
}

static int rowSuspectCnt(uint16_t *s, int len) {
    int cnt = 0;
    for (int i = 0; i < len; i++, s++)
        cnt += *s >> 8;
    return cnt;
}

static char *sectorToString(track_t *pTrack, uint8_t slot) {
    static char s[9];
    sector_t *p = &pTrack->sectors[slot];

    if (p->status & SS_IDAMGOOD)
        sprintf(s, "%02d/%d/%02d", p->idam.cylinder, p->idam.side, p->idam.sectorId);
    else if (p->status & SS_FIXED)
        sprintf(s, "%02d/%d/%02d*", p->idam.cylinder, p->idam.side, p->idam.sectorId);
    else
        sprintf(s, "%02d/%d/??", p->idam.cylinder, p->idam.side);
    return s;
}

void displayDefectMap() {
    bool badTrack[2] = { false };
    bool hasSomeSectors[2] = { false };
    int badSector = 0;
    int badIdam = 0;

    track_t *pTrack;
    // check whether we have any track on a side and whether there are any bad tracks on each side
    for (int head = 0; head <= maxHead; head++)
        for (int cyl = 0; cyl <= maxCylinder; cyl++)
            if (hasTrack(cyl, head)) {
                hasSomeSectors[head] = true;
                if (!(pTrack = getTrack(cyl, head)) || pTrack->cntGoodIdam != pTrack->fmt->spt || pTrack->cntGoodData != pTrack->fmt->spt) {
                    badTrack[head] = true;
                    break;
                }
            }

    for (int head = 0; head <= maxHead; head++) {
        if (!hasSomeSectors[head])
            continue;
        if (!badTrack[head])
            logFull(ALWAYS, "Side %d - all data processed successfully\n", head);
        else {
            logBasic("\n");
            logFull(ALWAYS, "Side %d - defect map - (x) bad sector, (.) bad idam only\n", head);
            int spt = 0;
            for (int cyl = 0; cyl <= maxCylinder; cyl++) {
                if (hasTrack(cyl, head)) {
                    if (!(pTrack = getTrack(cyl, head)))
                        logBasic("%02d     data unusable\n", cyl, head);
                    else if (pTrack->cntGoodData != pTrack->fmt->spt || pTrack->cntGoodIdam != pTrack->fmt->spt) {
                        if (spt != pTrack->fmt->spt) {
                            spt = pTrack->fmt->spt;
                            logBasic("   %.*s\n", spt, "0123456789 123456789 123456789 123456789 123456789 1");
                        }
                        char defects[MAXSECTOR + 1];
                        int fillCh = 0;
                        defects[spt] = 0;
                        for (int i = spt - 1; i >= 0; i--) {
                            if ((pTrack->sectors[i].status & SS_GOOD) != SS_GOOD) {
                                if (!(pTrack->sectors[i].status & SS_DATAGOOD)) {
                                    badSector++;
                                    defects[i] = 'x';
                                } else {
                                    badIdam++;
                                    defects[i] = '.';
                                }
                                fillCh = ' ';
                            } else
                                defects[i] = fillCh;
                        }
                        logBasic("%02d %s\n", cyl, defects);
                    }

                }
            }
            if (badSector || badIdam)
                logBasic("\n%d bad sectors and %d bad idam\n", badSector, badIdam);
        }
    }
}


void displayBadSlots(track_t *pTrack) {
    uint8_t const *slotToSector = pTrack->slotToSector;

    logBasic("  (#)Missing/(?)Corrupt Slots:");
    for (unsigned i = 0; i < pTrack->fmt->spt; i++)
        if (!(trackPtr->sectors[i].status & SS_DATAGOOD))
            logBasic("%3d%c", i, trackPtr->sectors[i].sectorDataList ? '?' : '#');
    logBasic("\n");
    logBasic("  Allocated SectorId (*)Fixed:");
    for (unsigned i = 0; i < pTrack->fmt->spt; i++) {
        if (!(trackPtr->sectors[i].status & SS_DATAGOOD))
            if (slotToSector[i] != 0xff)
                logBasic("%3d%c", slotToSector[i], pTrack->sectors[i].status & SS_IDAMGOOD ? ' ' : '*');
            else
                logBasic(" ?? ");
    }
    logBasic("\n");
}

void displayTrack(int cylinder, int side, unsigned options) {
    track_t *pTrack = getTrack(cylinder, side);

    if (pTrack == NULL || pTrack->cntAnyData == 0) {
        logFull(ALWAYS, "Track %02d/%d no data\n", cylinder, side);
        return;
    }
    cylinder = pTrack->cylinder;        // just in case they have been mapped
    side = pTrack->side;

    charMask = options & pOpt ? 0x7f : 0xff;
    int spt = pTrack->fmt->spt;

    if (!(options & gOpt) && pTrack->cntGoodData == spt && pTrack->cntGoodIdam == spt)
        return;

    if (pTrack->cntGoodIdam != spt || pTrack->cntGoodData != spt || (options & (bOpt | gOpt)))
        logFull(ALWAYS, "Track %02d/%d encoding %s\n", cylinder, side, pTrack->fmt->name);


    uint8_t sectorToSlot[MAXSECTOR];                    // used to support sorting by sector index is sectorId - firstSectorId
    uint8_t *slotToSector = pTrack->slotToSector;
    uint8_t firstSectorId = pTrack->fmt->firstSectorId;

    memset(sectorToSlot, 0xff, MAXSECTOR);  // mark all as not available
    for (int i = 0; i < spt; i++)
        if (slotToSector[i] != 0xff)
            sectorToSlot[slotToSector[i] - firstSectorId] = i;

    if (pTrack->status & TS_BADID) {
        logFull(D_WARNING, "Track %02d/%d unable to reconstruct sector order\n", cylinder, side);
        logBasic("  Missing sector Ids:");
        for (int i = 0; i < spt; i++)
            if (sectorToSlot[i] == 0xff)
                logBasic(" %02d", i + firstSectorId);
        logBasic("\n");
    } else if (pTrack->status & TS_FIXEDID) {
        logBasic("  Reconstructed sector order:");
        for (int i = 0; i < spt; i++) {
            if (spt == 52 && i % 26 == 0)
                logBasic("\n  ");
            logBasic(" %2d%c", sectorToSlot[i], pTrack->sectors[i].status & SS_IDAMGOOD ? ' ' : '*');
        }
        logBasic("\n");
    }

    if (pTrack->cntGoodData != spt)
        displayBadSlots(pTrack);

    if (pTrack->status & TS_BADID)      // without full sector map show in slot order
        for (int i = 0; i < spt; i++)
            displaySector(pTrack, i, options);
    else
        for (int i = 0; i < spt; i++)   // ok so display in sector order
            displaySector(pTrack, sectorToSlot[i], options);
    logBasic("\n");
}