#pragma once
#include <stdint.h>
#include <stdbool.h>


#define EODATA      -1      // end of data
#define SODATA      -2      // used to mark the start of data
#define SSSTART     -3      // start of a soft sector track
// HSTART is only used in inline getTs, otherwise sector number is used
#define HSSTART     -4      // subtract sector number from this




typedef struct {
    uint32_t pos;       // this is the index of the next flux sample
    int32_t ts;         // time of index in ns from start of data for SODATA this is <= 0
    int16_t itype;      // EODATA, SSSTART, hard sector slot number
} Index;


typedef bool (*OnIndex)(int16_t itype);



void beginFlux(uint32_t sampleCnt, int indexCnt, double sclk, double rpm, int16_t hsCnt);  // indexCnt should include index holes + 1 for EODATA. SODATA allocated internally
void setActualRPM(double rpm);
void addDelta(uint32_t delta);
void addIndex(int16_t itype, uint32_t delta);
void endFlux();


int seekIndex(uint16_t index);               // sets current position to first sample after ts, returns type, or EODATA if out of range
int16_t getType(uint16_t index);
int32_t peekTs();
int32_t getTs();
uint16_t getHsCnt();
double getRPM();
OnIndex setOnIndex(OnIndex pfunc);
int16_t getCyl();
int16_t getHead();
void setCylHead(int16_t cyl, int16_t head);
int16_t getCellWidth();