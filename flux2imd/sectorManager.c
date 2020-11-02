/****************************************************************************
 *  program: flux2imd - create imd image file from kryoflux file            *
 *  Copyright (C) 2020 Mark Ogden <mark.pm.ogden@btinternet.com>            *
 *                                                                          *
 *  This program is free software; you can redistribute it and/or           *
 *  modify it under the terms of the GNU General Public License             *
 *  as published by the Free Software Foundation; either version 2          *
 *  of the License, or (at your option) any later version.                  *
 *                                                                          *
 *  This program is distributed in the hope that it will be useful,         *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU General Public License for more details.                            *
 *                                                                          *
 *  You should have received a copy of the GNU General Public License       *
 *  along with this program; if not, write to the Free Software             *
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,              *
 *  MA  02110-1301, USA.                                                    *
 *                                                                          *
 ****************************************************************************/


// This is an open source non-commercial project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <assert.h>
#include "dpll.h"
#include "flux2imd.h"
#include "formats.h"
#include "trackManager.h"
#include "flux.h"
#include "util.h"

static int prevSlot = -1;
static unsigned curSpacing;
static unsigned minSpacing;
static unsigned maxSpacing;


// determine new spt if spacing has changed
static void chkSpacingChange(unsigned newSpacing) {

    for (formatInfo_t *p = curFormat + 1; p->encoding == curFormat->encoding && p->sSize == curFormat->sSize; p++) {
        if (abs(p->spacing - newSpacing) < 3) {
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


bool isBigGap(int delta) {
    return delta > 0 && ((double)delta / minSpacing - (double)delta / maxSpacing > 0.75);
}

// work out the slot on the disk corresponding the given pos
//  for pos <= 0 then -pos is the slot. this is used for hard sector disks

static unsigned slotAt(int pos, bool isIdam) {
    static unsigned prevIdamPos;
    static unsigned prevDataPos;
    int posDelta;

    if (pos <= 0)
        return -pos;

    if (prevSlot < 0 || pos + POSJITTER < (isIdam ? prevIdamPos : prevDataPos)) {       // initialise either explicit or due to seek from start
        curSpacing = curFormat->spacing;
        if (cntHardSectors())
            minSpacing = maxSpacing = curSpacing;
        else {
            minSpacing = curFormat->spacing * 97 / 100;     // +/- 3%
            maxSpacing = curFormat->spacing * 103 / 100;
        }
        if (isIdam) {
            prevIdamPos = pos;
            prevDataPos = prevIdamPos + curFormat->firstDATA - curFormat->firstIDAM;
        } else {
            prevDataPos = pos;
            prevIdamPos = prevDataPos + curFormat->firstIDAM - curFormat->firstDATA;
        }

        if (isBigGap(pos - (isIdam ? curFormat->firstIDAM : curFormat->firstDATA)) && !(trackPtr->status & TS_TOOMANY)) {
            logFull(D_WARNING, "Too many missing sectors at start of track, slot calculation may be wrong\n");
            trackPtr->status |= TS_TOOMANY;
        }
        prevSlot = pos / curSpacing;
    }
    posDelta = pos - (isIdam ? prevIdamPos : prevDataPos);
    if (isBigGap(posDelta) && !(trackPtr->status & TS_TOOMANY)) {
            logFull(D_WARNING, "Too many missing sectors, slot calculation may be wrong\n");
            trackPtr->status |= TS_TOOMANY;
        }

    // calculate change in slot, allow for some JITTER
    unsigned slotDelta = (posDelta + POSJITTER) / curSpacing;
    if (slotDelta == 0) {
        if (!isIdam)                    // already  have IDAM so put in real data pos
            prevDataPos = pos;
    } else {
        prevSlot += slotDelta;
        // now work out effective spacing
        unsigned newSpacing = posDelta / slotDelta;
        if ((newSpacing < minSpacing || newSpacing > maxSpacing) && curFormat->options & O_SPC)
            chkSpacingChange(newSpacing);
        // keep in tolerance
        newSpacing = newSpacing > maxSpacing ? maxSpacing : newSpacing < minSpacing ? minSpacing : newSpacing;

        if (newSpacing != curSpacing)
            DBGLOG(D_TRACKER, "Spacing changed from %d to %d\n", curSpacing, newSpacing);

        curSpacing = newSpacing;


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


void addIdam(int pos, idam_t *idam) {
    unsigned  slot = slotAt(pos, true);

    sector_t *p = &trackPtr->sectors[slot];

    if (idam->sectorId < curFormat->firstSectorId || idam->sectorId - curFormat->firstSectorId >= curFormat->spt)
        logFull(D_FATAL, "@slot %d sector Id %d out of range\n", slot, idam->sectorId);

    if (p->status & SS_IDAMGOOD) {
        if (memcmp(idam, &p->idam, sizeof(idam_t)) != 0)
            logFull(ALWAYS, "@slot %d idam mismatch prev %02d/%d/%02d (%d) new %02d/%d/%02d (%d) - change ignored\n", slot,
                p->idam.cylinder, p->idam.side, p->idam.sectorId, 128 << p->idam.sSize,
                idam->cylinder, idam->side, idam->sectorId, 128 << idam->sSize);

    } else {
        if (idam->cylinder != trackPtr->altCylinder) {
            if ((trackPtr->status & TS_CYL) == 0) {
                logFull(D_WARNING, "Expected cylinder %d, read %d\n", trackPtr->altCylinder, idam->cylinder);
                trackPtr->status |= TS_CYL;
            } else if ((trackPtr->status & TS_MCYL) == 0) {
                logFull(D_WARNING, "Multiple cylinder ids including %d & %d\n", trackPtr->altCylinder, idam->cylinder);
                trackPtr->status |= TS_MCYL;
            }
            trackPtr->altCylinder = idam->cylinder;
        }
        if (idam->side != trackPtr->altSide) {
            if ((trackPtr->status & TS_SIDE) == 0) {
                logFull(D_WARNING, "Expected head %d, read %d\n", trackPtr->altSide, idam->side);
                trackPtr->status |= TS_SIDE;
            } else if ((trackPtr->status & TS_MSIDE) == 0) {
                logFull(D_ERROR, "Multiple head ids %d & %d\n", trackPtr->altSide, idam->side);
                trackPtr->status |= TS_MSIDE;
            }
            trackPtr->altSide = idam->side;
        }
        trackPtr->cntGoodIdam++;
        p->status |= SS_IDAMGOOD;
        p->idam = *idam;
        trackPtr->slotToSector[slot] = idam->sectorId;
    }
}


void addSectorData(int pos, bool isGood, unsigned len, uint16_t rawData[]) {
    unsigned slot = slotAt(pos, false);
    sector_t *p = &trackPtr->sectors[slot];
    sectorDataList_t *q;

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
    q = (sectorDataList_t *)xmalloc(sizeof(sectorDataList_t) + len * sizeof(q->sectorData.rawData[0]));
    q->sectorData.len = len;
    memcpy(q->sectorData.rawData, rawData, len * sizeof(q->sectorData.rawData[0]));
    q->next = p->sectorDataList;
    p->sectorDataList = q;
}


void removeSectorData(sectorDataList_t *p) {
    sectorDataList_t *q;
    for (; p; p = q) {
        q = p->next;
        free(p);
    }
}


void resetTracker() {
    prevSlot = -1;
}
