#pragma once
#include <stdint.h>

extern uint64_t pattern;

int getBit();               // get next bit or -1 if end of flux stream
unsigned getBitCnt();       // support function to return number of bits processed
unsigned getByteCnt();      // support function to return number of bytes processed
void retrain(int profile);  // reset the dpll using specified profile


