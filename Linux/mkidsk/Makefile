TARGET = mkidsk
OBJS = isisDisk.o mkIsisDisk.o writeImage.o

include ../common.mk
mkIsisDisk.o : showVersion.h
$(OBJS): mkIsisDisk.h
