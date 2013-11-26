CXX=clang++
#CXX=g++

.PHONY: all release debug demo-release demo-debug

all: demo-release demo-debug

release: demo-release

debug: demo-debug

demo-release:
	@$(MAKE) demo CXXFLAGS="-O3" BINARY="$@"

demo-debug:
	@$(MAKE) demo CXXFLAGS="-g3 -Wfatal-errors" BINARY="$@"

demo: demo.cpp taskpp11.h
	$(CXX) -pthread -std=c++11 $(CXXFLAGS) $< taskpp11.cpp -o $(BINARY)
