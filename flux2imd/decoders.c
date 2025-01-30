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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include "dpll.h"
#include "flux.h"
#include "flux2imd.h"
#include "formats.h"
#include "trackManager.h"
#include "util.h"
#include "stdflux.h"
static void ssDumpTrack(char *usrfmt);

static void invert(uint16_t *data, int len) {
    while (len-- > 0)
        *data++ ^= 0xff;
}



// support for sector size option in list of possible formats (O_SIZE)
static bool chkSizeChange(unsigned sSize) {
    unsigned curSSize = curFormat->sSize;

    if (curFormat->options & O_SIZE) {
        for (formatInfo_t* p = curFormat + 1; p->encoding == curFormat->encoding; p++) {
            if (p->sSize == sSize) {
                logFull(D_DECODER, "Updated format to %s\n", p->name);
                curFormat = p;
                updateTrackFmt();
                return sSize != curSSize;       // true if sSize is different from trial sSize
            }
        }
        logFull(D_FATAL, "%s sector size %d not currently supported\n", curFormat->name, 128 << sSize);
    }
    if (sSize != curSSize)
        logFull(D_FATAL, "Multiple sector sizes per track not supported\n");
    return false;
}

static int getData(int marker, uint16_t* buf, int length) {
    int val;
    int i = 0;
    bool isGood;
     if (curFormat->options == O_ZDS || curFormat->options == O_MTECH)                      // ZDS includes track and sector in CRC check
        buf[i++] = marker >> 8;
   buf[i++] = marker & 0xff;
    for (; i < length; i++) {
        if ((val = getByte()) < 0)
            return -1;
        buf[i] = val;
    }
    if (curFormat->options  == O_HP) {
        buf++;
        length--;
    }
    if (isGood = curFormat->crcFunc(buf, length))
        for (int i = 0; i < length; i++)
            buf[i] &= 0xff;                 // remove suspect markers
    return isGood;
}



// there is a potential problem in that LSI start byte can match the first byte of ZDS start
// in particular the LSI cylinder / ZDS sector conflicts are
// 0/0 1/64 2/32 4/16 6/48 8/8 9/72 10/40 12/24 14/56
// 16/4 17/68 18/36 20/20 22/52 24/12 25/76 26/446 28/28 30/60

// if we know the track is ZDS then don't allow LSI detection
// otherwise check if match is > 12 bytes in assume ZDS if valid sectors hasn't been seen yet
// hopefully one of the other 31 sectors will detect LSI / ZDS if this guess is  wrong


// the cylinder sector conflicts
static uint8_t conflicts[32] = {
    0, 64, 32, 255, 16, 255, 48, 255, 8, 72, 40, 255, 24, 255, 56, 255,
    4, 68, 36, 255, 20, 255, 52, 255, 12, 76, 44, 255, 28, 255, 60, 255
};


int32_t fromTs = 0;

static uint16_t hs8Sync(unsigned cylinder, unsigned slot) {
    int matchType;

    makeHS8Patterns(cylinder, slot);

    matchType = matchPattern(30);
    if (curFormat->options == 0 && conflicts[slot] == cylinder && matchType == LSI_SECTOR && getByteCnt(fromTs) > 12)
        matchType = matchPattern(2);
    return matchType;

}

static uint8_t lsiInterleave[] = {
 0, 11, 22, 1, 12, 23, 2, 13, 24, 3, 14, 25, 4, 15, 26, 5,
 16, 27, 6, 17, 28, 7, 18, 29, 8, 19, 30, 9, 20, 31, 10, 21

};

static bool chkZeroHeader(int cylinder, int  side, int slot, uint16_t* rawData) {
    bool allZero = true;
    for (int i = 2; i < 12; i++)
        if (rawData[i] & 0xff) {
            allZero = false;
            break;
        }
    if (!allZero) {
        logBasic("Non zero header %02d/%d/%02d:", cylinder, side, slot);
        for (int i = 2; i < 12; i++)
            logBasic(" %02X", rawData[i]);
        logBasic("\n");
    }
    return allZero;
}


static uint8_t nsiMap[2][10] = {
    {1, 3, 5, 7, 9, 2, 4, 6, 8, 10 },
    {1, 3, 5, 7, 9, 2, 4, 6, 8, 10 },
};

//   MTECH not currently work, here for my debugging
static void hs5GetTrack(int cylinder, int side) {
    int matchType;
    int slot;
    int result;
    uint16_t rawData[1024];		// 268 data bytes & crc
    bool sectorStatus[16] = { false };
    idam_t idam = { cylinder, side, 0, 0 };

    bool done = false;

    int cntSlot = getHsCnt();
    int sectorSize = 128 << curFormat->sSize;

    initTrack(cylinder, side);
    resetTracker();
    
    for (int profile = 0; !done && retrain(profile); profile++) {
        seekIndex(0);
        fromTs = peekTs();
        for (int i = 0; (slot = seekIndex(i)) != EODATA; i++) {
            if (slot < 0)
                continue;
            fromTs = peekTs();
            if (!sectorStatus[slot] || (profile == 0 && (debug & D_NOOPTIMISE))) {
                retrain(0);
                if (cntSlot != 10)
                    makeHS5Patterns(cylinder, slot);

                while ((matchType = matchPattern(40)) == GAP)
                    ;
                if (matchType == NSI_SECTOR) {
                    result = getData(0xfb, rawData, sectorSize + 2);
                    addSectorData(-slot, result, sectorSize + 1, rawData + 1);
                    sectorStatus[slot] |= result;
                } else if (matchType == MTECH_SECTOR) {
                    int readCylinder = decode(pattern >> 16) & 0xff;
                    if (readCylinder >= 77)
                        logFull(D_DECODER, "Cylinder %d >= 77\n", readCylinder);
                    else if ((result = getData((readCylinder << 8) + slot, rawData, sectorSize + 13)) < 0)
                        logFull(D_DECODER, "sector %d premature end\n", slot);
                    else if (result == 0 && readCylinder != cylinder)
                        logFull(D_DECODER, "sector %d crc error in header\n", slot);
                    else if (result == 1 && !chkZeroHeader(cylinder, side, slot, rawData))
                        logFull(D_DECODER, "sector %d sync error\n", slot);
                    else {
                        if (result == 1) {
                            if (cylinder != readCylinder) {
                                logFull(D_DECODER, "cylinder %d expecting %d, assuming seek error\n", readCylinder & 0xff, cylinder);
                                idam.cylinder = cylinder = readCylinder;
                            }
                            idam.sectorId = slot;
                            addIdam(-slot, &idam);
                        }
                        addSectorData(-slot, result, sectorSize + 1, rawData + 12);
                        sectorStatus[slot] |= result;
                    }
                } else
                    logFull(D_DECODER, "cannot find start of sector %d\n", slot);
            }

        }
        done = true;
        for (int slot = 0; slot < cntSlot; slot++)
            if (!sectorStatus[slot]) {
                done = false;
                break;
            }
    }
    if (cntSlot == 10)
        idam.sSize = 2;
    for (slot = 0; slot < cntSlot; slot++) {
            idam.sectorId = (curFormat->options == O_NSI) ? nsiMap[side][slot] : slot;
            addIdam(-slot, &idam);
    }
    setOnIndex(NULL);
}

// only up to 32 sector 8" hard sector format supported
static void hs8GetTrack(int cylinder, int side) {
    int slot;
    uint8_t sectorStatus[32] = { 0 };
    unsigned dataPos;
    int matchType;

    bool done = false;
    initTrack(cylinder, 0);
    resetTracker();

    int cntSlot = getHsCnt();
    for (int profile = 0; !done; profile++) {
        for (int i = 0; (slot = seekIndex(i)) != EODATA; i++) {
            if (slot < 0)
                continue;
            fromTs = peekTs();
            if (sectorStatus[slot] && profile != 0 && !(debug & D_NOOPTIMISE))        // skip known good sectors unless D_NOOPTIMISE specified
                continue;
            uint16_t rawData[138];      // sector + cylinder + 128 bytes + 4 links + 2 CRC + 2 postamble
            int result;

            if (!retrain(profile)) {
                done = true;
                break;
            }

            if (matchType = hs8Sync(cylinder, slot)) {
                dataPos = getByteCnt(fromTs);
                    if (curFormat->options == O_LSI && matchType == ZDS_SECTOR) {    // LSI & ZDS on same track assume all ZDS
                        DBGLOG(D_DECODER, "@%d:%d ZDS sector after LSI sector, rescan assuming ZDS\n", slot, dataPos);
                        setFormat("ZDS");
                        updateTrackFmt();
                        memset(sectorStatus, 0, sizeof(sectorStatus));
                        initTrack(cylinder, 0);    // clear any data collected so far assume all ZDS
                        i = -1;                    // restart for a whole rescan
                        continue;

                    }
                if (!curFormat->options) {     // note LSI->ZDS detected above, ZDS->LSI cannot happen
                    setFormat(matchType == LSI_SECTOR ? "FM8H-LSI" : "ZDS");
                    updateTrackFmt();
                }
                if (matchType == LSI_SECTOR) {
                    if ((result = getData(slot, rawData, 131)) < 0) {
                        logFull(D_DECODER, "@%d LSI sector %d premature end\n", dataPos, slot);
                        continue;
                    }
                    addSectorData(-slot , result, 130, rawData + 1);
                } else {
                    if ((result = getData(((slot + 0x80) << 8) + cylinder, rawData, 138)) < 0) {
                        logFull(D_DECODER, "@%d ZDS sector %d premature end\n", dataPos, slot);
                        continue;
                    }
                    addSectorData(-slot, result, 136, rawData + 2);
                }

                DBGLOG(D_DECODER, "@%d-%d %s sector %d%s\n", dataPos, getByteCnt(fromTs), matchType == LSI_SECTOR ? "LSI" : "ZDS",
                    slot, result == 1 ? "" : " bad crc");
                sectorStatus[slot] |= result;
            } else
                logFull(D_DECODER, "cannot find start of sector %d\n", slot);

            if (debug & D_DECODER) {
                int val;
                while ((val = getByte()) >= 0)
                    logBasic(" %02X", val);
                logBasic("\n");
                DBGLOG(D_DECODER, "@%u end of sector %d\n", getByteCnt(fromTs));
            }

        }
        if (done == true)           // retrain caused exit
            break;
        done = true;                // assume all slots done, set to false if not
        for (int slot = 0; slot < cntSlot; slot++)
            if (!sectorStatus[slot]) {
                done = false;
                break;
            }
    }
    // add the idams
    idam_t idam = { cylinder, 0, 0, 0 };

    for (int slot = 0; slot < cntSlot; slot++) {
        idam.sectorId = (curFormat->options != O_NSI) ? slot : (lsiInterleave[slot] + cylinder * 8) % 32;
        addIdam(-slot, &idam);
    }
    setOnIndex(NULL);
    finaliseTrack();
}

static void ssGetTrack(int cylinder, int side, char const *usrfmt) {

    unsigned matchType;
    int result;

    idam_t idam;
    unsigned sectorLen;

    uint16_t rawData[1024 + 3];       // allow for 1024 byte sector + 2 byte CRC + Address Marker

    unsigned idamPos = 0;
    unsigned dataPos = 0;
    unsigned indexPos = 0;
    bool savedData = false;

    initTrack(cylinder, side);
    unsigned sSize = curFormat->sSize;

    bool done = false;
    int itype;
    int profile;
    for (profile = 0; !done; profile++) {
        for (int i = 0; !done && (itype = seekIndex(i)) != EODATA; i++) {
            if (itype == SODATA)
                continue;

            fromTs = peekTs();
            if (!retrain(profile)) {
                done = true;
                break;
            }
            resetTracker();
            
            bool restart = false;
            logFull(D_DECODER, "Profile %d, block %d\n", profile, i);
            while (!restart && (matchType = matchPattern(1200))) {
                switch (matchType) {
                case M2FM_INDEXAM:
                case INDEXAM:           // to note start of track
                    indexPos = getByteCnt(fromTs);
                    DBGLOG(D_DECODER, "@%d index\n", indexPos);
                    break;
                case TI_IDAM:
                    idamPos = getByteCnt(fromTs);
                    result = getData(matchType, rawData, 9);
                    if (result == 1) {
                        idam.cylinder = (rawData[2] & 0x7f);
                        idam.side = (rawData[1] >> 3) & 0xf;
                        idam.sSize = 2;         // treat as 256 byte sector
                        idam.sectorId = rawData[4] & 0xff;
                        addIdam(idamPos, &idam);
                        logFull(D_DECODER, "TI IDAM @%d - head=%d, cyl=%d, sector=%d\n", idamPos, (rawData[1] & 0xff) >> 3, rawData[2] & 0x7f, rawData[4] & 0xff);
                    }
                    else
                        logFull(D_DECODER, "TI IDAM @%d (crc error) - %02X % 02X % 02X % 02X % 02X % 02X\n", idamPos,
                        rawData[1] & 0xff, rawData[2] & 0xff, rawData[3] & 0xff, rawData[4] & 0xff, rawData[5] & 0xff, rawData[6] & 0xff);

                    break;
                case IDAM:
                case M2FM_IDAM:
                case HP_IDAM:
                    idamPos = getByteCnt(fromTs);
                    result = getData(matchType, rawData, matchType == HP_IDAM ? 5 : 7);
                    if (result == 1) {           // valid so ok
                        idam.cylinder = (uint8_t)rawData[1];
                        if (matchType == HP_IDAM) {
                            idam.sectorId = (uint8_t)rawData[2];
                            idam.side = 0;
                            idam.sSize = 1;
                        } else {
                            idam.side = (uint8_t)rawData[2];
                            idam.sectorId = (uint8_t)rawData[3];
                            sSize = idam.sSize = (uint8_t)rawData[4] & 0xf;
                        }
                        if (chkSizeChange(sSize) && savedData) {    // new size but we already saved data!!
                            DBGLOG(D_DECODER, "@%d restarted with new size\n", idamPos);
                            restart = true;
                            break;
                        }
                        //chkSptChange(idamPos, true);

                        DBGLOG(D_DECODER, "@%d-%d idam %d/%d/%d sSize %d\n", idamPos, getByteCnt(fromTs), idam.cylinder, idam.side, idam.sectorId, sSize);
                        addIdam(idamPos, &idam);
                    } else
                        logFull(D_DECODER, "@%d idam %s\n", idamPos, result < 0 ? "premature end\n" : "bad crc\n");
                    break;
                case TI_DATAAM:
                case DATAAM:
                case M2FM_DATAAM:
                case HP_DATAAM:
                    dataPos = getByteCnt(fromTs);

                    sectorLen = matchType == TI_DATAAM ? 288 : 128 << sSize;
                    result = getData(matchType, rawData, sectorLen + 3);
                    if (result >= 0) {
                        if (curFormat->options & O_UINV)
                            invert(rawData + 1, sectorLen);
                        addSectorData(dataPos, result, sectorLen + 2, rawData + 1);     // add data after address mark
                        savedData = true;
                        DBGLOG(D_DECODER, "@%d-%d data len %d%s\n", dataPos, getByteCnt(fromTs), sectorLen, result == 1 ? "" : " bad crc");
                    } else
                        logFull(D_DECODER, "@%d premature end of sector\n", dataPos);
                    break;
                case DELETEDAM:
                case M2FM_DELETEDAM:
                case HP_DELETEDAM:
                    logFull(ALWAYS, "@%d Deleted AM\n", getByteCnt(fromTs));
                    sectorLen = 128 << sSize;
                    getData(matchType, rawData, sectorLen + 3);
                    break;
                }
            }
            if (restart) {
                initTrack(cylinder, side);
                savedData = false;
                i = 0;
            } else {
                DBGLOG(D_DECODER, "@%d end of track\n", getByteCnt(fromTs));
                done = checkTrack(profile);
            }
        }
    }
    finaliseTrack();
}

bool flux2Track(char const *usrfmt) {
    char *fmt = NULL;
    int maxSlot = 0;
    int hs;
    int cylinder = getCyl();
    int head = getHead();
    if (cylinder < 0 && head < 0) {
        logFull(D_WARNING, "Unspecified Cylinder / Head not yet supported\n");
        return false;
    }
    logCylHead(cylinder, head);

    if (!setInitialFormat(getFormat(usrfmt))) {
        logFull(D_ERROR, "Could not determine encoding\n");
        return false;
    }
    if ((hs = getHsCnt()) > 0) {
        if (hs == 16 || hs == 10)
            hs5GetTrack(cylinder, head);
        else if (hs == 32)
            hs8GetTrack(cylinder, head);
        else {
            logFull(D_ERROR, "Disk has %d Hard Sectors - currently not supported\n", hs);
            return false;
        }
    } else
        // ssDumpTrack(usrfmt);
        ssGetTrack(cylinder, head, usrfmt);
    return true;
}


bool noIMD() {
    return curFormat->options & O_NOIMD;
}


int32_t indexHole = 0;
bool noIndex(int16_t itype) {
    if (itype == SSSTART)
     indexHole = getByteCnt(0);
    return true;
}



extern uint32_t cellSize;
static void ssDumpTrack(char *usrfmt) {

    unsigned matchType;
    int result;

    idam_t idam;
    unsigned sectorLen;

    uint16_t rawData[1024 + 3];       // allow for 1024 byte sector + 2 byte CRC + Address Marker

    unsigned idamPos = 0;
    unsigned dataPos = 0;
    unsigned indexPos = 0;
    unsigned gapStart = 0;

    unsigned syncStart = 0;

    bool savedData = false;
    int sSize = 0;

    setOnIndex(&noIndex);

    bool lock = false;
    seekIndex(0);                   // to start of track images
    retrain(0);
    while ((matchType = matchPattern2(lock)) >= 0) {
        if ((indexHole || matchType != GAP) && gapStart) {
            printf("%d: GAP %d bytes - %d\n", gapStart, getByteCnt(0) - gapStart, cellSize);
            gapStart = 0;
        }
        if ((indexHole || matchType != SYNC) && syncStart) {
            printf("%d: SYNC %d bytes - %d\n", syncStart, getByteCnt(0) - syncStart, cellSize);
            syncStart = 0;
        }
        if (indexHole) {
            printf("%d: index Hole - %d\n", indexHole, cellSize);
            indexHole = 0;
        }
        switch (matchType) {
        case GAP:
            if (!gapStart)
                gapStart = getByteCnt(0) - 1;
            break;
        case SYNC:
            if (!syncStart)
                syncStart = getByteCnt(0) - 1;

            break;
        case M2FM_INDEXAM:
        case INDEXAM:           // to note start of track
            indexPos = getByteCnt(0);
            printf("%d: index AM - %d\n", indexPos, cellSize);
            break;
        case TI_IDAM:
            idamPos = getByteCnt(0);
            result = getData(matchType, rawData, 9);
            if (result == 1) {
                idam.cylinder = (rawData[2] & 0x7f);
                idam.side = (rawData[1] >> 3) & 0xf;
                idam.sSize = 2;         // treat as 256 byte sector
                idam.sectorId = rawData[4] & 0xff;
                printf("%d: TI IDAM - head=%d, cyl=%d, sector=%d - %d\n", idamPos, (rawData[1] & 0xff) >> 3, rawData[2] & 0x7f, rawData[4] & 0xff, cellSize);
            } else
                printf("%d: TI IDAM (crc error) - %02X % 02X % 02X % 02X % 02X % 02X - %d\n", idamPos,
                        rawData[1] & 0xff, rawData[2] & 0xff, rawData[3] & 0xff, rawData[4] & 0xff, rawData[5] & 0xff, rawData[6] & 0xff, cellSize);
            break;
        case IDAM:
        case M2FM_IDAM:
        case HP_IDAM:
            idamPos = getByteCnt(0);
            result = getData(matchType, rawData, matchType == HP_IDAM ? 5 : 7);
            if (result == 1) {           // valid so ok
                idam.cylinder = (uint8_t)rawData[1];
                if (matchType == HP_IDAM) {
                    idam.sectorId = (uint8_t)rawData[2];
                    idam.side = 0;
                    idam.sSize = 1;
                } else {
                    idam.side = (uint8_t)rawData[2];
                    idam.sectorId = (uint8_t)rawData[3];
                    sSize = idam.sSize = (uint8_t)rawData[4] & 0xf;
                }
                printf("%d-%d: idam %d/%d/%d sSize %d - %d\n", idamPos, getByteCnt(fromTs), idam.cylinder, idam.side, idam.sectorId, sSize, cellSize);
            } else
                printf("%d: idam %s - %d\n", idamPos, result < 0 ? "premature end\n" : "bad crc\n", cellSize);
            break;
        case TI_DATAAM:
        case DATAAM:
        case M2FM_DATAAM:
        case HP_DATAAM:
            dataPos = getByteCnt(0);

            sectorLen = matchType == TI_DATAAM ? 288 : 128 << sSize;
            result = getData(matchType, rawData, sectorLen + 3);
            if (result >= 0) {
                printf("%d-%d: data len %d%s - %d\n", dataPos, getByteCnt(0), sectorLen, result == 1 ? "" : " bad crc", cellSize);
            } else
                printf("%d: premature end of sector - %d\n", dataPos, cellSize);
            break;
        case DELETEDAM:
        case M2FM_DELETEDAM:
        case HP_DELETEDAM:
            printf("%d: Deleted AM - %d\n", getByteCnt(0), cellSize);
            sectorLen = 128 << sSize;
            getData(matchType, rawData, sectorLen + 3);
            break;
        case 0:
        {
            int n = decode(pattern);
            printf("Data %02X\n", n);
            break;
        }
        }
    }
}