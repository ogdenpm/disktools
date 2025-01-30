/*
 * ImageDisk Utility
 *
 * This program performs a number of utility functions with .IMD files
 *
 * This program is compiled using my own development tools, and will not
 * build under mainstream compilers without significant work. It is being
 * provided for informational purposes only, and I provide no support for
 * it, technical or otherwise.
 *
 * Copyright 2005-2012 Dave Dunfield
 * All rights reserved.
 *
 * For the record: I am retaining copyright on this code, for the purpose
 * of keeping a say in it's disposition however I encourage the use of ideas,
 * algorithms and code fragments contained herein to be used in the creation
 * of compatible programs on other platforms (eg: Linux).
 */
#include "_version.h"
#include "imdVersion.h"
#include "utility.h"

#include <ctype.h>
#include <memory.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TSIZE      32768 // Max size of track

// Status index values
#define ST_TOTAL   0 // Total sector count
#define ST_COMP    1 // Number Compressed
#define ST_DAM     2 // Number Deleted
#define ST_BAD     3 // Number Bad
#define ST_UNAVAIL 4 // Number unavailable

typedef struct {
    unsigned Mode,           // Data rate/density
        Cyl,                 // Current cylinder
        Head,                // Current head
        Nsec,                // Number of sectors
        Size;                // Sector size
    unsigned char *Tbuf;     // Text buffer (data)
    unsigned char Hflag;     // Extra bits embedded in Head
    unsigned char Smap[256]; // Sector numbering map
    unsigned char Cmap[256]; // Cylinder numbering map
    unsigned char Hmap[256]; // Head numbering map
    unsigned char Sflag[256];
} TRACK; // Sector type flags
TRACK t1, t2, *AI;

unsigned Lmode, // Last track mode
    Lnsec,      // Last track number sectors
    Lsize,      // Last track sector size
    H0tracks,   // Number of head-0 tracks
    H1tracks,   // Number of head-1 tracks
    STcount[5]; // Statistics counters

char *ptr,         // General global pointer
    *Ifile,        // file to read
    *Wfile,        // File to write
    *Mfile,        // File to merge
    *CIfile,       // File to inject comments
    *CEfile;       // File to extract comments
uint8_t Rcomment,  // Replace comment flag
    Ioutput = 255, // IMD format output
    Verbose = 255, // Generate verbose output
    Wflag   = 255, // !warn if no outputput file
    Xmode   = 255, // Ignore mode differences
    Dam     = 255, // Deleted address marks
    Bad     = 255, // Convert bad sectors
    Fill,          // Value to fill missing sectors
    Fflag,         // Fill missing sectors
    Cflag,         // Compressed sector conversion
    Yes,           // Supress overwrite prompt
    Detail,        // Display detail output
    Intl,          // Interleave setting (0=Nochange, 255=BestGuess)
    Tracks[256],   // Track read flags
    Lsmap[256],    // Last sector map
    Index[256],    // Sector index map
    Imap[256],     // Interleave map
    Sskip[256];    // Skip (exclude) map

// Mode (data rate) index to value conversion table
unsigned Mtext[] = { 500, 300, 250 };

// Table for mode translation
unsigned char Tmode[] = { 0, 1, 2, 3, 4, 5 };

FILE *fp, // Input file
    *fpm, // Merge file
    *fpw; // Write (output) file

char const help[] = { 
                "Usage: imdu image [[merge-image] [output-image]] [options]\n\n"
                "opts:  /B                      - output Binary image\n"
                "       /C                      - Compress \"all-same\" sectors\n"
                "       /D                      - display track/sector Detail\n"
                "       /E                      - Expand compressed sectors to full data\n"
                "       /M                      - ignore Mode difference in merge/compare\n"
                "       /NB                     - force Non-Bad data\n"
                "       /ND                     - force Non-Deleted data\n"
                "       /Q                      - Quiet: suppress warnings\n"
                "       /Y                      - auto-Yes (no overwrite prompt)\n"
                "       AC=file[.TXT]           - Append Comment from file      [none]\n"
                "       EC=file[.TXT]           - Extract Comment to file       [none]\n"
                "       F=xx                    - missing sector Fill value     [00]\n"
                "       IL=[1-99]               - reInterLeave(blank=BestGuess) [As read]\n"
                "       RC=file[.TXT]           - Replace Comment from file     [none]\n"
                "       T2=250/300/500          - 250khz Translate              [250]\n"
                "       T3=250/300/500          - 300khz Translate              [300]\n"
                "       T5=250/300/500          - 500khz Translate              [500]\n"
                "       X=track[,to_track]      - eXclude entire track(s)       [None]\n"
                "       X0=track[,to_track]     - eXclude track(s) side 0 only  [None]\n"
                "       X1=track[,to_track]     - eXclude track(s) side 1 only  [None]\n" };

int check_sector(unsigned n);
int _getch(void);       // for windows this is standard, for POSIX see getch.c

/*
 * High-speed test for compressable sector (all bytes same value)
 */
int issame(unsigned char *buf, int size) {
    for (int i = 1; i < size; i++)
        if (buf[i] != buf[0])
            return 0;
    return 1;
}

/*
 * Display new cylinder/head if it has changed
 */
void xch(void) {
    static unsigned C = -1, H;
    if ((t1.Cyl != C) || (t1.Head != H))
        printf("%2u/%u ", C = t1.Cyl, H = t1.Head);
    else
        printf("     ");
}

/*
 * Load a track from an image into memory
 */
int load_track(TRACK *t, FILE *fp) {
    int c, m, h, n, s;
    AI = t;
    if ((m = getc(fp)) == EOF)
        return 0;
    if ((c = getc(fp)) == EOF)
        fatal("EOF at Cylinder");
    if ((h = getc(fp)) == EOF)
        fatal("EOF at Head");
    if ((n = getc(fp)) == EOF)
        fatal("EOF at Nsec");
    if ((s = getc(fp)) == EOF)
        fatal("EOF at Size");
    t->Mode  = (uint8_t)m;
    t->Cyl   = (uint8_t)c;
    t->Head  = h & 0x0F;
    t->Hflag = (h & 0xF0);
    t->Nsec  = (uint8_t)n;
    t->Size  = (uint8_t)s;

    if (m > 5) {
        fatal("Mode value %u out of range 0-5\n", m);
        t->Mode = 0;
    }
    if ((h & 0x0F) > 1) {
        fatal("Head value %u out of range 0-1\n", h);
        t->Head = 0;
    }
    if (s > 6) {
        fatal("Size value %u out of range 0-6\n", s);
        t->Size = 0;
    }

    unsigned sLen = 128U << s;

    if (n) {
        if ((int)fread(t->Smap, 1, n, fp) != n)
            fatal("EOF in Sector Map");

        if (h & 0x80) {
            if ((int)fread(t->Cmap, 1, n, fp) != n)
                fatal("EOF in Cylinder Map");
        } else
            memset(t->Cmap, t->Cyl, n);

        if (h & 0x40) {
            if ((int)fread(t->Hmap, 1, n, fp) != n)
                fatal("EOF in Head Map");
        } else
            memset(t->Hmap, h, n);
    }

    if ((n * s) > TSIZE)
        fatal("Data block two large");

    // Decode sector data blocks
    unsigned int d = 0;
    for (int i = 0; i < n; ++i) {
        switch (c = getc(fp)) {
        case EOF:
            fatal("EOF at sector %u/%u flag", i, t->Smap[i]);
        case 0: // Missing data
            memset(t->Tbuf + d, Fill, sLen);
            m = 255;
            break;
        default:
            m = 0;
            if (--c & 0x01) { // Compressed
                m |= 1;
                if ((h = getc(fp)) == EOF)
                    fatal("EOF in sector %u/%u compress data", i, t->Smap[i]);
                memset(t->Tbuf + d, h, sLen);
            } else { // Normal
                if (fread(t->Tbuf + d, 1, sLen, fp) != sLen)
                    fatal("EOF in sector %u/%u data", i, t->Smap[i]);
            }
            if (c & 0x02) { // Deleted data
                if (!Dam)
                    m |= 2;
            }
            if (c & 0x04) {
                if (Bad)
                    m |= 4;
            }
        }
        t->Sflag[i] = m + 1;
        d += sLen;
    }
    return 255;
}

/*
 * Reorder the sectors to the requested interleave.
 * If necessary, calculate optimal interleave.
 */
void interleave(TRACK *t) {
    unsigned i, j, k, n;
    unsigned char D, E, F, G, Slist[256], Sbuf[256];
    unsigned char map[256];

    // First sort sector numbering map to obtain a
    // sequential list of sectors
    memcpy(Slist, t->Smap, n = t->Nsec);

    for (i = 0; i < n; ++i)
        map[i] = i;
    for (i = 0; i < n; ++i) {
        D = Slist[i];
        F = map[i];
        for (j = i + 1; j < n; ++j) {
            E = Slist[j];
            if (E < D) {
                G        = map[j];
                Slist[j] = D;
                Slist[i] = D = E;
                map[j]       = F;
                map[i] = F = G;
            }
        }
    }
    if (Intl == 255) { // Best guess - calculate interleave
        // Scan sector map to determine interleave occurances
        memset(Sbuf, 0, n);
        for (i = j = k = 0; i < n; ++i) {
            D = Slist[i];
            E = Slist[(i + 1) % n];
            while (t->Smap[j] != D)
                j = (j + 1) % n;
            while (t->Smap[k] != E)
                k = (k + 1) % n;
            E = (k >= j) ? k - j : n - (j - k);
            ++Sbuf[E];
        }

        // Locate best interleave value
        D = k = 0;
        for (i = 0; i < n; ++i) {
            if (Sbuf[i] > D) {
                k = i;
                D = Sbuf[i];
            }
        }
    } else
        k = Intl;

    // Regenerate the Sector Numbering Map using
    // the sector list and requested interleave
    memset(Sbuf, 0, n);
    for (i = j = 0; i < n; ++i) {
        while (Sbuf[j])
            j = (j + 1) % n;
        Imap[j] = map[i];
        Sbuf[j] = 255;
        j       = (j + k) % n;
    }
}

/*
 * Update individual map value with interleave
 */
void inter_update(unsigned char map[], unsigned n) {
    unsigned i;
    unsigned char temp[256];
    memcpy(temp, map, n);
    for (i = 0; i < n; ++i)
        map[i] = temp[Imap[i]];
}

/*
 * Write track from memory to output file
 */
void write_track(TRACK *t, FILE *fp) {
    unsigned i, j, n, s;
    unsigned char f;

    // Don't write excluded tracks
    if ((f = Sskip[t->Cyl])) {
        if ((t->Head ? 2 : 1) & f)
            return;
    }

    // Build index table to access sector data
    n = t->Nsec;
    s = 128 << t->Size;
    for (i = j = 0; i < n; ++i) {
        Index[i] = j >> 7;
        j += s;
    }

    // Adjust interleave according to parameters
    if (Intl) {
        interleave(t);
        inter_update(t->Smap, n);
        inter_update(t->Sflag, n);
        if (t->Hflag & 0x80)
            inter_update(t->Cmap, n);
        if (t->Hflag & 0x40)
            inter_update(t->Hmap, n);
        inter_update(Index, n);
    }

    // If Binary output - write data only
    if (!Ioutput) {
        for (i = 0; i < n; ++i)
            fwrite(t->Tbuf + (Index[i] << 7), 1, s, fp);
        return;
    }

    // Output track header
    putc(Tmode[t->Mode], fp);
    putc(t->Cyl, fp);
    putc(t->Head | t->Hflag, fp);
    putc(n, fp);
    putc(t->Size, fp);
    if (n) {
        fwrite(t->Smap, 1, n, fp);
        if (t->Hflag & 0x80)
            fwrite(t->Cmap, 1, n, fp);
        if (t->Hflag & 0x40)
            fwrite(t->Hmap, 1, n, fp);
    }

    // Output sector data
    for (i = 0; i < n; ++i) {
        j = Index[i] << 7;
        if ((f = t->Sflag[i])) // Data available
            --f;
        else {
            if (!Fflag) { // No sector data
                putc(0, fp);
                continue;
            }
        }
        if (f & 0x01) { // Compressed
            if (Cflag & 0xF0)
                f &= ~1;
        } else if (Cflag & 0x0F) { // Not compressed
            if (issame(t->Tbuf + j, s))
                f |= 1;
        }
        putc(f + 1, fp);
        if (f & 1)
            putc(t->Tbuf[j], fp);
        else
            fwrite(t->Tbuf + j, 1, s, fp);
    }
}

/*
 * Update statistics counters
 */
void update_stats(TRACK *t) {
    unsigned i, b;
    if (t->Head)
        ++H1tracks;
    else
        ++H0tracks;
    for (i = 0; i < t->Nsec; ++i) {
        ++STcount[ST_TOTAL]; // Total sectors
        if ((b = t->Sflag[i])) {
            --b;
            if (b & 1)
                ++STcount[ST_COMP]; // Compressed
            if (b & 2)
                ++STcount[ST_DAM]; // Deleted
            if (b & 4)
                ++STcount[ST_BAD];
        } // Bad
        else
            ++STcount[ST_UNAVAIL];
    } // Unavail
}

/*
 * Test for additional command option value (comma separated)
 */
int tnext(void) {
    if (*ptr == ',') {
        ++ptr;
        return 255;
    }
    if (*ptr)
        fatal("Syntax error");
    return 0;
}

/*
 * Obtain a numeric value from command input
 */
unsigned number(unsigned base, unsigned low, unsigned high) {
    unsigned c, v;
    unsigned char f;
    v = f = 0;
    for (;;) {
        if (isdigit(c = *ptr))
            c -= '0';
        else if ((c >= 'A') && (c <= 'F'))
            c -= ('A' - 10);
        else if ((c >= 'a') && (c <= 'f'))
            c -= ('a' - 10);
        else
            switch (c) {
            case 0:
            case ',':
                if (f) {
                    if ((v < low) || (v > high))
                        fatal(((base == 16) ? "Value (%x) out of range: %x-%x\n"
                                            : "Value (%u) out of range: %u-%u\n"),
                              v, low, high);
                    return v;
                }
                // FALLTHROUGH
            default:
                fatal("Bad number");
            }
        if (c >= base)
            fatal("Bad numeric value");
        f = 255;
        v = (v * base) + c;
        ++ptr;
    }
}

/*
 * Validate track data * insert from merge
 */
void check_track(void) {
    unsigned i, m, c, h, h1, n, s;
    int x;
    m = t1.Mode;
    c = t1.Cyl;
    h = t1.Head;
    n = t1.Nsec;
    s = 128 << t1.Size;

    if (t2.Cyl >= c) {
        if ((t2.Cyl > c) || (t2.Head > h)) {
            // printf("Rewind\n");
            rewind(fpm);
            while ((x = getc(fpm)) != 0x1A) {
                if (x == EOF) {
                    warn("EOF in merge comment");
                    return;
                }
            }
            if (!load_track(&t2, fpm)) {
                warn("No tracks in merge file");
                return;
            }
        }
    }

    while ((t2.Cyl != c) || (t2.Head != h)) {
        h1 = 1 << t2.Head;
        // printf("%u %u/%u %02x\n", c, t2.Cyl, t2.Head, Tracks[t2.Cyl]);
        if (!(Tracks[t2.Cyl] & h1)) {
            if ((t2.Cyl < c) || ((t2.Cyl == c) && (t2.Head < h))) {
                warn("Adding track %u/%u from mergefile", t2.Cyl, t2.Head);
                Tracks[t2.Cyl] |= h1;
                update_stats(&t2);
                if (fpw)
                    write_track(&t2, fpw);
            }
        }
        if (!load_track(&t2, fpm)) {
            warn("Track not found in merge file");
            return;
        }
    }

    if ((t2.Mode != m) && Xmode) {
        warn("Incompatible track mode");
        return;
    }
    if (t2.Size != t1.Size) {
        warn("Incompatible sector size");
        return;
    }

    // Check for compatible data in merge file
    for (i = 0; i < n; ++i)
        check_sector(i);

    // Scan track and insert any missing sectors
    for (i = 0; i < t2.Nsec; ++i) {
        c = t2.Smap[i];
        for (unsigned x = 0; x < t1.Nsec; ++x) {
            if (t1.Smap[x] == c)
                goto secfound;
        }
        // Sector to add
        warn("Adding sector %u/%u", n, c);
        t1.Smap[n]  = c;
        t1.Cmap[n]  = t2.Cmap[i];
        t1.Hmap[n]  = t2.Hmap[i];
        t1.Sflag[n] = t2.Sflag[i];
        memcpy(t1.Tbuf + n * s, t2.Tbuf + i * s, s);
        t1.Nsec = ++n;
    secfound:;
    }
}

/*
 * Check an individual sector from input/merge files
 */
int check_sector(unsigned i) {
    unsigned s, j, si, f, f1, f2;

    s = t1.Smap[i];
    for (j = 0; j < t2.Nsec; ++j) {
        if (t2.Smap[j] == s)
            goto secok;
    }
    warn("Sector %u not found in merge image", s);
    return 0;

secok: // Sector was found
    si = 128 << t1.Size;
    f2 = t2.Sflag[j];
    if (!(f1 = t1.Sflag[i])) {
        if (f2) {
            warn("Filling in sector %u/%u", i, s);
            t1.Sflag[i] = f2;
            memcpy(t1.Tbuf + i * si, t2.Tbuf + j * si, si);
            return 255;
        }
        warn("Sector %u/%u data missing in merge image", i, s);
        return 0;
    }

    // We have sector data
    if (!f2) // No merge sector data
        return 0;
    --f1;
    --f2;
    f = f1 ^ f2;
    if (f & 2)
        warn("Deleted data status differs: %u %u", i, j);
    if (f & 4)
        warn("Bad sector status differs: %u %u", i, j);
    if (memcmp(t1.Tbuf + i * si, t2.Tbuf + j * si, si)) {
        warn("Merge data differs from orignal %u %u", i, j);
    }
    return 0;
}


/*
 * Main program
 */

// modified handling of options to support 2 char values portably
#define OPT(a, b) (((a) << 8) | (b))
enum {
    OPTAC = OPT('A', 'C'),
    OPTdB = OPT('-', 'B'),
    OPTsB = OPT('/', 'B'),
    OPTdC = OPT('-', 'C'),
    OPTsC = OPT('/', 'C'),
    OPTdD = OPT('-', 'D'),
    OPTsD = OPT('/', 'D'),
    OPTdE = OPT('-', 'E'),
    OPTsE = OPT('/', 'E'),
    OPTdM = OPT('-', 'M'),
    OPTsM = OPT('/', 'M'),
    OPTdN = OPT('-', 'N'),
    OPTsN = OPT('/', 'N'),
    OPTdQ = OPT('-', 'Q'),
    OPTsQ = OPT('/', 'Q'),
    OPTdY = OPT('-', 'Y'),
    OPTsY = OPT('/', 'Y'),
    OPTEC = OPT('E', 'C'),
    OPTFe = OPT('F', '='),
    OPTIL = OPT('I', 'L'),
    OPTRC = OPT('R', 'C'),
    OPTT2 = OPT('T', '2'),
    OPTT3 = OPT('T', '3'),
    OPTT5 = OPT('T', '5'),
    OPTX0 = OPT('X', '0'),
    OPTX1 = OPT('X', '1'),
    OPTXe = OPT('X', '='),
};

int main(int argc, char *argv[]) {
    int c;
    unsigned i, s, t;
    unsigned char f;
    static char *st_names[] = { "Compressed", "Deleted", "Bad", "Unavail" };

    fputs("IMageDisk Utility " VERSION " / " __DATE__ "\n", stdout);

    chkStdOptions(argc, argv);

    // Process command line arguments
    for (i = 1; i < (unsigned)argc; ++i) {
        ptr      = argv[i];
        char ch1 = toupper(*ptr++); // avoid sequence point risk
        char ch2 = toupper(*ptr++);
        switch (s = OPT(ch1, ch2)) {
        case OPTdB:
        case OPTsB:
            Ioutput = Wflag = 0;
            continue;
        case OPTdD:
        case OPTsD:
            Detail = 255;
            continue;
        case OPTdQ:
        case OPTsQ:
            Verbose = 0;
            continue;
        case OPTdY:
        case OPTsY:
            Yes = 255;
            continue;
        case OPTdC:
        case OPTsC:
            Cflag = 0x0F;
            Wflag = 0;
            continue;
        case OPTdE:
        case OPTsE:
            Cflag = 0xF0;
            Wflag = 0;
            continue;
        case OPTdM:
        case OPTsM:
            Xmode = 0;
            continue;
        case OPTdN:
        case OPTsN:
            switch (toupper(*ptr)) {
            case 'D':
                Dam = Wflag = 0;
                continue;
            case 'B':
                Bad = Wflag = 0;
                continue;
            }
            usage("/N must be followed by D or B");
        case OPTFe:
            Fill  = number(16, Wflag = 0, 255);
            Fflag = 255;
            continue;
        case OPTX0:
        case OPTX1:
        case OPTXe:
            f = ch2 == '0' ? 1 : ch2 == '1' ? 2 : 3;
            c = s = number(10, Wflag = 0, 255);
            if (tnext())
                c = number(10, 0, 255);
            while (s <= (unsigned)c)
                Sskip[s++] |= f;
            continue;
        }
        if (*ptr++ == '=')
            switch (s) {
            case OPTRC:
                Rcomment = 255;
                // FALLTHROUGH
            case OPTAC:
                CIfile = makeFilename(ptr, ".txt", false);
                continue;
            case OPTEC:
                CEfile = makeFilename(ptr, ".txt", false);
                continue;
            case OPTIL:
                Intl  = *ptr ? number(10, 1, 99) : 255;
                Wflag = 0;
                continue;
            case OPTT5:
            case OPTT3:
            case OPTT2:
                s = ch2 == '5' ? 0 : ch2 == '3' ? 1 : 3;
                switch (number(10, Wflag = 0, 65535)) {
                default:
                    usage("/T must be followed by 2, 3 or 5");
                case 500:
                    c = 0;
                    break;
                case 300:
                    c = 1;
                    break;
                case 250:
                    c = 2;
                }
                Tmode[s]     = c;
                Tmode[s + 3] = c + 3;
                continue;
            }
        if (!fp) { // 1st = Input
            char *iFile = makeFilename(ptr - 3, ".imd", false);
            if ((fp = fopen(iFile, "rb")) == NULL)
                fatal("can't open %s\n", iFile);
            continue;
        }
        if (!Mfile) {
            Mfile = makeFilename(ptr - 3, ".imd", false);
            continue;
        } // 2nd = Merge
        if (!Wfile) {
            Wfile = ptr - 3;
            continue;
        } // 3rd = Output
        usage("Too many file arguments");
    }

    if (!fp)
        usage("No image file"); // No input file - issue help/exit

    // Allocate 2 32k blocks for track data
    if (!(t1.Tbuf = (unsigned char *)malloc(0x8000)) ||
        !(t2.Tbuf = (unsigned char *)malloc(0x8000)))
        fatal("Out of memory");
    // Read and display input file comment, saving it to segment
    t = 0;
    while ((c = getc(fp)) != 0x1A) {
        if (c == EOF)
            fatal("EOF in comment");
        if (c != '\r') // avoid \r\n becoming \r\r\n
            putc(c, stdout);
        t1.Tbuf[t++] = c;
    }

    // If extracting comment, retrieve from segment and write to file
    if (CEfile) {
        if ((fpw = fopen(CEfile, "wb")) == NULL)
            fatal("can't create %s\n", CEfile);
        for (i = 0; i < t; ++i)
            putc(t1.Tbuf[i], fpw);
        fclose(fpw);
    }

    if (Rcomment) // Replacing - reset segment top
        t = 0;

    // If inserting comment - append to segment
    if (CIfile) {
        Wflag = 0;
        if ((fpw = fopen(CIfile, "rb")) == NULL)
            fatal("can't open %s\n", CIfile);
        while ((c = getc(fpw)) != EOF) {
            if (c == '\n' && (t != 0 || t1.Tbuf[t - 1] != '\r')) // handle linux
                t1.Tbuf[t++] = '\r';
            t1.Tbuf[t++] = c;
        }
        fclose(fpw);
    }
    fpw = 0;

    // 2nd filename (Merge) is actually output if no 3rd file and
    // other options indicate we are convertng data
    if (Mfile) {
        if (Wfile || Wflag) {
            if ((fpm = fopen(Mfile, "rb")) == NULL)
                fatal("can't open %s\n", Mfile);
        } else
            Wfile = Mfile;
    }

    if (Wfile) {                 // We are writing output
        if (!(Ioutput | Intl)) { // Binary output with no IL=
            Intl = 1;
            if (Verbose)
                printf("Assuming 1:1 for Binary output\n");
        }
        Wfile = makeFilename(Wfile, Ioutput ? ".imd" : ".bin", false);
        // Test for file already existing and prompt for overwrite
        if (!Yes)
            if ((fpw = fopen(Wfile, "rb"))) {
                printf("%s already exists - proceed(Y/N)?", Wfile);
            xxx:
                switch (_getch()) {
                default:
                    goto xxx;
                case 0x1B:
                case 0x03:
                case 'n':
                case 'N':
                    printf("N\n");
                    return 1;
                case 'y':
                case 'Y':
                    printf("Y\n");
                }
                fclose(fpw);
            }
        if ((fpw = fopen(Wfile, "wb")) == NULL)
            fatal("can't create %s\n", Wfile);
    } else {        // No output file specified
        if (!Wflag) // Error if we are converting
            fatal("Missing output file");
        if (Verbose && fpm)
            printf("No output file - compare only.\n");
    }

    // If writing a .IMD file, output the comment (saved in segment)
    if (Ioutput && fpw) {
        for (i = 0; i < t; ++i)
            putc(t1.Tbuf[i], fpw);
        putc(0x1A, fpw);
    }

    t2.Cyl = t2.Head = -1;

    // If merging, build track map for input and skip merge comment
    if (fpm) {
        while (load_track(&t1, fp)) {
            Tracks[t1.Cyl] |= (1 << t1.Head);
        }
        rewind(fp);
        while ((c = getc(fp)) != 0x1A) {
            if (c == EOF)
                fatal("EOF in comment");
        }
    }

    // Process input file & compare/merge
    f = 255;
    while (load_track(&t1, fp)) {
        if (fpm)
            check_track();
        update_stats(&t1);

        s = 128 << t1.Size;
        if ((t1.Mode != Lmode) || (t1.Nsec != Lnsec) || (t1.Size != Lsize))
            f = 255;
        if (f) {
            xch();
            if (!t1.Nsec) {
                printf("No data\n");
                f = 0;
                continue;
            }
            Lmode = t1.Mode; // avoid sequence point issues
            printf("%u kbps %cD  %ux%u\n", Mtext[Lmode % 3], (Lmode > 2) ? 'D' : 'S',
                   Lnsec = t1.Nsec, 128 << (Lsize = t1.Size));
        }
        if (f || memcmp(t1.Smap, Lsmap, sizeof(Lsmap))) {
            f = 255;
            memcpy(Lsmap, t1.Smap, sizeof(Lsmap));
            if (Detail) {
                xch();
                for (i = 0; i < t1.Nsec; ++i) {
                    if (t1.Nsec < 19)
                        putc(' ', stdout);
                    printf("%-3u", t1.Smap[i]);
                }
                putc('\n', stdout);
            }
        }
        if (Detail) { // Display detail records
            if (t1.Hflag & 0x80) {
                printf(" CL: ");
                for (i = 0; i < t1.Nsec; ++i) {
                    if (t1.Nsec < 19)
                        printf(" ");
                    printf("%-3u", t1.Cmap[i]);
                }
                printf("\n");
            }
            if (t1.Hflag & 0x40) {
                printf(" HD: ");
                for (i = 0; i < t1.Nsec; ++i) {
                    if (t1.Nsec < 19)
                        printf(" ");
                    printf("%-3u", t1.Hmap[i]);
                }
                printf("\n");
            }
            xch();
            for (i = 0; i < t1.Nsec; ++i) {
                if (t1.Nsec < 19)
                    printf(" ");
                if (!(f = t1.Sflag[i])) {
                    printf("U  ");
                    continue;
                }
                --f;
                c = (f & 4) ? 'B' : 'D';
                if (f & 0x20)
                    c += ('a' - 'A');
                if (f & 1)
                    printf("%c%02x", c, t1.Tbuf[i * s]);
                else
                    printf("%c  ", c);
            }
            printf("\n");
        }
        f = 0;
        if (fpw)
            write_track(&t1, fpw);
    }

    // Add extra tracks (beyond end) from merge file
    if (fpm) {
        rewind(fpm);
        while ((c = getc(fpm)) != 0x1A) {
            if (c == EOF) {
                warn("EOF in merge comment");
                return 1;
            }
        }
        while (load_track(&t2, fpm)) {
            if (!(Tracks[t2.Cyl] & (1 << t2.Head))) {
                warn("Adding track %u/%u from mergefile", t2.Cyl, t2.Head);
                update_stats(&t2);
                if (fpw)
                    write_track(&t2, fpw);
            }
        }
    }

    // Display summary
    printf("%u tracks(%u/%u), %u sectors", H0tracks + H1tracks, H0tracks, H1tracks,
           STcount[ST_TOTAL]);
    for (f = i = 0; i < 4; ++i) {
        if ((s = STcount[i + 1])) {
            printf("%s%u %s", f ? ", " : " (", s, st_names[i]);
            f = 255;
        }
    }
    if (f)
        putc(')', stdout);
    putc('\n', stdout);

    // Close files
    if (fpw)
        fclose(fpw);
    if (fpm)
        fclose(fpm);
    if (fp)
        fclose(fp);
    return 0;
}
