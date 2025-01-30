TARGET: utility.a
OBJS = file.o getch.o memory.o message.o option.o
include ../common.mk

utility.a: $(OBJS)
	ar rv $@ $(OBJS)
	cp -p utility.a ../Install

$(OBJS): utility.h
option.o: verInfo.h
