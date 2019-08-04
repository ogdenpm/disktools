#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <limits.h>
#include <memory.h>
#include <string.h>
#include "flux.h"

#define NSCLOCK					(SCK / 1000.0)	// 1 nS clock
#define CELLTICKS				(NSCLOCK * 2.0)


typedef struct _index {
	struct _index* next;
	uint32_t streamPos;
	uint32_t sampleCnt;
	uint32_t indexCnt;
	bool trackIndex;
} physBlock_t;



typedef uint8_t byte;


void* xmalloc(size_t size) {
	void* ptr;
	if (!(ptr = malloc(size))) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	return ptr;
}


	


static uint8_t phaseAdjust[3][16] = {		// C1/C2, C3
  //  8  9   A   B   C    D   E   F   0   1   2   3   4   5   6  7 
	{120, 130, 135, 140, 145, 150, 155, 160, 160, 165, 170, 175, 180, 185, 190, 200},
	{130, 140, 145, 150, 155, 160, 160, 160, 160, 160, 160, 165, 170, 175, 180, 185}

};
static uint32_t ctime, etime;
static uint32_t cellTicks = CELLTICKS;
static uint8_t fCnt, aifCnt, adfCnt, pcCnt;
static bool up = false;
static bool resync = true;
static uint32_t maxCell = CELLTICKS + 6 * (CELLTICKS / 100);
static uint32_t minCell = CELLTICKS - 6 * (CELLTICKS / 100);

static uint32_t cellDelta = CELLTICKS / 100;
static int samplingGuard = 0;
static bool cbitOnly = false;


void resetPLL(int percent) {

	int fluxVal = getNextFlux();
	if (fluxVal >= 0) {
		ctime = fluxVal;
		cellTicks = (uint32_t) (sck / 1000.0 * 2.0 + 0.5);
		fCnt = aifCnt = adfCnt = pcCnt = 0;
		up = false;
		etime = fluxVal + cellTicks / 2;	// prime with first sample
		resync = true;
		cellDelta = cellTicks / 100;		// 1% of cell timing
		maxCell = cellTicks + percent * cellDelta;	// caps on the cell timing
		minCell = cellTicks - percent * cellDelta;

	}
}

void synced() {
	resync = false;
	cellDelta = cellTicks / 100;
	maxCell = cellTicks + 8 * cellDelta;
	minCell = cellTicks - 8 * cellDelta;
}

int nextBit() {
	int fluxVal;
	int slot;
	uint8_t cstate = 1;			// default is IPC


	while (ctime < etime) {					// get next transition in a cell
		if ((fluxVal = getNextFlux()) < 0)
			return fluxVal;
		if ((ctime += fluxVal) < etime && resync) {	// assume glitch during resync is due to head settling and restart
			ctime = fluxVal;
			cellTicks = CELLTICKS;
			fCnt = aifCnt = adfCnt = pcCnt = 0;
			up = false;
			etime = fluxVal + cellTicks / 2;	// prime with first sample

		}
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
			if (up && cellTicks > minCell)
				cellTicks -= cellDelta;
			else if (!up && cellTicks < maxCell)
				cellTicks += cellDelta;
			cstate = fCnt = pcCnt = aifCnt = adfCnt = 0;
		} else if (++pcCnt >= 2) {
			cstate = pcCnt = 0;
		}
	}
	etime += phaseAdjust[cstate][slot] * cellTicks / 160;

	return 1;
}

unsigned flip(uint32_t pattern)	{
	uint8_t flipped = 0;

	for (int i = 0; i < 8; i++, pattern >>= 1)
		flipped = (flipped << 1) + (pattern & 1);
	return flipped;

}

int getByte(bool collect) {
	int bitCnt = 0;
	int pattern = 0;
	int cbit, dbit;;
	while (bitCnt != 8) {
		if ((cbit = nextBit()) < 0)
			return -1;
		if ((dbit = nextBit()) < 0)
			return -1;
		if ((collect = collect || dbit)) {
			pattern = (pattern >> 1) + (cbit ? 0x8000 : 0) + (dbit ? 0x80 : 0);
			bitCnt++;
		}
	}
	return pattern;
}

void readFlux() {
	int flux;
	uint32_t pattern;
	int val;
	char ascii[17];
	int sector;

	for (int i = 0; (sector = seekBlock(i)) >= 0; i++) {
		resetPLL(10);

		pattern = 0;
		while ((flux = nextBit()) >= 0) {
			pattern = (pattern << 1) + !!flux;
			if (pattern == 0xAAAAAAAA) {
				synced();
				break;
			}
		}
		if (flux < 0)
			printf("Sector failed to sync\n");
		else {
			printf("Sector %d:", sector);
			sector = (sector + 1) % 32;
			if ((val = getByte(false)) < 0) {
				printf(" failed to get data\n");
			} else {
				printf(" track=%d\n", (val >> 1) & 0x7f);
				for (int cnt = 0; cnt < 128 && val >= 0; cnt += 16) {
					memset(ascii, 0, 17);
					for (int i = 0; i < 16; i++) {
						if ((val = getByte(true)) < 0) {
							break;
						}
						printf("%c%02X", (val & 0xff00) != 0xff00 ? 'x' : ' ', val & 0xff);
						ascii[i] = (val & 0x7f) >= ' ' ? val & 0x7f : '.';
					}
					printf(" | %s\n", ascii);
					if (val < 0) {
						puts("premature end of sector");
						break;
					}
				}
			}
			if ((val = getByte(true)) >= 0) {
				printf("CRC1:%c%02X", (val & 0xff00) != 0xff00 ? 'x' : ' ', val & 0xff);
				if ((val = getByte(true)) >= 0)
					printf(" CRC2:%c%02X", (val & 0xff00) != 0xff00 ? 'x' : ' ', val & 0xff);
				putchar('\n');
			}
		}
	}
}


void  main(int argc, char** argv) {
	FILE* fp;
	if (argc != 2 || (fp = fopen(argv[1], "rb")) == NULL) {
		printf("usage: analysis fluxfile\n");
	} else {
		fseek(fp, 0L, SEEK_END);
		size_t fileSize = ftell(fp);
		fseek(fp, 0L, SEEK_SET);

		uint8_t *fluxBuf = (uint8_t*)xmalloc(fileSize);
		if (fread(fluxBuf, 1, fileSize, fp) != fileSize)
			fprintf(stderr, "error reading file\n");
		else {
			loadFlux(fluxBuf, fileSize);
			readFlux();
		}
		fclose(fp);
		free(fluxBuf);
	}


}