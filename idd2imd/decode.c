#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <ctype.h>
#include "flux.h"

enum {
    INDEX_HOLE_MARK,
    INDEX_ADDRESS_MARK,
    ID_ADDRESS_MARK,
    DATA_ADDRESS_MARK,
    DELETED_ADDRESS_MARK
};



#define JITTER_ALLOWANCE        10


#define BYTES_PER_LINE  16      // for display of data dumps

struct {
    char    *name;              // format name
    int     (*getBit)();        // get next bit routine
    word    indexAM;            // index address marker -- markers are stored with marker sig in high byte, marker in low
    word    idAM;               // id address marker
    word    dataAM;             // data address marker
    word    deletedAM;          // deleted data address marker
    short   fixedSector;        // min size of sector excluding data
    short   gap4;               // min gap4 size
    byte    imdMode;            // mode byte for IMD image
    double  byteClock;          // 1 byte in samples
    unsigned short crcInit;
} *curFmt, formats[] = {
    {"FM",     getFMBit, 0x28fc, 0x38fe, 0x38fb, 0x38f8, 60, 170, 0, 32 * SCK / 1000000.0, 0xffff},
    {"MFM",        NULL, 0x200d, 0x200a, 0x200b, 0x2005, 60, 240, 3, 16 * SCK / 1000000.0, 0},
    {"M2FM", getM2FMBit, 0x300c, 0x300e, 0x300b, 0x3008, 60, 240, 3, 16 * SCK / 1000000.0, 0},
    {"Unknown"}
};

struct {
    long slotByteNumber;
    byte slot;
    int interMarkerByteCnt;
    int interSectorByteCnt;

} slotInfo;

imd_t curTrack;

int time2Byte(long time) {
    return (int)(time / curFmt->byteClock + 0.5);
}


int getSlot(int marker) {
    int t = time2Byte(when(0));
    int slotInc;
 
    switch (marker) {
    case INDEX_HOLE_MARK:
        slotInfo.slotByteNumber = 0;           // reset for new track copy
        slotInfo.slot = 0;
        break;
    case DELETED_ADDRESS_MARK:
    case DATA_ADDRESS_MARK:
        if (t - slotInfo.slotByteNumber < 128)  // less than a sector away
            return slotInfo.slot;
        t -= slotInfo.interMarkerByteCnt;
    case ID_ADDRESS_MARK:
        if (slotInfo.slotByteNumber == 0) {
            slotInfo.slotByteNumber = t;              // back up to slot 0 if first sector missing
            while (slotInfo.slotByteNumber > slotInfo.interSectorByteCnt)
                slotInfo.slotByteNumber -= slotInfo.interSectorByteCnt;
            slotInfo.slot = 0;
        }
        for (slotInc = 0; t - slotInfo.slotByteNumber > slotInfo.interSectorByteCnt - JITTER_ALLOWANCE; slotInc++)
            slotInfo.slotByteNumber += slotInfo.interSectorByteCnt;
        if (slotInc > 3)
            logger(ALWAYS, "More than 3 consecutive sectors missing\n");
        slotInfo.slot += slotInc;
        if (slotInc != 0)
            slotInfo.slotByteNumber = t;          // reset base point
    }
    return slotInfo.slot;
}


bool crcCheck(byte * buf, unsigned length) {
    byte x;
    unsigned short crc = curFmt->crcInit;
    if (length < 2)
        return false;
    while (length-- > 2) {
        x = crc >> 8 ^ *buf++;
        x ^= x >> 4;
        crc = (crc << 8) ^ (x << 12) ^ (x << 5) ^ x;
    }

    return crc == (buf[0] * 256 + buf[1]);      // check with recorded crc
}

// read data into buffer and check crc  ok
// returns bytes read + marker if CRC error or bad flux transition
int getData(int marker, byte *buf) {
    int toRead;
    int byteCnt = 0;
    int val;

    switch (marker) {
    case ID_ADDRESS_MARK:
        marker = curFmt->idAM & 0xff;      // replace with real marker to make crc work
        toRead = 7;
        break;
    case DATA_ADDRESS_MARK:
        marker = curFmt->dataAM & 0xff;
        toRead = 3 + curTrack.size;
        break;
    case DELETED_ADDRESS_MARK:
        marker = curFmt->deletedAM & 0xff;
        toRead = 3 + curTrack.size;
        break;
    default:
        return 0;
    }

    for (buf[byteCnt++] = marker; byteCnt < toRead; buf[byteCnt++] = val) {
        val = 0;
        for (int bitCnt = 0; bitCnt < 8; bitCnt++)
            switch (curFmt->getBit()) {
            case BIT0: val <<= 1; break;
            case BIT1: val = (val << 1) + 1; break;
            default:
                bitLog(BITFLUSH);
                return -byteCnt;
            }
    }
    bitLog(BITFLUSH);

    return crcCheck(buf, byteCnt) ? byteCnt : -byteCnt;    // flag if crc error
}



void dumpBuf(byte *buf, int length) {
    char text[BYTES_PER_LINE + 1];

    if (debug < VERYVERBOSE)
        return;

    for (int i = 0; i < length; i += BYTES_PER_LINE) {
        printf("%04x:", i);
        for (int j = 0; j < BYTES_PER_LINE; j++)
            if (i + j < length) {
                printf(" %02X", buf[i + j]);
                if (' ' <= buf[i + j] && buf[i + j] <= '~')
                    text[j] = buf[i + j];
                else
                    text[j] = '.';
            }
            else {
                printf(" --");
                text[j] = 0;
            }
        text[BYTES_PER_LINE] = 0;
        printf(" |%s|\n", text);
    }
}





int getMarker() {
    byte marker = 0;   // marker sig bits
    byte am = 0;       // marker value
    int bit;

    while ((bit = curFmt->getBit()) != BITEND) {        // interleave marker & data bits
       marker <<= 1;
       am <<= 1;

        switch (bit) {
        case BIT1M: marker++;
        case BIT1:  am++; break;
        case BIT0M: marker++;
        case BIT0:  break;
        case BIT1S: marker = 0; am = 1;  break;
        default:    marker = 0; am = 0;  break;         // bad so reset
        }

        word am16 = (marker << 8) + am;
        if (am16 == curFmt->idAM)
            am = ID_ADDRESS_MARK;
        else if (am16 == curFmt->dataAM)
            am = DATA_ADDRESS_MARK;
        else if (am16 == curFmt->indexAM)
            am = INDEX_ADDRESS_MARK;
        else if (am16 == curFmt->deletedAM)
            am = DELETED_ADDRESS_MARK;
        else
            continue;

        bitLog(BITFLUSH);           // make sure we start a new bit stream if logging
        return am;
    }
    bitLog(BITFLUSH);
    return INDEX_HOLE_MARK;
}


void flux2track() {
    byte secToSlot[MAXSECTORS + 1];     // used to check for same sector at different locations

    if (!analyseFormat())
        return;
    /*
        here track, head and size have been determined
        and curFmt points to the disk format slotInfo info
    */
    memset(secToSlot, curTrack.spt, sizeof(secToSlot));


    for (int blk = 1; seekBlock(blk); blk++) {
        int marker, sector, slot, len;

        byte dataBuf[3 + MAXSECTORSIZE];        // includes AM and CRC

        if (seekBlock(blk) == 0) {
            addIMD(&curTrack);
            return;
        }
        logger(VERBOSE, "Processing block %d\n", blk);
        slot = getSlot(INDEX_HOLE_MARK);                    // initialise the pyhsical sector logic

        while ((marker = getMarker()) != INDEX_HOLE_MARK) {          // read this track
            if ((slot = getSlot(marker)) >= curTrack.spt) {
                logger(ALWAYS, "Physical sector (%d) >= %d\n", slot, curTrack.spt);
                continue;
            }

            switch (marker) {
            case INDEX_ADDRESS_MARK:
                logger(VERBOSE, "Block %d - Index Address Mark\n", blk);
                break;
            case ID_ADDRESS_MARK:
                if ((len = getData(marker, dataBuf)) < 0) {
                    logger(curTrack.smap[slot] ? VERBOSE : MINIMAL, "Block %d - Id corrupt for physical sector %d\n", blk, slot);
                    dumpBuf(dataBuf + 1, -len - 1);
                }
                else {
                    logger(VERBOSE, "Block %d - Sector Id - Track = %d, Sector = %d at physical sector %d\n", blk, dataBuf[1], dataBuf[3], slot);
                    dumpBuf(dataBuf + 1, len - 1);
                    if (curTrack.cyl != dataBuf[1] || curTrack.head != dataBuf[2]) {
                        logger(ALWAYS, "Block %d - Different Cyl/Head %d/%d and %d/%d- skipping track",
                            curTrack.cyl, curTrack.head, dataBuf[1], dataBuf[2]);
                        return;
                    }
                    sector = dataBuf[3];
                    if (sector == 0 || sector > curTrack.spt) {
                        logger(ALWAYS, "Block %d - Ignoring invalid sector number %d at physical sector %d\n", blk, sector, slot);
                        continue;
                    }

                    if (curTrack.smap[slot] && curTrack.smap[slot] != sector) {
                        logger(ALWAYS, "Block %d - Pyhsical sector %d already used by sector %d ignoring allocation to sector %d\n",
                            blk, slot, curTrack.smap[slot], sector);
                        continue;
                    }

                    if (secToSlot[sector] != slot && secToSlot[sector] != curTrack.spt) {   // physical is 0 to spt - 1
                        logger(ALWAYS, "Block %d - Sector %d already allocated to pyhsical sector %d ignoring allocation to physical sector %d\n",
                            blk, curTrack.smap[slot], slot, sector);
                        continue;

                    }
                    curTrack.smap[slot] = sector;
                    secToSlot[sector] = slot;
                }
                break;
            case DELETED_ADDRESS_MARK:
                logger(ALWAYS, "Block %d - Deleted Data Marker seen at pyhsical sector %d\n", blk, slot);
            case DATA_ADDRESS_MARK:
                if ((len = getData(marker, dataBuf)) < 0) {
                    logger(curTrack.smap[slot] ? VERBOSE : MINIMAL, "Block %d - Data corrupt for physical sector %d\n", blk, slot);
                    dumpBuf(dataBuf + 1, -len - 1);
                }
                else {
                    if (curTrack.smap[slot])
                        logger(VERBOSE, "Block %d - Data for sector %d at physical sector %d\n", blk, curTrack.smap[slot], slot);
                    else
                        logger(VERBOSE, "Block %d - Data for physical sector %d\n", blk, slot);

                    dumpBuf(dataBuf + 1, len - 1);

                    if (marker == DATA_ADDRESS_MARK) {
                        if (!curTrack.hasData[slot]) {
                            memcpy(curTrack.track + slot * curTrack.size, dataBuf + 1, curTrack.size);
                            curTrack.hasData[slot] = true;
                        }
                        else if (memcmp(curTrack.track + slot * curTrack.size, dataBuf + 1, curTrack.size) != 0)
                            logger(ALWAYS, "Block %d - Pyhsical sector %d has different valid sector data\n", blk, slot);
                    }
                }
                break;
            }

        }


        int missingIdCnt = 0, missingSecCnt = 0;

        for (int i = 0; i < curTrack.spt; i++) {
            if (!curTrack.smap[i])
                missingIdCnt++;
            if (!curTrack.hasData[i])
                missingSecCnt++;
        }
        ;
        if (missingIdCnt == 0 && missingSecCnt == 0) {
            addIMD(&curTrack);
            return;
        }
        logger(MINIMAL, "Missing %d ids and %d sectors trying block %d\n", missingIdCnt, missingSecCnt, blk + 1);
    }
}


#define BITS_PER_LINE   80


int bitLog(int bits) {
    static int bitCnt;
    static char bitBuf[BITS_PER_LINE + 1];
    if (debug > VERYVERBOSE) {
        if (bits == BITSTART)
            bitCnt = 0;
        else if (bitCnt && (bitCnt == BITS_PER_LINE || bits == BITFLUSH)) {
            printf("%.*s\n", bitCnt, bitBuf);
            bitCnt = 0;
        }

        if (BIT0 <= bits && bits <= BITBAD) {
            bitBuf[bitCnt++] = "0m1MsSEB"[bits - 1];
            if (bits >= BIT0S) {                // resync or bad causes flush
                printf("%.*s\n", bitCnt, bitBuf);
                bitCnt = 0;
            }
        }

    }
    return bits;
}


#define DGUARD     (USCLOCK * 3 / 5 )     // 60% guard for dbit
#define CGUARD     (USCLOCK * 2 / 5)     // 40% guard for cbit

int getM2FMBit() {
    static bool cbit;              // true if last flux transition was control clock
    static int dbitCnt = 0;
    static int queuedBit = 0;
    static int lastJitter;

    if (when(0) == 0) {            // reset bit extract engine
        cbit = true;
        dbitCnt = lastJitter = 0;
        queuedBit = NONE;
        bitLog(BITSTART);
    } else if (queuedBit) {
        int tmp = queuedBit;
        queuedBit = NONE;
        return bitLog(tmp);
    }

    while (1) {
        int val = getNextFlux();

        if (val == END_BLOCK || val == END_FLUX)
            return bitLog(BITEND);

        when(val);              // update running sample time

        int us = val / USCLOCK;

        if (us & 1)
            cbit = !cbit;

        if (val % USCLOCK + lastJitter > USCLOCK - (cbit ? DGUARD : CGUARD)) {   // check for early D or C bit
            us++;
            cbit = !cbit;
        }

        lastJitter = (val - us * USCLOCK) / 2;      // compensation for jitter
        // use consecutive 1s in the GAPs to sync clock i.e. pulses every 2us
        if (us != 2)                  // use consecutive 1s in the GAPs to sync clock
            dbitCnt = 0;
        else if (++dbitCnt == 4) {          // sync cbit
            dbitCnt = 0;
            if (cbit) {
                cbit = false;
                return bitLog(BIT1S);       // flag a resync
            }
        }
//      printf("[%3d %d]", val, us);

        switch (us) {
        case 2:
            return bitLog(cbit ? BIT0M : BIT1); // note 0 value is only vaild in markers
        case 3:
            if (!cbit)
                return bitLog(BIT1);            // previous 0 for cbit already sent
            queuedBit = BIT0;
            return bitLog(BIT0);
        case 4:
            queuedBit = (cbit ? BIT0 : BIT1);
            return bitLog(BIT0);
        case 5:
            if (cbit) {
                cbit = false;
                return bitLog(BIT1S);  // must have been a data bit ending so need to resync
            }
            queuedBit = BIT1;
            return bitLog(BIT0);
        default:                        // all invalid, but keep cbit updated
            return bitLog(BITBAD);
        }

    }
}


int getFMBit()
{
    static bool cbit;              // true if last flux transition was control clock
    static int cbitCnt = 0;
    int val;
    int us;
    static int lastJitter;

    while (1) {
        val = getNextFlux();
        if (val == END_BLOCK || val == END_FLUX)
            return bitLog(BITEND);

        when(val);              // update running sample time
        us = (val + USCLOCK / 2) / USCLOCK;     // simple rounding

        // use consecutive 0s in the GAPs to sync clock i.e. pulses every 4us
        if (us != 4)                  // use consecutive 0s in the GAPs to sync clock
            cbitCnt = 0;
        else if (++cbitCnt == 5) {          // sync cbit
            cbitCnt = 0;
            if (!cbit) {
                cbit = true;
                return bitLog(BIT0S);       // flag a resync
            }
        }

        switch (us) {
        case 2:
            if (!(cbit = !cbit))
                return bitLog(BIT1);
            break;
        case 4:
            return bitLog(cbit ? BIT0 : BIT1M);
        default:                        // all invalid, but keep cbit updated
            return bitLog(BITBAD);
        }

    }
}

bool analyseFormat()
{
    int val;
    int blk = 1;
    int cnt2, cnt3, cnt4, cnt5, cntOther, cntTotal;
    int minValid;

    memset(&slotInfo, 0, sizeof(slotInfo));     // make sure we start with fresh info
    memset(&curTrack, 0, sizeof(curTrack));

    /*
        look at the profile of flux transitions to work out
        the disk encoding format
    */
    cnt2 = cnt3 = cnt4 = cnt5 = cntOther = cntTotal = 0;

    while (seekBlock(blk++)) {
        while ((val = getNextFlux()) != END_FLUX && val != END_BLOCK) {
            switch ((val + USCLOCK / 2) / USCLOCK) {
            case 2: cnt2++; break;
            case 3: cnt3++; break;
            case 4: cnt4++; break;
            case 5: cnt5++; break;
            default: cntOther++;
            }
            cntTotal++;
        }
    }
    if (cntTotal > 0)
        minValid = cntTotal / SAMPLESCALER;
    //    printf("cnt2 = %d, cnt3 = %d, cnt4 = %d, cnt5 = %d, cntOther = %d, cntTotal = %d minValid = %d\n",
    //        cnt1, cnt2, cnt3, cnt4, cnt5, cntOther, cntTotal, minValid);
    if (cntTotal < MINSAMPLE || cntOther > minValid || cnt2 < minValid || cnt4 < minValid)
        curFmt = &formats[UNKNOWN_FMT];
    else if (cnt5 > minValid)
        curFmt = &formats[M2FM];
    else if (cnt3 > minValid)
        curFmt = &formats[MFM];
    else
        curFmt = &formats[FM];


    if (curFmt->getBit == NULL) {
        logger(ALWAYS, "%s format - not supported\n", curFmt->name);
        return false;
    }
    /* we have the imd mode so record it */
    curTrack.mode = curFmt->imdMode;

    /*
        determine the sector sizing and the timing between sectors
        also timing from id address marker to data address marker
        finally estimate the sectors per track
    */
    int savDebug = debug;       // surpress debug info
//    debug = 0;
    seekBlock(1);               // look at first track copy
    

    int sectorStart = 0, firstSector = 0, lastSectorStart = 0;

    int marker;
    byte buf[7];
    bool haveSize = false;
    /* scan the whole track or until we can determine the sector and interMarker timings */
    while ((marker = getMarker()) != INDEX_HOLE_MARK && (slotInfo.interSectorByteCnt == 0 || slotInfo.interMarkerByteCnt == 0)) {
        if (marker == ID_ADDRESS_MARK) {
            lastSectorStart = sectorStart;
            sectorStart = time2Byte(when(0));
            if (firstSector == 0)
                firstSector = sectorStart;
            if (!haveSize && getData(marker, buf)) {
                curTrack.size =  128 << (buf[4] & 0x7f);    // FM used 128, convert to newer form
                haveSize = true;
            }
            if (haveSize && lastSectorStart != 0 && slotInfo.interSectorByteCnt == 0
              && sectorStart - lastSectorStart < curFmt->fixedSector + (128 << curTrack.size) * 2) // less than 2 sectors
                slotInfo.interSectorByteCnt = sectorStart - lastSectorStart;
        }
        else if (marker == DATA_ADDRESS_MARK && slotInfo.interMarkerByteCnt == 0
              && time2Byte(when(0)) < sectorStart + 128)      // less than minimum size sector
            slotInfo.interMarkerByteCnt = time2Byte(when(0)) - sectorStart;
    }
    if (slotInfo.interMarkerByteCnt == 0 || slotInfo.interSectorByteCnt == 0) {
        logger(ALWAYS, "Couldn't determine sector timing\n");
        debug = savDebug;
        return false;
    }
    
    /* if we get here buf holds the cylinder, head and size info. fill in the missing info for curTrack */
#pragma warning(suppress: 6001)
    curTrack.cyl = buf[1];
    curTrack.head = buf[2];
    /*
        now find out spt, using get slot to do the hard work of tracking any missing sectors
    */
    int trackLen = time2Byte(seekBlock(1));
    getSlot(INDEX_HOLE_MARK);
    while ((marker = getMarker()) != INDEX_HOLE_MARK)
        curTrack.spt = getSlot(marker);
    do {
        curTrack.spt++;             // add at least 1 to convert from 0 base
    } while ((slotInfo.slotByteNumber += slotInfo.interSectorByteCnt) < trackLen - slotInfo.interSectorByteCnt - curFmt->gap4);

    debug = savDebug;               // restore debug options


    // if we get here then buf holds
    logger(MINIMAL, "%s format - cylinder %d, head %d, sector size %d, sectors per track %d\n",
        curFmt->name, curTrack.cyl, curTrack.head, curTrack.size, curTrack.spt);
    if (curTrack.spt > MAXSECTORS) {
        logger(ALWAYS, "Sector count %d too large - setting to %d\n", curTrack.spt, MAXSECTORS);
        curTrack.spt = MAXSECTORS;
    }
    return true;
}