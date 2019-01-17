# disktools
These tools were originally part of the Intel80tools repository, however to simplify development they have been separated out into their own repository

The tools are
* win32 ports of Dave Dunfield's utilities, specifically
  * dmk2imd
  * imdu
  * td02imd
* td02img a variant of td02imd that created .img files
* idd2imd - used to convert KryoFlux image files in imdimage files.
* unidsk - tool to decode an ISIS disk in imd or img format. Supports ISIS II (SD & DD) ISIS III, ISIS IV and some irmx
* mkidsk - takes a recipe file decribing an ISIS II (SD or DD) disk and creates a corresponding imd image

Mark

17-Jan-2019