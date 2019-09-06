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


static int zdsDisk = 0;     // to track if any track since last reset is in ZDS format

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
    buf[i++] = marker & 0xff;
    if (curFormat->options == O_ZDS)                      // ZDS includes track and sector in CRC check
        buf[i++] = marker >> 8;
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
// otherwise check if match is > 10 bytes in assume ZDS if valid sectors hasn't been seen yet
// hopefully one of the other 31 sectors will detect LSI / ZDS if this guess is  wrong


// the cylinder sector conflicts
static uint8_t conflicts[32] = {
    0, 64, 32, 255, 16, 255, 48, 255, 8, 72, 40, 255, 24, 255, 56, 255,
    4, 68, 36, 255, 20, 255, 52, 255, 12, 76, 44, 255, 28, 255, 60, 255
};

static uint16_t hsSync(unsigned cylinder, unsigned slot) {
    int matchType;
    makeHSPatterns(cylinder, slot);

    matchType = matchPattern(30);
    if (curFormat->encoding == E_FM8H && conflicts[slot] == cylinder && matchType == LSI_SECTOR && getByteCnt() > 10)
        matchType = matchPattern(2);
    return matchType;

}

static void hsGetTrack(int cylinder, int side) {
    int slot;
    uint8_t sectorStatus[32] = { 0 };
    int savedOptions;
    unsigned dataPos;
    unsigned dataTrackPos;
    int matchType;


    setInitialFormat("FM8H");
    initTrack(cylinder, 0);
    resetTracker();

    int cntSlot = cntHardSectors();

    // create the IDAM info
    for (int slot = 0; slot < cntSlot; slot++) {
        idam_t idam = { cylinder, 0, slot, 0 };
        addIdam(slot * curFormat->spacing + HSIDAM, &idam);
    }

    for (int i = 0; (slot = seekBlock(i)) >= 0; i++) {
        if (sectorStatus[slot] && !(debug & D_NOOPTIMISE))        // skip known good sectors unless D_NOOPTIMISE specified
            continue;

        uint16_t rawData[138];      // sector + cylinder + 128 bytes + 4 links + 2 CRC + 2 postamble
        int result;

        retrain(0);

        if (matchType = hsSync(cylinder, slot)) {
            dataPos = getByteCnt();
            if (curFormat->options == O_LSI && matchType == ZDS_SECTOR) {    // LSI & ZDS on same track assume all ZDS
                    DBGLOG(D_DECODER, "@%d ZDS sector after LSI sector, rescan assuming ZDS\n", dataTrackPos);
                    setFormat("FM8H-ZDS");
                    initTrack(cylinder, 0);    // clear any data collected so far assume all ZDS
                    i = -1;                    // restart for a whole rescan
                    continue;

            }
            if (!(savedOptions = curFormat->options)) {     // note LSI->ZDS detected above, ZDS->LSI cannot happen
                setFormat(matchType == LSI_SECTOR ? "FM8H-LSI" : "FM8H-ZDS");
                updateTrackFmt();
            }
            dataTrackPos = dataPos + curFormat->spacing * slot;       // adjust to track position

            if (matchType == LSI_SECTOR) {
                if ((result = getData(slot, rawData, 131)) < 0) {
                    logFull(D_DECODER, "@%d LSI sector %d premature end\n", dataPos, slot);
                    continue;
                }
                addSectorData(dataTrackPos, result, 130, rawData + 1);
            } else {
                if ((result = getData((cylinder << 8) + slot + 0x80, rawData, 138)) < 0) {
                    logFull(D_DECODER, "@%d ZDS sector %d premature end\n", dataPos, slot);
                    continue;
                }
                addSectorData(dataTrackPos, result, 136, rawData + 2);
            }
            if (result != 1 && !savedOptions) {     // restore previous format if different
                setFormat("FM8H");
                updateTrackFmt();
            }
            DBGLOG(D_DECODER, "@%d-%d %s sector %d%s\n", dataPos, getByteCnt(), matchType == LSI_SECTOR ? "LSI" : "ZDS",
                slot, result == 1 ? "" : " bad crc");
            sectorStatus[slot] |= result;
        } else
            logFull(D_DECODER, "cannot find start of sector %d\n", slot);

        if (debug & D_DECODER) {
            int val;
            while ((val = getByte()) >= 0)
                DBGLOG(D_DECODER, " %02X", val);
            DBGLOG(D_DECODER, "@%d end of sector %d\n", getByteCnt());
        }

    }
    zdsDisk |= curFormat->options & O_ZDS;

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

    bool done = false;


    initTrack(cylinder, side);
    unsigned sSize = curFormat->sSize;


    for (int i = 1; !done && (seekBlock(i) >= 0); i++) {
        retrain(0);
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
            done = checkTrack();
        }
    }
    finaliseTrack();
}

void clearZDS() {
    zdsDisk = 0;
}

bool flux2Track(int cylinder, int head) {
    char *fmt = NULL;
    int maxSlot = 0;
    int hs;
    logCylHead(cylinder, head);
    if ((hs = cntHardSectors()) > 0) {
        if (head != 0 || hs != 32) {
            logFull(D_ERROR, "%d/%d Hard Sector disk only supports single side\n", cylinder, head);
            return false;
        } else if (hs != 32) {
            logFull(D_ERROR, "Disk has %d Hard Sectors - only 32 sectors currently supported\n", hs);
            return false;
        }
        hsGetTrack(cylinder, head);
    } else if (!setInitialFormat(NULL)) {
        logFull(D_ERROR, "Could not determine encoding\n");
        return false;
    } else
        ssGetTrack(cylinder, head);
    return true;
}


bool isZDS() {
    return zdsDisk;
}