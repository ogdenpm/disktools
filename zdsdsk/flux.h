#pragma once
#include "zip.h"
#include <stdint.h>

#define SCK 24027428.5714285            //  sampling clock frequency
#define ICK 3003428.5714285625

#define USCLOCK                 24           // 1 uS clock used for flux calculations
#define USCLOCK_D               (SCK / 1000000.0) // 1 uS clock floating point for timing to byte no
#define NSCLOCK					24027



typedef unsigned char byte;
typedef unsigned short word;

enum {
    END_BLOCK = -1,
    END_FLUX = -2,
	BAD_FLUX = -3
};

#define NUMCYLINDER		77
#define NUMSECTOR 32
#define SECSIZE			128
#define SECMETA			8
#define SECPOSTAMBLE	2	// should be 0, 0
typedef union {
	uint16_t raw[SECMETA + SECSIZE + SECPOSTAMBLE];
	struct {
		uint16_t sector;
		uint16_t track;
		uint16_t data[SECSIZE];
		uint16_t bsector, btrack, fsector, ftrack;
		uint16_t crc1, crc2;
		uint16_t post[2];
	};
} sector_t;

typedef struct _secList {
	struct _secList* next;
	uint16_t	flags;
	sector_t secData;
} secList_t;

typedef struct {
	size_t fluxPos;
	size_t bitPos;
} location_t;

#define MINSAMPLE    40000
#define MINPERCENT   2

extern int debug;
extern bool showSectorMap;

extern char curFile[];

#define LINELEN 80
enum {
    ALWAYS = 0, MINIMAL, VERBOSE, VERYVERBOSE
};

int seekBlock(int blk);
void resetFlux();
void freeMem();
location_t where();
long when(int val);
void *xmalloc(size_t size);
void openFluxBuffer(byte *buf, size_t bufsize);

_declspec(noreturn) void error(char *fmt, ...);
void logger(int level, char *fmt, ...);
void displayHist(int levels);
void flux2track();
int getNextFlux();
void displaySector(sector_t* sec, bool showSuspect);
void dumpTrack();
void ungetFlux(int val);
bool seekSector(location_t pos, size_t lastPos);
int getFMByte(int bcnt, int adaptRate);
location_t endWhere();