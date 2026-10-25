CXX ?= g++
CXXFLAGS ?= -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic
LDLIBS += -pthread
.PHONY: all test clean
all: build/polyomino-search
build/polyomino-search: src/main.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS)
test: build/polyomino-search
	./build/polyomino-search --self-test
	python3 tests/test_cli.py ./build/polyomino-search
clean:
	rm -rf build
