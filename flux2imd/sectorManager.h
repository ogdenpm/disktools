#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "decoders.h"
#include "bits.h"

#define MAXSECTOR 52
#define MAXTRACK 84
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
    uint8_t cylinder;
    uint8_t side;
    uint8_t sectorId;
    uint8_t sSize;
} idam_t;

typedef struct {
    unsigned len;           // of data including CRC & any link data
    uint16_t rawData[0];       // extended as needed 
} sectorData_t;

typedef struct _sectorDataList {
    struct _sectorDataList* next;
    sectorData_t sectorData;           // unnamed structure
} sectorDataList_t;



typedef struct _sector {
    unsigned status;
    int sectorId;
    sectorDataList_t* sectorDataList;
} sector_t;

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
} formatInfo_t;

typedef struct {
    unsigned status;
    int cylinder;
    int side;
    formatInfo_t* fmt;
    int cntGoodIdam;
    int cntGoodData;
    int cntAnyData;
    uint8_t sectorToSlot[MAXSECTOR];        // index -> sectorId - firstSectorId
    uint8_t slotToSector[MAXSECTOR];
    sector_t sectors[];
} track_t;


extern formatInfo_t formatInfo[];
extern formatInfo_t *curFormat;
unsigned slotAt(unsigned pos, bool isIdam);
void addIdam(unsigned pos, idam_t* idam);
void addSectorData(unsigned pos, bool isGood, unsigned len, uint16_t rawData[]);

void initTrack(unsigned cylinder, unsigned side);
void updateTrackFmt();
void finaliseTrack();


bool checkTrack();
track_t* getTrack(unsigned cylinder, unsigned side);
void removeSectorData(sectorDataList_t* p);

extern track_t* trackPtr;
formatInfo_t* lookupFormat(char* fmtName);
void makeHSPatterns(unsigned cylinder, unsigned slot);
void setFormat(char* fmtName);
void removeDisk();
void resetTracker();