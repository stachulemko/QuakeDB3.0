//
// Created by stas on 11.06.2026.
//

#ifndef QUAKEDB3_0_BTREEFILEOPERATION_H
#define QUAKEDB3_0_BTREEFILEOPERATION_H

#include <stdio.h>
#include "types_converter.h"
#include "../../memory-mgmt/memory-mgmt/file_manager_c.h"
#include "fsmMapBtree.h"
#include "log.h"
#include "../../memory-mgmt/memory-mgmt/all_var.h"


typedef struct {
    int32_t pointerToPointerWithData;
    int32_t pointerToNextLabel;
} nodeMetaData;

typedef struct {
    int32_t pointerDataToNextPointerData;
    int32_t pointerToData;
}pointerToData;

typedef struct {
    AllVar data;
    int32_t pointerToBlocks;
}dataBtree;

typedef struct {
    int32_t blockId;
    int32_t pointerToNextBlock;
}Blocks ;



static inline void btree_index_file_path(char *out, size_t outSize, int32_t tableId, int32_t columnIndex);

static inline int8_t btree_save_block_at_path(const char *path, const uint8_t buf[BLOCK_SIZE], int32_t blockNum) {
    FILE *f = fopen(path, "r+b");
    if (f == NULL) {
        LOG_ERROR("btree_save_block_at_path: cannot open %s\n", path);
        return 0;
    }

    if (fseek(f, (long)blockNum * BLOCK_SIZE, SEEK_SET) != 0) {
        fclose(f);
        LOG_ERROR("btree_save_block_at_path: cannot seek block %d in %s\n", blockNum, path);
        return 0;
    }

    size_t written = fwrite(buf, 1, BLOCK_SIZE, f);
    fclose(f);
    return (int8_t)(written == BLOCK_SIZE);
}



static inline void createBtreeFile(int32_t tableId , int32_t columnIndex) {
    char name[64];
    snprintf(name, sizeof(name), "%d.%d", tableId, columnIndex);
    createBinFile(INDEX_TABLE_PATH, name);
}

static inline void createBtree(FSMMapBtree *fsm,
int32_t tableId,int32_t columnIndex) {
    uint8_t buffer[BLOCK_SIZE*4];
    memset(buffer, 0, BLOCK_SIZE * 4);

    nodeMetaData metaData ={-1,-1};
    marshal_int32(buffer, metaData.pointerToPointerWithData); // -1
    marshal_int32(buffer + 4, metaData.pointerToNextLabel);   // -1
    show_bytes(buffer, 8);

    fsm_btree_create_index(fsm, tableId, columnIndex);

    fsm_btree_add_block(fsm, tableId, columnIndex, 0, 8);
    fsm_btree_add_block(fsm, tableId, columnIndex, 1, 0);
    fsm_btree_add_block(fsm, tableId, columnIndex, 2, 0);
    fsm_btree_add_block(fsm, tableId, columnIndex, 3, 0);

    createBtreeFile(tableId, columnIndex);

    char path[512];
    btree_index_file_path(path, sizeof(path), tableId, columnIndex);
    btree_save_block_at_path(path, buffer, 0);
    btree_save_block_at_path(path, buffer + BLOCK_SIZE, 1);
    btree_save_block_at_path(path, buffer + BLOCK_SIZE * 2, 2);
    btree_save_block_at_path(path, buffer + BLOCK_SIZE * 3, 3);
}


typedef struct{
    uint8_t buf[BLOCK_SIZE];
    int32_t tableId;
    int32_t columnIndex;
    int32_t blockId;
    int32_t pinCount;
    int8_t isUsed;
    int8_t isDirty;
}BtreeBuffor;

typedef struct {
    BtreeBuffor *buffors;
    int32_t count;
}BtreeBuffors;

typedef struct {
    int32_t dataOffsets[M];
    AllVar data[M];
    int32_t size;
}DataBtree;


static inline void createBtreeBufforM(BtreeBuffor **buf) {
    *buf = malloc(sizeof(BtreeBuffor));
}
static inline void createBtreeBufforC(BtreeBuffor **buf) {
    *buf = calloc(1,sizeof(BtreeBuffor));
}

static inline void initBtreeBuffors(BtreeBuffors *buffors, int32_t numberOfBuffors) {
    LOG_DEBUG("Initializing btree buffors...");
    buffors->buffors = (BtreeBuffor *)calloc(numberOfBuffors, sizeof(BtreeBuffor));
    if (buffors->buffors != NULL) {
        buffors->count = numberOfBuffors;
    } else {
        buffors->count = 0;
    }
}


static inline void btree_index_file_path(char *out, size_t outSize, int32_t tableId, int32_t columnIndex) {
    snprintf(out, outSize, "%s/%d.%d.bin", INDEX_TABLE_PATH, tableId, columnIndex);
}

static inline BtreeBuffor* getIfExistingBtree(int32_t tableId, int32_t columnIndex, int32_t blockId, BtreeBuffors *buffors) {
    for (int i = 0; i < buffors->count; i++) {
        if (buffors->buffors[i].isUsed == 1
            && buffors->buffors[i].tableId == tableId
            && buffors->buffors[i].columnIndex == columnIndex
            && buffors->buffors[i].blockId == blockId) {
            LOG_DEBUG("Btree block already exists in buffor.");
            buffors->buffors[i].pinCount = 1;
            return &buffors->buffors[i];
        }
    }
    LOG_DEBUG("Btree block does not exist in buffor.");
    return NULL;
}

static inline BtreeBuffor* loadIfSpaceBtree(int32_t tableId, int32_t columnIndex, int32_t blockId, BtreeBuffors *buffors) {
    for (int i = 0; i < buffors->count; i++) {
        if (buffors->buffors[i].isUsed == 0) {
            char path[512];
            btree_index_file_path(path, sizeof(path), tableId, columnIndex);
            uint8_t *diskBuf = fm_get_blockU(path, blockId);
            if (diskBuf == NULL) {
                return NULL;
            }

            LOG_DEBUG("Loading btree block into buffor...");
            memcpy(buffors->buffors[i].buf, diskBuf, BLOCK_SIZE);
            free(diskBuf);

            buffors->buffors[i].tableId = tableId;
            buffors->buffors[i].columnIndex = columnIndex;
            buffors->buffors[i].blockId = blockId;
            buffors->buffors[i].pinCount = 1;
            buffors->buffors[i].isUsed = 1;
            buffors->buffors[i].isDirty = 0;
            return &buffors->buffors[i];
        }
    }
    return NULL;
}

static inline BtreeBuffor* evictBtree(BtreeBuffors *buffors, int32_t tableId, int32_t columnIndex, int32_t blockId) {
    while (1) {
        for (int i = 0; i < buffors->count; i++) {
            if (buffors->buffors[i].isUsed == 1 && buffors->buffors[i].pinCount == 0) {
                BtreeBuffor *victim = &buffors->buffors[i];
                victim->pinCount = 1;
                LOG_DEBUG("Evicting btree block from buffor...");

                if (victim->isDirty == 1) {
                    char oldPath[512];
                    btree_index_file_path(oldPath, sizeof(oldPath), victim->tableId, victim->columnIndex);
                    if (!btree_save_block_at_path(oldPath, victim->buf, victim->blockId)) {
                        victim->pinCount = 0;
                        continue;
                    }
                }

                char newPath[512];
                btree_index_file_path(newPath, sizeof(newPath), tableId, columnIndex);
                uint8_t *diskBuf = fm_get_blockU(newPath, blockId);
                if (diskBuf == NULL) {
                    victim->pinCount = 0;
                    continue;
                }

                memcpy(victim->buf, diskBuf, BLOCK_SIZE);
                free(diskBuf);

                victim->tableId = tableId;
                victim->columnIndex = columnIndex;
                victim->blockId = blockId;
                victim->isDirty = 0;
                victim->isUsed = 1;
                return victim;
            }
        }
    }
}

static inline BtreeBuffor* getBtreeBuffor(int32_t tableId, int32_t columnIndex, int32_t blockId, BtreeBuffors *buffors) {
    BtreeBuffor *existingBuffor = getIfExistingBtree(tableId, columnIndex, blockId, buffors);
    if (existingBuffor != NULL) {
        return existingBuffor;
    }

    BtreeBuffor *loadedBuffor = loadIfSpaceBtree(tableId, columnIndex, blockId, buffors);
    if (loadedBuffor != NULL) {
        return loadedBuffor;
    }

    return evictBtree(buffors, tableId, columnIndex, blockId);
}

/*
  Second commented-out block removed — it was dead code duplicating unused logic sketches.
*/
//--------------------------------------------------------------

static inline int32_t calculateBlock(int32_t pointer) {
    return pointer / BLOCK_SIZE;
}

static inline DataBtree* getData(int32_t start, int32_t tableId, int32_t columnIndex,
                                     BtreeBuffors *btreeBuffors) {
        int32_t offset = start;
        int32_t block = calculateBlock(offset);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor == NULL) return NULL;

        int32_t i = 0;
        // FIX 1: calloc(1, ...) - allocating 1 structure, not M structures!
        DataBtree *data = (DataBtree *)calloc(1, sizeof(DataBtree));
        if (data == NULL) return NULL;

        int32_t pointerToPointerWithData = -1;
        int32_t pointerToNextPointerWithData = -1;
        int32_t pointerToData = -1;
        int16_t type = 0;
        int32_t length = 0;

        int32_t inBlockOffset = offset % BLOCK_SIZE;
        unmarshal_int32(&pointerToPointerWithData, buffor->buf + inBlockOffset);
        if (pointerToPointerWithData == -1) {
            data->size = 0;
            return data;
        }

        int32_t curPtr = pointerToPointerWithData;
        // (Warning: until split is implemented, it reads at most M elements from an infinitely growing list)
        while (curPtr != -1 && i < M) {
            block = calculateBlock(curPtr);
            buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
            if (buffor == NULL) break;

            int32_t curOffset = curPtr % BLOCK_SIZE;
            unmarshal_int32(&pointerToNextPointerWithData, buffor->buf + curOffset);
            unmarshal_int32(&pointerToData, buffor->buf + curOffset + sizeof(int32_t));

            if (pointerToData != -1) {
                int32_t dataBlock = calculateBlock(pointerToData);
                BtreeBuffor *dataBuffor = getBtreeBuffor(tableId, columnIndex, dataBlock, btreeBuffors);
                if (dataBuffor != NULL) {
                    int32_t dataOffset = pointerToData % BLOCK_SIZE;
                    unmarshal_int16(&type, dataBuffor->buf + dataOffset);
                    unmarshal_int32(&length, dataBuffor->buf + dataOffset + sizeof(int16_t));

                    if (type > 0 && length >= 0) {
                        all_var_unmarshal(&(data->data[i]), type, dataBuffor->buf + dataOffset + sizeof(int16_t) + sizeof(int32_t), length);

                        // FIX 2: Correct reading of pointerToBlocks (from the end of data record)
                        int32_t ptrToBlocks = -1;
                        unmarshal_int32(&ptrToBlocks, dataBuffor->buf + dataOffset + sizeof(int16_t) + sizeof(int32_t) + length);

                        data->dataOffsets[i] = ptrToBlocks;
                        i++;
                    }
                }
            }

            curPtr = pointerToNextPointerWithData;
        }

        // FIX 3: Store the number of actually decoded elements
        data->size = i;
        return data;
    }

static inline int32_t findSmaller(AllVar *data, int32_t number, AllVar val) {
    for (int i = 0; i < number; i++) {
        if (all_var_cmp(&data[i], &val) > 0) {
            return i;
        }
    }
    return number;
}


static inline int32_t allocateBlocksEntry(int32_t blockIdVal, BtreeBuffors *btreeBuffors,
                                         int32_t tableId, int32_t columnIndex, FSMMapBtree *fsmMap) {
    BtreeBlockEntry *block3Entry = fsm_btree_get_block(fsmMap, tableId, columnIndex, 3);
    if (block3Entry == NULL) return -1;

    int32_t newBlocksOffset = 3 * BLOCK_SIZE + block3Entry->blockSize;
    int32_t blockId = calculateBlock(newBlocksOffset);
    BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, blockId, btreeBuffors);
    if (buffor == NULL) return -1;

    int32_t inBlockOffset = newBlocksOffset % BLOCK_SIZE;
    marshal_int32(buffor->buf + inBlockOffset, blockIdVal);
    marshal_int32(buffor->buf + inBlockOffset + sizeof(int32_t), -1);
    buffor->isDirty = 1;

    fsm_btree_append_to_block(fsmMap, tableId, columnIndex, 3, sizeof(int32_t) * 2);
    return newBlocksOffset;
}

int32_t getLastBlockOffset(int32_t firstBlockOffset, int32_t tableId, int32_t columnIndex, BtreeBuffors *btreeBuffors) {
    if (firstBlockOffset == -1 || btreeBuffors == NULL) {
        return -1;
    }

    int32_t curOffset = firstBlockOffset;
    int32_t nextOffset = -1;

    while (curOffset != -1) {
        int32_t block = calculateBlock(curOffset);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor == NULL) break;

        int32_t inBlockOffset = curOffset % BLOCK_SIZE;

        // 3. Read pointer to NEXT block (+4 bytes, because first 4B is blockId)
        unmarshal_int32(&nextOffset, buffor->buf + inBlockOffset + sizeof(int32_t));

        // 4. If next is -1, then curOffset is the LAST block!
        if (nextOffset == -1) {
            return curOffset; // Return offset of the last block
        }

        // 5. Move to the next block
        curOffset = nextOffset;
    }

    return curOffset;
}


void addBlockToVal(int32_t lastBlockOffset,int32_t tableId,int32_t columnIndex,BtreeBuffors *btreeBuffors,FSMMapBtree *fsmMap,int32_t blockIdVal) {
    int32_t blockOffset = getLastBlockOffset(lastBlockOffset, tableId, columnIndex, btreeBuffors);
    int32_t blockId = calculateBlock(blockOffset);
    int32_t blockExeededOffset = blockOffset % BLOCK_SIZE;
    BtreeBuffor * buffor = getBtreeBuffor(tableId, columnIndex, blockId, btreeBuffors);
    if (blockOffset != -1) {
        int32_t freeSpace = fsm_btree_get_free_space(fsmMap,tableId,columnIndex,blockExeededOffset);
        fsm_btree_update_block_size(fsmMap,tableId,columnIndex,blockId,freeSpace+sizeof(int32_t));
        marshal_int32(buffor->buf + blockExeededOffset, freeSpace);
        marshal_int32(buffor->buf + freeSpace ,blockId);
    }

}

int8_t checkForEqualAndAdd(AllVar val, int32_t blockIdVal, BtreeBuffors *btreeBuffors, int32_t tableId,
                              int32_t columnIndex, FSMMapBtree *fsmMap,DataBtree *data_btree) {
    DataBtree *dataAllvar = data_btree;
    for (int32_t i = 0; i < dataAllvar->size; i++) {
        if (all_var_cmp(&dataAllvar->data[i], &val) == 0) {
            int32_t indexFinded = dataAllvar->dataOffsets[i];
            addBlockToVal(indexFinded, tableId, columnIndex, btreeBuffors, fsmMap, blockIdVal);
            return 1; // Return 1 if value was added
        }
    }
    return 0; // Return 0 if value was not added
}

static inline void addValDirectly(int32_t offset, AllVar val, int32_t pointerToBlocks, BtreeBuffors *btreeBuffors,
                                  int32_t tableId, int32_t columnIndex) {
    if (btreeBuffors == NULL) return;

    int32_t blockId = calculateBlock(offset);
    BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, blockId, btreeBuffors);
    if (buffor == NULL) return;

    int32_t blockOffset = offset % BLOCK_SIZE;
    marshal_int16(buffor->buf + blockOffset, val.type);
    marshal_int32(buffor->buf + blockOffset + sizeof(int16_t), all_var_size(&val));
    all_var_marshal(buffor->buf + blockOffset + sizeof(int16_t) + sizeof(int32_t), &val);
    marshal_int32(buffor->buf + blockOffset + sizeof(int16_t) + sizeof(int32_t) + all_var_size(&val), pointerToBlocks);
    buffor->isDirty = 1;
}

static inline int32_t getToLastPointerToData(int32_t pointerToDataStart, int32_t tableId,
                                                 int32_t columnIndex, BtreeBuffors *btreeBuffors) {
    // Guard against invalid input data
    if (pointerToDataStart == -1 || btreeBuffors == NULL) {
        return -1;
    }

    int32_t curPtdOffset = pointerToDataStart;
    int32_t lastPtdOffset = curPtdOffset;

    // Traverse the chain until reaching the end (-1)
    while (curPtdOffset != -1) {
        // 1. Calculate block and get buffer
        int32_t block = calculateBlock(curPtdOffset);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor == NULL) {
            break; // Break on read error
        }

        // 2. Read the first 4 bytes in the structure (pointerDataToNextPointerData)
        int32_t inBlockOffset = curPtdOffset % BLOCK_SIZE;
        int32_t nextPtdOffset = -1;
        unmarshal_int32(&nextPtdOffset, buffor->buf + inBlockOffset);

        // 3. If next offset is -1, it means we are at the last element
        if (nextPtdOffset == -1) {
            lastPtdOffset = curPtdOffset;
            break;
        }

        // 4. Move to the next element
        curPtdOffset = nextPtdOffset;
        lastPtdOffset = curPtdOffset;
    }

    // Return offset of the last pointerToData
    return lastPtdOffset;
}

//--------------------- new one -------------------------------- //
static inline int32_t findFreeSpace(int32_t tableId, int32_t columnIndex, int32_t sizeNeeded,
                                        BtreeBuffors *btreeBuffors, int32_t blockEntry, FSMMapBtree *fsmMapBtree) {

    // Start search from the initial block for the given type (e.g., 1, 2, or 3)
    int32_t curBlock = blockEntry;

    while (1) {
        // Check if the given block is already registered in the FSM map
        BtreeBlockEntry *blockMeta = fsm_btree_get_block(fsmMapBtree, tableId, columnIndex, curBlock);

        if (blockMeta == NULL) {
            // Block does not exist in FSM - reached the end of allocated blocks of this type.
            // Register new block with used size = 0.
            fsm_btree_add_block(fsmMapBtree, tableId, columnIndex, curBlock, 0);

            // Return physical offset in file (start of the newly allocated block)
            return curBlock * BLOCK_SIZE;
        }
        else {
            // Block exists. Check if adding 'sizeNeeded' exceeds the limit (btreeFreeSpace).
            if (blockMeta->blockSize + sizeNeeded <= btreeFreeSpace) {
                // There is enough free space! Return the exact offset to write to.
                return (curBlock * BLOCK_SIZE) + blockMeta->blockSize;
            }
        }

        // No space left in this block - jump 4 blocks ahead
        curBlock += 4;
    }
}

static inline int32_t createBlocksEntry(int32_t blockIdVal, int32_t tableId, int32_t columnIndex,
                                            BtreeBuffors *btreeBuffors, FSMMapBtree *fsmMap) {
        int32_t size = sizeof(int32_t) * 2; // 8 bytes (blockId + nextBlockOffset)
        int32_t offset = findFreeSpace(tableId, columnIndex, size, btreeBuffors, 3, fsmMap);

        int32_t block = calculateBlock(offset);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor != NULL) {
            int32_t inBlock = offset % BLOCK_SIZE;
            marshal_int32(buffor->buf + inBlock, blockIdVal);
            marshal_int32(buffor->buf + inBlock + sizeof(int32_t), -1); // initially no next block
            buffor->isDirty = 1;
            fsm_btree_append_to_block(fsmMap, tableId, columnIndex, block, size);
        }
        return offset;
    }

    // 2. Creates entry for dataBtree (Block 2 - key values and pointer to Blocks)
    static inline int32_t createDataEntry(AllVar val, int32_t pointerToBlocks, int32_t tableId,
                                          int32_t columnIndex, BtreeBuffors *btreeBuffors, FSMMapBtree *fsmMap) {
        int32_t dataSize = sizeof(int16_t) + sizeof(int32_t) + all_var_size(&val) + sizeof(int32_t);
        int32_t offset = findFreeSpace(tableId, columnIndex, dataSize, btreeBuffors, 2, fsmMap);

        int32_t block = calculateBlock(offset);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor != NULL) {
            int32_t inBlock = offset % BLOCK_SIZE;
            marshal_int16(buffor->buf + inBlock, val.type);
            marshal_int32(buffor->buf + inBlock + sizeof(int16_t), all_var_size(&val));
            all_var_marshal(buffor->buf + inBlock + sizeof(int16_t) + sizeof(int32_t), &val);
            marshal_int32(buffor->buf + inBlock + sizeof(int16_t) + sizeof(int32_t) + all_var_size(&val), pointerToBlocks);
            buffor->isDirty = 1;
            fsm_btree_append_to_block(fsmMap, tableId, columnIndex, block, dataSize);
        }
        return offset;
    }

    // 3. Main orchestrating function, creates pointerToData (Block 1) calling deeper functions
    static inline void insertNewDataToLeaf(AllVar val, int32_t blockIdVal, int32_t lastPtdOffset, int32_t nodeOffset,
                                           int32_t tableId, int32_t columnIndex, BtreeBuffors *btreeBuffors, FSMMapBtree *fsmMap) {

        // A) Create from deepest layer: blocks entry (Block 3)
        int32_t blocksOffset = createBlocksEntry(blockIdVal, tableId, columnIndex, btreeBuffors, fsmMap);

        // B) Create value (Block 2) linked to created blocks entry
        int32_t dataOffset = createDataEntry(val, blocksOffset, tableId, columnIndex, btreeBuffors, fsmMap);

        // C) Create pointerToData (Block 1) and link it to the list in the leaf
        int32_t size = sizeof(int32_t) * 2; // 8 bytes (nextPtd + dataPtr)
        int32_t newPtdOffset = findFreeSpace(tableId, columnIndex, size, btreeBuffors, 1, fsmMap);

        int32_t block = calculateBlock(newPtdOffset);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor != NULL) {
            int32_t inBlock = newPtdOffset % BLOCK_SIZE;
            marshal_int32(buffor->buf + inBlock, -1); // Next -> -1
            marshal_int32(buffor->buf + inBlock + sizeof(int32_t), dataOffset);
            buffor->isDirty = 1;
            fsm_btree_append_to_block(fsmMap, tableId, columnIndex, block, size);
        }

        // D) Link new element to "last" (previous on the list) or to node head
        if (lastPtdOffset != -1) {
            int32_t prevBlock = calculateBlock(lastPtdOffset);
            BtreeBuffor *prevBuf = getBtreeBuffor(tableId, columnIndex, prevBlock, btreeBuffors);
            if (prevBuf != NULL) {
                marshal_int32(prevBuf->buf + (lastPtdOffset % BLOCK_SIZE), newPtdOffset);
                prevBuf->isDirty = 1;
            }
        } else {
            // If node was completely empty, link to header (nodeOffset)
            int32_t nodeBlock = calculateBlock(nodeOffset);
            BtreeBuffor *nodeBuf = getBtreeBuffor(tableId, columnIndex, nodeBlock, btreeBuffors);
            if (nodeBuf != NULL) {
                marshal_int32(nodeBuf->buf + (nodeOffset % BLOCK_SIZE), newPtdOffset);
                nodeBuf->isDirty = 1;
            }
        }
    }

//--------------------- new one -------------------------------- //


static inline void addToBtree(AllVar val, int32_t blockIdVal, BtreeBuffors *btreeBuffors, int32_t tableId,
                              int32_t columnIndex, FSMMapBtree *fsmMap) {
        printf("ADDING VALUE TO TREE (Type: %d, Val: %d)\n", val.type, val.val.i32);
        if (btreeBuffors == NULL || fsmMap == NULL) {
            LOG_ERROR("addToBtree: Buffors or FSMMap is NULL");
            return;
        }

        BtreeTableEntry *table = fsm_btree_get_table(fsmMap, tableId);
        if (table == NULL) return;

        BtreeColumnIndex *col = fsm_btree_get_column(table, columnIndex);
        if (col == NULL) return;

        int32_t offset = 0;
        int32_t nextLevel = 0;

        while (offset != -1) {
            int32_t block = calculateBlock(offset);
            BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
            if (buffor == NULL) break;

            int32_t nextLevelPtr = -1;
            unmarshal_int32(&nextLevelPtr, buffor->buf + (offset % BLOCK_SIZE) + sizeof(int32_t));
            if (nextLevelPtr == -1) {
                break;
            }

            DataBtree *dataAllvar = getData(offset, tableId, columnIndex, btreeBuffors);
            if (dataAllvar == NULL) break;

            if (checkForEqualAndAdd(val, blockIdVal, btreeBuffors, tableId, columnIndex, fsmMap, dataAllvar)) {
                free(dataAllvar);
                return;
            }

            int32_t idx = findSmaller(dataAllvar->data, dataAllvar->size, val);

            offset = nextLevelPtr + (int32_t)(idx * sizeof(int32_t));
            free(dataAllvar);
        }
        int32_t nodeBlock = calculateBlock(offset);
        BtreeBuffor *nodeBuffor = getBtreeBuffor(tableId, columnIndex, nodeBlock, btreeBuffors);
        int32_t pointerToPointerWithData = -1;

        if (nodeBuffor != NULL) {
            unmarshal_int32(&pointerToPointerWithData, nodeBuffor->buf + (offset % BLOCK_SIZE));
        }

        int32_t last = getToLastPointerToData(pointerToPointerWithData, tableId, columnIndex, btreeBuffors);

        insertNewDataToLeaf(val, blockIdVal, last, offset, tableId, columnIndex, btreeBuffors, fsmMap);
}

static inline void addExistingValues(FSMMapBtree *fsm,
                                     int32_t tableId,
                                     int32_t columnIndex,
                                     BtreeBuffors *btreeBuffors,
                                     Buffors *buffors,
                                     FSMCache *fsmCache) {
    if (fsm == NULL || btreeBuffors == NULL || buffors == NULL || fsmCache == NULL) {
        LOG_ERROR("addExistingValues: invalid args");
        return;
    }

    BlockCounterEntry *counter = fsm_cache_get(fsmCache, tableId);
    if (counter == NULL) {
        LOG_ERROR("addExistingValues: table %d not found in fsmCache", tableId);
        return;
    }

    for (int32_t blockId = 0; blockId <= counter->maxBlock; ++blockId) {
        DataBuffor *dataBuffor = getBuffor(tableId, blockId, buffors);
        if (dataBuffor == NULL ||
            dataBuffor->universalBlock == NULL ||
            dataBuffor->universalBlock->block == NULL) {
            continue;
            }

        Block8kb *block = dataBuffor->universalBlock->block;

        for (int32_t t = 0; t < block->tuple_count; ++t) {
            Tuple *tuple = &block->tuples[t];
            if (columnIndex < 0 || columnIndex >= tuple->dnb.data_count) {
                continue;
            }

            addToBtree(tuple->dnb.data[columnIndex], blockId, btreeBuffors, tableId, columnIndex, fsm);
        }

        dataBuffor->pinCount = 0;
    }
}


static inline int32_t getBlockBtree(AllVar val, BtreeBuffors *btreeBuffors, int32_t tableId,
                                        int32_t columnIndex, FSMMapBtree *fsmMap, int8_t operator, int16_t method) {
        if (btreeBuffors == NULL || fsmMap == NULL) {
            LOG_ERROR("getBlockBtree: Buffors or FSMMap is NULL");
            return -1;
        }

        BtreeTableEntry *table = fsm_btree_get_table(fsmMap, tableId);
        if (table == NULL) return -1;

        BtreeColumnIndex *col = fsm_btree_get_column(table, columnIndex);
        if (col == NULL) return -1;

        int32_t offset = 0;

        // 1. Traversing down the tree (internal nodes)
        while (offset != -1) {
            int32_t block = calculateBlock(offset);
            BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
            if (buffor == NULL) break;

            int32_t nextLevelPtr = -1;
            unmarshal_int32(&nextLevelPtr, buffor->buf + (offset % BLOCK_SIZE) + sizeof(int32_t));
            if (nextLevelPtr == -1) {
                break;
            }

            DataBtree *dataAllvar = getData(offset, tableId, columnIndex, btreeBuffors);
            if (dataAllvar == NULL) break;

            int32_t validCount = 0;
            for (int i = 0; i < M; i++) {
                if (dataAllvar->data[i].type > 0) {
                    validCount++;
                }
            }

            int32_t idx = findSmaller(dataAllvar->data, validCount, val);
            offset = nextLevelPtr + (int32_t)(idx * sizeof(int32_t));
            free(dataAllvar);
        }

        DataBtree *dataAllvar = getData(offset, tableId, columnIndex, btreeBuffors);
        if (dataAllvar == NULL) return -1;

        for (int i = 0; i < M; i++) {
            if (dataAllvar->data[i].type > 0) {
                if (evaluateAllVar(&dataAllvar->data[i], &val, operator, method) == 1) {
                    int32_t ptrToBlocks = dataAllvar->dataOffsets[i];
                    free(dataAllvar);

                    if (ptrToBlocks != -1) {
                        int32_t curBlk = calculateBlock(ptrToBlocks);
                        BtreeBuffor *curBuffor = getBtreeBuffor(tableId, columnIndex, curBlk, btreeBuffors);
                        if (curBuffor != NULL) {
                            int32_t bId = -1;
                            unmarshal_int32(&bId, curBuffor->buf + (ptrToBlocks % BLOCK_SIZE));
                            return bId;
                        }
                    }
                    return -1;
                }
            }
        }

        free(dataAllvar);
        return -1;
    }


//------------------------------------------delete-----------------------------

void deleteBlock(FSMMapBtree *fsm,BtreeBuffors *btreeBuffors, int32_t tableId, int32_t columnIndex,int32_t blockId,int32_t offset) {
    int32_t blockEach;
    int32_t prevOffset = -1;
    int32_t block = calculateBlock(offset);
    BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
    unmarshal_int32(&blockEach,buffor->buf + (offset % BLOCK_SIZE));
    while (blockEach!=blockId) {
        prevOffset = offset;
        unmarshal_int32(&offset,buffor->buf + (offset % BLOCK_SIZE) + sizeof(int32_t));
        block = calculateBlock(offset);
        buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        unmarshal_int32(&blockEach,buffor->buf + (offset % BLOCK_SIZE) + sizeof(int32_t));
    }
    if (prevOffset != -1) {
        int32_t nextOffset;
        unmarshal_int32(&nextOffset,buffor->buf + (offset % BLOCK_SIZE) + sizeof(int32_t));

        memset(buffor->buf + offset, 0, 8);
        block = calculateBlock(offset);
        buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        deleteElementUpdateSpace(fsm,tableId,columnIndex,block,offset,8);
        marshal_int32(buffor->buf+prevOffset+sizeof(int32_t), nextOffset );
    }
    else {

    }

}

/* ═══════════════════════════════════════════════════════════════════════════
 *  HELPER STRUCT — stores offsets needed for value deletion
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    int32_t ptdOffset;       // PointerToData entry offset (Block 1)
    int32_t prevPtdOffset;   // Previous PTD offset (-1 if head)
    int32_t dataOffset;      // Data entry offset (Block 2)
    int32_t ptrToBlocks;     // Block list head offset (Block 3)
    int8_t  found;           // 1 if value found
} BtreeDeleteInfo;


/* ═══════════════════════════════════════════════════════════════════════════
 *  STEP 1 — REMOVING BLOCK FROM LIST (Block 3)
 *
 *  Removes blockId from the linked list of blocks.
 *  Returns new head offset of the list (-1 if the list is now empty).
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline int32_t deleteBlockEntry(FSMMapBtree *fsm, BtreeBuffors *btreeBuffors,
                                       int32_t tableId, int32_t columnIndex,
                                       int32_t blockId, int32_t headOffset) {
    if (headOffset == -1) return -1;

    int32_t curOffset = headOffset;
    int32_t prevOffset = -1;

    while (curOffset != -1) {
        int32_t block = calculateBlock(curOffset);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor == NULL) return headOffset;

        int32_t inBlock = curOffset % BLOCK_SIZE;
        int32_t curBlockId = 0;
        int32_t nextOffset = -1;
        unmarshal_int32(&curBlockId, buffor->buf + inBlock);
        unmarshal_int32(&nextOffset, buffor->buf + inBlock + sizeof(int32_t));

        if (curBlockId == blockId) {
            // Found — zero out this entry (8 bytes: blockId + nextOffset)
            memset(buffor->buf + inBlock, 0, 8);
            buffor->isDirty = 1;
            deleteElementUpdateSpace(fsm, tableId, columnIndex, block, curOffset, 8);

            if (prevOffset != -1) {
                // Removing from middle/end — rewire prev->next
                int32_t prevBlock = calculateBlock(prevOffset);
                BtreeBuffor *prevBuf = getBtreeBuffor(tableId, columnIndex, prevBlock, btreeBuffors);
                if (prevBuf != NULL) {
                    marshal_int32(prevBuf->buf + (prevOffset % BLOCK_SIZE) + sizeof(int32_t), nextOffset);
                    prevBuf->isDirty = 1;
                }
                return headOffset;
            } else {
                // Removing head — new head is nextOffset (-1 if list is empty)
                return nextOffset;
            }
        }

        prevOffset = curOffset;
        curOffset = nextOffset;
    }

    return headOffset; // Not found — do not change anything
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  STEP 2 — UPDATING ptrToBlocks IN DATA ENTRY (Block 2)
 *
 *  When the block list head has changed, update the pointer in DataEntry.
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline void updateDataEntryBlocksPtr(BtreeBuffors *btreeBuffors,
                                            int32_t tableId, int32_t columnIndex,
                                            int32_t dataOffset, AllVar *val,
                                            int32_t newPtrToBlocks) {
    if (dataOffset == -1) return;

    int32_t block = calculateBlock(dataOffset);
    BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
    if (buffor == NULL) return;

    int32_t inBlock = dataOffset % BLOCK_SIZE;
    // ptrToBlocks is at the end of entry: type(2B) + length(4B) + value(varLen)
    int32_t ptrOffset = inBlock + sizeof(int16_t) + sizeof(int32_t) + all_var_size(val);
    marshal_int32(buffor->buf + ptrOffset, newPtrToBlocks);
    buffor->isDirty = 1;
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  STEP 3 — REMOVING DATA ENTRY (Block 2)
 *
 *  Zeros out the entire data entry when the block list is empty.
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline void deleteDataEntry(FSMMapBtree *fsm, BtreeBuffors *btreeBuffors,
                                   int32_t tableId, int32_t columnIndex,
                                   int32_t dataOffset, AllVar *val) {
    if (dataOffset == -1) return;

    int32_t block = calculateBlock(dataOffset);
    BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
    if (buffor == NULL) return;

    // Entry size: type(2B) + length(4B) + value(varLen) + ptrToBlocks(4B)
    int32_t dataSize = sizeof(int16_t) + sizeof(int32_t) + all_var_size(val) + sizeof(int32_t);
    int32_t inBlock = dataOffset % BLOCK_SIZE;

    memset(buffor->buf + inBlock, 0, dataSize);
    buffor->isDirty = 1;
    deleteElementUpdateSpace(fsm, tableId, columnIndex, block, dataOffset, dataSize);
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  STEP 4 — REMOVING POINTER-TO-DATA (Block 1)
 *
 *  Removes PTD entry from linked list and updates head in node if needed.
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline void deletePointerToDataEntry(FSMMapBtree *fsm, BtreeBuffors *btreeBuffors,
                                            int32_t tableId, int32_t columnIndex,
                                            int32_t nodeOffset,
                                            int32_t ptdOffset, int32_t prevPtdOffset) {
    if (ptdOffset == -1) return;

    int32_t block = calculateBlock(ptdOffset);
    BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
    if (buffor == NULL) return;

    int32_t inBlock = ptdOffset % BLOCK_SIZE;

    // Read nextPtd before zeroing out
    int32_t nextPtd = -1;
    unmarshal_int32(&nextPtd, buffor->buf + inBlock);

    // Zero out PTD entry (8 bytes: nextPtd + dataPtr)
    memset(buffor->buf + inBlock, 0, 8);
    buffor->isDirty = 1;
    deleteElementUpdateSpace(fsm, tableId, columnIndex, block, ptdOffset, 8);

    if (prevPtdOffset != -1) {
        // Rewire prev->next to our next
        int32_t prevBlock = calculateBlock(prevPtdOffset);
        BtreeBuffor *prevBuf = getBtreeBuffor(tableId, columnIndex, prevBlock, btreeBuffors);
        if (prevBuf != NULL) {
            marshal_int32(prevBuf->buf + (prevPtdOffset % BLOCK_SIZE), nextPtd);
            prevBuf->isDirty = 1;
        }
    } else {
        // Removing PTD list head — update pointer in node (Block 0)
        int32_t nodeBlock = calculateBlock(nodeOffset);
        BtreeBuffor *nodeBuf = getBtreeBuffor(tableId, columnIndex, nodeBlock, btreeBuffors);
        if (nodeBuf != NULL) {
            marshal_int32(nodeBuf->buf + (nodeOffset % BLOCK_SIZE), nextPtd);
            nodeBuf->isDirty = 1;
        }
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  HELPER — SEARCHING VALUE IN LEAF
 *
 *  Traverses the PTD list in the node and searches for value val.
 *  Returns BtreeDeleteInfo with all offsets needed for deletion.
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline BtreeDeleteInfo findValueInLeaf(int32_t nodeOffset, AllVar *val,
                                              int32_t tableId, int32_t columnIndex,
                                              BtreeBuffors *btreeBuffors) {
    BtreeDeleteInfo info = {-1, -1, -1, -1, 0};

    int32_t nodeBlock = calculateBlock(nodeOffset);
    BtreeBuffor *nodeBuf = getBtreeBuffor(tableId, columnIndex, nodeBlock, btreeBuffors);
    if (nodeBuf == NULL) return info;

    int32_t ptdHead = -1;
    unmarshal_int32(&ptdHead, nodeBuf->buf + (nodeOffset % BLOCK_SIZE));

    int32_t curPtd = ptdHead;
    int32_t prevPtd = -1;

    while (curPtd != -1) {
        int32_t block = calculateBlock(curPtd);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor == NULL) return info;

        int32_t inBlock = curPtd % BLOCK_SIZE;
        int32_t nextPtd = -1;
        int32_t dataPtr = -1;
        unmarshal_int32(&nextPtd, buffor->buf + inBlock);
        unmarshal_int32(&dataPtr, buffor->buf + inBlock + sizeof(int32_t));

        if (dataPtr != -1) {
            int32_t dataBlock = calculateBlock(dataPtr);
            BtreeBuffor *dataBuf = getBtreeBuffor(tableId, columnIndex, dataBlock, btreeBuffors);
            if (dataBuf != NULL) {
                int32_t dataInBlock = dataPtr % BLOCK_SIZE;
                int16_t type = 0;
                int32_t length = 0;
                unmarshal_int16(&type, dataBuf->buf + dataInBlock);
                unmarshal_int32(&length, dataBuf->buf + dataInBlock + sizeof(int16_t));

                if (type > 0 && length >= 0) {
                    AllVar curVal;
                    all_var_unmarshal(&curVal, type,
                                     dataBuf->buf + dataInBlock + sizeof(int16_t) + sizeof(int32_t),
                                     length);

                    if (all_var_cmp(&curVal, val) == 0) {
                        int32_t ptrToBlocks = -1;
                        unmarshal_int32(&ptrToBlocks,
                                        dataBuf->buf + dataInBlock + sizeof(int16_t) + sizeof(int32_t) + length);

                        info.ptdOffset     = curPtd;
                        info.prevPtdOffset = prevPtd;
                        info.dataOffset    = dataPtr;
                        info.ptrToBlocks   = ptrToBlocks;
                        info.found         = 1;
                        return info;
                    }
                }
            }
        }

        prevPtd = curPtd;
        curPtd = nextPtd;
    }

    return info;
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  HELPER — CHECKING IF NODE IS EMPTY
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline int8_t isNodeEmpty(int32_t nodeOffset, int32_t tableId,
                                 int32_t columnIndex, BtreeBuffors *btreeBuffors) {
    int32_t block = calculateBlock(nodeOffset);
    BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
    if (buffor == NULL) return 1;

    int32_t ptdHead = -1;
    unmarshal_int32(&ptdHead, buffor->buf + (nodeOffset % BLOCK_SIZE));
    return (ptdHead == -1) ? 1 : 0;
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  HELPER — COUNTING KEYS IN NODE
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline int32_t countNodeKeys(int32_t nodeOffset, int32_t tableId,
                                    int32_t columnIndex, BtreeBuffors *btreeBuffors) {
    int32_t block = calculateBlock(nodeOffset);
    BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
    if (buffor == NULL) return 0;

    int32_t ptdHead = -1;
    unmarshal_int32(&ptdHead, buffor->buf + (nodeOffset % BLOCK_SIZE));

    int32_t count = 0;
    int32_t curPtd = ptdHead;
    while (curPtd != -1) {
        count++;
        int32_t ptdBlock = calculateBlock(curPtd);
        BtreeBuffor *ptdBuf = getBtreeBuffor(tableId, columnIndex, ptdBlock, btreeBuffors);
        if (ptdBuf == NULL) break;
        unmarshal_int32(&curPtd, ptdBuf->buf + (curPtd % BLOCK_SIZE));
    }
    return count;
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  STEP 5a — REDISTRIBUTION BETWEEN NODES (Block 0)
 *
 *  Transfers LAST key from source node to destination node.
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline void redistributeFromNode(FSMMapBtree *fsm, BtreeBuffors *btreeBuffors,
                                        int32_t tableId, int32_t columnIndex,
                                        int32_t srcNodeOffset, int32_t destNodeOffset) {
    int32_t srcBlock = calculateBlock(srcNodeOffset);
    BtreeBuffor *srcBuf = getBtreeBuffor(tableId, columnIndex, srcBlock, btreeBuffors);
    if (srcBuf == NULL) return;

    int32_t ptdHead = -1;
    unmarshal_int32(&ptdHead, srcBuf->buf + (srcNodeOffset % BLOCK_SIZE));
    if (ptdHead == -1) return;

    // Traverse to the last element
    int32_t curPtd = ptdHead;
    int32_t prevPtd = -1;
    int32_t nextPtd = -1;

    while (1) {
        int32_t ptdBlock = calculateBlock(curPtd);
        BtreeBuffor *ptdBuf = getBtreeBuffor(tableId, columnIndex, ptdBlock, btreeBuffors);
        if (ptdBuf == NULL) return;

        unmarshal_int32(&nextPtd, ptdBuf->buf + (curPtd % BLOCK_SIZE));
        if (nextPtd == -1) break;

        prevPtd = curPtd;
        curPtd = nextPtd;
    }

    // Disconnect the last PTD from srcNode
    if (prevPtd != -1) {
        int32_t prevBlock = calculateBlock(prevPtd);
        BtreeBuffor *prevBuf = getBtreeBuffor(tableId, columnIndex, prevBlock, btreeBuffors);
        if (prevBuf != NULL) {
            marshal_int32(prevBuf->buf + (prevPtd % BLOCK_SIZE), -1);
            prevBuf->isDirty = 1;
        }
    } else {
        srcBuf = getBtreeBuffor(tableId, columnIndex, srcBlock, btreeBuffors);
        if (srcBuf != NULL) {
            marshal_int32(srcBuf->buf + (srcNodeOffset % BLOCK_SIZE), -1);
            srcBuf->isDirty = 1;
        }
    }

    // Insert the moved PTD at the beginning of destNode list
    int32_t destBlock = calculateBlock(destNodeOffset);
    BtreeBuffor *destBuf = getBtreeBuffor(tableId, columnIndex, destBlock, btreeBuffors);
    if (destBuf == NULL) return;

    int32_t destHead = -1;
    unmarshal_int32(&destHead, destBuf->buf + (destNodeOffset % BLOCK_SIZE));

    int32_t movedBlock = calculateBlock(curPtd);
    BtreeBuffor *movedBuf = getBtreeBuffor(tableId, columnIndex, movedBlock, btreeBuffors);
    if (movedBuf != NULL) {
        marshal_int32(movedBuf->buf + (curPtd % BLOCK_SIZE), destHead);
        movedBuf->isDirty = 1;
    }

    destBuf = getBtreeBuffor(tableId, columnIndex, destBlock, btreeBuffors);
    if (destBuf != NULL) {
        marshal_int32(destBuf->buf + (destNodeOffset % BLOCK_SIZE), curPtd);
        destBuf->isDirty = 1;
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  STEP 5b — MERGING TWO NODES (Block 0)
 *
 *  Transfers ALL keys from srcNode to the end of destNode.
 *  After the operation, srcNode is empty.
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline void mergeNodes(FSMMapBtree *fsm, BtreeBuffors *btreeBuffors,
                              int32_t tableId, int32_t columnIndex,
                              int32_t srcNodeOffset, int32_t destNodeOffset) {
    int32_t srcBlock = calculateBlock(srcNodeOffset);
    BtreeBuffor *srcBuf = getBtreeBuffor(tableId, columnIndex, srcBlock, btreeBuffors);
    if (srcBuf == NULL) return;

    int32_t srcHead = -1;
    unmarshal_int32(&srcHead, srcBuf->buf + (srcNodeOffset % BLOCK_SIZE));
    if (srcHead == -1) return;

    int32_t destBlock = calculateBlock(destNodeOffset);
    BtreeBuffor *destBuf = getBtreeBuffor(tableId, columnIndex, destBlock, btreeBuffors);
    if (destBuf == NULL) return;

    int32_t destHead = -1;
    unmarshal_int32(&destHead, destBuf->buf + (destNodeOffset % BLOCK_SIZE));

    if (destHead == -1) {
        // destNode is empty — head becomes srcHead
        marshal_int32(destBuf->buf + (destNodeOffset % BLOCK_SIZE), srcHead);
        destBuf->isDirty = 1;
    } else {
        // Search for the last PTD in destNode
        int32_t lastPtd = destHead;
        int32_t nextPtd = -1;
        while (1) {
            int32_t ptdBlock = calculateBlock(lastPtd);
            BtreeBuffor *ptdBuf = getBtreeBuffor(tableId, columnIndex, ptdBlock, btreeBuffors);
            if (ptdBuf == NULL) return;

            unmarshal_int32(&nextPtd, ptdBuf->buf + (lastPtd % BLOCK_SIZE));
            if (nextPtd == -1) break;
            lastPtd = nextPtd;
        }

        // Attach srcHead to the end of destNode
        int32_t lastBlock = calculateBlock(lastPtd);
        BtreeBuffor *lastBuf = getBtreeBuffor(tableId, columnIndex, lastBlock, btreeBuffors);
        if (lastBuf != NULL) {
            marshal_int32(lastBuf->buf + (lastPtd % BLOCK_SIZE), srcHead);
            lastBuf->isDirty = 1;
        }
    }

    // Zero out head of srcNode
    srcBuf = getBtreeBuffor(tableId, columnIndex, srcBlock, btreeBuffors);
    if (srcBuf != NULL) {
        marshal_int32(srcBuf->buf + (srcNodeOffset % BLOCK_SIZE), -1);
        srcBuf->isDirty = 1;
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  STEP 5c — REMOVING EMPTY CHILD FROM PARENT (Block 0)
 *
 *  Removes empty child node from parent's pointer array
 *  and shifts subsequent pointers one position to the left.
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline void removeChildFromParent(FSMMapBtree *fsm, BtreeBuffors *btreeBuffors,
                                         int32_t tableId, int32_t columnIndex,
                                         int32_t parentOffset, int32_t childOffset) {
    if (parentOffset == -1 || childOffset == -1) return;

    int32_t parentBlock = calculateBlock(parentOffset);
    BtreeBuffor *parentBuf = getBtreeBuffor(tableId, columnIndex, parentBlock, btreeBuffors);
    if (parentBuf == NULL) return;

    int32_t nextLevelPtr = -1;
    unmarshal_int32(&nextLevelPtr, parentBuf->buf + (parentOffset % BLOCK_SIZE) + sizeof(int32_t));
    if (nextLevelPtr == -1) return;

    for (int i = 0; i <= M; i++) {
        int32_t slotOffset = nextLevelPtr + (int32_t)(i * sizeof(int32_t));
        int32_t slotBlock = calculateBlock(slotOffset);
        BtreeBuffor *slotBuf = getBtreeBuffor(tableId, columnIndex, slotBlock, btreeBuffors);
        if (slotBuf == NULL) return;

        int32_t childPtr = -1;
        unmarshal_int32(&childPtr, slotBuf->buf + (slotOffset % BLOCK_SIZE));
        if (childPtr == -1) break;

        if (childPtr == childOffset) {
            // Shift subsequent pointers one position to the left
            for (int j = i; j < M; j++) {
                int32_t nextSlot = nextLevelPtr + (int32_t)((j + 1) * sizeof(int32_t));
                int32_t nextSlotBlock = calculateBlock(nextSlot);
                BtreeBuffor *nextSlotBuf = getBtreeBuffor(tableId, columnIndex, nextSlotBlock, btreeBuffors);
                if (nextSlotBuf == NULL) break;

                int32_t nextChild = -1;
                unmarshal_int32(&nextChild, nextSlotBuf->buf + (nextSlot % BLOCK_SIZE));

                int32_t curSlot = nextLevelPtr + (int32_t)(j * sizeof(int32_t));
                int32_t curSlotBlock = calculateBlock(curSlot);
                BtreeBuffor *curSlotBuf = getBtreeBuffor(tableId, columnIndex, curSlotBlock, btreeBuffors);
                if (curSlotBuf != NULL) {
                    marshal_int32(curSlotBuf->buf + (curSlot % BLOCK_SIZE), nextChild);
                    curSlotBuf->isDirty = 1;
                }

                if (nextChild == -1) break;
            }
            return;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  ORCHESTRATOR — deleteVal
 *
 *  Combines all steps:
 *    1. B-tree navigation down to leaf
 *    2. findValueInLeaf — searches for value and collects offsets
 *    3. deleteBlockEntry — removes blockId from block list (Block 3)
 *    4. If list is empty → deleteDataEntry + deletePointerToDataEntry
 *    5. If node is empty → removeChildFromParent
 * ═══════════════════════════════════════════════════════════════════════════ */

void deleteVal(FSMMapBtree *fsm, BtreeBuffors *btreeBuffors,
               int32_t tableId, int32_t columnIndex,
               int32_t blockId, AllVar val) {
    if (btreeBuffors == NULL || fsm == NULL) return;

    BtreeTableEntry *table = fsm_btree_get_table(fsm, tableId);
    if (table == NULL) return;

    BtreeColumnIndex *col = fsm_btree_get_column(table, columnIndex);
    if (col == NULL) return;

    int32_t offset = 0;
    int32_t parentOffset = -1;

    /* ── 1. Navigation down the tree ── */
    while (offset != -1) {
        int32_t block = calculateBlock(offset);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor == NULL) return;

        int32_t nextLevelPtr = -1;
        unmarshal_int32(&nextLevelPtr, buffor->buf + (offset % BLOCK_SIZE) + sizeof(int32_t));
        if (nextLevelPtr == -1) {
            break;
        }

        DataBtree *dataAllvar = getData(offset, tableId, columnIndex, btreeBuffors);
        if (dataAllvar == NULL) return;

        int32_t idx = findSmaller(dataAllvar->data, dataAllvar->size, val);
        parentOffset = offset;
        offset = nextLevelPtr + (int32_t)(idx * sizeof(int32_t));
        free(dataAllvar);
    }

    /* ── 2. Search for value in leaf ── */
    BtreeDeleteInfo info = findValueInLeaf(offset, &val, tableId, columnIndex, btreeBuffors);
    if (!info.found) return;

    /* ── 3. Remove blockId from block list (Block 3) ── */
    int32_t newHead = deleteBlockEntry(fsm, btreeBuffors, tableId, columnIndex,
                                       blockId, info.ptrToBlocks);

    if (newHead != info.ptrToBlocks) {
        if (newHead != -1) {
            /* Head changed but list is not empty — update pointer */
            updateDataEntryBlocksPtr(btreeBuffors, tableId, columnIndex,
                                     info.dataOffset, &val, newHead);
        } else {
            /* ── 4. Block list empty — remove DataEntry and PTD ── */
            deleteDataEntry(fsm, btreeBuffors, tableId, columnIndex,
                            info.dataOffset, &val);

            deletePointerToDataEntry(fsm, btreeBuffors, tableId, columnIndex,
                                     offset, info.ptdOffset, info.prevPtdOffset);

            /* ── 5. Check if node requires cleanup ── */
            if (isNodeEmpty(offset, tableId, columnIndex, btreeBuffors) && parentOffset != -1) {
                removeChildFromParent(fsm, btreeBuffors, tableId, columnIndex,
                                      parentOffset, offset);
            }
        }
    }
}


#endif //QUAKEDB3_0_BTREEFILEOPERATION_H
