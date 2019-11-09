CWD:=$(dir $(abspath $(lastword $(MAKEFILE_LIST))))

WFSLIB_CFLAGS=-I$(CWD)
WFSLIB_LIBS=-L$(CWD) -lwfs -lboost_system -lboost_filesystem -lboost_program_options -lcryptopp
