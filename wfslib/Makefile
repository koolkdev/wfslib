CXX=g++
CXXFLAGS := -Wall -Werror -std=c++11
LDFLAGS := -lboost_system -lboost_filesystem -lcryptopp
SOURCES=Area.cpp Block.cpp DataBlock.cpp DeviceEncryption.cpp Directory.cpp File.cpp FileDevice.cpp KeyFile.cpp MetadataBlock.cpp Structs.cpp SubBlockAllocator.cpp Wfs.cpp WfsItem.cpp
OBJECTS=$(SOURCES:.cpp=.o)
OUT=../libwfs.a

all: $(SOURCES) $(OUT)

$(OUT): $(OBJECTS) 
	ar rcs $(OUT) $(OBJECTS)

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -f $(OBJECTSS) $(OUT)

