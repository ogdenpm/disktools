#pragma once

#include "td0.h"
#include <stdarg.h>


#ifdef _MSC_VER
#ifndef strcasecmp
#define strcasecmp stricmp
#endif
#ifndef strncasecmp
#define strncasecmp strnicmp
#endif
#else
#include <strings.h>
#endif

// the following is used to record thedata to emit
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




// Teledisk sector flag names taken from Dave Dunfield's td02imd
#define SEC_DUP    0x01 // Sector was duplicated
#define SEC_CRC    0x02 // Sector has CRC error
#define SEC_DAM    0x04 // Sector has Deleted Address Mark
#define SEC_DOS    0x10 // Sector not allocated
#define SEC_NODAT  0x20 // Sector has no data field
#define SEC_NOID   0x40 // Sector has no IDAM
#define INTERLEAVE 0x80

#define F_USED     1 // sector is used
#define F_DUP      2 // sector is duplicate, with different content

enum { ALT = 0, OUTOUT = 1, OUTBACK = 2 };

enum { NORMAL = 0, NOMISSING = 1, LEAVECRC = 2 };

#define MAXSECTOR   64 // Note DOS can only handle up to 64 sectors per track
#define UNALLOCATED 0x100

#define MAXCYLINDER 80
#define MAXHEAD     2

uint8_t encodingsUsed;
uint8_t sizingsUsed;

typedef struct {
    uint8_t nSec;
    uint8_t nUsable;
    uint8_t first;
    uint8_t last;
    uint8_t sSize;
    uint8_t encoding;
    uint32_t trackSize;
    sector_t *sectors;
    uint8_t *sectorMap;
} track_t;



extern track_t disk[MAXCYLINDER][MAXHEAD];

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

extern char *srcFile;
void saveImage(char const *outFile, td0Header_t *pHead, uint8_t keep, uint8_t layout);

