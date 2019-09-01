#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "decoders.h"
#include "bits.h"
#include "flux.h"
#include "util.h"
#include "dpll.h"
#include "analysis.h"

#define FMSPLIT_US  180000      // track length in uS - 8" ~ 200000, 5 1/4" ~ 1666666


static uint8_t modulation;
uint64_t pattern;
static uint32_t bitCnt;


char* getName(int am) {
    switch (am) {
    case GAP: return "GAP";
    case SYNC: return "SYNC";
    case IBM_GAP: return "IBM_GAP";
    case INDEXAM: return "INDEXAM";
    case IDAM: return "IDAM";
    case DATAAM: return "DATAAM";
    case DELETEDAM: return "DELETEDAM";
    case M2FM_INDEXAM: return "M2FM_INDEXAM";
    case M2FM_IDAM: return "M2FM_IDAM";
    case M2FM_DATAAM: return "M2FM_DATAAM";
    case M2FM_DELETEDAM: return "M2FM_DELETEDAM";
    case HP_IDAM: return "HP_IDAM";
    case HP_DATAAM: return "HP_DATAAM";
    case HP_DELETEDAM: return "HP_DELETEDAM";
    case LSI_SECTOR: return "LSI_SECTOR";
    case ZDS_SECTOR: return "ZDS_SECTOR";
    }
    return "NO MATCH\n";
}




uint32_t matchPattern(int searchLimit) {
    int bit;
    pattern_t* p;
    int addedBits = 0;

    searchLimit *= 16;      // convert bytes to bits

    for (int i = 0; i < searchLimit && (bit = getBit()) >= 0; i++) {
        pattern = (pattern << 1) + bit;

        if (debug & PATTERN) {
            for (uint64_t i = 1LLU << 63; i; i >>= 1)
                logBasic(pattern & i ? "1" : "0");       // does not go to log file as primarily used for debug
            logBasic("  %llX\n", pattern);
        }

        if (++addedBits >= 16) {
            for (p = curFormat->patterns; p->mask; p++) {
                if (((pattern ^ p->match) & p->mask) == 0) {
                    logFull(ADDRESSMARK, "%.1f: %llX %llX %llX %s\n", where(), pattern, p->match, p->mask, getName(p->am));

                    bitCnt += addedBits;
                    return p->am;
                }
            }
        }
    }
    bitCnt += addedBits;
    return 0;
}

int getBitPair() {
    int cBit, dBit;
    if ((cBit = getBit()) < 0)
        return -1;
    if ((dBit = getBit()) < 0)
        return -1;
    int bitPair = (cBit << 1) + dBit;
    pattern = (pattern << 2) + bitPair;
    bitCnt += 2;
    return bitPair;
}



int getByte() {
    int result = 0;
    int bitPair;
    bool suspect = false;
    for (int i = 0; i < 8; i++) {
        if ((bitPair = getBitPair()) < 0)
            return -1;
        switch (curFormat->encoding) {
        case E_FM5: case E_FM8: case E_FM8H:
            suspect |= (bitPair & 2) == 0; break;
        case E_MFM5: case E_MFM8:
            suspect |= (bitPair & 2) && ((bitPair & 1) || (pattern & 4)); break;
        case E_M2FM8:
            suspect |= (bitPair & 2) && ((bitPair & 1) || (pattern & 0xc)); break;
        }
        if (curFormat->options & O_REV)
            result = (result >> 1) + ((bitPair & 1) ? 0x80 : 0);
        else
            result = (result << 1) + (bitPair & 1);
    }
    return result + (suspect ? SUSPECT : 0);
}





static char *probe() {
    int matchType;
    resetByteCnt();
    do {
        // 1200 byte search from previous pattern should be enough to find at least one more pattern
        matchType = matchPattern(1200);
        switch (matchType) {
        case IDAM:
        case INDEXAM:
        case DATAAM:
            if (curFormat->encoding == (curFormat + 1)->encoding)
                return (curFormat + 1)->shortName;
            else
                return NULL;
        case M2FM_IDAM:                 // skip IDAM bytes (ok even for HP as it will just skip 2 GAP bytes)
            for (int i = 0; i < 6; i++)
                getByte();
            break;
        case M2FM_INDEXAM:
        case M2FM_DATAAM:
        case M2FM_DELETEDAM:
            return "M2FM8-INTEL";
        case HP_DATAAM:
        case HP_DELETEDAM:
            return "M2FM8-HP";
        }
    } while (matchType > 0);
    return NULL;
}

bool setEncoding(char *fmtName) {
    int rate;
    if (!fmtName || !(curFormat = lookupFormat(fmtName))) {
        if (cntHardSectors() > 0) {
            fmtName = "FM8H";
        }
        rate = getRPM() < 320 ? 250 : 500;

        if (seekBlock(1) >= 0) {
            logFull(DETECT, "Trying DD\n");
            setFormat(rate == 500 ? "DD8" : "DD5");
            retrain(0);
            if (!(fmtName = probe())) {
                logFull(DETECT, "Trying SD\n");
                seekBlock(1);
                setFormat(rate == 500 ? "SD8" : "SD5");
                retrain(0);
                fmtName = probe();

            }
        }
    }
    if (fmtName) {
        logFull(DETECT, "Detected %s\n", fmtName);
        setFormat(fmtName);
        return true;
    }
    logFull(DETECT, "Could not detect encoding\n");
    return false;
}

void resetByteCnt() {
    bitCnt = 0;
}

uint32_t getByteCnt() {
    return bitCnt / 16;
}