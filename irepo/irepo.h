
#ifndef REFRESH_H
#define REFRESH_H
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "db.h"

#define MAXLINE	512

char *mkIname(const char *loc);
char *findMatch(const KeyPtr key, const char *iname);
const char *fileName(const char *loc);
bool printEntry(FILE *fp, const char *iname, const char *attrib, const KeyPtr key, const char *loc, bool showAlt);
void showDuplicates();
bool isSpecial(const char *name);
bool isAlt(const char *loc, const char *iname);

#endif#


