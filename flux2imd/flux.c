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
#include "stdflux.h"

// default smample & index clocks
#define SCK 24027428.5714285            // sampling clock frequency
#define ICK 3003428.5714285625          // index sampling clock frequency

#define MAXROTATE       8
#define MAXHARDSECTOR   32


/* flux stream types */
enum streamType_t {
    FLUX2 = 7, 		// 0-7
    NOP1, NOP2, NOP3,
    OVL16, FLUX3, OOB, FLUX1	// 0xe-0xff
};

enum oob_t {
    OOB_INVALID = 0, OOB_STREAMINFO, OOB_INDEX, OOB_STREAMEND, OOB_KFINFO, OOB_EOF = 0xd
};


static int fluxIndexCnt;
static struct {
    uint32_t streamPos;
    uint32_t sampleCnt;
    uint32_t indexCnt;
    int16_t itype;
} fluxIndex[MAXROTATE * (MAXHARDSECTOR + 1)];

static int16_t cyl;
static int16_t head;

/* parsed data from the kinfo blocks*/
static int hc = 0;			// hard sector count or 0 for soft sector
static double sck = SCK;	// sample clock frequency scaled
static double ick = ICK;	// index clock frequency
static char scanDate[11];	// date on which data was scanned
static char scanTime[9];	// and the time


bool pass1KryoFlux(const uint8_t *image, uint32_t size);
bool pass2KryoFlux(const uint8_t *image, uint32_t size);

static void addFluxIndex(uint32_t streamPos, uint32_t sampleCnt, uint32_t indexCnt) {
    if (fluxIndexCnt >= MAXROTATE * (MAXHARDSECTOR + 1))
        logFull(D_ERROR, "addFluxIndex: too many indexes\n");
    fluxIndex[fluxIndexCnt].streamPos = streamPos;
    if (streamPos == 0) {
        logFull(D_WARNING, "index before start of data. Please report the problem\n");
        sampleCnt = 0;
    }
    fluxIndex[fluxIndexCnt].sampleCnt = sampleCnt;
    fluxIndex[fluxIndexCnt].indexCnt = indexCnt;
    fluxIndex[fluxIndexCnt++].itype = SSSTART;        // will fix later for hard sector disks

}


static double calcRPM(int index) {
    if (index + hc + 1 < fluxIndexCnt - 1)
        return 60.0 / ((fluxIndex[index + hc + 1].indexCnt - fluxIndex[index].indexCnt) / ick);
    else if (index > 1)
        return 0.0;     // no update
    else if (hc > 0) {
        logFull(D_WARNING, "Hard sector disk, less than one full rotation assuming %d rpm\n", hc > 10 ? 360 : 300);
        return hc > 10 ? 360.0 : 300.0;
    }
    logFull(D_WARNING, "Cannot determine rotational speed assuming 360 rpm\n");
    return 360.0;
}


static void finishFluxPass1(uint32_t streamPos, uint32_t sampleCnt) {
    if (fluxIndexCnt >= MAXROTATE * (MAXHARDSECTOR + 1))
        logFull(D_ERROR, "finishFluxPass1: too many indexes\n");
    fluxIndex[fluxIndexCnt].streamPos = streamPos;
    fluxIndex[fluxIndexCnt].sampleCnt = 0;
    fluxIndex[fluxIndexCnt++].itype = EODATA;

    double rpm = calcRPM(1);     // get an initial RPM

    beginFlux(sampleCnt, fluxIndexCnt, sck, rpm < 327.0 ? 300.0 : 360.00, hc);
    setActualRPM(rpm);

    if (hc > 0) {
        uint32_t secLenM25 = (uint32_t)((60.0 * ick / rpm / hc) * 0.75);        // approx seclen - 25%
        int i;
        for (i = 1; i < fluxIndexCnt - 2; i++) {
            if (fluxIndex[i].indexCnt - fluxIndex[i - 1].indexCnt < secLenM25 &&
                fluxIndex[i + 1].indexCnt - fluxIndex[i].indexCnt < secLenM25)
                break;
        }
        if (i >= fluxIndexCnt - 2)
            logFull(D_ERROR, "finsihFluxPass1: could not determine index vs hardsector index\n");
        int sector = hc - i;
        for (int slot = 0; slot < fluxIndexCnt - 1; slot++)
            if (sector == hc)
                sector = 0;
            else
                fluxIndex[slot].itype = sector++;
    }
}


// the main flux load routines & support functions


static uint32_t getWord(const uint8_t *stream) {
    return (stream[1] << 8) + stream[0];
}

static uint32_t getDWord(const uint8_t *stream) {
    return (stream[3] << 24) + (stream[2] << 16) + (stream[1] << 8) + stream[0];
}

static uint32_t oob(const uint8_t *fluxBuf, uint32_t fluxPos, uint32_t size, uint32_t streamIdx) {

    const uint8_t *oobBlk = fluxBuf + fluxPos + 1;		// point to type in OOB

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
                addFluxIndex(num1, num2, num3);
            }
            break;
        case OOB_KFINFO:
        {
            char *tmp = xmalloc(len + 1);           // make a safe string
            memcpy(tmp, oobBlk, len);
            tmp[len] = '\0';
            char *s;
            if (s = strstr(tmp, "hc="))
                hc = atoi(s + 3);
            if (s = strstr(tmp, "sck="))
                sck = atof(s + 4);
            if (s = strstr(tmp, "ick=="))        // handle spurious double = in some older kryoflux versions
                ick = atof(s + 5);
            else if (s = strstr(tmp, "ick="))
                ick = atof(s + 4);
            if (s = strstr(tmp, "host_date="))
                strncpy(scanDate, s + 10, 10);		// scanDate[10] will be 0 due to bss initialisation
            if (s = strstr(tmp, "host_time="))
                strncpy(scanTime, s + 10, 8);		// scanTime[8] will be 0 due to bss initialisation
            free(tmp);
        }
        break;
        }
    }
    return fluxPos;	// skip block
}


/*
    Open the flux stream and extract the oob data to locate stream & index info
    the kryoflux file is scanned twice
    1) extract oob and work out number of samples
    2) build the standard flux format
*/
bool loadKryoFlux(const uint8_t *image, uint32_t size) {
    if (pass1KryoFlux(image, size) && pass2KryoFlux(image, size))
        return true;

    logFull(D_WARNING, "Invalid Kryoflux file\n");
    return false;
}


bool pass1KryoFlux(const uint8_t *image, uint32_t size) {
    fluxIndexCnt = 0;
    hc = 0;
    sck = SCK;
    ick = ICK;
    scanDate[0] = scanTime[0] = 0;

    uint32_t fluxPos = 0;		// location in the flux data
    uint32_t streamPos = 0;		// location in the stream (i.e. excluding OOB data
    uint32_t sampleCnt = 0;     // number of real samples


    while (fluxPos < size) {

        int matchType = image[fluxPos];
        switch (matchType) {
        case OOB:
            fluxPos = oob(image, fluxPos, size, streamPos);
            break;
        case FLUX3:
            sampleCnt++;
        case NOP3:
            streamPos += 3;
            fluxPos += 3;
            break;
        case NOP2:
            streamPos += 2;
            fluxPos += 2;
            break;
        case NOP1: case OVL16:
            streamPos++;
            fluxPos++;
            break;
        default:
            if (matchType <= FLUX2) {
                streamPos++;
                fluxPos++;
            }
            streamPos++;
            fluxPos++;
            sampleCnt++;
            break;
        }
    }
    if (fluxPos > size)
        logFull(D_ERROR, "premature EOF\n");

    finishFluxPass1(streamPos, sampleCnt);
    return true;
}



// assumes pass1 was ok so no additional checks needed for fluxPos overflow other than for EOF

bool pass2KryoFlux(const uint8_t *image, uint32_t size) {
    uint32_t fluxPos = 0;		// location in the flux data
    uint32_t streamPos = 0;		// location in the stream (i.e. excluding OOB data

    int c = 0;
    int ovl16 = 0;
    int curIndexPos = 0;

    while (fluxPos < size) {

        int matchType = image[fluxPos];
        switch (matchType) {
        case OOB:
            if (fluxPos + 3 >= size || image[fluxPos + 1] == OOB_EOF)
                fluxPos = size;
            else
                fluxPos += 4 + getWord(image + fluxPos + 2);

            continue;
        case NOP3:
            streamPos += 3;
            fluxPos += 3;
            continue;
        case NOP2:
            streamPos += 2;
            fluxPos += 2;
            continue;
        case OVL16:
            ovl16 += 0x10000;
        case NOP1:
            streamPos++;
            fluxPos++;
            continue;
        case FLUX3:
            c = (image[fluxPos + 1] << 8) + image[fluxPos + 2];
            streamPos += 3;
            fluxPos += 3;
            break;
        default:
            if (matchType <= FLUX2) {
                c = matchType << 8;
                streamPos++;
                fluxPos++;
            }
            c += image[fluxPos++];
            streamPos++;
            break;
        }
        while (curIndexPos < fluxIndexCnt && streamPos >= fluxIndex[curIndexPos].streamPos) {
            int16_t itype = fluxIndex[curIndexPos].itype;
            if (itype >= 0 || (hc == 0 && itype == SSSTART))
                addIndex(itype, fluxIndex[curIndexPos].sampleCnt);
            double rpm = calcRPM(curIndexPos++);
            if (rpm > 0)
                setActualRPM(rpm);
        }
        addDelta(c + ovl16);
        ovl16 = c = 0;
    }
    endFlux();

    return true;

}
