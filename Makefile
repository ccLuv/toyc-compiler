CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic

.PHONY: all clean

all: compiler

compiler: src/main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	$(RM) compiler
