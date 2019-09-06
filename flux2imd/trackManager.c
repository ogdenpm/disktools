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
    int8_t interleave;
    uint8_t* sectorToSlot = trackPtr->sectorToSlot;   // shorthand
    uint8_t* slotToSector = trackPtr->slotToSector;

    uint8_t interleaveMap[MAXSECTOR];
    uint8_t firstUsedSlot;

    if (trackPtr->cntGoodIdam == 0) {
        if (trackPtr->cntAnyData == 0)      // no data or idams sector order is irrelevant
            return;
        logFull(D_WARNING, "No sector Ids found, assuming first is %d\n", trackPtr->fmt->firstSectorId);
        sectorToSlot[0] = 0;                //  provide a value to find
    } else if (trackPtr->cntGoodIdam == 1)
        logFull(D_WARNING, "Only one sector Id found\n");

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
        logFull(D_WARNING, "Cannot find suitable interleave. Allocating unused slots sequentially\n");
        trackPtr->status |= TS_BADID;
    }
}

static void removeTrack(track_t* p) {
    if (p) {
        for (int i = 0; i < p->fmt->spt; i++)
            removeSectorData(p->sectors[i].sectorDataList);
        if (p == trackPtr)
            trackPtr = NULL;
        free(p);
    }
}




bool checkTrack() {
    assert(trackPtr);
    if (debug & D_NOOPTIMISE)
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
    if (trackPtr->status & TS_FIXEDID) {
        // update the missing sector Ids
        for (int i = 0; i < trackPtr->fmt->spt; i++)
            if (!(trackPtr->sectors[sectorToSlot[i]].status & SS_IDAMGOOD)) {
                slotToSector[sectorToSlot[i]] =
                    trackPtr->sectors[sectorToSlot[i]].sectorId = i + trackPtr->fmt->firstSectorId;
                trackPtr->sectors[sectorToSlot[i]].status |= SS_FIXED;

            }
    }
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
    memset(trackPtr->sectorToSlot, 0xff, curFormat->spt);
    trackPtr->cylinder = cylinder;
    trackPtr->side = head;
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








