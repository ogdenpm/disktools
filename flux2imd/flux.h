#pragma once
#include <stdint.h>

// default smample & index clocks
#define SCK 24027428.5714285            // sampling clock frequency
#define ICK 3003428.5714285625
#define HS8INCH	32						// 32 sectors for 8" else assume 5 1/4"
#define RPS8INCH		6				// rotations per second for 8"
#define RPS5INCH		5				// rotations per second for 5 1/4"

typedef struct {
    uint8_t* inPtr;
    uint8_t* endPtr;
    uint32_t totalSampleCnt;             // running sample count to remove rouding bias
    uint64_t prevNsCnt;                  // previous running ns count
} location_t;

extern double sck;		// clock ticks per ns
extern void* xmalloc(size_t size);

int seekBlock(unsigned num);
double where();
int32_t getNextFlux();
int loadFlux(uint8_t* image, size_t size);
void unloadFlux();
void getLocation(location_t *p);
void setLocation(location_t *p);
int cntHardSectors();
int getRPM();



