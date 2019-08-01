#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <memory.h>
#include <ctype.h>
#include "flux.h"



void decode();
char *curFile;

int debug = 0;      // debug level
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


bool loadFile(char *name, bool warnNoOpen) {
    size_t bufsize;
    byte *buf;
    FILE *fp;
    bool ok = false;

    curFile = name;

    if ((fp = fopen(name, "rb")) == NULL) {
        if (warnNoOpen)
            logger(ALWAYS, "Can't open\n");
        return false;
    }

    fseek(fp, 0, SEEK_END);
    bufsize = ftell(fp);
    rewind(fp);

    buf = (byte *)xmalloc(bufsize);
    if (fread(buf, bufsize, 1, fp) != 1)
        logger(ALWAYS, "Failed to load\n");
    else {
        openFluxBuffer(buf, bufsize);         // load in the flux data from buffer extracted from zip file
        ok = true;
    }
    fclose(fp);
    return ok;
}



int main(int argc, char **argv) {
    int arg;
    int optCnt, optVal;
    char optCh;

    for (arg = 1; arg < argc && (optCnt = sscanf(argv[arg], "-%c%d", &optCh, &optVal)); arg++) {
        switch (tolower(optCh)) {
        case 'd':
            debug = optCnt == 2 ? optVal : MINIMAL; break;
        default:
            fprintf(stderr, "unknown option %c\n", optCh);
        }
    }
    if (arg == argc) {
        fprintf(stderr, "no file\n");
        exit(1);
    }

    for (; arg < argc; arg++)
        if (loadFile(argv[arg], true)) {

            decode();
        }
    return 0;
}