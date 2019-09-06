#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

extern char curFile[];
int debug;
FILE *logFp = NULL;  

// flip the order of the data, leave the suspect bit unchanged

bool extMatch(const char* fname, const char* ext) {
    char* s = strrchr(fname, '.');
    return (s && _stricmp(s, ext) == 0);
}

const char* fileName(const char* fname) {
    char* s = strrchr(fname, '/');
    char* t = strrchr(fname, '\\');
    if (!s || (t && s < t))
        s = t;
    if (s)
        return s + 1;
    return fname;
}

unsigned flip(unsigned pattern) {
    uint16_t flipped = (pattern & 0x100) ? 1 : 0;       // suspect bit
    for (int i = 0; i < 8; i++, pattern >>= 1)
        flipped = (flipped << 1) + (pattern & 1);
    return flipped;
}

int logBasic(char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    if ((debug & D_ECHO) && logFp != stdout)
        vprintf(fmt, args);
    return vfprintf(logFp, fmt, args);
}

void logFull(int level, char *fmt, ...) {
    static char* prefix[] = { "WARNING", "ERROR", "FATAL" };

    va_list args;
    va_start(args, fmt);

    if (level >= D_WARNING) {
        if (logFp != stdout) {
            fprintf(logFp, "%s - %s: ", curFile, prefix[level - D_WARNING]);
            vfprintf(logFp, fmt, args);
        }
        fprintf(stderr, "%s - %s: ", curFile, prefix[level - D_WARNING]);
        vfprintf(stderr, fmt, args);
    }
    else if (level ==  0 || (debug & level)) {
        fprintf(logFp, "%s - ", curFile);
        vfprintf(logFp, fmt, args);
        if ((debug & D_ECHO) && logFp != stdout) {
            printf("%s - ", curFile);
            vprintf(fmt, args);
        }
    }
 
    if (level == D_FATAL) {
        if (logFp)
            fclose(logFp);
        exit(1);
    }
}

void createLogFile(const char* name) {
    char logFile[_MAX_PATH + 1];
    if (logFp && logFp != stdout)
        fclose(logFp);
    if (name) {
        strcpy(logFile, name);
        strcpy(strrchr(logFile, '.'), ".log");
        if ((logFp = fopen(logFile, "wt")) == NULL) {
            logFp = stdout;
            logFull(D_ERROR, "could not create %s; using stdout\n", fileName(logFile));
        }
    } else
        logFp = stdout;

}

void* xmalloc(size_t size) {
    void* ptr;
    if (!(ptr = malloc(size))) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return ptr;
}
