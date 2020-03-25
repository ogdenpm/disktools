// This is an open source non-commercial project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include "flux2imd.h"
#include "flux.h"
#include "util.h"

#define HIST_MAX_US    10          // max uS for histogram
#define HIST_SLOTS_PER_US    8   // slots per uS
#define HIST_SLOTS   (HIST_MAX_US * HIST_SLOTS_PER_US)

void displayHist(int levels)
{
    uint32_t histogram[HIST_MAX_US * HIST_SLOTS_PER_US + 1] = { 0 };
    uint32_t maxHistCnt = 0;
    int maxHistVal = 0;
    uint32_t outRange = 0;
    int val;

    // load the histogram data
    for (int i = 0; seekBlock(i) >= 0; i++) {
        while ((val = getNextFlux()) >= 0) {
            if (val > maxHistVal)
                maxHistVal = val;
            val = (val * HIST_SLOTS_PER_US + 500) / 1000;       // scale to slots with rounding
            if (val > HIST_SLOTS)
                outRange++;
            else if (++histogram[val] > maxHistCnt)
                maxHistCnt++;
        }
    }

    if (maxHistCnt == 0) {
        logFull(ALWAYS, "No histogram data\n");
        return;
    }

    int cols[HIST_MAX_US * HIST_SLOTS_PER_US + 1];
    char line[HIST_MAX_US * HIST_SLOTS_PER_US + 2];        // allow for 0 at end

    // scale the counts to peak at 2 * levels (i.e. 2 values per level)
    for (int i = 0; i <= HIST_SLOTS; i++)
        cols[i] = (2 * levels * histogram[i] + maxHistCnt / 2) / maxHistCnt;

    // narrow down the graph to only live data showing at least 0.5uS to 5.5uS
    int lowVal, highVal;
    for (lowVal = 0; lowVal < HIST_SLOTS_PER_US / 2 && cols[lowVal] == 0; lowVal++)
        ;
    for (highVal = HIST_SLOTS; highVal >= (int32_t)(HIST_SLOTS_PER_US * 5.5) && cols[highVal] == 0; highVal--)
        ;

    logBasic("\nHistogram\n"
        "---------\n");
    logBasic("max flux value = %.1fuS, flux values > %duS = %d\n", maxHistVal / 1000.0, HIST_MAX_US, outRange);
    logBasic("x axis range = %.1fus - %.1fus, y axis range 0 - %d\n\n",
        (double)(lowVal) / HIST_SLOTS_PER_US, (double)(highVal) / HIST_SLOTS_PER_US, maxHistCnt);
    int fillch;
    for (int i = levels * 2; i > 0; i -= 2) {
        line[highVal + 1] = fillch = 0;
        for (int j = highVal; j >= lowVal; j--) {
            if (cols[j] >= i - 1) {
                line[j] = cols[j] >= i ? '#' : '+';
                fillch = ' ';
            }
            else
                line[j] = fillch;
        }
        logBasic("%s\n", line + lowVal);
    }
    for (int i = lowVal; i <= highVal; i++)
        logBasic("-");
    logBasic("\n");
    for (int i = lowVal; i <= highVal; i++)
        if (i % HIST_SLOTS_PER_US == 0)
            i += logBasic("%dus", i / HIST_SLOTS_PER_US) - 1;
        else
            logBasic(" ");
    logBasic("\n");
}