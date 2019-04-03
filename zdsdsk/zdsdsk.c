#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <memory.h>
#include <ctype.h>
#include "flux.h"
#include "zip.h"

int debug = 0;      // debug level
int histLevels = 0;
bool showSectorMap = false;
char curFile[_MAX_PATH + _MAX_FNAME + _MAX_EXT + 3];    // core file name + possibly [zip file entry]
/*
 * Display error message and exit
 */
void error(char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    fprintf(stderr, "%s - ", curFile);
    vfprintf(stderr, fmt, args);
    exit(1);
}

void logger(int level, char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    if (debug >= level) {
        printf("%s - ", curFile);
        vprintf(fmt, args);
    }
}

_declspec(noreturn) void usage() {
    error("usage: idd2imd [-d[n]] -h[n] [zipfile|rawfile]+\n"
        "-d sets debug. n is optional level 0,1,2,3\n"
        "-h displays flux histogram. n is optional number of levels\n"
        "-s display sector mapping\n"
        "No IMD file is created for non zip files\n");
}


bool loadFile(char *name, bool warnNoOpen) {
    size_t bufsize;
    byte *buf;
    FILE *fp;
    bool ok = false;;

    if ((fp = fopen(name, "rb")) == NULL) {
        if (warnNoOpen)
            logger(ALWAYS, "Can't open\n");
        return false;
    }

    fseek(fp, 0, SEEK_END);
    bufsize = ftell(fp);
    rewind(fp);

    buf = (byte *)xalloc(NULL, bufsize);
    if (fread(buf, bufsize, 1, fp) != 1)
        logger(ALWAYS, "Failed to load\n");
    else {
        readFluxBuffer(buf, bufsize);         // load in the flux data from buffer extracted from zip file
        ok = true;
    }
    free(buf);
    fclose(fp);
    return ok;
}

bool loadZipFile(struct zip_t *zip) {
    size_t bufsize;
    byte *buf;
    const char *name = zip_entry_name(zip);
    char *s;

    if (zip_entry_isdir(zip))
        return false;

    if (!(s = strrchr(name, '.')) || _stricmp(s, ".raw") != 0)
        return false;
    
    bufsize = (size_t)zip_entry_size(zip);
    buf = (byte *)xalloc(NULL, bufsize);
    memset(buf, 0, bufsize);
    if (zip_entry_noallocread(zip, (void *)buf, bufsize) < 0) {
        logger(ALWAYS, "Failed to load\n");
        free(buf);
        return false;
    }
    readFluxBuffer(buf, bufsize);         // load in the flux data from buffer extracted from zip file
    free(buf);
    return true;
}


void decodeFile(char *name) {

    char dir[_MAX_DIR];
    char drive[_MAX_DRIVE];
    char fname[_MAX_FNAME];
    char ext[_MAX_EXT];
    char filename[_MAX_PATH];
    int cntRaw = 0;

    _splitpath_s(name, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);

    if (_stricmp(ext, ".raw") == 0) {
        sprintf(curFile, "%s%s", fname, ext);
        if (!loadFile(name, true))
            return;
         flux2track();
         if (histLevels)
            displayHist(histLevels);
         cntRaw = 1;
    }
    else if (_stricmp(ext, ".zip") == 0) {
        struct zip_t *zip;
        if ((zip = zip_open(name, 0, 'r')) == NULL)
            logger(ALWAYS, "Can't open\n");
        else {
            int n = zip_total_entries(zip);
            for (int i = 0; i < n; i++) {
                zip_entry_openbyindex(zip, i);
                sprintf(curFile, "%s%s[%s]", fname, ext, zip_entry_name(zip));
				printf("%s\n", curFile);
                if (loadZipFile(zip)) {
                    cntRaw++;
                    flux2track();
                    if (histLevels)
                        displayHist(histLevels);
                }
                zip_entry_close(zip);
            }
            zip_close(zip);
        }
    }
    else if (!*ext) {       // use name as a prefix
        for (int cyl = 0; cyl < MAXCYLINDER; cyl++)
            for (int head = 0; head < 2; head++) {
                sprintf(curFile, "%s%02d.%1d.raw", fname, cyl, head);
                _makepath_s(filename, _MAX_PATH, drive, dir, curFile, NULL);
                if (loadFile(filename, false)) {
                    cntRaw++;
                    flux2track();
                    if (histLevels)
                        displayHist(histLevels);
                }
            }
    }
    else
        usage();

    sprintf(curFile, "%s%s", fname, *ext ? ext : "*.raw");

}





int main(int argc, char **argv) {
    int arg;
    int optCnt, optVal;
    char optCh;

    for (arg = 1; arg < argc && (optCnt = sscanf(argv[arg], "-%c%d", &optCh, &optVal)); arg++) {
        switch (tolower(optCh)) {
        case 'd':
            debug = optCnt == 2 ? optVal : MINIMAL; break;
        case 'h':
            histLevels = (optCnt == 2 && optVal > 5) ? optVal : 10; break;
        case 's':
            showSectorMap = true; break;
        default:
            usage();
        }
    }
    if (arg == argc)
        usage();

    for (; arg < argc; arg++)
        decodeFile(argv[arg]);
    return 0;
}