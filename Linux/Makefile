SHELL=bash
.PHONY: clean distclean install rebuild
DIRS = mkidsk unidsk bin2imd dmk2imd td02imx irepo imdu
all:
	@for f in $(DIRS); do $(MAKE) -C $$f || exit 1; done;

publish:
	@for f in $(DIRS); do $(MAKE) -C $$f publish || exit 1; done;

clean: 
	@for f in $(LIBS) $(DIRS) utility; do $(MAKE) -C $$f clean || exit 1; done;

distclean: 
	@for f in $(LIBS) $(DIRS) utility; do $(MAKE) -C $$f distclean || exit 1; done;
	rm -fr Install/*

rebuild: 
	@for f in $(DIRS); do $(MAKE) -C $$f rebuild || exit 1; done;

