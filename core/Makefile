CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -g -O2
LIB_DIR = memory-mgmt
INC_DIR = memory-mgmt/memory-mgmt
LIB     = $(LIB_DIR)/libmemory_mgmt.a
TARGET  = main

.PHONY: all clean lib

all: lib $(TARGET)

lib:
	$(MAKE) -C $(LIB_DIR)

$(TARGET): main.c $(LIB)
	$(CC) $(CFLAGS) -I$(INC_DIR) main.c -L$(LIB_DIR) -lmemory_mgmt -o $@

clean:
	$(MAKE) -C $(LIB_DIR) clean
	rm -f $(TARGET)
