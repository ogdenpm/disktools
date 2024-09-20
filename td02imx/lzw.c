
#include <memory.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
    This code replicates the functionality of the Teledisk Old Advanced Compression
    The original appears to have been written in assembler using inline jmp targets
    to reflect state. Here this is synthesized by using a simple state variable.

    Complexities in the original code relating to common code used for encoding
    have been removed.

    The basic algorithm is LZW compression, using a fixed 12bit code length.
    The codes are  read in chunks, each proceeded by a little endian word with the value
    #codes * 3. The internal tables are reset for each block

    The codes are stored in (#codes * 3 + 1) / 2 bytes following the size word.
    Each pair of codes is packed  into a 3 byte little endian number.
    Specifically the value is (code1 + code2 >> 12).
    If there are an odd number of codes the last code is saved as a 2 byte number

    There are no special reserved codes.

*/
#define CODELEN    12
#define TABLE_SIZE (1 << CODELEN) /*size of main lzw table for 12 bit codes*/
#define ENDMARKER  0xffff

enum { INITIAL, DESTACK, ENTERNEW };

typedef struct {
    uint16_t predecessor; /*index to previous entry, if any*/
    uint8_t suffix;
} entry_t;

static entry_t table[TABLE_SIZE];
static entry_t *pEntry;

static uint16_t entryCnt; // current number of used entries

static bool refill; // if true force refile inBuf and reset LZW

static uint8_t inBuf[8192]; // read in block and current
static uint16_t inIdx;
static uint16_t codesLeft; // number of codes to left to read

static uint8_t suffix;      // byte to append
static uint16_t code;       // current code value
static uint8_t stack[8192]; // used to store prefix strings, in reverse order
static uint16_t sp;         // current index in oldStack
static bool highCode;       // used to control whether lower / upper packed 12 bit code is read

extern FILE *fpin; // file to read
// extern char *filename;   // only needed for multi-volume sets

static uint16_t enter(uint16_t pred, uint8_t suff);

void chainTd0() {
    fclose(fpin);
    fpin = NULL;
}

/*
 */
/// <summary>
/// this reads data from the file stream fpin.
/// It is separated out to facilitate support for multi-volume sets at a later date.
/// For multi-volume sets, chaining to another file will modify filename and fpin
/// </summary>
/// <param name="buf">The target buffer to copy to.</param>
/// <param name="len">The number of bytes to copy.</param>
/// <returns>true for successful read/returns>
///
bool readIn(uint8_t *buf, uint16_t len) {
    int actual;
    while (fpin && (actual = (int)fread(buf, 1, len, fpin)) != len) {
        chainTd0();
        buf += actual;
        len -= actual;
    }
    return fpin != NULL;
}

/// <summary>
/// read a little endian word from a buffer
/// </summary>
/// <param name="p">Pointer to the buffer.</param>
/// <returns>Word value as uint16_t</returns>
///
inline uint16_t leword(uint8_t *p) {
    return p[0] + p[1] * 256;
}

/* LZW routines */

/// Initialize the lzw and physical translation tables and key decoder parameters.
/// </summary>
///
void oldReset() {
    entryCnt = codesLeft = 0;
    memset(table, 0, sizeof(table));
    for (int i = 0; i < 256; i++) // mark the first 256 entries as terminators
        enter(ENDMARKER, i);
}

/// <summary>
/// Reads the next block of codes.
/// The codes are prefixed by a little endian word which is the number of codes * 3
/// Following this are the 12bit codes which are stored compacted in pairs to form
/// a 3 byte little endian word.
/// </summary>
/// <returns>true for successful read</returns>
///
static bool readBlk() {
    uint8_t rawCodeSize[2];

    highCode = false; // tracks toggling of first / second code in the 3 byte number
    inIdx    = 0;

    if (!readIn(rawCodeSize, 2) || !readIn(inBuf, (leword(rawCodeSize) + 1) >> 1))
        return false;
    codesLeft = leword(rawCodeSize) / 3;
    return true;
}

/// <summary>
/// Gets the next 12bit code.
/// </summary>
/// <returns>21 bit code</returns>
///
static uint16_t getCode() {
    uint16_t code = leword(inBuf + inIdx++);
    if (highCode) {
        code >>= 4;
        inIdx++; // skip to 3 byte boundary
    }
    highCode = !highCode;
    codesLeft--;
    return code & 0xfff;
}

/// <summary>
/// Enter the code into the LZW table
/// </summary>
/// <param name="pred">The predecessor.</param>
/// <param name="suff">The new code byte to add.</param>
///
static uint16_t enter(uint16_t pred, uint8_t suff) {
    if (entryCnt < TABLE_SIZE) {
        table[entryCnt].predecessor = pred;
        table[entryCnt].suffix      = suff;
        return entryCnt++;
    }
    return entryCnt;
}

/// <summary>
/// the main old Advanced compression decoding routine.
/// This uses a simple state model to support continuing decoding on consecutive calls
/// </summary>
/// <param name="buf">The target buffer.</param>
/// <param name="len">The length to copy.</param>
/// <returns>actual length copied.</returns>
///
int getOldByte() {
    static uint8_t state = INITIAL;
    static uint16_t newCode;
    static uint8_t newSuffix;

    for (;;) {
        if (state == INITIAL) {
            if (codesLeft == 0) {
                oldReset(); // will set refill for next  iteration of loop
                if (!readBlk()) // premature EOF
                    return EOF;
                code   = getCode();
                return (suffix = table[code].suffix);
            } else {
                newCode = getCode();
                if (newCode >= entryCnt) { // KwKwK scenario
                    newSuffix = suffix;
                    pEntry    = &table[code];
                } else
                    pEntry = &table[newCode];

                while (pEntry->predecessor !=
                       0xffff) { // stack the predecessor, generated in reverse order
                    stack[sp++] = pEntry->suffix;
                    pEntry      = &table[pEntry->predecessor];
                }
                state = DESTACK;
                return (suffix = pEntry->suffix);
            }
        } else if (sp)
                return stack[--sp];
        else {
            state = INITIAL;
            if (newCode >= enter(code, suffix)) { // handle the KwKwK scenario
                code = newCode;
                return (suffix = newSuffix);
            }
            code = newCode;
        }
    }
}

bool oldAdv(uint8_t *buf, uint16_t len) {
    int c;
    while (len--) {
        if ((c = getOldByte()) == EOF)
            return false;
        *buf++ = (uint8_t)c;
    }
    return true;
}