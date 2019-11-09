include ../common.mk

CXXFLAGS=$(WFSLIB_CFLAGS) -c -Wall -Werror -std=c++14
LDFLAGS=$(WFSLIB_LIBS)

SOURCES=main.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=wfs-extract

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
