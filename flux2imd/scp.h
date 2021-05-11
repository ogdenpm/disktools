#pragma once

// ## FILE FORMAT DEFINES ##

#define IFF_ID 0x00                      // "SCP" (ASCII CHARS)
#define IFF_VER 0x03                     // version (nibbles major/minor)
#define IFF_DISKTYPE 0x04                // disk type (0=CBM, 1=AMIGA, 2=APPLE II, 3=ATARI ST, 4=ATARI 800, 5=MAC 800, 6=360K/720K, 7=1.44MB)
#define IFF_NUMREVS 0x05                 // number of revolutions (2=default)
#define IFF_START 0x06                   // start track (0-167)
#define IFF_END 0x07                     // end track (0-167)
#define IFF_FLAGS 0x08                   // FLAGS bits (0=INDEX, 1=TPI, 2=RPM, 3=TYPE, 4=TYPE, 5=FOOTER, - see defines below)
#define IFF_ENCODING 0x09                // BIT CELL ENCODING (0=16 BITS, >0=NUMBER OF BITS USED)
#define IFF_HEADS 0x0A                   // 0=both heads are in image, 1=side 0 only, 2=side 1 only
#define IFF_RESOLUTION 0x0B              // 0=25ns, 1=50, 2=75, 3=100, 4=125, etc.
#define IFF_CHECKSUM 0x0C                // 32 bit checksum of data added together starting at 0x0010 through EOF
#define IFF_THDOFFSET 0x10               // first track data header offset
#define IFF_THDSTART 0x2B0               // start of first Track Data Header

// FLAGS BIT DEFINES (BIT NUMBER)

#define FB_INDEX 0x00                    // clear = no index reference, set = flux data starts at index
#define FB_TPI 0x01                      // clear = drive is 48TPI, set = drive is 96TPI (only applies to 5.25" drives!)
#define FB_RPM 0x02                      // clear = drive is 300 RPM drive, set = drive is 360 RPM drive
#define FB_TYPE 0x03                     // clear = image is has original flux data, set = image is flux data that has been normalized
#define FB_MODE 0x04                     // clear = image is read-only, set = image is read/write capable
#define FB_FOOTER 0x05                   // clear = image does not contain a footer, set = image contains a footer at the end of it
#define FB_EXTENDED 0x6

// MANUFACTURERS                            7654 3210
#define man_CBM 0x00                     // 0000 xxxx
#define man_Atari 0x10                   // 0001 xxxx
#define man_Apple 0x20                   // 0010 xxxx
#define man_PC 0x30                      // 0011 xxxx
#define man_Tandy 0x40                   // 0100 xxxx
#define man_TI 0x50                      // 0101 xxxx
#define man_Roland 0x60                  // 0110 xxxx
#define man_Amstrad 0x70                 // 0111 xxxx
#define man_Other 0x80                   // 1000 xxxx
#define man_TapeDrive 0xE0               // 1110 xxxx
#define man_HardDrive 0xF0               // 1111 xxxx

// DISK TYPE BIT DEFINITIONS
//
// CBM DISK TYPES
#define disk_C64 0x00                    // xxxx 0000
#define disk_Amiga 0x04                  // xxxx 0100

// ATARI DISK TYPES
#define disk_AtariFMSS 0x00              // xxxx 0000
#define disk_AtariFMDS 0x01              // xxxx 0001
#define disk_AtariFMEx 0x02              // xxxx 0010
#define disk_AtariSTSS 0x04              // xxxx 0100
#define disk_AtariSTDS 0x05              // xxxx 0101

// APPLE DISK TYPES
#define disk_AppleII 0x00                // xxxx 0000
#define disk_AppleIIPro 0x01             // xxxx 0001
#define disk_Apple400K 0x04              // xxxx 0100
#define disk_Apple800K 0x05              // xxxx 0101
#define disk_Apple144 0x06               // xxxx 0110

// PC DISK TYPES
#define disk_PC360K 0x00                 // xxxx 0000
#define disk_PC720K 0x01                 // xxxx 0001
#define disk_PC12M 0x02                  // xxxx 0010
#define disk_PC144M 0x03                 // xxxx 0011

// TANDY DISK TYPES
#define disk_TRS80SSSD 0x00              // xxxx 0000
#define disk_TRS80SSDD 0x01              // xxxx 0001
#define disk_TRS80DSSD 0x02              // xxxx 0010
#define disk_TRS80DSDD 0x03              // xxxx 0011

// TI DISK TYPES
#define disk_TI994A 0x00                 // xxxx 0000

// ROLAND DISK TYPES
#define disk_D20 0x00                    // xxxx 0000

// AMSTRAD DISK TYPES
#define disk_CPC 0x00                    // xxxx 0000

// OTHER DISK TYPES
#define disk_360 0x00                    // xxxx 0000
#define disk_12M 0x01                    // xxxx 0001
#define disk_Rrsvd1 0x02                 // xxxx 0010
#define disk_Rsrvd2 0x03                 // xxxx 0011
#define disk_720 0x04                    // xxxx 0100
#define disk_144M 0x05                   // xxxx 0101

// TAPE DRIVE DISK TYPES
#define tape_GCR1 0x00                   // xxxx 0000
#define tape_GCR2 0x01                   // xxxx 0001
#define tape_MFM 0x01                    // xxxx 0010

// HARD DRIVE DISK TYPES
#define drive_MFM 0x00                   // xxxx 0000
#define drive_RLL 0x01                   // xxxx 0001


// ## TRACK DATA HEADER DEFINES ##

#define TDH_ID 0x00                      // "TRK" (ASCII CHARS)
#define TDH_TRACKNUM 0x03                // track number
#define TDH_TABLESTART 0x04              // table of entries (3 longwords per revolution stored)
#define TDH_DURATION 0x4                 // duration of track, from index pulse to index pulse (1st revolution)
#define TDH_LENGTH 0x08                  // length of track (1st revolution)
#define TDH_OFFSET 0x0C                  // offset to flux data from start of TDH (1st revolution)

