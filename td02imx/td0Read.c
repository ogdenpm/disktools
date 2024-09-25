/* td0Read.c  module to perform read form td0 files
   supports, normal and both models of advanced compression as
   used in teledisk

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
   http://www.gnu.org/licenses/gpl.txt

   Also see notes below on individual compression models for sources of
   third party code or algorithms.


*/

#include "td0.h"

#include <ctype.h> // uses isdigit

// pattern buffer
#define MAXPATTERN 0x4000
td0SectorData_t patBuf;
// the pattern encodings
#define COPY   0
#define REPEAT 1
#define RLE    2

static int getTd0Byte(); // forward references to NORMAL read
static void initNew();   // forward reference to advance compression options
static void initOld();
static void readSector(sector_t *pSector);

static char *td0File;
static td0Header_t td0Header;
static td0Track_t curTrack;

static FILE *fpin;
static int (*getByte)() = getTd0Byte; // default decode is normal
static uint8_t lastNSec;              // used to protect readTrack release of memory

/// <summary>
/// helper function to convert 2 bytes stored in little endian format
/// into an int. Used for portability across machine architectures
/// </summary>
/// <param name="p">A pointer to the 2 byte pair</param>
/// <returns></returns>
inline int leword(uint8_t *p) {
    return p[0] + p[1] * 256;
}

void *safeMalloc(size_t size) {
    void *p = malloc(size);
    if (!p)
        fatal("Out of memory");
    return p;

}


/*******************/
/* CRC calculation */
/*******************/
static uint16_t crcTable[] = {
    0x0000, 0xa097, 0xe1b9, 0x412e, 0x63e5, 0xc372, 0x825c, 0x22cb, 0xc7ca, 0x675d, 0x2673, 0x86e4,
    0xa42f, 0x04b8, 0x4596, 0xe501, 0x2f03, 0x8f94, 0xceba, 0x6e2d, 0x4ce6, 0xec71, 0xad5f, 0x0dc8,
    0xe8c9, 0x485e, 0x0970, 0xa9e7, 0x8b2c, 0x2bbb, 0x6a95, 0xca02, 0x5e06, 0xfe91, 0xbfbf, 0x1f28,
    0x3de3, 0x9d74, 0xdc5a, 0x7ccd, 0x99cc, 0x395b, 0x7875, 0xd8e2, 0xfa29, 0x5abe, 0x1b90, 0xbb07,
    0x7105, 0xd192, 0x90bc, 0x302b, 0x12e0, 0xb277, 0xf359, 0x53ce, 0xb6cf, 0x1658, 0x5776, 0xf7e1,
    0xd52a, 0x75bd, 0x3493, 0x9404, 0xbc0c, 0x1c9b, 0x5db5, 0xfd22, 0xdfe9, 0x7f7e, 0x3e50, 0x9ec7,
    0x7bc6, 0xdb51, 0x9a7f, 0x3ae8, 0x1823, 0xb8b4, 0xf99a, 0x590d, 0x930f, 0x3398, 0x72b6, 0xd221,
    0xf0ea, 0x507d, 0x1153, 0xb1c4, 0x54c5, 0xf452, 0xb57c, 0x15eb, 0x3720, 0x97b7, 0xd699, 0x760e,
    0xe20a, 0x429d, 0x03b3, 0xa324, 0x81ef, 0x2178, 0x6056, 0xc0c1, 0x25c0, 0x8557, 0xc479, 0x64ee,
    0x4625, 0xe6b2, 0xa79c, 0x070b, 0xcd09, 0x6d9e, 0x2cb0, 0x8c27, 0xaeec, 0x0e7b, 0x4f55, 0xefc2,
    0x0ac3, 0xaa54, 0xeb7a, 0x4bed, 0x6926, 0xc9b1, 0x889f, 0x2808, 0xd88f, 0x7818, 0x3936, 0x99a1,
    0xbb6a, 0x1bfd, 0x5ad3, 0xfa44, 0x1f45, 0xbfd2, 0xfefc, 0x5e6b, 0x7ca0, 0xdc37, 0x9d19, 0x3d8e,
    0xf78c, 0x571b, 0x1635, 0xb6a2, 0x9469, 0x34fe, 0x75d0, 0xd547, 0x3046, 0x90d1, 0xd1ff, 0x7168,
    0x53a3, 0xf334, 0xb21a, 0x128d, 0x8689, 0x261e, 0x6730, 0xc7a7, 0xe56c, 0x45fb, 0x04d5, 0xa442,
    0x4143, 0xe1d4, 0xa0fa, 0x006d, 0x22a6, 0x8231, 0xc31f, 0x6388, 0xa98a, 0x091d, 0x4833, 0xe8a4,
    0xca6f, 0x6af8, 0x2bd6, 0x8b41, 0x6e40, 0xced7, 0x8ff9, 0x2f6e, 0x0da5, 0xad32, 0xec1c, 0x4c8b,
    0x6483, 0xc414, 0x853a, 0x25ad, 0x0766, 0xa7f1, 0xe6df, 0x4648, 0xa349, 0x03de, 0x42f0, 0xe267,
    0xc0ac, 0x603b, 0x2115, 0x8182, 0x4b80, 0xeb17, 0xaa39, 0x0aae, 0x2865, 0x88f2, 0xc9dc, 0x694b,
    0x8c4a, 0x2cdd, 0x6df3, 0xcd64, 0xefaf, 0x4f38, 0x0e16, 0xae81, 0x3a85, 0x9a12, 0xdb3c, 0x7bab,
    0x5960, 0xf9f7, 0xb8d9, 0x184e, 0xfd4f, 0x5dd8, 0x1cf6, 0xbc61, 0x9eaa, 0x3e3d, 0x7f13, 0xdf84,
    0x1586, 0xb511, 0xf43f, 0x54a8, 0x7663, 0xd6f4, 0x97da, 0x374d, 0xd24c, 0x72db, 0x33f5, 0x9362,
    0xb1a9, 0x113e, 0x5010, 0xf087,
};

/// <summary>
/// Calculates the CRC of the len bytes starting at buf
/// An initial crc value can be supplied, for normal
/// calculation this should be 0
/// </summary>
/// <param name="buf">The location of the byte stream to use.</param>
/// <param name="len">The number of bytes to use in the calculation.</param>
/// <param name="crc">An initial crc value. Normally 0</param>
/// <returns></returns>
uint16_t calcCrc(void *buf, uint16_t len, uint16_t crc) {
    uint8_t *p = buf;
    for (uint16_t i = 0; i < len; i++)
        crc = crcTable[p[i] ^ (crc >> 8)] ^ (crc << 8);
    return crc;
}

/*************************************/
/* primary interface from other code */
/*************************************/

/// <summary>
/// Opens the named td0 file. The file header is read and checked.
/// The file name is saved for use in subsequent
/// multi-volume processing.
/// If the file is compressed the appropriate initialisation function
/// is called and the decompression function is selected.
/// If there is a comment this is read and checked, after which the
/// comment is converted to a C string with '\0' replace by '\n'.
/// On error the user supplied function fatal is called. It is assumed
/// not to return.
/// Note the definition of the td0Header is in td0.h.
/// </summary>
/// <param name="name">The name of the file to open</param>
/// <returns>a pointer to the td0Header (including any comment).</returns>
td0Header_t *openTd0(char *name) {

    if ((fpin = fopen(name, "rb")) == NULL)
        fatal("Could not open %s", name);

    if (fread(&td0Header, 1, sizeof(td0FileHeader_t), fpin) != sizeof(td0FileHeader_t) ||
        (strcmp(td0Header.sig, "TD") != 0 && strcmp(td0Header.sig, "td") != 0) ||
        calcCrc(&td0Header, sizeof(td0FileHeader_t) - 2, 0) != leword(td0Header.fCrc))
        fatal("%s not a valid TD0 file", name);

    td0File = safeMalloc(strlen(name) + 1);
    strcpy(td0File, name);
    // check for advanced compression
    if (td0Header.sig[0] == 't') { // initialize decompression routine
        if (td0Header.ver > 19)
            initNew();
        else
            initOld();
    }
    if (td0Header.stepping & HAS_COMMENT) {
        if (!readTd0(td0Header.cCrc, sizeof(td0CommentHeader_t)))
            fatal("Failed to read comment header");
        uint16_t len = leword(td0Header.len); // portability
        if (len > MAXCOMMENT || !readTd0(td0Header.comment, len))
            fatal("Failed to read comment text");

        // check all but 1st 2 bytes of comment region
        uint16_t crc = calcCrc(td0Header.len, sizeof(td0CommentHeader_t) - 2, 0); // header
        crc          = calcCrc(td0Header.comment, len, crc);                     // comment text
        if (leword(td0Header.cCrc) != crc)
            fatal("CRC error in comment header");
        // convert comment to standard C string
        for (int i = 0; i < len; i++)
            if (td0Header.comment[i] == 0)
                td0Header.comment[i] = '\n';
        td0Header.comment[len] = '\0';
    }
    return &td0Header;
}

/// <summary>
/// Read a full track of data including all of the sectors
/// Memory is allocated for the underlying sector information
/// It is assumed that this memory is used by the consumer and if so it
/// should transfer the pointer to a local variable and clear the sectors
/// pointer e.g.
///     track_t *p;
///     if ((p = readTrack()) {
///         // use the information including copy p->sectors
///         p->sectors = NULL;
///     }
///
/// If it isn't cleared this routine will release the allocated
/// memory and any under lying data memory allocated in readSector
/// </summary>
/// <returns>Pointer to the track structure else NULL at end</returns>
td0Track_t *readTrack() {
    // release old info if necessary
    if (curTrack.sectors) {
        for (int i = 0; i < lastNSec; i++)
            free(curTrack.sectors[i].data); // note free(NULL) is harmless
        free(curTrack.sectors);
    }
    if (!readTd0(&curTrack.nSec, 1))
        fatal("Corrupt file, failed to read track header");
    if (curTrack.nSec == 0xff)
        return NULL;
    if (!readTd0(&curTrack.cyl, sizeof(td0TrackHeader_t) - 1))
        fatal("Corrupt file, failed to read track header");

    if (curTrack.tCrc != (calcCrc(&curTrack.nSec, sizeof(td0TrackHeader_t) - 1, 0) & 0xff))
        fatal("CRC error in track record %d", curTrack.cyl);

    curTrack.sectors = curTrack.nSec ? malloc(curTrack.nSec * sizeof(sector_t)) : NULL;
    for (int i = 0; i < curTrack.nSec; i++)
        readSector(curTrack.sectors + i);

    lastNSec = curTrack.nSec; // save in case next call requires release of memory
    return &curTrack;
}

/// <summary>
/// read a sector from the data stream
/// if the sector has content a buffer is allocated to record this.
/// See the notes under readTrack re whether this data is released
/// /// </summary>
/// <param name="pSector"></param>
static void readSector(sector_t *pSector) {
    if (!readTd0(&pSector->cyl, sizeof(td0SectorHeader_t)))
        fatal("Premature EOF reading sector header\n");

    if ((pSector->sSize & 0xf8) || (pSector->flags & 0x30)) {
        pSector->data = NULL; // no data available
        if ((calcCrc(&pSector->cyl, sizeof(td0SectorHeader_t) - 1, 0) & 0xff) !=
            pSector->sCrc)
            warn("%d/%d/%d: Sector header CRC error", pSector->cyl, pSector->head, pSector->sec);
    } else {
        uint16_t sSize = 128 << pSector->sSize;

        if (!readTd0(&patBuf.len, sizeof(patBuf.len)))
            fatal("Premature EOF reading pattern length\n");
        uint16_t len = leword(patBuf.len);
        if (len > MAXPATTERN)
            fatal("Pattern buffer length %d too large", len);

        if (len == 0)
            fatal("Corrupt file: no pattern type");

        if (!readTd0(&patBuf.pattern, len))
            fatal("Premature EOF reading pattern");

        pSector->data = malloc(sSize);
        uint8_t *ptr  = pSector->data;
        uint8_t *pEnd = ptr + sSize;

        switch (patBuf.pattern[0]) {
        case COPY:
            if (--len && ptr + len <= pEnd)
                memcpy(ptr, &patBuf.pattern[1], len);
            ptr += len;
            break;
        case REPEAT:
            if (len < 5)
                fatal("Corrupt file: short Repeat pattern");
            int16_t rept = patBuf.pattern[1] + (patBuf.pattern[2] << 8);
            if (rept < 0)
                fatal("Corrupt file: bad repeat count in Repeat pattern");
            if (ptr + 2 * rept <= pEnd) {
                while (rept-- > 0) {
                    *ptr++ = patBuf.pattern[3];
                    *ptr++ = patBuf.pattern[4];
                }
            } else
                ptr += 2 * rept;
            break;
        case RLE:
            uint16_t step;
            for (uint16_t i = 1; i < len; i += step) {
                switch (patBuf.pattern[i]) {
                case COPY:
                    if (i + 1 > len || patBuf.pattern[i + 1] + 2 > len)
                        fatal("Corrupt file: short RLE-Copy pattern");
                    if (ptr + patBuf.pattern[i + 1] <= pEnd)
                        memcpy(ptr, &patBuf.pattern[i + 2], patBuf.pattern[i + 1]);
                    ptr += patBuf.pattern[i + 1];
                    step = patBuf.pattern[i + 1] + 2;
                    break;
                case REPEAT:
                    if (i + 3 > len)
                        fatal("Corrupt file: short RLE-Repeat pattern");
                    if (ptr + 2 * patBuf.pattern[i + 1] <= pEnd) {

                        for (int j = 0; j < patBuf.pattern[i + 1]; j++) {
                            *ptr++ = patBuf.pattern[i + 2];
                            *ptr++ = patBuf.pattern[i + 3];
                        }
                    } else
                        ptr += 2 * patBuf.pattern[i + 1];
                    step = 4;
                    break;
                default:
                    fatal("Corrupt file: Unknown RLE subtype %d", patBuf.pattern[i]);
                }
            }
            break;
        default:
            fatal("Corrupt file: Unknown pattern type %d", patBuf.pattern[0]);
            break;
        }
        if (ptr != pEnd)
            fatal("Corrupt file: Sector length %d, expected %d", (int)(ptr - pSector->data), sSize);

        if ((calcCrc(pSector->data, sSize, 0) & 0xff) != pSector->sCrc)
            fatal("%d/%d/%d: Sector header CRC error", pSector->cyl, pSector->head, pSector->sec);
        pSector->extraFlag = 0; // safe to reuse as crc checked
    }
}

/// <summary>
/// Closes any open td0 or chained file
/// </summary>
void closeTd0() {
    if (fpin)
        fclose(fpin);
}

/// <summary>
/// Reads a buf from the possibly compressed data stream
/// </summary>
/// <param name="buf">The location to read to</param>
/// <param name="len">The number of bytes to read.</param>
/// <returns>true if successful read else false</returns>
bool readTd0(void *buf, uint16_t len) {
    int c;
    uint8_t *p = buf;
    while (len--) {
        if ((c = getByte()) == EOF)
            return false;
        *p++ = (uint8_t)c;
    }
    return true;
}

/// <summary>
/// Chain to next file in a mutli-volume set
/// currently not tested as I have no real examples to test with
/// warns of issues and sets fpin to NULL if it cannot find the
/// file or it is invalid.
/// The header of the next file is read and checked for a valid CRC
/// it should also have the same signature (first 4 bytes) as the original
/// td0 file
/// </summary>
static void chainTd0() {
    char *s = strchr(td0File, '\0') - 1;
    td0Header_t hdr;
    if (!isdigit(*s)) {
        warn("Cannot determine next file from %s\n", basename(td0File));
        fclose(fpin);
        fpin = NULL;
    } else {
        s++; // bump file sequence
        if (!(fpin = freopen(td0File, "rb", fpin)))
            warn("Cannot not find next file %s", td0File);
        else if (fread(&hdr, 1, sizeof(td0FileHeader_t), fpin) != sizeof(td0FileHeader_t) ||
                 memcmp(&td0Header, &hdr, 4) != 0 ||
                 calcCrc(&hdr, sizeof(td0FileHeader_t) - 2, 0) != leword(hdr.fCrc)) {
            warn("next file %s is invalid", basename(td0File));
            fclose(fpin);
            fpin = NULL;
        } else
            printf("Continuing with next file %s\n", basename(td0File));
    }
}

/***************************************/
/*  NORMAL decoder i.e. no compression */
/***************************************/
/// <summary>
/// Gets the byte from the td0 file, no decompression
/// is done, however on EOF, chaining is attempted.
/// </summary>
/// <returns>next byte or EOF</returns>
static int getTd0Byte() {
    int c;
    while (fpin && (c = getc(fpin)) == EOF)
        chainTd0();
    return fpin ? c : EOF;
}

/*******************************/
/* LZHUF (new Advanced) decoder*/
/*******************************/
/*
   lzhuff decompression derived from code noted below:
   * LZHUF.C English version 1.0
   * Based on Japanese version 29-NOV-1988
   * LZSS coded by Haruhiko OKUMURA
   * Adaptive Huffman Coding coded by Haruyasu YOSHIZAKI
   * Edited and translated to English by Kenji RIKITAKE
   * d_len processing based on Dave Dunfield's td02imd
*/
#define MBSIZE     4096 // Size of main buffer
#define LASIZE     60   // Size of look-ahead buffer
#define THRESHOLD  2    // Min match for compress

#define N_CHAR     (256 + LASIZE - THRESHOLD)
/* character code (= 0..N_CHAR-1) */
#define NEWTABSIZE (N_CHAR * 2 - 1) /* Size of Table */
#define ROOT       (NEWTABSIZE - 1) /* root position */
#define MAX_FREQ   0x8000           // trigger for reconstruct

/* lzhuf decoder tables */
static uint8_t d_code[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
    0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D, 0x0E, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F, 0x0F, 0x0F,
    0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11, 0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13,
    0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15, 0x15, 0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17,
    0x18, 0x18, 0x19, 0x19, 0x1A, 0x1A, 0x1B, 0x1B, 0x1C, 0x1C, 0x1D, 0x1D, 0x1E, 0x1E, 0x1F, 0x1F,
    0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23, 0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27,
    0x28, 0x28, 0x29, 0x29, 0x2A, 0x2A, 0x2B, 0x2B, 0x2C, 0x2C, 0x2D, 0x2D, 0x2E, 0x2E, 0x2F, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
};

static uint8_t d_len[] = { 2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7 };

static uint16_t parent[NEWTABSIZE + N_CHAR]; // pointing parent nodes.
                                             // last N_CHAR entries are pointers for leaves
static uint16_t child[NEWTABSIZE];           // pointing children nodes (child[], child[] + 1
static uint16_t freq[NEWTABSIZE + 1];        // cumulative freq table
static uint8_t ring[MBSIZE];                 // ring buffer for lzhuf decode
static uint16_t fillPos, drainCnt, drainPos; // used to control ring buffer

// bit stream accessors
// state variables for bit stream
static int16_t streamVal = 0; // holds streamLen bits
static uint8_t streamLen = 0;

static int getNewByte(); // forward ref

/// <summary>
/// Gets the next bit from the compressed td0 stream
/// </summary>
/// <returns></returns>
static int getNewBit() { /* get one bit */
    if (streamLen == 0) {
        if ((streamVal = getTd0Byte()) < 0)
            return EOF;
        streamLen = 8;
    }
    return (streamVal >> --streamLen) & 1;
}

/// <summary>
/// Gets the next 8 bits (byte) from the compressed td0
/// stream
/// </summary>
/// <returns>The next byte value or EOF</returns>
static int getNew8Bits() { /* get a byte */
    uint8_t topbits = streamVal << (8 - streamLen);
    if ((streamVal = getTd0Byte()) < 0)
        return EOF;
    return topbits | (streamVal >> streamLen);
}

/// <summary>
/// Initializes the new advanced compression model
/// including the key tables and state variables
/// the getByte routine is set up for decompression
/// </summary>
static void initNew() {

    for (uint16_t i = 0; i < N_CHAR; i++) {
        freq[i]                = 1;
        child[i]               = i + NEWTABSIZE;
        parent[i + NEWTABSIZE] = i;
    }

    for (uint16_t i = 0, j = N_CHAR; j <= ROOT; i += 2, j++) {
        freq[j]   = freq[i] + freq[i + 1];
        child[j]  = i;
        parent[i] = parent[i + 1] = j;
    }
    freq[NEWTABSIZE] = 0xffff; // sentinal to avoid inserting freq value past end
    parent[ROOT]     = 0;      // end marker for walking  tree

    drainCnt         = 0; // reset ring buffer
    fillPos          = MBSIZE - LASIZE;
    memset(ring, ' ', fillPos); // init rest of buffer
    getByte = getNewByte;       // set to use New Advanced decoder
}

/// <summary>
/// Reconstruct the freq tree to keep the freq in
/// a sensible range
/// </summary>
static void reconst() {
    /* collect leaf nodes in the first half of the table
       and replace the freq by (freq + 1) / 2.
       scaling does not impact the sorted values
    */
    for (int i = 0, j = 0; i < NEWTABSIZE; i++) {
        if (child[i] >= NEWTABSIZE) {
            freq[j]  = (freq[i] + 1) / 2;
            child[j] = child[i];
            j++;
        }
    }
    /* begin constructing tree by connecting children */
    for (int16_t i = 0, top = N_CHAR; top < NEWTABSIZE; i += 2, top++) {
        uint16_t pairFreq = freq[i] + freq[i + 1];
        uint16_t insertPos;
        for (insertPos = top - 1; pairFreq < freq[insertPos];
             insertPos--) // locate the insert point
            ;
        insertPos++;
        // insert in order
        for (int i = top; i > insertPos; i--) {
            freq[i]  = freq[i - 1];
            child[i] = child[i - 1];
        }
        freq[insertPos]  = pairFreq;
        child[insertPos] = i;
    }
    /* connect parent nodes */
    for (int i = 0; i < NEWTABSIZE; i++) {
        int16_t k = child[i];
        parent[k] = i;
        if (k < NEWTABSIZE)
            parent[k + 1] = i;
    }
}

/// <summary>
/// Updates the freq tree for the current code
/// if necessary it rebuilds the freq tables
/// to keep the counts in range
/// </summary>
/// <param name="curLoc">The current code location</param>
static void update(uint16_t curLoc) {

    if (freq[ROOT] == MAX_FREQ)
        reconst();

    curLoc = parent[curLoc + NEWTABSIZE];
    do {
        uint16_t newFreq = ++freq[curLoc];
        uint16_t newLoc  = curLoc + 1;

        /* swap nodes to keep the tree freq-ordered */
        if (newFreq > freq[newLoc]) {
            while (newFreq > freq[newLoc + 1]) // find swap location
                newLoc++;
            freq[curLoc]      = freq[newLoc]; // swap the freq
            freq[newLoc]      = newFreq;

            uint16_t curChild = child[curLoc];
            uint16_t newChild = child[newLoc];

            child[newLoc]     = curChild; // swap child
            child[curLoc]     = newChild;

            parent[curChild]  = newLoc;
            if (curChild < NEWTABSIZE) // has sibling
                parent[curChild + 1] = newLoc;

            parent[newChild] = curLoc;
            if (newChild < NEWTABSIZE) // has sibling
                parent[newChild + 1] = curLoc;

            curLoc = newLoc;
        }
    } while ((curLoc = parent[curLoc])); /* do it until reaching the root */
}

/// <summary>
/// Decodes the next code character.
/// Also updates the freq trees
/// </summary>
/// <returns>returns the next code or EOF</returns>
static int16_t DecodeChar() {
    /*
     * start searching tree from the root to leaves.
     * choose node #(child[]) if input bit == 0
     * else choose #(child[]+1) (input bit == 1)
     */
    uint16_t c = child[ROOT];
    while (c < NEWTABSIZE) {
        int16_t lr;
        if ((lr = getNewBit()) < 0)
            return EOF;
        c = child[c + lr];
    }
    c -= NEWTABSIZE;
    update(c);
    return c;
}

/// <summary>
/// Decodes the position of the start of the fragment
/// sequence to emit
/// </summary>
/// <returns>the offset or EOF</returns>
static int16_t DecodePosition() {
    int16_t i;

    /* decode upper 6 bits from d_code */
    if ((i = getNew8Bits()) < 0)
        return EOF;
    int16_t c = d_code[i] << 6;

    /* input lower 6 bits directly */
    int16_t j = d_len[i >> 4];
    while (--j) {
        int16_t k;
        if ((k = getNewBit()) < 0)
            return EOF;
        i = (i << 1) | k;
    }
    return (c | (i & 0x3f));
}

/// <summary>
/// Gets the next decompressed byte from the "New"
/// advanced compression model used by teledisk
/// </summary>
/// <returns>next byte or EOF</returns>
static int getNewByte() { /* Get next decompressed byte */
    int16_t c, pos;

    for (;;) {
        if (drainCnt == 0) {
            if ((c = DecodeChar()) < 0)
                return EOF;
            if (c < 256) {
                ring[fillPos++] = (uint8_t)c;
                fillPos %= MBSIZE;
                return c;
            } else {
                if ((pos = DecodePosition()) < 0)
                    return EOF; // premature EOF
                drainPos = (fillPos + MBSIZE - pos - 1) % MBSIZE;
                drainCnt = c - 255 + THRESHOLD;
            }
        } else { // still chars from last string
            drainCnt--;
            c = ring[fillPos] = ring[drainPos];
            fillPos           = (fillPos + 1) % MBSIZE;
            drainPos          = (drainPos + 1) % MBSIZE;
            return c;
        }
    }
}

/******************************/
/* LZW (Old Advanced) decoder */
/******************************/

/*
    This code replicates the functionality of the Teledisk Old Advanced Compression
    The original appears to have been written in assembler using inline jmp targets
    to reflect state. The code here implements a much simpler model, and removes
    old common code that was only necessary for encoding

    The basic algorithm is LZW compression, using a fixed 12bit code length.
    The codes are read in blocks of 4096 (6144 bytes), each proceeded by a little endian word
    with the value #codes * 3. The internal tables are reset for each block

    The codes are stored in (#codes * 3 + 1) / 2 bytes following the size word.
    Each pair of codes is packed into a 3 byte little endian number.
    Specifically the value is (code1 + code2 >> 12).
    If there are an odd number of codes the last code is saved as a 2 byte number

    There are no special reserved codes.

*/
#define CODELEN    12
#define OLDTABSIZE (1 << CODELEN) /*size of main lzw table for 12 bit codes*/
#define ENDMARKER  0xffff

typedef struct {
    uint16_t predecessor; /*index to previous entry, if any*/
    uint8_t suffix;
} entry_t;

static entry_t table[OLDTABSIZE];

static uint16_t entryCnt; // current number of used entries
static int codesLeft;     // number of codes to left to read

static uint8_t suffix;                // byte to append
static int16_t code;                  // current code value
static uint8_t stack[OLDTABSIZE + 1]; // used to store prefix strings, in reverse order
static uint16_t sp;                   // current index in stack
static uint16_t codePart;             // used to store 2 bytes of a packed code pair
static bool lowCode;                  // used to control which 12 bit code is read

static void enter(uint16_t pred, uint8_t suff); // forward ref
static int getOldByte();                        // forward ref

/// <summary>
/// Initializes the old advanced compression
/// the main initialisation is done later in resetOld
/// it sets the correct routine for decompression
/// </summary>
static void initOld() {
    sp = codesLeft = 0;
    getByte        = getOldByte;
}

/// <summary>
/// Resets the old advanced compression model and gets the
/// number of codes to be read
/// </summary>
/// <returns>true if codes to read else false</returns>
static bool resetOld() {
    entryCnt = 0;
    lowCode  = true;
    memset(table, 0, sizeof(table));
    for (int i = 0; i < 256; i++) // mark the first 256 entries as terminators
        enter(ENDMARKER, i);

    while (codesLeft == 0) { // gobble up 0 length blocks
        int c;
        if ((codesLeft = getTd0Byte()) == EOF || (c = getTd0Byte()) == EOF)
            return false;
        codesLeft = (codesLeft + c * 256) / 3;
    }
    return true;
}

/// <summary>
/// Gets the next 12 bit code.
/// The state variable lowCode is true if lower 12 bits of a code pair
/// are being read, otherwise the upper 12 bits are used
/// codePart holds the 16 bits read while processing the low 12 bits
/// </summary>
/// <returns>12 bit code or EOF</returns>
///
static int16_t getCode() {
    int c1, c2;
    if (lowCode) {
        if ((c1 = getTd0Byte()) == EOF || (c2 = getTd0Byte()) == EOF)
            return EOF;
        codePart = c1 + c2 * 256;
    } else if ((c1 = getTd0Byte()) == EOF)
        return EOF;
    else
        codePart = ((codePart >> 12) & 0xf) + (c1 << 4);
    lowCode = !lowCode;
    codesLeft--;
    return codePart & 0xfff;
}

/// <summary>
/// Enter the code into the LZW table. As this is purely
/// for decoding there is no need
/// to hash the data entry like other sample code
/// </summary>
/// <param name="pred">The predecessor.</param>
/// <param name="suff">The new code byte to add.</param>
///
static void enter(uint16_t pred, uint8_t suff) {
    if (entryCnt < OLDTABSIZE) {
        table[entryCnt].predecessor = pred;
        table[entryCnt++].suffix    = suff;
    }
}

/// <summary>
/// Gets the next decompressed byte from the "Old"
/// advanced compression model used by teledisk
/// </summary>
/// <returns>next decompress byte, or EOF</returns>
static int getOldByte() {

    for (;;) {
        if (sp)
            return stack[--sp];

        if (!codesLeft) {
            if (!resetOld()) // premature EOF
                return EOF;
            code = getCode();
            return (suffix = table[code].suffix);
        }
        int16_t newCode = getCode();
        if (newCode < 0)
            return EOF;

        entry_t *pEntry;
        if (newCode >= entryCnt) { // KwK scenario
            stack[sp++] = suffix;
            pEntry      = &table[code];
        } else
            pEntry = &table[newCode];
        while (pEntry->predecessor != 0xffff) { // stack the predecessor, generated in reverse order
            stack[sp++] = pEntry->suffix;
            pEntry      = &table[pEntry->predecessor];
        }
        suffix = pEntry->suffix;
        enter(code, suffix);
        code = newCode;
        return suffix;
    }
}
