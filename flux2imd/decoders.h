#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sectorManager.h"
#include "bits.h"

#define HSSPACING    168
#define HSIDAM   6        // synthetic HS IDAM




enum { UNK_DISK, STD, INTEL, ZDS, LSI, HP};
// types of marker detected
// SD_ prefix is SD data detected whilst decoding MFM
// M2FM_ prefix is M2FM data detected whilst decoding MFM
enum {
    GAP= 0x1ff, SYNC = 0x100, IBM_GAP = 0x4e, INDEXAM = 0xfc, IDAM = 0xfe, DATAAM = 0xfb, DELETEDAM = 0xf8,
    M2FM_INDEXAM = 0xc, M2FM_IDAM = 0xe, M2FM_DATAAM = 0xb, M2FM_DELETEDAM = 0x8,   // detection of M2FM in "Any" mode
    HP_IDAM = 0x10e, HP_DATAAM = 0xa , HP_DELETEDAM = 0xf,                      // HP M2FM flavour
    LSI_SECTOR = 0x200, ZDS_SECTOR = 0x300, RETRY = 0xffff};                               // hard sector specials


void hsGetTrack(int cylinder);
void ssGetTrack(int cylinder, int side);