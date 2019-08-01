#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <ctype.h>
#include <stdint.h>
#include "flux.h"


#define MAXCELL				(CELLTICKS + 6 * (CELLTICKS / 100))
#define MINCELL				(CELLTICKS - 6 * (CELLTICKS / 100))

#define NOSYNC	-4
#define NOTS	-5

#define MAXFLUX			32	// max flux transitions in a byte

#define	SUSPECT			0x100

#define RESYNCTIME	100
#define SYNCCNT	32
#define SYNCCELLS	(16 * 8 * 2)	// number of clock cells for a sync
#define SECTORCELLS	(SYNCCELLS + 138 * 8 * 2)	// number of clock cells for a whole sector (sync + data + links + crc + postamble)
#define MAXLEADIN	200
#define MINLEADOUT	50

#define SECTORBITS	105000		// minimum sector size in bitpos

#define MAXMISSING	8		// max missing sectors to allow for for sector Id check

secList_t track[NUMSECTOR];

typedef struct {
	location_t start;
	location_t end;
} blk_t;

typedef struct _blkList {
	struct _blkList *next;
	blk_t block;
	int secId;
} blkList_t;

#define JITTER_ALLOWANCE        20

#define BYTES_PER_LINE  16      // for display of data dumps

void removeSectors(secList_t *p)
{
	secList_t *q, *s;
	for (q = p->next; q; q = s) {
		s = q->next;
		free(q);
	}
	p->flags = 0;
	p->next = NULL;
}

void addSector(sector_t *sec, bool goodCRC) {		// track and sector must be in range


	secList_t *p = &track[sec->sector & 0x1f];
	secList_t *q;

	if (p->flags == 1)				// already good
		return;
	if (goodCRC) {
		if (p->flags == 2)			// had a bad list so remove it
			removeSectors(p);
		p->secData = *sec;
		p->flags = 1;
	} else {
		// allocate failed sector
		q = (secList_t *)xmalloc(sizeof(secList_t));
		q->next = p->next;
		p->next = q;
		q->flags = -1;							// flags used later for corrupt record processing
		q->secData = *sec;
		p->flags = 2;
	}
}

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
static int minSyncClk = 128;
static uint32_t cellDelta = CELLTICKS / 100;
static int samplingGuard = 0;
static bool cbitOnly = false;

void dumpstate()
{
	printf("ctime = %ld, etime = %ld, cellTicks = %ld, fCnt = %d, aifCnt = %d, adfCnt = %d pcCnt = %d\n", ctime, etime, cellTicks, fCnt, aifCnt, adfCnt, pcCnt);
}

bool resetPLL(int syncClk, int percent, int guard, bool cbit)
{
	int fluxVal = getNextFlux();
	if (fluxVal >= 0) {
		ctime = fluxVal;
		cellTicks = CELLTICKS;
		fCnt = aifCnt = adfCnt = pcCnt = 0;
		up = false;
		etime = fluxVal + cellTicks / 2;	// prime with first sample
		resync = true;
		cellDelta = cellTicks / 100;
		maxCell = cellTicks + percent * cellDelta;
		minCell = cellTicks - percent * cellDelta;
		minSyncClk = syncClk;
		samplingGuard = guard;
		cbitOnly = cbit;
	}
	return true;
}

void synced()
{
	resync = false;
	cellDelta = cellTicks / 100;
	maxCell = cellTicks + 8 * cellDelta;
	minCell = cellTicks - 8 * cellDelta;
}


int nextBit()
{
	int fluxVal;
	int slot;
	uint8_t cstate = 1;			// default is IPC
	static bool cbit = false;

	while (ctime < etime) {					// get next transition in a cell
		if ((fluxVal = getNextFlux()) < 0)
			return fluxVal;
		if ((ctime += fluxVal) < etime) {
			if (ctime + samplingGuard >= etime)	// treat as sampling error i.e. 3/4 clock out
				etime = ctime;
			else if (resync) {
				ctime = fluxVal;
				cellTicks = CELLTICKS;
				fCnt = aifCnt = adfCnt = pcCnt = 0;
				up = false;
				etime = fluxVal + cellTicks / 2;	// prime with first sample
			}
		}
	}
	while ((slot = 16 * (ctime - etime) / (cellTicks + samplingGuard)) >= 16) {
		etime += cellTicks;
		cbit = false;
		return 0;
	}
	if ( (cbit = !cbit) || !cbitOnly) {		// sync on all or only clock transitions
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
	} else
		etime +=  cellTicks;
	return 1;
}


location_t skip(size_t bitPos)
{
	while (where().bitPos < bitPos && getNextFlux() >= 0)
		;
	return where();
}


int sync(int trk)
{
	uint64_t pattern = 0;
	uint64_t clkPattern, dataPattern;
	int bit;
	int clkCnt = 0;
	uint64_t matchPattern, matchMask;
	if (trk >= 0) {
		matchMask = 0xFFFFFFE0FF;
		matchPattern = 0x8000 | trk;
	} else {
		matchMask = 0xFFFFFFE0;
		matchPattern = 0x80;
	}

	while ((pattern & 0xFFFFFFFFFFFFFFFF) != 0xAAAAAAAAAAAAAAAA) {			// minumum sync is 32 zeros of data i.e. 64 cells
		if (++clkCnt > MAXLEADIN + 64 && (pattern & 0xffff) != 0xAAAA && (pattern & 0xffff) != 0x5555)	// do if no sync
																			// extra test is in case it has started
			return NOSYNC;
		if ((bit = nextBit()) < 0)
			return bit;
		pattern = (pattern << 1) + bit;
	}
	synced();

	clkPattern = 0;
	dataPattern = 0;
	// check for a clean start (32 clock bits and 24 0 data bits and last 8 data bits are 10xxxxxx i.e. possible sector
	for (clkCnt = 64; clkCnt < minSyncClk || (dataPattern & matchMask) != matchPattern; clkCnt += 2) {
		if (clkCnt > MAXLEADIN + SYNCCELLS + 8 * 2)
			return NOTS;
		if ((clkPattern & 0xffff) == 0 && (dataPattern & 0xffff) == 0xffff) {		// we syned too early so flip
			synced();											// reset clock paramaters
			clkPattern = 0xffff;
			dataPattern = 0;
			clkCnt = 32;
		} else {
			if ((bit = nextBit()) < 0)
				return bit;
			clkPattern = (clkPattern << 1) + bit;
		}
		if ((bit = nextBit()) < 0)
			return bit;
		dataPattern = (dataPattern << 1) + bit;
	}

	return 0xff00 | (uint16_t)((trk >= 0 ? (dataPattern >> 8) : dataPattern) & 0xff);
}

int getByte()		// returns data in bits 0-7 and the clock bits in bits 8-15
{
	int bit;
	int bval = 0;
	int bitCnt = 8;
	if (debug >= VERYVERBOSE)
		putchar('\n');
	while (bitCnt-- > 0) {
		if ((bit = nextBit()) < 0)
			return bit;
		bval = (bval << 1) + (bit << 8);
		if ((bit = nextBit()) < 0)
			return bit;
		bval += bit;
	}
	if (debug >= VERYVERBOSE)
		printf("[%4X]", bval);
	return bval;
}





void printDataLine(uint16_t *p, int marker)
{
	if (marker)
		putchar(marker);
	for (int j = 0; j < 16; j++)
		printf("%02X%c ", p[j] & 0xff, marker && (p[j] >> 8) != 0xff ? '*' : ' ');

	for (int j = 0; j < 16; j++) {
		int c = p[j] & 0xff;
		printf("%c", (' ' <= c && c <= '~') ? c : '.');
	}
	putchar('\n');
}

void printLinkLine(uint16_t *p, int marker)
{
	// data stored in order, fsector, ftrack, bsector, btrack, crc1, crc2, postamble1, postable2
	if (marker)
		putchar(marker);
	printf(marker && (p[3] >> 8) != 0xff ? "forward %d*" : "forward %d", p[3] & 0xff);
	printf(marker && (p[2] >> 8) != 0xff ? "/%d*" : "/%d", p[2] & 0xff);
	printf(marker && (p[1] >> 8) != 0xff ? " backward %d*" : " backward %d", p[1] & 0xff);
	printf(marker && (p[0] >> 8) != 0xff ? "/%d*" : "/%d", p[0] & 0xff);
	printf(marker && (p[4] >> 8) != 0xff ? " crc %02X*" : " crc %02X", p[4] & 0xff);
	printf(marker && (p[5] >> 8) != 0xff ? " %02X*" : " %02X", p[5] & 0xff);
	printf((marker || (p[6] & 0xff)) && (p[6] >> 8) != 0xff ? " Postamble %02X*" : " Postamble %02X", p[6] & 0xff);
	printf((marker || (p[7] & 0xff)) && (p[7] >> 8) != 0xff ? " %02X*\n" : " %02X\n", p[7] & 0xff);

}

int mergeData(secList_t *p, uint16_t *line, int i)
{
	int cnt;
	int j;
	int cmpLen = i == 128 ? 8 : 16;

	secList_t *q;
	for (cnt = 0; p && p->flags == i; p = p->next, cnt++)
		;
	if (!p)
		return -1;
	memcpy(line, &p->secData.raw[2 + i], cmpLen * sizeof(uint16_t));
	p->flags = i;
	for (q = p->next; q; q = q->next) {
		for (j = 0; j < cmpLen; j++)
			if ((line[j] & 0xff) != (q->secData.raw[2 + i + j] & 0xff) && (q->secData.raw[2 + i + j] >> 8) == 0xff)
				break;
		if (j == cmpLen) {
			q->flags = i;
			for (j = 0; j < cmpLen; j++)
				if (!(line[j] >> 8) & !(q->secData.raw[2 + i + j] >> 8))
					line[j] &= 0xff;
		}
	}
	return cnt;
}


void dumpSector(sector_t *secPtr)
{
	printf("%d/%d\n", secPtr->track & 0xff, secPtr->sector & 0x7f);
	for (int i = 0; i < SECSIZE; i += 16) {
			printDataLine(&secPtr->data[i], 0);
	}
	printLinkLine((uint16_t *)&secPtr->bsector, 0);
	printf("\n");
}



void displaySector(secList_t *secPtr, int trkId, int secId)
{
	uint16_t line[16];
	int instance;

	printf("\n%d/%d:%s", trkId, secId, secPtr->flags == 1 ? "\n" : " ---- Corrupt Sector ----\n");


	for (int i = 0; i < SECSIZE; i += 16) {
		if (secPtr->flags == 1)
			printDataLine(&secPtr->secData.data[i], 0);
		else
			while ((instance = mergeData(secPtr->next, line, i)) >= 0)
				printDataLine(line, instance == 0 ? ' ' : '+');
	}
	if (secPtr->flags == 1)
		printLinkLine((uint16_t *)&secPtr->secData.bsector, 0);
	else
		while ((instance = mergeData(secPtr->next, line, 128)) >= 0)
			printLinkLine(line, instance == 0 ? ' ' : '+');


}

bool reasonable(sector_t *sptr, int limit)		// returns true if < limit bytes of bad clock
{
	int bad = 0;
	for (int i = 0; i < SECSIZE + SECMETA; i++)
		if ((sptr->raw[i] >> 8) != 0xff && ++bad >= limit)
			return false;
	return true;
}

void flux2track(int trk)
{
	sector_t data;
	int dbyte;
	int rcnt;
	int secValid[NUMSECTOR];	// holds the pass on which valid sector detected
	int cntValid = 0;			// number of unique valid sectors
	int sval;
	int guard = 0;
	int tolerance = 16;
	bool gotSector = false;
	bool cbit = false;

	memset(track, 0, sizeof(track));
	memset(secValid, 0, sizeof(secValid));

	location_t start = where();		// open will have rewound so this is start of stream

	cntValid = 0;
	while (resetPLL(150, 10, guard, cbit) && (sval = sync(trk)) != END_FLUX) {
		if (sval < 0) {
			if (sval == NOSYNC || !gotSector)
				start = where();
			else if (guard < 1000) {
				guard += 250;
				seekSector(start);
			} else {
				guard = 0;
				// failed to read so skip the sector
				start = skip(start.bitPos + (SECTORCELLS) * cellTicks / 1000);
			}
			continue;
		}

		gotSector = true;
		data.sector = sval;
		data.track = trk | 0xff00;
		for (rcnt = 2; rcnt < SECSIZE + SECMETA + SECPOSTAMBLE; rcnt++)
			if ((dbyte = getByte(rcnt)) >= 0)
				data.raw[rcnt] = dbyte;
			else
				break;

		if (dbyte == END_FLUX)
			break;

		if (debug >= VERYVERBOSE) dumpSector(&data);

		if (!secValid[sval & 0x1f]) {							// don't need to process if we already have a valid copy
			if (crcCheck(data.raw, SECSIZE + SECMETA)) {		// data looks good
				addSector(&data, true);
				if (secValid[sval & 0x1f]++ == 0 && ++cntValid == NUMSECTOR)
					break;
			} else {
				if (reasonable(&data, tolerance))		// make sure very bad sectors are ignored
					addSector(&data, false);
				/* try decoding again using the following heuristics 
					toggle training on all vs. clock only transitions - normal is all
					catch possible sampling errors by allowing 0.25 - 0.75 of a kryoflux clock cycle error
				*/
				if (!cbit || guard < 750) {			// cbit toggles all/clock only transitions
					if (cbit)
						guard += 250;				// 250 corresponds to 0.25 kryoflux clock cycle
					cbit = !cbit;
					seekSector(start);
					continue;
				}
			}
		}
		start = skip(where().bitPos + MINLEADOUT * cellTicks / 1000);

		guard = 0;
		tolerance = 16;
		cbit = false;
	}

	for (int sec = 0; sec < NUMSECTOR; sec++) {
		secList_t *p = &track[sec];
		if (!p->flags)
			printf("%d/%d: No data\n", trk, sec);
		else if (debug == 0 && p->flags == 2)
			printf("%d/%d: failed to extract clean sector\n", trk, sec);
		else
			displaySector(p, trk, sec);
		removeSectors(p);
	}
	putchar('\n');

}