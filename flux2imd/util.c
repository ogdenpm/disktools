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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#ifdef __GNUC__
#include <limits.h>
#define _MAX_PATH PATH_MAX
#endif

char logPrefix[_MAX_PATH + 3];      // fname[item];
unsigned debug;
FILE *logFp = NULL;  

// flip the order of the data
uint8_t flip[] = {
 0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
 0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
 0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
 0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
 0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
 0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
 0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
 0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
 0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
 0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
 0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
 0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
 0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
 0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
 0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
 0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
};


bool extMatch(const char* fname, const char* ext) {
    char* s = strrchr(fname, '.');
    return (s && stricmp(s, ext) == 0);
}

const char* basename(const char* fname) {
    char* s = strrchr(fname, '/');
    char* t = strrchr(fname, '\\');
    if (!s || (t && s < t))
        s = t;
    if (s)
        return s + 1;
    return fname;
}


int logBasic(char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    if ((debug & D_ECHO) && logFp != stdout)
        vprintf(fmt, args);
    int nchars = vfprintf(logFp, fmt, args);
    va_end(args);
    return nchars;
}

void logFull(int level, char *fmt, ...) {
    static char* prefix[] = { "WARNING", "ERROR", "FATAL" };

    va_list args;
    va_start(args, fmt);

    if (level >= D_WARNING) {
        if (logFp != stdout) {
            fprintf(logFp, "%s - %s: ", logPrefix, prefix[level - D_WARNING]);
            vfprintf(logFp, fmt, args);
        }
        fprintf(stderr, "%s - %s: ", logPrefix, prefix[level - D_WARNING]);
        vfprintf(stderr, fmt, args);
    }
    else if (level ==  0 || (debug & level)) {
        fprintf(logFp, "%s%s", logPrefix, *fmt ? " - " : ""); // don't use - separator if no string to emit
        vfprintf(logFp, fmt, args);
        if ((debug & D_ECHO) && logFp != stdout) {
            printf("%s%s", logPrefix, *fmt ? " - " : "");
            vprintf(fmt, args);
        }
    }
 
    if (level == D_FATAL) {
        if (logFp != stdout)
            fclose(logFp);
        exit(1);
    }
    va_end(args);
}

void createLogFile(const char* name) {
    char logFile[_MAX_PATH + 1];
    if (logFp && logFp != stdout)
        fclose(logFp);
    if (name) {
        strcpy(logFile, name);
        strcpy(strrchr(logFile, '.'), ".log");
        if ((logFp = fopen(logFile, "wt")) == NULL) {
            logFp = stdout;
            logFull(D_ERROR, "could not create %s; using stdout\n", basename(logFile));
        }
    } else
        logFp = stdout;

}

void* xmalloc(size_t size) {
    void* ptr;
    if (!(ptr = malloc(size))) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return ptr;
}


void setLogPrefix(const char *container, const char *element) {
    strcpy(logPrefix, basename(container));
    if (element && *element)
        sprintf(strchr(logPrefix, 0), "[%s]", basename(element));
}