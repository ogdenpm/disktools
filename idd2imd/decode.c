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

#define JITTER_ALLOWANCE        20

#define BYTES_PER_LINE  16      // for display of data dumps

typedef struct {
    char    *name;              // format name
    int(*getBit)(bool resync);        // get next bit routine
    int(*getMarker)();     // get marker routine
    word    indexAM;            // index address marker -- markers are stored with marker sig in high byte, marker in low
    word    idAM;               // id address marker
    word    dataAM;             // data address marker
    word    deletedAM;          // deleted data address marker
    short   fixedSector;        // min size of sector excluding data
    short   gap4;               // min gap4 size
    byte    imdMode;            // mode byte for IMD image
    byte    profile;            // disk profile
    byte    clockX2;            // 2 x clock in samples for 500kbs this is USCLOCK
    double  byteClock;          // 1 byte in samples
    unsigned short crcInit;
} format_t;

enum {
    BPS500 = 0, BPS300, BPS250
};

enum {      // profiles
    UNKNOWN,
    FM250, FM300, FM500,
    MFM250, MFM300, MFM500,
    M2FM500

};
format_t *curFmt, formats[] = {
    {"FM 500 kbps",      getFMBit, getMarkerOld, 0x28fc, 0x38fe, 0x38fb, 0x38f8, 60, 170, 0,   FM500,     USCLOCK, 32 * USCLOCK_D, 0xffff},
    {"FM 250 kbps",      getFMBit, getMarkerOld, 0x28fc, 0x38fe, 0x38fb, 0x38f8, 60, 170, 2,   FM250, 2 * USCLOCK, 64 * USCLOCK_D, 0xffff},
    {"MFM 500 kbps",         NULL, getMarkerOld, 0x200d, 0x200a, 0x200b, 0x2005, 60, 240, 3,  MFM500,     USCLOCK, 16 * USCLOCK_D, 0},
    {"MFM 250 kbps", getMFMBitNew, getMarkerNew, 0x01fc, 0x00fe, 0x00fb, 0x00f8, 60, 240, 5,  MFM250, 2 * USCLOCK, 32 * USCLOCK_D, 0xcdb4},
    {"M2FM 500 kpbs",  getM2FMBit, getMarkerOld, 0x300c, 0x300e, 0x300b, 0x3008, 60, 240, 3, M2FM500,     USCLOCK, 16 * USCLOCK_D, 0},

    {"Unknown"}
};

struct {
    long slotByteNumber;
    byte slot;
    int indexMarkerByteCnt;
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

 //   printf("%d - %d -", marker, t);
    switch (marker) {
    case INDEX_HOLE_MARK:
        slotInfo.slotByteNumber = 0;           // reset for new track copy
        slotInfo.slot = 0;
        break;
    case DELETED_ADDRESS_MARK:
    case DATA_ADDRESS_MARK:
        if (t - slotInfo.slotByteNumber < 128)   // less than a sector away
            break;
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
            logger(ALWAYS, "Warning %d consecutive sectors missing\n", slotInc);
        slotInfo.slot += slotInc;
        if (slotInc != 0)
            slotInfo.slotByteNumber = t;          // reset base point
    }
//    printf(" %d\n", slotInfo.slot);
    return slotInfo.slot;
}

bool crcCheck(byte * buf, unsigned length) {
    byte x;
    unsigned short crc = curFmt->crcInit;
    if (length < 2)
        return false;
    while (length-- > 2) {
        x = (crc >> 8) ^ *buf++;
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
            switch (curFmt->getBit(false)) {
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

int getMarkerOld() {
    byte marker = 0;   // marker sig bits
    byte am = 0;       // marker value
    int bit;

    while ((bit = curFmt->getBit(true)) != BITEND) {        // interleave marker & data bits
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
        if (marker) {
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
    }
    bitLog(BITFLUSH);
    return INDEX_HOLE_MARK;
}

int getMarkerNew() {
    byte marker = 0;   // marker sig bits
    byte am = 0;       // marker value
    int  bitCnt = 9;
    bool isIndex = false;
    int bit;

    while ((bit = curFmt->getBit(true)) != BITEND) {        // interleave marker & data bits
        marker <<= 1;
        am <<= 1;
        bitCnt++;

        switch (bit) {
        case BIT1M: marker++;
        case BIT1:  am++; break;
        case BIT0M: marker++;
        case BIT0:  break;
        case BIT1S: marker = 0; am = 1;  break;
        default:    marker = 0; am = 0;  break;         // bad so reset
        }
        if (marker == 8 && am == 0xc2) {
            isIndex = true;
            bitCnt = 0;
            bitLog(BITFLUSH);
        }
        else if (marker == 4 && am == 0xa1) {
            isIndex = false;
            bitCnt = 0;
        }
        else if (bitCnt == 8 && marker == 0) {
            word am16 = (isIndex << 8) + am;
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
    }
    bitLog(BITFLUSH);
    return INDEX_HOLE_MARK;
}

void flux2track() {
    byte secToSlot[MAXSECTORS + 1];     // used to check for same sector at different locations
    int trackLen;
    int missingIdCnt = 1, missingSecCnt = 1;        // forces at least one pass

    if (!analyseFormat())
        return;
    /*
        here track, head and size have been determined
        and curFmt points to the disk format info
        and slotInto has been set up
    */
    memset(secToSlot, 0xff, sizeof(secToSlot));

    for (int blk = 1; (missingIdCnt || missingSecCnt) && (trackLen = time2Byte(seekBlock(blk))); blk++) {
        int marker, sector, slot, len;

        byte dataBuf[3 + MAXSECTORSIZE];        // includes AM and CRC

        logger(VERBOSE, "Processing block %d\n", blk);
        slot = getSlot(INDEX_HOLE_MARK);                    // initialise the pyhsical sector logic

        while ((marker = curFmt->getMarker()) != INDEX_HOLE_MARK) {          // read this track
            if ((slot = getSlot(marker)) >= (curTrack.spt ? curTrack.spt : MAXSECTORS)) {
                logger(ALWAYS, "Physical sector (%d) >= %d\n", slot, curTrack.spt ? curTrack.spt : MAXSECTORS);
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
                    if (sector == 0 || sector > MAXSECTORS) {
                        logger(ALWAYS, "Block %d - Ignoring invalid sector number %d at physical sector %d\n", blk, sector, slot);
                        continue;
                    }

                    if (curTrack.smap[slot] && curTrack.smap[slot] != sector) {
                        logger(ALWAYS, "Block %d - Pyhsical sector %d already used by sector %d ignoring allocation to sector %d\n",
                            blk, slot, curTrack.smap[slot], sector);
                        continue;
                    }

                    if (secToSlot[sector] != slot && secToSlot[sector] != 0xff) {
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
        /* work out spt if not defined i.e. after processing block 1*/
        if (curTrack.spt == 0) {
            curTrack.spt = slotInfo.slot;
            do {
                curTrack.spt++;             // add at least 1 to convert from 0 base
            } while ((slotInfo.slotByteNumber += slotInfo.interSectorByteCnt) < trackLen - slotInfo.interSectorByteCnt - curFmt->gap4);

            logger(MINIMAL, "%s - %d/%d %d x %d\n",
                curFmt->name, curTrack.cyl, curTrack.head, curTrack.size, curTrack.spt);
            if (curTrack.spt > MAXSECTORS) {
                logger(ALWAYS, "Sector count %d too large - setting to %d\n", curTrack.spt, MAXSECTORS);
                curTrack.spt = MAXSECTORS;
            }
        }

        missingIdCnt = missingSecCnt = 0;
        for (int i = 0; i < curTrack.spt; i++) {
            if (!curTrack.smap[i])
                missingIdCnt++;
            if (!curTrack.hasData[i])
                missingSecCnt++;
        }
        ;
        if (missingIdCnt || missingSecCnt)
            logger(MINIMAL, "Missing %d ids and %d sectors trying block %d\n", missingIdCnt, missingSecCnt, blk + 1);
    }
    addIMD(&curTrack);
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

#define DGUARD     (curFmt->clockX2 * 3 / 5 )     // 60% guard for dbit
#define CGUARD     (curFmt->clockX2 * 2 / 5)     // 40% guard for cbit

int getM2FMBit(bool resync) {
    static bool cbit;              // true if last flux transition was control clock
    static int dbitCnt = 0;
    static int queuedBit = 0;
    static int lastJitter;

    if (when(0) == 0) {            // reset bit extract engine
        cbit = true;
        dbitCnt = lastJitter = 0;
        queuedBit = NONE;
        bitLog(BITSTART);
    }
    else if (queuedBit) {
        int tmp = queuedBit;
        queuedBit = NONE;
        return bitLog(tmp);
    }

    while (1) {
        int val = getNextFlux();

        if (val == END_BLOCK || val == END_FLUX)
            return bitLog(BITEND);

        when(val);              // update running sample time

        int us = val / curFmt->clockX2;

        if (us & 1)
            cbit = !cbit;

        if (val % curFmt->clockX2 + lastJitter > curFmt->clockX2 - (cbit ? DGUARD : CGUARD)) {   // check for early D or C bit
            us++;
            cbit = !cbit;
        }

        lastJitter = (val - us * curFmt->clockX2) / 2;      // compensation for jitter
        // use consecutive 1s in the GAPs to sync clock i.e. pulses every 2us
        if (!resync || us != 2)                  // use consecutive 1s in the GAPs to sync clock
            dbitCnt = 0;
        else if (++dbitCnt == 4) {          // sync cbit
            dbitCnt = 0;
            if (cbit) {
                cbit = false;
                return bitLog(BIT1S);       // flag a resync
            }
        }

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

int getMFMBitNew(bool resync) {
    static bool cbit;              // true if last flux transition was control clock
    static int syncPattern = 0;
    static int cnt2 = 0;
    static int queuedBit = 0;
    static int lastJitter;

    if (when(0) == 0) {            // reset bit extract engine
        cbit = false;
        syncPattern = lastJitter = 0;
        queuedBit = NONE;
        bitLog(BITSTART);
        cnt2 = 0;
    }
    else if (queuedBit) {
        int tmp = queuedBit;
        queuedBit = NONE;
        return bitLog(tmp);
    }

    while (1) {
        int val = getNextFlux();

        if (val == END_BLOCK || val == END_FLUX)
            return bitLog(BITEND);

        if (val < curFmt->clockX2) {
            int tmp;
            val += (tmp = getNextFlux());
            if (tmp == END_BLOCK || tmp == END_FLUX)
                return bitLog(BITEND);
        }
            
        when(val);              // update running sample time

        int us = val / curFmt->clockX2;

        if (us & 1)
            cbit = !cbit;

        if (val % curFmt->clockX2 + lastJitter > curFmt->clockX2 - (cbit ? DGUARD : CGUARD)) {   // check for early D or C bit
            us++;
            cbit = !cbit;
        }

        lastJitter = (val - us * curFmt->clockX2) / 2;      // compensation for jitter
        if (!resync || (us != 2 && cnt2 < 16))
            cnt2 = 0;
        else if (++cnt2 == 16) {
            if (!cbit) {
                cbit = true;
                return bitLog(BIT0S);
            }
        }
        syncPattern = syncPattern * 8 + (us < 7 ? us : 7);
        if ((syncPattern & 0777777) == 0323434 ||                                     // index sync sync pattern
            (syncPattern & 0777777) == 0234343 || (syncPattern & 077777) == 024343) { // id & data sync pattern{ 
            if (cbit) {
                cbit = false;
                return bitLog(BIT1S);
            }
        }
        

        switch (us) {
        case 2:
            return bitLog(cbit ? BIT0 : BIT1); //
        case 3:
            if (!cbit)
                return bitLog(BIT1);     // previous 0 for cbit already sent
            queuedBit = BIT0;
            return bitLog(BIT0);
        case 4:
            queuedBit = (cbit ? BIT0 : BIT1);
            return bitLog(cbit && resync ? BIT0M : BIT0);
        default:                        // all invalid, but keep cbit updated
            return bitLog(BITBAD);
        }
    }
}


#ifdef EXPERIMENTAL
/* alternative considered based on libdsk code */
/* currently doesn't perform as well */

#define CLOCK_MAX_ADJ 3     /* +/- 10% adjustment */
#define CLOCK_MIN(_c) (((_c) * (100 - CLOCK_MAX_ADJ)) / 100)
#define CLOCK_MAX(_c) (((_c) * (100 + CLOCK_MAX_ADJ)) / 100)


int getFluxNextBit(int guardPct) {
    static int flux, clock, clockCentre;
    static int clockedZeros;
    static const int pll_phase_adj_pct = 40;
    static const int pll_period_adj_pct = 3;

    if (when(0) == 0) {
        flux = 0;
        clockedZeros = 0;
        clock = clockCentre = 2000;         // 2000ns = 2us
    }

    while (flux < (clock * guardPct / 100)) {
        int f = getNextFlux();
        when(f);
        if (f == END_BLOCK || f == END_FLUX)
            return BITEND;
        flux += f * 1000000 / 24027;
    }


    flux -= clock;

    if (flux >= (clock * (100 - guardPct)/ 100)) {
        clockedZeros++;
        return BIT0;
    }

    /* PLL: Adjust clock frequency according to phase mismatch.
     * eg. pll_period_adj_pct=0% -> timing-window centre freq. never changes */
    if (clockedZeros <= 3) {
        /* In sync: adjust base clock by a fraction of phase mismatch. */
        clock += flux * pll_period_adj_pct / 100;
    }
    else {
        /* Out of sync: adjust base clock towards centre. */
        clock += (clockCentre - clock) * pll_period_adj_pct / 100;
    }

    /* Clamp the clock's adjustment range. */
    clock = max(CLOCK_MIN(clockCentre),
        min(CLOCK_MAX(clockCentre), clock));

    /* PLL: Adjust clock phase according to mismatch.
     * eg. pll_phase_adj_pct=100% -> timing window snaps to observed flux. */
    flux *= (100 - pll_phase_adj_pct) / 100;

    clockedZeros = 0;
    return BIT1;
}



int getMFMBitTest(bool resync) {
    static bool lastResync = false;
    static int cguard = 50, dguard = 50;
    static unsigned int bits;
    int bit;

    if (resync && !lastResync) {
        cguard = dguard = 50;
        bits = 1;                   // will not resync on 0s for at least 31 bits
    }
    lastResync = resync;

    if ((bit = getFluxNextBit(cguard)) == BITEND)
        return bitLog(BITEND);
    bits <<= 1;
    if (bit == BIT1) {
        if ((++bits & 0xffff) == 0x4489) {
//            cguard = 40; dguard = 60;
            return bitLog(BIT1S);
        }
    }
    else if ((bits & 0xffff) == 0x5224 || (resync && ((bits & 0xffffffff) == 0xaaaaaaaa || (bits & 0xffff) == 0x9254))) {
//        cguard = 40; dguard = 60;
        return bitLog(BIT0S);
    }

    if ((bit = getFluxNextBit(dguard)) == BITEND)
        return bitLog(BITEND);
    bits <<= 1;
    if (bit == BIT1) {
        if ((++bits & 0xffff) == 0x4489)
            ;   // cguard = 40; dguard = 60;
    }
    else if ((bits & 0xffff) == 0x5224 || (resync && ((bits & 0xffffffff) == 0xaaaaaaaa || (bits & 0xffff) == 0x9254)))
        ; // cguard = 40; dguard = 60;

    switch (bits & 3) {
    case 3: return bitLog(BITBAD);
    case 2: return bitLog(BIT0);
    case 1: return bitLog(BIT1);
    }
    if ((bits & 0xf) == 8)
        return bitLog(BIT0M);
    return bitLog((bits & 0xf) == 4 ? BIT0 : BITBAD);
}
#endif

int getFMBit(bool resync)
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
        us = (val + curFmt->clockX2 / 2) / curFmt->clockX2;     // simple rounding

        // use consecutive 0s in the GAPs to sync clock i.e. pulses every 4us
        if (!resync || us != 4)                  // use consecutive 0s in the GAPs to sync clock
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

#define MAXHALFBIT   7          // must be less than 8
#define FMCNT(n)    (cnts[n][0] + cnts[n][2])
#define MFMCNT(n)   (FMCNT(n) + cnts[n][1])
#define M2FMCNT(n)  (MFMCNT(n) + cnts[n][3])

bool analyseFormat()
{
    int val;
    int blk = 1;
    int cnts[3][4];   // counts of valid samples seen at 1/2 bit intervals cnts[BPS500] .. cnts[BP250]
    int cntTotal = 0;
    int profile = UNKNOWN;

    memset(&slotInfo, 0, sizeof(slotInfo));     // make sure we start with fresh info
    memset(&curTrack, 0, sizeof(curTrack));
    memset(cnts, 0, sizeof(cnts));

    /*
        look at the profile of flux transitions to work out
        the disk encoding format
    */

   while (seekBlock(blk++)) {

        while ((val = getNextFlux()) != END_FLUX && val != END_BLOCK) {
            int halfbit;
            if (2 <= (halfbit = (val + HALFBIT500 / 2) / HALFBIT500) && halfbit <= 5)
                cnts[BPS500][halfbit - 2]++;
            if (2 <= (halfbit = (val + HALFBIT300 / 2) / HALFBIT300) && halfbit <= 5)
                cnts[BPS300][halfbit - 2]++;
            if (2 <= (halfbit = (val + HALFBIT250 / 2) / HALFBIT250) && halfbit <= 5)
                cnts[BPS250][halfbit - 2]++;
            cntTotal++;
        }
    }
#ifdef _DEBUG
   logger(VERBOSE, "250kb/s FM=%.3f MFM=%.3f M2FM=%.3f\n", FMCNT(BPS250) * 100.0 / cntTotal, MFMCNT(BPS250) * 100.0 / cntTotal, M2FMCNT(BPS250) * 100.0 / cntTotal);
   logger(VERBOSE, "300kb/s FM=%.3f MFM=%.3f M2FM=%.3f\n", FMCNT(BPS300) * 100.0 / cntTotal, MFMCNT(BPS300) * 100.0 / cntTotal, M2FMCNT(BPS300) * 100.0 / cntTotal);
   logger(VERBOSE, "500kb/s FM=%.3f MFM=%.3f M2FM=%.3f\n", FMCNT(BPS500) * 100.0 / cntTotal, MFMCNT(BPS500) * 100.0 / cntTotal, M2FMCNT(BPS500) * 100.0 / cntTotal);
#endif
    if (cntTotal >= MINSAMPLE) {
        /* 500 kbps - all options*/
        int t500 = (int)(.998* cntTotal), t250 = (int)(.96 * cntTotal);

        if (FMCNT(BPS500) > t500)
            profile = FM500;
        else if (MFMCNT(BPS500) > t500)
            profile = MFM500;
        else if (M2FMCNT(BPS500) > t500)
            profile = M2FM500;

        /* 250 kbps - only FM & MFM */
        else if (FMCNT(BPS250) > t250)
            profile = FM250;
        else if (MFMCNT(BPS250) > t250)
            profile = MFM250;

    }

    for (curFmt = formats; curFmt->profile && curFmt->profile != profile; curFmt++)
        ;


    if (curFmt->getBit == NULL) {
        logger(ALWAYS, "%s not supported\n", curFmt->name);
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
    debug = 0;
    seekBlock(1);               // look at first track copy

    int sectorStart = 0, firstSector = 0, lastSectorStart = 0;

    int marker;
    byte buf[7];
    bool haveSize = false;
    /* scan the whole track or until we can determine the sector and interMarker timings */
    while ((marker = curFmt->getMarker()) != INDEX_HOLE_MARK && (slotInfo.interSectorByteCnt == 0 || slotInfo.interMarkerByteCnt == 0)) {
        if (marker == ID_ADDRESS_MARK) {
            lastSectorStart = sectorStart;
            sectorStart = time2Byte(when(0));
            if (firstSector == 0)
                firstSector = sectorStart;
            if (!haveSize && getData(marker, buf) > 0) {
                curTrack.size = 128 << (buf[4] & 0x7f);    // FM used 128, convert to newer form
                haveSize = true;
            }
            if (haveSize && lastSectorStart != 0 && slotInfo.interSectorByteCnt == 0
                && sectorStart - lastSectorStart > curTrack.size
                && sectorStart - lastSectorStart < curFmt->fixedSector + curTrack.size * 2) // less than 2 sectors
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

    debug = savDebug;               // restore debug options

    return true;
}