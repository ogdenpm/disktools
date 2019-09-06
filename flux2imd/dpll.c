#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include "dpll.h"
#include "flux.h"
#include "util.h"
#include "trackManager.h"


static uint32_t ctime, etime;       // clock time and end of cell time
static uint32_t cellSize;           // width of a cell
static int fCnt, aifCnt, adfCnt, pcCnt; // dpll paramaters
static bool up; 
static uint32_t maxCell;            //  bounds on cell width
static uint32_t minCell;

static uint32_t cellDelta;

uint64_t pattern;
static unsigned bitCnt;


// profile information 
static struct {
    int fastDivisor;
    int fastCnt;                                // note 18 1 bits allows up to 6 clock changes
    float fastTolerance;
    int mediumDivisor;
    int mediumCnt;                              // stabilising bits before patterns matches are checked
    float mediumTolerance;
    int slowDivisor;
    float slowTolerance;

} adaptConfig[] = {
      {100, 21, 8.0, 400, 32, 4.0, 600, 0.25},    // FM hard sector default as no gap pre sync bytes also short blocks
    //  {100, 16, 8.0, 200, 32, 4.0, 200, 0.25},
   //   {100, 21, 4.0, 400, 18, 1.0, 600, 0.5}, 
    //{100, 32, 10.0, 200, 32, 4.0, 200, 2.0},
    //  {100, 32, 8.0, 150, 32, 3.0, 200, 2.0}
};

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
    //  8   9   A   B   C   D   E   F   0   1   2   3   4   5   6   7
      {12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20},
      {13, 14, 14, 15, 15, 16, 16, 16, 16, 16, 16, 17, 17, 18, 18, 19}
};
int getBit() {
    int fluxVal;
    int slot;
    int cstate = 1;			// default is IPC

    pattern <<= 1;
    bitCnt++;

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
#if CALCULATE
	if (cstate == 0)
		etime += ((ctime - etime) * 5059 + cellSize * 7629) / 10000;
	else
		etime += ((ctime - etime) * 3206 + cellSize * 8497) / 10000;
#else
    etime += phaseAdjust[cstate][slot] * cellSize / 16;
#endif

    if (++adaptBitCnt == adaptCnt) {
        adaptDpll();
    }
    pattern++;
    return 1;
}

unsigned getBitCnt() {
    return bitCnt;
}

unsigned getByteCnt() {
    return bitCnt / 16;
}

void retrain(int profile) {
    switch (curFormat->encoding) {
    case E_FM5:
        cellSize = 4000;
        break;
    case E_FM8: case E_FM8H: case E_MFM5:
        cellSize = 2000;
        break;
    case E_MFM8: case E_M2FM8:
        cellSize = 1000;
        break;
    default:
        logFull(D_FATAL, "For %s unknown encoding %d\n", curFormat->name, curFormat->encoding);
    }

    pattern = 0;                        // reset pattern stream
    bitCnt = 0;
    fCnt = aifCnt = adfCnt = pcCnt = 0; // reset the dpll
    up = false;

    int flux = getNextFlux();           // prime dpll with firstr sample

    ctime = flux > 0 ? flux : 0;
    etime = ctime + cellSize / 2;       // assume its the middle of a cel

    adaptProfile = profile;             // rest the adapt model
    adaptState = INIT;
    adaptDpll();                        // trigger adapt start
}




