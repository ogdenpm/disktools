#pragma once
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef _MSC_VER
#define strnicmp strncasecmp
#endif


// the following is used to record the i data to emit
// when loading a track sectors with inconsistent trk or head
// are noted and the trk and head are replaced with the data from
// the track header

// once all sectors are loaded, the data is sorted dependent on the data allocation model
// ALT  - default, layout by ptrack, phead (also works for single sided)
// ROUTOUT - layout phead, ptrack
// ROUTBACK = layout phead 0, ascending ptrack then phead 1, descending ptrack
//
// In all cases the sectors within a track are sorted as follows
// ASC - ascending order
// DEC - descending order
// RAW - not sorted
//
// the sectors are then written out in order, i length padding when data is NULL
// Duplicate are handled as follows
// all no data - no warning
// i with data, rest with no data - no warning
// sectors with same data - no warning
// else warn and skip duplicate

// note the structure below assumes bytes are layed out without gaps
typedef struct {
    char sig[3];   // signature "TD" or "td", null terminated string
    uint8_t check, // used on multiple disk packs
        ver,       // only 10-21 supported.
        dataRate,  // see showHeader for how the 3 values are processed
        driveType, stepping,
#define HAS_COMMENT 0x80 // stepping top bit indicates comment record follows
        dosMode,         // != 0 image contains DOS allocated sectors only
        surfaces;        // number of surfaces
    uint8_t crc[2];      // checksum of 1st 10 bytes in record le word
} fileHdr_t;

// all data post the fileHdr is potentially compressed dependent on sig being "td"

#define MAXCOMMENT 584 // determined by partial diassembly of teledisk
typedef struct {
    struct commentHdr_t {
        uint8_t crc[2], //  checksum of 8 bytes from &len to end of record le word
            len[2];     // length of string data region following date le word
        uint8_t yr, mon, day, hr, min, sec; // date and time info. yr = year - 1900
    };
    uint8_t comment[MAXCOMMENT + 2];    // allow for trailing "\n" if necessary
} comment_t;

/*
 track_rec.crc below calculates crc for 1st three bytes
 of record, but only used the low byte of calculated value
 for comparison.
*/

typedef struct {
    uint8_t nsec, trk, head, crc; // nsec == 255 indicates end of processing
} trkHdr_t;

typedef struct {
    struct sec_hdr {
        uint8_t trk;
        uint8_t head;
        uint8_t sec;
        uint8_t sSize; // size is 128 << secs;
        uint8_t sFlags;
        uint8_t crc; // if there is data this is the low crc of the data
    } hdr;
    uint8_t flags; // flags to indicate i processing
    uint8_t *data; // null if no actual data;
} sector_t;

// Teledisk sector flag names taken from Dave Dunfield's td02imd
#define SEC_DUP   0x01 // Sector was duplicated
#define SEC_CRC   0x02 // Sector has CRC error
#define SEC_DAM   0x04 // Sector has Deleted Address Mark
#define SEC_DOS   0x10 // Sector not allocated
#define SEC_NODAT 0x20 // Sector has no data field
#define SEC_NOID  0x40 // Sector has no IDAM
#define INTERLEAVE 0x80

#define F_DUP     0x1 // sector is duplicate, with different content

enum { ALT = 0, OUTOUT = 1, OUTBACK = 2 };

enum { NORMAL = 0, NOMISSING = 1, LEAVECRC = 2 };

#define MAXPATTERN 0x4000
#define MAXSECTOR 64 // Note DOS can only handle up to 64 sectors per track
#define UNALLOCATED 0x100

typedef struct {
    uint8_t len[2];
    uint8_t pattern[MAXPATTERN];
} patBuf_t;


#define MAXCYLINDER 80
#define MAXHEAD     2

uint8_t encodingsUsed;
uint8_t sizingsUsed;
typedef struct {
    uint8_t first;
    uint8_t last;
    uint8_t sSize; // set to MIXEDSIZES for multiple sector sizes
    uint8_t encoding;
    uint8_t mapLen; // real number of sectors to write
    uint8_t *sectorMap;
    uint32_t trackSize;
    sector_t *sectors;
} track_t;

extern track_t tracks[MAXCYLINDER][MAXHEAD];

extern uint8_t nCylinder;
extern uint8_t nHead;



#define MFM 1
#define FM  2


// tracks sector layout info treating FM & MFM separately
typedef struct {
    uint8_t count;
    uint8_t first;
    uint8_t nSec;
} sizing_t;

#define MIXEDSIZES 8
extern sizing_t sizing[2][9][2]; // [encoding][sSize][head]
extern comment_t cHead; // used for comment extraction in IMD file
extern fileHdr_t fHead;
extern char *td0File;
// read word as little endian, avoids  mutating original which would impact crc

#define WORD(n) ((n)[0] + ((n)[1] << 8))

#define COPY    0
#define REPEAT    1
#define RLE   2


_Noreturn void fatal(char const *fmt, ...);
void warn(char const *fmt, ...);
bool Decode(uint8_t *buf, uint16_t len);
bool oldAdv(uint8_t *buf, uint16_t len);
void init_Decode(FILE *fp);
void oldReset();
uint16_t do_crc(void *buf, uint16_t len, uint16_t);
void saveImage(char const *outFile, uint8_t keep, uint8_t layout);
bool readIn(uint8_t *buf, uint16_t len);
#ifdef _MSC_VER
char *basename(char *path);
#endif