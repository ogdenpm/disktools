#pragma once

#define CELL_NS 2000                // cell width in nS

#define PRESYNC_TOLERANCE    10
#define POSTSYNC_TOLERANCE   8

void resetPLL(int preSync, int postSync);
bool clockSync();
int getBit();
int getBitPair();