# Builds the firmware's parser and renderer for this machine, to test the ATIS
# parsing and preview the screen without flashing a board.
#
#   make test                      run the parser tests
#   make preview                   render the live KMSP ATIS to preview.png
#   make preview ATIS=file.json    render a saved atis.info response

SRC := ../firmware/inkplate_atis/src
CORE := $(SRC)/atis.cpp $(SRC)/canvas.cpp $(SRC)/render.cpp $(SRC)/fonts.cpp $(SRC)/airport_data.cpp
# The board package compiles sketches as gnu++11, so hold the core to C++11 here too.
CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -I$(SRC)
ATIS ?= build/live.json
ICAO := $(shell sed -n 's/^const Airport AIRPORT = {"\([A-Z0-9]*\)".*/\1/p' $(SRC)/airport_data.cpp)

.PHONY: test preview clean

test: build/test
	./build/test

preview: build/preview $(ATIS)
	./build/preview $(ATIS) ../preview.png

build/live.json: | build
	curl -fsS https://atis.info/api/$(ICAO) -o $@

build/preview: preview.cpp $(CORE) $(wildcard $(SRC)/*.h) | build
	$(CXX) $(CXXFLAGS) -o $@ preview.cpp $(CORE)

build/test: test.cpp $(SRC)/atis.cpp $(SRC)/atis.h | build
	$(CXX) $(CXXFLAGS) -o $@ test.cpp $(SRC)/atis.cpp

build:
	mkdir -p build

clean:
	rm -rf build
