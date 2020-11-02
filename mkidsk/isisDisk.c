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
/*
  DESCRIPTION
    part of mkidsk
    The routines to create a memory based ISIS disk image

MODIFICATION HISTORY
    17 Aug 2018 -- original release as mkidsk onto github
    18 Aug 2018 -- added copyright info
    19 Aug 2018 -- added non system boot file incase ISIS.T0 is not specified
    13 Sep 2018 -- renamed skew to interleave to align with normal terminology
    later changes recorded in git
*/


#include "mkIsisDisk.h"

direct_t *directory;

byte *bitMap;
int sPerCyl = DDSECTORS;
byte *disk;
unsigned diskSize;
int sectorSize = 128;
int binHdrBlk;

format_t formats[4] = {
    { 0},
    {77, 1, 26, 4, 128, "00",  "1<6", 2},     // ISIS_SD
    {77, 1, 52, 7, 128, "30",  "145", 4},    // ISIS_DD
    {80, 2, 16, 0, 256, "2051", "14", 1}    // ISIS_PDS
};

#pragma pack(push, 1)
struct {
    char volName[10];
    byte fill;
    byte fileDriver;
    word volGran;
    uint32_t volSize;
    word maxFnode;
    uint32_t fnodeStart;
    word fnodeSize;
    word rootFnode;
    byte unknown[8];
    char osFile[12];
} rmxLabel = {"          ", 7, 0, 1024, I3TRACKS * I3SECTORS * I3SECTORSIZE, 0, 0, 0, 0,
              {0, 1, 4, 0, 0, 0, '0', 0}, "ISIS.PDS    "};

#pragma pack(pop)

/*
    the code block below is loaded into isis.t0 on non system disks to print a message
    the corresponding asm code is

        ASEG
        org 3000h

        in	79h
        in	7Bh
    L3004:	in	0FFh
        ani	2
        jnz	L3004
        lxi	h, Msg
        mvi	b, 32
    L3010:	mov	c, m
        call	0F809h	; CO
        inx	h
        dcr	b
        jnz	L3010
        rst	0
    Msg:	db	0Dh, 0Ah
        db	'NON-SYSTEM DISK, TRY ANOTHER'
        db	0Dh, 0Ah
        end
*/

byte nsBoot[] = {
    0xDB, 0x79, 0xDB, 0x7B, 0xDB, 0xFF, 0xE6, 0x2,
    0xC2, 0x4, 0x30, 0x21, 0x1A, 0x30, 0x6, 0x20, 0x4E,
    0xCD, 0x9, 0xF8, 0x23, 0x5, 0xC2, 0x10, 0x30, 0xC7,
    0xD, 0xA,
    'N', 'O', 'N', '-', 'S', 'Y', 'S', 'T', 'E', 'M', ' ',
    'D', 'I', 'S', 'K', ',', ' ', 'T', 'R', 'Y', ' ', 'A',
    'N', 'O', 'T', 'H', 'E', 'R', 0xD, 0xA
};

word AllocSector() {
    static int bytePos = 0;     // optimisation - can use static as we don't delete sectors
    int track, bitPos;

    while (bitMap[bytePos] == 0xff) // for ISIS II will always stop on last slot which is never fully used
        ++bytePos;

    // find the lowest group with a free sector
    if (diskType == ISIS_PDS) {
        if (bytePos >= formats[ISIS_PDS].nCyl) {
            fprintf(stderr, "track %d reached - disk full\n", bytePos);
        }
        for (bitPos = 0; bitMap[bytePos] & (1 << bitPos); bitPos++)
            ;
        bitMap[bytePos] |= 1 << bitPos;
        return BLOCK(bytePos, bitPos * 4 + 1);
    }

    for (bitPos = 0; bitMap[bytePos] & (0x80 >> bitPos); bitPos++)
        ;
    if ((track = (bytePos * 8 + bitPos) / sPerCyl) >= formats[diskType].nCyl) {
        fprintf(stderr, "track %d reached - disk full\n", track);
        exit(1);
    }
    bitMap[bytePos] |= 0x80 >> bitPos;
    return BLOCK(track, (bytePos * 8 + bitPos) % sPerCyl + 1);      // sectors start at 1
}

void ReserveSector(word trkSec) {
    if (diskType == ISIS_PDS)
        bitMap[BLKTRK(trkSec)] |= 1 << ((BLKSEC(trkSec) - 1) / 4);
    else {
        int lsec = BLKTRK(trkSec) * sPerCyl + BLKSEC(trkSec) - 1;
        bitMap[lsec / 8] |= 0x80 >> (lsec % 8);
    }

}

byte *GetSectorLoc(word trkSec) {
    return disk + (BLKTRK(trkSec) * sPerCyl + BLKSEC(trkSec) - 1) * sectorSize;
}

void NewDataSector(word *hdr) {
    *hdr = AllocSector();
    memset(GetSectorLoc(*hdr), 0, sectorSize);        // clear out sector
    if (diskType == ISIS_PDS) {                                                // record additional sectors of cluster
        hdr[1] = hdr[0] + 1;
        hdr[2] = hdr[0] + 2;
        hdr[3] = hdr[0] + 3;
    }
}


word NewHdrSector(word trkSec) {
    if (trkSec == 0)
        trkSec = AllocSector();
    else
        ReserveSector(trkSec);
    word *hdr = (word *)GetSectorLoc(trkSec);
    memset(hdr, 0, sectorSize);
    if (diskType == ISIS_PDS) {
        hdr[2] = trkSec + 1;
        hdr[3] = trkSec + 2;
        hdr[4] = trkSec + 3;
    }
    return trkSec;
}

word *NewHdrPtr(word trkSec) {
    return (word *)GetSectorLoc(NewHdrSector(trkSec));
}

void FormatDisk(int type, int formatCh) {
    if (formatCh < 0)
        formatCh = type == ISIS_PDS ? ALT_FMTBYTE : FMTBYTE;

    sectorSize = type == ISIS_PDS ? 256 : 128;
    unsigned diskSize = formats[type].nCyl * formats[type].nHead * formats[type].nSector * sectorSize ;

    if ((disk = (byte *)malloc(diskSize)) == NULL) {
        fprintf(stderr, "fatal error out of memory\n");
        exit(1);
    }
    memset(disk, formatCh, diskSize);           // initial format
                                                // now set up some of the global variables
    diskType = type;
    sPerCyl = formats[type].nHead * formats[type].nSector;


    bitMap = GetSectorLoc(type == ISIS_PDS ? ISISFRE_HDR + 1 : ISISMAP_HDR + 1);         // location of ISIS.(FRE/MAP) data
    // make sure bitMap is clear
    memset(bitMap, 0, formats[type].bitMapSize * sectorSize);
    if (type == ISIS_PDS)
        WriteVolLabels();
}

void WriteVolLabels()
{
    bitMap[0] = 0xf;        // track 0 blocked out      
    byte *label = GetSectorLoc(ISOLAB_LOC);
    memset(label, ' ', 128);
    memcpy(label, "VOL1ISISPDS", 11);
    label[71] = 'M';      // probably represents MFM
    label[76] = '0';      // interleave
    label[77] = '4';
    label[79] = '1';      // iso version
    label = GetSectorLoc(RMXLAB_LOC);
    memset(label, 0, 128);
    memcpy(label, &rmxLabel, sizeof(rmxLabel));
}




void SetLinks(word *hdr, int  startSlot, int cnt, int trkSec) {
    int i;
    for (i = 0; i < cnt; i++) {
        hdr[startSlot + i] = trkSec + i;
        ReserveSector(trkSec + i);
    }
    while (hdr[startSlot + i])
        hdr[startSlot + i++] = 0;
}

bool NameToIsis(byte *isisName, char *name) {
    // convert name to isis internal format
    for (int i = 0; i < 6; i++) {
        if (isalnum(*name))
            isisName[i] = (byte)toupper(*name++);
        else
            isisName[i] = 0;
    }
    if (*name == '.')
        name++;
    for (int i = 6; i < 9; i++) {
        if (isalnum(*name))
            isisName[i] = (byte)toupper(*name++);
        else
            isisName[i] = 0;
    }
    return *name == 0 && *isisName;
}

direct_t *Lookup(char *name, bool autoName) {
    byte isisName[9];
    if (!NameToIsis(isisName, name)) {
        fprintf(stderr, "illegal ISIS name %s\n", name);
        exit(1);
    }
    direct_t *firstFree = NULL;                     // used to mark new entry insert point
    for (int i = 0; i < (diskType == ISIS_PDS ? CNTI3DIRSECTORS * 16: CNTDIRSECTORS * 8); i++)
        if (directory[i].use != INUSE) {
            if (firstFree == NULL && autoName)
                firstFree = &directory[i];
            if (directory[i].use == NEVUSE)
                break;
        }
#pragma warning(disable : 6385 6386)        // suppress warning about writing the 9 chars to full file name
        else if (memcmp(directory[i].file, isisName, 9) == 0)
            return &directory[i];
    if (firstFree)
        memcpy(firstFree->file, isisName, 9);
    return firstFree;
#pragma warning(default : 6385 6386)
}

direct_t *MakeDirEntry(char *name, int attrib, int eofCnt, int blkCnt, int hdrBlk) {
    direct_t *dir = Lookup(name, true);
    if (dir == NULL) {
        fprintf(stderr, "too many directory entries - trying to create %s\n", name);
        exit(1);
    }
    if (dir->use == INUSE) {
        fprintf(stderr, "file %s already exists\n", name);
        exit(1);
    }
    dir->use = INUSE;
    dir->attrib = attrib;
    dir->eofCnt = eofCnt;
    dir->blkCnt = blkCnt;
    dir->hdrBlk = hdrBlk;
    return dir;
}

void CopyFile(char *isisName, char *srcName, int attrib) {
    direct_t *dir;
    int blkCnt = 0;
    int hdrIdx;
    word *hdr;
    byte buf[I3SECTORSIZE];      // used as staging area for data read
    int actual;
    FILE *fp;
    int curHdrBlk;

    if (strcmp(srcName, "AUTO") == 0) {
        if (dir = Lookup(isisName, false)) {
            if (attrib != 0)
                dir->attrib = attrib;
        } else
            fprintf(stderr, "Out of directory space for file %s\n", isisName);
        return;

    }
    if (strcmp(srcName, "ZERO") == 0) {     // empty file
        if ((dir = Lookup(isisName, false)) == NULL)  // create dir entry if it doesn't exist
            dir = MakeDirEntry(isisName, attrib, 0, 0, 0);
        return;
    }
    if (strcmp(srcName, "ZEROHDR") == 0) {     // empty file with header
        if ((dir = Lookup(isisName, false)) == NULL)  // create dir entry if it doesn't exist
            dir = MakeDirEntry(isisName, attrib, 128, 0, NewHdrSector(0));
        return;
    }
    /* all ISIS system files except ISIS.CLI are given the right attributes in WriteDirectory
    make sure it has the correct attributes */
    if (_stricmp(isisName, "ISIS.CLI") == 0 && attrib == 0)
        attrib = FMT | SYS | INV;

    if ((fp = fopen(srcName, "rb")) == NULL) {
        fprintf(stderr, "%s can't find source file %s\n", isisName, srcName);
        return;
    }
    if ((dir = Lookup(isisName, false)) == NULL)  // create dir entry if it doesn't exist
        dir = MakeDirEntry(isisName, attrib, 0, 0, 0);
    else if (attrib)
        dir->attrib = attrib;

    if (dir->hdrBlk == 0)
        dir->hdrBlk = NewHdrSector(0);
    hdr = (word *)GetSectorLoc(dir->hdrBlk);
    curHdrBlk = dir->hdrBlk;
    while (1) {
        for (hdrIdx = 2; hdrIdx < (diskType == ISIS_PDS ? 125 : 64); hdrIdx++) {
            if ((actual = (int)fread(buf, 1, sectorSize, fp)) == 0)
                break;
            blkCnt++;
            if (hdr[hdrIdx] == 0)
                NewDataSector(&hdr[hdrIdx]);
            memcpy(GetSectorLoc(hdr[hdrIdx]), buf, actual);
            if (actual != sectorSize)
                break;
        }
        if (actual != sectorSize)
            break;

        if (hdr[1] == 0) {     // see if forward link exists
            hdr[1] = NewHdrSector(0);
            word *nxtHdr = (word *)GetSectorLoc(hdr[1]);
            nxtHdr[0] = curHdrBlk;
        }
        curHdrBlk = hdr[1];
        hdr = (word *)GetSectorLoc(curHdrBlk);
    }
    if (blkCnt > dir->blkCnt) {
        dir->blkCnt = blkCnt;
        dir->eofCnt = (actual == 0) ? sectorSize : actual;
        if (diskType == ISIS_PDS)
            dir->eofCnt--;
    }
    fclose(fp);
}

// lay down the initial directory info
// FormatDisk must have been called before
void WriteI2Directory() {
    int cntBitMapSectors = formats[diskType].bitMapSize;
    // setup linkage for ISIS.T0
    word *hdr = NewHdrPtr(ISIST0_HDR);
    SetLinks(hdr, 2, ISIST0_SIZE, ISIST0_DAT);
    // setup linkage for ISIS.LAB

    hdr = NewHdrPtr(ISISLAB_HDR);
    if (diskType == ISIS_SD)
        SetLinks(hdr, 2, ISISLAB_SDSIZE, ISISLAB_HDR + 1);
    else {
        SetLinks(hdr, 2, ISISLAB_DDSIZE, ISISLAB_HDR + 1);
        SetLinks(hdr, 2 + ISISLAB_DDSIZE, ISISLAB_DDSIZEA, ISISLAB_DATA);
    }
    // setup linkage for ISIS.DIR
    hdr = NewHdrPtr(ISISDIR_HDR);
    SetLinks(hdr, 2, CNTDIRSECTORS, ISISDIR_HDR + 1);
    // set up linkage for ISIS.MAP
    hdr = NewHdrPtr(ISISMAP_HDR);
    SetLinks(hdr, 2, cntBitMapSectors, ISISMAP_HDR + 1);
    // now write the initial ISIS.DIR
    directory = (direct_t *)GetSectorLoc(ISISDIR_HDR + 1);
    memset(directory, 0, CNTDIRSECTORS * SECTORSIZE);  // initialise to 0
    for (int i = 0; i < CNTDIRSECTORS * sectorSize / 16; i++)    // set the usage flags
        directory[i].use = NEVUSE;
    MakeDirEntry("ISIS.DIR", FMT | INV, 128, CNTDIRSECTORS, ISISDIR_HDR);
    MakeDirEntry("ISIS.MAP", FMT | INV, 128, cntBitMapSectors, ISISMAP_HDR);
    MakeDirEntry("ISIS.T0", FMT | INV, 128, ISIST0_SIZE, ISIST0_HDR);
    MakeDirEntry("ISIS.LAB", FMT | INV, 128, diskType == ISIS_SD ? ISISLAB_SDSIZE : ISISLAB_DDSIZE + ISISLAB_DDSIZEA, ISISLAB_HDR);
    if (hasSystem) {
        // finally setup empty linkage for ISIS.BIN
        int binHdrBlk = diskType == ISIS_SD ? SDBINHDR : DDBINHDR;
        NewHdrSector(binHdrBlk);
        MakeDirEntry("ISIS.BIN", FMT | SYS | INV, 128, 0, binHdrBlk);
    }
    // make sure there is at least a non system boot file in ISIS.T0
    memcpy(GetSectorLoc(ISIST0_DAT), nsBoot, sizeof(nsBoot));
}


void WriteI3Directory() {

    int cntBitMapSectors = formats[diskType].bitMapSize;
    // setup linkage for ISIS.T0
    word *hdr = NewHdrPtr(ISIS3T0_HDR);
    SetLinks(hdr, 2, ISIS3T0_SIZE, ISIS3T0_HDR + 1);
    // setup linkage for ISIS.LAB

    hdr = NewHdrPtr(ISIS3LAB_HDR);
    SetLinks(hdr, 2, ISIS3LAB_SIZE, ISIS3LAB_HDR + 1);

    // setup linkage for ISIS.DIR
    hdr = NewHdrPtr(ISIS3DIR_HDR);
    SetLinks(hdr, 2, CNTI3DIRSECTORS, ISIS3DIR_HDR + 1);
    // set up linkage for ISIS.MAP
    hdr = NewHdrPtr(ISISFRE_HDR);
    SetLinks(hdr, 2, cntBitMapSectors, ISISFRE_HDR + 1);
    // now write the initial ISIS.DIR
    directory = (direct_t *)GetSectorLoc(ISIS3DIR_HDR + 1);
    memset(directory, 0, CNTI3DIRSECTORS * sectorSize);  // initialise to 0
    for (int i = 0; i < CNTI3DIRSECTORS * sectorSize / 16; i++)    // set the usage flags
        directory[i].use = NEVUSE;
    MakeDirEntry("ISIS.DIR", INV, sectorSize - 1, CNTI3DIRSECTORS, ISIS3DIR_HDR);
    MakeDirEntry("ISIS.FRE", INV, ISISFRE_LEN - 1, cntBitMapSectors, ISISFRE_HDR);
    MakeDirEntry("ISIS.T0", INV, sectorSize - 1, ISIS3T0_SIZE, ISIS3T0_HDR);
    MakeDirEntry("ISIS.LAB", FMT | INV, sectorSize - 1, ISIS3LAB_SIZE, ISIS3LAB_HDR);

}

void WriteLabel() {
    // iniitlise the supplementary label info and write to image
    if (diskType == ISIS_PDS) {
        char *labelSector = GetSectorLoc(ISIS3LAB_HDR + 1);
        memset(label.leftOver, ' ', sizeof(label.leftOver));
        memcpy(labelSector, &label, sizeof(label) - sizeof(label.fmtTable));
        labelSector = GetSectorLoc(ISIS3LAB_HDR + 3);
        for (int i = 0; i < 16; i++)
            memcpy(labelSector + i * 16, "DIAGNOSTICSECTOR", 16);
    } else {
        if (label.fmtTable[0] == 0) {
            char *interleaves = formats[diskType].tInterLeave;
            InitFmtTable(interleaves[0] - '0', interleaves[1] - '0', interleaves[2] - '0');
        }
        label.crlf[0] = '\r';
        label.crlf[1] = '\n';
        memcpy(GetSectorLoc(ISISLAB_HDR + 1), &label, sizeof(label));
    }
}
