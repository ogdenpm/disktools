#pragma once
#include "formats.h"
#include "sectorManager.h"

#define MAXCYLINDER 84


// track status flags
enum {TS_FIXEDID = 1, TS_BADID = 2};


typedef struct {
    unsigned status;
    int cylinder;
    int side;
    formatInfo_t* fmt;
    int cntGoodIdam;
    int cntGoodData;
    int cntAnyData;
    uint8_t sectorToSlot[MAXSECTOR];        // index -> sectorId - firstSectorId
    uint8_t slotToSector[MAXSECTOR];
    sector_t sectors[];
} track_t;

extern int maxCylinder;
extern int maxHead;

extern track_t* trackPtr;

bool checkTrack();
void finaliseTrack();
track_t* getTrack(int cylinder, int side);
bool hasTrack(int cylinder, int head);
void initTrack(int cylinder, int side);
void logCylHead(int cylinder, int head);
void removeDisk();
void updateTrackFmt();
