CWD:=$(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# This package isn't always shipped with package-config
LIBCRYPTOPP_CFLAGS=$(shell pkg-config --cflags libcrypto++ || echo) 
LIBCRYPTOPP_LIBS=$(shell pkg-config --libs libcrypto++ || echo "-lcryptopp")

WFSLIB_CFLAGS=-I$(CWD) $(LIBCRYPTOPP_CFLAGS)
WFSLIB_LIBS=-L$(CWD) -lwfs -lboost_system -lboost_filesystem -lboost_program_options $(LIBCRYPTOPP_LIBS)
