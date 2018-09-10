/* writeImage.c     (c) by Mark Ogden 2018

DESCRIPTION
    part of mkidsk
    Writes the disk image format to a file, applying interlave as requested
    Portions of the code are based on Dave Duffield's imageDisk sources

MODIFICATION HISTORY
    17 Aug 2018 -- original release as mkidsk onto github
    18 Aug 2018 -- added copyright info

*/
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include "flux.h"
enum {
    IMG, IMD
};

imd_t trackData[MAXCYLINDER][MAXHEAD];  // support 2 heads

#define RECOVERED    0x80        // flag for recovered sector id

int cylMax = 0, headMax = 0;

void resetIMD() {
    memset(trackData, 0, sizeof(trackData));
}

void addIMD(imd_t *trackPtr) {
    byte fmt[MAXSECTORS];             // skew assignments - assuming starting at 1
    int offset = 0;                      // offset to make fmt align to smap
    byte secToPhsMap[MAXSECTORS];     // map of sectors (-1) to slots
    int missingIdCnt = 0;
    int missingSecCnt = 0;

    bool canRecover = true;

    if (trackPtr->cyl >= MAXCYLINDER || trackPtr->head >= MAXHEAD) {
        logger(ALWAYS, "Track %d/%d invalid\n", trackPtr->cyl, trackPtr->head);
        return;
    }
    if (trackData[trackPtr->cyl][trackPtr->head].smap[0]) {
        logger(ALWAYS, "Ignoring duplicate track %d/%d\n", trackPtr->cyl, trackPtr->head);
        return;
    }
    /* see what sectors are present */
    memset(secToPhsMap, 0xff, sizeof(secToPhsMap));
    for (int i = 0; i < trackPtr->spt; i++) {
        if (trackPtr->smap[i])
            secToPhsMap[trackPtr->smap[i] - 1] = i;
        else
            missingIdCnt++;
        if (!trackPtr->hasData[i])
            missingSecCnt++;
    }
    if (missingIdCnt) {
        int skew = -1;
        for (int i = 0; i < trackPtr->spt - 1; i++)
            if (secToPhsMap[i] != 0xff && secToPhsMap[i + 1] != 0xff) {
                skew = (secToPhsMap[i + 1] - secToPhsMap[i] + trackPtr->spt) % trackPtr->spt;
                break;
            }
        if (skew < 0)
            canRecover = false;
        else {
            /* create a skew map for the track based at 1 */
            int slot = 0;

            memset(fmt, 0, sizeof(fmt));            //
            for (int i = 1; i <= trackPtr->spt; i++) {
                while (fmt[slot])
                    slot = (slot + 1) % trackPtr->spt;
                fmt[slot] = i;
                slot = (slot + skew) % trackPtr->spt;
            }

            for (slot = 0; trackPtr->smap[slot] == 0; slot++)       // find first slot with allocated sector
                ;

            for (offset = 0; fmt[offset] != trackPtr->smap[slot]; offset++)      // find this sector in fmt table
                ;
            offset -= slot;                                         // backup to align with first slot
            /* check whether skew recovery possible */
            for (int i = 0; i < trackPtr->spt; i++) {
                if (trackPtr->smap[i] && trackPtr->smap[i] != fmt[(i + offset) % trackPtr->spt]) {
                    canRecover = false;
                    break;
                }
            }
        }
    }

    if (showSectorMap) {
        logger(ALWAYS, "Track %d/%d Sector Mapping:\n", trackPtr->cyl, trackPtr->head);
        for (int i = 0; i < trackPtr->spt; i++) {
            if (trackPtr->smap[i])
                printf("%02d ", trackPtr->smap[i]);
            else if (canRecover) {
#pragma warning(suppress: 6001)
                trackPtr->smap[i] = fmt[(i + offset) % trackPtr->spt];
                printf("%02dr", trackPtr->smap[i]);
            }
            else
                printf("-- ");
        }
        putchar('\n');
        for (int i = 0; i < trackPtr->spt; i++) {
            putchar(trackPtr->hasData[i] ? 'D' : 'X');
            putchar(' ');
            putchar(' ');
        }putchar('\n');
    }
    else if (missingIdCnt) {
        if (canRecover) {
            logger(ALWAYS, "Track %d/%d recovering sector ids:", trackPtr->cyl, trackPtr->head);
            for (int i = 0; i < trackPtr->spt; i++) {
                if (!trackPtr->smap[i])
#pragma warning(suppress: 6385)
                    printf(" %02d", trackPtr->smap[i] = fmt[(i + offset) % trackPtr->spt]);
            }
            putchar('\n');
        }
        else {
            logger(ALWAYS, "Track %d/%d cannot recover sector ids:", trackPtr->cyl, trackPtr->head);
            for (int i = 0; i < trackPtr->spt; i++, offset = (offset + 1) % trackPtr->spt) {
                if (secToPhsMap[i] == 0xff)
                    printf(" %02d", i + 1);
            }
            putchar('\n');
        }
    }
    if (missingSecCnt) {
        if (missingIdCnt == 0 || canRecover) {
            logger(ALWAYS, "Track %d/%d missing data for sectors:", trackPtr->cyl, trackPtr->head);
            for (int i = 0; i < trackPtr->spt; i++) {
                if (!trackPtr->hasData[i]) {
                    printf(" %02d", trackPtr->smap[i]);
                }
            }
            putchar('\n');
        }
        else
            logger(ALWAYS, "Track %d/%d not usable - missing %d sector Ids and %d data sectors\n",
                trackPtr->cyl, trackPtr->head, missingIdCnt, missingSecCnt);
    }
    if (missingIdCnt == 0 || canRecover) {
        trackData[trackPtr->cyl][trackPtr->head] = *trackPtr;           /* structure copy */
        if (trackPtr->cyl > cylMax)
            cylMax = trackPtr->cyl;
        if (trackPtr->head > headMax)
            headMax = trackPtr->head;
    }
}

void WriteIMDHdr(FILE *fp, char *comment) {
    struct tm *dateTime;
    time_t curTime;

    if (strncmp(comment, "IMD ", 4) != 0) {		// if comment does not have IMD header put one in
        time(&curTime);
        dateTime = localtime(&curTime);
        fprintf(fp, "IMD 1.18 %02d/%02d/%04d %02d:%02d:%02d\r\n", dateTime->tm_mday, dateTime->tm_mon, dateTime->tm_year + 1900,
            dateTime->tm_hour, dateTime->tm_min, dateTime->tm_sec);
    }

    while (*comment) {
        if (*comment == '\n')
            putc('\r', fp);
        putc(*comment++, fp);
    }
    putc(0x1A, fp);
}

bool SameCh(byte *sec, int size) {
    for (int i = 1; i < size; i++)
        if (sec[0] != sec[i])
            return false;
    return true;
}

void WriteImgFile(char *fname, char *comment) {
    FILE *fp;
    imd_t *trackPtr;
    byte *sector;

    if ((fp = fopen(fname, "wb")) == NULL)
        error("cannot create %s\n", fname);

    WriteIMDHdr(fp, comment);
    for (int cyl = 0; cyl <= cylMax; cyl++)
        for (int head = 0; head <= headMax; head++) {
            trackPtr = &trackData[cyl][head];
            if (trackPtr->smap[0] == 0)

                //        if ((trackPtr = chkTrack(track)) == NULL)
                continue;
            putc(trackPtr->mode, fp);           // mode
            putc(trackPtr->cyl, fp);            // cylinder
            putc(trackPtr->head, fp);           // head
            putc(trackPtr->spt, fp);            // sectors in track
            putc(trackPtr->size >> 8, fp);      // sector size
            fwrite(trackPtr->smap, 1, trackPtr->spt, fp);    // sector numbering map

            for (int secNum = 0; secNum < trackPtr->spt; secNum++) {
                if (trackPtr->hasData[secNum]) {
                    sector = trackPtr->track + secNum * trackPtr->size;
                    if (SameCh(sector, trackPtr->size)) {
                        putc(2, fp);
                        putc(*sector, fp);
                    }
                    else {
                        putc(1, fp);
                        fwrite(sector, 1, trackPtr->size, fp);
                    }
                }
                else
                    putc(0, fp);            // data not available
            }
        }

    fclose(fp);
}