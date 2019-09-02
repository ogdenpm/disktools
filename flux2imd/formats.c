#include <stdint.h>
#include <stdbool.h>
#include "sectorManager.h"
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



static bool lsiCrc(uint16_t* data, int len);
static bool zdsCrc(uint16_t* data, int len);
static bool revCrc(uint16_t* data, int len);
static bool stdCrc(uint16_t* data, int len);
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
    assumed i.e. FM8H. The determination of the true format is done in hsGetTrack as encodings
    are non standard and examples so far have start information dependent on the sector
*/
formatInfo_t formatInfo[] = {
 //   name        siz 1st spt    enc     opt   crc         pattern      crcinit   idam data spc
    {"SD5"        , 0, 1, 16,   E_FM5,      0, stdCrc,     sdPatterns, 0xffff,     77,  99, 188}, 
    {"FM5"        , 0, 1, 16,   E_FM5,  O_SPC, stdCrc,   sdFMPatterns, 0xffff,     77,  99, 192},
    {"FM5-16x128" , 0, 1, 16,   E_FM5,      0, stdCrc,   sdFMPatterns, 0xffff,     77,  99, 188},
    {"FM5-15x128" , 0, 1, 15,   E_FM5,      0, stdCrc,   sdFMPatterns, 0xffff,     82, 106, 196},

    {"SD8"        , 0, 1, 26,   E_FM8,      0, stdCrc,     sdPatterns, 0xffff,     90, 115, 194},
    {"FM8-26x128" , 0, 1, 26,   E_FM8,      0, stdCrc,   sdFMPatterns, 0xffff,     39, 64, 191},    // manually adjusted 

    {"FM8H"       , 0, 0, 32,  E_FM8H,      0, zdsCrc,     hsPatterns,      0, HSIDAM,   8, 168},
    {"FM8H-ZDS"   , 0, 0, 32,  E_FM8H,  O_ZDS, zdsCrc, &hsPatterns[1],      0, HSIDAM,  18, 168},
    {"FM8H-LSI"   , 0, 0, 32,  E_FM8H,  O_LSI, lsiCrc,     hsPatterns,      0, HSIDAM,   8, 168}, 

    {"DD5"        , 1, 1, 16,  E_MFM5,      0, stdCrc,    dd5Patterns, 0xcdb4,    155, 200, 368},
    {"MFM5"       , 1, 1, 16,  E_MFM5, O_SIZE, stdCrc,  ddMFMPatterns, 0xcdb4,    155, 200, 368},
    {"MFM5-16x256", 1, 1, 16,  E_MFM5,      0, stdCrc,  ddMFMPatterns, 0xcdb4,    155, 200, 375},   // 375 works for both seen variants
    {"MFM5-8x512" , 2, 1,  8,  E_MFM5,      0, stdCrc,  ddMFMPatterns, 0xcdb4,    166, 210, 686}, 

    {"DD8"        , 0, 0 , 0,  E_MFM8,      0, stdCrc,    dd8Patterns,      0,      0,   0,   0},
    {"M2FM8-INTEL", 0, 1, 52, E_M2FM8,      0, stdCrc, ddM2FMPatterns,      0,     70, 106, 194},
    {"M2FM8-HP"   , 1, 0, 30, E_M2FM8,   O_HP, revCrc,   ddHPPatterns, 0xffff,     91, 122, 332},
    {0}
};
formatInfo_t *curFormat;

static bool lsiCrc(uint16_t* data, int len) {
    uint16_t crc = curFormat->crcInit;
    for (int i = 0; i < len - 2; i++)
        crc += data[i] & 0xff;
    return crc == ((data[len - 2] & 0xff) + ((data[len - 1] & 0xff) << 8));

}

static bool zdsCrc(uint16_t* data, int len) {
#define CRC16 0x8005
    uint16_t crc = curFormat->crcInit;
    len -= 2;               // exclude postamble
    for (int i = 0; i < len; i++)
        for (uint16_t mask = 0x80; mask; mask >>= 1)
            crc = ((crc << 1) | ((data[i] & mask) ? 1 : 0)) ^ ((crc & 0x8000) ? CRC16 : 0);
    return crc == 0;
}

static bool revCrc(uint16_t* data, int len) {
    uint8_t x;
    uint16_t crc = curFormat->crcInit;

    while (len-- > 0) {
        x = (crc >> 8) ^ (flip(*data++) & 0xff);
        x ^= x >> 4;
        crc = (crc << 8) ^ (x << 12) ^ (x << 5) ^ x;
    }
    return crc == 0;
}

static bool stdCrc(uint16_t* buf, int len) {
    uint8_t x;
    uint16_t crc = curFormat->crcInit;

    while (len-- > 0) {
        x = (crc >> 8) ^ (*buf++ & 0xff);
        x ^= x >> 4;
        crc = (crc << 8) ^ (x << 12) ^ (x << 5) ^ x;
    }
    return crc == 0;
}


uint64_t encodeFM(uint32_t val) {
    uint64_t fmVal = 0;
    for (uint32_t i = 0x80000000; i; i >>= 1)
        fmVal = (fmVal << 2) + ((val & i) ? 3 : 2);
    return fmVal;
}


void makeHSPatterns(unsigned cylinder, unsigned slot)     {
    hsPatterns[0].match = encodeFM(flip((cylinder ? cylinder : 32) * 2 + 1));            // set LSI match pattern
    hsPatterns[1].match = encodeFM(((slot + 0x80) << 8) + cylinder); // set ZDS match pattern

}

void setFormat(char* fmtName) {
    curFormat = lookupFormat(fmtName);
    if (!curFormat)
        logFull(FATAL, "Attempt to select unknown format %s\n", fmtName);

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
    }
    return "NO MATCH\n";
}
