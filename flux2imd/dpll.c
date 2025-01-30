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

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include "dpll.h"
#include "flux.h"
#include "util.h"
#include "trackManager.h"
#include "stdflux.h"

static int32_t ctime, etime;       // clock time and end of cell time
static int32_t nominalCellSize = 1; 
int32_t cellSize;           // width of a cell
static int fCnt, aifCnt, adfCnt, pcCnt; // dpll paramaters
static bool up; 
static int32_t maxCell;            //  bounds on cell width
static int32_t minCell;

static int32_t cellDelta;

uint64_t pattern;
uint16_t bits65_66 = 0;


// profile information 
typedef struct {
    int fastDivisor;
    int fastCnt;                                // note 18 1 bits allows up to 6 clock changes
    float fastTolerance;
    int mediumDivisor;
    int mediumCnt;                              // stabilising period for tuning clock
    float mediumTolerance;
    int slowDivisor;                            // normal mode with narrow clock change window and slow change
    float slowTolerance;

} adapt_t;

adapt_t profiles[] = {
/* 0 */    {100, 128, 10.0, 300, 256, 8.0, 400, 8.0},   // wide training window with long settle time
/* 1 */    {100, 32, 10.0, 200, 32, 4.0, 200, 2.0},     // narrow training window with short settle time
/* 2 */    {100, 16, 8.0, 200, 32, 4.0, 400, 0.25},     // very narrow training window
/* 3 */    {100, 21, 8.0, 400, 32, 4.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
/* 4 */    {200, 32, 6.0, 400, 32, 2.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
};

#define CNTPROFILE  (int)(sizeof(profiles)/ sizeof(profiles[0]))


// profiles used by encodings E_FM5 = 0, E_FM5H, E_FM8, E_FM8H, E_MFM5, E_MFM5H, E_MFM8, E_MFM8H, E_M2FM8

static uint32_t adaptCnt;
static uint32_t adaptBitCnt;
static int adaptProfile;

static enum {
    INIT, FAST, MEDIUM, SLOW
} adaptState;


// extension to narrow frequency adaption rate once in the right ball park
static void adaptDpll() {
    uint32_t deltaDivisor;
    uint32_t limit;

    switch (adaptState) {
    case INIT:
        adaptCnt = profiles[adaptProfile].fastCnt;
        deltaDivisor = profiles[adaptProfile].fastDivisor;
        limit = (uint32_t)(cellSize * profiles[adaptProfile].fastTolerance / 100);
        break;
    case FAST:
        adaptCnt = profiles[adaptProfile].mediumCnt;
        deltaDivisor = profiles[adaptProfile].mediumDivisor;
        limit = (uint32_t)(cellSize * profiles[adaptProfile].mediumTolerance / 100);
        break;
    case MEDIUM:
        deltaDivisor = profiles[adaptProfile].slowDivisor;
        limit = (uint32_t)(cellSize * profiles[adaptProfile].slowTolerance / 100);
        break;
    default:
        return;
    }
    adaptState++;
    adaptBitCnt = 0;
    cellDelta = cellSize / deltaDivisor;
    maxCell = cellSize + limit;
    minCell = cellSize - limit;
}



/* this is the key dpll model largely based on US patent 4808884  */
static uint8_t phaseAdjust[2][16] = {		// C1/C2, C3
    //  8    9   A     B    C    D    E    F    0    1    2    3    4    5    6    7 
    {120, 130, 135, 140, 145, 150, 155, 160, 160, 165, 170, 175, 180, 185, 190, 200},
    //    {120, 130, 130, 140, 140, 150, 150, 160, 160, 170, 170, 180, 180, 190, 190, 200},
    {130, 140, 145, 150, 155, 158, 160, 160, 160, 160, 162, 165, 170, 175, 180, 190}

    //    {130, 140, 140, 150, 150, 160, 160, 160, 160, 160, 160, 170, 170, 180, 180, 190}
    //  8   9   A   B   C   D   E   F   0   1   2   3   4   5   6   7
    //  {12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20},
    //  {13, 14, 14, 15, 15, 16, 16, 16, 16, 16, 16, 17, 17, 18, 18, 19}
};

int getBit() {
    int slot;
    int cstate = 1;			// default is IPC

    bits65_66 = ((bits65_66 << 1) + (pattern >> 63)) & 3;
    pattern <<= 1;

    while (ctime < etime) {					// get next transition in a cell
        if ((ctime = getTs()) < 0)
            return ctime;
    }

    if ((slot = 16 * (ctime - etime) / cellSize) >= 16) {	// too big a flux transition so treat as 0
        etime += cellSize;
        return 0;
    }

    if (slot < 7 || slot > 8) {
        if ((slot <= 6 && !up) || (slot >= 9 && up)) {			// check for up/down switch
            up = !up;
            pcCnt = fCnt = 0;
        }
        if (++fCnt >= 3 || (slot < 3 && ++aifCnt >= 3) || (slot > 12 && ++adfCnt >= 3)) {	// check for frequency change
            if (up) {
                if ((cellSize -= cellDelta) < minCell)
                    cellSize = minCell;
            } else if ((cellSize += cellDelta) > maxCell)
                cellSize = maxCell;
            cstate = fCnt = pcCnt = aifCnt = adfCnt = 0;
        } else if (++pcCnt >= 2)
            cstate = pcCnt = 0;
    }
#ifdef CALCULATE
    if (cstate == 0)
        etime += ((ctime - etime) * 5059 + cellSize * 7629) / 10000;
    else
        etime += ((ctime - etime) * 3206 + cellSize * 8497) / 10000;
#else
    etime += phaseAdjust[cstate][slot] * cellSize / 160;
#endif

    if (++adaptBitCnt == adaptCnt) {
        adaptDpll();
    }
    pattern++;
    return 1;
}

int32_t getBitCnt(int32_t fromTs) {
    return (peekTs() - fromTs) / nominalCellSize;
}

int32_t getByteCnt(int32_t fromTs) {
    return getBitCnt(fromTs) / 16;
}

bool retrain(int profile) {
    if (curFormat->encoding > E_M2FM8)
        logFull(D_FATAL, "For %s unknown encoding %d\n", curFormat->name, curFormat->encoding);
    nominalCellSize = curFormat->nominalCellSize;

    if (profile >= CNTPROFILE || profile >= (int)strlen(curFormat->profileOrder)) {
        adaptProfile = 0;
        return false;
    }

    adaptProfile = curFormat->profileOrder[profile] - '0';


    pattern = 0;                        // reset pattern stream
    fCnt = aifCnt = adfCnt = pcCnt = 0; // reset the dpll
    up = false;

    while ((ctime = getTs()) < 0)
        if (ctime == EODATA)
            return false;               // prime dpll with first sample
    cellSize = nominalCellSize;

    etime = ctime + cellSize / 2;       // assume its the middle of a cel

    adaptState = INIT;

    adaptDpll();                        // trigger adapt start
    return true;
}




