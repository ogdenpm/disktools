// This is an open source non-commercial project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

#include <stdint.h>
#include <stdbool.h>
#include "sectorManager.h"
#include "dpll.h"
#include "formats.h"
#include "flux.h"
#include "util.h"

// initial patterns for DD disks

pattern_t dd5Patterns[] = {
    { 0xffff, 0x8888, GAP},
    { 0xffff, 0x5555, SYNC},                  // always preceeded by GAP

    { 0xffff,0x9254, IBM_GAP},
    { 0xffffffff, 0x52245552, INDEXAM},            // IBM std
    { 0xffffffff, 0x44895554, IDAM},               // IBM std
    { 0xffffffff, 0x44895545, DATAAM},
//    { 0xffffffff, 0x4489554A, DELETEDAM},
    {0}
};


pattern_t dd8Patterns[] = {
    { 0xffff, 0x8888, GAP},
    { 0xffff, 0x5555, SYNC},                  // always preceeded by GAP

    { 0xffff,0x9254, IBM_GAP},
    { 0xffffffff, 0x52245552, INDEXAM},            // IBM std
    { 0xffffffff, 0x44895554, IDAM},               // IBM std
    { 0xffffffff, 0x44895545, DATAAM},
    { 0xffffffff, 0x4489554A, DELETEDAM},
    {0xffffffff, 0x55552A52, M2FM_INDEXAM},
    {0xffffffff, 0x55552A54, M2FM_IDAM},
    {0xffffffff, 0x55552A44, HP_DATAAM},
    {0xffffffff, 0x55552A45, M2FM_DATAAM},
    {0xffffffff, 0x55552A48, M2FM_DELETEDAM},
    {0xffffffff, 0x55552A55, HP_DELETEDAM},
    {0}
};

// initial patterns for SD disks
pattern_t sdPatterns[] = {
    { 0xffff, 0xffff, GAP},
    { 0xffff, 0xAAAA, SYNC},
    { 0xffffff, 0xAAF77A, INDEXAM},            // IBM std
    { 0xffffff, 0xAAF57E, IDAM},               // IBM std
    { 0xffffff, 0xAAF56F, DATAAM},
    { 0xffffff, 0xAAF56A, DELETEDAM},
    {0}
};

static pattern_t hsPatterns[] = {
    { 0xffffffff,     0, LSI_SECTOR },
    { 0xffffffffffff, 0, ZDS_SECTOR },
    { 0 }
};

static pattern_t lsiPatterns[] = {
    { 0xffffffff,     0, LSI_SECTOR },
    { 0 }
};



pattern_t ddMFMPatterns[] = {
    { 0xffffff, 0x555555, SYNC},
    { 0xffff,0x9254, GAP},
    { 0xffffffff, 0x52245552, INDEXAM},            // IBM std
    { 0xffffffff, 0x44895554, IDAM},               // IBM std
    { 0xffffffff, 0x44895545, DATAAM},
//    { 0xffffffff, 0x4489554A, DELETEDAM},
    {0}
};

pattern_t ddM2FMPatterns[] = {
    { 0xffff, 0x8888, GAP},
    { 0xffff, 0x5555, SYNC},
    { 0xffffff, 0x552A52, M2FM_INDEXAM},
    {0xffffff, 0x552A54, M2FM_IDAM},
    {0xffffff, 0x552A45, M2FM_DATAAM},
    {0xffffff, 0x552A48, M2FM_DELETEDAM},

    {0}
};


pattern_t hs5Patterns[] = {
      { 0xffffffC0007fff ,0x0, MTECH_SECTOR},     // created from 0xff track sector, note track is wildcarded due to track alignment problems
      { 0xffffffffffff, 0xAAAAAAAAAAAA, GAP},             // for efficiency 

    {0}
};


pattern_t ddHPPatterns[] = {
    { 0xffff, 0x8888, GAP},
    { 0xffff, 0x5555, SYNC},
    {0xffffff, 0x552A54, HP_IDAM},
    {0xffffff, 0x552A44, HP_DATAAM},
    {0xffffff, 0x552A55, HP_DELETEDAM},

    {0}
};
pattern_t sdFMPatterns[] = {
    { 0xffffff, 0xffffff, GAP},
    { 0xffffff, 0xAAAAAA, SYNC},
    { 0xffffff, 0xAAF77A, INDEXAM},
    { 0xffffff, 0xAAF57E, IDAM},
    { 0xffffff, 0xAAF56F, DATAAM},
    { 0xffffff, 0xAAF56A, DELETEDAM},
    {0}
};



static bool crcLSI(uint16_t* data, int len);
static bool crcZDS(uint16_t* data, int len);
static bool crcRev(uint16_t* data, int len);
static bool crcStd(uint16_t* data, int len);
bool crc8(uint16_t* data, int len);
/*
    some notes on the ordering of this table
    each group should start with one of SDx DDx which are used to detect the disk format
    immediately after the each of these is a list of possible formats all with the same encoding
    Final formats have the O_SPC and O_SIZE options cleared

    Where there is more than one format the list should be organised grouped by sector size (smallest first)
    and then by spt (highest first) then insert additional trial formats as follows

    Duplicate the first format and set O_SIZE if multiple sector sizes supported else set O_SPC
    (note this should set the first entry to have the max spt - if not increase spt to max)

    If there are multiple sector sizes and more than one format for a given size,
    then duplicate the head of the group  and set the O_SPC option.

    Note hardsectors are treated differently, currently only 8" is supported and FM encoding is
    assumed i.e. FM8H. The determination of the true format is done in hs8GetTrack as encodings
    are non standard and examples so far have start information dependent on the sector
*/
formatInfo_t formatInfo[] = {
 //   name        siz 1st spt    enc     opt   crc         pattern      crcinit   idam data spc
    {"SD5"        , 0, 1, 16,   E_FM5,      0, crcStd,     sdPatterns, 0xffff,     77,  99, 188}, 
    {"FM5"        , 0, 1, 16,   E_FM5,  O_SPC, crcStd,   sdFMPatterns, 0xffff,     77,  99, 192},
    {"FM5-16x128" , 0, 1, 16,   E_FM5,      0, crcStd,   sdFMPatterns, 0xffff,     77,  99, 188},
    {"FM5-15x128" , 0, 1, 15,   E_FM5,      0, crcStd,   sdFMPatterns, 0xffff,     82, 106, 196},

    {"SD8"        , 0, 1, 26,   E_FM8,      0, crcStd,     sdPatterns, 0xffff,     90, 115, 194},
    {"FM8-26x128" , 0, 1, 26,   E_FM8,      0, crcStd,   sdFMPatterns, 0xffff,     39, 64, 191},    // manually adjusted 

    {"MFM5H"      , 1, 0, 16, E_MFM5H,O_MTECH, crc8,      hs5Patterns, 0xffff,	   40,  42, 350},
    {"FM8H"       , 0, 0, 32,  E_FM8H,      0, crcZDS,     hsPatterns,      0, HSIDAM,   8, 168},
    {"FM8H-ZDS"   , 0, 0, 32,  E_FM8H,  O_ZDS, crcZDS, &hsPatterns[1],      0, HSIDAM,  18, 168},
    {"FM8H-LSI"   , 0, 0, 32,  E_FM8H,  O_LSI, crcLSI,     hsPatterns,      0, HSIDAM,   8, 168}, 
    {"LSI"        , 0, 0, 32,  E_FM8H,  O_LSI, crcLSI,    lsiPatterns,      0, HSIDAM,   8, 168},  // for future use (LSI with no option to detect ZDS)

    {"DD5"        , 1, 1, 16,  E_MFM5,      0, crcStd,    dd5Patterns, 0xcdb4,    155, 200, 368},
    {"MFM5"       , 1, 1, 16,  E_MFM5, O_SIZE, crcStd,  ddMFMPatterns, 0xcdb4,    155, 200, 368},
    {"MFM5-16x256", 1, 1, 16,  E_MFM5,      0, crcStd,  ddMFMPatterns, 0xcdb4,    155, 200, 375},   // spc=375 works for both seen variants
    {"MFM5-8x512" , 2, 1,  8,  E_MFM5,      0, crcStd,  ddMFMPatterns, 0xcdb4,    166, 210, 686}, 

    {"DD8"        , 0, 0 , 0,  E_MFM8,      0, crcStd,    dd8Patterns,      0,      0,   0,   0},
    {"M2FM8-INTEL", 0, 1, 52, E_M2FM8,      0, crcStd, ddM2FMPatterns,      0,     70, 106, 194},
    {"M2FM8-HP"   , 1, 0, 30, E_M2FM8,   O_HP, crcRev,   ddHPPatterns, 0xffff,     91, 122, 332},
    {0}
};

formatInfo_t *curFormat;



static bool crcLSI(uint16_t* data, int len) {
    uint16_t crc = curFormat->crcInit;
    for (int i = 0; i < len - 2; i++)
        crc += data[i] & 0xff;
    return crc == ((data[len - 2] & 0xff) + ((data[len - 1] & 0xff) << 8));

}

static bool crcZDS(uint16_t* data, int len) {
#define CRC16 0x8005
    uint16_t crc = curFormat->crcInit;
    len -= 2;               // exclude postamble
    for (int i = 0; i < len; i++)
        for (uint16_t mask = 0x80; mask; mask >>= 1)
            crc = ((crc << 1) | ((data[i] & mask) ? 1 : 0)) ^ ((crc & 0x8000) ? CRC16 : 0);
    return crc == 0;
}

static bool crcRev(uint16_t* data, int len) {
    uint8_t x;
    uint16_t crc = curFormat->crcInit;

    while (len-- > 0) {
        x = (crc >> 8) ^ flip[*data++ & 0xff];
        x ^= x >> 4;
        crc = (crc << 8) ^ (x << 12) ^ (x << 5) ^ x;
    }
    return crc == 0;
}

bool crc8(uint16_t* data, int len) {
    uint16_t crc = 0;
    for (int i = 0; i < len - 1; i++)
        crc = (crc & 0xff) + (data[i] & 0xff) + ((crc & 0x100) ? 1 : 0);
    return (crc & 0xff) == (data[len - 1] & 0xff);
}

static bool crcStd(uint16_t* buf, int len) {
    uint8_t x;
    uint16_t crc = curFormat->crcInit;

    while (len-- > 0) {
        x = (crc >> 8) ^ (*buf++ & 0xff);
        x ^= x >> 4;
        crc = (crc << 8) ^ (x << 12) ^ (x << 5) ^ x;
    }
    return crc == 0;
}

static formatInfo_t *lookupFormat(char* fmtName) {
    formatInfo_t* p;
    for (p = formatInfo; p->name; p++) {
        if (_stricmp(p->name, fmtName) == 0)
            return p;
    }
    return NULL;
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



/*
    Encoder Rules 
    FM encode: 
        1. Write data bits at the center of the bit cell, and 
        2. Write clock bits at the beginning of the bit cell. 
    MFM encode: 
        1. Write data bits at the center of the bit cell, and 
        2. Write clock bits at the beginning of the bit cell if: 
        A. no data has been written in the previous bit cell, and 
        B. no data bit will be written in the present bit cell. 
    M2FM encode: 
        1. Write data bits at the center of the bit cell, and 
        2. Write clock bits at the beginning of the bit cell if: 
        A. no data or clock bit has been written in the previous bit cell, and 
        B. no data bit will be written in the present bit cell. 
    GCR encode: NOT SUPPORTED YET
        This code translates 4 bits into 5 bits of binary data for storing information and then re-translates the 5 
        bits into 4 bits during a read operation. 

                5 Bit 
        4 Bit  Recorded 
        Data     Data 
        0000    11001 
        0001    11011 
        0010    10010 
        0011    10011 
        0100    11101 
        0101    10101 
        0110    10110 
        0111    10111 
        1000    11010 
        1001    01001 
        1010    01010 
        1011    01011 
        1100    11110 
        1101    01101 
        1110    01110 
        1111    01111 
*/
// prevPattern is used for MFM & M2FM encoding specifically that last dbit (MFM & M2FM) and last cbit (M2FM)
uint64_t encode(uint32_t val, uint32_t prevPattern) {
    uint64_t pattern = prevPattern;
    unsigned mask;           // mask for determining if cbit needed

    switch (curFormat->encoding) {
    case E_MFM5: case E_MFM8: case E_MFM5H: mask = 5; break;
    case E_M2FM8: mask = 0xd; break;
    default: mask = 0; break;
    }

    for (uint32_t i = 0x80000000; i; i >>= 1) {
        pattern <<= 2;
        if (val & i)                // add dbit
            pattern++;
        if ((pattern & mask) == 0)  // add cbit
            pattern += 2;
    }
    return pattern;
}


// decode lower 16 bits of pattern into data byte + flag to indicate if suspect encoding
// note for MFM & M2FM bits 17, 18 will are used to determine if suspect
int decode(uint64_t pattern) {
    unsigned mask;
    int val = 0;
    bool suspect = false;

    switch (curFormat->encoding) {
    case E_MFM5: case E_MFM8: case E_MFM5H: mask = 5; break;
    case E_M2FM8: mask = 0xd; break;
    default: mask = 0; break;
    }
    for (int i = 0; i < 8; i++, pattern >>= 2) {
        val = (val >> 1) + ((pattern & 1) ? 0x80 : 0);
        suspect |= ((pattern & 2) == 2) ^ ((pattern & mask) == 0);
    }
    if (curFormat->options & O_REV)
        val = flip[val];
    if (curFormat->options & O_INV)
        val ^= 0xff;

    return val + (suspect ? SUSPECT : 0);
}



int getByte() {
    for (int i = 0; i < 16; i++)    // get the 16 c/d bits for the byte
        if (getBit() < 0)
            return -1;
    return decode(pattern);
}

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
    case MTECH_SECTOR: return "MTECH_SECTOR";
    }
    return "NO MATCH\n";
}


void makeHS5Patterns(unsigned cylinder, unsigned slot) {
    hs5Patterns[0].match = encode(0xff0000 + (cylinder << 8) + slot, 0xA);

}

void makeHS8Patterns(unsigned cylinder, unsigned slot) {
    lsiPatterns[0].match = hsPatterns[0].match = encode(flip[(cylinder ? cylinder : 32) * 2 + 1], 0);            // set LSI match pattern
    hsPatterns[1].match = encode(((slot + 0x80) << 8) + cylinder, 0); // set ZDS match pattern

}

int matchPattern(int searchLimit) {
    pattern_t* p;
    int addedBits = 0;
    // scale searchLimit to bits to check 
    for (searchLimit *= 16; searchLimit > 0 && getBit() >= 0; searchLimit--) {
        // speed optimisation, don't consider pattern until at least a byte seen (16 data/clock)
        if (++addedBits >= 16) {
            if (debug & D_PATTERN) {      // pattern could potentially generate a lot of data
                char binStr[64];        // so reduce the overhead by building binary string before output
                uint64_t val = pattern;
                for (int j = 63; j >= 0; val >>= 1, j--)
                    binStr[j] = '0' + (val & 1);
                logBasic("%6u: %.64s  %llX\n", getBitCnt(), binStr, pattern);
            }
            // if we have a pattern match
            for (p = curFormat->patterns; p->mask; p++) {
                if (((pattern ^ p->match) & p->mask) == 0) {
                    DBGLOG(D_ADDRESSMARK, "%u: %llX %llX %llX %s\n", getBitCnt(), pattern, p->match, p->mask, getName(p->am));
                    return p->am;
                }
            }
        }
    }
    return 0;
}


void setFormat(char* fmtName) {
    curFormat = lookupFormat(fmtName);
    if (!curFormat)
        logFull(D_FATAL, "Attempt to select unknown format %s\n", fmtName);

}

bool setInitialFormat(char *fmtName) {
    if (!fmtName || !lookupFormat(fmtName)) {
        if (cntHardSectors() > 0)
            fmtName = "FM8H";
        else {
            int diskSize = getRPM() < 320.0 ? 5 : 8;
            if (seekBlock(1) >= 0) {
                fmtName = diskSize == 5 ? "DD5" : "DD8";
                DBGLOG(D_DETECT, "Trying %s\n", fmtName);
                setFormat(fmtName);
                if (!(fmtName = probe())) {
                    seekBlock(1);
                    fmtName = diskSize == 5 ? "SD5" : "SD8";
                    DBGLOG(D_DETECT, "Trying %s\n", fmtName);
                    setFormat(fmtName);
                    fmtName = probe();
                }
            }
        }
    }
    if (fmtName) {
        DBGLOG(D_DETECT, "Detected %s\n", fmtName);
        setFormat(fmtName);
        return true;
    }
    DBGLOG(D_DETECT, "Could not detect encoding\n");
    return false;
}




