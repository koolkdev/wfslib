CXX=g++
LIB=-L../
INC=-I../
CXXFLAGS=$(INC) -c -Wall -Werror -std=c++11
LDFLAGS=$(LIB) -lboost_system -lboost_filesystem -lboost_program_options -lcryptopp -lwfs
SOURCES=main.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=wfsdump

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) 
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@

.cpp.o:
	$(CXX) $(CXXFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -f $(OBJECTS) $(EXECUTABLE)
