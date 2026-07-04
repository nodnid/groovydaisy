CXX ?= c++
CXXFLAGS = -std=c++17 -Wall -Wextra -g -I../src

SRCS = $(wildcard *.cpp)
HDRS = $(wildcard *.h) $(wildcard ../src/*.h)

all: run_tests
	./run_tests

run_tests: $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o run_tests

clean:
	rm -f run_tests

.PHONY: all clean
