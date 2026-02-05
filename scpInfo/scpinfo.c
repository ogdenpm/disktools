#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct {
    uint8_t type;
    char *name;
} disktypes[] = {
    { 0x00, "C64" },        { 0x04, "Amiga" },      { 0x10, "FMSS" },       { 0x11, "FMDS" },
    { 0x12, "FMEx" },       { 0x14, "STSS" },       { 0x15, "STDS" },       { 0x16, "STEx" },
    { 0x20, "II" },         { 0x21, "IIPro" },      { 0x24, "400k" },       { 0x25, "800k" },
    { 0x26, "1.44M" },      { 0x30, "360k" },       { 0x31, "720k" },       { 0x32, "1.2M" },
    { 0x33, "1.44M" },      { 0x41, "TRS80 SSDD" }, { 0x42, "TRS80 DSSD" }, { 0x43, "TRS80 DSDD" },
    { 0x40, "TRS80 SSSD" }, { 0x50, "99/4A" },      { 0x60, "D20" },        { 0x70, "CPC" },
    { 0x80, "360k" },       { 0x81, "1.2M" },       { 0x82, "Reserved 1" }, { 0x83, "Reserved 2" },
    { 0x84, "720k" },       { 0x85, "1.44M" },      { 0xe0, "GCR1" },       { 0xe1, "GCR2" },
    { 0xe2, "MFM" },        { 0xf0, "MFM" },        { 0xf1, "RLL" }
};

char *manufacturer[] = {
    "Commodore",   "Atari",       "Apple",      "PC",         "Tandy",       "Texas Instruments",
    "Roland",      "Amstrad",     "Other",      "Unknown(9)", "Unknown(10)", "Unknown(11)",
    "Unknown(12)", "Unknown(13)", "Tape drive", "Hard disk"
};

char *flags[] = { "INDEX", "TPI", "RPM", "TYPE", "MODE", "FOOTER", "EXTENDED MODE", "FLUX CREATOR" };

uint32_t offsets[168];


uint32_t get32(uint8_t *p) {
    return p[0] + (p[1] << 8) + (p[2] << 16) + (p[3] << 24);
}

uint32_t read32(FILE *fp) {
    uint8_t n32[4];
    if (fread(n32, 1, 4, fp) != 4) {
        fprintf(stderr, "premature EOF in read32\n");
        exit(1);
    }
    return get32(n32);
}


uint32_t trackHeader(FILE *fp, uint8_t track, uint8_t revolutions) {
    if (offsets[track] == 0)
        return 0;
    uint8_t thead[4];
    if (fseek(fp, offsets[track], SEEK_SET) || fread(thead, 1, 4, fp) != 4) {
        fprintf(stderr, "premature EOF in trackHeader @ track %d\n", track);
        exit(1);
    }
    if (memcmp(thead, "TRK", 3) != 0) {
        fprintf(stderr, "invalid TRK header @ track %d\n", track);
        return 0;
    }
    printf("Track %d:\n", thead[3]);
    uint32_t length, offset;
    for (uint8_t i = 0; i < revolutions; i++) {
        printf("  %d: index %uns", i, read32(fp) * 25);
        printf(", length %u", length = read32(fp));
        printf(", offset %u", offset = read32(fp));
        printf(", location 0x%08x\n", offsets[track] + offset);
    }
    return offsets[track] + offset + length * 2;

}


int main(int argc, char **argv) {
    FILE *fp;

    if (argc != 2 || !(fp = fopen(argv[1], "rb"))) {
        fprintf(stderr, "Usage: %s filename\n", argv[0]);
        return 1;
    }
    uint8_t buffer[16];
    if (fread(buffer, 1, sizeof(buffer), fp) != sizeof(buffer) || memcmp(buffer, "SCP", 3) != 0) {
        fprintf(stderr, "File is not a valid SCP file\n");
        fclose(fp);
        return 1;
    }

    printf("Id: SCP\n");
    printf("Version: %u.%u\n", buffer[3] / 16, buffer[3] % 16);
    printf("Disk type: %s", manufacturer[buffer[4] >> 4]);
    char *subtype = NULL;
    for (uint8_t i = 0; i < sizeof(disktypes) / sizeof(disktypes[0]); i++) {
        if (disktypes[i].type == buffer[4]) {
            subtype = disktypes[i].name;
            break;
        }
    }
    if (subtype)
        printf(" %s\n", subtype);
    else
        printf(" Unknown subtype (%d)", buffer[4] & 0xf);
    printf("%u Revolutions, Tracks %u-%u", buffer[5], buffer[6], buffer[7]);
    if (buffer[10] == 0)
        fputs(", Both sides", stdout);
    else
        printf(", Side %u only", buffer[10]);
    fputs(buffer[8] & 2 ? ", 96TPI" : ", 48TPI", stdout);
    fputs(buffer[8] & 4 ? ", 360 RPM" : ", 300 RPM", stdout);
    fputs(buffer[8] & 0x10 ? ", Read/Write\n" : "\n", stdout);

    if (buffer[8] & 1)
        fputs("Flux data starts at Index\n", stdout);
    if (buffer[8] & 8)
        fputs("Normalised flux data\n", stdout);
    if (buffer[8] & 0x40)
        fputs("Non floppy disk image\n", stdout);
    if (buffer[8] & 0x80)
        fputs("Non SuperCard Pro creator\n", stdout);

    if (buffer[9] != 0)
        printf("Bit Cell width: %u\n", buffer[9]);

    printf("Resolution: %uns\n", (buffer[11] + 1) * 25);
    printf("Checksum: 0x%08X\n", get32(buffer + 12));
    for (uint8_t i = 0; i < 168; i++) {
        offsets[i] = read32(fp);
        if (offsets[i] && (i < buffer[6] || i > buffer[7]))
            fprintf(stderr, "track %d - unexpected data\n", i);
    }
    uint32_t footer = 0;
    for (uint8_t i = buffer[6]; i <= buffer[7]; i++)
        footer = trackHeader(fp, i, buffer[5]);
    printf("footer at 0x%08X\n", footer);
    fclose(fp);
    return 0;
}