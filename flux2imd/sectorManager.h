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
#include <stdint.h>
#include <stdbool.h>
//#include "decoders.h"
//#include "formats.h"

#define MAXSECTOR   52
#define POSJITTER   40

enum {
    SS_IDAMGOOD = 1, SS_DATAGOOD = 2, SS_GOOD = 3, SS_FIXED = 4,
};

typedef struct {
    uint8_t cylinder;
    uint8_t side;
    uint8_t sectorId;
    uint8_t sSize;
} idam_t;

typedef struct {
    unsigned len;           // of data including CRC & any link data
    uint16_t rawData[0];       // extended as needed 
} sectorData_t;

typedef struct _sectorDataList {
    struct _sectorDataList* next;
    sectorData_t sectorData;           // unnamed structure
} sectorDataList_t;



typedef struct _sector {
    unsigned status;
    idam_t idam;
    sectorDataList_t* sectorDataList;
} sector_t;


void addIdam(int pos, idam_t* idam);
void addSectorData(int pos, bool isGood, unsigned len, uint16_t rawData[]);
void removeSectorData(sectorDataList_t* p);
void resetTracker();