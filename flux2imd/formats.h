// This is an open source non-commercial project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

#pragma once
#include <stdint.h>
#include <stdbool.h>

#define SUSPECT 0x100     // marker added to data if clock bit error
#define HSIDAM   6        // synthetic HS IDAM

// types of marker detected
enum {
    GAP= 0x1ff, SYNC = 0x100, IBM_GAP = 0x4e, INDEXAM = 0xfc, IDAM = 0xfe, DATAAM = 0xfb, DELETEDAM = 0xf8,
    M2FM_INDEXAM = 0xc, M2FM_IDAM = 0xe, M2FM_DATAAM = 0xb, M2FM_DELETEDAM = 0x8,   // INTEL M2FM flavour
    HP_IDAM = 0x10e, HP_DATAAM = 0xa , HP_DELETEDAM = 0xf,                          // HP M2FM flavour
    TI_IDAM = 0x10a, TI_DATAAM = 0x10b,
    LSI_SECTOR = 0x200, ZDS_SECTOR = 0x300,  MTECH_SECTOR = 0x400,                  // Hard sector specials
    NSI_SECTOR = 0x500, NSI_IDAM = 0x600};                 


enum encodings {
    E_FM5 = 0, E_FM5H, E_FM8, E_FM8H, E_MFM5,  E_MFM5H, E_MFM8, E_MFM8H, E_M2FM8  // raw encodings supported although E_MFM8 is currently used for M2FM8 detection
};
enum options {
    O_UINV = 0x400,                     // user data invert
    O_NOIMD = 0x200,
    O_INV = 0x100,                      // bit to indicate bits inverted
    O_REV = 0x80,                       // bit to indicate reversed bytes
    O_SIZE = 0x40,                      // bit to indicate auto size detection
    O_SPC = 0x20,                       // bit to indicate auto spacing detection
    O_ZDS = 1 + O_NOIMD,                // encodings for special formats
    O_LSI = O_REV + 2,
    O_HP = O_REV + 3,
    O_MTECH = O_NOIMD + 4,
    O_NSI = 5,
    O_TI = O_NOIMD + 6
};



typedef struct {
    uint64_t mask;
    uint64_t match;
    uint16_t am;
} pattern_t;


typedef struct {
    char* name;
    int sSize;      // sector len = 128 << ssize
    int firstSectorId;
    int spt;
    int encoding;
    unsigned options;
    bool (*crcFunc)(uint16_t *data, int len);
    pattern_t* patterns;
    uint16_t crcInit;
    int firstIDAM;
    int firstDATA;
    int spacing;
    unsigned nominalCellSize;
    char *profileOrder;
    char *description;
} formatInfo_t;

extern formatInfo_t *curFormat;
int decode(uint64_t pattern);
uint64_t encode(uint32_t val, uint32_t prevPattern);
int getByte();
char *getName(int am);
void makeHS5Patterns(unsigned cylinder, unsigned slot);
void makeHS8Patterns(unsigned cylinder, unsigned slot);
int matchPattern(int searchLimit);
void setFormat(char *fmtName);
bool setInitialFormat(char *fmtName);
bool crc8(uint16_t* data, int len);
char *getFormat(const char *userfmt, int cyl, int head);
void showFormats();