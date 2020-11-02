/****************************************************************************
 *  program: mkidsk - create IMD or IMG file from a recipe file             *
 *  Copyright (C) 2020 Mark Ogden <mark.pm.ogden@btinternet.com>            *
 *                                                                          *
 *  This program is free software; you can redistribute it and/or           *
 *  modify it under the terms of the GNU General Public License             *
 *  as published by the Free Software Foundation; either version 2          *
 *  of the License, or (at your option) any later version.                  *
 *                                                                          *
 *  This program is distributed in the hope that it will be useful,         *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU General Public License for more details.                            *
 *                                                                          *
 *  You should have received a copy of the GNU General Public License       *
 *  along with this program; if not, write to the Free Software             *
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,              *
 *  MA  02110-1301, USA.                                                    *
 *                                                                          *
 ****************************************************************************/


#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

typedef unsigned char byte;
typedef unsigned short word;

#define MAXLINE 512                     // maximum length of recipe line
#define ROOT "F:/OneDrive/Intel/"       // my local copy of the repository - to use if recipe is not in .../Intel/diskindex/
#define EXT ".imd"                      // default extension and hence format
#define MAXCOMMENT	4096				// upper limit on comment length from source IMD

// names for magic numbers
// disk types
enum { UNKNOWN = 0, ISIS_SD, ISIS_DD, ISIS_PDS, ISIS_IV };

#define SDSECTORS   26  // sectors on a SD disk
#define DDSECTORS   52  // sectors on a DD disk
#define I3SECTORS   32  // sectors per cylinder ISIS III disk
#define I2TRACKS      77  // number of tracks used
#define I3TRACKS    80  // tracks on ISIS III disk
#define SECTORSIZE  128 // bytes per sector
#define I3SECTORSIZE 256

#define MAXSECTOR   52


#define FMTBYTE     0xc7    // default byte used by isis for newly formatted data sectors
#define ALT_FMTBYTE 0xe5

// fixed location information
#define SDBINHDR    0x204
#define DDBINHDR    0x206
#define CNTDIRSECTORS   25
#define CNTI3DIRSECTORS 15
#define RMXLAB_LOC   4
#define ISOLAB_LOC   7

#define ISIST0_HDR  0x18
#define ISIST0_DAT  1
#define ISIST0_SIZE 23

#define ISIS3T0_HDR 0x11
#define ISIS3T0_SIZE 15

#define ISISLAB_HDR 0x19
#define ISISLAB_DATA 0x11B      // location of isis.lab additional blocks
#define ISISLAB_SDSIZE  1
#define ISISLAB_DDSIZE  27
#define ISISLAB_DDSIZEA 26

#define ISIS3LAB_HDR 0x101
#define ISIS3LAB_SIZE   3

#define ISISDIR_HDR     0x101
#define ISIS3DIR_HDR    0x2701

#define ISISMAP_HDR 0x201
#define ISISFRE_HDR 0x2711
#define ISISFRE_LEN 80

//activity values

#define     INUSE   0x00
#define     DELETE  0xFF
#define     NEVUSE  0x7F

//attribute values

#define     FMT     0x80
#define     WP      0x04
#define     SYS     0x02
#define     INV     0x01

// Accessor macros
#define BLKTRK(ts)  ((ts) >> 8)
#define BLKSEC(ts)  ((ts) &  0xff)
#define BLOCK(t, s) (((t) << 8) + s)

#pragma pack(push, 1)
typedef struct {
    byte use;
    byte file[6];
    byte ext[3];
    byte attrib;
    byte eofCnt;
    word blkCnt;
    word hdrBlk;
} direct_t;

typedef struct {
    byte name[9];
    byte version[2];
    byte leftOver[38];
    byte crlf[2];
    byte fmtTable[77];
} label_t;

#pragma pack(pop)

extern byte diskType;
extern label_t label;
extern bool hasSystem;

typedef struct {
    char *name;
    int len;
    uint8_t attrib;
    uint16_t hdr_blk;
    uint16_t data_blk;
} sfile_t;

typedef struct {
    uint8_t nCyl;
    uint8_t nHead;
    uint8_t nSector;
    uint8_t skew;
    int allocSSize;         // space allocated per sector
    char *modeSize;         // encoded as per tInterleave, 2 chars 1st mode, 2nd size
    char *tInterLeave;
    uint8_t bitMapSize;
} format_t;





extern format_t formats[4];

extern int sPerCyl;
extern byte *disk;
extern int sectorSize;
extern bool interTrackSkew;
extern int formatCh;

#ifdef _MSC_VER
#define stricmp _stricmp
#endif
void WriteImgFile(char *fname, int diskType, char *interleaves, bool useSkew, char *comment);
void InitFmtTable(byte t0Interleave, byte t1Interleave, byte interleave);
void CopyFile(char *isisName, char *srcName, int attrib);
void FormatDisk(int type, int formatCh);
void WriteVolLabels();
void WriteI2Directory();
void WriteI3Directory();
void WriteLabel();
byte *GetSectorLoc(word trkSec);