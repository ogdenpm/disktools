#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "flux.h"

#define HIST_MAX  (USCLOCK * 10)        // 10 uS max
#define HIST_MAX_D (USCLOCK_D * 10.0)




/*** input buffer management ***/
// varaibles used for input buffer management
static byte* inBuf;
static size_t inSize;
static size_t inIdx;


unsigned short inline getWord(size_t n)
{
	return inBuf[n] + (inBuf[n + 1] << 8);
}

unsigned long inline getDWord(size_t n)
{
	return inBuf[n] + (inBuf[n + 1] << 8) + (inBuf[n + 2] << 16) + (inBuf[n + 3] << 24);
}


/*
    The core flux processing functions
    and data definitions
*/




static size_t fluxRead;           // total number of flux transitions read
static size_t fluxPos;            // current flux transition
static size_t bitPos;		      // current bit position in data
static size_t inSize;


// seek to the specified position, setting flux and bit position information
void seekSector(location_t pos)
{
		fluxPos = pos.fluxPos;
		bitPos = pos.bitPos;
}

/* utility function to return current position in flux stream */
location_t where() {
	location_t pos = { fluxPos, bitPos };
	return pos;
}


// memory allocator with out of memory detection
void *xmalloc(size_t size) {
    void *p;
    if (p = malloc(size))
		return p;
    error("Out of memory\n");
}


/*
    The Flux read engine
*/

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

	if (debug) {
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
				//            addStreamInfo(streamPos, transferTime);
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

				//            addIndexBlock(streamPos, sampleCounter, indexCounter);
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

	fluxPos = bitPos = 0;			// rewind

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
				if ((c = oob(i)) < 0) {
					return;
				}
				i += c;
				break;
			}
	}
}


int fluxLog(int n)
{
	if (debug >= VERYVERBOSE)
		switch (n) {
		case END_FLUX: printf("END_FLUX");  break;
		case END_BLOCK: printf("END_BLOCK"); break;
		default:
			printf("%d,", n);
			break;
		}
	if (n > 0) {
		bitPos += n;
		return n * 1000;		// convert to NS resolution
	}
	if (n == END_FLUX)
		fluxPos = bitPos = ~0;
	return n;
}





int getNextFlux() {
    int ovl16 = 0;
	int val;

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
	location_t start = { 0, 0 };

    /* read all of the flux data */
    memset(histogram, 0, sizeof(histogram));

	seekSector(start);
	    while ((val = getNextFlux()) != END_FLUX && val != END_BLOCK) {
        if (val > maxHistVal)
            maxHistVal = val;
        if (val > HIST_MAX)
            outRange++;
        else if (++histogram[val] > maxHistCnt)
            maxHistCnt++;
    }

    if (maxHistCnt == 0) {
        logger(ALWAYS, "No histogram data\n");
        return;
    }

    for (int i = 0; i <= HIST_MAX; i++)
        cols[i] = (2 * levels * histogram[i] + maxHistCnt / 2) / maxHistCnt;

    int lowVal, highVal;
    for (lowVal = 0; lowVal < NSCLOCK / 2 && cols[lowVal] == 0; lowVal++)
        ;
    for (highVal = HIST_MAX; highVal >= NSCLOCK * 5 + NSCLOCK / 2 && cols[highVal] == 0; highVal--)
        ;

    printf("max flux value = %.1fus, flux values > %.1fus = %d\n", maxHistVal * 1000.0 / sck, HIST_MAX_D * 1000.0 / sck, outRange);
    printf("x axis range = %.1fus - %.1fus, y axis range 0 - %d\n", lowVal * 1000.0 / sck, highVal * 1000.0 / sck, maxHistCnt);
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
    for (int i = lowVal; i <= (highVal / NSCLOCK) * NSCLOCK; i++)
        if (i % USCLOCK == 0)
            i += printf("%dus", i / NSCLOCK) - 1;
        else
            putchar(' ');
    putchar('\n');
}
