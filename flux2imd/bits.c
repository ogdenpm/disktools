#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "decoders.h"
#include "bits.h"
#include "flux.h"
#include "util.h"
#include "dpll.h"
#include "analysis.h"

extern uint64_t pattern;
extern unsigned bitCnt;
extern char *getName(int am);


int matchPattern(int searchLimit) {
    pattern_t* p;
    int addedBits = 0;

    searchLimit *= 16;      // convert bytes to bits

    for (int i = 0; i < searchLimit && getBit() >= 0; i++) {

        if (++addedBits >= 16) {
            if (debug & PATTERN) {      // pattern could potentially generate a lot of data
                char binStr[64];        // so reduce the overhead by building binary string before output
                uint64_t val = pattern;
                for (int j = 63; j >= 0; val >>= 1, j++)
                    binStr[j] = '0' + (val & 1);
                logBasic("%6u: %.64s  %llX\n", bitCnt, binStr, pattern);
            }
            for (p = curFormat->patterns; p->mask; p++) {
                if (((pattern ^ p->match) & p->mask) == 0) {
                    DBGLOG((ADDRESSMARK, "%u: %llX %llX %llX %s\n", bitCnt, pattern, p->match, p->mask, getName(p->am)));
                    return p->am;
                }
            }
        }
    }
    return 0;
}



int getByte() {
    int val = 0;
    int bit;
    bool suspect = false;
    for (int i = 0; i < 8; i++) {
        if (getBit() < 0 || (bit = getBit()) < 0)
            return -1;
        switch (curFormat->encoding) {
        case E_FM5: case E_FM8: case E_FM8H:
            suspect |= !(pattern & 2); break;
        case E_MFM5: case E_MFM8:
            suspect |= (pattern & 2) && (pattern & 5); break;
        case E_M2FM8:
            suspect |= (pattern & 2) && (pattern & 0xd); break;
        }
        if (curFormat->options & O_REV)
            val = (val >> 1) + (bit ? 0x80 : 0);
        else
            val = (val << 1) + bit;
    }
    return val + (suspect ? SUSPECT : 0);
}





static char *probe() {
    int matchType;
    retrain(0);
    do {
        // 1200 byte search from previous pattern should be enough to find at least one more pattern
        matchType = matchPattern(1200);
        switch (matchType) {
        case IDAM:
        case INDEXAM:
        case DATAAM:
            if (curFormat->encoding == (curFormat + 1)->encoding)
                return (curFormat + 1)->name;
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
    if (!fmtName || !lookupFormat(fmtName)) {
        if (cntHardSectors() > 0)
            fmtName = "FM8H";
        else {
            int diskSize = getRPM() < 320 ? 5 : 8;
            if (seekBlock(1) >= 0) {
                fmtName = diskSize == 5 ? "DD5" : "DD8";
                DBGLOG((DETECT, "Trying %s\n", fmtName));
                setFormat(fmtName);
                if (!(fmtName = probe())) {
                    seekBlock(1);
                    fmtName = diskSize == 5 ? "SD5" : "SD8";
                    DBGLOG((DETECT, "Trying %s\n", fmtName));
                    setFormat(fmtName);
                    fmtName = probe();
                }
            }
        }
    }
    if (fmtName) {
        DBGLOG((DETECT, "Detected %s\n", fmtName));
        setFormat(fmtName);
        return true;
    }
    DBGLOG((DETECT, "Could not detect encoding\n"));
    return false;
}

