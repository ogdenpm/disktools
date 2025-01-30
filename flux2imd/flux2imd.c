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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <limits.h>
#include <memory.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "flux.h"
#include "flux2imd.h"
#include "trackManager.h"
#include "util.h"
#include "zip.h"
#include "container.h"
#include "stdflux.h"
#include "utility.h"


void writeImdFile(const char *fname);



static int histLevels = 0;
static int options;
static char const *userfmt;       // user specified format
static char const *aopt;          // user specified analysis format

char const help[] =
    "usage: %s [-b] [-d [=n]] [-f format] [-g] [-h [=n]] [-p] [-s] [zipfile|rawfile]+\n"
    "options can be in any order before the first file name\n"
    //"  -a encoding - undocumented option to help analyse new disk formats\n"
    "  -b      write bad (idam or data) sectors to the log file\n"
    "  -d [=n] sets debug flags to n (n is in hex) default is 1 which echos log to console\n"
    "  -f fmt  forces the specified format, use -f help for more info\n"
    "  -g      write good (idam and data) sectors to the log file\n"
    "  -h [=n] displays flux histogram. n is optional number of levels\n"
    "  -p      ignores parity bit in sector dump ascii display\n"
    "  -s      force writing of physical sector order in the log file\n"
    "Note ZDS disks and rawfiles force -g as image files are not created\n"
#ifdef _DEBUG
    "\nDebug options - add the hex values:\n"
    "  01 -> echo    02 -> flux     04 -> detect    08 -> pattern\n"
    "  10 -> AM      20 -> decode   40 -> no Opt    80 -> tracker\n"
#endif
    ;


static void decodeFile(const char *name) {

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
            else if (flux2Track(userfmt))
                displayTrack(getCyl(), getHead(), options | (singleTrack || noIMD() ? gOpt : 0));
        }
   
        displayDefectMap();
        closeFluxFile();

        if (!singleTrack && !noIMD())
            writeImdFile(name);
        removeDisk();
   }
}




int main(int argc, char** argv) {
    char *endPtr;

    createLogFile(NULL);

    while (getopt(argc, argv, "a:bd=f:gh=ps") != EOF) {
        switch (optopt) {
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
            if (optarg) {
                debug = (unsigned)strtoul(optarg, &endPtr, 16);
                if (*endPtr) {
                    warn("Invalid hex value '%s' for -d option", optarg);
                    debug = D_ECHO;
                }
            } else
                debug = D_ECHO;
            break;
        case 'h':
            if (optarg) {
                histLevels = (unsigned)strtoul(optarg, &endPtr, 10);
                if (*endPtr)
                    warn("Invalid numeric value '%s' for -h  option", optarg);
                if (histLevels <= 5)
                    histLevels = 10;
            } else
                histLevels = 10;
            break;
        case 'p':
            options |= pOpt;
            break;
        case 'f':
            userfmt = optarg;
            if (stricmp(userfmt, "help") == 0)
                showFormats();
            break;
        case 'a':
            aopt = optarg;
            break;

        default:
            usage("invalid option -%c", optopt);
        }
    }
    if (aopt && userfmt)
        usage("-a and -f cannot be both specified");
    if (optind >= argc)
        usage("No files to process");

    while (optind < argc)
        decodeFile(argv[optind++]);
    return 0;
}