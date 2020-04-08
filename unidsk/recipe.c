/* recipe.c     (c) by Mark Ogden 2018

DESCRIPTION
    Generates the recipe file for ISIS II and ISISI III disks.
    Part of unidsk

MODIFICATION HISTORY
    17 Aug 2018 -- original release as unidsk onto github
    18 Aug 2018 -- added attempted extraction of deleted files for ISIS II/III
    19 Aug 2018 -- Changed to use environment variable IFILEREPO for location of
                   file repository. Changed location name to use ^ prefix for
                   repository based files, removing need for ./ prefix for
                   local files
    20 Aug 2018 -- replaced len, checksum model with full sha1 checksum
                   saves files with lower case names to avoid conflict with
                   special paths e.g. AUTO, ZERO, DIR which became a potential
                   issue when ./ prefix was removed. Lower case is also compatible
                   with the thames emulator and the repository file names
    21 Aug 2018 -- Added support for auto looking up files in the repository
                   the new option -l or -L supresses the lookup i.e. local fiiles
    13 Sep 2018 -- Renamed skew to interleave to align with normal terminolgy
                   skew is kept as an option but not currently used
    15-Oct 2019 -- Added crlf recipe line for junk fill in ISIS III disk
                   Modified version and crlf recipes to accept #nn for hex value
                   as ISIS PDS does not explicitly fill these so this allows
                   arbitary data to be written

TODO
    Review the information generated for an ISIS IV disk to see if it is sufficient
    to allow recreation of the original .imd or .img file
    Review the information generated for an ISIS III disk to see it is sufficient to
    recreate the ISIS.LAB and ISIS.FRE files.

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
    label: name[.|-]ext             Used in ISIS.LAB name has max 6 chars, ext has max 3 chars
    version: nn                     Up to 2 chars used in ISIS.LAB (extended to allow char to have #nn hex value)
    format: diskFormat              ISIS II SD, ISIS II DD or ISIS III
    interleave:  interleaveInfo     optional non standard interleave info taken from ISIS.LAB. Rarely needed
    skew: skewInfo                  inter track skew - not currently used
    os: operatingSystem             operating system on disk. NONE or ISIS ver, PDS ver, OSIRIS ver
    crlf: nn                        Up to 2 chars used in ISIS.LAB Each char can be #nn hex value
marker for start of files
    Files:
List of files to add to the image in ISIS.DIR order. Comment lines starting with # are ignored
each line is formated as
    IsisName,attributes,len,checksum,srcLocation
where
    isisName is the name used in the ISIS.DIR
    attibutes are the file's attributes any of FISW
    sha1 checksum of the file stored in base64
    location is where the file is stored or a special marker as follows
    AUTO - file is auto generated and the line is optional
    DIR - file was a listing file and not saved - depreciated
    ZERO - file has zero length and is auto generated
    ZEROHDR - file has zero length but header is allocated eofcnt will be set to 128
    *text - problems with the file the text explains the problem. It is ignored
    path - location of file to load a leading ^ is replaced by the repository path

if you haave ISIS files files named AUTO, DIR or ZERO please prefix the path with ./, or use lower case
for local files.

Note unidsk creates special comment lines as follows

if there are problems reading a file the message below is emitted before the recipe line
    # there were errors reading filename

if there were problems reading a delete file the message
    # file filename could not be recovered
For other deleted files the recipe line is commented out and the file is extracted to a file
with a leading # prefix

If there are aliases in the repository these are shown in one or more comments after the file
    # also path [path]*
If there is a match with the ISIS name then only alises with the same ISIS name will match
otherwise all files with the same checksum will be listed
The primary use of this is to change to prefered path names for human reading

*/

#include <stdio.h>
#include <stdlib.h>
#include <direct.h>
#include <string.h>
#include <ctype.h>

#include "unidsk.h"

struct {
    char label[(6 + 3) * 3 + 2];    // allow for encoded numbers
    char suffix[4];
    char version[(2 * 3) + 1];      // allow for encoded numbers
    char crlf[2 * 3 + 1];           // allow for encoded numbers
    char *format;
    char interleave[4];
    char *os;
} recipeInfo;


struct {
    char *checksum;
    char *os;
} osMap[] = {
    "/YmatRu9vhzEjMhDylDjhykvqZo", "ISIS II 2.2",
    "CDiOIW5+mmg+lMLUzBiKotg1Q58", "ISIS II 3.4",
    "sRiKeMMS6BzoByW8JMfR602LkRc", "ISIS II 4.0",
    "o3AUw0iiH+s7gzpbQng+QpJOpUU", "ISIS II 4.1",
    "sWQzN17YlQdDPy7hXQc+2Htq9lA", "ISIS II 4.2",
    "u/pkKs6Q7J2EBWs9znd6aTF7j3o", "ISIS II 4.2w",
    "IT/NSStKstpz2wMCcHbGrEj0GJM", "ISIS II 4.3",
    "oQXgd+7fmrQ/wH/Wfr9ptN4yK2s", "ISIS II 4.3w",
    "6zffO/tlxHVojxlqs2BQvu31814", "PDS 1.0",
    "kr87fI+DkuGXQec3i2IG7S+8usI", "PDS 1.1",
    "DJ5TiAs5F4yNGlp7fzUjT/k5Zsk", "TEST 1.0",
    "7SzuQtZxju9XU/+ehbFOQ7W0tz8", "TEST 1.1",
    "yOeRj3n6yo8SYlu3Ne4L8Ci52BI", "OSIRIS 3.0",
    "JnbT/FfQLj4sVO/yJLIL8tyoGUU", "ISIS III(N) 2.0",
    NULL, "NONE"
};

char *osFormat[] = { "UNKNOWN", "ISIS II SD", "ISIS II DD", "ISIS PDS", "ISIS IV" };
char *suffixFormat = "-SDP4";


void EncodePair(FILE *fp, unsigned char *s) {
    putc(' ', fp);
    for (int i = 0; i < 2; i++)
        if (isupper(*s) || isdigit(*s) || *s == '.')
            putc(*s++, fp);
        else
            fprintf(fp, "#%02X", *s++);
}

void StrEncodeNCat(char *dst, uint8_t const *src, int len) {
    static char *hexDigits = "0123456789ABCDEF";
    dst = strchr(dst, '\0');        // point to end of dst string

    while (len-- > 0 && *src) {
        if (isupper(*src) || isdigit(*src) || *src == '.')
            *dst++ = *src++;
        else {
            *dst++ = '#';
            *dst++ = hexDigits[*src / 16];
            *dst++ = hexDigits[*src++ % 16];
        }
    }
    *dst = 0;
}


void getRecipeInfo(isisDir_t *isisDir, int diskType, bool isOK) {
    FILE *lab;
    label_t label;

    // get the none label related info
    recipeInfo.format = osFormat[diskType];
    recipeInfo.suffix[0] = suffixFormat[diskType];

    if (osIdx >= 0) {
        recipeInfo.os = "UNKNOWN";       // default to unknown os
        recipeInfo.suffix[1] = 'b';      // small b if os but unknown
        for (int i = 0; osMap[i].checksum; i++)
            if (strcmp(osMap[i].checksum, isisDir[osIdx].checksum) == 0) {
                recipeInfo.os = osMap[i].os;
                recipeInfo.suffix[1] = 'B'; // yes it is bootable
                break;
            }
    } else
        recipeInfo.os = "NONE";
    if (!isOK)
        strcat(recipeInfo.suffix, "E");

    // now get the info from isis.lab
    if ((lab = fopen("isis.lab", "rb")) == 0) {
        strcpy(recipeInfo.label, "nolabel");
        return;
    }
    if (fread(&label, sizeof(label), 1, lab) != 1)
        memset(&label, 0, sizeof(label));              // just in case of partial read
    else {
        if (label.name[0]) {
            StrEncodeNCat(recipeInfo.label, label.name, 6);        // relies on labelInfo array being 0;
            if (label.name[6]) {                            // copy -ext if present
                strcat(recipeInfo.label, "-");
                StrEncodeNCat(recipeInfo.label, &label.name[6], 3);
            }
        }
        if (label.version)
            StrEncodeNCat(recipeInfo.version, label.version, 2);
        if (diskType == ISIS_PDS && (label.crlf[0] != '\r' || label.crlf[1] != '\n'))
            StrEncodeNCat(recipeInfo.crlf, label.crlf, 2);

        if (diskType == ISIS_SD && strncmp(label.fmtTable, "1<6", 3) ||  // nonstandard interleave suffix
            diskType == ISIS_DD && strncmp(label.fmtTable, "145", 3))
            strncpy(recipeInfo.interleave, label.fmtTable, 3);
    }
    fclose(lab);
}



void mkRecipe(char const *name, isisDir_t *isisDir, char *comment, int diskType, bool isOK)
{
    FILE *fp;
    char recipeName[(6+3) * 3 + 1 + 4 + 1];     // 6-3 possibly encoded - suffix '\0'
    char *dbPath;
    char *prefix;
    isisDir_t *dentry;

    getRecipeInfo(isisDir, diskType, isOK);

    sprintf(recipeName, "@%s-%s", recipeInfo.label, recipeInfo.suffix);

    if ((fp = fopen(recipeName, "wt")) == NULL) {
        fprintf(stderr, "can't create recipe file\n");
        return;
    }
    fprintf(fp, "# IN-%s\n", recipeName + 1);

    fprintf(fp, "label: %s\n", recipeInfo.label);
    if (*recipeInfo.version)
        fprintf(fp, "version: %s\n", recipeInfo.version);

    fprintf(fp, "format: %s\n", recipeInfo.format);
    fprintf(fp, "os: %s\n", recipeInfo.os);
    if (*recipeInfo.interleave)
    fprintf(fp, "interleave: %s\n", recipeInfo.interleave);
    if (*recipeInfo.crlf)
    fprintf(fp, "crlf: %s\n", recipeInfo.crlf);
    fprintf(fp, "source: %s\n", name);
    if (*comment) {
        while (*comment) {
            fputs("# ", fp);
            while (*comment && *comment != '\n')
                putc(*comment++, fp);
            if (*comment)
                putc(*comment++, fp);
        }
    }



    fprintf(fp, "Files:\n");

    for (dentry = isisDir; dentry < &isisDir[MAXDIR] && dentry->name[0]; dentry++) {
        if (dentry->errors || (dentry->name[0] == '#' && dentry->dirLen <= 0)) {
            if (dentry->name[0] == '#') {           // if the file was deleted and has errors note it and skip
                fprintf(fp, "# file %s could not be recovered\n", dentry->name + 1);
                continue;
            }
        }

        fprintf(fp, "%s,", dentry->name);
        if (dentry->attrib & 0x80) putc('F', fp);
        if (dentry->attrib & 4) putc('W', fp);
        if (dentry->attrib & 2) putc('S', fp);
        if (dentry->attrib & 1) putc('I', fp);

        prefix = "";
        if (dentry->dirLen < 0)
            sprintf(dentry->checksum, "** EOF count %d **", -dentry->dirLen);
        else if (dentry->dirLen == 0)
            strcpy(dentry->checksum, "** null **");
        else if (dentry->actLen != dentry->dirLen)
            sprintf(dentry->checksum, "** size %d **", dentry->dirLen);
        else if (dentry->errors)
            sprintf(dentry->checksum, "** corrupt **");

        if (strcmp(dentry->name, "ISIS.DIR") == 0 ||
            strcmp(dentry->name, "ISIS.LAB") == 0 ||
            strcmp(dentry->name, "ISIS.MAP") == 0 ||
            strcmp(dentry->name, "ISIS.FRE") == 0)
            dbPath = "AUTO";
        else if (dentry->dirLen == 0)
            dbPath = "ZERO";
        else if (dentry->dirLen < 0)
            dbPath = "ZEROHDR";        // zero but link block allocated
        else if (dbPath = Dblookup(dentry))
            prefix = "^";
        else if (dentry->dirLen != dentry->actLen || dentry->errors)
            dbPath = "*Corrupt - missing data";
        else
            dbPath = dentry->fname;

        fprintf(fp, ",%s,%s%s", dentry->checksum, prefix, dbPath);
        // print the alternative names if available

        int llen = MAXOUTLINE + 1;
        while ((dbPath = Dblookup(NULL)) && *dbPath) {
            if (llen + strlen(dbPath) >= MAXOUTLINE - 1)
                llen = fprintf(fp, "\n# also");
            llen += fprintf(fp, " %s", dbPath);
        }
        putc('\n', fp);
    }
    fclose(fp);
}