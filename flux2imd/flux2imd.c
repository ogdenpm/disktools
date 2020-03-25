// This is an open source non-commercial project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

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
#include "flux.h"
#include "flux2imd.h"
#include "trackManager.h"
#include "util.h"
#include "zip.h"

_declspec(noreturn) void usage();
void writeImdFile(char* fname);

char curFile[_MAX_FNAME * 2 + 3];      // fname[zipname]

static int histLevels = 0;
static int options;

static bool loadFile(char *name);
static bool loadZipFile(struct zip_t *zip);
static bool getCylinder(const char *fname, int *pCyl, int *pHead);

static void decodeFile(char* name) {
    int cyl, head;

    strcpy(curFile, fileName(name));
    createLogFile(name);

    if (extMatch(name, ".raw")) {
        
        if (!loadFile(name))
            return;

        if (histLevels)
            displayHist(histLevels);

        if (getCylinder(name, &cyl, &head)) {
            if (flux2Track(cyl, head)) {
                displayTrack(cyl, head, options | gOpt);
                displayDefectMap();
            }
        } else
            logFull(ALWAYS, "Skipping decode - cannot determine cylinder & head from file name\n");

        unloadFlux();
        removeDisk();

    } else if (extMatch(name, ".zip")) {

        struct zip_t* zip;
        if ((zip = zip_open(name, 0, 'r')) == NULL)
            logFull(D_ERROR, "Can't open %s\n", name);
        else {
            int n = zip_total_entries(zip);
            removeDisk();
            for (int i = 0; i < n; i++) {
                zip_entry_openbyindex(zip, i);
                sprintf(curFile, "%s[%s]", fileName(name), fileName(zip_entry_name(zip)));
                if (loadZipFile(zip)) {
                    if (histLevels)
                        displayHist(histLevels);
                    if (getCylinder(zip_entry_name(zip), &cyl, &head)) {
                        if (flux2Track(cyl, head))
                            displayTrack(cyl, head, options | (noIMD() ? gOpt : 0));
                    }
                    else
                        logFull(ALWAYS, "Skipping decode - cannot determine cylinder & head from file name\n");
                }
                zip_entry_close(zip);
                unloadFlux();
            }
            zip_close(zip);
            strcpy(curFile, fileName(name));        // revert logging to original file name
            displayDefectMap();
            if (!noIMD())
                writeImdFile(name);
            removeDisk();
        }
    } else {
        logFull(D_ERROR, "file %s does not have .raw or .zip extension\n", name);
        usage();
    }
    createLogFile(NULL);
}

static bool getCylinder(const char *fname, int *pCyl, int *pHead)
{
    char *s = strrchr(fname, '.');

    return s && s - fname > 4 && sscanf(s - 4, "%d.%d", pCyl, pHead) == 2;
}

static bool loadFile(char *name) {
    size_t bufsize;
    uint8_t *buf;
    FILE *fp;
    bool ok = false;;

    if ((fp = fopen(name, "rb")) == NULL) {
        logFull(D_ERROR, "Can't open %s\n", name);
        return false;
    }

    fseek(fp, 0, SEEK_END);
    bufsize = ftell(fp);
    rewind(fp);

    buf = (uint8_t *)xmalloc(bufsize);
    if (fread(buf, bufsize, 1, fp) != 1)
        logFull(D_ERROR, "Failed to load %s\n", name);
    else {
        loadFlux(buf, bufsize);         // load in the flux data from buffer extracted from zip file
        ok = true;
    }
    fclose(fp);
    return ok;
}

static bool loadZipFile(struct zip_t *zip) {
    size_t bufsize;
    uint8_t *buf;
    const char *name = zip_entry_name(zip);

    if (zip_entry_isdir(zip))
        return false;

    if (!extMatch(name, ".raw")) {
        logFull(D_ERROR, "cannot process %s\n", name);
        return false;
    }
    
    bufsize = (size_t)zip_entry_size(zip);
    buf = (uint8_t *)xmalloc(bufsize);
    memset(buf, 0, bufsize);
    if (zip_entry_noallocread(zip, (void *)buf, bufsize) < 0) {
        logFull(D_ERROR, "Failed to load\n");
        free(buf);
        return false;
    }
    loadFlux(buf, bufsize);         // load in the flux data from buffer extracted from zip file
    return true;
}

static _declspec(noreturn) void usage() {
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
    char optCh;

    createLogFile(NULL);

    for (arg = 1; arg < argc && sscanf(argv[arg], "-%c", &optCh) == 1; arg++) {
        switch (tolower(optCh)) {
        case 'g':
            options |= gOpt;
            break;
        case 'b':
            options |= bOpt;
            break;
        case 'd':
            if (argv[arg][2]) {
                if (sscanf(argv[arg] + 2, "%x", &debug) != 1)
                    debug = D_ECHO;
            } else if (arg + 1 < argc && sscanf(argv[arg + 1], "%x%c", &debug, &optCh) == 1)
                arg++;
             else
                 debug = D_ECHO;;
            break;
        case 'h':
            if (argv[arg][2]) {
                if (sscanf(argv[arg] + 2, "%d", &histLevels) != 1)
                    histLevels = 10;
            } else if (arg + 1 < argc && sscanf(argv[arg + 1], "%d%c", &histLevels, &optCh) == 1)
                arg++;
            else
                histLevels = 10;
            if (histLevels <= 5)
                histLevels = 10;
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