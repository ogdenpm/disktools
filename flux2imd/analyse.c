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


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include "dpll.h"
#include "flux.h"
#include "flux2imd.h"
#include "formats.h"
#include "trackManager.h"
#include "util.h"
#include "stdflux.h"


unsigned flip32(unsigned n) {
    unsigned result = 0;
    for (int i = 0; i < 32; i += 8) {
        result += flip[(n >> i) & 0xff] << i;
    }
    return result;
}

void analyse(char const *opt) {
    char afmt[16] = "\x80";         // prepend a 0x80 to avoid conflict with normal formats
    strncat(afmt, opt, 14);
    setFormat(afmt);

    int limit = getHsCnt() ? getHsCnt() : 1;
    int i;
    int bit;
    unsigned bitCnt = 0;
    unsigned mask;
    unsigned pattern = 0;;
    unsigned data[2] = { 0, 0 };
    unsigned suspect[2] = { 0, 0 };
    unsigned flipped;
    
    switch(curFormat->encoding) {   
    case E_MFM5: case E_MFM8: case E_MFM5H: mask = 5; break;
    case E_M2FM8: mask = 0xd; break;
    default: mask = 0; break;
    }
    logBasic("bitPos   bitCnt  Pattern  Clk  Normal  Inverted  Flipped  InvFlip  |  bitPos   bitCnt  Pattern  Clk  Normal  Inverted  Flipped  InvFlip");

    for (i = 1; i < limit + 1 && seekIndex(i) != EODATA; i++) {
            retrain(0);
            while ((bit = getBit()) >= 0) {
                bitCnt++;
                pattern = pattern * 2 + bit;
                data[bitCnt & 1] = data[bitCnt & 1] * 2 + bit;
                suspect[bitCnt & 1] = suspect[bitCnt & 1] * 2 + (((pattern & 2) == 2) ^ ((pattern & mask) == 0));
                flipped = flip32(data[bitCnt & 1]);
                logBasic("%s%6d %5d.%d: %08X  %02X%s %08X %08X %08X %08X", (bitCnt & 1) ? "\n" : "  |  ", getBitCnt(0), bitCnt/16, (bitCnt % 16 / 2), pattern, 
                    data[(bitCnt - 1) & 1] & 0xff, (suspect[bitCnt & 1] & 0xff) ? "*" : " ", data[bitCnt & 1], ~data[bitCnt & 1] & 0xffffffff,
                    flipped, ~flipped & 0xffffffff);
            }
    }
    logBasic("\n");
    if (i != limit)
        logFull(D_ERROR, "incomplete track\n");
}