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


static uint32_t ctime, etime;       // clock time and end of cell time
static uint32_t nominalCellSize = 1; 
static uint32_t cellSize;           // width of a cell
static int fCnt, aifCnt, adfCnt, pcCnt; // dpll paramaters
static bool up; 
static uint32_t maxCell;            //  bounds on cell width
static uint32_t minCell;

static uint32_t cellDelta;

uint64_t pattern;
uint16_t bits65_66 = 0;


// profile information 
typedef struct {
    int model;                                  // -1 -> end of list,  0 -> dpll, 1+ other methods
    int fastDivisor;
    int fastCnt;                                // note 18 1 bits allows up to 6 clock changes
    float fastTolerance;
    int mediumDivisor;
    int mediumCnt;                              // stabilising period for tuning clock
    float mediumTolerance;
    int slowDivisor;                            // normal mode with narrow clock change window and slow change
    float slowTolerance;

} adapt_t;


adapt_t adaptConfig_FM5[] = {
    {0, 100, 128, 10.0, 300, 256, 8.0, 400, 8.0},   // wide training window with long settle time
    {0, 100, 32, 10.0, 200, 32, 4.0, 200, 2.0},     // narrow training window with short settle time
    {0, 100, 16, 8.0, 200, 32, 4.0, 400, 0.25},     // very narrow training window
    {0, 100, 21, 8.0, 400, 32, 4.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {0, 200, 32, 6.0, 400, 32, 2.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {-1}
};

adapt_t adaptConfig_FM5H[] = {
    {0, 100, 21, 8.0, 400, 32, 4.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {0, 200, 32, 6.0, 400, 32, 2.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {0, 100, 128, 10.0, 300, 256, 8.0, 400, 8.0},   // wide training window with long settle time
    {0, 100, 32, 10.0, 200, 32, 4.0, 200, 2.0},     // narrow training window with short settle time
    {0, 100, 16, 8.0, 200, 32, 4.0, 400, 0.25},     // very narrow training window
    {-1}
};


adapt_t adaptConfig_FM8[] = {
    {0, 100, 128, 10.0, 300, 256, 8.0, 400, 8.0},   // wide training window with long settle time
    {0, 100, 32, 10.0, 200, 32, 4.0, 200, 2.0},     // narrow training window with short settle time
    {0, 100, 16, 8.0, 200, 32, 4.0, 400, 0.25},     // very narrow training window
    {0, 100, 21, 8.0, 400, 32, 4.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {0, 200, 32, 6.0, 400, 32, 2.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {-1}
};

adapt_t adaptConfig_FM8H[] = {
    {0, 100, 21, 8.0, 400, 32, 4.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {0, 200, 32, 6.0, 400, 32, 2.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {0, 100, 128, 10.0, 300, 256, 8.0, 400, 8.0},   // wide training window with long settle time
    {0, 100, 32, 10.0, 200, 32, 4.0, 200, 2.0},     // narrow training window with short settle time
    {0, 100, 16, 8.0, 200, 32, 4.0, 400, 0.25},     // very narrow training window
    {-1}
};


adapt_t adaptConfig_MFM5[] = {
    {0, 100, 128, 10.0, 300, 256, 8.0, 400, 8.0},   // wide training window with long settle time
    {0, 100, 32, 10.0, 200, 32, 4.0, 200, 2.0},     // narrow training window with short settle time
    {0, 100, 16, 8.0, 200, 32, 4.0, 400, 0.25},     // very narrow training window
    {0, 100, 21, 8.0, 400, 32, 4.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {0, 200, 32, 6.0, 400, 32, 2.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {-1}
};

adapt_t adaptConfig_MFM5H[] = {
    {0, 100, 21, 8.0, 400, 32, 4.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {0, 200, 32, 6.0, 400, 32, 2.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {0, 100, 128, 10.0, 300, 256, 8.0, 400, 8.0},   // wide training window with long settle time
    {0, 100, 32, 10.0, 200, 32, 4.0, 200, 2.0},     // narrow training window with short settle time
    {0, 100, 16, 8.0, 200, 32, 4.0, 400, 0.25},     // very narrow training window
    {-1}
};

adapt_t adaptConfig_MFM8[] = {
    {0, 100, 128, 10.0, 300, 256, 8.0, 400, 8.0},   // wide training window with long settle time
    {0, 100, 32, 10.0, 200, 32, 4.0, 200, 2.0},     // narrow training window with short settle time
    {0, 100, 16, 8.0, 200, 32, 4.0, 400, 0.25},     // very narrow training window
    {0, 100, 21, 8.0, 400, 32, 4.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {0, 200, 32, 6.0, 400, 32, 2.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {-1}
};

adapt_t adaptConfig_MFM8H[] = {
    {0, 100, 21, 8.0, 400, 32, 4.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {0, 200, 32, 6.0, 400, 32, 2.0, 600, 0.25},    // hard sector default as no gap pre sync bytes also short blocks
    {0, 100, 128, 10.0, 300, 256, 8.0, 400, 8.0},   // wide training window with long settle time
    {0, 100, 32, 10.0, 200, 32, 4.0, 200, 2.0},     // narrow training window with short settle time
    {0, 100, 16, 8.0, 200, 32, 4.0, 400, 0.25},     // very narrow training window
    {-1}
};

adapt_t adaptConfig_M2FM8[] = {
    {0, 100, 128, 10.0, 300, 256, 8.0, 400, 8.0},   // wide training window with long settle time
    {0, 100, 32, 10.0, 200, 32, 4.0, 200, 2.0},     // narrow training window with short settle time
    {0, 100, 16, 8.0, 200, 32, 4.0, 400, 0.25},     // very narrow training window
    {0, 100, 32, 10.0, 200, 32, 4.0, 200, 2.0},     // narrow training window with short settle time
    {0, 100, 16, 8.0, 200, 32, 4.0, 400, 0.25},     // very narrow training window
    {-1}
};

// profiles used by encodings E_FM5 = 0, E_FM5H, E_FM8, E_FM8H, E_MFM5, E_MFM5H, E_MFM8, E_MFM8H, E_M2FM8

struct {
    unsigned nominalCellSize;
    adapt_t *adaptConfig;                           // the current adapt config set
} dpllConfigs[] = {
    { 4000, adaptConfig_FM5 },
    { 4000, adaptConfig_FM5H},
    { 2000, adaptConfig_FM8},
    { 2000, adaptConfig_FM8H },
    { 2000, adaptConfig_MFM5 },
    { 2000, adaptConfig_MFM5H },
    { 1000, adaptConfig_MFM8 },
    { 1000, adaptConfig_MFM8H},
    { 1000, adaptConfig_M2FM8 },
};

adapt_t *adaptConfig;

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
        adaptCnt = adaptConfig[adaptProfile].fastCnt;
        deltaDivisor = adaptConfig[adaptProfile].fastDivisor;
        limit = (uint32_t)(cellSize * adaptConfig[adaptProfile].fastTolerance / 100);
        break;
    case FAST:
        adaptCnt = adaptConfig[adaptProfile].mediumCnt;
        deltaDivisor = adaptConfig[adaptProfile].mediumDivisor;
        limit = (uint32_t)(cellSize * adaptConfig[adaptProfile].mediumTolerance / 100);
        break;
    case MEDIUM:
        deltaDivisor = adaptConfig[adaptProfile].slowDivisor;
        limit = (uint32_t)(cellSize * adaptConfig[adaptProfile].slowTolerance / 100);
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
    int fluxVal;
    int slot;
    int cstate = 1;			// default is IPC

    bits65_66 = ((bits65_66 << 1) + (pattern >> 63)) & 3;
    pattern <<= 1;

    while (ctime < etime) {					// get next transition in a cell
        if ((fluxVal = getNextFlux()) < 0)
            return fluxVal;
        ctime += fluxVal;
    }

    if ((slot = 16 * (ctime - etime) / cellSize) >= 16) {	// too big a flux transition so treat as 0
        etime += cellSize;
        return 0;
    }

    if (slot < 7 || slot > 8) {
        if (slot <= 6 && !up || slot >= 9 && up) {			// check for up/down switch
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

unsigned getBitCnt() {
    return getBitPos(nominalCellSize);
}

unsigned getByteCnt() {
    return getBitCnt() / 16;
}


bool retrain(int profile) {
    if (curFormat->encoding > E_M2FM8)
        logFull(D_FATAL, "For %s unknown encoding %d\n", curFormat->name, curFormat->encoding);
    nominalCellSize = dpllConfigs[curFormat->encoding].nominalCellSize;
    adaptConfig = dpllConfigs[curFormat->encoding].adaptConfig;

    for (int i = 0; i <= profile; i++)          // check we have enough profiles
        if (adaptConfig[i].model < 0) {
            adaptProfile = 0;
            return false;
        }

    adaptProfile = profile;
    
    if (adaptConfig[profile].model == 0) {
        pattern = 0;                        // reset pattern stream
        fCnt = aifCnt = adfCnt = pcCnt = 0; // reset the dpll
        up = false;

        int flux = getNextFlux();           // prime dpll with firstr sample
        double rpm = getRPM();
        cellSize = (uint32_t)(nominalCellSize * (rpm < 320.0 ? 300.0 : 360.0) / rpm);

        ctime = flux > 0 ? flux : 0;
        etime = ctime + cellSize / 2;       // assume its the middle of a cel

        adaptState = INIT;

        adaptDpll();                        // trigger adapt start
    }
    return true;
}




