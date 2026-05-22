CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

NDN_CXX_CFLAGS := $(shell pkg-config --cflags libndn-cxx)
NDN_CXX_LIBS := $(shell pkg-config --libs libndn-cxx)

SRC := main.cpp weaver-node.cpp congestion-control.cpp trace-collector.cpp fl-payload.cpp
OBJ := weaverd

.PHONY: all clean

all: $(OBJ)

$(OBJ): $(SRC)
	$(CXX) $(CXXFLAGS) $(NDN_CXX_CFLAGS) $(SRC) -o $(OBJ) $(NDN_CXX_LIBS)

clean:
	rm -f $(OBJ)
