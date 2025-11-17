# Simple Makefile for vp C++ conversion
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -O2
INCLUDES = -Isrc
LDFLAGS = -pthread

TARGET = vp
TEST_TARGET = vp_test
SRCDIR = src

# Main sources (exclude test files)
MAIN_SOURCES = $(SRCDIR)/main.cpp
LIB_SOURCES = $(filter-out $(SRCDIR)/main.cpp $(SRCDIR)/test_main.cpp, $(wildcard $(SRCDIR)/*.cpp))
LIB_OBJECTS = $(LIB_SOURCES:.cpp=.o)
MAIN_OBJECTS = $(MAIN_SOURCES:.cpp=.o)

# Test sources
TEST_SOURCES = $(SRCDIR)/test_main.cpp
TEST_OBJECTS = $(TEST_SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(LIB_OBJECTS) $(MAIN_OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_TARGET): $(LIB_OBJECTS) $(TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(SRCDIR)/*.o $(TARGET) $(TEST_TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

.PHONY: all test clean install
