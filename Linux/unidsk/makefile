TARGET = unidsk
OBJS = db.o recipe.o repo.o sha1.o unidsk.o

include ../common.mk
unidsk.o : utility.h db.h unidsk.h
db.o: sha1.h db.h
recipe.o repo.o : db.h unidsk.h
sha1.o: sha1.h
