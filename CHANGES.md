# Change log

6-Jun-2024

- Fixed an issue with the interleave generation in mkidsk which impacted .img files
- Moved mkidsk to use my latest version model, see Scripts/versionTools.pdf for details.
- Modified mkidsk to build cleanly under linux with gcc. Added a makefile to build the tool. This can be found under Linux/mkidsk, please see the common.mk in the Linux director for supported make targets. As I modify the other applications I will include appropriate makefiles and a top level makefile to make all

