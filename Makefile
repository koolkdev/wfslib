WFSLIB_DIR=wfslib
WFS_EXTRACT_DIR=wfs-extract
WFS_FUSE_DIR=wfs-fuse
WFS_FILE_INJECTOR_DIR=wfs-file-injector
SUBDIRS=$(WFSLIB_DIR) $(WFS_EXTRACT_DIR) $(WFS_FUSE_DIR) $(WFS_FILE_INJECTOR_DIR)
CLEAN_SUBDIRS=$(addsuffix clean,$(SUBDIRS))
PREFIX ?= /usr/local
DESTDIR ?= /

.PHONY: all clean $(SUBDIRS) $(CLEAN_SUBDIRS)

all: $(SUBDIRS)

clean: $(CLEAN_SUBDIRS)

$(WFS_EXTRACT_DIR): $(WFSLIB_DIR)
$(WFS_FUSE_DIR): $(WFSLIB_DIR)
$(WFS_FILE_INJECTOR_DIR): $(WFSLIB_DIR)

$(SUBDIRS):
	$(MAKE) -C $@

$(CLEAN_SUBDIRS):
	$(MAKE) -C $(@:%clean=%) clean

install: all
	install -dm755 $(DESTDIR)/$(PREFIX)/lib
	install -dm755 $(DESTDIR)/$(PREFIX)/bin
	install -Dm644 libwfs.a $(DESTDIR)/$(PREFIX)/lib
	install -Dm755 $(WFS_EXTRACT_DIR)/wfs-extract $(WFS_FUSE_DIR)/wfs-fuse $(WFS_FILE_INJECTOR_DIR)/wfs-file-injector $(DESTDIR)/$(PREFIX)/bin
