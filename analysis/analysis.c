#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <limits.h>
#include <memory.h>
#include <string.h>
#include "flux.h"
#include "dpll.h"

void* xmalloc(size_t size) {
    void* ptr;
    if (!(ptr = malloc(size))) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return ptr;
}

   



unsigned flip(uint32_t pattern)	{
    uint8_t flipped = 0;

    for (int i = 0; i < 8; i++, pattern >>= 1)
        flipped = (flipped << 1) + (pattern & 1);
    return flipped;

}

int getByte(bool collect) {
    int bitCnt = 0;
    int pattern = 0;
    int cbit, dbit;;
    while (bitCnt != 8) {
        if ((cbit = getBit()) < 0)
            return -1;
        if ((dbit = getBit()) < 0)
            return -1;
        if ((collect = collect || dbit)) {
            pattern = (pattern >> 1) + (cbit ? 0x8000 : 0) + (dbit ? 0x80 : 0);
            bitCnt++;
        }
    }
    return pattern;
}

void readFlux() {
    int val;
    char ascii[17];
    int sector;

    for (int i = 0; (sector = seekBlock(i)) >= 0; i++) {
        resetPLL(PRESYNC_TOLERANCE, POSTSYNC_TOLERANCE);

        if (!clockSync())
            printf("Sector failed to sync\n");
        else {
            printf("Sector %d:", sector);
            sector = (sector + 1) % 32;
            if ((val = getByte(false)) < 0) {
                printf(" failed to get data\n");
            } else {
                printf(" track=%d\n", (val >> 1) & 0x7f);
                for (int cnt = 0; cnt < 128 && val >= 0; cnt += 16) {
                    memset(ascii, 0, 17);
                    for (int i = 0; i < 16; i++) {
                        if ((val = getByte(true)) < 0) {
                            break;
                        }
                        printf("%c%02X", (val & 0xff00) != 0xff00 ? 'x' : ' ', val & 0xff);
                        ascii[i] = (val & 0x7f) >= ' ' ? val & 0x7f : '.';
                    }
                    printf(" | %s\n", ascii);
                    if (val < 0) {
                        puts("premature end of sector");
                        break;
                    }
                }
            }
            if ((val = getByte(true)) >= 0) {
                printf("CRC1:%c%02X", (val & 0xff00) != 0xff00 ? 'x' : ' ', val & 0xff);
                if ((val = getByte(true)) >= 0)
                    printf(" CRC2:%c%02X", (val & 0xff00) != 0xff00 ? 'x' : ' ', val & 0xff);
                putchar('\n');
            }
        }
    }
}


void  main(int argc, char** argv) {
    FILE* fp;
    if (argc != 2 || (fp = fopen(argv[1], "rb")) == NULL) {
        printf("usage: analysis fluxfile\n");
    } else {
        fseek(fp, 0L, SEEK_END);
        size_t fileSize = ftell(fp);
        fseek(fp, 0L, SEEK_SET);

        uint8_t *fluxBuf = (uint8_t*)xmalloc(fileSize);
        if (fread(fluxBuf, 1, fileSize, fp) != fileSize)
            fprintf(stderr, "error reading file\n");
        else {
            loadFlux(fluxBuf, fileSize);
            readFlux();
        }
        fclose(fp);
        free(fluxBuf);
    }


}