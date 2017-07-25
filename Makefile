WFSLIB_DIR=wfslib
WFSDUMP_DIR=wfsdump
WFSLIB_OUT=libwfs.a
WFSDUMP_OUT=wfsdump/wfsdump

SUBDIRS=$(WFSLIB_DIR) $(WFSDUMP_DIR)

.PHONY: all clean $(SUBDIRS)

all: $(WFSLIB_OUT) $(WFSDUMP_OUT)

$(WFSLIB_OUT):
	$(MAKE) -C $(WFSLIB_DIR)

$(WFSDUMP_OUT): $(WFSLIB_OUT)
	$(MAKE) -C $(WFSDUMP_DIR)

$(SUBDIRS):
	$(MAKE) -C $@ clean

clean: $(SUBDIRS)
