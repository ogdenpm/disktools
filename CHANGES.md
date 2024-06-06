# Change log

6-Jun-2024

- Fixed an issue with the interleave generation in mkidsk which impacted .img files. mkidsk now ignores  inteleave and skew for .img files.

- Moved mkidsk and unidsk to use my latest version model, see Scripts/versionTools.pdf for details.

- Modified mkidsk and unidsk to build cleanly under Linux with GCC. Added makefiles to build each tool, and a top level makefile in the Linux directory that can be used to build all tools. Please read the common.mk in the Linux directory for supported make targets. As I modify the other applications. I will add further makefiles.

- mkidsk now ignores '\r' in files so there is now no longer a need to convert the recipe files to Linux format.

  

