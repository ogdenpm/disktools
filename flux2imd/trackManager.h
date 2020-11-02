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

#pragma once
#include "formats.h"
#include "sectorManager.h"

#define MAXCYLINDER 84


// track status flags
enum {TS_FIXEDID = 1, TS_BADID = 2, TS_CYL = 4, TS_MCYL = 8, TS_SIDE = 16, TS_MSIDE = 32, TS_TOOMANY = 64};


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
