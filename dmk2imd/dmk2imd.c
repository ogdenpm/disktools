/*
 * DMK2IMD - Convert .DMK images to ImageDisk .IMD format
 *
 * DMK is a popular emulator disk format, which records all track
 * data including formatting fields.
 *
 * This program is compiled using my own development tools, and will not
 * build under mainstream compilers without significant work. This source
 * code is provided for informational purposes only, and I provide no
 * support for it, technical or otherwise.
 *
 * Copyright 2007-2012 Dave Dunfield
 * All rights reserved.
 *
 * For the record: I am retaining copyright on this code, however this is
 * for the purpose of keeping a say in it's disposition. I encourage the
 * use of ideas, algorithms and code fragments contained herein to be used
 * in the creation of compatible programs on other platforms (eg: Linux).
 */
#include "imdVersion.h"
#include "utility.h"
#include <ctype.h>
#include <memory.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IS_LITTLE_ENDIAN (*(uint16_t *)"\x2\0" == 0x2)

#define A1A1A1           0xCDB4 // CRC of: 0xA1, 0xA1, 0xA1

// DMK Image file header
#pragma pack(push, 1)
struct DMK_header {
    unsigned char WP;          // Write protect
    unsigned char Tracks;      // #tracks
    unsigned short Tsize;      // Track size
    unsigned char Dflags;      // Disk flags
    unsigned char Filler[7];   // Not-used
    unsigned short Natflag[2]; // Native mode flag
} Dheader;
#pragma pack(pop)

unsigned char byteStream[0x10000]; // holds track data
unsigned P,                        // Segment read position
    Nsec,                          // Number of sectors
    crctab[256],                   // CRC generator table
    Omap[256];                     // Data offset map

char *File,         // Filename
    *Wfile,         // Write file
    *Comment,       // Comment filename
    *ptr;           // General pointer
uint8_t SD,         // Single density flag
    SD2,            // SD emulation (2 accesses)
    ForceSD,        // Force SD mode
    AMflag,         // Address mark encountered flag
    Udam,           // User defined data address mark handling
    Wstop    = 255, // Stop on warnings
    Datarate = 255, // Datarate value
    Verbose  = 255, // Allow messages
    Nmap[256],      // Numbering map
    Cmap[256],      // Cylinder map
    Hmap[256],      // Head map
    Smap[256],      // Size map
    SDmap[256],     // Density select map
    STmap[256];     // Sector type map

FILE *fpi, *fpo;

#if '\r' != 0xd
#error "Only ascii text is currently supported"
#endif

uint16_t rdStreamWord(unsigned offset) {
    return byteStream[offset] + (byteStream[offset + 1] << 8);
}

/*
 * Debug aid - dump track data memory
 *
void dump(unsigned a, unsigned l)
{
    int c;
    unsigned i, j;

    for(i=0; i < l; i += 16) {
        printf("\n%04x", i);
        for(j=0; j < 16; ++j) {
            if(!(j & 3)) putc(' ', stdout);
            printf(" %02x", byteStreak[a+j]); }
        printf("   ");
        for(j=0; j < 16; ++j) {
            c = byteStream[a++];
            putc( ((c < ' ') || (c > 0x7E)) ? '.' : c, stdout); } }
} */

/*
 * Get a byte from the DMK track with SD duplication if needed
 */
unsigned char A, B;
unsigned char getbyte(void) {
    unsigned char A, B;

    A = byteStream[P++];
    if (SD2 && A != (B = byteStream[P++]))
        fatal("SD data mismatch at %04x %02x!=%02x", P - 2, A, B);
    return A;
}

/*
 * Get a word from the DMK track with byte reversal & SD dup if needed
 */
unsigned getword(void) {
    unsigned l, h;
    h = getbyte();
    l = getbyte();
    return (h << 8) | l;
}

/*
 * CRC using reversed byte/bit ordering
 */
unsigned crc(unsigned length, unsigned crc) {
    do {
        crc = (crc << 8) ^ crctab[((crc >> 8) ^ getbyte()) & 255];
    } while (--length);
    return crc;
}

/*
 * Test for block of data being the same in local segment
 */
int issame(unsigned char *p, unsigned l) {
    unsigned char c;
    c = *p++;
    while (--l) {
        if (*p++ != c)
            return 0;
    }
    return 255;
}

/*
 * Test for block of data being the same in external segment
 */
int isxsame(unsigned p, unsigned l) {
    unsigned char c;
    if (SD2)
        l *= 2;
    c = byteStream[p++];
    while (--l) {
        if (byteStream[p++] != c)
            return 0;
    }
    return 255;
}

/*
 * Display DMK header information to file
 */
void showheader(FILE *fp) {
    unsigned char f;
    f = Dheader.Dflags;
    fprintf(fp, "Tracks:%ux%u %cS%cD", Dheader.Tracks, Dheader.Tsize, (f & 0x10) ? 'S' : 'D',
            (f & 0x40) ? 'S' : 'M');
    if (f & 0x80)
        fputs(" Ign-D", fp);
    if (Dheader.WP)
        fputs(" WP", fp);
    putc('\n', fp);
}


char const help[] = { "Usage: %s filename[.dmk] [options]\n\n"
                "Options:\n"
                "  /C[filename]    - insert Comment record\n"
                "  /H              - force High density    (500kbps)\n"
                "  /L              - force Low  density    (250kbps)\n"
                "  /S              - force Single-density image\n"
                "  /UD             - allow User defined DAM - treat as Deleted\n"
                "  /UN             - allow User defined DAM - treat as Normal\n"
                "  /W              - continue after Warning\n"
                "  O=file[.imd]    - specify output file   [filename.imd]\n" };

/*
 * Main program
 */
// modified handling of options to support 2 char values portably
#define OPT(a, b) (((a) << 8) | (b))
enum {
    OPTsC = OPT('/', 'C'),
    OPTsL = OPT('/', 'L'),
    OPTsH = OPT('/', 'H'),
    OPTsS = OPT('/', 'S'),
    OPTsW = OPT('/', 'W'),
    OPTsU = OPT('/', 'U'),
    OPTOe = OPT('O', '='),
};

int main(int argc, char *argv[]) {
    int c;
    unsigned short i, j, k, s, t, p, so;

    chkStdOptions(argc, argv);

    // Process command line arguments modified for portability
    for (i = 1; i < argc; ++i) {
        ptr      = argv[i];
        char ch1 = toupper(*ptr++); // avoid sequence point risk
        char ch2 = toupper(*ptr++);

        switch (OPT(ch1, ch2)) {
        case OPTsC:
            Comment = ptr;
            continue;
        case OPTsL:
            Datarate = 2;
            continue;
        case OPTsH:
            Datarate = 0;
            continue;
        case OPTsS:
            ForceSD = 255;
            continue;
        case OPTsW:
            Wstop = 0;
            continue;
        case OPTOe:
            Wfile = makeFilename(ptr, ".imd", false);
            continue;
        case OPTsU:
            switch (toupper(*ptr)) {
            case 'N':
                Udam = 255;
                continue;
            case 'D':
                Udam = 15;
                continue;
            }
        }
        if (File)
            usage("Multiple files specified");
        File = ptr - 2;
    }

    if (!File)
        usage("No file specified");

    // If comment file specified, preload it into buffer
    j = k = 0;
    if (Comment && *Comment) {
        if ((fpi = fopen(Comment, "rt")) == NULL)
            fatal("can't open %s\n", Comment);
        while ((c = getc(fpi)) != EOF) {
            if (c == '\n')
                byteStream[j++] = '\r';
            if (c != '\r')
                byteStream[j++] = c;
        }
        fclose(fpi);
    }

    //  IOB_size = 4096;

    // Open files
    char *dmkFile = makeFilename(File, ".dmk", false);
    if ((fpi = fopen(dmkFile, "rb")) == NULL)
        fatal("can't open %s\n", dmkFile);
    if (!Wfile)
        Wfile = makeFilename(File, ".imd", true);
    if ((fpo = fopen(Wfile, "wb")) == NULL)
        fatal("can't create %s\n", Wfile);

    // Read header
    if (fread(&Dheader, sizeof(Dheader), 1, fpi) != 1) {
        fatal("can't read header from %s\n", dmkFile);
    }
    if (!IS_LITTLE_ENDIAN) { // on big endian we need to fix the shorts
        Dheader.Tsize = (Dheader.Tsize << 8) + (Dheader.Tsize >> 8);
        // current Natflag isn't used so no need to fix
    }

    if (Verbose)
        showheader(stdout);

    // Generate IMD comment
    fputs("IMD DMK ", fpo);
    showheader(fpo);
    if (Comment) {
        if (j) { // Already preloaded
            for (i = 0; i < j; ++i)
                putc(k = byteStream[i], fpo);
            if (k == '\r')
                putc(k = '\n', fpo);
        } else { // Prompt for input
            printf("Enter image file comment - ^Z to end:\n");
            int c;

            while ((c = getc(stdin)) != EOF) {
                if (c == '\n')
                    putc('\r', fpo);
                putc(k = i, fpo);
            }
        }
        if (k != '\n') {
            putc('\r', fpo);
            putc('\n', fpo);
        }
    }
    putc(0x1A, fpo); // Ascii EOF - terminate comment

    if (ForceSD)
        Dheader.Dflags |= 0x40;

    // If data rate not specified, make a "guess" based on tracksize
    if (Datarate > 2) {
        i = Dheader.Tsize;
        if (Dheader.Dflags & 0x40) // Single-density
            i *= 2;                // Double the bits
        Datarate = (i < 6500) ? 2 : 0;
        if (Verbose)
            printf("Assuming %ukbps data rate.\n", Datarate ? 250 : 500);
    }

    // Build the high-speed CRC table
    for (i = 0; i < 256; ++i) {
        k = i << 8;
        for (j = 0; j < 8; ++j)
            k = (k << 1) ^ ((k & 0x8000) ? 0x1021 : 0);
        crctab[i] = k;
    }

    if ((Dheader.Dflags & 0x10) == 0) // if double sided read twice as many tracks
        Dheader.Tracks *= 2;

    for (t = 0; t < Dheader.Tracks; ++t) {
        if (fread(byteStream, 1, Dheader.Tsize, fpi) != Dheader.Tsize)
            fatal("%u/ Unexpected EOF in DMK image", t);

        Nsec = so = 0;
        /* some dmk files have incorrect IDAM offsets, probably due to a bad conversion program
           if the first IDAM offset doesn't point to an 0xFE, then look for the first 0XFE and
           calculate a bias to remove from the specified IDAM to fix the entries
        */
        uint16_t bias = 0;
        s             = rdStreamWord(0) & 0x3fff;
        if (byteStream[s] != 0xFE) {
            for (i = 128; i < 512; i++) {
                if (byteStream[i] == 0xFE)
                    break;
            }
            if (i < 512)
                bias = s - i;
        }
        while ((Nsec < 64) && (s = rdStreamWord(so))) { // fix there are only up to 64 IDAMs
            so += 2;
            if ((SDmap[Nsec] = SD2 = SD = (s & 0x8000) ? 0 : 255)) { // SD
                if (Dheader.Dflags & 0xC0)                           // Physical format is SD
                    SD2 = 0;
            }                            // No need to emulate
            P = p = (s & 0x3FFF) - bias; // DMK address mark offset
            if (p == 128 || p == 0) {    // should end in 0 but examples exist with offset 128 used
                break;                   // No more sectors
            }
            if ((p < 128) || (p >= Dheader.Tsize)) {
                warn("%u/%u Sector offset %04x out of range", t, Nsec, p);
                continue;
            } // NoSector
            if (byteStream[P] != 0xFE) {
                warn("%u/%u Invalid ID address mark: %02x", t, Nsec, byteStream[s]);
                continue;
            } // NoSector

            // Check header CRC & Extract data
            i = crc(5, SD ? -1 : A1A1A1);
            if (getword() != i) {
                warn("%u/%u ID CRC error", t, Nsec);
                continue;
            } // NoSector

            // Extract ID fields
            P = p;                   // Reset input pointer
            getbyte();               // Skip address mark
            Cmap[Nsec]  = getbyte(); // Get Cylinder
            Hmap[Nsec]  = getbyte(); // Get Head
            Nmap[Nsec]  = getbyte(); // Get Number
            Smap[Nsec]  = getbyte(); // Get Size
            STmap[Nsec] = 1;         // Assume normal data
            getword();               // Clear CRC

            // Validate data block
            k = 50;
            do {
                i = getbyte();
            } while ((i < 0xf8 || 0xfb < i) && --k);
            if (!k) {
                warn("%u/%u Did not find DAM", t, Nsec);
                Omap[Nsec] = 0; // Data unavailable
            } else {
                switch (i) {
                case 0xF9: // User defined ...
                case 0xFA: // Data marks
                    if (!Udam)
                        fatal("%u/%u User defined DAM %02x", t, Nsec, i);
                    if (Udam & 0xF0)
                        break;
                    // FALLTHROUGH
                case 0xF8:
                    STmap[Nsec] = 3; // Deleted data mark
                    // FALLTHROUGH
                case 0xFB:
                    break;
                } // Normal data mark

                // Check data CRC
                Omap[Nsec] = P; // Record offset
                P -= (SD2 ? 2 : 1);
                i = crc((128 << Smap[Nsec]) + 1, SD ? -1 : A1A1A1);
                if (getword() != i) {
                    warn("%u/%u Data CRC error", t, Nsec);
                    STmap[Nsec] += 4;
                }
            }
            ++Nsec;
        }

        if (!Nsec) // No sectors on this track
            continue;

        // Check conditions IMD cannot currently handle (due to 765 limitation)
        if (!issame(SDmap, Nsec))
            fatal("%u/%u PC cannot format mixed density within a track", t, Nsec);
        if (!issame(Smap, Nsec))
            fatal("%u/%u PC cannot format mixed sector size within a track", t, Nsec);

        // Generate an IMD track record
        putc((SD ? 0x00 : 0x03) + Datarate, fpo); // Encoding type
        if (Dheader.Dflags & 0x10) {              // Single-sided
            i = t;                                // Track index as is
            j = 0;
        } // Head always even
        else {
            i = t >> 1; // Track index / 2
            j = t & 1;
        }                                        // Head = even/odd
        if ((*Hmap != j) || !issame(Hmap, Nsec)) // Head map required
            j |= 0x40;
        if ((*Cmap != i) || !issame(Cmap, Nsec)) // Cylinder map required
            j |= 0x80;
        putc(i, fpo);               // Cylinder
        putc(j, fpo);               // Head
        putc(Nsec, fpo);            // # sectors
        putc(*Smap, fpo);           // Sector size
        fwrite(Nmap, 1, Nsec, fpo); // Sector numbering map
        if (j & 0x80)               // Cylinder numbering map
            fwrite(Cmap, 1, Nsec, fpo);
        if (j & 0x40) // Head numbering map
            fwrite(Hmap, 1, Nsec, fpo);

        // Generate the sector data records
        s = (unsigned)128 << *Smap; // Actual sector size
        for (i = 0; i < Nsec; ++i) {
            SD2 = SDmap[i];            // Single density
            if (Dheader.Dflags & 0xC0) // Physical format is SD
                SD2 = 0;               // No need to emulate
            if (!(P = Omap[i])) {      // Data unavailable
                putc(0x00, fpo);
                continue;
            }
            if (isxsame(P, s)) { // Compressed
                putc(STmap[i] + 1, fpo);
                putc(getbyte(), fpo);
            } else { // Normal data
                putc(STmap[i], fpo);
                for (j = 0; j < s; ++j)
                    putc(getbyte(), fpo);
            }
        }
    }

    fclose(fpo);
    fclose(fpi);
}
