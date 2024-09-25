#pragma once
// standard headers
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// internal format of td0 structures
// they are read  in as bytes, and assumes
// uint8_t are byte aligned

typedef struct {
    char sig[3];         // signature "TD" or "td",  null terminated string
    uint8_t check;       // used on multiple disk packs
    uint8_t ver;         // only 10-21 supported.
    uint8_t density;     // see showHeader for how the 3 values are processed
    uint8_t driveType;   // dos drive type
    uint8_t stepping;    // determines 48/96 top and if comment follows
#define HAS_COMMENT 0x80 // stepping top bit indicates comment record follows
    uint8_t dosMode;     // != 0 image contains DOS allocated sectors only
    uint8_t surfaces;    // number of surfaces
    uint8_t fCrc[2];     // checksum of 1st 10 bytes in record le word
} td0FileHeader_t;

// all data post the td0 file header is potentially compressed dependent on sig being "td"

#define MAXCOMMENT 584 // determined by partial diassembly of teledisk
typedef struct {
    uint8_t cCrc[2]; //  checksum of 8 bytes from &len to end of record le word
    uint8_t len[2];  // length of string data region following date le word
    uint8_t yr;      // date and time info. yr = year - 1900
    uint8_t mon;
    uint8_t day;
    uint8_t hr;
    uint8_t min;
    uint8_t sec;
    // uint8_t comment[len];    // handled explicitly
} td0CommentHeader_t;

typedef struct {
    uint8_t nSec; // nSec == 255 indicates end of processing
    uint8_t cyl;
    uint8_t head;
    uint8_t tCrc;
} td0TrackHeader_t;

typedef struct {
    uint8_t cyl;
    uint8_t head;
    uint8_t sec;
    uint8_t sSize; // size is 128 << sSize;
    uint8_t flags;
    uint8_t sCrc; // the low crc of data data if present else of the 5 bytes above
} td0SectorHeader_t;

#define MAXPATTERN 0x4000
typedef struct {
    uint8_t len[2];
    uint8_t pattern[MAXPATTERN];
} td0SectorData_t;

// external facing structures
// note gcc doesn't allow tagged anonymous structures
typedef struct {
    struct {               // td0FileHeader
        char sig[3];       // signature "TD" or "td",  null terminated string
        uint8_t check;     // used on multiple disk packs
        uint8_t ver;       // only 10-21 supported.
        uint8_t density;   // see showHeader for how the 3 values are processed
        uint8_t driveType; // dos drive type
        uint8_t stepping;  // determines 48/96 top and if comment follows
#define HAS_COMMENT 0x80   // stepping top bit indicates comment record follows
        uint8_t dosMode;   // != 0 image contains DOS allocated sectors only
        uint8_t surfaces;  // number of surfaces
        uint8_t fCrc[2];   // checksum of 1st 10 bytes in record le word
    };
    struct {             // td0CommentHeader
        uint8_t cCrc[2]; //  checksum of 8 bytes from &len to end of record le word
        uint8_t len[2];  // length of string data region following date le word
        uint8_t yr;      // date and time info. yr = year - 1900
        uint8_t mon;
        uint8_t day;
        uint8_t hr;
        uint8_t min;
        uint8_t sec;
        // uint8_t comment[len];    // handled explicitly
    };
    char comment[MAXCOMMENT + 2]; // +2 to allow for trailing \n\0 if necessary
} td0Header_t;

typedef struct {
    struct { // td0 sector header
        uint8_t cyl;
        uint8_t head;
        uint8_t sec;
        uint8_t sSize; // size is 128 << sSize;
        uint8_t flags;
        uint8_t sCrc;
    };
    uint8_t extraFlag; // used to record usage / duplicate sector with different data
    uint8_t *data;     // decoded data, NULL if not used
} sector_t;

typedef struct {
    struct {          // td0 track header
        uint8_t nSec; // nSec == 255 indicates end of processing
        uint8_t cyl;
        uint8_t head;
        uint8_t tCrc;
    };
    sector_t *sectors; // allocated array sized [nSec]
} td0Track_t;

// user supplied functions
_Noreturn void fatal(char const *fmt, ...);
void warn(char const *fmt, ...);
char *basename(char *path);

// public interface
uint16_t calcCrc(void *buf, uint16_t len, uint16_t crc);
td0Header_t *openTd0(char *name);
td0Track_t *readTrack();
void closeTd0();
bool readTd0(void *buf, uint16_t len);
void *safeMalloc(size_t size);
