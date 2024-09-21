/* tdlzhuf.c  module to perform lzss-huffman decompression
   as is used in teledisk.exe


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


    11/10/02 this was working with struct tdlzhuf * passed to
    both Decode() and init_Decode() as first arg.  Save this as
    tdlzhuf1.c and then make this structure locally static and try
    to switch to unsigned shorts where it matters so works in linux.

Started with program below:
 * LZHUF.C English version 1.0
 * Based on Japanese version 29-NOV-1988
 * LZSS coded by Haruhiko OKUMURA
 * Adaptive Huffman Coding coded by Haruyasu YOSHIZAKI
 * Edited and translated to English by Kenji RIKITAKE

In summary changes by WTK:
  wrote a new conditionally compiled main()
  remove Encode() modules and arrays
  make remaing arrays and variables static to hide from external modules
  add struct tdlzhuf to provide state retension between calls to Decode()
  change from fgetc(FILE *) to read(int fp) so read
     a block at a time into an input buffer.  Now the
     Decode() routine can be called instead of read()
     by a user, ie from wteledisk.c
  change size of data elements for Linux,
     int -> short
     unsigned int -> unsigned short
*/

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef unix
#include <unistd.h>
#endif

#include "td02imx.h"
extern FILE *fpin; // global as chaining may alter

/* WTK adds top level control structure to give Decode()
   a memory between calls
*/
#define BUFSZ 512 // new input buffer

static struct tdlzhuf {
    FILE *fp; // source file stream, opened by caller
              // next four variables were local in original main()
              // we need to save these values between calls to Decode()
    unsigned short r, bufcnt, bufndx, bufpos; // string buffer
} tdctl;

/* LZSS Parameters */

#define SBSIZE    4096 // Size of string buffer
#define LASIZE    60   // Size of look-ahead buffer
#define THRESHOLD 2    // Min match for compress

#define NIL       SBSIZE /* End of tree's node  */

static unsigned char text_buf[SBSIZE + LASIZE - 1];
static short match_position, match_length, leftSon[SBSIZE + 1], rightSon[SBSIZE + 257],
    parent[SBSIZE + 1];

void InitTree(void) /* Initializing tree */
{
    for (int i = SBSIZE + 1; i <= SBSIZE + 256; i++)
        rightSon[i] = NIL; /* root */
    for (int i = 0; i < SBSIZE; i++)
        parent[i] = NIL; /* node */
}

void InsertNode(int r) /* Inserting node to the tree */
{
    int i, p, cmp;
    unsigned char *key;
    unsigned c;

    cmp         = 1;
    key         = &text_buf[r];
    p           = SBSIZE + 1 + key[0];
    rightSon[r] = leftSon[r] = NIL;
    match_length             = 0;
    for (;;) {
        if (cmp >= 0) {
            if (rightSon[p] != NIL)
                p = rightSon[p];
            else {
                rightSon[p] = r;
                parent[r]   = p;
                return;
            }
        } else {
            if (leftSon[p] != NIL)
                p = leftSon[p];
            else {
                leftSon[p] = r;
                parent[r]  = p;
                return;
            }
        }
        for (i = 1; i < LASIZE; i++)
            if ((cmp = key[i] - text_buf[p + i]) != 0)
                break;
        if (i > THRESHOLD) {
            if (i > match_length) {
                match_position = ((r - p) & (SBSIZE - 1)) - 1;
                if ((match_length = i) >= LASIZE)
                    break;
            }
            if (i == match_length) {
                if ((c = ((r - p) & (SBSIZE - 1)) - 1) < match_position) {
                    match_position = c;
                }
            }
        }
    }
    parent[r]           = parent[p];
    leftSon[r]          = leftSon[p];
    rightSon[r]         = rightSon[p];
    parent[leftSon[p]]  = r;
    parent[rightSon[p]] = r;
    if (rightSon[parent[p]] == p)
        rightSon[parent[p]] = r;
    else
        leftSon[parent[p]] = r;
    parent[p] = NIL; /* remove p */
}

/* Huffman coding parameters */

#define N_CHAR    (256 - THRESHOLD + LASIZE)
/* character code (= 0..N_CHAR-1) */
#define TABLESIZE (N_CHAR * 2 - 1) /* Size of Table */
#define ROOT      (TABLESIZE - 1)  /* root position */
#define MAX_FREQ  0x8000
/* update when cumulative frequency */
/* reaches to this value */

/*
 * Tables for encoding/decoding upper 6 bits of
 * sliding dictionary pointer
 */

/* decoder crcTable */
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

static uint16_t freq[TABLESIZE + 1]; /* cumulative freq crcTable */

/*
 * pointing parent nodes.
 * area [T..(T + N_CHAR - 1)] are pointers for leaves
 */
short prnt[TABLESIZE + N_CHAR];

/* pointing children nodes (son[], son[] + 1)*/
short son[TABLESIZE];

static int getbuf     = 0;
static uint8_t getlen = 0;



int getStreamBit() { /* get one bit */
    if (getlen == 0) {
        while (fpin && (getbuf = getc(fpin)) == EOF)
            chainTd0();
        if (!fpin)
            return -1;
        getlen = 8;
    }
    return (getbuf & (1 << --getlen)) != 0;
}

int GetStreamByte() { /* get a byte */
    uint8_t topbits = getbuf << (8 - getlen);
    while (fpin && (getbuf = getc(fpin)) == EOF)
        chainTd0();
    if (!fpin)
        return -1;
    return topbits | (getbuf >> getlen);
}


/* initialize freq tree */

void StartHuff() {
    int i, j;

    for (i = 0; i < N_CHAR; i++) {
        freq[i]             = 1;
        son[i]              = i + TABLESIZE;
        prnt[i + TABLESIZE] = i;
    }
    i = 0;
    j = N_CHAR;
    while (j <= ROOT) {
        freq[j] = freq[i] + freq[i + 1];
        son[j]  = i;
        prnt[i] = prnt[i + 1] = j;
        i += 2;
        j++;
    }
    freq[TABLESIZE] = 0xffff;
    prnt[ROOT]      = 0;
}

/* reconstruct freq tree */

void reconst() {
    short i, j, k;
    unsigned short f, span;

    /* halven cumulative freq for leaf nodes */
    j = 0;
    for (i = 0; i < TABLESIZE; i++) {
        if (son[i] >= TABLESIZE) {
            freq[j] = (freq[i] + 1) / 2;
            son[j]  = son[i];
            j++;
        }
    }
    /* make a tree : first, connect children nodes */
    for (i = 0, j = N_CHAR; j < TABLESIZE; i += 2, j++) {
        k = i + 1;
        f = freq[j] = freq[i] + freq[k];
        for (k = j - 1; f < freq[k]; k--)
            ;
        k++;
        span = j - k;

        // insert node
        (void)memmove(&freq[k + 1], &freq[k], span * sizeof(uint16_t));
        freq[k] = f;
        (void)memmove(&son[k + 1], &son[k], span * sizeof(int16_t));
        son[k] = i;
    }
    /* connect parent nodes */
    for (i = 0; i < TABLESIZE; i++) {
        if ((k = son[i]) >= TABLESIZE) {
            prnt[k] = i;
        } else {
            prnt[k] = prnt[k + 1] = i;
        }
    }
}

/* update freq tree */

void update(int c) {
    int i, j, k, l;

    if (freq[ROOT] == MAX_FREQ) {
        reconst();
    }
    c = prnt[c + TABLESIZE];
    do {
        k = ++freq[c];

        /* swap nodes to keep the tree freq-ordered */
        if (k > freq[l = c + 1]) {
            while (k > freq[++l])
                ;
            l--;
            freq[c] = freq[l];
            freq[l] = k;

            i       = son[c];
            prnt[i] = l;
            if (i < TABLESIZE)
                prnt[i + 1] = l;

            j       = son[l];
            son[l]  = i;

            prnt[j] = c;
            if (j < TABLESIZE)
                prnt[j + 1] = c;
            son[c] = j;

            c      = l;
        }
    } while ((c = prnt[c]) != 0); /* do it until reaching the root */
}

short DecodeChar() {
    int ret;
    unsigned short c;

    c = son[ROOT];

    /*
     * start searching tree from the root to leaves.
     * choose node #(son[]) if input bit == 0
     * else choose #(son[]+1) (input bit == 1)
     */
    while (c < TABLESIZE) {
        if ((ret = getStreamBit()) < 0)
            return (-1);
        c += (unsigned)ret;
        c = son[c];
    }
    c -= TABLESIZE;
    update(c);
    return c;
}

int16_t DecodePosition() {
    int val;
    uint16_t i, j, c;

    /* decode upper 6 bits from d_code */
    if ((val = GetStreamByte()) < 0)
        return (-1); // bad read
    i = (uint16_t)val;
    c = d_code[i] << 6;

    /* input lower 6 bits directly */
    j = d_len[i >> 4];

    while (--j) {
        if ((val = getStreamBit()) < 0) // bad read
            return (-1);
        i = (i << 1) | (uint16_t)val;
    }
    return (c | (i & 0x3f));
}

/* DeCompression

split out initialization code to init_Decode()

*/

void initNew() {
    tdctl.bufcnt = 0;
    StartHuff();
    memset(text_buf, ' ', SBSIZE - LASIZE);
    tdctl.r = SBSIZE - LASIZE;
}

bool decodeNew(uint8_t *buf, uint16_t len) /* Decoding/Uncompressing */
{
    short c, pos;
    int count; // was an unsigned long, seems unnecessary
    for (count = 0; count < len;) {
        if (tdctl.bufcnt == 0) {
            if ((c = DecodeChar()) < 0)
                return (count); // fatal error
            if (c < 256) {
                *(buf++)            = (uint8_t)c;
                text_buf[tdctl.r++] = (uint8_t)c;
                tdctl.r &= (SBSIZE - 1);
                count++;
            } else {
                if ((pos = DecodePosition()) < 0)
                    return (count); // premature EOF
                tdctl.bufpos = (tdctl.r - pos - 1) & (SBSIZE - 1);
                tdctl.bufcnt = c - 255 + THRESHOLD;
                tdctl.bufndx = 0;
            }
        } else { // still chars from last string
            while (tdctl.bufndx < tdctl.bufcnt && count < len) {
                c        = text_buf[(tdctl.bufpos + tdctl.bufndx) & (SBSIZE - 1)];
                *(buf++) = (uint8_t)c;
                tdctl.bufndx++;
                text_buf[tdctl.r++] = (uint8_t)c;
                tdctl.r &= (uint16_t)(SBSIZE - 1);
                count++;
            }
            // reset bufcnt after copy string from text_buf[]
            if (tdctl.bufndx >= tdctl.bufcnt)
                tdctl.bufndx = tdctl.bufcnt = 0;
        }
    }
    return count == len;
}
