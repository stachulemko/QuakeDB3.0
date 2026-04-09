CC      = gcc
CXX     = g++
CFLAGS  = -Wall -Wextra -std=c11 -g -O2
CXXFLAGS = -Wall -Wextra -std=c++11 -g -O2
AR      = ar
ARFLAGS = rcs

SRC_DIR  = memory-mgmt
TEST_DIR = tests
OBJ_DIR  = obj
TEST_BIN = test_bin

SRCS = \
	$(SRC_DIR)/types_converter.c \
	$(SRC_DIR)/all_var.c \
	$(SRC_DIR)/block_header.c \
	$(SRC_DIR)/tuple_header.c \
	$(SRC_DIR)/data_null_bitmap.c \
	$(SRC_DIR)/tuple.c \
	$(SRC_DIR)/block8kb.c \
	$(SRC_DIR)/table_header.c \
	$(SRC_DIR)/all_table.c \
	$(SRC_DIR)/file_manager_c.c

OBJS   = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
TARGET = libmemory_mgmt.a

TEST_SRCS = \
	$(TEST_DIR)/test_types_converter.c \
	$(TEST_DIR)/test_all_var.c \
	$(TEST_DIR)/test_block_header.c \
	$(TEST_DIR)/test_tuple.c \
	$(TEST_DIR)/test_block8kb.c \
	$(TEST_DIR)/test_table_header.c \
	$(TEST_DIR)/test_universal_block.c

TEST_BINS = $(patsubst $(TEST_DIR)/%.c,$(TEST_BIN)/%,$(TEST_SRCS))

CMOCKA_FLAGS = $(shell pkg-config --cflags --libs cmocka)

.PHONY: all clean test

all: $(OBJ_DIR) $(TARGET)

$(TARGET): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c $< -o $@

# ---- tests ----

test: $(TEST_BIN) $(TEST_BINS)
	@echo "=== Running all tests ==="
	@for t in $(TEST_BINS); do \
		echo "--- $$t ---"; \
		./$$t; \
	done

$(TEST_BIN):
	mkdir -p $(TEST_BIN)

$(TEST_BIN)/%: $(TEST_DIR)/%.c $(TARGET)
	$(CC) $(CFLAGS) -I$(SRC_DIR) $< -L. -lmemory_mgmt $(CMOCKA_FLAGS) -o $@


clean:
	rm -rf $(OBJ_DIR) $(TEST_BIN) $(TARGET)
