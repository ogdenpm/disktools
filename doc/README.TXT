ImageDisk
---------
ImageDisk is a program to read entire diskette images into files (type .IMD),
and to recreate a copy of the diskette from that image. A detailed analysis
is performed on the diskette, and information about the formatting is recorded
in the image file. This allows ImageDisk to work with virtually any soft-
sectored diskette format that is compatible with the PC's type 765 floppy
diskette controller and drives.

ImageDisk performs direct hardware access to the floppy disk controller
chip, and therefore cannot run under operating systems which do not
permit application programs such access. These include Windows NT,
2000 and XP. I strongly recommend running the program under pure DOS.

I have placed most of the information you should need about the program
into a simple help system which will activate when you run IMD. Please
be sure to read the LICENSE and GENERAL NOTES before you use ImageDisk.

Content of this archive:

    README.TXT    - This file
    HELP.TXT      - Hints about common problems
    IMD.TXT       - ImageDisk documentation
    IMD.COM       - ImageDisk program
    IMD.HLP       - ImageDisk HELP file (used by IMD.COM)
    IMDA.COM      - ImageDisk Analyzer
    IMDU.COM      - ImageDisk Utility **
    IMDV.COM      - ImageDisk Viewer
    TESTFDC.COM   - Utility to evaluate floppy controller
    BIN2IMD.COM   - Binary to ImageDisk conversion utility
    TD02IMD.COM   - Teledisk to ImageDisk conversion (direct)
    DMK2IMD.COM   - DMK to ImageDisk conversion utility
    ANY2IMD.COM   - ANY format to ImageDisk conversion (via floppy)
    IMDCMT.BAT    - Preset .IMD comment from text file
    IMDEDT.BAT    - Extract, Edit and Replace .IMD comment

** Note: IMDU replaces IMDAM in earlier versions of ImageDisk.

Enjoy,
Dave Dunfield
www.dunfield.com


This ImageDisk package is  Copyright 2005-2012  Dave Dunfield, who
grants the following free license for non-commercial use.  You may:

- Use the package for personal/hobby purposes for as long as you
  wish, on as many computers as you wish.

- Redistribute copies of the package provided none of the copyright
  material has been altered or omitted from the distributed version.
  Additions to the package must be clearly marked as such.

- Create/manipulate .IMD files with no restriction.  The disk image
  file format specification is hereby placed into the public domain.

All rights and permissions not listed above are explicitly reserved
by the author.

Use of this software is entirely at your own risk. The author makes
no guarantee of the suitability of this software for any particular
purpose.

Copyright law prohibits duplication of material so protected. It is
your responsibility to determine the legality of any particular use
of this software. This package is NOT intended as a means to bypass
software copy protection.

By using this software, you accept all responsibility of any kind
which may result from such use.   You agree to indemnify and hold
harmless the author and his agents from any action which may arise
from your use of the software.


Revision history:
-----------------
1.00 = Inital public release
1.01 = Added Sector Cylinder/Head maps for disks with non-standard C/H
       values. Minor fixes and help update.
1.02 = Fully independant side0/side1 analysis.
       Fix reanalyze corrupting sector numbering map (major bug)
       Fix IMDAM to find sectors in "short" images.
       Major code cleanup, numerous minor bugs fixed and enhancements.
1.03 = Added options to control read/write interleave.
       Added low-level format function.
       Fix to recal twice to handle 765's limited to 77 tracks.
       Fix to timeout on hung FDC
       Update IMDAM to insert missing/excluded tracks from merge.
1.04 = Various updates to support PC/XT (8088)
       Fix to insure FDC interrupt is enabled before command begins.
       Fix to handle BIOSs which constantly turn off floppies.
       Fix to write with different interleave (mixed up sectors).
       Fix to end of disk detection on side-1.
       Reset disk system on exit.
1.05 = Fix to interleave logic (could mix up Cylinder/Head mapping).
       Improvements to HELP system.
       Rename IMDAM to IMDU (IMageDisk Utility) and add capabilities:
         - Handle larger tracks
         - Remove specific tracks/sides
         - Convert image to raw binary sector dump
         - Translate data rates within image
         - reGenerate tracks with new interleave factor
         - Append, Replace and Extract comment record
1.06 = Added 'B' to silence beeper in Alignment/Test function
       Fix cursor update in direct file entry
       Updated HELP to include F1=Back and jump-to-link on first character
       Updated HELP content
       Update IMDU with options to:
         - Expand compressed sectors to full data
         - Compress "all-same" sectors
         - Ignore mode difference in compare/merge
1.07 = Fix DMA segment allocation
       Added 'FWRZD' display to align/test function
       Added 'P', 'F', 'B' and 'W' commands to align/test function
1.08 = Added BIN2IMD utility
       Added IMD.TXT manual/documentation
       Improved IMD file selection manual entry
       Fix IMDU comment append/replace to preserve LFCR in files
       Fix minor cosmetic defects in help and displays
     + 1.08a: Fix bug in new file select functions.
1.09 = Added support for 4 drives/FDC and second controller
       Added 'I' align/test function
1.10 = Added support for "Deleted-Data" address marks.
1.11 = Improved gap calculation for unknown formats
1.12 = Fix gap calculation for single-density
1.13 = Fix gap calculation for very high density/packed tracks
       Fix reading full-length comment lines.
1.14 = Add '$' prefix for command files in home directory, and
       auto-run IMD.CMD at startup.
       Added IMDV utility
1.15 = Add manual TG43 control option
       Add TG43 control to Format function
       Clean up handling of RPM function when disk data lost
       Fixes and enhancements to IMDV
1.15a= Added TD02IMD, and TESTFDC utilities
1.16 = Fix DMA problem on noisy bus
       Allow multi-error messages (eg: format)
       Update data entry to highlight input fields
       Added DMK2IMD amd ANY2IMD utilities
1.17 = Add sector types for bad-sectors and option to record bad sectors
       Scan entire disk on "Full analysis"
       26JUL07 Fix bug in write causing corrupted disks
       27JUN08 Add warnings to TD02IMD, allow non-0/1 head value
       05AUG08 Fix DMK2IMD data for single-density
1.18 = Add E)rase command
       Minor internal processing improvements
       Add IMDA utility
       Add IMDCMT utility (batch file calling IMDU)
       Add IMDEDT utility (batch file calling IMDU)
       DMK2IMD add work-around for bad-AM images produced by some emulators.
       Update .IMD file specification with extension for mixed sector-sizes.
       Minor documentation and help file updates.
