#pragma once
#include <stdint.h>

int cntHardSectors();
int getNextFlux();
double getRPM();
int loadFlux(uint8_t* image, size_t size);
int seekBlock(unsigned num);
void unloadFlux();





