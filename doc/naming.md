# File naming convention

To help identify disk images, I use the following naming convention 

**platform id** '-' {**product id**} ['+' {**discriminator**}]  '-' {**tags**} [ ' ' **description**} ] '.' **extent**}

Where [ ] enclose optional elements and characters inside quotes are verbatim.

```
e.g. II-123472-001-D 8080_5 Macro Assembler V4.0.imd
```

Recipe files follow the same convention except that there is an @ prefix and the {dot} {extent} not present.

```
e.g. @II-123472-001-D 8080_5 Macro Assembler V4.0
```

Below is a description of each  element

## platform id

This is used to identify the intended runtime platform for the disk image. The current set I have defined are

```
II		Intel ISIS
IR		Intel iRMX
MD		Microsoft MSDOS
MW		Microsoft Windows
DR		Generic Digital Reseach CP/M
UN		Unknown

Note CP/M in particular will potentially have many variants due to vender specific disk formats. 
```

## product id

This is the vendor specific product id code. 

For Intel ISIS this is the disk label in the xxxxxx-xxx format taken directly from the ISI.LAB file on the disk.

An exception to this is if the  disk is believed to be a copy of an Intel master, in which case the master product id is used and the disk label is shown as [label] at the start of the description.

## discriminator

This is an optional alphanumeric code. It should only be used to differentiate between floppy disk images when the platform id and product id combination does not uniquely identify the image content.

For a multi disk set, it is recommended that this is the disk number, for other cases use an alphabetic character.

## tags

Tags are formed from a string of letters.

The first letter is used to record the disk format. There is a predefined standard set that can be used, but if required additional platform specific formats can be added. 

```
Standard Set
#	multi format, typically for hard disk or archive.
S	Single Density
D	Double Density
Q	Quad Density

if a platform supports both single and double sided disks, use the lower case version of these letters for the double sided disk

Platform Specific
Intel ISIS
P	ISIS-PDS
3	reserved for ISIS III
4	ISIS IV
Note S and D are used for ISIS-II 8" SSSD and 8" DSDD

others to be defined
```

The remaining letters can be any of the following, in the order specified

```
B	bootable
E	file has Errors, includes potentially omitted corrupt files
F	fixed files i.e. bad files have been replaced
U	User modified, not used for minor changes e.g. directory listing added
```



## description

This provides a human readable description of the content. The recommended components in order are

1. Main description, typically taken from any disk label, but note the handling of the optional information below and that format information is handled by tags etc.
   Ensure that specific targeted hardware is included in the description, especially for multi targeted images.

2.  Optional (processor) used when the processor requirements are different to normal
   e.g. (x86) is used for 8086 requirement on ISIS II, (z80) for z80 required on CP/M.

   For multi device targeting separate the targets with an _ or use the following

   - use x as a wild card e.g. 804x
   - if only the suffix changes then separated the options with _ e.g. 8080_5 for 8080 & 8085
   - for prefix/internal changes then use a list of characters in [ ] using a - for no char
        e.g. 80[-123]86 for 8086, 80186, 80286 and 80386

3. Optional version in format Vx.y

4. Optional (n of m) for multiple disk sets

5. Optional (+ description of user added content)<div style="page-break-after: always; break-after: page;"></div>

## extent

The extent corresponding to the image format e.g. imd, img, zip.

## examples

```
double density, not bootable, with user added asm48 files
	II-114532-001-DU Aedit-80 Text Editor V2.0 (+ asm48 V4.0).imd
single density bootable
	II-123413-001-SB Series III Diagnostic Confidence Test 1.0.imd
double density bootable
	II-123416-001-DB Series III Diagnostic Confidence Test 1.0.imd
double density, not bootable, with fixed files
	II-115125-001-DF 8086 Fortran Compiler & Libraries (x86) V3.0 (2 of 2).imd
double density, not bootable, copy of master disk with user label EXC830-003
	II-970032-02-D [EXC830-003] RMX 80 for iSBC 80_30 V1.3.imd
double density, not bootable, targetted at NDS-II (recipe file)
	@II-113914-001-D NDS-II ICE Support (1 of 2)
```

```
Updated by Mark Ogden 01-Nov-2020
```

