#pragma once
#include "zip.h"

#define SCK 24027428.5714285            //  sampling clock frequency
#define ICK 3003428.5714285625

#define USCLOCK                 24           // 1 uS clock used for flux calculations
#define USCLOCK_D               (SCK / 1000000.0) // 1 uS clock floating point for timing to byte no
#define HALFBIT500              USCLOCK
#define HALFBIT250              (2 * USCLOCK)
#define HALFBIT300              (5 * USCLOCK / 3)

typedef unsigned char byte;
typedef unsigned short word;

#define MAXCYLINDER       80
#define MAXSECTORS      52
#define MAXHEAD         2
#define MAXSECTORSIZE   1024
#define DDTRACKSIZE     8192
#define SDTRACKSIZE     4096

enum {
    END_BLOCK = -1,
    END_FLUX = -2,
};

enum {
    HASID = 1,
    HASDATA = 2
};

typedef struct {
    byte mode;
    byte spt;                    // sectors per track
    byte cyl;                   // cylinder
    byte head;
    int  size;                  // sector size in bytes
    byte firstSector;           // first sector (0 or 1)
    byte smap[MAXSECTORS];       // physical slot to sector map
    bool hasData[MAXSECTORS];    // in physical slot order
    byte track[DDTRACKSIZE];    // in physical slot order
} imd_t;

enum {      // bit combinations returned by nextBits
    NONE = 0,
    BIT0,
    BIT0M,  // 0 with marker
    BIT1,
    BIT1M,  // 1 with marker
    BIT0S,  // resync to 0
    BIT1S,  // resync to 1
    BITEND, // no more bits
    BITBAD,  // illegal flux pattern

    BITSTART,   // used to signal start of bitstream for bitLogging
    BITFLUSH,    // used to flush bitstream for bitLogging
    BITFLUSH_1,   // variant that flushes all but last bit (used for marker alignment)
};

enum {      // disk format types
    FM = 0,
    MFM = 1,
    M2FM = 2,
    UNKNOWN_FMT
};

#define MINSAMPLE    40000
#define MINPERCENT   2

extern int debug;
extern bool showSectorMap;

extern char curFile[];
extern imd_t curTrack;

#define LINELEN 80
enum {
    ALWAYS = 0, MINIMAL, VERBOSE, VERYVERBOSE
};

int getM2FMBit(bool resync);
int getMFMBitNew(bool resync);
int getMFMBitTest(bool resync);
int getFMBit(bool resync);
int getMarkerOld();
int getMarkerNew();
int seekBlock(int blk);
void resetFlux();
void freeMem();
size_t where();
long when(int val);
void *xalloc(void *buf, size_t size);
size_t readFluxBuffer(byte *buf, size_t bufsize);

void addIMD(imd_t *trackPtr);
void WriteImgFile(char *fname, char *comment);
_declspec(noreturn) void error(char *fmt, ...);
void logger(int level, char *fmt, ...);
void displayHist(int levels);
void resetIMD();
void flux2track();
int getNextFlux();

int bitLog(int bits);
bool analyseFormat();
int time2Byte(long time);