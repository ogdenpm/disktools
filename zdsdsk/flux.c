#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "flux.h"

#define HIST_MAX  (USCLOCK * 10)        // 10 uS max

// private definitions
enum {
    OVL16 = -2, PAD = -3
};

#define FLUXCHUNKSIZE 100000UL        // default chunk size for flux stream
#define ARRAYCHUNKSIZE 250            // default chunk size for stream and index info


/* private types for managing the data from the flux stream */
typedef struct _flux {
    struct _flux *link;
    word blkId;
    int cnt;
    int flux[FLUXCHUNKSIZE];
} flux_t;

typedef struct {
    unsigned long streamPos;
    unsigned long sampleCnt;
    unsigned long indexCnt;
} index_t;

typedef struct {
    long streamPos;
    long transferTime;
} stream_t;

/*** input buffer management ***/
// varaibles used for input buffer management
static byte* inBuf;
static size_t inSize;
static size_t inIdx;

void fixupFlux();
static int savedFlux = 0;


// get next byte from input
int get1() {
    if (inIdx < inSize)
        return inBuf[inIdx++];
    return EOF;
}

// read in 2 byte little edian word
unsigned int get2() {
    int cl;
    int ch;

    if ((cl = get1()) == EOF || (ch = get1()) == EOF)
        return EOF;
    return (ch << 8) + cl;
}

unsigned inline getWord(int n)
{
	return inBuf[n] + (inBuf[n + 1] << 8);
}

unsigned long inline getDWord(int n)
{
	return inBuf[n] + (inBuf[n + 1] << 8) + (inBuf[n + 2] << 16) + (inBuf[n + 3] << 24);
}

// read in 4 byte little edian word
unsigned long get4() {
    int cl;
    int ch;

    if ((cl = get2()) == EOF || (ch = get2()) == EOF)
        return EOF;
    return (ch << 16) + cl;
}

// check at least given number of bytes left

bool atLeast(int size) {
    return inIdx + size < inSize;
}

size_t inPos() {
    return inIdx;
}

// skip input bytes
void skip(int size) {       // skip forward size bytes or if size < 0 skip to end of input
    if (size >= 0)
        inIdx += size;
    else
        inIdx = inSize;
}

/*
    The core flux processing functions
    and data definitions
*/

static flux_t *fluxData;        // the list of flux data blocks
static flux_t *curBlk;          // current block being processed
static index_t *indexArray;     // the array of info block
static int indexArrayCnt;       // the count of index info records
static int indexArraySize;      // the current size of indexArray
static stream_t *streamArray;   // stream info block equivalent of above
static int streamArrayCnt;
static int streamArraySize;

static size_t fluxRead;           // total number of flux transitions read
static size_t fluxPos;            // current flux transition

static size_t endTrack;         // flux index for end of track

// seeks to the start of a block and returns the sck clock count for the track
// or 0 if no block or seek error

void seekFlux(size_t pos)
{
	if (pos < inSize)
		fluxPos = pos;
	savedFlux = 0;
}

int seekBlock(int blk) {
    if (blk < 0 || blk >= indexArrayCnt - 1)
        return 0;
    seekFlux(indexArray[blk].streamPos);
    endTrack = indexArray[blk + 1].streamPos;
#pragma warning(suppress: 26451)
    return (int)((indexArray[blk + 1].indexCnt - indexArray[blk].indexCnt) / ICK * SCK);
}



/* utility function to return current position in flux stream */
size_t where() {
    return fluxPos;
}


// memory allocator with out of memory detection
void *xalloc(void *blk, size_t size) {
    void *p;
    if (blk == NULL) {          // no pre-existing memory
        if (p = malloc(size))
            return p;
    }
    else
        if (p = realloc(blk, size))
            return p;
    error("Out of memory\n");
}

// free up all memory - not realy needed as system will clear
void freeMem() {
    flux_t *p, *q;

    for (p = fluxData; p; p = q) {
        q = p->link;
        free(p);
    }
    free(streamArray);
    free(indexArray);
}

/*
    The Flux read engine
*/
// reset the flux machine for the next track

void resetFlux() {
    indexArrayCnt = 0;
    streamArrayCnt = 0;
}


// add another stream Info entry allocating more memory if needed
void addStreamInfo(long streamPos, long transferTime) {
    if (streamArrayCnt == streamArraySize) {
        streamArray = (stream_t *)xalloc(streamArray, (streamArraySize + ARRAYCHUNKSIZE) * sizeof(stream_t));
        streamArraySize += ARRAYCHUNKSIZE;
    }
    stream_t *p = &streamArray[streamArrayCnt++];
    p->streamPos = streamPos;
    p->transferTime = transferTime;
}

// add another index block entry allocating more memory if needed
void addIndexBlock(unsigned long streamPos, unsigned long sampleCnt, unsigned long indexCnt) {
	index_t* p;

	/* junk index hole */
	if (indexArrayCnt >= 2 && indexCnt - indexArray[indexArrayCnt - 1].indexCnt < 10000
		&& indexArray[indexArrayCnt - 1].indexCnt - indexArray[indexArrayCnt - 2].indexCnt < 10000) {
		p = &indexArray[indexArrayCnt - 1];
		p->streamPos = streamPos;
		p->indexCnt = indexCnt;
		p->sampleCnt += sampleCnt;
	}
	else {
		if (indexArrayCnt == indexArraySize) {
			indexArray = (index_t*)xalloc(indexArray, (indexArraySize + ARRAYCHUNKSIZE) * sizeof(index_t));
			indexArraySize += ARRAYCHUNKSIZE;
		}

		p = &indexArray[indexArrayCnt++];
		p->streamPos = streamPos;
		p->sampleCnt = sampleCnt;
		p->indexCnt = indexCnt;
	}
}

// handle flux Out Of Band info
int oob(size_t pos) {
	if (pos + 4 >= inSize) {
		logger(VERBOSE, "EOF in OOB block\n");
		return END_FLUX;
	}
	int type = inBuf[pos + 1];
	int size = getWord(pos + 2);
	pos += 4;
	if (type == 0xd) {		// EOF?
		inSize = pos;	// update to reflect real end
		return END_FLUX;
	}
	if (pos + size >= inSize) {
		logger(VERBOSE, "EOF in OOB block\n");
		return END_FLUX;
	}

    switch (type) {
    case 0:
        logger(MINIMAL, "Invaild OOB - skipped\n");
		break;
    case 1:
    case 3:
        if (size != 8) {
            logger(MINIMAL, "bad OOB block type %d size %d - skipped\n", type, size);
			break;
        }

		unsigned streamPos = getDWord(pos);

        if (type == 1) {
            unsigned transferTime = getDWord(pos + 4);

            logger(VERBOSE, "StreamInfo block, Stream Position = %lu, Transfer Time = %lu\n", streamPos, transferTime);
            addStreamInfo(streamPos, transferTime);
        }
        else {
			unsigned long resultCode = getDWord(pos + 4);

            if (resultCode != 0)
                logger(ALWAYS, "Read error %ld %s\n", resultCode, resultCode == 1 ? "Buffering problem" :
                    resultCode == 2 ? "No index signal" : "Unknown error");
        }
		break;
    case 2:
        if (size != 12)
            logger(MINIMAL, "bad OOB block type %d size %d - skipped\n", type, size);
        else {
            unsigned streamPos = getDWord(pos);
            unsigned sampleCounter = getDWord(pos + 4);
            unsigned indexCounter = getDWord(pos + 8);

            logger(VERBOSE, "Index block, Stream Position = %lu, Sample Counter = %lu, Index Counter = %lu\n", streamPos, sampleCounter, indexCounter);

            addIndexBlock(streamPos, sampleCounter, indexCounter);
        }
        break;
    case 4:
        logger(VERBOSE, "KFInfo block:\n");
        if (debug >= VERBOSE) {
			for (int i = 0; i < size; i++)
				putchar(inBuf[pos + i] ? inBuf[pos + i] : '\n');
        }
        break;

    default:
        logger(MINIMAL, "unknown OOB block type = %d @ %lX\n", type, pos);
    }
	return 4 + size;
}

/*
    Open the flux stream and extract the oob data to locate stream & index info
*/
void openFluxBuffer(byte* buf, size_t size) {
	if (inBuf)
		free(inBuf);
	inBuf = buf;
	inSize = size;
	if (!inBuf)
		return;
	resetFlux();
	addIndexBlock(0, 0, 0);         // mark start of all data

	for (size_t i = 0; i < inSize;) {
		int c = inBuf[i];
		if (c <= 7)
			i += 2;
		else if (c >= 0xe)
			i++;
		else
			switch (c) {
			case 0xa:	i++;
			case 0x9:	i++;
			case 0x8:	i++; break;
			case 0xb:	i++;  break;
			case 0xc:	i += 3;	 break;
			case 0xd:
				if ((c = oob(i)) < 0)
					return;
				i += c;
				break;
			}
	}
}


int fluxLog(int n)
{
	if (debug >= VERYVERBOSE)
		switch (n) {
		case END_FLUX: printf("END_FLUX"); break;
		case END_BLOCK: printf("END_BLOCK"); break;
		default:
//			printf("%d,", n);
			printf("%d,", (n + USCLOCK)/(2 * USCLOCK));
			break;
		}
	return n;
}



void ungetFlux(int val)
{
	savedFlux = val;
}



int getNextFlux() {
    int ovl16 = 0;
	int val;

	if (savedFlux) {
		val = savedFlux;
		savedFlux = 0;
		return fluxLog(val);
	}

	while (fluxPos < inSize) {
		val = inBuf[fluxPos];
		if (val <= 7) {
			if (++fluxPos >= inSize)
				return fluxLog(END_FLUX);
			return fluxLog(ovl16 + inBuf[fluxPos++]);
		}
		else if (val >= 0xe)
			return fluxLog(ovl16 + inBuf[fluxPos++]);
		else
			switch (val) {
			case 0xa:	if (++fluxPos >= inSize) return fluxLog(END_FLUX);
			case 0x9:	if (++fluxPos >= inSize) return fluxLog(END_FLUX);
			case 0x8:	if (++fluxPos >= inSize) return fluxLog(END_FLUX);
				break;
			case 0xb:	if (++fluxPos >= inSize) return fluxLog(END_FLUX);
				ovl16 += 0x10000;
				break;
			case 0xc:	if ((fluxPos += 3) >= inSize) return fluxLog(END_FLUX);
				return fluxLog(ovl16 + getWord(fluxPos - 2));
			case 0xd:
				if (fluxPos + 4 >= inSize) return fluxLog(END_FLUX);

				int type = inBuf[fluxPos + 1];
				int size = getWord(fluxPos + 2);
				fluxPos += 4;
				if (type == 0xd || fluxPos + size >= inSize)		// EOF?
					return fluxLog(END_FLUX);
				fluxPos += size;
				break;
			}
	}
	return fluxLog(END_FLUX);
}

void displayHist(int levels)
{
    int histogram[HIST_MAX + 1];
    int cols[HIST_MAX + 1];
    char line[HIST_MAX + 2];        // allow for 0 at end
    int val;
    int blk = 1;
    int maxHistCnt = 0;
    int maxHistVal = 0;
    int outRange = 0;
    double sck = 24027428.5714285;

    /* read all of the flux data */
    memset(histogram, 0, sizeof(histogram));

    while (seekBlock(blk++)) {
        while ((val = getNextFlux()) != END_FLUX && val != END_BLOCK) {
            if (val > maxHistVal)
                maxHistVal = val;
            if (val > HIST_MAX)
                outRange++;
            else if (++histogram[val] > maxHistCnt)
                maxHistCnt++;
        }
    }
    if (maxHistCnt == 0) {
        logger(ALWAYS, "No histogram data\n");
        return;
    }

    for (int i = 0; i <= HIST_MAX; i++)
        cols[i] = (2 * levels * histogram[i] + maxHistCnt / 2) / maxHistCnt;

    int lowVal, highVal;
    for (lowVal = 0; lowVal < USCLOCK / 2 && cols[lowVal] == 0; lowVal++)
        ;
    for (highVal = HIST_MAX; highVal >= USCLOCK * 5 + USCLOCK / 2 && cols[highVal] == 0; highVal--)
        ;

    printf("max flux value = %.1fus, flux values > %.1fus = %d\n", maxHistVal * 1000000.0 / sck, HIST_MAX * 1000000.0 / sck, outRange);
    printf("x axis range = %.1fus - %.1fus, y axis range 0 - %d\n", lowVal * 1000000.0 / sck, highVal * 1000000.0 / sck, maxHistCnt);
    puts("\nHistogram\n"
        "---------");
    int fillch;
    for (int i = levels * 2; i > 0; i -= 2) {
        line[highVal + 1] = fillch = 0;
        for (int j = highVal; j >= lowVal; j--) {
            if (cols[j] >= i - 1) {
                line[j] = cols[j] >= i ? '#' : '+';
                fillch = ' ';
            }
            else
                line[j] = fillch;
        }
        puts(line + lowVal);
    }
    for (int i = lowVal; i <= highVal; i++)
        putchar('-');
    putchar('\n');
    for (int i = lowVal; i <= (highVal / USCLOCK) * USCLOCK; i++)
        if (i % USCLOCK == 0)
            i += printf("%dus", i / USCLOCK) - 1;
        else
            putchar(' ');
    putchar('\n');
}

void forceFixupFlux(int blk)
{
	//indexArray[blk + 1].streamPos = where();
}

void fixupFlux()
{
	int saveDebug = debug;		// disable debug whilst probing for real starts
	debug = 0;
	for (int blk = 1; blk < indexArrayCnt; blk++) {

	}
	for (int blk = 0; seekBlock(blk); blk++) {
#ifdef OPTION1
		endTrack = indexArray[blk].streamPos;			// look at 100 samples before registered start
		fluxSeek(endTrack > 1000 ? endTrack - 1000 : 0);	// to check if start occured earlier
		unsigned long start;
		int run = 0, c;
		endTrack += 100;
		while ((c = getNextFlux()) > 0) {
			if (abs(c - 4 * USCLOCK) < USCLOCK) {
				if (run++ == 0) {
					start = where();
				}
				else if (run > 20)
					break;
			}
			else
				run = 0;
		}
		if (c > 0) {
			indexArray[blk].streamPos = start;
			logger(VERYVERBOSE, "blk %d moved from %u to %u\n", blk, endTrack - 100, start);
		}
		else {
			logger(VERYVERBOSE, "blk %d not moved\n", blk);
		}
#else
		unsigned long maxLoc, start;
		maxLoc = start = where();
		int maxFlux = 0, c;
		int sectorTime = 0;
		while ((c = getNextFlux()) >= 0) {
			sectorTime += c;
			if (blk == 0 || sectorTime > (8 + 138) * 32 * USCLOCK) {
				if (c >= maxFlux) {
					maxFlux = c;
					maxLoc = where();
				}
			}
		}
		if (maxLoc > start)
			indexArray[blk + 1].streamPos = maxLoc;

#endif

	}
	debug = saveDebug;
}