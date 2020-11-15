/****************************************************************************
 *  program: mkidsk - create IMD or IMG file from a recipe file             *
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

/*
DESCRIPTION
    part of mkidsk
    Writes the disk image format to a file, applying interlave as requested
    Portions of the code are based on Dave Duffield's imageDisk sources

MODIFICATION HISTORY
    17 Aug 2018 -- original release as mkidsk onto github
    18 Aug 2018 -- added copyright info
    14 Oct 2019 -- revised to support ISIS II & ISIS III disks
*/
#include "mkIsisDisk.h"
#include <time.h>
#include <stdint.h>

enum {
    IMG, IMD
};

int BuildSMap(byte *smap, int nSector, int interleave, int bias)
{
    int slot = bias;
    memset(smap, 0, nSector * sizeof(byte));
    for (int i = 1; i <= nSector; i++) {
        slot = (slot + interleave) % nSector;
        while (smap[slot])
            slot = ++slot % nSector;
        smap[slot] = i;

    }
    return slot;        // return last slot assigned.
}



void WriteIMDHdr(FILE *fp, char *comment) {
    struct tm *dateTime;
    time_t curTime;

    time(&curTime);
    dateTime = localtime(&curTime);
    fprintf(fp, "IMD 1.18 %02d/%02d/%04d %02d:%02d:%02d\r\n", dateTime->tm_mday, dateTime->tm_mon + 1, dateTime->tm_year + 1900,
        dateTime->tm_hour, dateTime->tm_min, dateTime->tm_sec);

    while (*comment) {
        if (*comment == '\n')
            putc('\r', fp);
        putc(*comment++, fp);
    }
    putc(0x1A, fp);
}

bool SameCh(byte *sec, int len) {
    for (int i = 1; i < len; i++)
        if (sec[0] != sec[i])
            return false;
    return true;
}


// interleaves points to disk interleave format as per standard ISIS.LAB format i.e. '0' biased
// if interleaves == "" then use default
// if inteleaves == NULL then no interleave

void WriteImgFile(char *fname, int diskType, char *interleaves, bool useSkew, char *comment) {
    FILE *fp;
    char *fmtExt;
    int fmt;

    if ((fp = fopen(fname, "wb")) == NULL) {
        fprintf(stderr, "cannot create %s\n", fname);
        exit(1);
    }
    
    int skew = interleaves && useSkew ? formats[diskType].skew : -1;
    char *modeSize = formats[diskType].modeSize;
    int interleave = 1;
    int mode = 0;
    int sSize = 0;

    if (!interleaves) {
        interleaves = "1";
    } else if (!*interleaves)
        interleaves = formats[diskType].tInterLeave;

 //   BuildSMap(interleaves);
    fmtExt = strrchr(fname, '.');
    fmt = _stricmp(fmtExt, ".img") == 0 ? IMG : IMD;

    if (fmt == IMD)
        WriteIMDHdr(fp, comment);

    int lastSlot = -skew;
    int bias;
    for (int cyl = 0; cyl < formats[diskType].nCyl; cyl++) {
        for (int head = 0; head < formats[diskType].nHead; head++) {
            int spt = formats[diskType].nSector;

            if (*modeSize) {
                mode = *modeSize++ - '0';
                sSize = *modeSize++ - '0';
            }
            if (*interleaves) {
                interleave = *interleaves++ - '0';
                bias = Format(diskType) == ISISP ? -interleave : 0;
            } else if (skew < 0)
                bias = Format(diskType) == ISISP ? -interleave : 0;
            else
                bias = lastSlot + skew;

            byte smap[MAXSECTOR];
            lastSlot = BuildSMap(smap, spt, interleave, bias);

            if (fmt == IMD) {
                putc(mode, fp);        // mode
                putc(cyl, fp);    // cylinder
                putc(head, fp);        // head
                putc(spt, fp);      // sectors in track
                putc(sSize, fp);        // sector size
                fwrite(smap, 1, spt, fp);    // sector numbering map
            }
            for (int secNum = 0; secNum < spt; secNum++) {
                byte *sector = GetSectorLoc(BLOCK(cyl, smap[secNum] + head * spt));
                if (fmt == IMD) {
                    if (SameCh(sector, 128 << sSize)) {
                        putc(2, fp);
                        putc(*sector, fp);
                    }
                    else {
                        putc(1, fp);
                        fwrite(sector, 1, 128 << sSize, fp);
                    }
                }
                else
                    fwrite(sector, 1, 128 << sSize, fp);
            }
        }
    }

    fclose(fp);
}