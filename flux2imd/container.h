#pragma once
#include "flux2imd.h"

bool openFluxFile(const char *fname);
TrackId *loadFluxStream();
bool closeFluxFile();