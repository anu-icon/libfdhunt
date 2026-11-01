CC ?= gcc
CFLAGS := -Wall -Wextra -O2 -Iinclude -pthread
SOFLAGS := -shared -ldl -pthread

SRC_DIR := src
BUILD_DIR := build
TEST_DIR := tests
INCLUDE_DIR := include

TARGET := $(BUILD_DIR)/libfdhunt.so

LIB_SRCS := $(SRC_DIR)/tracker.c $(SRC_DIR)/interceptor.c
LIB_OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(LIB_SRCS))

TEST_SRCS := $(wildcard $(TEST_DIR)/*.c)
TEST_BINS := $(patsubst $(TEST_DIR)/%.c, $(BUILD_DIR)/%, $(TEST_SRCS))

all : $(TARGET)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(INCLUDE_DIR)/tracker.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(TARGET): $(LIB_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LIB_OBJS) $(SOFLAGS) -o $@

$(BUILD_DIR)/%: $(TEST_DIR)/%.c $(TARGET)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

test: $(TARGET) $(TEST_BINS)
	@bash tests/run_tests.sh

clean: 
	@echo "#--- Cleaning up build artifacts ---#"
	rm -rf $(BUILD_DIR)

.PHONY: all test clean