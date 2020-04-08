// This is an open source non-commercial project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

#pragma once
#include <stdint.h>
#include <stdbool.h>

extern uint64_t pattern;
extern uint16_t bits65_66;

int getBit();               // get next bit or -1 if end of flux stream
unsigned getBitCnt();       // support function to return number of bits processed
unsigned getByteCnt();      // support function to return number of bytes processed
bool retrain(int profile);  // reset the dpll using specified profile


