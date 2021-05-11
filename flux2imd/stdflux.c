#include <stdbool.h>
#include <stdlib.h>
#include "util.h"
#include "stdflux.h"
#include <limits.h>
#include <stdint.h>

Flux *stdFlux;

#ifdef _DEBUG
uint32_t newSampleCnt;
uint16_t newIndexCnt;
uint32_t actSampleCnt;
uint16_t actIndexCnt;
#endif

static uint32_t tsToPos(int32_t ts);

void beginFlux(uint32_t sampleCnt, int indexCnt, double sclk, double rpm, int16_t hsCnt) {  // indexCnt is the count of index holes
    if (stdFlux) {
        free(stdFlux->ts);
        free(stdFlux);
        stdFlux = NULL;
    }
    if (sampleCnt + indexCnt == 0)
        return;
    stdFlux = xmalloc(sizeof(Flux) + indexCnt * sizeof(Index));     // include space for index info
    stdFlux->indexLen = indexCnt + 2;                               // allow SODATA as first
    stdFlux->tsLen = sampleCnt + 1;                                 // allow start ts of INT32_MIN
    stdFlux->ts = xmalloc(sizeof(int32_t) * (stdFlux->tsLen + 1));    // one extra for INT32_MAX at end
    stdFlux->ts[0] = INT32_MIN;                                     // start sentinal
    stdFlux->tsPos = 1;
    stdFlux->indexPos = 1;                                          // initial sentinal incase we are not index hole aligned
    stdFlux->index[0].itype = SODATA;
    stdFlux->index[0].pos = 1;
    stdFlux->index[0].ts = INT32_MIN;

    stdFlux->cyl = stdFlux->head = -1;
    stdFlux->hsCnt = hsCnt;
    stdFlux->timeNs = 0.0;
    stdFlux->onIndex = NULL;
    stdFlux->sclk = sclk;
    stdFlux->scaler = 1.0E9 / sclk;
    stdFlux->rpm = rpm;
    stdFlux->nextIndexTs = 1;
    stdFlux->indexHandled = false;

#ifdef _DEBUG
    newSampleCnt = sampleCnt;
    newIndexCnt = indexCnt;
    actSampleCnt = actIndexCnt = 0;
#endif

}

void setCylHead(int16_t cyl, int16_t head) {
    stdFlux->cyl = cyl;
    stdFlux->head = head;
}

void setActualRPM(double rpm) {
    stdFlux->scaler = 1.0E9 / stdFlux->sclk * stdFlux->rpm / rpm;
}


void addDelta(uint32_t delta) {
#ifdef _DEBUG
    actSampleCnt++;
#endif
    stdFlux->timeNs += delta * stdFlux->scaler;
    if (stdFlux->tsPos < stdFlux->tsLen)
        stdFlux->ts[stdFlux->tsPos++] = (int32_t)(stdFlux->timeNs + 0.5);
    else
        logFull(D_ERROR, "addTs out of bounds\n");
};


void addIndex(int16_t itype, uint32_t delta) {
#ifdef _DEBUG
    actIndexCnt++;
#endif
    if (stdFlux->indexPos == 1 && stdFlux->tsPos == 1 && itype < 1)                  // if start of track or sector 0 before data no need for SODATA index
        stdFlux->indexPos = 0;

    if (stdFlux->indexPos < stdFlux->indexLen - 1 && stdFlux->tsPos < stdFlux->tsLen - 1) {
        stdFlux->index[stdFlux->indexPos].itype = itype;
        if (stdFlux->tsPos > 1 || delta == 0)
            stdFlux->index[stdFlux->indexPos].ts = (int32_t)(stdFlux->timeNs + delta * stdFlux->scaler + 0.5);
        else
            stdFlux->index[stdFlux->indexPos].ts = -1;

        if (itype < 1 && stdFlux->index[0].ts == INT32_MIN) {                       // fix up an data prior to start of first full track
            stdFlux->index[0].ts = (int32_t)(stdFlux->index[stdFlux->indexPos].ts - 60.0 / stdFlux->rpm * stdFlux->scaler);     // back  up a disk revolution
            if (stdFlux->index[0].ts >= 0)       // if >= 0 then we would have seen the SSSTART index
                stdFlux->index[0].ts = -1;       // adjust to say we just missed it
        }
        stdFlux->index[stdFlux->indexPos++].pos = stdFlux->tsPos;
    } else
        logFull(D_ERROR, "addIndex: too many indexes\n");

}


void endFlux() {
    stdFlux->index[stdFlux->indexPos].itype = EODATA;
    stdFlux->tsLen = stdFlux->tsPos;        // correct any variance for SCP files with 0 counts merged.
    stdFlux->index[stdFlux->indexPos].ts = stdFlux->ts[stdFlux->tsLen] = INT32_MAX;
}


int seekIndex(uint16_t index) {         // sets current position to first sample after index, returns type, or EODATA if out of range
    if (index > stdFlux->indexPos)
        index = stdFlux->indexPos;
    stdFlux->tsPos = tsToPos(stdFlux->index[index].ts);
    stdFlux->nextIndex = index < stdFlux->indexPos ? index + 1 : index;
    stdFlux->nextIndexTs = stdFlux->index[stdFlux->nextIndex].ts;
    stdFlux->indexHandled = false;
    return stdFlux->index[index].itype;
}

// get the index of the given ts in the data stream
// retuns the position of the first sample >= ts
static uint32_t tsToPos(int32_t ts) {
    if (ts <= stdFlux->ts[1])
        return 1;
    if (ts > stdFlux->ts[stdFlux->tsLen - 1])
        return stdFlux->tsLen;
    // find the approx position and the adjust if necessary
    uint32_t pos = (uint32_t)((ts / stdFlux->timeNs) * (stdFlux->tsLen - 2)) + 1;
    while (stdFlux->ts[pos + 1] < ts)
            pos++;
    while (stdFlux->ts[pos - 1] >= ts)
        pos--;
    return pos;
}

int16_t getType(uint16_t index) {
    return index < stdFlux->indexPos ? stdFlux->index[index].itype : EODATA;
}


int32_t getTs() {
    while (stdFlux->tsPos < stdFlux->tsLen) {
        int32_t ts = stdFlux->ts[stdFlux->tsPos];

        if (ts < stdFlux->nextIndexTs || stdFlux->indexHandled) {       // note EODATA will never trigger as it has ts INT32_MAX
            stdFlux->indexHandled = false;
            stdFlux->tsPos++;
            return ts;
        }

        int16_t itype = stdFlux->index[stdFlux->nextIndex].itype;
        stdFlux->nextIndexTs = stdFlux->index[++stdFlux->nextIndex].ts;
        stdFlux->indexHandled = ts < stdFlux->nextIndexTs;

        if (!stdFlux->onIndex || !stdFlux->onIndex(itype))
            return itype < 0 ? itype : HSSTART - itype;
    }
    return EODATA;
}


int32_t peekTs() {
    return stdFlux->tsPos < stdFlux->tsLen ? stdFlux->ts[stdFlux->tsPos] : INT32_MAX;
}

uint16_t getHsCnt() {
    return stdFlux->hsCnt;
}

OnIndex setOnIndex(OnIndex pfunc) {
    OnIndex oldFunc = stdFlux->onIndex;
    stdFlux->onIndex = pfunc;
    return oldFunc;
}

double getRPM() {
    return stdFlux->rpm;
}

int16_t getCyl() {
    return stdFlux->cyl;
}
int16_t getHead() {
    return stdFlux->head;
}