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

unsigned inline getWord(int n)
{
	return inBuf[n] + (inBuf[n + 1] << 8);
}

unsigned long inline getDWord(int n)
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
static size_t lastFluxPos;


// seek to the specified position, setting flux and bit position information
bool seekSector(location_t pos, size_t lastPos)
{

		fluxPos = pos.fluxPos;
		bitPos = pos.bitPos;
		if (lastPos != 0)
			lastFluxPos = lastPos != ~0 ? lastPos : inSize;
		return fluxPos < lastFluxPos;
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
		case END_FLUX: printf("END_FLUX"); break;
		case END_BLOCK: printf("END_BLOCK"); break;
		default:
			printf("%d,", (n + USCLOCK)/(2 * USCLOCK));
			break;
		}
	if (n >= 0) {
		bitPos += n;
		return n * 1000;
	} else if (n == END_FLUX)
		fluxPos = bitPos = ~0;
	return n;
}





int getNextFlux() {
    int ovl16 = 0;
	int val;

	while (fluxPos < lastFluxPos) {
		val = inBuf[fluxPos];
		if (val <= 7) {
			if (++fluxPos >= lastFluxPos)
				return fluxLog(END_FLUX);
			return fluxLog(ovl16 + inBuf[fluxPos++]);
		}
		else if (val >= 0xe)
			return fluxLog(ovl16 + inBuf[fluxPos++]);
		else
			switch (val) {
			case 0xa:	if (++fluxPos >= lastFluxPos) return fluxLog(END_FLUX);
			case 0x9:	if (++fluxPos >= lastFluxPos) return fluxLog(END_FLUX);
			case 0x8:	if (++fluxPos >= lastFluxPos) return fluxLog(END_FLUX);
				break;
			case 0xb:	if (++fluxPos >= lastFluxPos) return fluxLog(END_FLUX);
				ovl16 += 0x10000;
				break;
			case 0xc:	if ((fluxPos += 3) >= lastFluxPos) return fluxLog(END_FLUX);
				return fluxLog(ovl16 + getWord(fluxPos - 2));
			case 0xd:
				if (fluxPos + 4 >= lastFluxPos) return fluxLog(END_FLUX);

				int type = inBuf[fluxPos + 1];
				int size = getWord(fluxPos + 2);
				fluxPos += 4;
				if (type == 0xd || fluxPos + size >= lastFluxPos)		// EOF?
					return fluxLog(END_FLUX);
				fluxPos += size;
				break;
			}
	}
	return fluxLog(END_FLUX);
}


