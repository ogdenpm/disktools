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


static bool imdNotPossible = false;     // to track if any track since last reset is in ZDS format

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

static uint16_t hs8Sync(unsigned cylinder, unsigned slot) {
    int matchType;
    makeHS8Patterns(cylinder, slot);

    matchType = matchPattern(30);
    if (curFormat->options == 0 && conflicts[slot] == cylinder && matchType == LSI_SECTOR && getByteCnt() > 12)
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

    int cntSlot = cntHardSectors();
    if (!setInitialFormat(NULL))
        return;
    int sectorSize = 128 << curFormat->sSize;

    initTrack(cylinder, side);
    resetTracker();
    
    for (int profile = 0; !done && retrain(profile); profile++) {
        for (int i = 0; (slot = seekBlock(i)) >= 0; i++) {
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
}

// only up to 32 sector 8" hard sector format supported
static void hs8GetTrack(int cylinder, int side) {
    int slot;
    uint8_t sectorStatus[32] = { 0 };
    unsigned dataPos;
    int matchType;

    bool done = false;
    setInitialFormat("FM8H");

    initTrack(cylinder, 0);
    resetTracker();
    imdNotPossible = false;

    int cntSlot = cntHardSectors();
    for (int profile = 0; !done; profile++) {
        for (int i = 0; (slot = seekBlock(i)) >= 0; i++) {
            if (sectorStatus[slot] && profile != 0 && !(debug & D_NOOPTIMISE))        // skip known good sectors unless D_NOOPTIMISE specified
                continue;
            uint16_t rawData[138];      // sector + cylinder + 128 bytes + 4 links + 2 CRC + 2 postamble
            int result;

            if (!retrain(profile)) {
                done = true;
                break;
            }

            if (matchType = hs8Sync(cylinder, slot)) {
                dataPos = getByteCnt();
                    if (curFormat->options == O_LSI && matchType == ZDS_SECTOR) {    // LSI & ZDS on same track assume all ZDS
                        DBGLOG(D_DECODER, "@%d:%d ZDS sector after LSI sector, rescan assuming ZDS\n", slot, dataPos);
                        setFormat("FM8H-ZDS");
                        updateTrackFmt();
                        memset(sectorStatus, 0, sizeof(sectorStatus));
                        initTrack(cylinder, 0);    // clear any data collected so far assume all ZDS
                        i = -1;                    // restart for a whole rescan
                        continue;

                    }
                if (!curFormat->options) {     // note LSI->ZDS detected above, ZDS->LSI cannot happen
                    setFormat(matchType == LSI_SECTOR ? "FM8H-LSI" : "FM8H-ZDS");
                    updateTrackFmt();
                }
                if (matchType == LSI_SECTOR) {
                    if ((result = getData(slot, rawData, 131)) < 0) {
                        logFull(D_DECODER, "@%d LSI sector %d premature end\n", dataPos, slot);
                        continue;
                    }
                    addSectorData(-slot , result, 130, rawData + 1);
                } else {
                    if ((result = getData((slot << 8) + cylinder + 0x80, rawData, 138)) < 0) {
                        logFull(D_DECODER, "@%d ZDS sector %d premature end\n", dataPos, slot);
                        continue;
                    }
                    addSectorData(-slot, result, 136, rawData + 2);
                }

                DBGLOG(D_DECODER, "@%d-%d %s sector %d%s\n", dataPos, getByteCnt(), matchType == LSI_SECTOR ? "LSI" : "ZDS",
                    slot, result == 1 ? "" : " bad crc");
                sectorStatus[slot] |= result;
            } else
                logFull(D_DECODER, "cannot find start of sector %d\n", slot);

            if (debug & D_DECODER) {
                int val;
                while ((val = getByte()) >= 0)
                    logBasic(" %02X", val);
                logBasic("\n");
                DBGLOG(D_DECODER, "@%u end of sector %d\n", getByteCnt());
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
    imdNotPossible |= curFormat->options & O_NOIMD;
    // add the idams
    idam_t idam = { cylinder, 0, 0, 0 };

    for (int slot = 0; slot < cntSlot; slot++) {
        idam.sectorId = (curFormat->options != O_NSI) ? slot : (lsiInterleave[slot] + cylinder * 8) % 32;
        addIdam(-slot, &idam);
    }
    finaliseTrack();
}

static void ssGetTrack(int cylinder, int side) {

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

    for (int profile = 0; !done && retrain(profile); profile++) {
        for (int i = 1; !done && (seekBlock(i) >= 0); i++) {
            resetTracker();
            bool restart = false;

            while (!restart && (matchType = matchPattern(1200))) {
                switch (matchType) {
                case M2FM_INDEXAM:
                case INDEXAM:           // to note start of track
                    indexPos = getByteCnt();
                    DBGLOG(D_DECODER, "@%d index\n", indexPos);
                    break;
                case IDAM:
                case M2FM_IDAM:
                case HP_IDAM:
                    idamPos = getByteCnt();
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

                        DBGLOG(D_DECODER, "@%d-%d idam %d/%d/%d sSize %d\n", idamPos, getByteCnt(), idam.cylinder, idam.side, idam.sectorId, sSize);
                        addIdam(idamPos, &idam);
                    } else
                        logFull(D_DECODER, "@%d idam %s\n", idamPos, result < 0 ? "premature end\n" : "bad crc\n");
                    break;
                case DATAAM:
                case M2FM_DATAAM:
                case HP_DATAAM:
                    dataPos = getByteCnt();
                    //chkSptChange(dataPos, false);

                    sectorLen = 128 << sSize;

                    result = getData(matchType, rawData, sectorLen + 3);
                    if (result >= 0) {
                        addSectorData(dataPos, result, sectorLen + 2, rawData + 1);     // add data after address mark
                        savedData = true;
                        DBGLOG(D_DECODER, "@%d-%d data len %d%s\n", dataPos, getByteCnt(), sectorLen, result == 1 ? "" : " bad crc");
                    } else
                        logFull(D_DECODER, "@%d premature end of sector\n", dataPos);
                    break;
                case DELETEDAM:
                case M2FM_DELETEDAM:
                case HP_DELETEDAM:
                    logFull(ALWAYS, "@%d Deleted AM\n", getByteCnt());
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
                DBGLOG(D_DECODER, "@%d end of track\n", getByteCnt());
                done = checkTrack(profile);
            }
            retrain(profile);
        }
    }
    finaliseTrack();
}

bool flux2Track(int cylinder, int head) {
    char *fmt = NULL;
    int maxSlot = 0;
    int hs;
    imdNotPossible = false;
    logCylHead(cylinder, head);
    if ((hs = cntHardSectors()) > 0) {
        if (hs != 32 && hs != 16 && hs != 10) {
            logFull(D_ERROR, "Disk has %d Hard Sectors - currently not supported\n", hs);
            return false;
        }

        if (hs == 16 || hs == 10)           // note 16 not yet supported this will only accces DEBUG builds
            hs5GetTrack(cylinder, head);
        else
            hs8GetTrack(cylinder, head);
    } else if (!setInitialFormat(NULL)) {
        logFull(D_ERROR, "Could not determine encoding\n");
        return false;
    } else
        ssGetTrack(cylinder, head);
    return true;
}


bool noIMD() {
    return imdNotPossible;
}