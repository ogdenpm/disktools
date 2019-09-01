#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
enum {
    ALWAYS = 0, MINIMAL = 1, VERBOSE = 3, VERYVERBOSE = 7,
    DECODER = 8, ADDRESSMARK = 0x10, PATTERN = 0x20, DETECT = 0x40,
    NOOPTIMISE = 0x80, ECHO = 0x100, TRACKER = 0x200,
    WARNING = 0xfffd, ERROR = 0xfffe, FATAL = 0xffff
};

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