/****************************************************************************
 *  program: irepo - Intel Repository Tool                                  *
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


#include <stdbool.h>


#define KEYSIZE    27      // base 64 SHA1 hash length
typedef char *KeyPtr, Key[KEYSIZE + 1];

void closeDb();
KeyPtr firstKey();
char *firstLoc(KeyPtr key);


int getFileKey(const char *fname, KeyPtr key);
const char *getDbLoc(const char *loc);
const KeyPtr getDbLocKey(const char *loc);
bool isDir(const char *fname);
bool isFile(const char *fname);
bool isValidKey(const KeyPtr key);
bool keyExists(const KeyPtr key);
bool isValidPair(const KeyPtr key, const char *loc);
KeyPtr nextKey();
char *nextLoc();
#ifdef _REBUILD
void openDb(bool rebuild);
#else
void openDb();
#endif