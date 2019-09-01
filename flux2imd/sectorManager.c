#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <assert.h>
#include "decoders.h"
#include "dpll.h"
#include "util.h"
#include "bits.h"
#include "sectorManager.h"
#include "flux.h"

void chkSptChange();


void removeSectorData(sectorDataList_t *p)
{
    sectorDataList_t *q;
    for (; p; p = q) {
        q = p->next;
        free(p);
    }
}


static uint8_t prevSlot = 0xff;
static uint16_t prevIdamPos = 0;
static uint16_t prevDataPos = 0;
static uint16_t curSpacing = 1;
static uint16_t minSpacing = 0;
static uint16_t maxSpacing = 0;

void resetTracker() {
    prevSlot = 0xff;
    curSpacing = curFormat->spacing;
    if (cntHardSectors() > 0)
        minSpacing = maxSpacing = curSpacing;      // synthetic hence will always be correct
    else {
        minSpacing = (uint16_t)(curFormat->spacing * 0.97);
        maxSpacing = (uint16_t)(curFormat->spacing * 1.03);
    }

}

uint8_t slotAt(uint16_t pos, bool isIdam) {
    if (prevSlot == 0xff || pos < prevIdamPos) {                         // initialise
        uint16_t adjust;
        prevSlot = pos / curSpacing;
        if (isIdam) {
            prevIdamPos = pos;
            prevDataPos = prevIdamPos + curFormat->firstDATA - curFormat->firstIDAM;
            adjust = (uint16_t)(curFormat->firstIDAM * 0.75);
        } else {
            prevDataPos = pos;
            prevIdamPos = prevDataPos + curFormat->firstIDAM - curFormat->firstDATA;
            adjust = (uint16_t)(curFormat->firstDATA * 0.75);
        }
        if ((pos - adjust) / minSpacing != (pos - adjust) / maxSpacing)
            logFull(ALWAYS, "WARNING: Too many missing sectors, slot calculation may be wrong\n");
    }
    uint16_t slotDelta = (pos - (isIdam ? prevIdamPos : prevDataPos) + 4 * POSJITTER) / curSpacing;
    if (slotDelta == 0) {
        if (!isIdam)
            prevDataPos = pos;
    } else {
        prevSlot += slotDelta;
        uint16_t newSpacing = (pos - (isIdam ? prevIdamPos : prevDataPos)) / slotDelta;
        if (newSpacing > maxSpacing)
            newSpacing = maxSpacing;
        else if (newSpacing < minSpacing)
            newSpacing = minSpacing;
#if _DEBUG
        if (newSpacing != curSpacing)
            logFull(TRACKER, "Spacing changed from %d to %d\n", curSpacing, newSpacing);
#endif
        curSpacing = newSpacing;
        if (curFormat->options & O_SPC)
            chkSptChange();
        if (isIdam) {
            prevDataPos += pos - prevIdamPos;
            prevIdamPos = pos;
        } else {
            prevIdamPos += pos - prevDataPos;
            prevDataPos = pos;
        }
    }

    if (prevSlot >= curFormat->spt) {
        logFull(ERROR, "Sector slot calculated as (%d) >= spt (%d) - setting to max\n", prevSlot, curFormat->spt);
        prevSlot = curFormat->spt - 1;
   }
    return prevSlot;
}


void chkSptChange() {
    for (formatInfo_t* p = curFormat + 1; p->encoding == curFormat->encoding && p->sSize == curFormat->sSize; p++) {
        if (abs(p->spacing - curSpacing) < 3) {
            logFull(DECODER, "Updated format to %s\n", p->shortName);
            curFormat = p;
            minSpacing = (uint16_t)(curFormat->spacing * 0.97);
            maxSpacing = (uint16_t)(curFormat->spacing * 1.03);
            updateTrackFmt();
            return;
        }
    }
    logFull(DECODER, "WARNING could not determine SPT\n");
}


void addIdam(uint16_t pos, idam_t* idam) {
    uint8_t  slot = slotAt(pos, true);

    sector_t* p = &trackPtr->sectors[slot];

    if (idam->track != trackPtr->track || idam->side != trackPtr->side)
        logFull(FATAL, "@slot %d track/side %d/%d does not match expected %d/%d\n",
            slot, idam->track, idam->side, trackPtr->track, trackPtr->side);

    if (idam->sectorId < curFormat->firstSectorId ||  idam->sectorId - curFormat->firstSectorId >= curFormat->spt)
        logFull(FATAL, "@slot %d sector Id %d out of range\n", slot, idam->sectorId);

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


void addSectorData(uint16_t pos, bool isGood, uint16_t len, uint16_t rawData[]) {
    uint8_t slot = slotAt(pos, false);
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
    q = (sectorDataList_t*)xmalloc(sizeof(sectorDataList_t) + len * sizeof(uint16_t));
    q->sectorData.len = len;
    memcpy(q->sectorData.rawData, rawData, len * sizeof(uint16_t));
    q->next = p->sectorDataList;
    p->sectorDataList = q;
}


