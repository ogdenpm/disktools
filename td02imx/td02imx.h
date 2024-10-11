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
#define SEC_DUP     0x01 // Sector was duplicated - no longer used in Teledisk
#define SEC_CRC     0x02 // Sector has CRC error
#define SEC_DAM     0x04 // Sector has Deleted Address Mark
#define SEC_DOS     0x10 // Sector not allocated
#define SEC_NODAT   0x20 // Sector has no data field
#define SEC_AUTO    0x40 // auto generated sector

// extra flag bits
#define F_USED      1 // sector is used
#define F_DUP       2 // sector is duplicate, with different content

// option flags
#define K_CRC       1
#define K_DAM       2
#define K_AUTO      4
#define K_AUTOCRC   8
#define K_GAP       0x10
#define K_TRACK     0x20
#define S_OO        0x40
#define S_OB        0x80
#define NOHEAD0     0x200   // special to skip head 0 processing

#define MAXSECTOR   64 // Note DOS can only handle up to 64 sectors per track
#define UNALLOCATED 0x100
#define MAXIDAMS    110 // actually probably only needs to be 100 which is Teledisk limit

#define MAXCYLINDER 96
#define MAXHEAD     2


typedef struct {
    uint8_t nSec;       // number of sector entries
    uint8_t nUsable;    // number after de-duplication
    uint8_t first;      // lowest sector number
    uint8_t last;       // highest sector number
    uint8_t sSize;      // sector size = 128 << sSize
    uint8_t encoding;   // 0->MFM, 1->FM]
    uint8_t spacing;    // sector spacing interval
    uint8_t tFlags;     // not a standard layout so no padding
    uint32_t trackSize; // calculated track size from valid Address Markers
    sector_t *sectors;  // sector entries nSec is number of sector_t entries
    uint8_t *sectorMap; // map of usable sectors, indexes into sectors
} track_t;

// tFlags
#define TRK_NONSTD  1 // track has mixed sector sizes or multiple cylinder / head changes
#define TRK_HASGAP  2 // there are non sequential sector Ids
#define TRK_HASBAD  4 // the first or last sector has bad CRC
#define TRK_HASAUTO 8 // there is an auto generated sector

extern track_t disk[MAXCYLINDER][MAXHEAD];

extern uint8_t nCylinder;
extern uint8_t nHead;

// tracks sector layout info treating FM & MFM separately
typedef struct {
    uint8_t count;  // count of entries
    uint16_t first; // sum of all the "first sector values" when fixed it is the rounded up average
    uint16_t last;  // same but for last sector values
    uint16_t spacing; // same but for sector spacing
} sizing_t;

#define MIXEDSIZES 8
#define SUSPECT    9

extern sizing_t sizing[2][SUSPECT + 1][2]; // [encoding][sSize][head]

extern char *srcFile;
void saveImage(char const *outFile, td0Header_t *pHead, uint16_t options);

bool sameValues(uint8_t *buf, uint16_t len);