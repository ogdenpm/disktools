# Readme - disktools
The tools here are ones I created  to manage disk images.

They fall broadly into three categories

1. Win32 ports of some of Dave Dunfield's IMD tools, specifically
   - imdu
   - bin2imd
   - dmk2imd
   - td02imd
   - td02img - a variant of td02imd that I created to create image files directly, options as per td02imd
2. Tools to manage isis disks and my Intel repository
   - unidsk - decodes an ISIS disk in IMD or IMG format. Supports ISIS I & II (SD & DD), ISIS PDS, ISIS IV and some iRMX
   - mkidsk - takes a recipe file describing an ISIS I, ISIS II (SD or DD) or ISIS PDS disk and creates a corresponding IMD or IMG file
   - irepo - this manages the Intel repository and can be used to identify files. It also supports updating recipe files.
3. Specialist tool to work with KryoFlux files
   - flux2imd - used to convert KryoFlux image files in IMD image files. Where IMD does not currently support the disk format, sector data is displayed  in hex format.

```
Updated by Mark Ogden 30-Oct-2020
```