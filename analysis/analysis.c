#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <limits.h>
#include <memory.h>

#define SCK 24027428.5714285            //  sampling clock frequency
#define ICK 3003428.5714285625

#define USCLOCK                 24           // 1 uS clock used for flux calculations
#define USCLOCK_D               (SCK / 1000000.0) // 1 uS clock floating point for timing to byte no
#define NSCLOCK					24027
#define CELLTICKS				(NSCLOCK * 2)
#define INDEX_GUARD		10000		// used to detect sectors split by start of track index



typedef struct _index {
	struct _index* next;
	uint32_t streamPos;
	uint32_t sampleCnt;
	uint32_t indexCnt;
	bool trackIndex;
} index_t;


index_t* indexHead = NULL;
index_t* indexTail = NULL;
uint8_t* fluxBuf;
uint8_t* inPtr;
uint8_t* endPtr;
int firstSector = 31;


typedef uint8_t byte;

void* xmalloc(size_t size) {
	void* ptr;
	if (!(ptr = malloc(size))) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
}

void addIndexEntry(uint32_t streamPos, uint32_t sampleCnt, uint32_t indexCnt) {
	index_t* newIndex = (index_t*)xmalloc(sizeof(index_t));
	newIndex->streamPos = streamPos;
	newIndex->sampleCnt = sampleCnt;
	newIndex->indexCnt = indexCnt;
	newIndex->trackIndex = false;
	newIndex->next = NULL;
	if (indexTail) {
		indexTail->next = newIndex;
		indexTail = newIndex;
	} else {
		indexHead = indexTail = newIndex;
	}

}




size_t getWord(FILE* fp) {
	int c = getc(fp);
	return (getc(fp) << 8) + c;
}

size_t getDWord(FILE* fp) {
	size_t c = getWord(fp);
	return (getWord(fp) << 16) + c;
}

int oob(FILE* fp) {
	int c = 0;
	int type = getc(fp);
	int size = getWord(fp);
	bool surpress = true;

	switch (type) {
	case 0xd:	return EOF;
	case 0:
		printf(" Invalid OOB %d - skipped\n", type);
		break;
	case 1:
	case 3:
		if (size != 8) {
			printf(" Bad OOB block type %d size %d - skipped\n", type, size);
			break;
		}

		unsigned streamPos = getDWord(fp);
		if (streamPos != (inPtr - fluxBuf)) {
			printf("streamPos mismatch, is %lu, expected %lu\n", (inPtr - fluxBuf), streamPos);
			surpress = false;
		}

		if (type == 1) {
			unsigned transferTime = getDWord(fp);

			if (!surpress) printf(" StreamInfo block, Stream Position = %lu, Transfer Time = %lu\n", streamPos, transferTime);
			//            addStreamInfo(streamPos, transferTime);
		} else {
			unsigned long resultCode = getDWord(fp);

			if (resultCode != 0)
				printf(" Read error %ld %s\n", resultCode, resultCode == 1 ? "Buffering problem" :
					resultCode == 2 ? "No index signal" : "Unknown error");
		}
		break;
	case 2:
		if (size != 12)
			printf(" Bad OOB block type %d size %d - skipped\n", type, size);
		else {
			uint32_t streamPos = getDWord(fp);
			uint32_t sampleCnt = getDWord(fp);
			uint32_t indexCnt = getDWord(fp);
			addIndexEntry(streamPos, sampleCnt, indexCnt);

		}
		break;
	case 4:
		printf(" KFInfo block:\n\t");
		for (int i = 0; i < size; i++) {
			if (c = getc(fp))
				putchar(c);
			else
				printf("\n\t");
		}
		putchar('\n');
		break;

	default:
		printf(" Unknown OOB block type = %d\n", type);
		break;
	}
	return 3 + size;
}

/*
	Open the flux stream and extract the oob data to locate stream & index info
*/
void loadFlux(FILE* fp) {
	size_t fileSize;
	fseek(fp, 0L, SEEK_END);
	fileSize = ftell(fp);
	fseek(fp, 0L, SEEK_SET);

	inPtr = fluxBuf = (uint8_t*)xmalloc(fileSize);



	int c, d;
	while ((c = getc(fp)) != EOF) {
		if (c == 0xd) {
			if ((c = oob(fp)) == EOF)
				break;
		} else {
			*inPtr++ = c;
			if (c != 8 && c != 0xb && c < 0xe) {
				if ((d = getc(fp)) == EOF)
					break;
				*inPtr++ = d;
				if (c == 0xA || c == 0xC) {
					if ((d = getc(fp)) == EOF)
						break;
					*inPtr++ = d;
				}


			}
		}

	}
	endPtr = inPtr;
	// convert indexCnt to reflect sector length
	for (index_t* p = indexHead; p && p->next; p = p->next) {
		p->indexCnt = p->next->indexCnt - p->indexCnt;	// as unsigned should also work with numeric wrap round
	}
	// check for sectors interrupted by start of track index hole
	// merge the sector into one and set the track index length to 0

	bool seenIndex = false;
	for (index_t* p = indexHead; p && p->next; p = p->next) {
		if (p->indexCnt < INDEX_GUARD && p->next->indexCnt < INDEX_GUARD) {
			p->indexCnt += p->next->indexCnt;
			p->next->indexCnt = 0;
			p = p->next;				// will cause track index hole to be skipped
			seenIndex = true;
		} else if (!seenIndex)
			firstSector--;
	}
}


void unloadFlux() 	{
	free(fluxBuf);
	for (index_t* q, *p = indexHead; p; p = q) {
		q = p->next;
		free(p);
	}
}

size_t streamPos = 0;
size_t streamEnd = 0;
index_t* p;

int getNextFlux() {

	uint32_t c;
	uint8_t type;

	int ovl16 = 0;

	while (streamPos < streamEnd) {
		type = fluxBuf[streamPos++];

		assert(type != 0xd);
		if (type <= 7 || type == 0xC || type >= 0xe) {
			if (type >= 0xe)
				c = type;
			else if (type <= 7)
				c = (type << 8) + fluxBuf[streamPos++];
			else {
				c = fluxBuf[streamPos++] << 8;
				c += fluxBuf[streamPos++];
			}
			c += ovl16;
			ovl16 = 0;
			return c * 1000;


		} else if (type == 0xb)
			ovl16 += 0x10000;
		else
			inPtr += (type - 7);
	}
	return -1;


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


void resetPLL(size_t start, size_t end, size_t adjust, int percent) {
	if (start && fluxBuf[start] == 0xd && fluxBuf[start - 1] == 0)
		start--;
	streamPos = start;
	streamEnd = end;

	int fluxVal = getNextFlux();
	if (fluxVal >= 0) {
		ctime = fluxVal - adjust;
		cellTicks = CELLTICKS;
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
	int sector = firstSector;

	for (index_t* p = indexHead; p && p->next; p = p->next) {
		if (p->indexCnt > INDEX_GUARD) {
			if (p->next->indexCnt != 0) {
				resetPLL(p->streamPos, p->next->streamPos, p->sampleCnt, 10);
			} else if (p->next->next)
				resetPLL(p->streamPos, p->next->next->streamPos, p->sampleCnt, 10);
			else
				break;

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
		} else if (p->indexCnt == 0)
			printf("Index Marker\n");
	}
}

void  main(int argc, char** argv) {
	FILE* fp;
	if (argc != 2 || (fp = fopen(argv[1], "rb")) == NULL) {
		printf("usage: analysis fluxfile\n");
	} else {
		loadFlux(fp);
		fclose(fp);
		readFlux();
		unloadFlux();
	}

}