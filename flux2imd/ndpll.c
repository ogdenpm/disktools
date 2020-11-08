/****************************************************************************
 *  program: description                                                    *
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


#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "formats.h"
#include <math.h>
#include "util.h"
#include "flux.h"

static unsigned history;
static unsigned counter;
static unsigned correction;
static int32_t ctime, ftime;
static unsigned step;
static bool freqFixed = false;

#define MAXCOUNTER	4096
#define PHASEHIGH	(256 + 200) // 258
#define PHASELOW	(256 - 200) // 34
#define TIGHTHIGH	(256 + 100)
#define TIGHTLOW	(256 - 100)
#define FREQHIGH	278 // 159
#define FREQLOW		234 // 134
#define FREQMID		256 // 146

static int freqCorrection = 0;
static int phaseCorrection = 0;
static unsigned phaseHigh = PHASEHIGH;
static unsigned phaseLow = PHASELOW;
static unsigned freqHigh = FREQHIGH;
static unsigned freqLow = FREQLOW;
static unsigned clockInc = FREQMID;


static int mode[8] = { 2, 1, 0, 0, 0, 0, 1 , 2 };		// [msb << 2 + history]
static int freqCorrections[3][8] = {		// [mode][3 msb of counter]
	{0, 0, 0, 0, 0, 0, 0, 0},
	{3, 2, 1, 0, 0, -1, -2, -3},
	{4, 3, 2, 1, -1, -2, -3, -4},

};

static int phaseCorrections[8] = {			// [3 msb of counter]
	4, 3, 2, 1, -1, -2, -3, -4
};



extern uint64_t pattern;
extern uint16_t bits65_66;
static uint32_t phaseTolerance;
static unsigned bitCnts;
static unsigned cntClockInc;
static unsigned histInc[FREQHIGH - FREQLOW + 1];

#define TUNESTART 16
#define FREQTUNED1	  (TUNESTART + 400)
#define FREQTUNED2	  (TUNESTART + 800)
#define PHASETUNEEND (TUNESTART + 800)
#define TUNEEND (FREQTUNED2 > PHASETUNEEND ? FREQTUNED2 : PHASETUNEEND)
#define TUNELIMIT1	8
#define TUNELIMIT2	4

#define MODEL2
int freqStep = 2;

int getBit() {
	int bitRead = 0;
	int transition = 0;
	int iter = 0;
	bits65_66 = ((bits65_66 << 1) + (pattern >> 63)) & 3;
	pattern <<= 1;

	while (counter < MAXCOUNTER) {
		if ((ctime += step) >= ftime) {
			bitRead = 1;
			while ((ftime = getNextFlux()) < ctime)
				if (ftime < 0)
					return -1;
		}

		if (bitRead && !transition) {
			history |= (counter >> 9) & 4;
			freqCorrection = freqCorrections[mode[history]][(counter >> 9) & 7];
			phaseCorrection = phaseCorrections[(counter >> 9) & 7];
			history >>= 1;
			transition = 1;
		}
		if (freqCorrection) {
			clockInc += freqCorrection > 0 ? freqStep : -freqStep;
			if (clockInc > freqHigh)
				clockInc = freqHigh;
			else if (clockInc < freqLow)
				clockInc = freqLow;
			freqCorrection += freqCorrection > 0 ? -1 : 1;
		}

		if (phaseCorrection) {
			counter += phaseCorrection > 0 ? phaseHigh : phaseLow;
			phaseCorrection += phaseCorrection > 0 ? -1 : 1;
		} else
			counter += clockInc;
	}
	++bitCnts;
	counter -= MAXCOUNTER;
#ifdef MODEL2
	if (bitCnts <= TUNEEND) {
		if (bitCnts == TUNESTART)
			cntClockInc = 0;
		else
			cntClockInc += clockInc;
		if (!freqFixed && bitCnts == FREQTUNED1) {
			// get average of clockInc i.e. freq base
			unsigned avgClockInt = (cntClockInc + (FREQTUNED1 - TUNESTART) / 2) / (FREQTUNED1 - TUNESTART);
			if (avgClockInt + TUNELIMIT1 <= freqHigh)
				freqHigh = avgClockInt + TUNELIMIT1;
			if (avgClockInt - TUNELIMIT1 >= freqLow)
				freqLow = avgClockInt - TUNELIMIT1;
			freqStep = 1;
		} else if (!freqFixed && bitCnts == FREQTUNED2) {
			// get average of clockInc i.e. freq base
			unsigned avgClockInt = (cntClockInc + (FREQTUNED2 - TUNESTART) / 2) / (FREQTUNED2 - TUNESTART);
			if (avgClockInt + TUNELIMIT2 <= freqHigh)
				freqHigh = avgClockInt + TUNELIMIT2;
			if (avgClockInt - TUNELIMIT2 >= freqLow)
				freqLow = avgClockInt - TUNELIMIT2;
			freqFixed = true;
		}
		if (bitCnts == PHASETUNEEND) {
			phaseHigh = TIGHTHIGH;
			phaseLow = TIGHTLOW;
		}
	 }
#endif
	 pattern += bitRead;
	 return bitRead;
}

extern uint32_t nominalCellSize;

bool retrain(int profile, bool full) {
	if (curFormat->encoding > E_M2FM8)
		logFull(D_FATAL, "For %s unknown encoding %d\n", curFormat->name, curFormat->encoding);
	nominalCellSize = curFormat->nominalCellSize;
	step = 64 * (curFormat->nominalCellSize / 1000);
	if (profile >= strlen(curFormat->profileOrder)) {
		return false;
	}
	freqCorrection = 0;
	phaseCorrection = 0;
	if (full) {
		phaseHigh = PHASEHIGH;
		phaseLow = PHASELOW;
		freqHigh = FREQHIGH;
		freqLow = FREQLOW;
		clockInc = FREQMID;
		bitCnts = 0;
		freqFixed = false;
		freqStep = 2;
	}
	ctime = getNextFlux();
	counter = 0;

	pattern = 0;

	return ((ftime = getNextFlux()) > 0);
}

void retune() {
	phaseHigh = PHASEHIGH;
	phaseLow = PHASELOW;
//	freqHigh = FREQHIGH;
//	freqLow = FREQLOW;
//	clockInc = FREQMID;
//	freqFixed = false;
//	freqStep = 2;
	bitCnts = 0;
}