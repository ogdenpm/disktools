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
#include <assert.h>
#include "flux.h"
#include "trackManager.h"
#include "sectorManager.h"
#include "util.h"

int maxHead = -1;
int maxCylinder = -1;
static track_t *disk[MAXCYLINDER][2];                  // two sided disk
static bool trackLog[MAXCYLINDER][2];

track_t* trackPtr = NULL;


static void buildInterleaveMap(uint8_t *interleaveMap, int interleave, int spt) {
    memset(interleaveMap, 0xff, spt * sizeof(uint8_t));
    for (int i = 0, slot = 0; i < spt; i++) {
        while (interleaveMap[slot] != 0xff)     // skip already used slots
            slot = (slot + 1) % spt;
        interleaveMap[slot] = i;                // fill in the sector info
        slot = (slot + interleave) % spt;       // move to next sector
    }
}

static void fixSectorMap() {
    uint8_t spt = trackPtr->fmt->spt;             // makes it easier to read the code
    uint8_t* slotToSector = trackPtr->slotToSector;
    uint8_t firstSectorId = trackPtr->fmt->firstSectorId;

    uint8_t interleaveMap[MAXSECTOR];
    uint8_t firstUsedSlot;

    if (trackPtr->cntGoodIdam == 0) {
        if (trackPtr->cntAnyData == 0)      // no data or idams sector order is irrelevant
            return;
        logFull(D_WARNING, "No sector Ids found, assuming first sector is %d\n", trackPtr->fmt->firstSectorId);
        slotToSector[0] = firstSectorId;                // provide a value to find
        trackPtr->sectors[0].status |= SS_FIXED;        // mark as a fixed sectorId
    } else if (trackPtr->cntGoodIdam == 1)
        logFull(D_WARNING, "Only one sector Id found\n");

    for (firstUsedSlot = 0; firstUsedSlot < spt; firstUsedSlot++)
        if (slotToSector[firstUsedSlot] != 0xff)
            break;

    bool match = false;
    for (int interleave = 1; !match && interleave < 13; interleave++) {         // try interleaves 1-12
        buildInterleaveMap(interleaveMap, interleave, spt);
        // slotToSector uses real sector ids so find adjustment to make to interleaveMap
        uint8_t offset = (slotToSector[firstUsedSlot] - firstSectorId - interleaveMap[firstUsedSlot] + spt) % spt;
        for (int i = 0; i < spt; i++)
            interleaveMap[i] = (interleaveMap[i] + offset) % spt + firstSectorId;           // fixup interleave map 

        match = true;
        for (int i = firstUsedSlot; match && i < spt; i++) {
            if (slotToSector[i] != interleaveMap[i] && slotToSector[i] != 0xff)
                match = false;
        }
    }
    if (match) {      // one fitted so ok
        for (int i = 0; i < spt; i++) {
            if (slotToSector[i] == 0xff) {          // fixup the missing entries
                slotToSector[i] = interleaveMap[i];
                trackPtr->sectors[i].status |= SS_FIXED;
                trackPtr->sectors[i].idam.sectorId = interleaveMap[i];
            }
        }
        trackPtr->status |= TS_FIXEDID;
    } else {                                                    // can't fit interleave
        logFull(D_WARNING, "Cannot find suitable interleave. Allocating unused slots sequentially\n");
        trackPtr->status |= TS_BADID;
    }
}

static void removeTrack(track_t* p) {
    if (p) {
        for (int i = 0; i < p->fmt->spt; i++) {
            removeSectorData(p->sectors[i].sectorDataList);
            p->sectors[i].sectorDataList = NULL;
        }
        if (p == trackPtr)
            trackPtr = NULL;
        free(p);
    }
}




bool checkTrack(int profile) {
    assert(trackPtr);
    if (debug & D_NOOPTIMISE)
        return false;
    for (int i = 0; i < trackPtr->fmt->spt; i++)
        if ((trackPtr->sectors[i].status & SS_GOOD) != SS_GOOD)
            return false;
    return true;
}

void finaliseTrack() {

    if (trackPtr->cntAnyData == 0)
        return;

    if (trackPtr->cntGoodIdam == trackPtr->fmt->spt)
        return;                                         // have a full set of sectorIds

    fixSectorMap();
}

track_t* getTrack(int cylinder, int head) {
    if (cylinder >= MAXCYLINDER || head > 1)
        return NULL;
    return disk[cylinder][head];
}

bool hasTrack(int cylinder, int head) {
    return (cylinder < MAXCYLINDER && head < 2 && trackLog[cylinder][head]);
}

void initTrack(int cylinder, int head) {
    if (cylinder >= MAXCYLINDER || head > 1)
        logFull(D_FATAL, "Track %02u/%u exceeds program limits\n", cylinder, head);

    removeTrack(disk[cylinder][head]);     // clean out any pre-existing track data
    

    trackPtr = disk[cylinder][head] = (track_t*)xmalloc(sizeof(track_t) + sizeof(sector_t) * curFormat->spt);
    memset(trackPtr, 0, sizeof(*trackPtr) + sizeof(sector_t) * curFormat->spt);
    memset(trackPtr->slotToSector, 0xff, curFormat->spt);
    trackPtr->altCylinder = trackPtr->cylinder = cylinder;
    trackPtr->altSide = trackPtr->side = head;
    trackPtr->fmt = curFormat;
}

void logCylHead(int cylinder, int head) {
    if (cylinder > maxCylinder)
        maxCylinder = cylinder;
    if (head > maxHead)
        maxHead = head;
    if (cylinder < MAXCYLINDER && head < 2)
        trackLog[cylinder][head] = true;
}


void removeDisk() {
    for (int i = 0; i < MAXCYLINDER; i++) {
        removeTrack(disk[i][0]);
        removeTrack(disk[i][1]);
        disk[i][0] = disk[i][1] = NULL;
    }
    memset(trackLog, false, sizeof(trackLog));
    maxCylinder = -1;
    maxHead = -1;
}



void updateTrackFmt() {
    assert(trackPtr);
    trackPtr->fmt = curFormat;
}








