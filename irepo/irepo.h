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



#ifndef REFRESH_H
#define REFRESH_H
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "db.h"

#define MAXLINE	512

typedef struct {
	char *iname;
	char *altname;		// used to select alternatives. If null then match all, else just those with the iname prefix
	char *attrib;
	char *key;
	bool inRepo;
	bool changed;
	char *loc;
	char *tail;
} recipe_t;


char *findMatch(const KeyPtr key, const char *iname);
const char *fileName(const char *loc);
bool isAlt(const char *loc, const char *iname);
char *mkIname(const char *loc);

char *chkSpecial(recipe_t *r);
bool parseRecipe(char *line, recipe_t *recipe);
bool printRecipe(FILE *fp, recipe_t *recipe, bool showAlt);
bool updateRecipe(recipe_t *r);

#endif#


