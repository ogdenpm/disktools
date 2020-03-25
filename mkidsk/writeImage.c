/* writeImage.c     (c) by Mark Ogden 2018

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

    if (strncmp(comment, "IMD ", 4) != 0) {		// if comment does not have IMD header put one in
        time(&curTime);
        dateTime = localtime(&curTime);
        fprintf(fp, "IMD 1.18 %02d/%02d/%04d %02d:%02d:%02d\r\n", dateTime->tm_mday, dateTime->tm_mon + 1, dateTime->tm_year + 1900,
            dateTime->tm_hour, dateTime->tm_min, dateTime->tm_sec);
    }
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
    
    int bias = diskType == ISIS_SD || diskType == ISIS_DD ? 0 : -1;
    int skew = useSkew ? formats[diskType].skew : 0;
    char *modeSize = formats[diskType].modeSize;
    int interleave = 1;
    int mode = 0;
    int sSize = 0;

    if (interleaves == NULL) {
        bias = -1;
        skew = 0;
        interleaves = "1";
    } else if (!*interleaves)
        interleaves = formats[diskType].tInterLeave;

 //   BuildSMap(interleaves);
    fmtExt = strrchr(fname, '.');
    fmt = _stricmp(fmtExt, ".img") == 0 ? IMG : IMD;

    if (fmt == IMD)
        WriteIMDHdr(fp, comment);

    for (int cyl = 0; cyl < formats[diskType].nCyl; cyl++) {
        for (int head = 0; head < formats[diskType].nHead; head++) {
            int spt = formats[diskType].nSector;

            if (*modeSize) {
                mode = *modeSize++ - '0';
                sSize = *modeSize++ - '0';
            }
            if (*interleaves) {
                interleave = *interleaves++ - '0';
                if (bias < 0)
                    bias = -interleave;
            }  else
                bias += skew;

            byte smap[MAXSECTOR];
            BuildSMap(smap, spt, interleave, bias);

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