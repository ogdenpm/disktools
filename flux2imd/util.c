#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "util.h"

extern char curFile[];
extern int debug;
extern FILE* logFp;

void* xmalloc(size_t size) {
    void* ptr;
    if (!(ptr = malloc(size))) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return ptr;
}

   

// flip the order of the data, leave the suspect bit unchanged
unsigned flip(unsigned pattern) {
    uint16_t flipped = (pattern & 0x100) ? 1 : 0;       // suspect bit
    for (int i = 0; i < 8; i++, pattern >>= 1)
        flipped = (flipped << 1) + (pattern & 1);
    return flipped;

}


unsigned getDbits(unsigned pattern) {
    return pattern & 0xff;
}

unsigned getCbits(unsigned pattern) {
    return (pattern >> 8) & 0xff;
}





void logFull(int level, char *fmt, ...) {
    static char* prefix[] = { "WARNING", "ERROR", "FATAL" };

    va_list args;
    va_start(args, fmt);

    if (level >= WARNING) {
        if (logFp != stdout) {
            fprintf(logFp, "%s - %s: ", curFile, prefix[level - WARNING]);
            vfprintf(logFp, fmt, args);
        }
        fprintf(stderr, "%s - %s: ", curFile, prefix[level - WARNING]);
        vfprintf(stderr, fmt, args);
    }
    else if (level ==  0 || (debug & level)) {
        fprintf(logFp, "%s - ", curFile);
        vfprintf(logFp, fmt, args);
        if ((debug & ECHO) && logFp != stdout) {
            printf("%s - ", curFile);
            vprintf(fmt, args);
        }
    }
 
    if (level == FATAL) {
        if (logFp)
            fclose(logFp);
        exit(1);
    }
}

int logBasic(char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    if ((debug & ECHO) && logFp != stdout)
        vprintf(fmt, args);
    return vfprintf(logFp, fmt, args);
}

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
