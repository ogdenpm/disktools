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

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

// option flags
#define pOpt    1
#define gOpt    2
#define bOpt    4
#define sOpt    8

// current file for log prefix
extern char curFile[];

enum {
    ALWAYS = 0, D_ECHO = 1, D_FLUX = 2, D_DETECT = 4, D_PATTERN = 8,
    D_ADDRESSMARK = 0x10, D_DECODER = 0x20,  D_NOOPTIMISE = 0x40, D_TRACKER = 0x80,
    D_WARNING = 0xfffd, D_ERROR = 0xfffe, D_FATAL = 0xffff
};

#if _DEBUG
#define DBGLOG(level, ...)   logFull(level, __VA_ARGS__)
#else
#define DBGLOG(level, ...)
#endif

extern unsigned debug;

void* xmalloc(size_t size);
uint8_t flip[];
void logFull(int level, char* fmt, ...);
int logBasic(char* fmt, ...);

bool extMatch(const char* fname, const char* ext);
const char* fileName(const char* fname);
void createLogFile(const char *fname);
