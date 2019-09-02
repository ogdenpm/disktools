#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <memory.h>
#include <string.h>
#include "display.h"
#include "sectorManager.h"
#include "util.h"

#define pOpt    1
#define gOpt    2
#define bOpt    4

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

void displayDataLine(uint16_t* p, int len) {
    for (int j = 0; j < len; j++)
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


// to minimse the noise in the dump. If there is a row copy without suspect tags
// choose to display only it any any other non duplicate copys of the row that also have no suspect tags

void displayLine(sector_t *pSector, int offset, int len, void (*displayFunc)(uint16_t *, int)) {
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




static char* sectorToString(track_t* pTrack, uint8_t slot) {
    static char s[9];
    if (pTrack->sectors[slot].status & SS_IDAMGOOD)
        sprintf(s, "%02d/%d/%02d", pTrack->cylinder, pTrack->side, pTrack->sectors[slot].sectorId);
    else if (pTrack->sectors[slot].status & SS_FIXED)
        sprintf(s, "%02d/%d/%02d*", pTrack->cylinder, pTrack->side, pTrack->sectors[slot].sectorId);
    else
        sprintf(s, "%02d/%d/??", pTrack->cylinder, pTrack->side);
    return s;
}




void displaySector(track_t *pTrack, uint8_t slot, unsigned options) {
    sector_t *pSector = &pTrack->sectors[slot];

    int size = 128 << pTrack->fmt->sSize;

    if (!pSector->sectorDataList) {
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
        displayLine(pSector, i, 16, displayDataLine);

    if (!isGood || pTrack->fmt->options == O_ZDS) {
        int cntExtra = pTrack->fmt->options != O_ZDS ? 2 : (pSector->status & SS_DATAGOOD) ? 4 : 8;

        displayLine(pSector, size, cntExtra, displayExtraLine);
    }
    if (!isGood)
        logBasic("       ---- End Corrupt Sector ----\n");
}

void displayTrack(int cylinder, int side, unsigned options) {
    track_t* pTrack = getTrack(cylinder, side);
          
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

    if (pTrack->cntGoodIdam != spt || pTrack->cntGoodData != spt || (options & (bOpt|gOpt)))
        logFull(ALWAYS, "Track %02d/%d encoding %s\n", cylinder, side, pTrack->fmt->name);

    if (pTrack->status & TS_BADID)
        logFull(WARNING, "Track %02d/%d unable to reconstruct sector order\n", cylinder, side);
    else if (pTrack->status & TS_FIXEDID) {
        logBasic("  Reconstructed sector order:");
        for (int i = 0; i < spt; i++) {
                logBasic(" %2d%c", pTrack->sectors[i].sectorId, pTrack->sectors[i].status & SS_IDAMGOOD ? ' ' : '*');
                if (i == 20)
                    logBasic("\n");
            }
            logBasic("\n");
    }
    if (pTrack->cntGoodData != spt) {
        logBasic("  data missing (#) / corrupt (?) for sectors:");
        for (int i = 0; i < trackPtr->fmt->spt; i++)
            if (!(trackPtr->sectors[trackPtr->sectorToSlot[i]].status & SS_DATAGOOD))
                logBasic("%3d%c", i + trackPtr->fmt->firstSectorId, trackPtr->sectors[trackPtr->sectorToSlot[i]].sectorDataList ? '?' : '#');
        logBasic("\n");
    }

    if (!(pTrack->status & TS_BADID))
        for (int i = 0; i < spt; i++)
            displaySector(pTrack, pTrack->sectorToSlot[i], options);
    else
        for (int i = 0; i < spt; i++)
            displaySector(pTrack, i, options);
    logBasic("\n");
}