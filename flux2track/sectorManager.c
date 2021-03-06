#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <assert.h>
#include "dpll.h"
#include "flux2track.h"
#include "formats.h"
#include "trackManager.h"
#include "flux.h"
#include "util.h"

static int prevSlot = -1;
static unsigned curSpacing;
static unsigned minSpacing;
static unsigned maxSpacing;

static void chkSptChange() {
    for (formatInfo_t* p = curFormat + 1; p->encoding == curFormat->encoding && p->sSize == curFormat->sSize; p++) {
        if (abs(p->spacing - curSpacing) < 3) {
            DBGLOG(D_DECODER, "Updated format to %s\n", p->name);
            curFormat = p;
            minSpacing = (uint16_t)(curFormat->spacing * 0.97);
            maxSpacing = (uint16_t)(curFormat->spacing * 1.03);
            updateTrackFmt();
            return;
        }
    }
    DBGLOG(D_DECODER, "WARNING could not determine SPT\n");
}

static unsigned slotAt(unsigned pos, bool isIdam) {
    static unsigned prevIdamPos;
    static unsigned prevDataPos;

    if (prevSlot < 0 || pos < prevIdamPos) {       // initialise either explicit or due to seek from start
        unsigned adjust;
        curSpacing = curFormat->spacing;
        if (cntHardSectors())
            minSpacing = maxSpacing = curSpacing;
        else {
            minSpacing = curFormat->spacing * 97 / 100;     // +/- 3%
            maxSpacing = curFormat->spacing * 103 / 100;
        }

        prevSlot = pos / curSpacing;

        if (isIdam) {
            prevIdamPos = pos;
            prevDataPos = prevIdamPos + curFormat->firstDATA - curFormat->firstIDAM;
            adjust = curFormat->firstIDAM * 3 / 4;      // allow 75% of first IDAM as offset
        } else {
            prevDataPos = pos;
            prevIdamPos = prevDataPos + curFormat->firstIDAM - curFormat->firstDATA;
            adjust = curFormat->firstDATA * 3 / 4;      // allow 75% of first DATA as offset
        }
        // see if we get different sectors numbers depending on min/max spacing
        if ((pos - adjust) / minSpacing != (pos - adjust) / maxSpacing)
            logFull(D_WARNING, "Too many missing sectors, slot calculation may be wrong\n");
    }
    // calculate change in slot, allow for some JITTER
    unsigned slotDelta = (pos - (isIdam ? prevIdamPos : prevDataPos) + POSJITTER) / curSpacing;
    if (slotDelta == 0) {
        if (!isIdam)                    // already  have IDAM so put in real data pos
            prevDataPos = pos;
    } else {
        prevSlot += slotDelta;
        // now work out effective spacing
        unsigned newSpacing = (pos - (isIdam ? prevIdamPos : prevDataPos)) / slotDelta;
        // keep in tolerance
        newSpacing = newSpacing > maxSpacing ? maxSpacing : newSpacing < minSpacing ? minSpacing : newSpacing;

        if (newSpacing != curSpacing)
            DBGLOG(D_TRACKER, "Spacing changed from %d to %d\n", curSpacing, newSpacing);

        curSpacing = newSpacing;


        if (curFormat->options & O_SPC)         // update format if dependent on spacing
            chkSptChange();
        if (isIdam) {                           // provide new estimate for other pos info
            prevDataPos += pos - prevIdamPos;
            prevIdamPos = pos;
        } else {
            prevIdamPos += pos - prevDataPos;
            prevDataPos = pos;
        }
    }

    if (prevSlot >= curFormat->spt) {
        logFull(D_ERROR, "Sector slot calculated as (%d) >= spt (%d) - setting to max\n", prevSlot, curFormat->spt);
        prevSlot = curFormat->spt - 1;
    }
    return prevSlot;
}


void addIdam(unsigned pos, idam_t* idam) {
    unsigned  slot = slotAt(pos, true);

    sector_t* p = &trackPtr->sectors[slot];

    if (idam->cylinder != trackPtr->cylinder || idam->side != trackPtr->side) {
        logFull(D_WARNING, "Unexpected cylinder/side %d/%d - expected %d/%d @ slot %d\n",
                idam->cylinder, idam->side, trackPtr->cylinder, trackPtr->side, slot);
        trackPtr->cylinder = idam->cylinder;
        trackPtr->side = idam->side;
    }

    if (idam->sectorId < curFormat->firstSectorId ||  idam->sectorId - curFormat->firstSectorId >= curFormat->spt)
        logFull(D_FATAL, "@slot %d sector Id %d out of range\n", slot, idam->sectorId);

    if (p->status & SS_IDAMGOOD) {
        if (idam->sectorId != p->sectorId)
            logFull(ALWAYS, "@slot %d sector Id conflict new %d previous %d\n", slot, idam->sectorId, p->sectorId);
    } else {
        trackPtr->cntGoodIdam++;
        p->status |= SS_IDAMGOOD;
        p->sectorId = idam->sectorId;
        trackPtr->slotToSector[slot] = idam->sectorId;
        trackPtr->sectorToSlot[idam->sectorId - trackPtr->fmt->firstSectorId] = slot;
    }
}


void addSectorData(unsigned pos, bool isGood, unsigned len, uint16_t rawData[]) {
    unsigned slot = slotAt(pos, false);
    sector_t* p = &trackPtr->sectors[slot];
    sectorDataList_t* q;

    if (p->status & SS_DATAGOOD) {     // already have good data
        if (isGood && memcmp(p->sectorDataList->sectorData.rawData, rawData, len * sizeof(rawData[0])) != 0)
            logFull(ALWAYS, "@slot %d valid sectors with different data\n", slot);
        return;
    }

    if (isGood) {                    
        p->status |= SS_DATAGOOD;
        removeSectorData(p->sectorDataList);        // remove any previous bad sector data
        p->sectorDataList = NULL;
        trackPtr->cntGoodData++;
        trackPtr->cntAnyData++;
    } else if (!p->sectorDataList)
        trackPtr->cntAnyData++;

    // put data at head of list
    q = (sectorDataList_t*)xmalloc(sizeof(sectorDataList_t) + len * sizeof(q->sectorData.rawData[0]));
    q->sectorData.len = len;
    memcpy(q->sectorData.rawData, rawData, len * sizeof(q->sectorData.rawData[0]));
    q->next = p->sectorDataList;
    p->sectorDataList = q;
}


void removeSectorData(sectorDataList_t *p)
{
    sectorDataList_t *q;
    for (; p; p = q) {
        q = p->next;
        free(p);
    }
}


void resetTracker() {
    prevSlot = -1;
}
