//
// Created by stas on 20.05.2026.
//

#ifndef QUAKEDB3_0_FSMMAPBTREE_H
#define QUAKEDB3_0_FSMMAPBTREE_H

#include <stdint.h>
#include <stdlib.h>
#include "../../bufforing-stm/bufforing-stm/uthash.h"

typedef struct {
    int16_t *pointerTofreeSpaceGaps[BLOCK_SIZE];
    int16_t *sizeFreeSpaceGaps[BLOCK_SIZE];   // only in data case
    int32_t freeSpaceGapsCount;
}FreeSpaceGaps;

typedef struct {
    int32_t blockId;
    FreeSpaceGaps *freeSpaceGaps;
    int32_t blockSize;
    UT_hash_handle hh;
} BtreeBlockEntry;

typedef struct {
    int32_t columnIndex;
    BtreeBlockEntry *blocks;
    int32_t blockCount;
    int32_t currentBlockId;
    UT_hash_handle hh;
} BtreeColumnIndex;

typedef struct {
    int32_t tableId;
    BtreeColumnIndex *columns;
    int32_t columnCount;
    UT_hash_handle hh;
} BtreeTableEntry;

typedef struct {
    BtreeTableEntry *tables;
    int32_t tableCount;
} FSMMapBtree;

static inline void fsm_btree_init(FSMMapBtree *fsm) {
    if (fsm == NULL) return;
    fsm->tables = NULL;
    fsm->tableCount = 0;
}

static inline BtreeTableEntry *fsm_btree_get_table(FSMMapBtree *fsm, int32_t tableId) {
    BtreeTableEntry *entry = NULL;
    HASH_FIND_INT(fsm->tables, &tableId, entry);
    return entry;
}

static inline BtreeTableEntry *fsm_btree_get_or_create_table(FSMMapBtree *fsm, int32_t tableId) {
    BtreeTableEntry *entry = fsm_btree_get_table(fsm, tableId);
    if (entry == NULL) {
        entry = (BtreeTableEntry *)calloc(1, sizeof(BtreeTableEntry));
        if (entry == NULL) return NULL;
        entry->tableId = tableId;
        HASH_ADD_INT(fsm->tables, tableId, entry);
        fsm->tableCount++;
    }
    return entry;
}

static inline BtreeColumnIndex *fsm_btree_get_column(BtreeTableEntry *table, int32_t columnIndex) {
    BtreeColumnIndex *col = NULL;
    HASH_FIND_INT(table->columns, &columnIndex, col);
    return col;
}

static inline BtreeColumnIndex *fsm_btree_get_or_create_column(BtreeTableEntry *table, int32_t columnIndex) {
    BtreeColumnIndex *col = fsm_btree_get_column(table, columnIndex);
    if (col == NULL) {
        col = (BtreeColumnIndex *)calloc(1, sizeof(BtreeColumnIndex));
        if (col == NULL) return NULL;
        col->columnIndex = columnIndex;
        col->currentBlockId = -1;
        HASH_ADD_INT(table->columns, columnIndex, col);
        table->columnCount++;
    }
    return col;
}

static inline BtreeBlockEntry *fsm_btree_get_block(FSMMapBtree *fsm, int32_t tableId,
                                                   int32_t columnIndex, int32_t blockId) {
    BtreeTableEntry *table = fsm_btree_get_table(fsm, tableId);
    if (table == NULL) return NULL;

    BtreeColumnIndex *col = fsm_btree_get_column(table, columnIndex);
    if (col == NULL) return NULL;

    BtreeBlockEntry *block = NULL;
    HASH_FIND_INT(col->blocks, &blockId, block);
    return block;
}

static inline int8_t fsm_btree_add_block(FSMMapBtree *fsm, int32_t tableId, int32_t columnIndex,
                                         int32_t blockId, int32_t blockSize) {
    if (fsm == NULL) return 0;

    BtreeTableEntry *table = fsm_btree_get_or_create_table(fsm, tableId);
    if (table == NULL) return 0;

    BtreeColumnIndex *col = fsm_btree_get_or_create_column(table, columnIndex);
    if (col == NULL) return 0;

    BtreeBlockEntry *block = NULL;
    HASH_FIND_INT(col->blocks, &blockId, block);
    if (block != NULL) return 0;

    block = (BtreeBlockEntry *)calloc(1, sizeof(BtreeBlockEntry));
    if (block == NULL) return 0;
    block->blockId = blockId;
    block->blockSize = blockSize;

    HASH_ADD_INT(col->blocks, blockId, block);
    col->blockCount++;
    col->currentBlockId = blockId;
    return 1;
}

static inline int8_t fsm_btree_update_block_size(FSMMapBtree *fsm, int32_t tableId,
                                                 int32_t columnIndex, int32_t blockId,
                                                 int32_t newSize) {
    BtreeBlockEntry *block = fsm_btree_get_block(fsm, tableId, columnIndex, blockId);
    if (block == NULL) return 0;
    if (newSize < 0 || newSize > btreeFreeSpace) return 0;

    block->blockSize = newSize;
    return 1;
}

static inline int8_t fsm_btree_append_to_block(FSMMapBtree *fsm, int32_t tableId,
                                               int32_t columnIndex, int32_t blockId,
                                               int32_t bytesToAdd) {
    if (bytesToAdd <= 0) return 0;

    BtreeBlockEntry *block = fsm_btree_get_block(fsm, tableId, columnIndex, blockId);
    if (block == NULL) return 0;
    if (block->blockSize + bytesToAdd > btreeFreeSpace) return 0;

    block->blockSize += bytesToAdd;

    BtreeTableEntry *table = fsm_btree_get_table(fsm, tableId);
    if (table != NULL) {
        BtreeColumnIndex *col = fsm_btree_get_column(table, columnIndex);
        if (col != NULL) col->currentBlockId = blockId;
    }

    return 1;
}

static inline int8_t fsm_btree_remove_block(FSMMapBtree *fsm, int32_t tableId,
                                            int32_t columnIndex, int32_t blockId) {
    BtreeTableEntry *table = fsm_btree_get_table(fsm, tableId);
    if (table == NULL) return 0;

    BtreeColumnIndex *col = fsm_btree_get_column(table, columnIndex);
    if (col == NULL) return 0;

    BtreeBlockEntry *block = NULL;
    HASH_FIND_INT(col->blocks, &blockId, block);
    if (block == NULL) return 0;

    HASH_DEL(col->blocks, block);
    free(block);
    col->blockCount--;
    if (col->currentBlockId == blockId) col->currentBlockId = -1;
    return 1;
}

static inline int8_t fsm_btree_create_index(FSMMapBtree *fsm, int32_t tableId, int32_t columnIndex) {
    if (fsm == NULL) return 0;

    BtreeTableEntry *table = fsm_btree_get_or_create_table(fsm, tableId);
    if (table == NULL) return 0;
    if (fsm_btree_get_column(table, columnIndex) != NULL) return 0;

    BtreeColumnIndex *col = (BtreeColumnIndex *)calloc(1, sizeof(BtreeColumnIndex));
    if (col == NULL) return 0;
    col->columnIndex = columnIndex;
    col->currentBlockId = -1;
    HASH_ADD_INT(table->columns, columnIndex, col);
    table->columnCount++;
    return 1;
}

static inline int32_t fsm_btree_get_block_count(FSMMapBtree *fsm, int32_t tableId,
                                                int32_t columnIndex) {
    BtreeTableEntry *table = fsm_btree_get_table(fsm, tableId);
    if (table == NULL) return -1;

    BtreeColumnIndex *col = fsm_btree_get_column(table, columnIndex);
    if (col == NULL) return -1;

    return col->blockCount;
}

static inline int32_t fsm_btree_get_free_space(FSMMapBtree *fsm, int32_t tableId,
                                               int32_t columnIndex, int32_t blockId) {
    BtreeBlockEntry *block = fsm_btree_get_block(fsm, tableId, columnIndex, blockId);
    if (block == NULL) return -1;
    return btreeFreeSpace - block->blockSize;
}

static inline void fsm_btree_free(FSMMapBtree *fsm) {
    if (fsm == NULL) return;

    BtreeTableEntry *table, *tmp_t;
    HASH_ITER(hh, fsm->tables, table, tmp_t) {
        BtreeColumnIndex *col, *tmp_c;
        HASH_ITER(hh, table->columns, col, tmp_c) {
            BtreeBlockEntry *block, *tmp_b;
            HASH_ITER(hh, col->blocks, block, tmp_b) {
                HASH_DEL(col->blocks, block);
                free(block);
            }
            HASH_DEL(table->columns, col);
            free(col);
        }
        HASH_DEL(fsm->tables, table);
        free(table);
    }
    fsm->tables = NULL;
    fsm->tableCount = 0;
}

// freeSpaceFunctions

void deleteElementUpdateSpace(FSMMapBtree *fsm, int32_t tableId,int32_t columnIndex,int32_t block,int32_t startPosition,int32_t dataSize) {
    BtreeBlockEntry* btreeBlockEntry = fsm_btree_get_block(fsm, tableId, columnIndex,block);
    if (btreeBlockEntry == NULL) return;
    if (btreeBlockEntry->freeSpaceGaps == NULL) {
        FreeSpaceGaps *newGaps = malloc(sizeof(FreeSpaceGaps));
        newGaps->freeSpaceGapsCount = 0;
        newGaps->freeSpaceGapsCount++;
        if (block%4==0) {
            newGaps->pointerTofreeSpaceGaps[newGaps->freeSpaceGapsCount] = malloc(sizeof(FreeSpaceGaps));
            newGaps->sizeFreeSpaceGaps[newGaps->freeSpaceGapsCount] = malloc(sizeof(FreeSpaceGaps));
            *(newGaps->pointerTofreeSpaceGaps[newGaps->freeSpaceGapsCount]) = (int16_t)(startPosition%BLOCK_SIZE);
            *(newGaps->sizeFreeSpaceGaps[newGaps->freeSpaceGapsCount]) = dataSize;
        }
        else {
            newGaps->sizeFreeSpaceGaps[newGaps->freeSpaceGapsCount] = malloc(sizeof(FreeSpaceGaps));
            *(newGaps->sizeFreeSpaceGaps[newGaps->freeSpaceGapsCount]) = (int16_t)(startPosition%BLOCK_SIZE);
        }
    }

}


#endif //QUAKEDB3_0_FSMMAPBTREE_H