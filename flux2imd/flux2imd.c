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
#include "container.h"
#include "stdflux.h"

void showVersion(FILE *fp, bool full);

_declspec(noreturn) void usage();
void writeImdFile(char* fname);



static int histLevels = 0;
static int options;
static char *userfmt;       // user specified format
static char *aopt;          // user specified analysis format




static void decodeFile(char *name) {

    if (openFluxFile(name)) {
        bool singleTrack = extMatch(name, ".raw");
        while (loadFluxStream()) {
            if (histLevels)
                displayHist(histLevels);
            if (aopt)
                if (singleTrack)
                    analyse(aopt);
                else
                    logFull(D_WARNING, "-a only supported for single .raw files\n");
            else if (flux2Track(getFormat(userfmt, getCyl(), getHead())))
                displayTrack(getCyl(), getHead(), options | (singleTrack || noIMD() ? gOpt : 0));

        }
        displayDefectMap();
        closeFluxFile();

        if (!singleTrack && !noIMD())
            writeImdFile(name);
        removeDisk();
    }
}



static _declspec(noreturn) void usage() {
    fprintf(stderr,
        "flux2imd - convert fluxfiles to imd format (c) Mark Ogden 15-Jul-2020\n\n"
        "usage: flux2imd -v|-V | [-b] [-d[n]] [-f format] [-g] [-h[n]] [-p] [-s] [zipfile|rawfile]+\n"
        "options can be in any order before the first file name\n"
//      "  -a encoding - undocumented option to help analyse new disk formats\n"
        "  -v|-V  show version information and exit. Must be only option\n"
        "  -b     will write bad (idam or data) sectors to the log file\n"
        "  -d     sets debug flags to n (n is in hex) default is 1 which echos log to console\n"
        "  -f     forces the specified format, use -f help for more info\n"
        "  -g     will write good (idam and data) sectors to the log file\n"
        "  -h     displays flux histogram. n is optional number of levels\n"
        "  -p     ignores parity bit in sector dump ascii display\n"
        "  -s     force writing of physical sector order in the log file\n"
        "Note ZDS disks and rawfiles force -g as image files are not created\n"
#ifdef _DEBUG
        "\nDebug options - add the hex values:\n"
        "  01 -> echo    02 -> flux     04 -> detect    08 -> pattern\n"
        "  10 -> AM      20 -> decode   40 -> no Opt    80 -> tracker\n"
#endif

    );
    exit(1);
}


int main(int argc, char** argv) {
    int arg;
    char optCh;

    createLogFile(NULL);

    if (argc == 2 && _stricmp(argv[1], "-v") == 0) {
        showVersion(stdout, argv[1][1] == 'V');
        exit(0);
    }
    for (arg = 1; arg < argc && sscanf(argv[arg], "-%c", &optCh) == 1; arg++) {
        switch (tolower(optCh)) {
        case 'g':
            options |= gOpt;
            break;
        case 'b':
            options |= bOpt;
            break;
        case 's':
            options |= sOpt;
            break;
        case 'd':
            if (argv[arg][2]) {
                if (sscanf(argv[arg] + 2, "%x", &debug) != 1)
                    debug = D_ECHO;
            } else if (arg + 1 < argc && isxdigit(argv[arg + 1][0]) && sscanf(argv[arg + 1], "%x%c", &debug, &optCh) == 1)
                arg++;
             else
                 debug = D_ECHO;;
            break;
        case 'h':
            if (argv[arg][2]) {
                if (sscanf(argv[arg] + 2, "%d", &histLevels) != 1)
                    histLevels = 10;
            } else if (arg + 1 < argc && isxdigit(argv[arg + 1][0]) && sscanf(argv[arg + 1], "%d%c", &histLevels, &optCh) == 1)
                arg++;
            else
                histLevels = 10;
            if (histLevels <= 5)
                histLevels = 10;
            break;
        case 'p':
            options |= pOpt;
            break;
        case 'f':
            if (argv[arg][2])
                userfmt = argv[arg] + 2;
            else if (arg + 1 < argc) {
                userfmt = argv[++arg];
            } else {
                fprintf(stderr, "missing format after -f\n");
                usage();
            }
            if (_stricmp(userfmt, "help") == 0)
                showFormats();

            break;
        case 'a':
            if (argv[arg][2])
                aopt = argv[arg] + 2;
            else if (arg + 1 < argc) {
                aopt = argv[++arg];
            } else {
                fprintf(stderr, "missing analysis format after -a\n");
                usage();
            }
            break;

        default:
            fprintf(stderr, "invalid option -%c\n", optCh);
            usage();
        }
    }
    if (aopt && userfmt) {
        fprintf(stderr, "-a and -f cannot be both specified");
        usage();
    }
    if (arg == argc)
        usage();

    for (; arg < argc; arg++)
        decodeFile(argv[arg]);
    return 0;
}