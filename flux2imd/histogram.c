/****************************************************************************
 *  program: flux2imd - create imd image file from kryoflux file            *
 *  Copyright (C) 2020 Mark Ogden <mark.pm.ogden@btinternet.com>            *
 *                                                                          *
 *  This program is free software; you can redistribute it and/or           *
 *  modify it under the terms of the GNU General Public License             *
 *  as published by the Free Software Foundation; either version 2          *
 *  of the License, or (at your option) any later version.                  *
 *                                                                          *
 *  This program is distributed in the hope that it will be useful,         *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU General Public License for more details.                            *
 *                                                                          *
 *  You should have received a copy of the GNU General Public License       *
 *  along with this program; if not, write to the Free Software             *
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,              *
 *  MA  02110-1301, USA.                                                    *
 *                                                                          *
 ****************************************************************************/


// This is an open source non-commercial project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include "flux2imd.h"
#include "flux.h"
#include "util.h"
#include "stdflux.h"

#define HIST_MAX_US    10        // max uS for histogram
#define HIST_SLOTS_PER_US    8   // slots per uS
#define HIST_SLOTS   (HIST_MAX_US * HIST_SLOTS_PER_US)

void displayHist(int levels)
{
    uint32_t histogram[HIST_MAX_US * HIST_SLOTS_PER_US + 1] = { 0 };
    uint32_t maxHistCnt = 0;
    int maxHistVal = 0;
    uint32_t outRange = 0;
    int32_t val;
    int32_t prevTs = 0;
    int32_t newTs;

    seekIndex(0);               // to start of data
    int scnt = 0;
    int icnt = 0;
    while ((newTs = getTs()) != EODATA) {
        if (newTs >= 0) {        // ignore index markers
            scnt++;
            val = newTs - prevTs;
            prevTs = newTs;
            if (val > maxHistVal)
                maxHistVal = val;
            val = (val * HIST_SLOTS_PER_US + 500) / 1000; // scale to slots with rounding
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

    logFull(ALWAYS, "");
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
    logBasic("\n\n");
}