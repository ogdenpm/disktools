## idd2idm -- convert Kryoflux M2FM files to idm

## SYNOPSIS

​	idd2dm \[*OPTIONS*] \[*FILE*]...

## DESCRIPTION

**idd2idm** processes KryoFlux files generated from an ISIS M2FM encoded SD DD disk.

**FILE** can be specified as

- An individual KryoFlux file with a **.raw** extension. In this case the file is checked and using the options below information can be extracted
- A zip file containing KryoFlux files for each of the tracks on a disk. The individual files within the zip file should end in **tt.0.raw** where **tt** is a two digit track number from 0-76.
  In addition to processing each of the files, an imd disk image file will be created named after the zip file with the **.zip** replaced with **.imd**.
  Note only .zip is currently supported.

## OPTIONS

| Option                         | Description                                                  |
| :----------------------------- | :----------------------------------------------------------- |
| **-d0**                        | Also default<br />Shows recovered sectors and missing data sector information<br/>Non critical flux data stream errors<br />Errors which cause the track to be skipped<br />Deleted data marker |
| **-d, -d1**                    | As **-d0** plus<br/>File name being processed<br />Invalid flux data stream errors<br />Warning when there are more than 3 consecutive physical sectors missing<br />Corrupt id or data blocks where a good  copy has not been seen<br />Information about missing id and data blocks after each track copy scanned |
| **-d2**                        | As **-d1** plus<br/>Information from the KryoFlux out of band data blocks<br />Detection of Index Address Marker<br />Information about valid Id and data blocks e.g. track and sector<br />Corrupt Id or data blocks scanned until all are found or data runs out |
| **-d3**                        | As **-d2** plus<br/>Dump of Id and Data blocks, excluding the address marker but including the crc bytes. |
| **-d4**                        | As **-d3** plus<br/>Dump of bit stream data where<br />     **0, 1** are normal bits<br />     **M** is a marker bit 0<br />     **S** indicates a resync was required to align 1 bits<br />     **E** is an end marker i.e. index hole seen<br />     **B** is a bad transition marker<br />It is recommended that this is only ever used for individual **.raw** files |
| **&#8209;h,&nbsp;&#8209;h*n*** | Display a histogram of flux transitions.<br />When n is specified then this is the number of lines the display is scaled to, default is 10.<br />In the display # represents a full block and + represents a half full block. |
| **-s**                         | Display sector mapping, on two lines per track.<br />The first line has the sector Ids , followed optionally by **'r'** if the sector Id  has been recovered. Missing ids are shown as **'--'**.<br /> The second line shows a **'D'** where the data sector is available and **'X'** where it is missing. |
