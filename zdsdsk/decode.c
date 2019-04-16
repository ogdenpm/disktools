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
	int minSyncCnt;
	int adaptRate;
} passOptions[] = { { 96, 128},  {96, 8}, {8, 128}, {8, 16}};


secList_t track[NUMSECTOR];
int badness(sector_t *p);

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
	if ((sec->sector & 0x7f) >= NUMSECTOR) {
		if (debug == VERBOSE) {
			printf("Bad sector\n");
			displaySector(sec, true);
		}
		return;
	}

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
		q->flags = 0;							// flags used later for corrupt record processing
		q->secData = *sec;
		p->flags = 2;
	}
}

bool crcCheck(const uint16_t *data, uint16_t size) {
#define CRC16 0x8005
#define INIT 0

	uint16_t crc = INIT;
	int bits_read = 0, bit_flag;

	/* Sanity check: */
	if (data == NULL)
		return 0;

	while (size > 2)
	{
		bit_flag = crc >> 15;

		/* Get next bit: */
		crc <<= 1;
		crc |= (*data >> (7 - bits_read)) & 1; // item a) work from the most significant bits

		/* Increment bit counter: */
		bits_read++;
		if (bits_read > 7)
		{
			bits_read = 0;
			data++;
			size--;
		}

		/* Cycle check: */
		if (bit_flag)
			crc ^= CRC16;

	}

	// item b) "push out" the last 16 bits
	int i;
	for (i = 0; i < 16; ++i) {
		bit_flag = crc >> 15;
		crc <<= 1;
		if (bit_flag)
			crc ^= CRC16;
	}

	return crc == ((data[0] & 0xff) * 256 + (data[1] & 0xff));      // check with recorded crc
}


int  adaptRate = 8;		// adjustment factor for clock change



int clock = NSCLOCK;	// used to keep track of nS clock sample count

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

		if (abs(val - 4 * NSCLOCK) <= NSCLOCK) {	// looks like a clock bit spacing
			syncCnt += 2;
			sampleCnt += val;
			pos = where();							// start of next bit
		} else if (abs(val - 2 * NSCLOCK) <= NSCLOCK && syncCnt > minSyncCnt) {
			clock = (sampleCnt / syncCnt + 1) / 2;
			seekSector(pos, 0);						// back to start of this bit
			return true;
		} else
			syncCnt = sampleCnt = 0;

	}
}



int getFMByte(int bcnt, int adaptRate)
{
	int cellVal[MAXFLUX];		// used to store the cell value for each flux transition
	int cellIdx = 0;			// if there are more then there are real problems
	static int initial = 0;	// only non 0 if last flux exceeded a byte boundary
	int val;
	int frame = 0;			// used to track frame size
	int result = 0;			// byte returned + (suspect features >> 8)

	if (!bcnt)
		initial = 0;

	if (initial) {
		val = initial;
		initial = 0;
	}  else if ((val = getNextFlux()) < 0)	// ran out of data
		return val;
	else
		val *= 1000; // scale to nS

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

	clock += ((frame + 31) / 32 - clock + adaptRate / 2) / adaptRate;	// tweak clock
	return result;
}



void displaySector(sector_t * sec, bool showSuspect)
{
	printf(showSuspect && sec->track > 255 ? "%d*" : "%d", sec->track & 0xff);
	printf(showSuspect && sec->sector > 255 ? "/%d*:" : "/%d:", sec->sector & 0x7f);
	if (showSuspect) {
		printf(" [%d%%]", ((138 - badness(sec)) * 100 + 138 / 2) / 138);
	}
	putchar('\n');

	for (int i = 0; i < SECSIZE; i += 16) {
		for (int j = i; j < i + 16; j++)
			printf("%02X%c ", sec->data[j] & 0xff, showSuspect && sec->data[j] > 255 ? '*' : ' ');

		for (int j = i; j < i + 16; j++) {
			int c = sec->data[j] & 0xff;
			printf("%c", (' ' <= c && c <= '~') ? c : '.');
		}
		putchar('\n');
	}

	printf(showSuspect && sec->ftrack > 255 ? "forward %d*" : "forward %d", sec->ftrack & 0xff);
	printf(showSuspect && sec->fsector > 255 ? "/%d*" : "/%d", sec->fsector & 0xff);
	printf(showSuspect && sec->btrack > 255 ? " backward %d*" : " backward %d", sec->btrack & 0xff);
	printf(showSuspect && sec->bsector > 255 ? "/%d*" : "/%d", sec->bsector & 0xff);
	printf(showSuspect && sec->crc1 > 255 ? " crc %02X*" : " crc %02X", sec->crc1 & 0xff);
	printf(showSuspect && sec->crc2 > 255 ? " %02X*" : " %02X", sec->crc2 & 0xff);
	printf(showSuspect && sec->post[0] > 255 ? " Postamble %02X*" : " Postamble %02X", sec->post[0] & 0xff);
	printf(showSuspect && sec->post[1] > 255 ? " %02X*" : " %02X", sec->post[1] & 0xff);
	printf("\n");
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

int badness(sector_t *p)
{
	int cnt = 0;
	for (int i = 0; i < SECMETA + SECSIZE + SECPOSTAMBLE; i++)
		if (p->raw[i] > 0xff)
			cnt++;
	return cnt;
}


void printBlockChain()
{
	for (blkList_t *p = head->next; p->next; p = p->next) {
		printf("%d %lu-%lu length %lu gap %lu\n", p->secId, p->block.start.bitPos, p->block.end.bitPos, p->block.end.bitPos - p->block.start.bitPos,
												  p->next->block.start.bitPos - p->block.end.bitPos);
	}
}

void flux2track()
{
	sector_t data;
	int dbyte;
	int rcnt;
	bool goodCRC;
	int trk = 99;
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
					if ((dbyte = getFMByte(rcnt, passOptions[pass].adaptRate)) < 0)
						break;
					else
						data.raw[rcnt] = dbyte;

				if (dbyte == BAD_FLUX)
					continue;
				else if (dbyte == END_FLUX)
					break;
				if (dbyte >= 0) {
					secId = data.sector & 0x7f;
					if (!(goodCRC = crcCheck(data.raw, SECSIZE + SECMETA)) || (data.sector & 0x7f) > NUMSECTOR) {		// save if it looks like an expected sector
						if (trk != 99 && (data.track & 0xff) == trk					// valid track
							&& (data.post[0] & 0xff) == 0 && (data.post[1] & 0xff) == 0) {			// has postamble
							int lowSecId = lastSecId();											// check secId is in expected range
							int highSecId = nextSecId();
							if ((lowSecId == -1 || lowSecId < secId || (lowSecId + NUMSECTOR - cntValid - 1) % NUMSECTOR > secId)
								&& (highSecId == -1 || secId < highSecId || (secId + NUMSECTOR - cntValid - 1) % NUMSECTOR > highSecId))
								addSector(&data, false);
						}
						continue;												// try a later resync
					}
					addSector(&data, goodCRC);
					if (trk != 99 && trk != (data.track & 0xff))
						logger(ALWAYS, "track different was %d now %d\n", trk, data.track & 0xff);
					trk = data.raw[1] & 0xff;
					blk.end = where();
					addBlock(secId, blk);
					if (secValid[secId] == 0) {
						secValid[secId] = pass + 1;
						if (++cntValid == NUMSECTOR)
							break;
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
	if (trk == 99)
		printf("could not identify track reliably\n");
	//	for (int i = 0; i < NUMSECTOR; i++)
	//		printf("%d", secValid[i]);
	for (int sec = 0; sec < NUMSECTOR; sec++) {
		secList_t *p = &track[sec];
		switch (p->flags) {
		case 0:
			printf("%d/%d: No data\n", trk, sec);
			break;
		case 1:
			putchar('\n');
			displaySector(&p->secData, false);
			break;
		case 2:
			if (debug >= MINIMAL) {
				printf("---%d/%d--------------- Corrupt Sector ---------------------\n", trk, sec);
				for (secList_t *q = p->next; q; q = q->next)
					displaySector(&q->secData, true);
				removeSectors(p);
				printf("--------------------- End Corrupt Sector--------------------\n\n");
			} else
				printf("%d/%d: failed to extract clean sector\n", trk, sec);
		}
	}
	putchar('\n');

}