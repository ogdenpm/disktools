#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "flux.h"
#include "sectorManager.h"
#include "util.h"


formatInfo_t *lookupFormat(char* fmtName) {
    formatInfo_t* p;
    for (p = formatInfo; p->shortName; p++) {
        if (_stricmp(p->shortName, fmtName) == 0)
            return p;
    }
    return NULL;
}

void removeTrack(track_t* p);
void fixSectorMap();

track_t *disk[MAXTRACK][2];                  // two sided disk
track_t* trackPtr = NULL;




track_t* getTrack(uint8_t track, uint8_t side) {
    if (track >= MAXTRACK || side > 1)
        return NULL;
    return disk[track][side];
}


void initTrack(uint8_t track, uint8_t side) {
    removeTrack(disk[track][side]);     // clean out any pre-existing track data
    

    trackPtr = disk[track][side] = (track_t*)xmalloc(sizeof(track_t) + sizeof(sector_t) * curFormat->spt);
    memset(trackPtr, 0, sizeof(*trackPtr) + sizeof(sector_t) * curFormat->spt);
    memset(trackPtr->slotToSector, 0xff, curFormat->spt);
    memset(trackPtr->sectorToSlot, 0xff, curFormat->spt);
    trackPtr->track = track;
    trackPtr->side = side;
    trackPtr->fmt = curFormat;
}


void removeDisk() {
    for (int i = 0; i < MAXTRACK; i++) {
        removeTrack(disk[i][0]);
        removeTrack(disk[i][1]);
        disk[i][0] = disk[i][1] = NULL;
    }
}



void removeTrack(track_t* p) {
    if (p) {
        for (int i = 0; i < p->fmt->spt; i++)
            removeSectorData(p->sectors[i].sectorDataList);
        if (p == trackPtr)
            trackPtr = NULL;
        free(p);
    }
}


void updateTrackFmt() {
    assert(trackPtr);
    trackPtr->fmt = curFormat;
}

bool checkTrack() {
    assert(trackPtr);
    if (debug & NOOPTIMISE)
        return false;
    for (int i = 0; i < trackPtr->fmt->spt; i++)
        if (!(trackPtr->sectors[i].status & SS_IDAMGOOD))
            return false;
    return true;
}


void finaliseTrack() {

    if (trackPtr->cntAnyData == 0)
        return;

    uint8_t* sectorToSlot = trackPtr->sectorToSlot;       // short hand
    uint8_t* slotToSector = trackPtr->slotToSector;

    uint8_t spt = trackPtr->fmt->spt;

    if (trackPtr->cntGoodIdam == spt)
        return;                                         // have a full set of sectorIds

    fixSectorMap();
  
    // update the missing sector Ids
    for (int i = 0; i < trackPtr->fmt->spt; i++)
        if (!(trackPtr->sectors[sectorToSlot[i]].status & SS_IDAMGOOD)) {
            slotToSector[sectorToSlot[i]] =
                trackPtr->sectors[sectorToSlot[i]].sectorId = i + trackPtr->fmt->firstSectorId;
            trackPtr->sectors[sectorToSlot[i]].status |= SS_FIXED;
            
        }
}



void buildInterleaveMap(uint8_t *interleaveMap, int interleave, int spt) {
    memset(interleaveMap, 0xff, spt * sizeof(uint8_t));
    for (int i = 0, slot = 0; i < spt; i++) {
        while (interleaveMap[slot] != 0xff)                 // skip already used slots
            slot = (slot + 1) % spt;
        interleaveMap[slot] = i;                     // fill in the sector info
        slot = (slot + interleave) % spt;         // move to next sector
    }
}


void fixSectorMap() {
    uint8_t spt = trackPtr->fmt->spt;             // makes it easier to read the code
    int8_t interleave;
    uint8_t* sectorToSlot = trackPtr->sectorToSlot;   // shorthand
    uint8_t* slotToSector = trackPtr->slotToSector;

    uint8_t interleaveMap[MAXSECTOR];
    uint8_t firstUsedSlot;

    if (trackPtr->cntGoodIdam == 0) {
        if (trackPtr->cntAnyData == 0)      // no data or idams sector order is irrelevant
            return;
        logFull(WARNING, "No sector Ids found, assuming first is %d\n", trackPtr->fmt->firstSectorId);
        sectorToSlot[0] = 0;                //  provide a value to find
    } else if (trackPtr->cntGoodIdam == 1)
        logFull(WARNING, "Only one sector Id found\n");

    for (firstUsedSlot = 0; firstUsedSlot < spt; firstUsedSlot++)
        if (sectorToSlot[firstUsedSlot] != 0xff)
            break;

    for (interleave = 1; interleave < 13; interleave++) {       // try interleaves 1-12
        buildInterleaveMap(interleaveMap, interleave, spt);
        uint8_t offset = (sectorToSlot[firstUsedSlot] - interleaveMap[firstUsedSlot] + spt) % spt;
        int i;
        for (i = firstUsedSlot; i < spt; i++)
            if (sectorToSlot[i] != 0xff && sectorToSlot[i] != (interleaveMap[i] + offset) % spt)
                break;
        if (i == spt)
            break;
    }
    if (interleave < 13) {                                      // one fitted so ok
        memcpy(sectorToSlot, interleaveMap, spt * sizeof(uint8_t));
        trackPtr->status |= TS_FIXEDID;
    } else {                                                    // can't fit interleave
        logFull(WARNING, "Cannot find suitable interleave. Allocating unused slots sequentially\n");
        for (int i = 0, j = 0; i < spt; i++)
            if (sectorToSlot[i] == 0xff) {
                while (slotToSector[j] != 0xff)
                    j++;
                sectorToSlot[i] = j++;
            }
        trackPtr->status |= TS_BADID;
    }
}

