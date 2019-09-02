#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <limits.h>
#include <memory.h>
#include <string.h>
#include <ctype.h>
#include "zip.h"
#include "flux.h"
#include "dpll.h"
#include "analysis.h"
#include "util.h"
#include "decoders.h"
#include "bits.h"
#include "sectorManager.h"
#include "display.h"

_declspec(noreturn) void usage();
void writeImdFile(char* fname);

char curFile[_MAX_FNAME * 2 + 3];      // fname[zipname]
char logFile[_MAX_PATH + 1];

int histLevels = 0;
int debug = 0;
int options;
bool isZDS = false;
#define pOpt    1
#define gOpt    2
#define bOpt    4


FILE* logFp;

void hsFlux2Track(int cylinder, int side) {
    if (side != 0) {
        logFull(ALWAYS, "%d/%d hard Sector disk only supports single side\n", cylinder, side);
        return;
    }
    int hardSectors = cntHardSectors();
    int sectorToSlot[32] = { 0 };
    hsGetTrack(cylinder);
    if (trackPtr->fmt->options & O_ZDS)
        isZDS = true;


}

void flux2Track(int cylinder, int side, unsigned options) {
    char *fmt = NULL;
    int maxSlot = 0;
    if (cntHardSectors() > 0) {
        hsFlux2Track(cylinder, side);
        displayTrack(cylinder, side, options | (isZDS ? gOpt: 0));

    }
    else if (!setEncoding(NULL)) {
        logFull(WARNING, "could not determine encoding\n");
        return;
    } else {
        ssGetTrack(cylinder, side);
        displayTrack(cylinder, side, options);
    }
}


bool loadFile(char *name, bool warnNoOpen) {
    size_t bufsize;
    uint8_t *buf;
    FILE *fp;
    bool ok = false;;

    if ((fp = fopen(name, "rb")) == NULL) {
        if (warnNoOpen)
            logFull(ERROR, "Can't open %s\n", name);
        return false;
    }

    fseek(fp, 0, SEEK_END);
    bufsize = ftell(fp);
    rewind(fp);

    buf = (uint8_t *)xmalloc(bufsize);
    if (fread(buf, bufsize, 1, fp) != 1)
        logFull(ERROR, "Failed to load %s\n", name);
    else {
        loadFlux(buf, bufsize);         // load in the flux data from buffer extracted from zip file
        ok = true;
    }
    fclose(fp);
    return ok;
}

bool loadZipFile(struct zip_t *zip) {
    size_t bufsize;
    uint8_t *buf;
    const char *name = zip_entry_name(zip);

    if (zip_entry_isdir(zip))
        return false;

    if (!extMatch(name, ".raw")) {
        logFull(ERROR, "cannot process %s\n", name);
        return false;
    }
    
    bufsize = (size_t)zip_entry_size(zip);
    buf = (uint8_t *)xmalloc(bufsize);
    memset(buf, 0, bufsize);
    if (zip_entry_noallocread(zip, (void *)buf, bufsize) < 0) {
        logFull(ERROR, "Failed to load\n");
        free(buf);
        return false;
    }
    loadFlux(buf, bufsize);         // load in the flux data from buffer extracted from zip file
    return true;
}

int getCylinder(char *fname)
{
    char *s = strrchr(fname, '.');
    int cyl, head;
    if (s && s - fname > 4 && sscanf(s - 4, "%d.%d", &cyl, &head) == 2)
        return cyl + (head << 16);
    return 99;
}


void openLogFile(char* name) {
    char logFile[_MAX_PATH + 1];
    strcpy(logFile, name);
    strcpy(strrchr(logFile, '.'), ".log");
    if (logFp && logFp != stdout)
        fclose(logFp);
    if ((logFp = fopen(logFile, "wt")) == NULL) {
        logFp = stdout;
        logFull(ERROR, "could not create %s; using stdout\n", fileName(logFile));
    }

}

void decodeFile(char* name) {
    int cylHead;
    strcpy(curFile, fileName(name));
    isZDS = false;

    if (extMatch(name, ".raw")) {
        openLogFile(name);
        
        if (!loadFile(name, true))
            return;
        if (histLevels)
            displayHist(histLevels);
        cylHead = getCylinder(curFile);

        flux2Track(cylHead & 0xff, cylHead >> 16, options | gOpt);
        unloadFlux();
        removeDisk();
    } else if (extMatch(name, ".zip")) {
        struct zip_t* zip;
        openLogFile(name);
        if ((zip = zip_open(name, 0, 'r')) == NULL)
            logFull(ERROR, "Can't open %s\n", name);
        else {
            int n = zip_total_entries(zip);
            removeDisk();
            for (int i = 0; i < n; i++) {
                zip_entry_openbyindex(zip, i);
                sprintf(curFile, "%s[%s]", fileName(name), zip_entry_name(zip));
                if (loadZipFile(zip)) {
                    if (histLevels)
                        displayHist(histLevels);
                    cylHead = getCylinder(curFile);

                    flux2Track(cylHead & 0xff, cylHead >> 16, options);

                }
                zip_entry_close(zip);
                unloadFlux();
            }
            zip_close(zip);
            strcpy(curFile, fileName(name));        // revert logging to original file name
            if (!isZDS)
                writeImdFile(name);
            removeDisk();
        }
    } else {
        logFull(ERROR, "file %s does not have .raw or .zip extension\n", name);
        usage();
    }
    if (logFp && logFp != stdout) {
        fclose(logFp);
        logFp = NULL;
    }

}





_declspec(noreturn) void usage() {
    fprintf(stderr,
        "usage: analysis [-b] [-d[n]] [-g] [-h[n]] [-p] [zipfile|rawfile]+\n"
        "options can be in any order before the first file name\n"
        "-b will write bad (idam or data) sectors to the log file\n"
        "-d sets debug flags to n (n is in hex) default is 1 which echos log to console\n"
        "-g will write good (idam and data) sectors to the log file\n"
        "-h displays flux histogram. n is optional number of levels\n"
        "-p ignores parity bit in sector dump ascii display\n"
        "Note ZDS disks and rawfiles force -g as image files are not created\n"
    );
    exit(1);
}


int main(int argc, char** argv) {
    int arg;
    int optVal;
    char optCh;


    for (arg = 1; arg < argc && sscanf(argv[arg], "-%c", &optCh); arg++) {
        switch (tolower(optCh)) {
        case 'g':
            options |= gOpt;
            break;
        case 'b':
            options |= bOpt;
            break;
        case 'd':
            debug = sscanf(argv[arg] + 2, "%x", &optVal) ? optVal : ECHO;
            break;
        case 'h':
            histLevels = sscanf(argv[arg] +2, "%d", &optVal) && optVal > 5 ? optVal : 10;
            break;
        case 'p':
            options |= pOpt;
            break;
        default:
            fprintf(stderr, "invalid option -%c\n", optCh);
            usage();
        }
    }
    if (arg == argc)
        usage();

    for (; arg < argc; arg++)
        decodeFile(argv[arg]);

    return 0;
}