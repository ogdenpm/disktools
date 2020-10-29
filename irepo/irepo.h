
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
	char *loc;
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


