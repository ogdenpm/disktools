#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <ctype.h>
#include <stdint.h>
#include "flux.h"

#define MAXFLUX			32	// max flux transitions in a byte

#define	SUSPECT			0x100

#define RESYNCTIME	100
#define SYNCCNT	32
#define SECTORBITS	105000		// minimum sector size
#define MAXMISSING	8		// max missing sectors to allow for for sector Id check
struct {
	int minSyncCnt;			// min number of 0 bits to sync up on
	int adaptRate;			// number of bytes to average over for clock delta
	int sesnsitivity;		// fraction of clock delta applied
} passOptions[] = {{ 96, 16, 32},	// long sync stable adaption - gets most good sectors
				   {96, 1, 128},	// long sync slow  resync every byte
				   { 96, 1, 8},		// reasonable long sync agressive adapt
				   {8, 1, 128},		// short sync slow adapt
				   {8, 1, 16},		// short sync medium adapt
				   {8, 1, 1}		// last throw of the dice, short adapt agressive adapt
					};		// short sync agressive adapt every byte	


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


	secList_t *p = &track[sec->sector & 0x7f];
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

int  adaptRate = 8;		// adjustment factor for clock change



int clock = NSCLOCK;	// used to keep track of nS clock sample count
#define FIRSTADJUST	32
bool syncFMByte(int minSyncCnt)
{
	int syncCnt = 0;	// count of clock samples for resync
	int sampleCnt = 0;	// count of clock bit only ticks for resync
	int glitch = 0;
	int val;
	location_t pos = where();

	clock = NSCLOCK;

	while (1) {
		if ((val = getNextFlux()) < 0)	// ran out of data
			return false;
		val *= 1000;	// scale up to avoid fp calculations

		if (abs(val - 4 * clock) <= clock) {	// looks like a clock bit spacing
			sampleCnt += val;
#if 1
			syncCnt += 2;
#else
			if ((syncCnt += 2) == FIRSTADJUST) {
				clock = (sampleCnt + FIRSTADJUST) / (FIRSTADJUST * 2);
//				printf("clock adjust: %d\n", clock);
			}
#endif
			pos = where();							// start of next bit
		} else if (abs(val - 2 * clock) <= clock && syncCnt > minSyncCnt) {
//			printf("clock = %d", clock);
			clock = (sampleCnt + syncCnt) / (syncCnt * 2);
//			printf(" - new clock: %d\n", clock);
//			printf("syncCnt = %d\n", syncCnt);
			seekSector(pos, 0);						// back to start of this bit
			return true;
		} else
			syncCnt = sampleCnt = 0;

	}
}



int getFMByte(int bcnt, int adaptRate, int sensitivity)
{
	int cellVal[MAXFLUX];		// used to store the cell value for each flux transition
	int cellIdx = 0;			// if there are more then there are real problems
	static int initial = 0;	// only non 0 if last flux exceeded a byte boundary
	static int bitPos, byteCnt;
	int val;
	int frame = 0;			// used to track frame size
	int result = 0;			// byte returned + (suspect features >> 8)

	if (!bcnt) {
		initial = 0;
		bitPos = where().bitPos;
		byteCnt = 0;
	}
	if (initial) {
		val = initial;
		initial = 0;
	} else {
		if (byteCnt >= adaptRate) {
			int nsDelta = (where().bitPos - bitPos) * 1000;
			clock += ((nsDelta + 16 * byteCnt) / (32 * byteCnt) - clock + sensitivity / 2) / sensitivity;
//			printf("clock adapt = %d\n", clock);
			bitPos = where().bitPos;
			byteCnt = 0;
		}
		if ((val = getNextFlux()) < 0)	// ran out of data
			return val;
		else
			val *= 1000; // scale to ns
	}
	byteCnt++;
	while (1) {
		if (val > 512000) {
			return BAD_FLUX;
		}
		if (abs(val - 2 * clock) > clock / 2 && abs(val - 4 * clock) > clock / 2)	// suspect if > .5uS jitter
			result++;
		if (frame + val >= 31 * clock)
			break;
		frame += val;
		if (cellIdx < MAXFLUX)
			cellVal[cellIdx++] = (frame + clock) / (2 * clock);
		if ((val = getNextFlux()) < 0)
			return val;
		val *= 1000;
	}

	if (frame + val > 33 * clock) {		// overrun so limit to last clock pulse
		result++;
		while (frame + val > 33 * clock) {
			initial += 2 * clock;
			val -= 2 * clock;
		}
	}

	frame += val;						// add in last clock bit
	if (cellIdx < MAXFLUX)
		cellVal[cellIdx++] = (frame + clock) / (2 * clock);

	int bitStream = 0;
	for (int i = 0; i < cellIdx; i++) {
		if (bitStream & (1 << (cellVal[i] - 1)))							// duplicate pluse in a single cell
			result++;
		else
			bitStream |= (1 << (cellVal[i] - 1));
	}
	if ((bitStream & 0xAAAA) != 0xAAAA)							// missing clock bit
		result++;

	for (int i = 0; i < 16; i += 2, bitStream >>= 2)				// high byte will be number of suspect features
		result = (result << 1) | (bitStream & 1);

//	clock += ((frame + 31) / 32 - clock + adaptRate / 2) / adaptRate;	// tweak clock
	return result;
}

void printDataLine(uint16_t *p, int marker)
{
	if (marker)
		putchar(marker);
	for (int j = 0; j < 16; j++)
		printf("%02X%c ", p[j] & 0xff, marker && p[j] > 255 ? '*' : ' ');

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
	printf(marker && p[3] > 255 ? "forward %d*" : "forward %d", p[3] & 0xff);
	printf(marker && p[2] > 255 ? "/%d*" : "/%d", p[2] & 0xff);
	printf(marker && p[1] > 255 ? " backward %d*" : " backward %d", p[1] & 0xff);
	printf(marker && p[0] > 255 ? "/%d*" : "/%d", p[0] & 0xff);
	printf(marker && p[4] > 255 ? " crc %02X*" : " crc %02X", p[4] & 0xff);
	printf(marker && p[5] > 255 ? " %02X*" : " %02X", p[5] & 0xff);
	printf((marker || (p[6] & 0xff)) && p[6] > 255 ? " Postamble %02X*" : " Postamble %02X", p[6] & 0xff);
	printf((marker || (p[7] & 0xff)) && p[7] > 255 ? " %02X*\n" : " %02X\n", p[7] & 0xff);

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
			if ((line[j] & 0xff) != (q->secData.raw[2 + i + j] & 0xff))
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



/* block management */
blkList_t endBlk = { NULL, {{~0, ~0}, {~0, ~0}}, -1 };		// sentinals for linked list
blkList_t startBlk = { &endBlk, {{0,0},{0,0}}, -1 };
blkList_t *head = &startBlk;

blkList_t *curBlk = &startBlk;

blkList_t *addBlock(int secId, blk_t blk)
{
	blkList_t *p = (blkList_t *)xmalloc(sizeof(blkList_t));
	p->secId = secId;
	p->block = blk;


	blkList_t *q;

	for (q = head; q->next->block.end.fluxPos < blk.start.fluxPos; q = q->next)
		;
	p->next = q->next;
//	printf("sector %d fluxpos %lu - %lu bitpos %lu - %lu\n", secId, blk.start.fluxPos, blk.end.fluxPos, blk.start.bitPos, blk.end.bitPos);
	return curBlk = q->next = p;
}


void blkFree()
{
	blkList_t *p, *q;
	for (p = startBlk.next; p != &endBlk; p = q) {
		q = p->next;
		free(p);
	}
	head->next = &endBlk;
	curBlk = head;
}

blk_t nextBlk(blk_t blk)
{
	blk_t newBlk;

	if (blk.start.bitPos < curBlk->block.start.bitPos)	
		curBlk = head;
	while (curBlk->next && blk.start.bitPos > curBlk->next->block.start.bitPos)		// find starting block
		curBlk = curBlk->next;
	for (; curBlk->next; curBlk = curBlk->next)
		if (blk.start.bitPos + SECTORBITS >= curBlk->next->block.start.bitPos)
			blk.start = curBlk->next->block.end;
		else {
			newBlk.start = blk.start;
			newBlk.end = curBlk->next->block.start;
			return newBlk;
		}
	newBlk.start = endBlk.block.start;
	newBlk.end = endBlk.block.end;
	return newBlk;
}

int lastSecId()
{
	return curBlk->secId;
}

int nextSecId()
{
	if (curBlk->next)
		return curBlk->next->secId;
	return -1;
}


void printBlockChain()
{
	for (blkList_t *p = head->next; p->next; p = p->next) {
		printf("%d %lu-%lu length %lu gap %lu\n", p->secId, p->block.start.bitPos, p->block.end.bitPos, p->block.end.bitPos - p->block.start.bitPos,
												  p->next->block.start.bitPos - p->block.end.bitPos);
	}
}


bool isValidRef(int t, int s)
{
	t &= 0xff;
	s &= 0xff;
	return ((0 <= t && t < 77) || t == 255) && ((0 <= s && s < NUMSECTOR) || s == 255);
}

void flux2track(int trk)
{
	sector_t data;
	int dbyte;
	int rcnt;
	int secValid[NUMSECTOR];	// holds the pass on which valid sector detected
	int cntValid = 0;			// number of unique valid sectors
	int attempt = 0;
	int good = 0;
	blk_t blk;
	int secId;

	memset(track, 0, sizeof(track));
	memset(secValid, 0, sizeof(secValid));

	cntValid = 0;
	for (int pass = 0; pass < sizeof(passOptions) / sizeof(passOptions[0]) && cntValid != NUMSECTOR; pass++) {
		blk = nextBlk(startBlk.block);
		while (seekSector(blk.start, blk.end.fluxPos)) {
			if (syncFMByte(passOptions[pass].minSyncCnt)) {		// if we could sync check if sector data is ok
				blk.start = where();		// where to restart if a problem
				for (rcnt = 0; rcnt < SECSIZE + SECMETA + SECPOSTAMBLE; rcnt++)
					if ((dbyte = getFMByte(rcnt, passOptions[pass].adaptRate, passOptions[pass].sesnsitivity)) < 0)
						break;
					else
						data.raw[rcnt] = dbyte;

				if (dbyte == BAD_FLUX)
					continue;
				else if (dbyte == END_FLUX)
					break;
				if (dbyte >= 0) {
					/* ignore sectors and tracks out of range*/
					if ((secId = data.sector & 0x7f) >= NUMSECTOR || (data.track & 0xff) != trk)	// start error
						continue;
//					printf("track %d sector %d\n", trk, secId);
					if (crcCheck(data.raw, SECSIZE + SECMETA)) {		// data looks good
						addSector(&data, true);
						blk.end = where();
						addBlock(secId, blk);
						if (secValid[secId] == 0) {
							secValid[secId] = pass + 1;
							if (++cntValid == NUMSECTOR)
								break;
						}
					} else {
						if ((data.post[0] & 0xff) == 0 || (data.post[1] & 0xff) == 0)			// looks like some level of sync
							addSector(&data, false);
						continue;												// try a later resync
					}
				}
			}

			blk.start = blk.end;
			blk = nextBlk(blk);
		}
//		printf("At end of pass %d: trk=%d\n", pass + 1, trk);
//		printBlockChain();
		//		printf("pass %d score %d good %d looked at\n", pass + 1, good, attempt);
	}
	blkFree();
	/* now display the results */
	//	for (int i = 0; i < NUMSECTOR; i++)
	//		printf("%d", secValid[i]);
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