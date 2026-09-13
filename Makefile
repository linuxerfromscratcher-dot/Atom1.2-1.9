CXX = g++
CXXFLAGS = -Wall -Wextra -O2

TARGET = dist/atomc

CXX_SRCS = $(wildcard src/*.cpp)
CXX_OBJS = $(CXX_SRCS:src/%.cpp=build/%.o)

all: $(TARGET)

build: $(TARGET)

$(TARGET): $(CXX_OBJS)
	@mkdir -p dist
	$(CXX) $(CXXFLAGS) $^ -o $@

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf build/ dist/

.PHONY: all build clean
