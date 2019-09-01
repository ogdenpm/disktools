#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include "analysis.h"
#include "dpll.h"
#include  "flux.h"
#include "util.h"
#include "bits.h"
#include "sectorManager.h"


#define FM500_NS 2000                // cell width in nS for FM 500bps


static uint8_t phaseAdjust[3][16] = {		// C1/C2, C3
  //  8  9   A   B   C    D   E   F   0   1   2   3   4   5   6  7 
//    {120, 130, 135, 140, 145, 150, 155, 160, 160, 165, 170, 175, 180, 185, 190, 200},
    {120, 130, 130, 140, 140, 150, 150, 160, 160, 170, 170, 180, 180, 190, 190, 200},
    {130, 140, 145, 150, 155, 158, 160, 160, 160, 160, 162, 165, 170, 175, 180, 190}

//    {130, 140, 140, 150, 150, 160, 160, 160, 160, 160, 160, 170, 170, 180, 180, 190}

};
static uint32_t ctime, etime;
static uint32_t cellSize = FM500_NS;
static int fCnt, aifCnt, adfCnt, pcCnt;
static bool up = false;
static uint32_t maxCell = FM500_NS + FM500_NS / 1000;
static uint32_t minCell = FM500_NS - FM500_NS / 1000;

static uint32_t cellDelta = FM500_NS / 100;

//     FM5, FM8, FM8H, MFM5, MFM8, M2FM,
int cellSizes[] = {
    4000, 2000, 2000, 2000, 1000, 1000
};



struct {
    int fastDivisor;
    int fastCnt;                                // note 18 1 bits allows up to 6 clock changes
    float fastTolerance;
    int mediumDivisor;
    int mediumCnt;                              // stabilising bits before patterns matches are checked
    float mediumTolerance;
    int slowDivisor;
    float slowTolerance;

} adaptConfig[] = {
//   {100, 32, 8.0, 400, 32, 4.0, 600, 0.25},
//  {100, 16, 8.0, 200, 32, 4.0, 200, 0.25},
//  {100, 21, 4.0, 400, 18, 1.0, 600, 0.5},     // FM hard sector default as no gap pre sync bytes also short blocks
  {100, 32, 10.0, 200, 32, 4.0, 200, 2.0},   
//  {100, 32, 8.0, 150, 32, 3.0, 200, 2.0}
};

static uint32_t adaptCnt;
static uint32_t adaptBitCnt;
static int adaptState;
static int adaptProfile;



enum {
    INIT, FAST, MEDIUM, SLOW
};



static void adaptDpll()     {
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

static int bitCnt = 0;

void retrain(int profile) {
    assert(curFormat->encoding < sizeof(cellSizes) / sizeof(cellSizes[0]));

    cellSize = cellSizes[curFormat->encoding];
    pattern = 0;
    fCnt = aifCnt = adfCnt = pcCnt = 0;
    up = false;

    int flux = getNextFlux();

    ctime = flux > 0 ? flux : 0;          // prime with first sample
    etime = ctime + cellSize / 2;

    adaptProfile = profile;
    adaptState = INIT;
    adaptDpll();
    bitCnt = 0;
}


int getBit() {
    int fluxVal;
    int slot;
    int cstate = 1;			// default is IPC

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
        if (++fCnt >= 3 || slot < 3 && ++aifCnt >= 3 || slot > 12 && ++adfCnt >= 3) {	// check for frequency change
            if (up) {
                if ((cellSize -= cellDelta) < minCell)
                    cellSize = minCell;
            } else if ((cellSize += cellDelta) > maxCell)
                cellSize = maxCell;
            cstate = fCnt = pcCnt = aifCnt = adfCnt = 0;
        } else if (++pcCnt >= 2)
            cstate = pcCnt = 0;
    }
    etime += phaseAdjust[cstate][slot] * cellSize / 160;

    if (++adaptBitCnt == adaptCnt) {
        adaptDpll();
    }
    return 1;
}







