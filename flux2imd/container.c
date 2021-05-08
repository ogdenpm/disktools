#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "flux2imd.h"
#include "util.h"
#include "zip.h"
#include "flux.h"

#ifdef _MSC_VER
#define stricmp _stricmp
#endif

static TrackId trackId;

typedef struct {
    bool (*open)(const char *fname);
    TrackId *(*load)();
    bool (*close)();
} IOFunc;



static bool rawOpen(const char *fname);
static TrackId *rawLoad();
static bool rawClose();
static bool zipOpen(const char *fname);
static TrackId *zipLoad();
static bool zipClose();
bool scpOpen(const char *fname);
TrackId *scpLoad();
bool scpClose();
static bool errOpen(const char *fname);

static TrackId *getTrackId(const char *fname);


static IOFunc io = { NULL, NULL, NULL };




static const IOFunc rawFuncs = {&rawOpen, &rawLoad, &rawClose};
static const IOFunc zipFuncs = {&zipOpen, &zipLoad, &zipClose};
static const IOFunc scpFuncs = {&errOpen, NULL, NULL};
static const IOFunc errFuncs = {&errOpen, NULL, NULL };



bool openFluxFile(const char *fname) {
    const char *s;

    if (io.close) {
        io.close();
        io = errFuncs;
    }
    setLogPrefix(fname, NULL);
    createLogFile(NULL);            // revert to stdout for general errors

    if (s = strrchr(fname, '.')) {
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

    bool isOk =  io.open(fname);
    if (!isOk)
        io = errFuncs;
    return isOk;
}


TrackId *loadFluxStream() {
    return io.load ? io.load() : NULL;
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
    return false;
}



// data variables for raw files
static FILE *rawfp;
static bool rawEof = false;
static const char *rawName;
static size_t rawBufSize = 0;
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

static TrackId *rawLoad() {
    if (rawEof)
        return NULL;
    rawEof = true;                       // only one attempt at loading

    // work out size of file to load
    fseek(rawfp, 0, SEEK_END);
    rawBufSize = ftell(rawfp);
    rewind(rawfp);

    rawBuf = (uint8_t *)xmalloc(rawBufSize);
    if (fread(rawBuf, rawBufSize, 1, rawfp) != 1) {
        logFull(D_ERROR, "Failed to load file\n");
        return NULL;
    }
    loadFlux(rawBuf, rawBufSize);         // load in the flux data from buffer extracted from zip file
    return getTrackId(rawName);
}

static bool rawClose() {
    fclose(rawfp);
    free(rawBuf);
    return true;
}


// data variables for zip files
static struct zip_t *zip;
static bool zipEof = true;
static int zipReadCnt = 0;
static int zipEntriesCnt = 0;
static const char *zipName;
static size_t zipBufSize = 0;
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


static TrackId *zipLoad() {
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
            zipBufSize = (size_t)zip_entry_size(zip);
            zipBuf = (uint8_t *)xmalloc(zipBufSize);
            memset(zipBuf, 0, zipBufSize);
            if (zip_entry_noallocread(zip, (void *)zipBuf, zipBufSize) < 0)
                logFull(D_ERROR, "Failed to load\n");
            else {
                loadFlux(zipBuf, zipBufSize);         // load in the flux data from buffer extracted from zip file
                return getTrackId(entryName);
            }

        }
    }
    return NULL;
}





static bool zipClose() {
    zip_close(zip);
    free(zipBuf);
    return true;

}


static TrackId *getTrackId(const char *fname) {

    char *s = strrchr(fname, '\0') - 8;

    if (s < fname|| sscanf(s, "%2d.%1d.", &trackId.cyl, &trackId.head) != 2)
        trackId.cyl = trackId.head = -1;
    return &trackId;
}
