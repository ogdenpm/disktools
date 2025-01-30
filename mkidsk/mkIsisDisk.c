/****************************************************************************
 *  program: mkidsk - create IMD or IMG file from a recipe file             *
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
/*
DESCRIPTION
    Creates ISIS II format disk images in .imd or .img format using a recipe file
    Supports:
    * SD and DD formats and will apply both sector and track interleave as required
    * non standard values for disk formatting byte
    Portions of the code are based on Dave Duffield's imageDisk sources

MODIFICATION HISTORY
    17 Aug 2018 -- original release as mkidsk onto github
    18 Aug 2018 -- updated to use non standard skew information and disk formatting byte
    19 Aug 2019 -- Changed to use environment variable IFILEREPO for location of
                   file repository. Changed location name to use ^ prefix for
                   repository based files, removing need for ./ prefix for
                   local files
    20 Aug 2019 -- Updated to use SHA1 checksum, removing the len field
    21 Aug 2019 -- correted handling of a directory path before the recipe file
    13 Sep 2018 -- renamed skew to interleave to align with normal terminology
    15 Oct 2019 -- added support to allow fill of junk values for ISIS III version and
                   crlf values in ISIS.LAB - see undisk
    later changes see git.


LIMITATIONS
    There are three cases where mkidsk will not create an exact image, although physical
    disks create from these images should perform without problem.
    1) The disk used for the original image had files added and deleted. In this case
       the residual data from the deleted files will be missing in the image.
       Also deleted files may cause some files be created in different isis.dir slots
       and with different link block information.
    2) The default disk images are created without interleave and using 0xc7 as the
       initialisation byte for formating.
       Sector interleave according to the ISIS.LAB information can be applied as can
       the inter track interleave used in the isis formatting utilities.
       Also the 0xC7 byte can be changed e.g. to 0xe5 or 0.
       Despite this flexibility for the .imd disks there may be differences because IMD
       sometimes incorrectly detects the first sector. Use Dave Duffield's MDU utility
       to see this.
    3) If the comment information is changed, the IMD files will be different although
       disks created will not.

 NOTES
    This version relies on visual studio's pack pragma to force structures to be byte
    aligned.
    An alternative would be to use byte arrays and index into these to get the relevant
    data via macros or simple function. This approach would also support big edian data


RECIPE file format
The following is the current recipe format, which has change a little recently
with attributes split out and new locations behaviour

initial comment lines
    multiple comment lines each beginning a # and a space
    these are included in generated IMD files and if the first comment starts with IMD
    then this is used as the signature otherwise a new IMD signature is generated with
    the current date /time
information lines which can occur in any order, although the order below is the default
and can be separated with multiple comment lines starting with #. These comments are ignored
    label: name[.|-]ext     Used in ISIS.LAB name has max 6 chars, ext has max 3 chars
    version: nn             Up to 2 chars used in ISIS.LAB (extended to allow char as #xx hex value)
    format: diskFormat      ISIS II SD, ISIS II DD or ISIS III
    interleave:  interleaveInfo     optional non standard interleave info taken from ISIS.LAB.
Rarely needed skew: skewInfo                  inter track skew - not currently used os:
operatingSystem     operating system on disk. NONE or ISIS ver, PDS ver, OSIRIS ver crlf: Up to 2
chars used in ISIS.LAB allows char as #xx hex value marker for start of files Files: List of files
to add to the image in ISIS.DIR order. Comment lines starting with # are ignored each line is
formated as IsisName,attributes,len,checksum,srcLocation where isisName is the name used in the
ISIS.DIR attibutes are the file's attributes any of FISW checksum is the file's SHA1 checksum
    location is where the file is stored or a special marker as follows
    AUTO - file is auto generated and the line is optional
    ZERO - file has zero length and is auto generated
    ZEROHDR - file has zero length but header is allocated eofcnt will be set to 128
    path - location of file to load a leading ^ is replaced by the repository path
 *
*/

#include "mkIsisDisk.h"
#include "utility.h"
#include <stdarg.h>
#include <string.h>

#define MAXFORMAT 15 // e.g ISIS II SD
#define MAXOS     15 // ISIS III(n) 2.0
#define MAXYEAR   30 // ideally only one year but allow for longer
#define MAXPN     64 // maximum part number/name

#ifdef _WIN32
#define DIRSEP "/\\"
#else
#define DIRSEP "/"
#endif

char comment[MAXCOMMENT];
char source[MAXCOMMENT];
char path[_MAX_PATH + 1];
char *relPath = path;
char *filePath;

uint8_t diskType = NOFORMAT;
uint8_t osType   = NONE;
label_t label;
bool isisBinSeen    = false;
bool isisT0Seen     = false;
bool isisCliSeen    = false;
bool interleave     = false;
bool interTrackSkew = false;
bool addSource      = false;
int formatCh =
    -1; // format character -e sets, otherwise defaults to 0xc7 for ISIS_II and 0xe5 for ISIS_PDS
char const *forcedSkew = NULL;

char *Match(char *s, char *ref);

char *special[] = { "AUTO", "DIR", "ZERO", "ZEROHDR", NULL };

#define _DT(f, d) ((f << 1) + d)
// clang-format off
struct {
    char *format;
    int disktype;
} formatLookup[] = { { "SD", _DT(ISISU, SD) },
                     { "ISIS II SD", _DT(ISISU, SD) },
                     { "ISIS SD1", _DT(ISIS1, SD) },
                     { "ISIS SD2", _DT(ISIS2, SD) },
                     { "ISIS SD3", _DT(ISIS3, SD) },
                     { "ISIS SD4", _DT(ISIS4, SD) },
                     { "DD", _DT(ISISU, DD) },
                     { "ISIS II DD", _DT(ISISU, DD) },
                     { "ISIS DD3", _DT(ISIS3, DD) },
                     { "ISIS DD4", _DT(ISIS4, DD) },
                     { "PDS", _DT(ISISP, DD) },
                     { "ISIS PDS", _DT(ISISP, DD) },
                     { NULL, NOFORMAT }
                };
#undef _DT

struct {
    char *os;
    uint8_t osType;
} osLookup[]    = { {"NONE", NONE}, // 0 always NONE
                    {"UNKNOWN", UNKNOWN}, // 1 always UNKNOWN
                    {"ISIS I 1.1", I11},
                    {"ISIS II 2.2", II22},
                    {"ISIS II 3.4", II34},
                    {"ISIS II 4.0", II40},
                    {"ISIS II 4.1", II41},
                    {"ISIS II 4.2", II42},
                    {"ISIS II 4.2w", II42W},
                    {"ISIS II 4.3", II43},
                    {"ISIS II 4.3w", II43W},
                    {"PDS 1.0", PDS10}, // update function chkCompatible if more pds variants added
                    {"PDS 1.1", PDS11},
                    {"TEST 1.0", TEST10},
                    {"TEST 1.1", TEST11},
                    {"ISIS III(N) 2.0", III20},
                    {"ISIS III(N) 2.2", III22},
                    {"ISIS 1.1", I11},
                    {"ISIS 2.2", II22},
                    {"ISIS 3.4", II34},
                    {"ISIS 4.0", II40},
                    {"ISIS 4.1", II41},
                    {"ISIS 4.2", II42},
                    {"ISIS 4.2w", II42W},
                    {"ISIS 4.3", II43},
                    {"ISIS 4.3w", II43W},
                    {"CORRUPT", NONE},
                    {NULL, NONE }
                };

osMap_t osMap[] = {
    {"non", NULL, 0},
    {"UNKNOWN", "", 0}, // will pick up from user files
    {"ISIS I 1.1", "Intel80/isis_i/1.1/", USESWP},
    {"ISIS II 2.2", "Intel80/isis_ii/2.2/", USESWP},
    {"ISIS II 3.4", "Intel80/isis_ii/3.4/", 0},
    {"ISIS II 4.0", "Intel80/isis_ii/4.0/", 0},
    {"ISIS II 4.1", "Intel80/isis_ii/4.1/", 0},
    {"ISIS II 4.2", "Intel80/isis_ii/4.2/", HASOV0},
    {"ISIS II 4.2w", "Intel80/isis_ii/4.2w/", HASOV0},
    {"ISIS II 4.3", "Intel80/isis_ii/4.3/", HASOV0},
    {"ISIS II 4.3w", "Intel80/isis_ii/4.3w/", HASOV0},
    {"ISIS PDS 1.0", "Intel80/pds/1.0/", 0},
    {"ISIS PDS 1.1", "Intel80/pds/1.1/", 0},
    {"TEST 1.0", "Intel80/confid/1.0/", 0},
    {"TEST 1.1", "Intel80/confid/1.1/", 0},
    {"ISIS III(N) 2.0", "Intel80/isis_iii(n)/2.0/", HASOV0 + HASOV1},
    {"ISIS III(N) 2.2", "Intel80/isis_iii(n)/2.2/", HASOV0 + HASOV1},

};
// clang-format on

char const help[] = {
    "Usage: %s [options]* recipe [diskname][.fmt]\n"
    "Where\n"
    "recipe     file with instructions to build image.\n"
    "diskname   generated disk image name - defaults to recipe without leading @\n"
    "fmt        disk image format - defaults to " EXT "\n"
    "\nAnd options are\n"
    "  -s         add source: info to imd images\n"
    "  -f[=nn]    override C7 format byte with E5 or hex value specified by nn\n"

    "  -i[=xyz]   apply interleave. xyz forces the interleave for track 0, 1, 2-76 for ISIS disks\n"
    "             x,y & z are as per ISIS.LAB i.e. interleave + '0'\n"
    "  -t         apply inter track skew\n"
    "Note for .img files interleave and skew are ignored\n"
};

int getOsIndex(char *osname) {
    if (!*osname)
        return 0;
    for (int i = 0; osLookup[i].os; i++)
        if (Match(osname, osLookup[i].os)) // look up location
            return i;                      // return the index
    return 1;                              // not standard treat as UNKNOWN
}

int getFormatIndex(char *format) {
    for (int i = 0; formatLookup[i].format; i++)
        if (Match(format, formatLookup[i].format))
            return i;
    return -1;
}

void InitFmtTable(uint8_t t0Interleave, uint8_t t1Interleave, uint8_t interleave) {
    label.fmtTable[0] = t0Interleave + '0';
    label.fmtTable[1] = t1Interleave + '0';
    for (int i = 2; i < I2TRACKS; i++)
        label.fmtTable[i] = interleave + '0';
}

char *Match(char *s, char *ref) {
    while (*ref && toupper(*s) == toupper(*ref)) {
        if (isblank(*ref))
            while (isblank(*++s))
                s++;
        else
            s++;
        ref++;
    }
    if (*ref || (*s && !isblank(*s)))
        return NULL;
    while (isblank(*s))
        s++;
    return s;
}

bool checkSkew(char const *s) { // check skew is valid
    for (int i = 0; i < 3; i++)
        if (s[i] < '1' || '0' + 52 < s[i])
            return false;
    return true;
}

void decodePair(char *dest, char *src) {
    for (int i = 0; i < 2 && (isalnum(*src) || *src == '.' || *src == '#'); i++) {
        if (*src == '#') {
            ++src;
            if (isxdigit(src[0]) && isxdigit(src[1])) {
                *dest = (isdigit(*src) ? *src++ - '0' : toupper(*src++) - 'A' + 10) << 4;
                *dest++ += (isdigit(*src) ? *src++ - '0' : toupper(*src++) - 'A' + 10);
            } else {
                warn("invalid hex number #%.2s", src);
                break;
            }
        } else
            *dest++ = toupper(*src++);
    }
}

void append(char *dst, char const *s) {
    if (strlen(dst) + strlen(s) < MAXCOMMENT) // ignore if comment too long
        strcat(dst, s);
}

unsigned getDiskType(int formatIndex, int osIndex) { // returns usable diskType
    unsigned osType  = osLookup[osIndex].osType;
    unsigned diskFmt = Format(formatLookup[formatIndex].disktype);
    unsigned density = Density(formatLookup[formatIndex].disktype);

    if (osType == NONE || osType == UNKNOWN) {
        if (diskFmt == ISISU) {
            if (*label.fmtTable) {  // recipe specified interleave
                if (density == 0) { // SD, does it look like ISIS I or ISIS II 2.2 interleave
                    if (strncmp((char *)label.fmtTable, "1<3", 3) == 0)
                        diskFmt = ISIS2; // could be ISIS1 but the same format anyway
                } else
                    diskFmt = strncmp((char *)label.fmtTable, "1H5", 3) == 0 ? ISIS3 : ISIS4;
            }
            if (diskFmt == ISISU)
                diskFmt = label.version[0] == '3' ? ISIS3 : ISIS4;
            return (diskFmt << 1) | density; // update generic disk type
        }
    }
    if (osType != NONE &&
        ((diskFmt == ISISP) ^ (osType == PDS10 || osType == PDS11))) // PDS must not have non PDS os
        return NOFORMAT;
    if ((diskFmt & 1) && (osType == I11 || osType == II22)) // os versions don't support DD
        return NOFORMAT;

    if ((diskFmt == ISIS1 && osType != I11) || (diskFmt == ISIS2 && osType != II22) ||
        (diskFmt == ISIS3 && osType != II34) || (diskFmt == ISIS4 && osType < II40)) {
        warn("non standard format %s with os %s", formatLookup[formatIndex].format,
             osLookup[osIndex].os);
    }
    if (diskFmt == ISISU) {
        switch (osType) {
        case I11:
            return ISIS1 << 1;
        case II22:
            return ISIS2 << 1;
        case II34:
            return (ISIS3 << 1) | density;
        default:
            return (ISIS4 << 1) | density;
        }
    }
    return formatLookup[formatIndex].disktype;
}

void ParseRecipeHeader(FILE *fp) {
    char line[MAXLINE];
    char *s;
    int c;
    char *appendTo  = comment;
    int formatIndex = -1;
    int osIndex     = NONE;

    while (fgets(line, MAXLINE, fp)) {
        s = line;
        if (*s == '\n')
            continue;
        if (*s == '#') {
            if (appendTo)
                append(appendTo, ++s);
            continue;
        }
        appendTo = NULL;

        if ((s = strchr(line, '\n'))) {
            while (--s != line && (isblank(*s) || *s == '\r')) // trim
                ;
            s[1] = 0;
        } else if (strlen(line) == MAXLINE - 1) {
            warn("line too long %s", line);
            while ((c = getc(fp)) != EOF && c != '\n')
                ;
            if (c == EOF)
                break;
            else
                continue;
        }
        if (Match(line, "Files:"))
            break;
        if (Match(line, "#"))
            continue;
        if ((s = Match(line, "label:"))) {
            for (int i = 0; i < 6; i++)
                if (isalnum(*s))
                    label.name[i] = toupper(*s++);
            if (*s == '.' || *s == '-')
                s++;
            for (int i = 6; i < 9; i++)
                if (isalnum(*s))
                    label.name[i] = toupper(*s++);
        } else if ((s = Match(line, "version:")))
            decodePair((char *)label.version, s);
        else if ((s = Match(line, "format:"))) {
            if ((formatIndex = getFormatIndex(s)) < 0)
                fatal("Unsupported disk format %s", s);

        } else if ((s = Match(line, "interleave:"))) { // we have non standard interleave
            if ('1' <= s[0] && s[0] <= 52 + '0' && '1' <= s[1] && s[1] <= 52 + '0' && '1' <= s[2] &&
                s[2] <= 52 + '0')
                InitFmtTable(s[0] - '0', s[1] - '0', s[2] - '0');
        } else if ((s = Match(line, "os:")))
            osIndex = getOsIndex(s);
        else if ((s = Match(line, "crlf:")))
            decodePair((char *)label.crlf, s);
        else if (((s = Match(line, "source:"))) && addSource) {
            append(source, "\n");
            append(source, line);
            append(source, "\n");
            appendTo = source;
        }
    }
    if (formatIndex < 0)
        fatal("No format specified - format: is mandatory");

    if ((diskType = getDiskType(formatIndex, osIndex)) == NOFORMAT)
        fatal("Invalid disk format %s for os %s", formatLookup[formatIndex].format,
              osLookup[osIndex].os);

    osType = osLookup[osIndex].osType;
    char fmt[128]; // enough string space
    if (osIndex > 0)
        sprintf(fmt, "\n%s System Disk - format %s\n", osLookup[osIndex].os,
                formatLookup[formatIndex].format);
    else
        sprintf(fmt, "\nNon-System Disk - format %s\n", formatLookup[formatIndex].format);
    append(comment, fmt);
    append(comment, source);
}

char *ParseIsisName(char *isisName, char *src) {
    while (isblank(*src)) // ignore leading blanks
        src++;
    for (int i = 0; i < 6 && isalnum(*src); i++)
        *isisName++ = *src++;
    if (*src == '.') {
        *isisName++ = toupper(*src++);
        for (int i = 0; i < 3 && isalnum(*src); i++)
            *isisName++ = toupper(*src++);
    }
    *isisName = '\0';
    return src;
}

char *ParseAttributes(char *src, int *attributep) {
    int attributes = -1;
    while (*src && *src != ',') {
        if (attributes < 0)
            attributes = 0;
        switch (toupper(*src)) {
        case 'I':
            attributes |= INV;
            break;
        case 'W':
            attributes |= WP;
            break;
        case 'S':
            attributes |= SYS;
            break;
        case 'F':
            attributes |= FMT;
            break;
        case ' ':
            break;
        default:
            warn("bad attribute %c", *src);
        }
        src++;
    }
    *attributep = attributes;
    return src;
}

// the ParsePath return codes
enum { PATHOK = 1, DIRLIST = 2, IGNORE = 3, TOOLONG = 4 };

void ParsePath(char *src) {
    char *s;
    static bool warnRepo = true;

    while (isblank(*src)) // remove any leading space;
        src++;
    if ((s = strchr(src, ','))) // remove any additional field
        *s = 0;

    s = strchr(src, '\0'); // remove trailing spaces
    while (s > src && isblank(s[-1]))
        *--s = 0;

    if (!*src) {
        filePath = "*missing path";
        return;
    }

    for (int i = 0; special[i]; i++) {
        if (strcmp(src, special[i]) == 0) {
            filePath = src;
            return;
        }
    }

    if (*src != '^') {
        filePath = src;
        return;
    }

    // repository file
    if (relPath == path && warnRepo) {
        warn("IFILEREPO not defined assuming local directory\n");
        warnRepo = false;
    }
    src++;
    *relPath = 0;
    if ((relPath - path) + strlen(src) >= _MAX_PATH) {
        filePath = "*path too long";
        return;
    }
    strcpy(relPath, src);
    filePath = path;
}

void chkSystem() {
    if (!isisT0Seen) {
        if ((diskType >> 1) == ISISP)
            fatal("PDS System disk requires ISIS.T0");
        else if (osType > 0)
            warn("System disk requires ISIS.T0 - using Non-System boot file");
    }
    if (osType > 0) {
        if (!isisBinSeen)
            fatal("System disk requires %s", (diskType >> 1) == ISISP ? "ISIS.PDS" : "ISIS.BIN");
        if (!isisCliSeen)
            fatal("System disk requires ISIS.CLI");
    }
}

void GenIsisName(char *isisName, char *line) {
    char *s;
    char *fname = (s = strrchr(line, ',')) ? s + 1 : line;
    if (strcmp(fname, "AUTO") == 0 || strcmp(fname, "ZERO") == 0) {
        *isisName = 0;
        return;
    }
    if ((s = strrchr(fname, '/')))
        fname = s + 1;
    if ((s = strrchr(fname, '\\')))
        fname = s + 1;
    if (*fname == '^')
        fname++;
    ParseIsisName(isisName, fname);               // generate the ISIS name
    if ((s = strchr(isisName, '.')) && s[1] == 0) // don't auto generate name ending in  .
        *s = 0;
}

char *skipws(char *s) {
    while (isblank(*s))
        s++;
    return s;
}

bool isIsisName(const char *name) {
    int i;
    for (i = 0; i < 6 && isalnum(*name); i++)
        name++;
    if (i && *name == '.') {
        name++;
        for (i = 0; i < 3 && isalnum(*name); i++)
            name++;
    }
    return i && *name == 0;
}
void ParseFiles(FILE *fp) {
    char line[MAXLINE];
    char isisName[11];
    int attributes;
    char *s;
    int c;

    while (fgets(line, MAXLINE, fp)) {
        if (line[0] == '#')
            continue;
        if (!strchr(line, '\n')) { // truncated line
            while ((c = getc(fp)) != '\n' && c != EOF)
                ;
            if (!strchr(line, '#')) { // don't worry about extended comments
                warn("%s - line truncated", line);
                continue;
            }
        }
        if ((s = strchr(line, '#')) || (s = strchr(line, '\n'))) { // trim trailing blanks
            while (--s != line && (*s == '\r' || isblank(*s)))
                ;
            s[1] = 0;
        }

        s = skipws(line);
        if (*s == 0)
            continue;

        if (*s == ',' ||
            !strchr(s, ',')) // null ISIS name or no ISIS name, so generate from location
            GenIsisName(isisName, s);
        else {
            s = ParseIsisName(isisName, s); // get the isisFile
            if (*s != ',')
                *isisName = 0; // force error message
        }
        if (!isIsisName(isisName)) {
            warn("%s - Invalid ISIS name", line);
            continue;
        }
        if (strchr(s + 1, ',')) { // attribute present or only location
            s = ParseAttributes(s + 1, &attributes);
            if (*s != ',') {
                warn("%s - Invalid Attributes", line);
                continue;
            }
        } else
            attributes = 0;

        if (*s == ',' && strchr(s + 1, ',')) // checksum present or only location
            s = strchr(s + 1, ',') + 1;

        ParsePath(s);
        if (*filePath == '*') {
            warn("%s - skipped because %s", line, filePath + 1);
            continue;
        } else if (strcmp(isisName, "ISIS.BIN") == 0) {
            if (!osType || (diskType >> 1) == ISISP) {
                warn("ISIS.BIN not allowed for %s system disk - skipping", osMap[osType].osname);
                continue;
            }
            isisBinSeen = true;
        } else if (strcmp(isisName, "ISIS.PDS") == 0) {
            if (!osType || (diskType >> 1) != ISISP) {
                fprintf(stderr, "ISIS.PDS not allowed for %s system disk - skipping\n",
                        osMap[osType].osname);
                continue;
            } else if (strcmp(filePath, "AUTO") != 0)
                isisBinSeen = true;
        } else if (strcmp(isisName, "ISIS.T0") == 0 && strcmp(filePath, "AUTO") != 0)
            isisT0Seen = true;
        else if (strcmp(isisName, "ISIS.CLI") == 0 && strcmp(filePath, "AUTO") != 0)
            isisCliSeen = true;
        if (!CopyFile(isisName, attributes))
            fprintf(stderr, "%s can't find source file %s\n", isisName, filePath);
    }
    chkSystem();
}

int main(int argc, char **argv) {
    FILE *fp;
    char *recipe;
    char *outfile;
    char *s;
    char *diskname;

    while (getopt(argc, argv, "hsf=i=t") != EOF) {
        switch (optopt) {
        case 'h':
            usage(NULL);

        case 'i':
            interleave = true;
            if (optarg) {
                if (checkSkew(optarg))
                    forcedSkew = optarg;
                else
                    warn("Ignoring invalid skew specification in -i option");
            }
            break;
        case 't':
            interTrackSkew = true;
            break;
        case 'f':
            if (optarg) {
                char *endPtr;
                formatCh = (int)strtoul(optarg, &endPtr, 16);
                if (endPtr == optarg || *endPtr || formatCh > 0xff) {
                    warn("Invalid numeric value '%s' for -f  option", optarg);
                    formatCh = ALT_FMTBYTE;
                }
            } else
                formatCh = ALT_FMTBYTE;
            break;
        case 's':
            addSource = true;
            break;
        }
    }
    if (optind < argc)
        recipe = argv[optind++];
    else
        usage("Missing recipe file name");

    diskname = optind < argc ? argv[optind++] : EXT;
    if (optind < argc)
        usage("Unexpected argument after diskname");

    if (*diskname == '.' && !strchr(diskname + 1, '.') && diskname == basename(diskname)) // simple .ext
        outfile = makeFilename(*recipe == '@' ? recipe + 1 : recipe, diskname, true);
    else
        outfile = makeFilename(diskname, EXT, false);


    // use environment variable to define root
    if ((s = getenv("IFILEREPO"))) {     // environment variable defined
        strncat(path, s, _MAX_PATH - 1); // path too long will be detected later
        for (s = path; *s; s++)
            if (*s == '\\')
                *s = '/';
        if (s > path && s[-1] != '/')
            *s++ = '/';
        *s      = 0;
        relPath = s; // where we add relative path
    }

    if ((fp = fopen(recipe, "rt")) == NULL)
        fatal("Cannot open %s", recipe);

    ParseRecipeHeader(fp);
    FormatDisk(diskType, formatCh);
    if (Format(diskType) != ISISP)
        WriteI2Directory();
    else
        WriteI3Directory();
    WriteLabel();
    ParseFiles(fp);
    fclose(fp);
    WriteImgFile(outfile, diskType, interleave ? forcedSkew ? forcedSkew : "" : NULL,
                 interTrackSkew, comment);
}
