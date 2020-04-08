// This is an open source non-commercial project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

#pragma once
#include "formats.h"
#include "sectorManager.h"

#define MAXCYLINDER 84


// track status flags
enum {TS_FIXEDID = 1, TS_BADID = 2, TS_CYL = 4, TS_MCYL = 8, TS_SIDE = 16, TS_MSIDE = 32};


typedef struct {
    unsigned status;
    int cylinder;
    int side;
    int altCylinder;
    int altSide;
    formatInfo_t* fmt;
    int cntGoodIdam;
    int cntGoodData;
    int cntAnyData;
    uint8_t slotToSector[MAXSECTOR];
    sector_t sectors[];
} track_t;

extern int maxCylinder;
extern int maxHead;

extern track_t* trackPtr;

bool checkTrack(int profile);
void finaliseTrack();
track_t* getTrack(int cylinder, int side);
bool hasTrack(int cylinder, int head);
void initTrack(int cylinder, int side);
void logCylHead(int cylinder, int head);
void removeDisk();
void updateTrackFmt();
