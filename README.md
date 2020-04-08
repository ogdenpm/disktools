# disktools
These tools were originally part of the Intel80tools repository, however to simplify development they have been separated out into their own repository

The tools are
* win32 ports of Dave Dunfield's utilities, specifically
  * dmk2imd
  * imdu
  * td02imd
* td02img a variant of td02imd that created .img files
* flux2imd - used to convert KryoFlux image files in imd image files. Replaces idd2imd
* unidsk - tool to decode an ISIS disk in imd or img format. Supports ISIS II (SD & DD) ISIS III, ISIS IV and some irmx
* mkidsk - takes a recipe file describing an ISIS II (SD or DD) disk and creates a corresponding imd image

Mark

8-Apr-2020