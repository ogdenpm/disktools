#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <ctype.h>
#include <stdint.h>
#include "flux.h"

#define MAXFLUX			32	// max flux transitions in a byte
#define MAXCELL				(CELLTICKS + 6 * (CELLTICKS / 100))
#define MINCELL				(CELLTICKS - 6 * (CELLTICKS / 100))
#define	SUSPECT			0x100

#define RESYNCTIME	100
#define SYNCCNT	32
#define SECTORBITS	105000		// minimum sector size



typedef struct {
	location_t start;
	location_t end;
} blk_t;



#define BYTES_PER_LINE  16      // for display of data dumps

bool crcCheck(const uint16_t *data, uint16_t size) {
#define CRC16 0x8005
#define INIT 0

	uint16_t crc = INIT;

	/* Sanity check: */
	if (data == NULL)
		return 0;

	while (size-- > 0) {
		for (byte i = 0x80; i; i >>= 1)
			crc = ((crc << 1) | ((*data & i) ? 1 : 0)) ^ ((crc & 0x8000) ? CRC16 : 0);
		data++;
	}
	return crc == 0;
}

static int8_t phaseAdjust[3][16] = {		// C1/C2, C3
	{12, 12, 11, 11, 10, 10, 9,  9, 8, 8, 7, 7, 6, 6, 5, 5},
	{13, 13, 12, 12, 11, 11, 10, 9, 8, 7, 6, 6, 5, 5, 4, 4}
};
static uint32_t ctime, etime;
static uint32_t cellTicks;
static uint8_t fCnt, aifCnt, adfCnt, pcCnt;
static bool up = false;
static bool resync = true;
static location_t syncLoc;
static uint32_t maxCell = CELLTICKS + 6 * (CELLTICKS / 100);
static uint32_t minCell = CELLTICKS - 6 * (CELLTICKS / 100);
static int minSyncClk = 128;

void resetPLL(int syncClk, int percent)
{
	int fluxVal = getNextFlux();
	if (fluxVal >= 0) {
		ctime = fluxVal;
		cellTicks = CELLTICKS;
		fCnt = aifCnt = adfCnt = pcCnt = 0;
		up = false;
		etime = fluxVal + cellTicks / 2;	// prime with first sample
		resync = true;
		maxCell = CELLTICKS + percent * (CELLTICKS / 100);
		minCell = CELLTICKS - percent * (CELLTICKS / 100);
		minSyncClk = syncClk;
	}
}

void synced()
{
	resync = false;
	syncLoc = where();
}

int nextBit()
{
	int fluxVal;
	int slot;
	uint8_t cstate = 1;			// default is IPC

	while (ctime < etime) {					// get next transition in a cell
		if ((fluxVal = getNextFlux()) < 0)
			return fluxVal;
		if (resync && ctime + fluxVal < etime) {	// glitch during syncing so start again
			ctime = fluxVal;
			cellTicks = CELLTICKS;
			fCnt = aifCnt = adfCnt = pcCnt = 0;
			up = false;
			etime = fluxVal + cellTicks / 2;	// prime with first sample
		} else
			ctime += fluxVal;
	}
	while ((slot = 16 * (ctime - etime) / cellTicks) >= 16) {
		etime += cellTicks;
		return 0;
	}
	if (slot < 7 || slot > 8) {
		if (slot <= 6 && !up || slot >= 9 && up) {			// check for up/down switch
			up = !up;
			pcCnt = fCnt = 0;
		}
		if (++fCnt >= 3 || slot < 3 && ++aifCnt >= 3 || slot > 12 && ++adfCnt >= 3) {	// check for frequency change
			if (up && cellTicks > minCell)
				cellTicks -= CELLTICKS / 100;
			else if (!up && cellTicks < maxCell)
				cellTicks += CELLTICKS / 100;
			cstate = fCnt = pcCnt = aifCnt = adfCnt = 0;
		} else if (++pcCnt >= 2) {
			cstate = pcCnt = 0;
		}
	}
	etime = ctime + phaseAdjust[cstate][slot] * cellTicks / 16;
	return 1;
}



int sync()
{
	uint64_t pattern = 0;
	uint32_t clkPattern, dataPattern;
	int bit;
	int clkCnt;
	int nzCnt = 0;
	int result = 0;

	while ((pattern & 0xFFFFFFFFFFFFFFFF) != 0xAAAAAAAAAAAAAAAA) {			// minumum sync is 32 zeros of data i.e. 64 cells
		if ((bit = nextBit()) < 0)
			return bit;
		pattern = (pattern << 1) + bit;
	}
	synced();

	clkCnt = 64;
	clkPattern = 0xffffffff;
	dataPattern = 0;
	// check for a clean start (32 clock bits and 24 0 data bits and last 8 data bits are 10xxxxxx i.e. possible sector
	for (clkCnt = 32; clkCnt < minSyncClk || clkPattern != 0xffffffff || (dataPattern & 0xFFFFFFC0) != 0x80; clkCnt += 2) {
		if ((bit = nextBit()) < 0)
			return bit;
		clkPattern = (clkPattern << 1) + bit;
		if ((bit = nextBit()) < 0)
			return bit;
		dataPattern = (dataPattern << 1) + bit;

	}
	return 0xff00 | dataPattern;
}

int getByte(int bcnt)		// returns data in bits 0-7 and the clock bits in bits 8-15
{
	int bit;
	int bval = 0;
	int bitCnt = 8;

	if (bcnt == 0)
		return sync();

	while (bitCnt-- > 0) {
		if ((bit = nextBit()) < 0)
			return bit;
		bval = (bval << 1) + (bit << 8);
		if ((bit = nextBit()) < 0)
			return bit;
		bval += bit;
	}
	return bval;
}


location_t start = { 0, 0 };

void decode()
{
	uint16_t data[138];
	int bval;

	seekSector(start, ~0);
	while (1) {
		resetPLL(200, 8);
		for (int i = 0; i < 138; i++) {
			bval = getByte(i);
			if (bval < 0) {
				fprintf(stderr, "unexpected EOF\n");
				exit(1);
			}
			data[i] = bval;
			printf("%04X%c", bval, i % 16 == 15 ? '\n' : ' ');
		}
		putchar('\n');
		printf("crc check %s %d\n", crcCheck(data, 136) ? "Ok" : "Bad", data[0] & 0x7f);
		if (!seekSector(where(), ~0))
			break;
	}
}