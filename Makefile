CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
INC      := -Iinclude -Isrc
BIN      := bin
BUILD    := build

CORE     := src/isa.cpp src/object.cpp src/assembler.cpp src/linker.cpp
COMPILER := $(wildcard src/compiler/*.cpp)

all: $(BIN)/r32as $(BIN)/r32ld $(BIN)/r32sim $(BIN)/r32objdump $(BIN)/mcc $(BUILD)/crt0.o $(BUILD)/rt.o

$(BIN) $(BUILD):
	mkdir -p $@

$(BIN)/r32as: src/tools/r32as.cpp $(CORE) | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) $^ -o $@
$(BIN)/r32ld: src/tools/r32ld.cpp $(CORE) | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) $^ -o $@
$(BIN)/r32sim: src/tools/r32sim.cpp src/isa.cpp src/object.cpp | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) $^ -o $@
$(BIN)/r32objdump: src/tools/r32objdump.cpp src/isa.cpp src/object.cpp | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) $^ -o $@
$(BIN)/mcc: src/tools/mcc.cpp $(COMPILER) $(wildcard src/compiler/*.hpp) | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) src/tools/mcc.cpp $(COMPILER) src/isa.cpp -o $@

$(BUILD)/%.o: runtime/%.s $(BIN)/r32as | $(BUILD)
	$(BIN)/r32as $< -o $@

test: all
	./tests/run_tests.sh

clean:
	rm -rf $(BIN) $(BUILD)

.PHONY: all test clean
