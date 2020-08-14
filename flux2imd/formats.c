// This is an open source non-commercial project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include "sectorManager.h"
#include "dpll.h"
#include "formats.h"
#include "flux.h"
#include "util.h"

// initial patterns for DD disks

pattern_t dd5Patterns[] = {
    { 0xffffffff, 0x88888888, GAP},
    { 0xffffffff, 0x55555555, SYNC},                  // always preceeded by GAP

    { 0xffffffff,0x92549254, IBM_GAP},
    { 0xffffffff, 0x52245552, INDEXAM},            // IBM std
    { 0xffffffff, 0x44895554, IDAM},               // IBM std
    { 0xffffffff, 0x44895545, DATAAM},
//    { 0xffffffff, 0x4489554A, DELETEDAM},
    {0}
};


pattern_t dd8Patterns[] = {
    { 0xffffffff, 0x88888888, GAP},
    { 0xffffffff, 0x55555555, SYNC},                  // always preceeded by GAP

    { 0xffffffff,0x92549254, IBM_GAP},
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
    {0xffffffff, 0x11112244, TI_IDAM},
    {0xffffffff, 0x11112245, TI_DATAAM},
    {0}
};

// initial patterns for SD disks
pattern_t sdPatterns[] = {
    { 0xffffffff, 0xffffffff, GAP},
    { 0xffffffff, 0xAAAAAAAA, SYNC},
    { 0xffffffff, 0xAAAAF77A, INDEXAM},            // IBM std
    { 0xffffffff, 0xAAAAF57E, IDAM},               // IBM std
    { 0xffffffff, 0xAAAAF56F, DATAAM},
    { 0xffffffff, 0xAAAAF56A, DELETEDAM},
    {0}
};

pattern_t sd5HPatterns[] = {
    { 0xffffffffffffffff, 0xAAAAAAAAAAAAAAAA, GAP},     // for efficiency 
    { 0xffffffffffff, 0xAAAAAAAAFFEF, NSI_SECTOR },      // 00 00 00 FB
    {0}
};

pattern_t nsi5SPatterns[] = {
    { 0xffffffffffffffff, 0xAAAAAAAAAAAAAAAA, GAP},     // for efficiency 
    { 0xffffffffffff, 0xAAAAAAAAFFEF, NSI_SECTOR },      // 00 00 00 FB
    {0}
};

pattern_t dd5HPatterns[] = {
      { 0xffffffffffffffff, 0xAAAAAAAAAAAAAAAA, GAP},     // for efficiency 
      { 0xffffffffffffffff, 0xAAAAAAAA55455545, NSI_SECTOR },      // 00 00 FB FB
      { 0xffffffffffff ,0xAAAAAAAA5555, MTECH_SECTOR},     // 00 00 00 FF
    {0}
};

static pattern_t sd8HPatterns[] = {
    { 0xffffffff,     0, LSI_SECTOR },
    { 0xffffffffffff, 0, ZDS_SECTOR },
    { 0 }
};

static pattern_t lsiPatterns[] = {
    { 0xffffffff,     0, LSI_SECTOR },
    { 0 }
};



pattern_t ddMFMPatterns[] = {
    { 0xffffffff, 0x55555555, SYNC},
    { 0xffffffff, 0x92549254, GAP},
    { 0xffffffff, 0x52245552, INDEXAM},            // IBM std
    { 0xffffffff, 0x44895554, IDAM},               // IBM std
    { 0xffffffff, 0x44895545, DATAAM},
//    { 0xffffffff, 0x4489554A, DELETEDAM},
    {0}
};

pattern_t ddM2FMPatterns[] = {
    {0xffffffff, 0x88888888, GAP},
    {0xffffffff, 0x55555555, SYNC},
    {0xffffffff, 0x55552A52, M2FM_INDEXAM},
    {0xffffffff, 0x55552A54, M2FM_IDAM},
    {0xffffffff, 0x55552A45, M2FM_DATAAM},
    {0xffffffff, 0x55552A48, M2FM_DELETEDAM},

    {0}
};


pattern_t mtech5Patterns[] = {
      { 0xffffffffC0007fff ,0x0, MTECH_SECTOR},     // created from 0xff track sector, note track is wildcarded due to track alignment problems
      { 0xffffffffffffffff, 0xAAAAAAAAAAAAAAAA, GAP},     // for efficiency 
    {0}
};

pattern_t ddTIPatterns[] = {
    {0xffffffff, 0x11112244, TI_IDAM},
    {0xffffffff, 0x11112245, TI_DATAAM},
    {0xffffffff, 0x11111111, SYNC},
    {0}
};

pattern_t nsi5DPatterns[] = {
      { 0xffffffffffffffff, 0xAAAAAAAA55455545, NSI_SECTOR },      // 00  FD VOL TRK
      { 0xffffffffffff, 0xAAAAAAAAAAAA, GAP},     // for efficiency 
    {0}
};

pattern_t nsi5DataPatterns[] = {
      { 0xffffffff, 0xAAAA5551, NSI_SECTOR },     // 00 FD
      { 0xffffffffffff, 0xAAAAAAAAAAAA, GAP},     // for efficiency 
    {0}
};

pattern_t ddHPPatterns[] = {
    { 0xffffffff, 0x88888888, GAP},
    { 0xffffffff, 0x55555555, SYNC},
    {0xffffffff, 0x55552A54, HP_IDAM},
    {0xffffffff, 0x55552A44, HP_DATAAM},
    {0xffffffff, 0x55552A55, HP_DELETEDAM},

    {0}
};
pattern_t sdFMPatterns[] = {
    { 0xffffffff, 0xffffffff, GAP},
    { 0xffffffff, 0xAAAAAAAA, SYNC},
    { 0xffffffff, 0xAAAAF77A, INDEXAM},
    { 0xffffffff, 0xAAAAF57E, IDAM},
    { 0xffffffff, 0xAAAAF56F, DATAAM},
    { 0xffffffff, 0xAAAAF56A, DELETEDAM},
    {0}
};



static bool crcLSI(uint16_t* data, int len);
static bool crcZDS(uint16_t* data, int len);
static bool crcRev(uint16_t* data, int len);
static bool crcStd(uint16_t* data, int len);
bool crc8(uint16_t* data, int len);
bool crcNSI(uint16_t *data, int len);
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
       {"SD5"        , 0, 1, 16,   E_FM5,      0, crcStd,     sdPatterns, 0xffff,     77,  99, 188, 4000, "01234", NULL},
       {"FM5"        , 0, 1, 16,   E_FM5,  O_SPC, crcStd,   sdFMPatterns, 0xffff,     79, 103, 191, 4000, "01234", "5 1/4\" SD **"},    // U
       {"FM5-16x128" , 0, 1, 16,   E_FM5,      0, crcStd,   sdFMPatterns, 0xffff,     77, 101, 187, 4000, "01234", "5 1/4\" SD 16 x 128 sectors" },    // U
       {"FM5-15x128" , 0, 1, 15,   E_FM5,      0, crcStd,   sdFMPatterns, 0xffff,     82, 106, 196, 4000, "01234", "5 1/4\" SD 15 x 128 sectors" },    // U

       {"SD8"        , 0, 1, 26,   E_FM8,      0, crcStd,     sdPatterns, 0xffff,     90, 115, 194, 2000, "01234", NULL},
       {"FM8-26x128" , 0, 1, 26,   E_FM8,      0, crcStd,   sdFMPatterns, 0xffff,     82, 106, 191, 2000, "01234", "8\" SD 26 x 128 sectors"},    // manually adjusted 

       {"SD5H"        , 0, 1, 16, E_FM5H,      0, crcStd,     sd5HPatterns, 0xffff,     77,  99, 188, 4000, "34012", NULL},
       {"NSI-SD"    , 1, 1, 10, E_FM5H,   O_NSI, crcNSI, nsi5SPatterns, 0xffff,	    0,   0,  0, 4000, "34012", "5 1/4\" SD NSI 10 x 256 hard sectors"},

       {"SD8H"       , 0, 0, 32,  E_FM8H,      0, crcZDS,     sd8HPatterns,      0,       0,   0,  0, 2000, "34012", "8\" SD hard sectors **"},
       {"ZDS"        , 0, 0, 32,  E_FM8H,  O_ZDS, crcZDS, &sd8HPatterns[1],      0,       0,   0,  0, 2000, "34012", "ZDS 8\" SD 32 x 128 sectors"},
       {"FM8H-LSI"   , 0, 0, 32,  E_FM8H,  O_LSI, crcLSI,     sd8HPatterns,      0,       0,   0,  0, 2000, "34012", NULL},
       {"LSI"        , 0, 0, 32,  E_FM8H,  O_LSI, crcLSI,    lsiPatterns,      0,       0,   0,  0, 2000, "34012", "LSI 8\" SD 32 x 128 sectors"},  // LSI with no option to detect ZDS

       {"DD5"        , 1, 1, 16,  E_MFM5,      0, crcStd,    dd5Patterns, 0xcdb4,    155, 200, 368, 2000, "01234", NULL},
       {"MFM5"       , 1, 1, 16,  E_MFM5, O_SIZE, crcStd,  ddMFMPatterns, 0xcdb4,    163, 207, 378, 2000, "01234", "5 1/4\" DD **"},
       {"MFM5-16x256", 1, 1, 16,  E_MFM5,      0, crcStd,  ddMFMPatterns, 0xcdb4,    160, 204, 378, 2000, "01234", "5 1/4\" DD 16 x 256 sectors"},   // U spc=375 works for both seen variants
       {"MFM5-10x512", 2, 1, 10,  E_MFM5,  O_SPC, crcStd,  ddMFMPatterns, 0xcdb4,     66,  72, 593, 2000, "01234", "5 1/4\" DD 10 x 512 sectors **"},
       {"MFM5-8x512" , 2, 1,  8,  E_MFM5,      0, crcStd,  ddMFMPatterns, 0xcdb4,    166, 211, 689, 2000, "01234", "5 1/4\" DD 8 x 512 sectors"},   // U

       {"DD8"        , 0, 0 , 0,  E_MFM8,      0, crcStd,    dd8Patterns,      0,      0,   0,   0, 1000, "01234", "8\" DD MFM & M2FM **"},
       {"MFM8-52x128", 0, 1, 52,  E_MFM8, O_SIZE, crcStd,    dd8Patterns, 0xcdb4,     138, 182, 195, 1000, "01234", "8\" DD 52 x 128 sectors"},    // needs checking with real disk
       {"MFM8-26x256", 1, 1, 26,  E_MFM8,      0, crcStd,    dd8Patterns, 0xcdb4,     138, 182, 368, 1000, "01234", "8\" DD 26 x 256 sectors"},
       {"M2FM8-INTEL", 0, 1, 52, E_M2FM8,      0, crcStd, ddM2FMPatterns,      0,     75, 111, 195, 1000, "01234", "8\" Intel M2FM DD 52 x 128 sectors"},    // U
       {"M2FM8-HP"   , 1, 0, 30, E_M2FM8,   O_HP, crcRev,   ddHPPatterns, 0xffff,     92, 123, 332, 1000, "01234", "8\" HP DD 30 x 256 sectors"},   // U
       {"TI",     1, 0, 26, E_MFM8,    O_TI, crcStd,   ddTIPatterns, 0xffff,     131, 169, 392, 1000, "34012", "8\" TI 26 x 288 sectors"},

       {"DD5H"       , 1, 0, 16,  E_MFM5H,      0, crcStd,    dd5HPatterns, 0xcdb4,    155, 200, 368, 2000, "01234", "** 5 1/4\" DD hard sectors"},    // only used to probe 
       {"MTECH"      , 1, 0, 16,  E_MFM5H,O_MTECH,   crc8, mtech5Patterns, 0xffff,	    0,   0,  0, 2000, "34012", "Mtech 5 1/4\" DD 16 x 256 hard sectors" },
       {"NSI-DD"  , 2, 1, 10,  E_MFM5H,   O_NSI, crcNSI, nsi5DPatterns, 0xffff,	    0,   0,  0, 2000, "34012", "NSI 5 1/4\" DD 10 x 512 hard sectors"},
       {"\x80""FM5"    , 0, 1, 16,  E_FM5,         0, NULL,  sdPatterns,          0,      0,   0,  0, 4000, "01234", NULL},
       {"\x80""MFM5"   , 0, 1, 16,  E_MFM5,        0, NULL,  dd8Patterns,          0,      0,   0,  0, 2000, "01234", NULL},
       {"\x80""FM8"    , 0, 1, 16,  E_FM8,         0, NULL,  sdPatterns,          0,      0,   0,  0, 2000, "01234", NULL},
       {"\x80""MFM8"   , 0, 1, 16,  E_MFM8,        0, NULL,  dd8Patterns,          0,      0,   0,  0, 1000, "01234", NULL},
       {"\x80""M2FM8"  , 0, 1, 16,  E_M2FM8,       0, NULL,  dd8Patterns,          0,      0,   0,  0, 1000, "01234", NULL},
    {0}
};

char *precannedFormats[][2] = {
    {"PDS", "[0/0]FM5,MFM5-16x256"},
    {NULL, NULL}
};


void showFormats() {
    printf("Current user specified single formats are\n");
    printf("    %-12s  Description\n", "Format");
    for (int i = 0; formatInfo[i].name; i++)
        if (*formatInfo[i].name != 0x80 && formatInfo[i].description)
            printf("    %-12s  %s\n", formatInfo[i].name, formatInfo[i].description);
    printf("Note ** formats will auto adapt based on detected sectors / encoding\n\n");
    printf("Current predefined multi formats are\n");
    for (int i = 0; precannedFormats[i][0]; i++)
        printf("    %-12s %s\n", precannedFormats[i][0], precannedFormats[i][1]);

    printf("\n"
        "Bespoke multi formats can be created using a comma separated list of formats\n"
        "a prefix is used to determine when the format applies; they are in precedence order\n"
        "  [c/h]                     -> format applies to cylinder c head h\n"
        "  [c] or [c/*]              -> format applies to cylinder c\n"
        "  [*/h]                     -> format applies to head h\n"
        "  [*] or [*/*] or no prefix -> format applies to all cylinders / heads\n"
        "if there is no match then flux2imd will attempt to auto detect for format\n\n");

    exit(0);
}




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

bool crcNSI(uint16_t *data, int len) {
    uint8_t crc = 0;
    for (int i = 1; i < len - 1; i++) {     // skip the 0xfb at the start
        crc ^= data[i];
        crc = (crc >> 7) | (crc << 1);
    }
    return crc == (data[len - 1] & 0xff);
}
bool crc8(uint16_t* data, int len) {
    uint16_t crc = 0;
    uint16_t crc1 = 0;
    for (int i = 0; i < len - 1; i++) {
        crc1 += data[i];
        crc = (crc & 0xff) + (data[i] & 0xff) + ((crc & 0x100) ? 1 : 0);
    }
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
    int firstMatch = 0;     // 1 MFM or FM, 2 M2FM, 3 Intel M2FM, 4 HP M2FM, 5 TI
    int limit = cntHardSectors() > 0 ? 100 : 1200;
    retrain(0);
    do {
        // 1200 byte search from previous pattern should be enough to find at least one more pattern
        matchType = matchPattern(limit);
        switch (matchType) {
        case IDAM:
        case INDEXAM:
        case DATAAM:
            if (firstMatch == 1) {
                if (curFormat->encoding == (curFormat + 1)->encoding)
                    return (curFormat + 1)->name;
                else
                    return NULL;
            } else
                firstMatch = 1;
            break;
        case M2FM_IDAM:                 // skip IDAM bytes (ok even for HP as it will just skip 2 GAP bytes)
            for (int i = 0; i < 6; i++)
                getByte();
            firstMatch = 2;
            break;

        case M2FM_INDEXAM:
        case M2FM_DATAAM:
        case M2FM_DELETEDAM:
            if (firstMatch == 2 || firstMatch == 3)
                return "M2FM8-INTEL";
            firstMatch = 3;
            break;
                
        case HP_DATAAM:
        case HP_DELETEDAM:
            if (firstMatch == 2 || firstMatch == 4)
                return "M2FM8-HP";
            firstMatch = 4;
            break;
        case TI_IDAM:
        case TI_DATAAM:
            if (firstMatch == 5)
                return "TI";
            firstMatch = 5;
            break;
        case MTECH_SECTOR:
            if (cntHardSectors() != 16)
                break;
            return getByteCnt() > 50 ? NULL: "MTECH";
        case NSI_SECTOR:
            if (cntHardSectors() != 10)
                break;
            if (getByteCnt() > 50)
                return NULL;
            return curFormat->encoding == E_MFM5H ? "NSI-DD" : "NSI-SD";
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
    case NSI_SECTOR: return "NSI_SECTOR";
    case TI_IDAM: return "TI_IDAM";
    case TI_DATAAM: return "TI_DATAAM";
    }
    return "NO MATCH\n";
}


void makeHS5Patterns(unsigned cylinder, unsigned slot) {
    mtech5Patterns[0].match = encode(0xFF0000 + (cylinder << 8) + slot, 0xA);      // MTECH
}

void makeHS8Patterns(unsigned cylinder, unsigned slot) {
    lsiPatterns[0].match = sd8HPatterns[0].match = encode(flip[(cylinder ? cylinder : 32) * 2 + 1], 0);            // set LSI match pattern
    sd8HPatterns[1].match = encode(((slot + 0x80) << 8) + cylinder, 0); // set ZDS match pattern
}

char *bin64Str(uint64_t pattern) {
    static char binStr[65];
    binStr[64] = 0;
    for (int i = 63; i >= 0; pattern >>= 1, i--)
        binStr[i] = '0' + (pattern & 1);
    return binStr;
}

char *decodePattern64() {
    static char decodeStr[14];
    uint32_t decoded = 0;
    bool suspect = false;
    uint32_t highbits = bits65_66 << 16;
    uint64_t dPattern = pattern;

    for (int i = 48; i >= 0; i -= 16) {
        uint16_t tmp = decode((dPattern >> i) + highbits);
        highbits = 0;
        decoded = (decoded << 8) + (tmp & 0xff);
        if (tmp >= 256)
            suspect = true;
    }
    sprintf(decodeStr, "%08.8X%s", decoded, suspect ? "*" : "");
    return decodeStr;
}

// if a mask had wild cards, test that data matched is valid
// only low 32 half bits will ever be wild
bool chkPattern(uint64_t mask) {
    if ((mask & 0xffffffff) == 0xffffffff)
        return true;
    return !(decode(pattern >> 16) & SUSPECT) && !(decode(pattern) & SUSPECT);

}

int matchPattern(int searchLimit) {
    pattern_t* p;
    int addedBits = 0;
    // scale searchLimit to bits to check 
    for (searchLimit *= 16; searchLimit > 0 && getBit() >= 0; searchLimit--) {
        // speed optimisation, don't consider pattern until at least a byte seen (16 data/clock)
        if (++addedBits >= 16) {
            if (debug & D_PATTERN)              // avoid costly processing unless necessary
                logBasic("%6u: %s %016llX %s\n", getBitCnt(), bin64Str(pattern), pattern, decodePattern64());
            // see if we have a pattern match
            for (p = curFormat->patterns; p->mask; p++) {
                if (((pattern ^ p->match) & p->mask) == 0 && chkPattern(p->mask)) {
                        if (debug & D_ADDRESSMARK)      // avoid costly processing unless necessary
                            logBasic("%u: %016llX %016llX %016llX %s %s\n", getBitCnt(), pattern,
                                p->match, p->mask, decodePattern64(), getName(p->am));
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
        logFull(D_FATAL, "Attempt to select unknown format %s\n", *fmtName == 0x80 ? fmtName + 1 : fmtName);

}

bool setInitialFormat(char *fmtName) {
    int hs = cntHardSectors();
    formatInfo_t *fmt = NULL;

    if (fmtName && (fmt = lookupFormat(fmtName))) {
        if (fmt->encoding == E_FM5H || fmt->encoding == E_FM8H || fmt->encoding == E_MFM5H || fmt->encoding == E_MFM8H) {
            if (hs == 0) {
                logFull(D_WARNING, "format %s is incompatible with soft sector disk\n", fmtName);
                fmt = NULL;
            }
        } else if (hs != 0) {
            logFull(D_WARNING, "format %s is incompatible with hard sector disk\n", fmtName);
            fmt = NULL;
        }
    }
    if (!fmt) {
        int diskSize = getRPM() < 320.0 ? 5 : 8;
        char *testFmt;

        fmtName = NULL;

        if (diskSize == 8)
            testFmt = hs > 0 ? "DD8H" : "DD8";
        else
            testFmt = hs > 0 ? "DD5H" : "DD5";

        // now see if we can detect the type
        if (strcmp(testFmt, "DD8H") == 0) {      // currently only support FM for 8" hard sector, decoder determines type
            setFormat("SD8H");
            DBGLOG(D_DETECT, "8\" hard sector trying SD8H %s\n", testFmt);
            return true;
        }  else {
            int blks = hs > 0 ? hs : 1;         // number of blocks to try for a whole track
            DBGLOG(D_DETECT, "Trying %s\n", testFmt);
            setFormat(testFmt);

            for (int i = 0; !fmtName && i < blks && seekBlock(i) >= 0; i++)        // try the MFM formats
                fmtName = probe();
            if (!fmtName) {
                if (diskSize == 8)
                    testFmt = hs > 0 ? "SD8H" : "SD8";
                else
                    testFmt = hs > 0 ? "SD5H" : "SD5";

                DBGLOG(D_DETECT, "Trying %s\n", testFmt);
                setFormat(testFmt);

                for (int i = 0; !fmtName && i < blks && seekBlock(i) >= 0; i++)        // try the MFM formats
                    fmtName = probe();

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



char *getFormat(const char *userfmt, int cyl, int head) {
    const char *s;
    int i;
    static char format[32]; // used to hold current format
    int score = 0;          // 0 no match, 1 match any, 2 match head, 3 match cylinder, 4 match both
    int tscore;             // current test score
    const char *match = ""; // where highest score matched

    if (userfmt)
        for (int i = 0; precannedFormats[i][0]; i++)
            if (_stricmp(userfmt, precannedFormats[i][0]) == 0) {
                userfmt = precannedFormats[i][1];
                break;
            }

    for (s = userfmt; score != 4 && s && *s && (*s != ',' || *++s); s = strchr(s, ',')) {
        if (*s != '[')
            tscore = 1;
        else {
            if (isdigit(*++s)) {        // we have a cylinder specified
                int mcyl = *s - '0';
                while (isdigit(*++s))
                    mcyl = mcyl * 10 + *s - '0';
                if (mcyl != cyl)        // doesn't match the cylinder
                    continue;
                tscore = 3;             // match cylinder
            } else if (*s == '*')       // match any
                tscore = 1;
            else
                continue;               // bad format should be [nn  or [*
            if (*++s == '/') {          // we have head specified
                if (isdigit(*++s)) {
                    int mhead = *s - '0';
                    while (isdigit(*++s))
                        mhead = mhead * 10 + *s - '0';
                    if (mhead != head)      // head mismatch
                        continue;
                    score++;                // convert to head match or both match
                } else if (*s++ != '*')     // error if not wild card
                    continue;
            }
            if (*s++ != ']')                // should have closing ]
                continue;
        }
        if (tscore > score) {               // better match
            score = tscore;
            match = s;
        }
    }
    if (*match == 0)                         // revert to auto match
        return NULL;
    for (i = 0; i < 31 && *match && *match != ','; i++) // copy matched format
        format[i] = *match++;
    format[i] = 0;
    return format;
}
