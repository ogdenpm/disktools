#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "decoders.h"
#include "bits.h"

#define MAXSECTOR 52
#define MAXTRACK 80
#define EOTGAP  210
#define POSJITTER   10

enum {
    SS_IDAMGOOD = 1, SS_DATAGOOD = 2, SS_GOOD = 3, SS_FIXED = 4,
    TS_ALLID = 1, TS_ALLDATA = 2, TS_FIXEDID = 4, TS_BADID = 8
};


enum encodings {
    E_FM5, E_FM8, E_FM8H, E_MFM5, E_MFM8, E_M2FM8,   // raw encodings supported although MFM8 is currently used for M2FM8 detection
};
enum options {

    O_REV = 0x80,                       // bit to indicate reversed bytes
    O_SIZE = 0x40,                      // bit to indicate auto size detection
    O_SPC = 0x20,                       // bit to indicate auto spacing detection
    O_ZDS = 1,                          // encodings for special formats
    O_LSI = O_REV + 2,
    O_HP = O_REV + 3
};
typedef struct {
    uint8_t track;
    uint8_t side;
    uint8_t sectorId;
    uint8_t sSize;
} idam_t;

typedef struct {
    uint16_t len;           // of data including CRC & any link data
    uint16_t rawData[0];       // extended as needed 
} sectorData_t;

typedef struct _sectorDataList {
    struct _sectorDataList* next;
    sectorData_t sectorData;           // unnamed structure
} sectorDataList_t;



typedef struct _sector {
    uint8_t status;
    uint8_t sectorId;
    sectorDataList_t* sectorDataList;
} sector_t;

typedef struct {
    char* shortName;
    uint8_t sSize;      // sector len = 128 << ssize
    uint8_t firstSectorId;
    uint8_t spt;
    uint8_t encoding;
    uint8_t options;
    bool (*crcFunc)(uint16_t *data, uint16_t len);
    pattern_t* patterns;
    uint16_t crcInit;
    int firstIDAM;
    int firstDATA;
    int spacing;
} formatInfo_t;

typedef struct {
    uint16_t status;
    uint8_t track;
    uint8_t side;
    formatInfo_t* fmt;
    uint16_t cntGoodIdam;
    uint16_t cntGoodData;
    uint16_t cntAnyData;
    uint8_t sectorToSlot[MAXSECTOR];        // index -> sectorId - firstSectorId
    uint8_t slotToSector[MAXSECTOR];
    sector_t sectors[];
} track_t;


extern formatInfo_t formatInfo[];
extern formatInfo_t *curFormat;
uint8_t slotAt(uint16_t pos, bool isIdam);
void addIdam(uint16_t pos, idam_t* idam);
void addSectorData(uint16_t pos, bool isGood, uint16_t len, uint16_t rawData[]);

void initTrack(uint8_t track, uint8_t side);
void updateTrackFmt();
void finaliseTrack();


bool checkTrack();
track_t* getTrack(uint8_t track, uint8_t side);
void removeSectorData(sectorDataList_t* p);

extern track_t* trackPtr;
formatInfo_t* lookupFormat(char* fmtName);
void makeHSPatterns(uint8_t track, uint8_t slot);
void setFormat(char* fmtName);
void removeDisk();
void resetTracker();