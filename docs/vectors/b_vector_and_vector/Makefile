# Makefile for Vector Test Suite

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g
TARGET = test_vector
DEMO_TARGET = demo_conversions
SOURCES = test_vector.cpp
DEMO_SOURCES = demo_conversions.cpp
VECTOR_HEADER = vector.cpp
BVECTOR_HEADER = b_vector.cpp

# Include paths
INCLUDES = -I../../../src

.PHONY: all clean test run demo

all: $(TARGET)

$(TARGET): $(SOURCES) $(VECTOR_HEADER) $(BVECTOR_HEADER)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(SOURCES)

$(DEMO_TARGET): $(DEMO_SOURCES) $(VECTOR_HEADER) $(BVECTOR_HEADER)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(DEMO_TARGET) $(DEMO_SOURCES)

test: $(TARGET)
	./$(TARGET)

run: test

demo: $(DEMO_TARGET)
	./$(DEMO_TARGET)

clean:
	rm -f $(TARGET) $(DEMO_TARGET) *.o

# Debug build
debug: CXXFLAGS += -DDEBUG -g3
debug: $(TARGET)

# Release build  
release: CXXFLAGS += -DNDEBUG -O3
release: $(TARGET)

# Memory check with valgrind (if available)
memcheck: $(TARGET)
	@which valgrind > /dev/null 2>&1 && \
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET) || \
	echo "Valgrind not found, skipping memory check"

# Quick syntax check
syntax:
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fsyntax-only $(SOURCES)

help:
	@echo "Available targets:"
	@echo "  all       - Build the test executable"
	@echo "  test/run  - Build and run tests"
	@echo "  demo      - Build and run conversion demo"
	@echo "  clean     - Remove built files"
	@echo "  debug     - Build with debug symbols"
	@echo "  release   - Build optimized version"
	@echo "  memcheck  - Run with valgrind (if available)"
	@echo "  syntax    - Check syntax only"
	@echo "  help      - Show this help"
