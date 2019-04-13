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

struct {
	int minSyncCnt;
	int adaptRate;
} passOptions[] = { { 96, 128}, {96, 64}, {96, 8}, {8, 128}, {8, 16} };


secList_t track[NUMSECTOR];


#define JITTER_ALLOWANCE        20

#define BYTES_PER_LINE  16      // for display of data dumps

void removeSectors(secList_t* p)
{
	secList_t* q, * s;
	for (q = p->next; q; q = s) {
		s = q->next;
		free(q);
	}
	p->flags = 0;
	p->next = NULL;
}

void addSector(sector_t* sec, bool goodCRC) {		// track and sector must be in range
	if ((sec->sector & 0x7f) >= NUMSECTOR) {
		if (debug == VERBOSE) {
			printf("Bad sector\n");
			displaySector(sec, true);
		}
		return;
	}

	secList_t* p = &track[sec->sector & 0x7f];
	secList_t *q;

	switch (p->flags) {
	case 2:
		if (goodCRC) {								// delete the failed sectors
			removeSectors(p);
			p->secData = *sec;
			p->flags = 1;
		}
		else {										// allocate another failed sector
			q = (secList_t*)malloc(sizeof(secList_t));
			q->next = p->next;
			p->next = q;
			q->secData = *sec;						// flags are currently irrelevant
		}
		return;
	case 0:											// unused
		p->secData = *sec;
		p->flags = goodCRC ? 1 : 2;					// fall through to return
	case 1:											// already have good
		return;
	}
}

bool crcCheck(const uint16_t * data, uint16_t size) {
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
		}
		else if (abs(val - 2 * NSCLOCK) <= NSCLOCK && syncCnt > minSyncCnt) {
			clock = (sampleCnt / syncCnt + 1) / 2;
			seekFlux(pos, 0);						// back to start of this bit
			return true;
		}
		else
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

	if (initial)
		val = initial;
	else if ((val = getNextFlux()) < 0)	// ran out of data
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



void displaySector(sector_t* sec, bool showSuspect)
{
	printf(showSuspect && sec->track > 255 ? "%d*" : "%d", sec->track & 0xff);
	printf(showSuspect && sec->sector > 255 ? "/%d*:\n" : "/%d:\n", sec->sector & 0x7f);

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
	printf("\n\n");
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
	location_t start;
	location_t end;

	memset(track, 0, sizeof(track));
	memset(secValid, 0, sizeof(secValid));


	for (int pass = 0; pass < sizeof(passOptions)/sizeof(passOptions[0]); pass++) {
		start.fluxPos = 0;
		start.bitPos = 0;
		printf("---- Pass %d\n", pass + 1);
		cntValid = 0;						// to see how each pass works
		attempt = 0;
		good = 0;
		while (1) {
			seekFlux(start, ~0);
			if (!syncFMByte(passOptions[pass].minSyncCnt))
				break;
			attempt++;
			start = where();		// where to restart if a problem
			memset(&data, 0, sizeof(data));

			for (rcnt = 0; rcnt < SECSIZE + SECMETA + SECPOSTAMBLE; rcnt++)
				if ((dbyte = getFMByte(rcnt, passOptions[pass].adaptRate)) < 0)
					break;
				else
					data.raw[rcnt] = dbyte;
			if (rcnt == SECSIZE + SECMETA + SECPOSTAMBLE) {
				if (goodCRC = crcCheck(data.raw, SECSIZE + SECMETA)) {
					good++;
					end = where();
					addSector(&data, goodCRC);
					int secId = data.raw[0] & 0x7f;
					if (trk == 99)
						trk = data.raw[1] & 0xff;
					printf("%d/%d - %d/%d @ %lu(%lu) - %lu(%lu) length %lu\n", attempt, good, trk, secId, start.bitPos, start.fluxPos, end.bitPos, end.fluxPos, end.bitPos - start.bitPos);
					if (secValid[secId] == 0) {
						secValid[secId] = pass;
						if (++cntValid == NUMSECTOR)
							printf("Success\n");

					}
				}
			}
		}
		printf("Score %d out of %d\n", good, attempt);
	}
	/* now display the results */
	if (trk == 99)
		printf("could not identify track reliably\n");
//	for (int i = 0; i < NUMSECTOR; i++)
//		printf("%d", secValid[i]);
	putchar('\n');
	for (int sec = 0; sec < NUMSECTOR; sec++) {
		secList_t* p = &track[sec];
		switch (p->flags) {
		case 0:
			printf("%d/%d: No data\n\n", trk, sec);
			break;
		case 1:
			displaySector(&p->secData, false);
			break;
		case 2:
			if (debug >= MINIMAL) {
				printf("---%d/%d--------------- Corrupt Sector ---------------------\n", trk, sec);
				displaySector(&p->secData, true);
				for (secList_t* q = p->next; q; q = q->next)
					displaySector(&q->secData, true);
				removeSectors(p);
				printf("--------------------- End Corrupt Sector--------------------\n\n");
			}
			else
				printf("%d/%d: failed to extract clean sector\n", trk, sec);
		}
	}

}