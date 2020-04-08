// This is an open source non-commercial project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

#pragma once
#include <stdint.h>

int cntHardSectors();
int getNextFlux();
double getRPM();
int loadFlux(uint8_t* image, size_t size);
int seekBlock(unsigned num);
void unloadFlux();
uint32_t getBitPos(uint32_t cellSize);





