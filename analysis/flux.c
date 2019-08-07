#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "flux.h"

/* flux stream types */
enum streamType_t {
    FLUX2 = 7, 		// 0-7
    NOP1, NOP2, NOP3,
    OVL16, FLUX3, OOB, FLUX1	// 0xe-0xff
};

enum oob_t {
    OOB_INVALID = 0, OOB_STREAMINFO, OOB_INDEX, OOB_STREAMEND, OOB_KFINFO, OOB_EOF = 0xd
};


static uint8_t* fluxBuf;		// buffer where flux data / stream data held
static size_t streamIdx;		// current position of stream data with OOB data removed


uint8_t* inPtr;
uint8_t* endPtr;
int firstSector = 31;



/* parsed data from the kinfo blocks*/
static int hc = 0;			// hard sector count or 0 for soft sector
double sck = SCK;			// sample clock frequency scaled
static double fluxScaler = 1.0e9 / SCK;
static double ick = ICK;	// index clock frequency
static char scanDate[11];	// date on which data was scanned
static char scanTime[9];	// and the time

// used to record the phyisical blocks of data between index markers
// for soft sectors the data before the first and after the last marker
//	will be kept and all phySector values will be 0
// for hard sectors only whole sectors are kept. The track index marker
//	is removed and phySector is set to sector number after track index
//	marker starting at 0


typedef struct _physBlock {
    struct _physBlock* next;			// chains list of information
    uint32_t start;						// sample count of first sample or 0 for  first soft sector
    uint32_t end;						// sample count of last sample, usually start of next block
    uint32_t sampleCnt;					// offset in first sample of index hole start
    uint32_t indexCnt;					// and index clock of the marker, 0 for first soft sector
    uint8_t physSector;					// physical sector number always 0 for soft sector
} physBlock_t;



static physBlock_t* indexHead = NULL;			// list start
static physBlock_t* indexPtr = NULL;			// current item being processed, for load this is the end
static unsigned blkNumber = 0;					// current block number
static uint32_t totalSampleCnt = 0;             // running sample count to remove rouding bias
static uint64_t prevNsCnt = 0;                  // previous running ns count

static void rewindBlock();

static void startIndex() {
    for (physBlock_t* q, *p = indexHead; p; p = q) {
        q = p->next;
        free(p);
    }
    blkNumber = 0;
    indexHead = indexPtr = (physBlock_t*)xmalloc(sizeof(physBlock_t));
    indexPtr->start = indexPtr->end = indexPtr->sampleCnt = indexPtr->indexCnt = indexPtr->physSector = 0;
    indexPtr->next = NULL;
}




// assumes startIndex has been called
static void addIndexEntry(uint32_t streamPos, uint32_t sampleCnt, uint32_t indexCnt) {
    physBlock_t* newIndex = (physBlock_t*)xmalloc(sizeof(physBlock_t));
    newIndex->start = newIndex->end = streamPos;
    newIndex->sampleCnt = sampleCnt;
    newIndex->indexCnt = indexCnt;
    newIndex->physSector = 0;
    newIndex->next = NULL;
    indexPtr->next = newIndex;
    indexPtr->end = streamPos;
    indexPtr = newIndex;
}


// assumes start head has been called and possibly several addIndexEntry calls
static void endIndex(uint32_t streamPos) {

    indexPtr->end = streamPos;
    if (hc) {						// for hard sectors merge blocks split by track index mark
                                // set indexCnt to 0 for track index mark, leadin and trailing data blocks
        // calculate nominal sector length  in index clock cycles and assume full sector is > 0.75 * nominal
        unsigned minSectorIck = (unsigned)(((hc == HS8INCH) ? (ick / RPS8INCH / hc) : (ick / RPS5INCH / hc)) * 0.75);
        bool seenTrackIndex = false;
        int preTrackIndexSecCnt = 0;
        physBlock_t* p;

        for (p = indexHead; p && p->next; p = p->next) {
            if (p->indexCnt && p->next->indexCnt - p->indexCnt < minSectorIck) {
                if (!p->next->next)			// missing final index marker for next sector
                    p->indexCnt = 0;		// set to ignore
                else if (p->next->next->indexCnt - p->next->indexCnt < minSectorIck) {
                    /* merge split block */
                    p->end = p->next->end;
                    p->next->indexCnt = 0;	// this is a complete sector with track marker split
                    if (!seenTrackIndex) {
                        preTrackIndexSecCnt++;
                        seenTrackIndex = true;
                    }
                } else {					// track marker is  first marker
                    p->indexCnt = 0;		// set to ignore
                    seenTrackIndex = true;
                }
            } else if (!seenTrackIndex && p->indexCnt)
                preTrackIndexSecCnt++;
        }
        if (p)
            p->indexCnt = 0;
        int sectorNum = hc - preTrackIndexSecCnt;
        for (p = indexHead; p && p->next; p = p->next) {
            if (p->indexCnt)
                p->physSector = sectorNum++ % hc;
        }
    }
#if _DEBUG
    for (physBlock_t *p = indexHead; p; p = p->next)
        printf("start %lu, end %lu, sector %d, indexCnt %lu, sampleCnt %lu\n", p->start, p->end, p->physSector, p->indexCnt, p->sampleCnt);
    printf("scanned: %s at %s, hc: %d, sck: %.7f ick: %.7f\n", scanDate, scanTime, hc, sck, ick);
#endif
    rewindBlock();
}





// skip unused blocks
static void skipUnusedBlocks() {
    while (indexPtr && ((hc && indexPtr->indexCnt == 0) || indexPtr->start == indexPtr->end))
        indexPtr = indexPtr->next;
}

static void rewindBlock() 	{
    indexPtr = indexHead;
    blkNumber = 0;
    skipUnusedBlocks();
}


// seek to the specified stream block, setting the bounds for getNextFlux returns physical sector
// number (0 for soft sector) or -1 if block does not exist
int seekBlock(unsigned num) {
    if (num < blkNumber)					// need to start from beginning?
        rewindBlock();

    if (!indexPtr)							// no blocks!!!
        return -1;


    while (num > blkNumber && indexPtr->next) {				// scan the list
        indexPtr = indexPtr->next;
        skipUnusedBlocks();
        if (indexPtr)
            blkNumber++;
        else
            return -1;
    }
    
    if (num == blkNumber) {					// setup getNextFlux
        inPtr = fluxBuf + indexPtr->start;
        endPtr = fluxBuf + indexPtr->end;
        prevNsCnt = totalSampleCnt = 0;
        return indexPtr->physSector;
    } else
        return -1;
}


// the main flux load routines & support functions


static unsigned getWord(uint8_t *stream) {
    return (stream[1] << 8) + stream[0];
}

static unsigned getDWord(uint8_t *stream) {
    return (stream[3] << 24) + (stream[2] << 16) + (stream[1] << 8) + stream[0];
}

static size_t oob(size_t fluxPos, size_t size) {

    uint8_t* oobBlk = fluxBuf + fluxPos + 1;		// point to type in OOB

    fluxPos += 4;				// header length
    if (fluxPos > size)			// check we have at least the 0xd, type, len info
        return fluxPos;			// will force end and premature end message

    int type = *oobBlk++;
    if (type == OOB_EOF)
        return size;
    size_t len = getWord(oobBlk);
    oobBlk += 2;					// skip length field

    fluxPos += len;
    if (fluxPos <= size) {		// next data is at or before end so we can process here
        unsigned num1, num2, num3;

        switch (type) {
        case OOB_INVALID:
        default:
            fprintf(stderr, "invalid OOB block type = %d, len = %d\n", *fluxBuf, len);
            break;
        case OOB_STREAMINFO:
        case OOB_STREAMEND:
            if (len != 8)
                fprintf(stderr, "Incorrect length for OOB Stream %s Block - len = %d vs. 8\n", type == OOB_STREAMINFO ? "Info" : "End", len);
            else {
                num1 = getDWord(oobBlk);
                num2 = getDWord(oobBlk + 4);

                if (num1 != streamIdx) {
                    fprintf(stderr, "Stream Position error: expected %lu actual is %lu\n", num1, streamIdx);
                    while (streamIdx < num1 && streamIdx < fluxPos)
                        fluxBuf[(streamIdx)++] = NOP1;
                    if (streamIdx == num1)
                        fprintf(stderr, "Realigned with NOP1 filler\n");

                }
                if (type == OOB_STREAMEND && num2 != 0)
                    fprintf(stderr, "Steam End Block Error Code = %lu\n", num2);
            }
            break;
        case OOB_INDEX:
            if (len != 12)
                fprintf(stderr, "Incorrect length for OOB Index Block - len = %d vs. 12\n", len);
            if (len >= 12) {		// try to process if enough bytes
                num1 = getDWord(oobBlk);
                num2 = getDWord(oobBlk + 4);
                num3 = getDWord(oobBlk + 8);
                addIndexEntry(num1, num2, num3);
            }
            break;
        case OOB_KFINFO:
        {
            char* s;
            fluxBuf[fluxPos - 1] = '\0';			// last byte should already 0 but just in case
            if (s = strstr(oobBlk, "hc="))
                hc = atoi(s + 3);
            if (s = strstr(oobBlk, "sck=")) {
                sck = atof(s + 4);
                fluxScaler = 1.0e9 / sck;
            }
            if (s = strstr(oobBlk, "ick="))
                ick = atof(s + 4);
            if (s = strstr(oobBlk, "host_date="))
                strncpy(scanDate, s + 10, 10);		// scanDate[10] will be 0 due to bss initialisation
            if (s = strstr(oobBlk, "host_time="))
                strncpy(scanTime, s + 10, 8);		// scanTime[8] will be 0 due to bss initialisation
        }
            break;
        }
    }
    return fluxPos;	// skip block
}


/*
    Open the flux stream and extract the oob data to locate stream & index info
*/
int loadFlux(uint8_t* image, size_t size) {
    startIndex();		// mark start of leading data block

    size_t fluxPos = 0;			// location in the flux data
    streamIdx = 0;				// location in the stream (i.e. excluding OOB data
    fluxBuf = image;

    hc = 0;							// reset kinfo data
    sck = SCK;
    ick = ICK;
    scanDate[0] = scanTime[0] = 0;	

    while (fluxPos < size) {

        int type = fluxBuf[fluxPos];
        switch (type) {
        case OOB:
            fluxPos = oob(fluxPos, size);
            break;
        case NOP3: case FLUX3:
            if (fluxPos < size)
                fluxBuf[streamIdx++] = fluxBuf[fluxPos];
            fluxPos++;
        case NOP2:
            if (fluxPos < size)
                fluxBuf[streamIdx++] = fluxBuf[fluxPos];
            fluxPos++;
        default:
            if (type <= FLUX2) {
                if (fluxPos < size)
                    fluxBuf[streamIdx++] = fluxBuf[fluxPos];
                fluxPos++;
            }
            if (fluxPos < size)
                fluxBuf[streamIdx++] = fluxBuf[fluxPos];
            fluxPos++;
            break;
        }
    }

    endIndex(streamIdx);


    if (fluxPos > size)
        fprintf(stderr, "premature EOF\n");
    


    return seekBlock(0);

}




int32_t getNextFlux() {

    uint32_t c;
    uint8_t type;

    int ovl16 = 0;

    while (inPtr < endPtr) {
        type = *inPtr++;

        assert(type != 0xd);
        if (type <= FLUX2 || type == FLUX3|| type >= FLUX1) {
            if (type >= FLUX1)
                c = type;
            else if (type <= FLUX2)
                c = (type << 8) + *inPtr++;
            else {
                c = *inPtr++ << 8;
                c += *inPtr++;
            }
            c += ovl16;
            ovl16 = 0;
            // determine current sample position in ns
            totalSampleCnt += c;
            uint64_t newNsCnt = (uint64_t)(fluxScaler * totalSampleCnt + 0.5);
            c = (int32_t)(newNsCnt - prevNsCnt);
            prevNsCnt = newNsCnt;
            return c;

        } else if (type == 0xb)
            ovl16 += 0x10000;
        else
            inPtr += (type - NOP1);		// maps NOP1 - NOP3 to 1 - 3
    }
    return -1;


}
