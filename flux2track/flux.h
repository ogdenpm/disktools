#pragma once
#include <stdint.h>

int cntHardSectors();
int getNextFlux();
int getRPM();
int loadFlux(uint8_t* image, size_t size);
int seekBlock(unsigned num);
void extendToEOT();
void extendNext();
void unloadFlux();






