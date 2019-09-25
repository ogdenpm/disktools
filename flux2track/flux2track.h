#pragma once
#include <stdbool.h>
#include "trackManager.h"

// decoders.c
void clearZDS();
bool flux2Track(int cylinder, int side);
bool isZDS();

// display.c
void displayDefectMap();
void displayTrack(int cylinder, int side, unsigned options);

// histogram.c
void displayHist(int levels);

// writeImage.c
void writeImdFile(char *fname);