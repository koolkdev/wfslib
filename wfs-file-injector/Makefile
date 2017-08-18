LIB=-L../
INC=-I../
CXXFLAGS=$(INC) -c -Wall -Werror -std=c++14
LDFLAGS=$(LIB) -lwfs -lboost_system -lboost_filesystem -lboost_program_options -lcryptopp -lstdc++
SOURCES=main.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=wfs-file-injector

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
