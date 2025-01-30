#include <stdbool.h>
#include <stdlib.h>
#include "util.h"
#include "stdflux.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

/*
* flux data is stored as the time in ns since the start of the data stream. These times are adjusted to
* account for rotational variances as determined by the track rotation time
* within the samples -ve numbers are used to represnt index hole events or end of data
* the absolute values are used  to index into the index table to get further information
*/
#define PULSECNTS   20
static int32_t *sfTs;            // where the samples are saved
static uint32_t sfTsLen;         // length of the allocated array
static double sfSclk;            // sample period in ns
static double sfRpm;             // rotational speed (300.0 or 360.0) revolutions per minute
static double sfScaler;          // scaler used to convert cnts to ns with adjustments for rotational variation
static double sfBaseNs;         // position in ns of last change of RPM actual
static int32_t sfBaseDelta;     // cummulative delta since last change of RPM actual           
static uint32_t sfTsPos;         // index of next sample to use
static OnIndex sfOnIndex;        // function called when start of track is seen - true if full handled
static int16_t sfCyl;            // expected cylinder from file name or internal file data (-1) if not known
static int16_t sfHead;           // ditto for head (-1) if not known
static int16_t sfHsCnt;          // hard sector count - zero for softsector
static int16_t sfIndexPos;       // next index slot to use
static int16_t sfIndexLen;       // number of index entries (includes extra for SODATA)
static int32_t sfNextIndexTs;    // next ts at which an index occurs
static int16_t sfNextIndex;      // corresponding index id
static bool sfIndexHandled;      // true if last getTs index processing is done
static Index *sfIndex;           // always includes 1 for SODATA entry
static uint32_t sfPulseCnt[PULSECNTS];  // used to track pulse counts in 0.5us slots
static int32_t sfCellWidth;      // best guess at cell width in us

#ifdef _DEBUG
uint32_t newSampleCnt;
uint16_t newIndexCnt;
uint32_t actSampleCnt;
uint16_t actIndexCnt;
#endif

static uint32_t tsToPos(int32_t ts);

void beginFlux(uint32_t sampleCnt, int indexCnt, double sclk, double rpm, int16_t hsCnt) {  // indexCnt is the count of index holes
    free(sfTs);
    free(sfIndex);
    sfTs = NULL;
    sfIndex = NULL;
   
    if (sampleCnt + indexCnt == 0)
        return;
    sfIndexLen = indexCnt + 2;                               // allow SODATA as first + EODATA
    sfTsLen = sampleCnt + 1;                                 // allow start ts of INT32_MIN
    sfTs = xmalloc(sizeof(int32_t) * (sfTsLen + 1));         // one extra for INT32_MAX at end
    sfTs[0] = INT32_MIN;                                     // start sentinal
    sfTsPos = 1;
    sfIndex = xmalloc(sizeof(Index) * sfIndexLen);
    sfIndexPos = 1;                                          // initial sentinal incase we are not index hole aligned
    sfIndex[0].itype = SODATA;
    sfIndex[0].pos = 1;
    sfIndex[0].ts = INT32_MIN;

    sfCyl = sfHead = -1;
    sfHsCnt = hsCnt;
    sfBaseNs = 0.0;
    sfBaseDelta = 0;
    sfOnIndex = NULL;
    sfSclk = sclk;
    sfScaler = 1.0E9 / sclk;
    sfRpm = rpm;
    sfNextIndexTs = 1;
    sfIndexHandled = false;
    memset(sfPulseCnt, 0, sizeof(sfPulseCnt));
    sfCellWidth = 2000;                                     // assume 2us

#ifdef _DEBUG
    newSampleCnt = sampleCnt;
    newIndexCnt = indexCnt;
    actSampleCnt = actIndexCnt = 0;
#endif

}

void setCylHead(int16_t cyl, int16_t head) {
    sfCyl = cyl;
    sfHead = head;
}

void setActualRPM(double rpm) {
    sfBaseNs += (sfBaseDelta * sfScaler);
    sfBaseDelta = 0;
    sfScaler = 1.0E9 / sfSclk * sfRpm / rpm;
}


void addDelta(uint32_t delta) {
#ifdef _DEBUG
    actSampleCnt++;
#endif
    sfBaseDelta += delta;
    if (sfTsPos < sfTsLen) {
        sfTs[sfTsPos] = (int32_t)(sfBaseNs + sfBaseDelta * sfScaler);
        if (sfTsPos > 1) {
            int32_t halfusDelta = (sfTs[sfTsPos] - sfTs[sfTsPos - 1] + 250) / 500;
            if (halfusDelta < PULSECNTS)
                sfPulseCnt[halfusDelta]++;
        }
        sfTsPos++;
    } else
        logFull(D_ERROR, "addTs out of bounds\n");
};


void addIndex(int16_t itype, uint32_t delta) {
#ifdef _DEBUG
    actIndexCnt++;
#endif
    if (sfIndexPos == 1 && sfTsPos == 1 && itype < 1)                  // if start of track or sector 0 before data no need for SODATA index
        sfIndexPos = 0;

    if (sfIndexPos < sfIndexLen - 1) {
        sfIndex[sfIndexPos].itype = itype;
        if (sfTsPos > 1 || delta == 0)
            sfIndex[sfIndexPos].ts = (int32_t)(sfBaseNs + (sfBaseDelta + delta) * sfScaler);
        else
            sfIndex[sfIndexPos].ts = -1;

        if (itype < 1 && sfIndex[0].ts == INT32_MIN) {                       // fix up an data prior to start of first full track
            sfIndex[0].ts = (int32_t)(sfIndex[sfIndexPos].ts - 60.0 / sfRpm * sfScaler);     // back  up a disk revolution
            if (sfIndex[0].ts >= 0)       // if >= 0 then we would have seen the SSSTART index
                sfIndex[0].ts = -1;       // adjust to say we just missed it
        }
        sfIndex[sfIndexPos++].pos = sfTsPos;
    } else
        logFull(D_ERROR, "addIndex: too many indexes\n");

}


void endFlux() {
    sfIndex[sfIndexPos].itype = EODATA;
    sfTsLen = sfTsPos;        // correct any variance for SCP files with 0 counts merged.
    sfIndex[sfIndexPos].ts = sfTs[sfTsLen] = INT32_MAX;

    
    // work out the most likely cell width by looking for the largest value of
    // count of pulse width + pulse width * 2
    uint32_t largestCnt = 0;
    for (int i = 1; i < PULSECNTS / 2; i++)
        if (sfPulseCnt[i] + sfPulseCnt[i * 2] > largestCnt) {
            largestCnt = sfPulseCnt[i] + sfPulseCnt[i * 2];
            sfCellWidth = i * 500;
        }
}


int seekIndex(uint16_t index) {         // sets current position to first sample after index, returns type, or EODATA if out of range
    if (index > sfIndexPos)
        index = sfIndexPos;
    sfTsPos = tsToPos(sfIndex[index].ts);
    sfNextIndex = index < sfIndexPos ? index + 1 : index;
    sfNextIndexTs = sfIndex[sfNextIndex].ts;
    sfIndexHandled = false;
    return sfIndex[index].itype;
}

// get the index of the given ts in the data stream
// retuns the position of the first sample >= ts
static uint32_t tsToPos(int32_t ts) {
    // binary search for the entry
    uint32_t high = sfTsLen;
    uint32_t low = 0;
    uint32_t mid = (low + high) / 2;
    while (sfTs[mid] != ts && low <= high) {
        if (sfTs[mid] < ts)
            low = mid + 1;
        else
            high = mid - 1;
        mid = (low + high) / 2;
    }
    return sfTs[mid] < ts ? mid + 1 : mid;
}

int16_t getType(uint16_t index) {
    return index < sfIndexPos ? sfIndex[index].itype : EODATA;
}



int32_t getTs() {
    while (sfTsPos < sfTsLen) {
        int32_t ts = sfTs[sfTsPos];

        if (ts < sfNextIndexTs || sfIndexHandled) {       // note EODATA will never trigger as it has ts INT32_MAX
            sfIndexHandled = false;
            sfTsPos++;
            return ts;
        }

        int16_t itype = sfIndex[sfNextIndex].itype;
        sfNextIndexTs = sfIndex[++sfNextIndex].ts;
        sfIndexHandled = ts < sfNextIndexTs;

        if (!sfOnIndex || !sfOnIndex(itype))
            return itype < 0 ? itype : HSSTART - itype;
    }
    return EODATA;
}


int32_t peekTs() {
    return sfTsPos < sfTsLen ? sfTs[sfTsPos] : INT32_MAX;
}

uint16_t getHsCnt() {
    return sfHsCnt;
}

OnIndex setOnIndex(OnIndex pfunc) {
    OnIndex oldFunc = sfOnIndex;
    sfOnIndex = pfunc;
    return oldFunc;
}

double getRPM() {
    return sfRpm;
}

int16_t getCyl() {
    return sfCyl;
}
int16_t getHead() {
    return sfHead;
}

int16_t getCellWidth() {
 //   printf("Cell Width %d\n", sfCellWidth);
    return sfCellWidth;
}
