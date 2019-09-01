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
int getBitPair();
int getByte();
uint32_t matchPattern(int searchLimit);
bool setEncoding(char *fmtName);
void resetByteCnt();

uint32_t getByteCnt();