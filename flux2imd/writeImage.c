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

/* writeImage.c     (c) by Mark Ogden 2019

DESCRIPTION
    part of mkidsk
    Writes the disk image format to a file, applying interlave as requested
    Portions of the code are based on Dave Duffield's imageDisk sources

MODIFICATION HISTORY
    17 Aug 2018 -- original release as mkidsk onto github
    18 Aug 2018 -- added copyright info

*/
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "flux2imd.h"
#include "trackManager.h"
#include "util.h"

static bool SameCh(uint8_t *pData, int size) {      // valid sectors have the SUSPECT tag cleared so simple compare

    for (int i = 1; i < size; i++)
        if (pData[0] != pData[i])
            return false;
    return true;
}

static uint8_t *sectorToUint8(track_t* trackPtr, uint8_t slot) {
    static uint8_t sectorBytes[1024];
    uint16_t* sectorRawData = trackPtr->sectors[slot].sectorDataList->sectorData.rawData;

    for (int i = 0; i < 128 << trackPtr->fmt->sSize; i++)
        sectorBytes[i] = (uint8_t)sectorRawData[i];
    return sectorBytes;
}

static void WriteIMDHdr(FILE* fp, char* fname) {
    struct tm* dateTime;
    time_t curTime;

    time(&curTime);
    dateTime = localtime(&curTime);
    fprintf(fp, "IMD 1.18 %02d/%02d/%04d %02d:%02d:%02d\r\n", dateTime->tm_mday, dateTime->tm_mon + 1, dateTime->tm_year + 1900,
    dateTime->tm_hour, dateTime->tm_min, dateTime->tm_sec);
    fprintf(fp, "Created from %s by flux2imd\r\n\x1a", fileName(fname));
}

// E_FM5, E_FM5H, E_FM8, E_FM8H, E_MFM5, E_MFM5H, E_MFM8, E_MFM8H, E_M2FM8
static uint8_t imdModes[] = { 2, 2, 0, 0, 5, 5, 3, 3, 3 };

void writeImdFile(char *fname) {
    FILE *fp;
    track_t *trackPtr;
    char imdFile[_MAX_PATH + 1];


    if (maxCylinder < 0)
        return;

    strcpy(imdFile, fname);
    strcpy(strrchr(imdFile, '.'), ".imd");

    if ((fp = fopen(imdFile, "wb")) == NULL) {
        logFull(D_ERROR, "cannot create %s\n", fname);
        return;
    }
    logFull(ALWAYS, "IMD file %s created\n", fileName(imdFile));

    WriteIMDHdr(fp, fname);
    for (int cyl = 0; cyl <= maxCylinder; cyl++)
        for (int head = 0; head <= maxHead; head++) {
            trackPtr = getTrack(cyl, head);
            if (!trackPtr || trackPtr->status & TS_BADID || !hasTrack(cyl, head))
                continue;
            putc(imdModes[trackPtr->fmt->encoding], fp);           // mode
            putc(cyl, fp);                      // cylinder
            putc(head | ((trackPtr->status & TS_CYL) ? 0x80 : 0) | ((trackPtr->status & TS_SIDE) ? 0x40 : 0), fp);                     // head
            putc(trackPtr->fmt->spt, fp);       // sectors in track
            putc(trackPtr->fmt->sSize, fp);     // sector size
            fwrite(trackPtr->slotToSector, 1, trackPtr->fmt->spt, fp);    // sector numbering map
            if (trackPtr->status & TS_CYL) {
                logFull(D_WARNING, "cylinder map needed for track %02d/%d\n", trackPtr->cylinder, trackPtr->side);
                for (int i = 0; i < trackPtr->fmt->spt; i++)
                    putc(trackPtr->sectors[i].idam.cylinder, fp);
            }
            if (trackPtr->status & TS_SIDE) {
                logFull(D_WARNING, "head map needed for track %02d/%d\n", trackPtr->cylinder, trackPtr->side);
                for (int i = 0; i < trackPtr->fmt->spt; i++)
                    putc(trackPtr->sectors[i].idam.side, fp);
            }

            for (int slot = 0; slot < trackPtr->fmt->spt; slot++) {
                if (trackPtr->sectors[slot].status & SS_DATAGOOD) {
                    uint8_t *pSec = sectorToUint8(trackPtr, slot);
                    if (SameCh(pSec, 128 << trackPtr->fmt->sSize)) {
                        putc(2, fp);
                        putc(pSec[0], fp);
                    } else {
                        putc(1, fp);
                        fwrite(pSec, 1, 128 << trackPtr->fmt->sSize, fp);
                    }
                } else
                    putc(0, fp);            // data not available
                

            }
        }

    fclose(fp);
}