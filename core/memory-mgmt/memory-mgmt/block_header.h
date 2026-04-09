#ifndef BLOCK_HEADER_H
#define BLOCK_HEADER_H

/*
 * block_header.h — 8 kb block header (C struct replacing the blockHeader class)
 *
 * Binary layout (17 bytes):
 *   nextblock       int32_t  4 B
 *   block_id        int32_t  4 B
 *   pd_lsn          int32_t  4 B
 *   pd_checksum     int16_t  2 B
 *   pd_flags        int16_t  2 B
 *   contain_toast   int8_t   1 B
 */

#include <stdint.h>
#include "types_converter.h"

#define BLOCK_HEADER_SIZE 17

typedef struct {
    int32_t nextblock;
    int32_t block_id;
    int32_t pd_lsn;
    int16_t pd_checksum;
    int16_t pd_flags;
    int8_t  contain_toast;
} BlockHeader;

void block_header_init   (BlockHeader *h);
void block_header_set    (BlockHeader *h, int32_t nextblock, int32_t block_id,
                          int32_t pd_lsn, int16_t pd_checksum,
                          int16_t pd_flags, int8_t contain_toast);

/* Serialization — writes exactly BLOCK_HEADER_SIZE bytes to buf */
int  block_header_marshal  (uint8_t *buf, const BlockHeader *h);
/* Deserialization — reads exactly BLOCK_HEADER_SIZE bytes from buf */
void block_header_unmarshal(BlockHeader *h, const uint8_t *buf);

void block_header_show(const BlockHeader *h);

#endif
