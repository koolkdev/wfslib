CXXFLAGS := -c -Wall -Werror -std=c++14
#LDFLAGS := -lboost_system -lboost_filesystem -lcryptopp
SOURCES=Area.cpp Block.cpp DataBlock.cpp DeviceEncryption.cpp Directory.cpp File.cpp FileDevice.cpp KeyFile.cpp MetadataBlock.cpp Structs.cpp SubBlockAllocator.cpp Wfs.cpp WfsItem.cpp
OBJECTS=$(SOURCES:.cpp=.o)
OUT=../libwfs.a

all: $(SOURCES) $(OUT)

$(OUT): $(OBJECTS) 
	ar rcs $(OUT) $(OBJECTS)

.cpp.o:
	$(CXX) $(CXXFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -f $(OBJECTS) $(OUT)