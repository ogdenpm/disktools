SHELL=bash
.PHONY: clean distclean install rebuild
DIRS = abstool aomf2bin dumpomf fixobj genpatch omfcmp plmpp unpack
all:
	@for f in $(DIRS); do $(MAKE) -C $$f || exit 1; done;

publish:
	@for f in $(DIRS); do $(MAKE) -C $$f publish || exit 1; done;

clean: 
	@for f in $(LIBS) $(DIRS); do $(MAKE) -C $$f clean || exit 1; done;

distclean: 
	@for f in $(LIBS) $(DIRS); do $(MAKE) -C $$f distclean || exit 1; done;

rebuild: 
	@for f in $(DIRS); do $(MAKE) -C $$f rebuild || exit 1; done;

