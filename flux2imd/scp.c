#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <memory.h>

#include "stdflux.h"
#include "scp.h"
#include "util.h"


#define MAXREV  10      // maximum number of revolutions (normally 5)


static FILE *scpFp;

static uint8_t scpHeader[16];
static uint32_t trkOffset[168];
static uint8_t curTrack;
static const char *scpFname;

static struct {
    double rpm;
    uint32_t fluxCnt;
    uint32_t base;
} trkData[MAXREV];



static uint32_t scp32(FILE *scpFp) {
    uint32_t val = 0;
    for (int i = 0; i < 32; i += 8)
        val += getc(scpFp) << i;
    return val;
}


bool scpOpen(const char *fname) {
    curTrack = 0;
    if (scpFp)
        fclose(scpFp);


    if ((scpFp = fopen(fname, "rb")) == NULL) {
        logFull(D_WARNING, "cannot open file\n");
        return false;
    }
    if (fread(scpHeader, 1, sizeof(scpHeader), scpFp) != sizeof(scpHeader) || memcmp(scpHeader, "SCP", 3) != 0) {
        fclose(scpFp);
        scpFp = NULL;
        logFull(D_WARNING, "file is not valid\n");
        return false;
    }
    if (scpHeader[IFF_FLAGS] & (1 << FB_EXTENDED))
        fseek(scpFp, 0x80, SEEK_SET);
    for (int i = 0; i <= scpHeader[IFF_END]; i++)
        trkOffset[i] = scp32(scpFp);
    if (feof(scpFp)) {
        fclose(scpFp);
        scpFp = NULL;
        logFull(D_WARNING, "file is not valid\n");
        return false;
    }
    if (!(scpHeader[IFF_FLAGS] & (1 << FB_INDEX)))
        logFull(D_WARNING, "data is not index pulse aligned\n");
    scpFname = fname;
    if (scpHeader[IFF_NUMREVS] > MAXREV) {
        scpHeader[IFF_NUMREVS] = MAXREV;
        logFull(D_WARNING, "Revolutions limited to %d\n", MAXREV);
    }
    createLogFile(fname);
    return true;
}



scpClose() {
    if (scpFp)
        fclose(scpFp);
    scpFp = NULL;
    curTrack = scpHeader[IFF_HEADS] == 2;
    return true;
}



int32_t scpSample(FILE *fp) {
    int ch, cl;
    if ((ch = getc(fp)) != EOF  && (cl = getc(fp)) != EOF)
            return (ch << 8) + cl;
    return EOF;
}

bool scpLoadTrk(uint16_t trk) {
    char ct[10];
    uint8_t trkHdr[4];
    sprintf(ct, "%d,%d", trk / 2, trk % 2);
    setLogPrefix(scpFname, ct);
    if (fseek(scpFp, trkOffset[trk], SEEK_SET) ||
        fread(trkHdr, 1, 4, scpFp) != 4 ||
        memcmp(trkHdr, "TRK", 3) != 0 ||
        trkHdr[3] != trk) {
        logFull(D_WARNING, "track info missing\n");
        return false;
    }
    uint32_t fluxTotal = 0;
    for (int i = 0; i < scpHeader[IFF_NUMREVS]; i++) {
        trkData[i].rpm = 60.0 / (scp32(scpFp) * 25e-9);
        fluxTotal += trkData[i].fluxCnt = scp32(scpFp);
        trkData[i].base = scp32(scpFp);
    }
    double sclk = 1 / (25e-9 * (scpHeader[IFF_RESOLUTION] + 1));
    beginFlux(fluxTotal, scpHeader[IFF_NUMREVS] + 1, sclk, (scpHeader[IFF_FLAGS] & (1 << FB_RPM)) ? 360.0 : 300.0, 0);
    setCylHead(trk / 2, trk % 2);
    int32_t delta = 0;
    int32_t sample;
    bool loaded = true;
    for (int i = 0; loaded && i < scpHeader[IFF_NUMREVS]; i++) {
        setActualRPM(trkData[i].rpm);
        addIndex(SSSTART, 0);
        if (fseek(scpFp, trkOffset[trk] + trkData[i].base, SEEK_SET)) {
            loaded = false;
            break;
        }
        for (uint32_t j = 0; j < trkData[i].fluxCnt; j++) {
            if ((sample = scpSample(scpFp)) == EOF) {
                loaded = false;
                break;
            } else if (sample == 0)
                delta += 0x1000;
            else {
                addDelta(delta + sample);
                delta = 0;
            }
        }
    }
    if (loaded == false) {
        logFull(D_WARNING, "Track load error\n");
        setLogPrefix(scpFname, NULL);
    } else
        endFlux();
    return loaded;
}

bool scpLoad() {
    bool loaded = false;

    while (!loaded && curTrack <= scpHeader[IFF_END]) {
        setLogPrefix(scpFname, NULL);
        if (trkOffset[curTrack])
            loaded = scpLoadTrk(curTrack);
        curTrack += scpHeader[IFF_HEADS] == 0 ? 1 : 2;
    }
    return loaded;
}


