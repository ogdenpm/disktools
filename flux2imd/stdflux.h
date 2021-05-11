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


typedef bool (*OnIndex)(uint16_t index);
/*
* flux data is stored as the time in ns since the start of the data stream. These times are adjusted to
* account for rotational variances as determined by the track rotation time
* within the samples -ve numbers are used to represnt index hole events or end of data
* the absolute values are used  to index into the index table to get further information
*/
typedef struct {
    int32_t *ts;            // where the samples are saved
    uint32_t tsLen;         // length of the allocated array
    double sclk;            // sample period in ns
    double rpm;             // rotational speed (300.0 or 360.0) revolutions per minute
    double scaler;          // scaler used to convert cnts to ns with adjustments for rotational variation
    double timeNs;          // current position in ns
    uint32_t tsPos;         // index of next sample to use
    OnIndex onIndex;        // function called when start of track is seen - true if full handled
    int16_t cyl;            // expected cylinder from file name or internal file data (-1) if not known
    int16_t head;           // ditto for head (-1) if not known
    int16_t hsCnt;          // hard sector count - zero for softsector
    int16_t indexPos;       // next index slot to use
    int16_t indexLen;       // number of index entries (includes extra for SODATA)
    int32_t nextIndexTs;    // next ts at which an index occurs
    int16_t nextIndex;      // corresponding index id
    bool indexHandled;      // true if last getTs index processing is done
    Index index[1];         // always includes 1 for SODATA entry
} Flux;


extern Flux *stdFlux;


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