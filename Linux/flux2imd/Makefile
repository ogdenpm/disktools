TARGET = flux2imd
OBJS =  analyse.o container.o decoders.o display.o dpll.o flux.o flux2imd.o formats.o \
	histogram.o scp.o sectorManager.o stdflux.o trackManager.o util.o writeImage.o zip.o 

include ../common.mk

analyse.o: dpll.h flux.h flux2imd.h trackManager.h formats.h sectorManager.h util.h stdflux.h
container.o: flux2imd.h trackManager.h formats.h sectorManager.h util.h zip.h flux.h stdflux.h
decoders.o: dpll.h flux.h flux2imd.h trackManager.h formats.h sectorManager.h util.h stdflux.h
display.o: flux2imd.h trackManager.h formats.h sectorManager.h util.h
dpll.o: dpll.h flux.h util.h trackManager.h formats.h sectorManager.h stdflux.h
flux.o: flux.h util.h stdflux.h
flux2imd.o: flux.h flux2imd.h trackManager.h formats.h sectorManager.h util.h zip.h container.h stdflux.h utility.h
formats.o: sectorManager.h dpll.h formats.h flux.h util.h stdflux.h
histogram.o: flux2imd.h trackManager.h formats.h sectorManager.h flux.h util.h stdflux.h
scp.o: stdflux.h scp.h util.h
sectorManager.o: dpll.h flux2imd.h trackManager.h formats.h sectorManager.h flux.h util.h stdflux.h
stdflux.o: util.h stdflux.h
trackManager.o: flux.h trackManager.h formats.h sectorManager.h util.h
util.o: util.h
writeImage.o: flux2imd.h trackManager.h formats.h sectorManager.h util.h
zip.o: miniz.h zip.h


