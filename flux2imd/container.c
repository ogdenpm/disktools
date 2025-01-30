#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "flux2imd.h"
#include "util.h"
#include "zip.h"
#include "flux.h"
#include "stdflux.h"

#ifdef _MSC_VER
#define stricmp _stricmp
#endif

typedef struct {
    bool (*open)(const char *fname);
    bool (*load)();
    bool (*close)();
} IOFunc;

static bool rawOpen(const char *fname);
static bool rawLoad();
static bool rawClose();
static bool zipOpen(const char *fname);
static bool zipLoad();
static bool zipClose();
bool scpOpen(const char *fname);
bool scpLoad();
bool scpClose();
static bool errOpen(const char *fname);
static bool updateCylHead(const char *name);


static IOFunc io = { NULL, NULL, NULL };

static const IOFunc rawFuncs = { &rawOpen, &rawLoad, &rawClose };
static const IOFunc zipFuncs = { &zipOpen, &zipLoad, &zipClose };
static const IOFunc scpFuncs = { &scpOpen, &scpLoad, &scpClose};
static const IOFunc errFuncs = { &errOpen, NULL, NULL };



bool openFluxFile(const char *fname) {
    const char *s;

    if (io.close)
        io.close();
    io = errFuncs;
    setLogPrefix(fname, NULL);
    createLogFile(NULL);            // revert to stdout for general errors

    if ((s = strrchr(fname, '.'))) {
        if (stricmp(s, ".raw") == 0)
            io = rawFuncs;
        else if (stricmp(s, ".zip") == 0)
            io = zipFuncs;
        else if (stricmp(s, ".scp") == 0)
            io = scpFuncs;
        else
            logFull(D_WARNING, "unsupported file type %s\n", s);
    } else
        logFull(D_WARNING, "missing extent\n");

    bool isOk = io.open(fname);
    if (!isOk)
        io = errFuncs;
    return isOk;
}


bool loadFluxStream() {
    if (io.load && io.load()) {
        getCellWidth();
        return true;
    }
    return false;
}

bool closeFluxFile() {
    bool result = io.close ? io.close() : true;
    io = errFuncs;
    createLogFile(NULL);
    setLogPrefix("", NULL);
    return result;
}

// handler for invalid files
static bool errOpen(const char *fname) {
    (void)fname;
    return false;
}

// data variables for raw files
static FILE *rawfp;
static bool rawEof = false;
static const char *rawName;
static uint32_t rawBufSize = 0;
static uint8_t *rawBuf;

static bool rawOpen(const char *fname) {
    rawName = fname;
    if ((rawfp = fopen(fname, "rb")) == NULL)
        logFull(D_WARNING, "Cannot open .raw file\n");
    else
        createLogFile(fname);
    rawEof = false;
    return rawfp != NULL;
}

static bool rawLoad() {
    if (rawEof)
        return false;
    rawEof = true;                       // only one attempt at loading

    // work out size of file to load
    fseek(rawfp, 0, SEEK_END);
    rawBufSize = ftell(rawfp);
    rewind(rawfp);

    rawBuf = (uint8_t *)xmalloc(rawBufSize);
    if (fread(rawBuf, rawBufSize, 1, rawfp) != 1) {
        logFull(D_ERROR, "Failed to load file\n");
        return false;
    }
    return loadKryoFlux(rawBuf, rawBufSize) && updateCylHead(rawName);         // load in the flux data from buffer extracted from zip file

}

static bool rawClose() {
    fclose(rawfp);
    free(rawBuf);
    return true;
}

// data variables for zip files
static struct zip_t *zip;
static int zipReadCnt = 0;
static int zipEntriesCnt = 0;
static const char *zipName;
static uint32_t zipBufSize = 0;
static uint8_t *zipBuf;

static bool zipOpen(const char *fname) {
    if ((zip = zip_open(fname, 0, 'r')) == NULL)
        logFull(D_WARNING, "Cannot open .zip file\n");
    else if ((zipEntriesCnt = zip_total_entries(zip)) == 0) {
        logFull(D_WARNING, "No content\n");
        zip_close(zip);
        zip = NULL;
    } else {
        zipName = fname;
        createLogFile(fname);           // we have some content so create a log file for it
        zipReadCnt = 0;
    }
    return zip != 0;
}

static bool zipLoad() {
    while (zipReadCnt < zipEntriesCnt) {
        if (zipBuf) {
            free(zipBuf);
            zipBuf = NULL;
        }
        zip_entry_openbyindex(zip, zipReadCnt++);
        if (zip_entry_isdir(zip))
            continue;
        const char *entryName = zip_entry_name(zip);
        setLogPrefix(zipName, entryName);
        if (!extMatch(entryName, ".raw"))
            logFull(ALWAYS, "Skipping as non .raw file\n");
        else {
            zipBufSize = (uint32_t)zip_entry_size(zip); // breaks if zip files entries > 4G !!!!
            zipBuf = (uint8_t *)xmalloc(zipBufSize);
            memset(zipBuf, 0, zipBufSize);
            if (zip_entry_noallocread(zip, (void *)zipBuf, zipBufSize) < 0)
                logFull(D_ERROR, "Failed to load\n");
            else if (loadKryoFlux(zipBuf, zipBufSize))         // load in the flux data from buffer extracted from zip file
                return updateCylHead(entryName);
        }
    }
    return false;
}

static bool zipClose() {
    zip_close(zip);
    free(zipBuf);
    return true;
}

static bool updateCylHead(const char *name) {
    int cyl, head;
    char *s = strrchr(name, '\0') - 8;

    if (s >= name && sscanf(s, "%2d.%1d.", &cyl, &head) == 2)
        setCylHead(cyl, head);
    return true;        // simplifies use after loadKryoFlux
}
