CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -g -O2 -DLOG_LEVEL=0

SRC_DIR    = bufforing-stm
TEST_DIR   = tests
TEST_BIN   = test_bin

MEM_DIR    = ../memory-mgmt
MEM_SRC    = $(MEM_DIR)/memory-mgmt
MEM_LIB    = $(MEM_DIR)/libmemory_mgmt.a

INCLUDES   = -I$(SRC_DIR) -I$(MEM_SRC)
CMOCKA     = $(shell pkg-config --cflags --libs cmocka)

TEST_SRCS  = \
	$(TEST_DIR)/test_dataBuffor.c \
	$(TEST_DIR)/test_fsmMap.c

TEST_BINS  = $(patsubst $(TEST_DIR)/%.c,$(TEST_BIN)/%,$(TEST_SRCS))

.PHONY: all clean test lib

all: test

lib:
	$(MAKE) -C $(MEM_DIR)

$(MEM_LIB): lib

$(TEST_BIN):
	mkdir -p $(TEST_BIN)

$(TEST_BIN)/%: $(TEST_DIR)/%.c $(MEM_LIB) | $(TEST_BIN)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(MEM_DIR) -lmemory_mgmt $(CMOCKA) -o $@

test: $(TEST_BINS)
	@echo "=== Running bufforing-stm tests ==="
	@for t in $(TEST_BINS); do \
		echo "--- $$t ---"; \
		./$$t; \
	done

clean:
	rm -rf $(TEST_BIN)
