#pragma once

/* unidsk.h     (c) by Mark Ogden 2018

DESCRIPTION
    Common type, defines and externs for unidsk
    Portions of the code are based on Dave Duffield's imageDisk sources

MODIFICATION HISTORY
    17 Aug 2018 -- original release as unidsk onto github
    18 Aug 2018 -- added attempted extraction of deleted files for ISIS II/III
    21 Aug 2018 -- Added support for auto looking up files in the repository
                   the new option -l or -L supresses the lookup i.e. local fiiles

NOTES
    This version relies on visual studio's pack pragma to force structures to be byte
    aligned.
    An alternative would be to use byte arrays and index into these to get the relevant
    data via macros or simple function. This approach would also support big edian data.

TODO
    Add support in the recipe file to reference files in the repository vs. local files
    Review the information generated for an ISIS IV disk to see if it is sufficient
    to allow recreation of the original .imd or .img file
    Review the information generated for an ISIS III disk to see it is sufficient to
    recreate the ISIS.LAB and ISIS.FRE files.

*/

#include <stdbool.h>
#include "db.h"
typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned __int32 dword;

#define	TSIZE	32768				// Max size of track

#define MAXCYLINDER	84
#define	MAXSECTOR	52
#define MAXHEAD		2

// disk types
enum { UNKNOWN, ISIS_SD, ISIS_DD, ISIS_PDS, ISIS_IV, ISIS_DOS, ZXENIX, CPM };
#define ISIS_SD_SIZE   256256
#define ISIS_DD_SIZE   512512

// Status index values
#define	ST_TOTAL	0				// Total sector count
#define	ST_COMP		1				// Number Compressed
#define	ST_DAM		2				// Number Deleted
#define	ST_BAD		3				// Number Bad
#define	ST_UNAVAIL	4				// Number unavailable

#define MAXDIR      200
#define MAXCOMMENT  2048
#define MAXLINE   256
#define MAXOUTLINE  120

#define ChecksumSize    27      // base 64 SHA1 hash length

typedef struct {
    char name[12];      // isis name + optional # prefix for recovered files
    char fname[16];     // name file saved as - isis name + possibly #nnn_ prefix
    byte attrib;
    int dirLen;         // dir record of file size or it no header block -eofcnt
    int actLen;         // bytes saved
    Key key;            // base 64 encoding of SHA1
    int errors;
}  isisDir_t;

#pragma pack(push, 1)
typedef struct {
    byte name[9];
    byte version[2];
    byte leftOver[38];
    byte crlf[2];
    byte fmtTable[77];
} label_t;
#pragma pack(pop)


extern int osIdx;
extern bool debug;

void mkRecipe(char const *name, isisDir_t  *isisDir, char *comment, int diskType, bool isOK);
bool isAlt(const char *loc, const char *iname);
char *findMatch(const KeyPtr key, const char *iname);