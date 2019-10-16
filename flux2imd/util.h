#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

// option flags
#define pOpt    1
#define gOpt    2
#define bOpt    4
// current file for log prefix
extern char curFile[];

enum {
    ALWAYS = 0, D_ECHO = 1, D_FLUX = 2, D_DETECT = 4, D_PATTERN = 8,
    D_ADDRESSMARK = 0x10, D_DECODER = 0x20,  D_NOOPTIMISE = 0x40, D_TRACKER = 0x80,
    D_WARNING = 0xfffd, D_ERROR = 0xfffe, D_FATAL = 0xffff
};

#if _DEBUG
#define DBGLOG(level, ...)   logFull(level, __VA_ARGS__)
#else
#define DBGLOG(level, ...)
#endif

extern int debug;

void* xmalloc(size_t size);
uint8_t flip[];
void logFull(int level, char* fmt, ...);
int logBasic(char* fmt, ...);

bool extMatch(const char* fname, const char* ext);
const char* fileName(const char* fname);
void createLogFile(const char *fname);
