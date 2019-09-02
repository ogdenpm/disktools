#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

enum {
    ALWAYS = 0, ECHO = 1, FLUX = 2, DETECT = 4, PATTERN = 8,
    ADDRESSMARK = 0x10, DECODER = 0x20,  NOOPTIMISE = 0x40, TRACKER = 0x80,
    WARNING = 0xfffd, ERROR = 0xfffe, FATAL = 0xffff
};

#if _DEBUG
#define DBGLOG(x)   logFull##x
#else
#define DBGLOG(x)
#endif

extern int debug;

void* xmalloc(size_t size);
unsigned flip(unsigned pattern);
unsigned getDbits(unsigned pattern);
unsigned getCbits(unsigned pattern);
uint64_t encodeFM(uint32_t val);
void logFull(int level, char* fmt, ...);
int logBasic(char* fmt, ...);

bool extMatch(const char* fname, const char* ext);
const char* fileName(const char* fname);