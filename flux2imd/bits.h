#pragma once
#include <stdint.h>

#define SUSPECT 0x100               // marker added to data if clock bit error


typedef struct {
    uint64_t mask;
    uint64_t match;
    uint16_t am;
} pattern_t;

extern uint64_t pattern;

int getBit();
int getByte();
int matchPattern(int searchLimit);
bool setEncoding(char *fmtName);

unsigned getByteCnt();