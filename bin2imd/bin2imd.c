/*
 * Program to convert raw binary data into .IMD image format.
 *
 * This program is compiled using my own development tools, and will not
 * build under mainstream compilers without significant work. It is being
 * provided for informational purposes only, and I provide no support for
 * it, technical or otherwise.
 *
 * Copyright 2005-2012 Dave Dunfield
 * All rights reserved.
 *
 * For the record: I am retaining copyright on this code, however this is
 * for the purpose of keeping a say in it's disposition. I encourage the
 * use of ideas, algorithms and code fragments contained herein to be used
 * in the creation of compatible programs on other platforms (eg: Linux).
 */
#include "utility.h"
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "imdVersion.h"
#if '\r' != 0xd
#error "Currently only ascii text is supported"
#endif



char *ptr,                // General parse pointer
    *ptr1;                // General pointer
uint8_t Twoside,          // Double sided indicator
    Verbose,              // Display detail information
    Fill,                 // Fill value
    Compress = 255;       // Compress sectors
char *Ifile,              // Input filename
    *Ofile,               // Output filename
    *Ffile,               // Format file
    Buffer[32768];        // Comment/track buffer
unsigned Day,             // Current day
    Month,                // Current month
    Year,                 // Current year
    Hour,                 // Current hour
    Minute,               // Current minite
    Second,               // Current second
    Fmtnext = UINT_MAX,   // Next format record
    Cylinder,             // Current cylinder
    Cylinders = UINT_MAX, // Number of cylinders
    Cmtsize;              // Comment size

struct SIDE {
    unsigned Mode;           // Data-rate/Density
    unsigned Ssize;          // Sector size (encoded)
    unsigned SSize;          // Sector size (plain)
    unsigned Nsec;           // Number of sectors/track
    unsigned CMsize;         // Cylinder numbering map size
    unsigned HMsize;         // Head numbering map size
    unsigned char Smap[256]; // Sector numbering map
    unsigned char Cmap[256]; // Cylinder numbering map
    unsigned char Hmap[256]; // Head numbering map
} Side0, Side1;

FILE *fpi, // Binary file in
    *fpo,  // IMD file out
    *fpf;  // Detail record fp

/*
 * Obtain a number from the command line
 */
unsigned char get_num(unsigned *value, unsigned l, unsigned h) {
    unsigned c, v, b;
    b = 10;
    switch (*ptr) {
    case '$':
        b = 16;
        goto l1;
    case '@':
        b = 8;
        goto l1;
    case '%':
        b = 1;
    l1:
        ++ptr;
    }
    v = 0;
    c = *ptr;
    for (;;) {
        if ((c >= '0') && (c <= '9'))
            c -= '0';
        else if ((c >= 'A') && (c <= 'F'))
            c -= ('A' - 10);
        else if ((c >= 'a') && (c <= 'f'))
            c -= ('a' - 10);
        else
            c = 65535;
        if (c >= b)
            fatal("Bad digit '%c'", *ptr);
        v = (v * b) + c;
        switch (c = *++ptr) {
        case ' ':
        case '\t':
        case ',':
        case '-':
        case '.':
            ++ptr;
            // FALLTHROUGH
        case 0:
            if ((v < l) || (v > h))
                fatal("Value %u out of range %u-%u", v, l, h);
            *value = v;
            return c;
        }
    }
}

/*
 * Check a size value and issue error if it exceeds set limit
 */
void checksize(unsigned v, unsigned s) {
    if (v > s)
        fatal("Too large (exceeds %u bytes)", s);
}

/*
 * Obtain a map value from the command line
 */
void get_map(unsigned char map[], unsigned *size, unsigned l, unsigned h) {
    unsigned c, i, j, s;
    s = 0;
top:
    c = get_num(&i, l, h);
    checksize(s, 255);
    map[s++] = i;
next:
    switch (c) {
    case ',':
        goto top;
    case '-':
        c = get_num(&j, l, h);
        if (j > i) {
            while (i < j) {
                checksize(s, 255);
                map[s++] = ++i;
            }
        } else {
            while (i > j) {
                checksize(s, 255);
                map[s++] = --i;
            }
        }
        goto next;
    case '.':
        c = get_num(&j, 0, 256);
        while (--j) {
            checksize(s, 255);
            map[s++] = i;
        }
        goto next;
    }
    *size = s;
}

/*
 * Check a map for duplicate entries
 */
void check_dup(unsigned char map[], unsigned size) {
    unsigned i, j;
    unsigned char flags[256];
    memset(flags, 0, sizeof(flags));
    for (i = 0; i < size; ++i) {
        if (flags[j = map[i]])
            fatal("Duplicate entry %u", j);
        flags[j] = 255;
    }
}

/*
 * Parse a (command-line/file) option value
 */
// modified handling of options to support 2 char values portably
#define OPT(a, b) (((a) << 8) | (b))
enum {
    OPTs1 = OPT('/', '1'),
    OPTs2 = OPT('/', '2'),
    OPTsV = OPT('/', 'V'),
    OPTsC = OPT('/', 'C'),
    OPTsU = OPT('/', 'U'),
    OPTFe = OPT('F', '='),
    OPTNe = OPT('N', '='),
    OPTCe = OPT('C', '='),
    OPTDM = OPT('D', 'M'),
    OPTSS = OPT('S', 'S'),
    OPTSM = OPT('S', 'M'),
    OPTCM = OPT('C', 'M'),
    OPTHM = OPT('H', 'M'),
};

int option(char flag) {
    unsigned i, j;
    struct SIDE *s;
    char *fname;
    uint8_t f;
    FILE *fp;
#define MAXSSIZE 7 // 8192 byte sectors

    ptr1                     = ptr;
    char ch1                 = toupper(*ptr++); // avoid sequence point issues
    char ch2                 = toupper(*ptr++);
    switch (i = OPT(ch1, ch2)) {
    case OPTs1:
    case OPTs2:
        Twoside = i == OPTs1 ? 0 : 255;
        return 0;
    case OPTsV:
        if (*ptr == '0' || *ptr == '1')
            Verbose = *ptr++ == '0' ? 0 : 255;
        else
            Verbose = 15;
        return 0;
    case OPTsC:
    case OPTsU:
        Compress = i == OPTsC ? 255 : 0;
        return 0;
    case OPTFe:
        get_num(&i, 1, 255);
        Fill = i;
        return 0;
    case OPTNe:
        if (flag)
            fatal("N= not allowed in track records");
        get_num(&Cylinders, 1, 256);
        return 0;
    case OPTCe:
        if (flag)
            fatal("C= not allowed in track records");
        if (*ptr == '@') {
            fname = makeFilename(ptr + 1, ".txt", false);
            fp    = fopen(fname, "rt");     // read in as text. Remove spurious '\r' 
            int c;
            while ((c = getc(fp)) != EOF) {
                if (c == '\n') {
                    checksize(Cmtsize, sizeof(Buffer) - 1);
                    Buffer[Cmtsize++] = '\r';
                }
                if (c != '\r') {    // Linux might see \r\n so skip \r
                    checksize(Cmtsize, sizeof(Buffer) - 1);
                    Buffer[Cmtsize++] = (char)c;
                }
            }
            fclose(fp);
            ptr = "";
        } else {
            // changed old style processing to assume user had quoted/escaped the text
            // as necessary
            checksize(Cmtsize + (unsigned)strlen(ptr) + 2, sizeof(Buffer) - 1);
            while (*ptr)
                Buffer[Cmtsize++] = *ptr++;
            Buffer[Cmtsize++] = '\r';
            Buffer[Cmtsize++] = '\n';
        }
        return 0;
    }
    s = &Side0;
    f = 255;
    switch (*ptr) {
    case '1':
        s       = &Side1;
        Twoside = 255;
        // FALLTHROUGH
    case '0':
        f = 0;
        ++ptr;
    }
    if (*ptr++ == '=')
        switch (i) {
        case OPTDM:
            get_num(&j, 0, 5);
            s->Mode = j;
            if (f)
                Side1.Mode = j;
            return 0;
        case OPTSS:
            get_num(&j, 0, 128U << MAXSSIZE);
            for (i = 0; i <= MAXSSIZE; ++i) {
                if (j == (128U << i)) {
                    s->Ssize = i;
                    s->SSize = j;
                    if (f) {
                        Side1.Ssize = Side0.Ssize;
                        Side1.SSize = Side0.SSize;
                    }
                    return 0;
                }
            }
            fatal("Bad sector size");
        case OPTSM:
            get_map(s->Smap, &s->Nsec, 0, 255);
            if (f)
                memcpy(Side1.Smap, Side0.Smap, Side1.Nsec = Side0.Nsec);
            return 0;
        case OPTCM:
            get_map(s->Cmap, &s->CMsize, 0, 255);
            if (f)
                memcpy(Side1.Cmap, Side0.Cmap, Side1.CMsize = Side0.CMsize);
            return 0;
        case OPTHM:
            get_map(s->Hmap, &s->HMsize, 0, 1);
            if (f)
                memcpy(Side1.Hmap, Side0.Hmap, Side1.HMsize = Side0.HMsize);
            return 0;
        }
    return 255;
}

/*
 * Display a map value
 */
void showmap(char *title, unsigned char Map[], unsigned size) {
    unsigned i, j;
    printf("%5s:", title);
    for (i = j = 0; i < size; ++i) {
        if (++j > 18) {
            printf("\n     ");
            j = 1;
        }
        printf("%4u", Map[i]);
    }
    putc('\n', stdout);
}

/*
 * Validate settings before generating image
 */
void validate(unsigned side) {
    unsigned i, j;
    struct SIDE *s;
    unsigned char flags[256];

    if (side) {
        s    = &Side1;
        ptr1 = "Side1";
    } else {
        s    = &Side0;
        ptr1 = "Side0";
    }
    if (Verbose) {
        printf("%3u/%u:  Mode:%u  Sectors:%u  Ssize:%u/%u\n", Cylinder, side, s->Mode, s->Nsec,
               s->Ssize, s->SSize);
        if (Verbose & 0xF0) {
            showmap("Smap", s->Smap, s->Nsec);
            if (s->HMsize)
                showmap("Hmap", s->Hmap, s->HMsize);
            if (s->CMsize)
                showmap("Cmap", s->Cmap, s->CMsize);
        }
    }

    if (s->Mode > 5)
        fatal("Data Mode must be defined.");

    if (!s->SSize)
        fatal("Sector Size must be defined");

    if (!s->Nsec)
        fatal("At least 1 sector must be defined");

    if ((s->CMsize && (s->CMsize != s->Nsec)) || (s->HMsize && (s->HMsize != s->Nsec)))
        fatal("Cylinder/Head maps must match sector map size");

    memset(flags, 0, sizeof(flags));
    for (i = 0; i < s->Nsec; ++i) {
        if (flags[j = s->Smap[i]])
            fatal("Duplicate sector map entry %u", j);
        flags[j] = 255;
    }
    ptr1 = 0;
}

/*
 * Skip to next non-blank in input
 */
unsigned char skip(void) {
    while (isspace(*ptr))
        ++ptr;
    return *ptr;
}

/*
 * Read disk format data from command option file
 */
void read_format(void) {
again:
    if (!fgets(ptr = Ffile, sizeof(Ffile) - 1, fpf)) {
        Fmtnext = -1;
        return;
    }
    switch (skip()) {
    case ';':
    case 0:
        goto again;
    }
    get_num(&Fmtnext, 0, 65535);
}

/*
 * High-speed test for compressable sector (all bytes same value)
 */
int issame(unsigned size) {
    for (unsigned i = 1; i < size; i++)
        if (Buffer[0] != Buffer[i])
            return 0;
    return 1;
}

/*
 * Write a .IMD format track record
 */
void write_track(unsigned char head) {
    unsigned i, j, size;
    struct SIDE *s;

    s = head == 0 ? &Side0 : &Side1;
    if (s->CMsize)
        head |= 0x80;
    if (s->HMsize)
        head |= 0x40;

    putc(s->Mode, fpo);
    putc(Cylinder, fpo);
    putc(head, fpo);
    putc(s->Nsec, fpo);
    putc(s->Ssize, fpo);

    fwrite(s->Smap, 1, s->Nsec, fpo);
    if (s->CMsize)
        fwrite(s->Cmap, 1, s->CMsize, fpo);
    if (s->HMsize)
        fwrite(s->Hmap, 1, s->HMsize, fpo);

    size = s->SSize;
    for (i = 0; i < s->Nsec; ++i) {
        if (fpi) {
            j = (unsigned)fread(Buffer, 1, size, fpi);
            if (size != j) {
                memset(Buffer + j, Fill, size - j);
                printf("Input file is smaller than output image (ends at CHS %u/%u/%u)\n", Cylinder,
                       head, i);
                fclose(fpi);
                fpi = 0;
            }
        } else
            memset(Buffer, Fill, size);
        if (Compress) {
            if (issame(size)) {
                putc(0x02, fpo);
                putc(Buffer[0], fpo);
                continue;
            }
        }
        putc(0x01, fpo);
        fwrite(Buffer, 1, s->SSize, fpo);
    }
}

char const help[] =
                "Usage: %s binary-input-file IMD-output-file [option-file] [options]\n\n"
                "opts: /1                   - 1-sided output\n"
                "      /2                   - 2-sided output\n"
                "      /C                   - write Compressed sectors\n"
                "      /U                   - write Uncompressed sectors\n"
                "      /V[0|1]              - Verbose output\n"
                "      C=text | @file       - image Comment\n"
                "      N=#cylinders         - set Number of output cylinders\n"
                "      DM[s]=0-5            - track Data Mode\n"
                "      SS[s]=128-8192       - track Sector Size\n"
                "      SM[s]=n[,n-n][n.#]   - track Sector numbering Map\n"
                "      CM[s]=n[,n-n][n.#]   - track/sector Cylinder  Map\n"
                "      HM[s]=n[,n-n][n.#]   - track/sector Head  Map\n";

/*
 * Main program
 */
int main(int argc, char *argv[]) {
    fputs("BINary-2-IMageDisk " VERSION " / " __DATE__ "\n", stdout);

    chkStdOptions(argc, argv);

    Side0.Mode = Side1.Mode = 255;

    for (int i = 1; i < argc; ++i) {
        ptr = argv[i];
        if (!option(0))
            continue; // Option was recognized
        if (!Ifile) { // 1st file = input
            Ifile = makeFilename(argv[i], ".bin", false);
            continue;
        }
        if (!Ofile) { // 2nd file = output
            Ofile = makeFilename(argv[i], ".imd", false);
            continue;
        }
        if (Ffile) // 3rd file = option
            fatal("Bad option");
        Ffile = makeFilename(argv[i], ".b2i", false);
    }
    ptr1 = 0;

    if (!(Ifile && Ofile)) // Insufficent files
        usage("Input and output files must be specified");

    // If option file, open it and read first set of parameters
    // Apply them if they apply to track-0
    if (*Ffile) {
        if ((fpf = fopen(Ffile, "rt")) == NULL)
            fatal("can't open %s", Ffile);
        read_format();
        if (!Fmtnext) { // Initial parameters
            while (skip())
                option(0);
            read_format();
        }
    }

    validate(0); // Validate side-0 settings
    if (Twoside)
        validate(1); // Validate side-1 settings

    if (Cylinders == UINT_MAX)
        fatal("N= (cylinders) not specified");

    if ((fpi = fopen(Ifile, "rb")) == NULL)
        fatal("can't open %s", Ifile);
    if ((fpo = fopen(Ofile, "wb")) == NULL)
        fatal("can't create %s", Ofile);

    time_t ltime;
    time(&ltime);
    struct tm *now = localtime(&ltime);

    fprintf(fpo, "IMD " VERSION ": %2u/%02u/%04u %2u:%02u:%02u\r\n", now->tm_mday, now->tm_mon + 1,
            now->tm_year + 1900, now->tm_hour, now->tm_min, now->tm_sec);

    if (Cmtsize) // Output comment if enabled
        fwrite(Buffer, 1, Cmtsize, fpo);
    putc(0x1A, fpo);

    // For each cylinder write side0 and side1(if enabled) records
    for (Cylinder = 0; Cylinder < Cylinders; ++Cylinder) {
        if (Cylinder == Fmtnext) {
            while (skip())
                option(255);
            validate(0);
            if (Twoside)
                validate(1);
            read_format();
        }
        write_track(0);
        if (Twoside)
            write_track(1);
    }

    // Close files + test for excess data in input file
    fclose(fpo);
    if (fpi) {
        if (fread(Buffer, 1, sizeof(Buffer), fpi))
            printf("Input file is larger than output image.\n");
        fclose(fpi);
    }
    if (fpf)
        fclose(fpf);
}
