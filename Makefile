# Compiler and flags
CXX := g++
CXXFLAGS := -Wall -Wextra -O2 -std=c++17

# Source and output paths
SRC := container.cpp
BUILD_DIR := build
TARGET := $(BUILD_DIR)/container

# Default target
all: $(TARGET)

# Build target
$(TARGET): $(SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

# Create build directory if not exists
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
