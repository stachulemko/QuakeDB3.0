#ifndef BLOCK8KB_H
#define BLOCK8KB_H

/*
 * block8kb.h — 8 kb data block (C struct replacing the block8kb class)
 * Fixed-size tuple array instead of vector<tuple>.
 *
 * Binary layout (always BLOCK_SIZE = 8192 bytes):
 *   ID_ALL_BLOCK (int16)
 *   BlockHeader  (17 B)
 *   tuples...
 *   zero padding up to 8192 B
 */

#include <stdint.h>
#include <stdlib.h>

#include "config.h"
#include "type_ids.h"
#include "block_header.h"
#include "tuple.h"

typedef struct {
    BlockHeader header;
    Tuple       tuples[MAX_TUPLES_PER_BLOCK];
    int32_t     tuple_count;
    int32_t     free_space;
    int32_t     usable_size;  /* BLOCK_SIZE - free_space */
} Block8kb;



static inline void create_block8kbM(Block8kb **b) {
    *b = (Block8kb *)malloc(sizeof(Block8kb));
}
static inline void create_block8kbC(Block8kb **b) {
    *b = (Block8kb *)calloc(1, sizeof(Block8kb));
}

void block8kb_init(Block8kb *b, int32_t free_space,
                      int32_t nextblock, int32_t block_id,
                      int32_t pd_lsn, int16_t pd_checksum,
                      int16_t pd_flags, int8_t contain_toast);

/* Bytes already used (header + all serialized tuples) */
int32_t block8kb_used(const Block8kb *b);

/* Returns 1 if tuple t does not fit, 0 if there is space */
int     block8kb_full(const Block8kb *b, const Tuple *t);

/* Add a tuple — returns 0 on success, -1 if no space */
int     block8kb_add(Block8kb *b, const Tuple *t);

/* Serialization — writes exactly BLOCK_SIZE bytes to buf */
int     block8kb_marshal  (uint8_t buf[BLOCK_SIZE], const Block8kb *b);
/* Deserialization — reads from buf[BLOCK_SIZE] */
void    block8kb_unmarshal(Block8kb *b, const uint8_t buf[BLOCK_SIZE]);

void    block8kb_show(const Block8kb *b);

#endif
