// This is an open source non-commercial project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

#pragma once
#include <stdbool.h>
#include "trackManager.h"

// decoders.c
void assumeIMD();
bool flux2Track(int cylinder, int side, char *usrfmt);
bool noIMD();

// display.c
void displayDefectMap();
void displayTrack(int cylinder, int side, unsigned options);

// histogram.c
void displayHist(int levels);

// writeImage.c
void writeImdFile(char *fname);
void analyse(char *opt);