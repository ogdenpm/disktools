
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
static int16_t code;        // current code value
static uint8_t stack[8192]; // used to store prefix strings, in reverse order
static uint16_t sp;         // current index in oldStack
static bool lowCode;       // used to control whether lower / upper packed 12 bit code is read
static uint16_t codePart;

extern FILE *fpin; // file to read
// extern char *filename;   // only needed for multi-volume sets

static void enter(uint16_t pred, uint8_t suff);

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


int readTd0Byte() {
    int c;
    while (fpin && (c = getc(fpin)) == EOF)
        chainTd0();
    return fpin ? c : EOF;
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
void initOld() {
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

    lowCode = true; // true if reading low code in 3 byte number

    if (!readIn(rawCodeSize, 2))
        return EOF;
    codesLeft = leword(rawCodeSize) / 3;
    return true;
}

/// <summary>
/// Gets the next 12bit code.
/// </summary>
/// <returns>21 bit code</returns>
///
static int16_t getCode() {
    int c1, c2;
    if (lowCode) {
        if ((c1 = readTd0Byte()) == EOF || (c2 = readTd0Byte()) == EOF)
            return EOF;
        codePart = c1 + c2 * 256;
    } else if ((c1 = readTd0Byte()) == EOF)
        return EOF;
    else
        codePart = ((codePart >> 12) & 0xf) + (c1 << 4);
    lowCode = !lowCode;
    codesLeft--;
    return codePart & 0xfff;
}

/// <summary>
/// Enter the code into the LZW table
/// </summary>
/// <param name="pred">The predecessor.</param>
/// <param name="suff">The new code byte to add.</param>
///
static void enter(uint16_t pred, uint8_t suff) {
    if (entryCnt < TABLE_SIZE) {
        table[entryCnt].predecessor = pred;
        table[entryCnt++].suffix      = suff;
    }
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

    for (;;) {
        if (sp)
            return stack[--sp];

        if (codesLeft == 0) {
            initOld();
            if (!readBlk()) // premature EOF
                return EOF;
            if (codesLeft) { // avoid the case of a zero length block
                code = getCode();
                return (suffix = table[code].suffix);
            }
        } else {
            int16_t newCode = getCode();
            if (newCode < 0)
                return EOF;
            if (newCode >= entryCnt) { // KwKwK scenario
                stack[sp++] = suffix;
                pEntry      = &table[code];
            } else
                pEntry = &table[newCode];
            while (pEntry->predecessor !=
                   0xffff) { // stack the predecessor, generated in reverse order
                stack[sp++] = pEntry->suffix;
                pEntry      = &table[pEntry->predecessor];
            }
            suffix = pEntry->suffix;
            enter(code, suffix);
            code = newCode;
            return suffix;
        }
    }
}

bool decodeOld(uint8_t *buf, uint16_t len) {
    int c;
    while (len--) {
        if ((c = getOldByte()) == EOF)
            return false;
        *buf++ = (uint8_t)c;
    }
    return true;
}