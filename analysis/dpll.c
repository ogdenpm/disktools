#include <stdint.h>
#include <stdbool.h>
#include "dpll.h"
#include  "flux.h"

static uint8_t phaseAdjust[3][16] = {		// C1/C2, C3
  //  8  9   A   B   C    D   E   F   0   1   2   3   4   5   6  7 
    {120, 130, 135, 140, 145, 150, 155, 160, 160, 165, 170, 175, 180, 185, 190, 200},
    {130, 140, 145, 150, 155, 160, 160, 160, 160, 160, 160, 165, 170, 175, 180, 185}

};
static uint32_t ctime, etime;
static uint32_t cellTicks = CELL_NS;
static uint8_t fCnt, aifCnt, adfCnt, pcCnt;
static bool up = false;
static uint32_t maxCell = CELL_NS + PRESYNC_TOLERANCE * CELL_NS / 100;
static uint32_t minCell = CELL_NS - PRESYNC_TOLERANCE * CELL_NS / 100;
static int postSyncTolerance = POSTSYNC_TOLERANCE;

static uint32_t cellDelta = CELL_NS / 100;


void resetPLL(int preSync, int postSync) {
    // reset PLL parameters to defaults
    cellTicks = CELL_NS;
    fCnt = aifCnt = adfCnt = pcCnt = 0;
    up = false;

    cellDelta = cellTicks / 100;		        // 1% of cell timing
    maxCell = cellTicks + preSync * cellDelta;	// caps on the cell timing
    minCell = cellTicks - preSync * cellDelta;
    postSyncTolerance = postSync;

    int fluxVal = getNextFlux();

    ctime = fluxVal > 0 ? fluxVal : 0;          // prime with first sample
    etime = ctime + cellTicks / 2;	
}

bool clockSync() {
    uint32_t pattern = 0;
    int flux;

    while ((flux = getBit()) >= 0) {
        pattern = (pattern << 1) + flux;
        if (pattern == 0xAAAAAAAA)
            break;
    }
    cellDelta = cellTicks / 100;
    maxCell = cellTicks + postSyncTolerance * cellDelta;
    minCell = cellTicks - postSyncTolerance * cellDelta;
    return flux >= 0;
}

int getBit() {
    int fluxVal;
    int slot;
    uint8_t cstate = 1;			// default is IPC


    while (ctime < etime) {					// get next transition in a cell
        if ((fluxVal = getNextFlux()) < 0)
            return fluxVal;
        ctime += fluxVal;
    }
    while ((slot = 16 * (ctime - etime) / cellTicks) >= 16) {	// too big a flux transition so treat as 0
        etime += cellTicks;
        return 0;
    }

    if (slot < 7 || slot > 8) {
        if (slot <= 6 && !up || slot >= 9 && up) {			// check for up/down switch
            up = !up;
            pcCnt = fCnt = 0;
        }
        if (++fCnt >= 3 || slot < 3 && ++aifCnt >= 3 || slot > 12 && ++adfCnt >= 3) {	// check for frequency change
            if (up) {
                if ((cellTicks -= cellDelta) < minCell)
                    cellTicks = minCell;
            } else if ((cellTicks += cellDelta) > maxCell)
                cellTicks = maxCell;
            cstate = fCnt = pcCnt = aifCnt = adfCnt = 0;
        } else if (++pcCnt >= 2)
            cstate = pcCnt = 0;
    }
    etime += phaseAdjust[cstate][slot] * cellTicks / 160;

    return 1;
}


int getBitPair() {
    int cBit, dBit;
    if ((cBit = getBit()) < 0)
        return -1;
    if ((dBit = getBit()) < 0)
        return -1;
    return (cBit << 8) + dBit;

}