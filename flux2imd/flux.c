/****************************************************************************
 *  program: flux2imd - create imd image file from kryoflux file            *
 *  Copyright (C) 2020 Mark Ogden <mark.pm.ogden@btinternet.com>            *
 *                                                                          *
 *  This program is free software; you can redistribute it and/or           *
 *  modify it under the terms of the GNU General Public License             *
 *  as published by the Free Software Foundation; either version 2          *
 *  of the License, or (at your option) any later version.                  *
 *                                                                          *
 *  This program is distributed in the hope that it will be useful,         *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU General Public License for more details.                            *
 *                                                                          *
 *  You should have received a copy of the GNU General Public License       *
 *  along with this program; if not, write to the Free Software             *
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,              *
 *  MA  02110-1301, USA.                                                    *
 *                                                                          *
 ****************************************************************************/


// This is an open source non-commercial project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "flux.h"
#include "util.h"

// default smample & index clocks
#define SCK 24027428.5714285            // sampling clock frequency
#define ICK 3003428.5714285625          // index sampling clock frequency

#define HS8NSECTOR      32
#define HS8ICK	        (ick / 6 / hc)  // sector index cnt for HS 8" (assumes 360 rpm i.e. 6 rps)
#define HS5ICK          (ick / 5 / hc)  // sector index cnt for HS 5" (assumes 300 rpm i.e  5 rps)


/* flux stream types */
enum streamType_t {
    FLUX2 = 7, 		// 0-7
    NOP1, NOP2, NOP3,
    OVL16, FLUX3, OOB, FLUX1	// 0xe-0xff
};

enum oob_t {
    OOB_INVALID = 0, OOB_STREAMINFO, OOB_INDEX, OOB_STREAMEND, OOB_KFINFO, OOB_EOF = 0xd
};


static uint8_t *fluxBuf;		// buffer where flux data / stream data held
static uint32_t streamIdx;		// current position of stream data with OOB data removed
uint8_t *inPtr;
uint8_t *endPtr;
static double rpm;

int firstSector = 31;
static uint32_t trkFluxCount;       // used to capture the current clock offset of last processed flux entry


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
    struct _physBlock *next;			// chains list of information
    uint32_t start;						// sample count of first sample or 0 for  first soft sector
    uint32_t end;						// sample count of last sample, usually start of next block
    uint32_t sampleCnt;					// offset in first sample of index hole start
    uint32_t indexCnt;					// and index clock of the marker, 0 for first soft sector
    uint32_t indexDelta;                // delta to next index hole (ick based)
    uint8_t physSector;					// physical sector number always 0 for soft sector
} physBlock_t;



static physBlock_t *indexHead = NULL;			// list start
static physBlock_t *indexPtr = NULL;			// current item being processed, for load this is the end
static unsigned blkNumber = 0;					// current block number


static void clearIndex() {
    for (physBlock_t *q, *p = indexHead; p; p = q) {    // free any previous index chain
        q = p->next;
        free(p);
    }
    indexPtr = indexHead = NULL;
    blkNumber = 0;
    rpm = 360.0;                                // safe value 8" disk
}

static void addIndexEntry(uint32_t streamPos, uint32_t sampleCnt, uint32_t indexCnt) {
    physBlock_t *newIndex = (physBlock_t *)xmalloc(sizeof(physBlock_t));
    newIndex->start = newIndex->end = streamPos;
    newIndex->sampleCnt = sampleCnt;
    newIndex->indexCnt = indexCnt;
    newIndex->physSector = 0;
    newIndex->indexDelta = 0;
    newIndex->next = NULL;
    if (!indexPtr)
        indexHead = indexPtr = newIndex;
    else {
        indexPtr->next = newIndex;
        indexPtr->end = streamPos;
        indexPtr->indexDelta = indexCnt - indexPtr->indexCnt;
        indexPtr = newIndex;
    }
}


// assumes start head has been called and possibly several addIndexEntry calls
static void endIndex(uint32_t streamPos) {
    if (!indexPtr)      // no blocks
        return;
    indexPtr->end = streamPos;

    logFull(D_FLUX, "scanned: %s at %s, hc: %d, sck: %.7f ick: %.7f\n", scanDate, scanTime, hc, sck, ick);
#if _DEBUG
    for (physBlock_t *p = indexHead; p; p = p->next)
        logFull(D_FLUX, "start %lu, end %lu, sector %d, indexCnt %lu, sampleCnt %lu\n",
            p->start, p->end, p->physSector, p->indexCnt, p->sampleCnt);
#endif
    // for hard sectors merge blocks split by track index mark
    if (hc) {
        // calculate nominal sector length  in index clock cycles and assume full sector is > 0.75 * nominal
        unsigned minSectorIck = (unsigned)(((hc == HS8NSECTOR) ? HS8ICK : HS5ICK) * 0.75);
        int sectorId = -1;
        int firstSectorId = hc - 1;
        physBlock_t *p;


        for (p = indexHead; p && p->next; p = p->next) {
            if (p->indexDelta < minSectorIck) {             // indexDelta only 0 for last sector
                if (p->next->indexDelta < minSectorIck) {   // indexDelta may be 0 for last sector but merge anyway
                    p->end = p->next->end;                  // merge blocks
                    if (p->next->indexDelta == 0)
                        p->indexDelta = 0;
                    else
                        p->indexDelta += p->next->indexDelta;
                    physBlock_t *tmp = p->next;
                    p->next = p->next->next;
                    free(tmp);
                    if (sectorId < 0) {         // first sector 0 we have seen
                        sectorId = hc - 1;      // id of this sector
                        for (physBlock_t *q = indexHead; firstSectorId < sectorId; q = q->next)    // fix up sector ids so far
                            q->physSector = firstSectorId++;
                    }
                }
            }
            if (sectorId >= 0)
                p->physSector = sectorId++ % hc;
            else
                firstSectorId--;

        }
        if (indexHead->indexDelta < minSectorIck) {         // first sector was actually split so junk it
            p = indexHead->next;
            free(indexHead);
            indexHead = p;
        }
    }

    // work out rpm
    uint32_t startIndex = 0, endIndex = 0;
    for (physBlock_t *p = indexHead; p; p = p->next) {
        if (p->physSector == 0)
            if (startIndex) {
                endIndex = p->indexCnt;
                break;
            } else
                startIndex = p->indexCnt;
    }
    if (endIndex && endIndex != startIndex)
        rpm = ick / (endIndex - startIndex) * 60;

    seekBlock(0);
}





// skip unused blocks
static void skipUnusedBlocks() {
    while (indexPtr && ((hc && indexPtr->indexCnt == 0) || indexPtr->start == indexPtr->end))
        indexPtr = indexPtr->next;
}


// seek to the specified stream block, setting the bounds for getNextFlux returns physical sector
// number (0 for soft sector) or -1 if block does not exist
int seekBlock(unsigned num) {
    if (num == 0 || num < blkNumber) {					// need to start from beginning?
        indexPtr = indexHead;
        blkNumber = 0;
    }

    if (!indexPtr)							// no blocks!!!
        return -1;


    while (num > blkNumber && indexPtr->next) {				// scan the list
        indexPtr = indexPtr->next;
        if (indexPtr) //-V547
            blkNumber++;
        else
            return -1;
    }

    if (num == blkNumber && indexPtr->end - indexPtr->start > 128) {					// setup getNextFlux
        inPtr = fluxBuf + indexPtr->start;
        endPtr = fluxBuf + indexPtr->end;
        trkFluxCount = 0;
        return indexPtr->physSector;
    } else
        return -1;
}



int cntHardSectors() {
    return hc;
}

// the main flux load routines & support functions


static uint32_t getWord(uint8_t *stream) {
    return (stream[1] << 8) + stream[0];
}

static uint32_t getDWord(uint8_t *stream) {
    return (stream[3] << 24) + (stream[2] << 16) + (stream[1] << 8) + stream[0];
}

static size_t oob(size_t fluxPos, size_t size, size_t streamIdx) {

    uint8_t *oobBlk = fluxBuf + fluxPos + 1;		// point to type in OOB

    fluxPos += 4;				// header length
    if (fluxPos > size)			// check we have at least the 0xd, type, len info
        return fluxPos;			// will force end and premature end message

    int matchType = *oobBlk++;
    if (matchType == OOB_EOF)
        return size;
    uint32_t len = getWord(oobBlk);
    oobBlk += 2;					// skip length field

    fluxPos += len;
    if (fluxPos <= size) {		// next data is at or before end so we can process here
        unsigned num1, num2, num3;

        switch (matchType) {
        case OOB_INVALID:
        default:
            logFull(D_ERROR, "invalid OOB block type = %d, len = %d\n", *fluxBuf, len);
            break;
        case OOB_STREAMINFO:
        case OOB_STREAMEND:
            if (len != 8)
                logFull(D_ERROR, "Incorrect length for OOB Stream %s Block - len = %d vs. 8\n", matchType == OOB_STREAMINFO ? "Info" : "End", len);
            else {
                num1 = getDWord(oobBlk);
                num2 = getDWord(oobBlk + 4);
                if (num1 != streamIdx) {
                    logFull(D_ERROR, "Stream Position error: expected %lu actual is %lu - delta %ld\n", num1, streamIdx, (int)streamIdx - (int)num1);
                }
                if (matchType == OOB_STREAMEND && num2 != 0)
                    logFull(D_ERROR, "Steam End Block Error Code = %lu\n", num2);
            }
            break;
        case OOB_INDEX:
            if (len != 12)
                logFull(D_ERROR, "Incorrect length for OOB Index Block - len = %d vs. 12\n", len);
            if (len >= 12) {		// try to process if enough bytes
                num1 = getDWord(oobBlk);
                num2 = getDWord(oobBlk + 4);
                num3 = getDWord(oobBlk + 8);
                addIndexEntry(num1, num2, num3);
            }
            break;
        case OOB_KFINFO:
        {
            char *s;
            fluxBuf[fluxPos - 1] = '\0';			// last byte should already 0 but just in case
            if (s = strstr(oobBlk, "hc="))
                hc = atoi(s + 3);
            if (s = strstr(oobBlk, "sck=")) {
                sck = atof(s + 4);
                fluxScaler = 1.0e9 / sck;
            }
            if (s = strstr(oobBlk, "ick=="))        // handle spurious double = in some older kryoflux versions
                ick = atof(s + 5);
            else if (s = strstr(oobBlk, "ick="))
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
int loadFlux(uint8_t *image, size_t size) {
    clearIndex();		// clean up any old index list

    size_t fluxPos = 0;			// location in the flux data
    size_t streamIdx = 0;		// location in the stream (i.e. excluding OOB data
    fluxBuf = image;

    hc = 0;							// reset kinfo data
    sck = SCK;
    ick = ICK;
    scanDate[0] = scanTime[0] = 0;

    while (fluxPos < size) {

        int matchType = fluxBuf[fluxPos];
        switch (matchType) {
        case OOB:
            fluxPos = oob(fluxPos, size, streamIdx);
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
            if (matchType <= FLUX2) {
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
        logFull(D_ERROR, "premature EOF\n");

    seekBlock(0);
    if (indexHead && indexHead->next)
        return hc;
    return -1;
}


void unloadFlux() {
    if (fluxBuf)                // release any old image;
        free(fluxBuf);
    fluxBuf = NULL;
}


int peekNextFlux() {
    uint8_t *here = inPtr;
    uint32_t clock = trkFluxCount;
    int val = getNextFlux();
    inPtr = here;
    trkFluxCount = clock;
    return val;
}

int getNextFlux() {

    int c;
    int matchType;
    int ovl16 = 0;

    while (inPtr < endPtr) {
        matchType = *inPtr++;

        assert(matchType != 0xd);
        if (matchType <= FLUX2 || matchType == FLUX3 || matchType >= FLUX1) {
            if (matchType >= FLUX1)
                c = matchType;
            else if (matchType <= FLUX2)
                c = (matchType << 8) + *inPtr++;
            else {
                c = *inPtr++ << 8;
                c += *inPtr++;
            }
            c += ovl16;
            ovl16 = 0;
            trkFluxCount += c;
            return (int)(trkFluxCount * fluxScaler);

        } else if (matchType == 0xb)
            ovl16 += 0x10000;
        else
            inPtr += (matchType - NOP1);		// maps NOP1 - NOP3 to 1 - 3
    }
    return -1;
}



double getRPM() {
    if (hc != 0 && indexPtr->indexDelta)
        rpm = ick * 60.0 / (indexPtr->indexDelta * hc);
    return rpm;
}

uint32_t getBitPos(uint32_t cellSize) {
    return (uint32_t)(trkFluxCount * fluxScaler / cellSize);
}