#ifndef FILE_MANAGER_C_H
#define FILE_MANAGER_C_H

/*
 * file_manager_c.h — binary file operations (C version)
 * Uses plain buffers + sizes instead of std::vector<uint8_t>.
 */

#include <stdint.h>
#include <stdlib.h>
#include "config.h"

/* Write len bytes from buf to path/name.bin.
 * Returns 0 on success, -1 on error. */
int fm_save_block(const char *path, const char *name,
                  const uint8_t *buf, int32_t len);

/* Read path/name.bin into buf (up to buf_size bytes).
 * Sets *out_len to actual bytes read, or -1 on error.
 * Returns 0 on success, -1 on error. */
int fm_load_block(const char *path, const char *name,
                  uint8_t *buf, int32_t buf_size, int32_t *out_len);

/* Read bytes_to_read bytes starting at start_offset.
 * Returns 0 on success, -1 on error. */
int fm_load_range(const char *path, const char *name,
                  int32_t start_offset, int32_t bytes_to_read,
                  uint8_t *buf);

/* Overwrite the block at position block_num * BLOCK_SIZE.
 * Returns 0 on success, -1 on error. */
int fm_save_block_at(const char *path, int32_t table_id,
                     const uint8_t buf[BLOCK_SIZE], int32_t block_num);

/* Create an empty .bin file. Returns 0 on success, -1 on error. */
int fm_create(const char *path, const char *name);

/* File size in bytes, or -1 on error. */
int32_t fm_file_size(const char *path, const char *name);

/* Returns 1 if file exists, 0 otherwise. */
int fm_exists(const char *path, const char *name);

/* Reads block block_num from path/{table_id}.bin and returns it as a
 * malloc'd BLOCK_SIZE buffer. Returns NULL on error or out-of-range.
 * Caller must free() the returned buffer. */
uint8_t *fm_get_block(const char *path, int32_t table_id, int32_t block_num);

/* Create a .bin file at path/name. Returns 0 on success, -1 on error. */
int createBinFile(const char *path, const char *name);

#endif
