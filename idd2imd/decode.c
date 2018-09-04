#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <ctype.h>
#include "flux.h"

#define INDEX_HOLE_MARK         0
#define ID_ADDRESS_MARK         0xe
#define INDEX_ADDRESS_MARK      0xc
#define DATA_ADDRESS_MARK       0xb
#define DELETED_ADDRESS_MARK    0x8

// byte lengths
#define NOMINAL_LEADIN          46
#define GAP_LEN                 28
#define ID_LEN                  7
#define DATA_LEN                131
#define SECTOR_LEN              (GAP_LEN + ID_LEN + GAP_LEN + DATA_LEN)
#define JITTER_ALLOWANCE        28
#define USCLOCK                 24      // 1 uS clock 

#define BYTES_PER_LINE  16      // for display of data dumps

imd_t curTrack;

int byte2Time(int cnt) {  // return estimated time of cnt bytes
    return (cnt * x8ByteTime() + 4) / 8;
}


int getSlot(int marker) {
    static long indexTime, lastSectorTime, lastSlot;
    long time = when(0);
    int initialSlot = lastSlot;

    switch (marker) {
    case INDEX_HOLE_MARK:
        indexTime = lastSectorTime = lastSlot = 0;
        return 0;
    case INDEX_ADDRESS_MARK:
        indexTime = time;
        return 0;
    case ID_ADDRESS_MARK:
        if (indexTime == 0)
            indexTime = NOMINAL_LEADIN * x8ByteTime() / 8;     // we didn't have an index marker assume one
        if (lastSectorTime == 0) {                         // first id block seen
            if (time - indexTime < byte2Time(GAP_LEN + JITTER_ALLOWANCE)) {
                lastSectorTime = time;
                return lastSlot = 0;
            }
            else {
                lastSectorTime = byte2Time(GAP_LEN + 1) + indexTime;        // estimated time after id marker
                lastSlot = 0;
            }
        }
        while (time - lastSectorTime > byte2Time(SECTOR_LEN - JITTER_ALLOWANCE)) {
            lastSectorTime += byte2Time(SECTOR_LEN);
            lastSlot++;
        }
        lastSectorTime = time;
        break;
    case DELETED_ADDRESS_MARK:
    case DATA_ADDRESS_MARK:
        if (indexTime == 0)
            indexTime = NOMINAL_LEADIN * x8ByteTime() / 8;      // we didn't have an index marker assume one
        if (lastSectorTime == 0) {                          // we didn't have a sector id marker
            if (time - indexTime < byte2Time(GAP_LEN + ID_LEN + GAP_LEN + JITTER_ALLOWANCE)) {
                lastSlot = 0;
                lastSectorTime = time - byte2Time(GAP_LEN + ID_LEN);    // set an effective time;
                return lastSlot;
            }
            else {
                lastSectorTime = byte2Time(GAP_LEN + 1) + indexTime;        // estimated time after id marker
                lastSlot = 0;
            }
        }
        while (time - lastSectorTime > byte2Time(ID_LEN + GAP_LEN + JITTER_ALLOWANCE)) {
            lastSectorTime += byte2Time(SECTOR_LEN);
            lastSlot++;
        }
        lastSectorTime = time - byte2Time(GAP_LEN + ID_LEN);
        break;
    default:
        error("Coding error marker %d parameter error in getSlot\n", marker);

    }
    if (lastSlot - initialSlot > 3)
        logger(MINIMAL, "Warning more than 3 sectors missing\n");

    return lastSlot;
}



bool crcCheck(byte * buf, unsigned length) {
    byte x;
    unsigned short crc = 0;
    if (length < 2)
        return false;
    while (length-- > 2) {
        x = crc >> 8 ^ *buf++;
        x ^= x >> 4;
        crc = (crc << 8) ^ (x << 12) ^ (x << 5) ^ x;
    }

    return crc == (buf[0] * 256 + buf[1]);      // check with recorded crc
}

enum {
    BAD_DATA = 256,
};

// read data into buffer and check crc  ok
// returns bytes read + marker if CRC error or bad flux transition
int getData(int marker, byte *buf, bool cbit) {
    int val = 0;
    int bitCnt = cbit ? 1 : 0;
    int toRead;
    int byteCnt = 0;

    switch (marker) {
    case ID_ADDRESS_MARK:
        toRead = 7;
        break;
    case DATA_ADDRESS_MARK:
    case DELETED_ADDRESS_MARK:
        toRead = 131;
        break;
    case INDEX_ADDRESS_MARK:
        return 0;
    default:
        return 0;
    }

    buf[byteCnt++] = marker;        // record the marker

    do {
        switch (nextBitsM2FM()) {
        case BIT0: val <<= 1; bitCnt++; break;
        case BIT1: val = (val << 1) + 1; bitCnt++; break;
        case BIT00: val <<= 2; bitCnt += 2; break;
        case BIT01: val = (val << 2) + 1; bitCnt += 2; break;

        case BIT0M:         // marker bit illegal in data
        case BIT1S:         // had to resync in data which shouldn't happen
        case BITEND:        // ran out of flux data
        case BITBAD:        // corrupt flux data
            bitLog(BITFLUSH);   // flush every thing
            return byteCnt + BAD_DATA;
        }
        if (bitCnt >= 8) {
            buf[byteCnt++] = bitCnt == 8 ? val : val >> 1;
            bitCnt -= 8;
        }

    } while (byteCnt < toRead);
    bitLog(bitCnt ? BITFLUSH_1 : BITFLUSH);

    return crcCheck(buf, byteCnt) ? byteCnt : byteCnt + BAD_DATA;    // flag if crc error
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
    int bits;

    while ((bits = nextBitsM2FM()) != BITEND) {          // read this track
    /* look for the marker start signature 00MM1 where M is special marker specific 0 */
        if (bits == BIT00 && nextBitsM2FM() == BIT0M && nextBitsM2FM() == BIT0M && nextBitsM2FM() == BIT1)
            switch (nextBitsM2FM()) {
            case BIT1:                              // 00MM11                     
                if ((bits = nextBitsM2FM()) == BIT00) { // 00MM1100 - index address mark
                    bitLog(BITFLUSH);
                    return INDEX_ADDRESS_MARK;
                }
                else if (bits == BIT1 && nextBitsM2FM() == BIT00) {
                    bitLog(BITFLUSH_1);
                    return ID_ADDRESS_MARK;         // 00MM1110 (0) id address mark + preread of next 0 bit
                }
                break;
            case BIT01:
                if (nextBitsM2FM() == BIT1) {             // 00MM1011 - data address mark
                    bitLog(BITFLUSH);
                    return DATA_ADDRESS_MARK;
                }
                break;
            case BIT00:                             // 00MM1000 (0) deleted address mark
                if (nextBitsM2FM() == BIT00) {
                    bitLog(BITFLUSH);
                    return DELETED_ADDRESS_MARK;
                }
            }
    }
    return INDEX_HOLE_MARK;
}



void flux2track() {
    byte dataBuf[131];
    int track = -1, sector;
    int blk = 1;
    int slot;
    int i;
    int marker;
    int missingIdCnt, missingSecCnt;
    byte secToSlot[MAXSECTORS + 1];
    int len;

    
    if (guessFormat() != M2FM)
        return;

    memset(secToSlot, MAXSECTORS, sizeof(secToSlot));
    memset(&curTrack, 0, sizeof(curTrack));

    while (1) {
        if (seekBlock(blk) == 0) {
            if (track == -1) {
                logger(ALWAYS, "No Id markers\n");
                return;
            }
            addIMD(track, &curTrack);
            return;
        }
        logger(MINIMAL, "Processing block %d\n", blk);
        slot = getSlot(INDEX_HOLE_MARK);                    // initialise the pyhsical sector logic

        while ((marker = getMarker()) != INDEX_HOLE_MARK) {          // read this track
            if ((slot = getSlot(marker)) >= MAXSECTORS) {
                logger(ALWAYS, "Physical sector (%d) >= %d\n", slot, MAXSECTORS);
                continue;
            }

            switch (marker) {
            case INDEX_ADDRESS_MARK:
                logger(VERBOSE, "Block %d - Index Address Mark\n", blk);
                break;
            case ID_ADDRESS_MARK:
                if ((len = getData(marker, dataBuf, true)) & BAD_DATA) {
                    logger(curTrack.smap[slot] ? VERBOSE : MINIMAL, "Block %d - Id corrupt for physical sector %d\n", blk, slot);
                    dumpBuf(dataBuf + 1, (len & 0xff) - 1);
                }
                else {
                    logger(VERBOSE, "Block %d - Sector Id - Track = %d, Sector = %d at physical sector %d\n", blk, dataBuf[1], dataBuf[3], slot);
                    dumpBuf(dataBuf + 1, len - 1);
                    if (track != -1 && track != dataBuf[1]) {
                        logger(ALWAYS, "Block %d - Multiple track Ids %d and %d - skipping track", track, dataBuf[1]);
                        return;
                    }
                    track = dataBuf[1];
                    sector = dataBuf[3];
                    if (sector == 0 || sector > MAXSECTORS) {
                        logger(ALWAYS, "Block %d - Ignoring invalid sector number %d at physical sector %d\n", blk, sector, slot);
                        continue;
                    }

                    if (curTrack.smap[slot] && curTrack.smap[slot] != sector) {
                        logger(ALWAYS, "Block %d - Pyhsical sector %d already used by sector %d ignoring allocation to sector %d\n",
                            blk, slot, curTrack.smap[slot], sector);
                        continue;
                    }

                    if (secToSlot[sector] != slot && secToSlot[sector] != MAXSECTORS) {   // physical is 0 to DDSECTORS - 1
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
                if ((len = getData(marker, dataBuf, marker == DELETED_ADDRESS_MARK)) & BAD_DATA) {
                    logger(curTrack.smap[slot] ? VERBOSE : MINIMAL, "Block %d - Data corrupt for physical sector %d\n", blk, slot);
                    dumpBuf(dataBuf + 1, (len & 0xff) - 1);
                }
                else {
                    if (curTrack.smap[slot])
                        logger(VERBOSE, "Block %d - Data for sector %d at physical sector\n", blk, curTrack.smap[slot], slot);
                    else
                        logger(VERBOSE, "Block %d - Data for physical sector %d\n", blk, slot);

                    dumpBuf(dataBuf + 1, len - 1);

                    if (marker == DATA_ADDRESS_MARK) {
                        if (!curTrack.hasData[slot]) {
                            memcpy(curTrack.track + slot * SECTORSIZE, dataBuf + 1, 128);
                            curTrack.hasData[slot] = true;
                        }
                        else if (memcmp(curTrack.track + slot * SECTORSIZE, dataBuf + 1, 128) != 0)
                            logger(ALWAYS, "Block %d - Pyhsical sector %d has different valid sector data\n", blk, slot);
                    }
                }
                break;
            }

        }
        missingIdCnt = missingSecCnt = 0;
        for (i = 0; i < MAXSECTORS; i++) {
            if (!curTrack.smap[i])
                missingIdCnt++;
            if (!curTrack.hasData[i])
                missingSecCnt++;
        }
        ;
        if (missingIdCnt == 0 && missingSecCnt == 0) {
            addIMD(track, &curTrack);
            return;
        }
        logger(MINIMAL, "Missing %d ids and %d sectors after block %d\n", missingIdCnt, missingSecCnt, blk++);
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
        else if (bitCnt > 0 && (bitCnt == BITS_PER_LINE + 1 || bits == BITFLUSH_1)) {
            char tmp = bitBuf[bitCnt - 1];      // last char
            printf("%.*s\n", bitCnt - 1, bitBuf);
            bitCnt = 1;
            bitBuf[0] = tmp;
        }
        switch (bits) {
        case BIT00: bitBuf[bitCnt++] = '0';
        case BIT0:  bitBuf[bitCnt++] = '0'; break;
        case BIT0M: bitBuf[bitCnt++] = 'M'; break;
        case BIT01: bitBuf[bitCnt++] = '0';
        case BIT1:  bitBuf[bitCnt++] = '1'; break;
        case BIT1S: bitBuf[bitCnt++] = 'S'; break;
        case BITEND: bitBuf[bitCnt++] = 'E'; break;
        case BITBAD: bitBuf[bitCnt++] = 'B'; break;
        }

    }
    return bits;
}


#define DGUARD     (USCLOCK * 3 / 5 )     // 60% guard for dbit
#define CGUARD     (USCLOCK * 2 / 5)     // 40% guard for cbit

#define FILTERSIZE  32

int nextBitsM2FM() {
    static bool cbit;              // true if last flux transition was control clock
    static int dbitCnt = 0;
    int val;
    int us;
    static int lastJitter;

    if (when(0) == 0) {            // reset bit extract engine
        cbit = true;
        dbitCnt = 0;
        lastJitter = 0;
        bitLog(BITSTART);
    }

    while (1) {
        val = getNextFlux();
        if (val == END_BLOCK || val == END_FLUX)
            return bitLog(BITEND);

        when(val);              // update running sample time
        us = val / USCLOCK;
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
//        printf("[%3d %d]", val, us);

        switch (us) {
        case 2:
            return bitLog(cbit ? BIT0M : BIT1); // note 0 value is only vaild in markers
        case 3:
            return bitLog(cbit ? BIT00 : BIT1); // previous 0 for cbit already sent
        case 4:
            return bitLog(cbit ? BIT00 : BIT01);
        case 5:
            if (cbit) {
                cbit = false;
                return bitLog(BIT1S);  // must have been a data bit ending so need to resync
            }
            return bitLog(BIT01);
        default:                        // all invalid, but keep cbit updated

            return bitLog(BITBAD);
        }

    }
}

#define BUCKETCNT   12
int guessFormat()
{
    int val;
    int blk = 1;
    int cnt1, cnt2, cnt3, cnt4, cnt5, cntOther, cntTotal;
    int minValid;

    cnt1 = cnt2 = cnt3 = cnt4 = cnt5 = cntOther = cntTotal = 0;

    while (seekBlock(blk++)) {
        while ((val = getNextFlux()) != END_FLUX && val != END_BLOCK) {
            switch ((val + USCLOCK / 2) / USCLOCK) {
            case 1: cnt1++; break;
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
//    printf("cnt1 = %d, cnt2 = %d, cnt3 = %d, cnt4 = %d, cnt5 = %d, cntOther = %d, cntTotal = %d minValid = %d\n",
//        cnt1, cnt2, cnt3, cnt4, cnt5, cntOther, cntTotal, minValid);
    if (cntTotal < MINSAMPLE || cnt1 > minValid || cntOther > minValid
        || cnt2 < minValid || cnt4 < minValid) {
        logger(ALWAYS, "Unknown format\n");
        return UNKNOWN_FMT;
    }
    if (cnt5 > minValid) {
        logger(MINIMAL, "M2FM format\n");
        return M2FM;
    }
    logger(ALWAYS, cnt3 > minValid ? "MFM format\n" : "FM format\n");
    return cnt3 > minValid ? MFM : FM;
}