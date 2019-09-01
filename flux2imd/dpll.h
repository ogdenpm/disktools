#pragma once
#include <stdint.h>
#include <stdbool.h>


// nominal gap sizes in bytes
#define G4A     40
#define G1SD    26
#define G1DD    12
#define G2SD    11
#define G2DD    21
#define G3SD8   56
#define G3SD15  41
#define G3SD26  26
#define G3DD    20
#define G4BSD8  319
#define G4BSD15 170
#define G4BSD26 247
#define G4BDD32 248





typedef struct {
    uint16_t bitRatekbs;       // bit rate in kb/s
    uint8_t modulation;        // unknown FM, MFM ,M2FM
    uint8_t prevBitPair;           // last bitPair matched
    bool reversed;             // true if reversed
} dpllInfo_t;


void retrain(int profile);


