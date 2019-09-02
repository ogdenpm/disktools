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
#include "sectorManager.h"
#include "util.h"

#define RECOVERED    0x80        // flag for recovered sector id

int cylMax = 0, headMax = 0;



void WriteIMDHdr(FILE* fp, char* fname) {
    struct tm* dateTime;
    time_t curTime;

    time(&curTime);
    dateTime = localtime(&curTime);
    fprintf(fp, "IMD 1.18 %02d/%02d/%04d %02d:%02d:%02d\r\n", dateTime->tm_mday, dateTime->tm_mon + 1, dateTime->tm_year + 1900,
    dateTime->tm_hour, dateTime->tm_min, dateTime->tm_sec);
    fprintf(fp, "Created from %s by flux2imd\r\n\x1a", fileName(fname));
}

bool SameCh(uint8_t *pData, int size) {      // valid sectors have the SUSPECT tag cleared so simple compare

    for (int i = 1; i < size; i++)
        if (pData[0] != pData[i])
            return false;
    return true;
}


uint8_t *sectorToBytes(track_t* trackPtr, uint8_t slot) {
    static uint8_t sectorBytes[1024];
    uint16_t* sectorRawData = trackPtr->sectors[slot].sectorDataList->sectorData.rawData;

    for (int i = 0; i < 128 << trackPtr->fmt->sSize; i++)
        sectorBytes[i] = (uint8_t)sectorRawData[i];
    return sectorBytes;
}



// E_FM5, E_FM8, E_FM8H, E_MFM5, E_MFM8, E_M2FM8
static uint8_t imdModes[] = { 2, 0, 0, 5, 3, 3 };

void writeImdFile(char *fname) {
    FILE *fp;
    track_t *trackPtr;
    char imdFile[_MAX_PATH + 1];
    strcpy(imdFile, fname);
    strcpy(strrchr(imdFile, '.'), ".imd");

    if ((fp = fopen(imdFile, "wb")) == NULL) {
        logFull(ERROR, "cannot create %s\n", fname);
        return;
    }
    logFull(ALWAYS, "IMD file %s created\n", fileName(imdFile));

    WriteIMDHdr(fp, fname);
    for (int cyl = 0; cyl < MAXTRACK; cyl++)
        for (int head = 0; head < 2; head++) {
            trackPtr = getTrack(cyl, head);
            if (!trackPtr || trackPtr->status & TS_BADID)
                continue;
            if (trackPtr->cylinder != cyl || trackPtr->side != head) {
                logFull(ERROR, "Non-standard cylinder/side mapping not implemented, skipping track %d/%d\n", cyl, head);
                continue;
            }

            putc(imdModes[trackPtr->fmt->encoding], fp);           // mode
            putc(cyl, fp);                      // cylinder
            putc(head, fp);                     // head
            putc(trackPtr->fmt->spt, fp);       // sectors in track
            putc(trackPtr->fmt->sSize, fp);     // sector size
            fwrite(trackPtr->slotToSector, 1, trackPtr->fmt->spt, fp);    // sector numbering map


            for (int slot = 0; slot < trackPtr->fmt->spt; slot++) {
                if (trackPtr->sectors[slot].status & SS_DATAGOOD) {
                    uint8_t *pSec = sectorToBytes(trackPtr, slot);
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