#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "irepo.h"


const char *fileName(const char *loc) {
	const char *s;
#ifdef _WIN32						// windows allows \ separator
    if (s = strrchr(loc, '\\'))
        loc = s + 1;
#endif
    if (s = strrchr(loc, '/'))		// *nix and windows / separator
        loc =  s + 1;
    return loc;
}


// return the ISIS compatible prefix of the filename part of the loc path
// i.e. upto 6.3 alphanumeric name, coverted to uppercase
char *mkIname(const char *loc) {
    static char iname[11];
    loc = fileName(loc);
    char *s = iname;

    for (int i = 0; i < 6 && isalnum(*loc);)
        *s++ = toupper(*loc++);
    if (*loc++ == '.' && isalnum(*loc)) {
        *s++ = '.';
        for (int i = 0; i < 3 && isalnum(*loc);)
            *s++ = toupper(*loc++);
    }
    *s = 0;
    return iname;
}


// compare the location and iname
// only the ISIS compatible prefix of the location filename is used see mkIname

int cmpIname(const char *loc, const char *iname) {
    const char *fname = mkIname(loc);

    if (_stricmp(iname, fname) == 0)
        return (int)(strlen(fileName(loc)) - strlen(iname) + 1);
    return 0;
}

// finds a location with a filename part match to iname, or any name if iname is NULL
// exact match will return immediately
// if not the match with the shortest filename (then shortest path) is chosen 
char *findMatch(const KeyPtr key, const char *iname) {
    char *loc = firstLoc(key);

    char *candidate = NULL;
    int clen;

    while (loc) {
        int cmp;
        if (!iname)
            cmp = (int)strlen(fileName(loc));
        else if ((cmp = cmpIname(loc, iname)) == 1)
            return loc;

        if (cmp && (!candidate || cmp < clen || (cmp == clen && strlen(loc) < strlen(candidate)))) {
            candidate = loc;
            clen = cmp;
        }
        loc = nextLoc();
    }

    return candidate;
}

bool isAlt(const char *loc, const char *iname) {
    return !iname || cmpIname(loc, iname) > 0;

}



// split the recipe line, return false it it doesn't contain enough fields
bool parseRecipe(char *line, recipe_t *recipe) {
	recipe->altname = recipe->iname = line;
	if ((recipe->attrib = strchr(line, ',')) && (recipe->key = strchr(recipe->attrib + 1, ',')) && (recipe->loc = strchr(recipe->key + 1, ','))) {
		*recipe->attrib++ = *recipe->key++ = *recipe->loc++ = 0;
		recipe->inRepo = *recipe->loc == '^';
		if (recipe->inRepo)
			recipe->loc++;
		return true;
	}
	return false;
}

// print the recipe line, along with alternates if requested
// returns true if alternatives printed
bool printRecipe(FILE *fp, recipe_t *recipe, bool showAlt) {
	fprintf(fp, "%s,%s,%s,%s%s\n", recipe->iname, recipe->attrib, recipe->key, recipe->inRepo ? "^" : "", recipe->loc);
	if (showAlt && recipe->inRepo) {
		bool altPrinted = false;
		for (char *lp = firstLoc(recipe->key); lp; lp = nextLoc())
			if (lp != recipe->loc && isAlt(lp, recipe->altname)) {
				fprintf(fp, "# also ^%s\n", lp);
				altPrinted = true;
			}
		return altPrinted;
	}
	return false;
}

char *chkSpecial(recipe_t *r) {
	static char *special[] = { "AUTO", "ZERO", "ZEROHDR", NULL };
	static char *isisauto[] = { "ISIS.DIR", "ISIS.MAP", "ISIS.LAB", "ISIS.FRE", NULL };
	for (int i = 0; special[i]; i++)
		if (strcmp(r->loc, special[i]) == 0)
			return special[i];
	for (int i = 0; isisauto[i]; i++)
		if (stricmp(r->iname, isisauto[i]) == 0)
			return "";

	return NULL;
}


// update the recipe
// note if in repository loc is updated to point to the repository entry
// returns true if updated
bool updateRecipe(recipe_t *r) {
	bool changed = false;
	const char *s;
	// check for special cases
	if (!r->inRepo && (s = chkSpecial(r))) {
		if (s && *s == 0) {			// loc was not special but original is reserved name
			r->loc = "AUTO";		// new standard is to make into AUTO
			changed = true;
		}
		if (*r->key && *r->key != '*') {	// error messages are kept
			r->key = "";					// clear any key
			changed = true;
		}
		return changed;
	}

	// fix up repo references
	if (r->inRepo) {
		const char *lp = getDbLoc(r->loc);
		if (!lp) {
			fprintf(stderr, "Warning (re)moved file: %s\n", r->loc);
			r->inRepo = false;
			strcpy(r->loc, "*");		// force iname behaviour below
		} else
			r->loc = (char *)lp;		// safe
	}
	if (*r->loc == '*') {				// old format allowed comment instead of name for errors
		strcpy(r->loc, r->iname);		// iname is upper case so copy to loc and convert to lowercase
		strlwr(r->loc);
		changed = true;
	}

	// if the file is in the repository then update the key if necessary
	if (r->inRepo) {
		KeyPtr key = getDbLocKey(r->loc);
		if (strcmp(r->key, key) != 0) {
			r->key = key;
			changed = true;
		}
	} else if (isValidKey(r->key)) {				// if key looks valid see if name exists
		const char *newloc;
		r->inRepo = (newloc = findMatch(r->key, r->iname)) ||			// do we have an entry matching the isis name
			(newloc = findMatch(r->key, r->altname = NULL));	// or any entry
		if (r->inRepo) {
			r->loc = (char *)newloc;				// safe
			changed = true;
		}
	}

	if (!r->inRepo) {								   // if not in repo and file exists use it information to override key
		if (*r->loc == '*') {                          // old comment format, replace with name
			r->loc = r->iname;
			changed = true;
		}
		if (*r->key != '*') {							// don't update if recipe signalled error
			static Key fileKey;							// so we can return an updated key
			switch (getFileKey(r->loc, fileKey)) {
			case -1:
				r->key = "* file not found *";
				fprintf(stderr, "Warning: no match to key: %s or location: %s\n", r->key, r->loc);
				break;
			case 0:
				r->loc = "ZERO";
				r->key = "";
				break;
			default:
				changed |= strcmp(r->key, fileKey) != 0;
				r->key = fileKey;
			}
		}
	}
	return changed;
}
