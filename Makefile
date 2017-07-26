WFSLIB_DIR=wfslib
WFSDUMP_DIR=wfsdump
SUBDIRS=$(WFSLIB_DIR) $(WFSDUMP_DIR)
CLEAN_SUBDIRS=$(addsuffix clean,$(SUBDIRS))

.PHONY: all clean $(SUBDIRS) $(CLEAN_SUBDIRS)

all: $(SUBDIRS)

clean: $(CLEAN_SUBDIRS)

$(WFSDUMP_DIR): $(WFSLIB_DIR)

$(SUBDIRS):
	$(MAKE) -C $@

$(CLEAN_SUBDIRS):
	$(MAKE) -C $(@:%clean=%) clean
