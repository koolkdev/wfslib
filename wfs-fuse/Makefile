include ../common.mk

ifneq ($(shell pkg-config fuse; echo $$?),0)
 $(error "Can' find fuse library")
endif

FUSE_CFLAGS=$(shell pkg-config --cflags fuse)
FUSE_LIBS=$(shell pkg-config --libs fuse)

CXXFLAGS=$(WFSLIB_CFLAGS) -c -Wall -Werror -std=c++14 $(FUSE_CFLAGS)
LDFLAGS=$(WFSLIB_LIBS) $(FUSE_LIBS)

SOURCES=wfs-fuse.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=wfs-fuse

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) ../libwfs.a 
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@

.cpp.o:
	$(CXX) $(CXXFLAGS) $< -o $@

depend: .depend

.depend: $(SOURCES)
	rm -f .depend
	$(CXX) $(CXXFLAGS) -MM $^ > .depend

include .depend

.PHONY: clean
clean:
	rm -f $(OBJECTS) $(EXECUTABLE) .depend
