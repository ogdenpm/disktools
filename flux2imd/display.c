#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <memory.h>
#include <string.h>
#include "display.h"
#include "sectorManager.h"
#include "util.h"

#define pOpt    1
#define aOpt    2
static int charMask = 0xff;
static bool all = false;

extern char curFile[];

int rowSuspectCnt(uint16_t* s, int len) {
    int cnt = 0;
    for (int i = 0; i < len; i++, s++)
        cnt += *s >> 8;
    return cnt;
}

// only called when there is data for the sector
// cleans up suspect flag if same byte without suspect has been seen
// in one of the copies

static void cleanUpSuspect(sectorDataList_t* pList) {
    sectorDataList_t* qList;
    uint16_t* p, * q;
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

void displayDataLine(uint16_t* p) {
    for (int j = 0; j < 16; j++)
        logBasic("%02X%c ", p[j] & 0xff, (p[j] & SUSPECT) ? '*' : ' ');

    for (int j = 0; j < 16; j++) {
        int c = p[j] & charMask;
        logBasic("%c", (' ' <= c && c <= '~') ? c : '.');
    }
    logBasic("\n");
}

void displayExtraLine(uint16_t* p, int len) {       // len: 2 just CRC, 4 just fwd/bwd len, 8 fwd/bwd/crc & postamble
    switch (len) {
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
void displayExtra(track_t* pTrack, uint8_t slot) {
    sector_t* pSector = &pTrack->sectors[slot];
    int cntExtra = pTrack->fmt->options != O_ZDS ? 2 : (pSector->status & SS_DATAGOOD) ? 4 : 8;
    int offset = 128 << pTrack->fmt->sSize;

    sectorDataList_t* p;

    char *marker = (pSector->status & SS_DATAGOOD) ? NULL : " ";
    bool cleanOnly = false;
    for (p = pSector->sectorDataList; p && rowSuspectCnt(&p->sectorData.rawData[offset], cntExtra); p = p->next)    // find row with no tags
        ;
    if (p)                  // got one
        cleanOnly = true;
    else                    // none exists so full dump
        p = pSector->sectorDataList;

    for (; p; p = p->next) {        // go through each of the sectors
        if (cleanOnly && rowSuspectCnt(&p->sectorData.rawData[offset], cntExtra))     // if clean only skip bad rows
            continue;
        bool duplicate = false;                                 // check if a duplicate
        for (sectorDataList_t* q = pSector->sectorDataList; q != p && !duplicate; q = q->next)
            if (memcmp(&p->sectorData.rawData[offset], &q->sectorData.rawData[offset], cntExtra * sizeof(uint16_t)) == 0)
                duplicate = true;
        if (!duplicate) {                                       // no its new
            if (marker)
                logBasic(marker);
            displayExtraLine(&p->sectorData.rawData[offset], cntExtra);
            marker = "+";                                       // make sure any more have + marker
        }
    }
}

static char* sectorToString(track_t* pTrack, uint8_t slot) {
    static char s[9];
    if (pTrack->sectors[slot].status & SS_IDAMGOOD)
        sprintf(s, "%02d/%d/%02d", pTrack->track, pTrack->side, pTrack->sectors[slot].sectorId);
    else if (pTrack->sectors[slot].status & SS_FIXED)
        sprintf(s, "%02d/%d/%02d*", pTrack->track, pTrack->side, pTrack->sectors[slot].sectorId);
    else
        sprintf(s, "%02d/%d/??", pTrack->track, pTrack->side);
    return s;
}

// to minimse the noise in the dump. If there is a row copy without suspect tags
// choose to display only it any any other non duplicate copys of the row that also have no suspect tags
void displayData(track_t* pTrack, uint8_t slot) {
    sectorDataList_t* p;
    sector_t* pSector = &pTrack->sectors[slot];

    int len = 128 << pTrack->fmt->sSize;

    for (int i = 0; i < len; i += 16) {
        char *marker = (pSector->status & SS_DATAGOOD) ? NULL : " ";
        bool cleanOnly = false;

        for (p = pSector->sectorDataList; p && rowSuspectCnt(&p->sectorData.rawData[i], 16); p = p->next)    // find row with no tags
            ;
        if (p)                  // got one
            cleanOnly = true;
        else                    // none exists so full dump
            p = pSector->sectorDataList;

        for (; p; p = p->next) {        // go through each of the sectors
            if (cleanOnly && rowSuspectCnt(&p->sectorData.rawData[i], 16))     // if clean only skip bad rows
                continue;
            bool duplicate = false;                                 // check if a duplicate
            for (sectorDataList_t* q = pSector->sectorDataList; q != p && !duplicate; q = q->next) {
                if (memcmp(&p->sectorData.rawData[i], &q->sectorData.rawData[i], 16 * sizeof(uint16_t)) == 0)
                    duplicate = true;
            }
            if (!duplicate) {                                       // no its new
                if (marker)
                    logBasic(marker);
                displayDataLine(&p->sectorData.rawData[i]);
                marker = "+";                                       // make sure any more have + marker
            }
        }
    }
}

void displaySector(track_t* pTrack, uint8_t slot, bool all) {
    sector_t* pSector = &pTrack->sectors[slot];
    int size = 128 << pTrack->fmt->sSize;

    if (!all && (pSector->status & SS_GOOD) == SS_GOOD)
        return;

    if (!pSector->sectorDataList) {
        logBasic("%s: No data\n", sectorToString(pTrack, slot));
        return;
    }
    bool isGood = (pSector->status & SS_DATAGOOD);
    if (!isGood)
        cleanUpSuspect(pSector->sectorDataList);
        logBasic("\n%s:%s\n", sectorToString(pTrack, slot), isGood ? "" : " ---- Corrupt Sector ----");

    displayData(pTrack, slot);
    if (!isGood || pTrack->fmt->options == O_ZDS)
        displayExtra(pTrack, slot);

    if (!isGood)
        logBasic("       ---- End Corrupt Sector ----\n");
}

void displayTrack(int track, int side, int options) {
    track_t* pTrack = getTrack(track, side);


    charMask = options & pOpt ? 0x7f : 0xff;
    all = (options & aOpt) || curFormat->options == O_ZDS;
    
    if (pTrack == NULL || pTrack->cntAnyData == 0) {
        logFull(ALWAYS, "No data\n");
        return;
    }
    // if all data is good and only reporting errors then all done
    if (!all && trackPtr->cntGoodIdam == trackPtr->fmt->spt && trackPtr->cntGoodData == trackPtr->fmt->spt)
        return;

    logFull(ALWAYS, "Encoding %s\n", pTrack->fmt->shortName);

    if (!(pTrack->status & TS_BADID)) {
        if (pTrack->sectorToSlot[0] != pTrack->fmt->firstSectorId || pTrack->sectorToSlot[1] != pTrack->fmt->firstSectorId + 1) {
            logBasic("%02d/%d: *** Physical sector order on track ***\n", track, side);
            for (int i = 0; i < pTrack->fmt->spt; i++) {
                logBasic(" %2d%c", pTrack->sectors[i].sectorId, pTrack->sectors[i].status & SS_IDAMGOOD ? ' ' : '*');
                if (i % 16 == 15)
                    logBasic("\n");
            }
            if (pTrack->fmt->spt % 16)
                logBasic("\n");
        }
        for (int i = 0; i < pTrack->fmt->spt; i++)
            displaySector(pTrack, pTrack->sectorToSlot[i], all);
    } else {
        logFull(WARNING, "Interleave fixup failed, using physical order\n");
        for (int i = 0; i < pTrack->fmt->spt; i++)
            displaySector(pTrack, i, all);
    }
}