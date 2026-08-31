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
    int32_t dataOffsets[M + 2];
    AllVar data[M + 2];
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
        if (data == NULL) {
            buffor->pinCount = 0;
            return NULL;
        }

        int32_t pointerToPointerWithData = -1;
        int32_t pointerToNextPointerWithData = -1;
        int32_t pointerToData = -1;
        int16_t type = 0;
        int32_t length = 0;

        int32_t inBlockOffset = offset % BLOCK_SIZE;
        unmarshal_int32(&pointerToPointerWithData, buffor->buf + inBlockOffset);
        buffor->pinCount = 0;
        if (pointerToPointerWithData == -1) {
            data->size = 0;
            return data;
        }

        int32_t curPtr = pointerToPointerWithData;
        // (Warning: until split is implemented, it reads at most M elements from an infinitely growing list)
        while (curPtr != -1 && i < M + 2) {
            block = calculateBlock(curPtr);
            buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
            if (buffor == NULL) break;

            int32_t curOffset = curPtr % BLOCK_SIZE;
            unmarshal_int32(&pointerToNextPointerWithData, buffor->buf + curOffset);
            unmarshal_int32(&pointerToData, buffor->buf + curOffset + sizeof(int32_t));
            buffor->pinCount = 0;

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
                    dataBuffor->pinCount = 0;
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
    buffor->pinCount = 0;

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
        buffor->pinCount = 0;

        // 4. If next is -1, then curOffset is the LAST block!
        if (nextOffset == -1) {
            return curOffset; // Return offset of the last block
        }

        // 5. Move to the next block
        curOffset = nextOffset;
    }

    return curOffset;
}

void addBlockToVal(int32_t ptrToBlocks, int32_t tableId, int32_t columnIndex,
                   BtreeBuffors *btreeBuffors, FSMMapBtree *fsmMap, int32_t blockIdVal) {
    int32_t lastOffset = getLastBlockOffset(ptrToBlocks, tableId, columnIndex, btreeBuffors);
    if (lastOffset == -1) return;

    // Allocate new block entry [blockIdVal, -1] in block region 3
    int32_t newOffset = allocateBlocksEntry(blockIdVal, btreeBuffors, tableId, columnIndex, fsmMap);
    if (newOffset == -1) return;

    // Update the last entry's nextPtr to point to the new entry
    int32_t lastBlock = calculateBlock(lastOffset);
    BtreeBuffor *lastBuf = getBtreeBuffor(tableId, columnIndex, lastBlock, btreeBuffors);
    if (lastBuf != NULL) {
        marshal_int32(lastBuf->buf + (lastOffset % BLOCK_SIZE) + sizeof(int32_t), newOffset);
        lastBuf->isDirty = 1;
        lastBuf->pinCount = 0;
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
    buffor->pinCount = 0;
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
        buffor->pinCount = 0;

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

    int32_t gapOffset = getFreeGap(tableId, columnIndex, sizeNeeded, blockEntry, fsmMapBtree);
    if (gapOffset != -1) {
        return gapOffset;
    }

    int32_t curBlock = blockEntry;

    while (1) {
        BtreeBlockEntry *blockMeta = fsm_btree_get_block(fsmMapBtree, tableId, columnIndex, curBlock);

        if (blockMeta == NULL) {
            fsm_btree_add_block(fsmMapBtree, tableId, columnIndex, curBlock, 0);
            return curBlock * BLOCK_SIZE;
        }

        if (BLOCK_SIZE - blockMeta->blockSize >= sizeNeeded) {
            return curBlock * BLOCK_SIZE + blockMeta->blockSize;
        }
        curBlock += 4;
    }
}

static inline int32_t createBlocksEntry(int32_t blockIdVal, int32_t tableId, int32_t columnIndex,
                                        BtreeBuffors *btreeBuffors, FSMMapBtree *fsmMap) {
    int32_t size = sizeof(int32_t) * 2; 
    int32_t offset = findFreeSpace(tableId, columnIndex, size, btreeBuffors, 3, fsmMap);

    int32_t block = calculateBlock(offset);
    BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
    if (buffor != NULL) {
        int32_t inBlock = offset % BLOCK_SIZE;
        marshal_int32(buffor->buf + inBlock, blockIdVal);
        marshal_int32(buffor->buf + inBlock + sizeof(int32_t), -1);
        buffor->isDirty = 1;
        buffor->pinCount = 0;
        fsm_btree_append_to_block(fsmMap, tableId, columnIndex, block, size);
    }
    return offset;
}

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
        buffor->pinCount = 0;
        fsm_btree_append_to_block(fsmMap, tableId, columnIndex, block, dataSize);
    }
    return offset;
}

static inline int32_t insertSortedToNode(AllVar val, int32_t blockIdVal, int32_t nodeOffset,
                                       int32_t tableId, int32_t columnIndex, BtreeBuffors *btreeBuffors, FSMMapBtree *fsmMap) {
    int32_t blocksOffset = createBlocksEntry(blockIdVal, tableId, columnIndex, btreeBuffors, fsmMap);
    int32_t dataOffset = createDataEntry(val, blocksOffset, tableId, columnIndex, btreeBuffors, fsmMap);
    int32_t size = sizeof(int32_t) * 2;
    int32_t newPtdOffset = findFreeSpace(tableId, columnIndex, size, btreeBuffors, 1, fsmMap);
    

    int32_t block = calculateBlock(newPtdOffset);
    BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
    if (buffor != NULL) {
        int32_t inBlock = newPtdOffset % BLOCK_SIZE;
        marshal_int32(buffor->buf + inBlock, -1);
        marshal_int32(buffor->buf + inBlock + sizeof(int32_t), dataOffset);
        buffor->isDirty = 1;
        buffor->pinCount = 0;
        fsm_btree_append_to_block(fsmMap, tableId, columnIndex, block, size);
    }

    int32_t nodeBlock = calculateBlock(nodeOffset);
    BtreeBuffor *nodeBuf = getBtreeBuffor(tableId, columnIndex, nodeBlock, btreeBuffors);
    if (nodeBuf == NULL) return -1;

    int32_t ptdHead = -1;
    unmarshal_int32(&ptdHead, nodeBuf->buf + (nodeOffset % BLOCK_SIZE));
    nodeBuf->pinCount = 0;

    int32_t curPtd = ptdHead;
    int32_t prevPtd = -1;
    int32_t idx = 0;

    while (curPtd != -1) {
        
        int32_t ptdBlock = calculateBlock(curPtd);
        BtreeBuffor *ptdBuf = getBtreeBuffor(tableId, columnIndex, ptdBlock, btreeBuffors);
        if (ptdBuf == NULL) break;

        int32_t nextPtd = -1;
        int32_t dataPtr = -1;
        unmarshal_int32(&nextPtd, ptdBuf->buf + (curPtd % BLOCK_SIZE));
        unmarshal_int32(&dataPtr, ptdBuf->buf + (curPtd % BLOCK_SIZE) + sizeof(int32_t));
        ptdBuf->pinCount = 0;

        if (dataPtr != -1) {
            int32_t dataBlock = calculateBlock(dataPtr);
            BtreeBuffor *dataBuf = getBtreeBuffor(tableId, columnIndex, dataBlock, btreeBuffors);
            if (dataBuf != NULL) {
                int16_t type = 0;
                int32_t length = 0;
                int32_t dataInBlock = dataPtr % BLOCK_SIZE;
                unmarshal_int16(&type, dataBuf->buf + dataInBlock);
                unmarshal_int32(&length, dataBuf->buf + dataInBlock + sizeof(int16_t));

                AllVar curVal;
                all_var_unmarshal(&curVal, type, dataBuf->buf + dataInBlock + sizeof(int16_t) + sizeof(int32_t), length);
                dataBuf->pinCount = 0;

                if (all_var_cmp(&curVal, &val) > 0) {
                    break;
                }
            }
        }

        prevPtd = curPtd;
        curPtd = nextPtd;
        idx++;
    }

    BtreeBuffor *newPtdBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(newPtdOffset), btreeBuffors);
    if (newPtdBuf) {
        marshal_int32(newPtdBuf->buf + (newPtdOffset % BLOCK_SIZE), curPtd);
        newPtdBuf->isDirty = 1;
        newPtdBuf->pinCount = 0;
    }

    if (prevPtd != -1) {
        BtreeBuffor *prevBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(prevPtd), btreeBuffors);
        if (prevBuf) {
            marshal_int32(prevBuf->buf + (prevPtd % BLOCK_SIZE), newPtdOffset);
            prevBuf->isDirty = 1;
            prevBuf->pinCount = 0;
        }
    } else {
        nodeBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(nodeOffset), btreeBuffors);
        if (nodeBuf) {
            marshal_int32(nodeBuf->buf + (nodeOffset % BLOCK_SIZE), newPtdOffset);
            nodeBuf->isDirty = 1;
            nodeBuf->pinCount = 0;
        }
    }

    return idx;
}

// B-tree node split logic
static inline int32_t countNodeKeys(int32_t nodeOffset, int32_t tableId, int32_t columnIndex, BtreeBuffors *btreeBuffors);

static inline void splitNode(int32_t nodeOffset, int32_t parentOffset,
                             int32_t tableId, int32_t columnIndex,
                             BtreeBuffors *btreeBuffors, FSMMapBtree *fsmMap) {
    // 1. Read all PTDs in this node
    int32_t ptds[M + 2];
    int32_t numPtds = 0;

    int32_t nodeBlock = calculateBlock(nodeOffset);
    BtreeBuffor *nodeBuf = getBtreeBuffor(tableId, columnIndex, nodeBlock, btreeBuffors);
    if (!nodeBuf) return;

    int32_t ptdHead = -1;
    int32_t nextLevelPtr = -1;
    unmarshal_int32(&ptdHead, nodeBuf->buf + (nodeOffset % BLOCK_SIZE));
    unmarshal_int32(&nextLevelPtr, nodeBuf->buf + (nodeOffset % BLOCK_SIZE) + sizeof(int32_t));
    nodeBuf->pinCount = 0;

    int32_t cur = ptdHead;
    while (cur != -1 && numPtds < M + 2) {
        ptds[numPtds++] = cur;
        BtreeBuffor *curBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(cur), btreeBuffors);
        if (!curBuf) break;
        int32_t next = -1;
        unmarshal_int32(&next, curBuf->buf + (cur % BLOCK_SIZE));
        curBuf->pinCount = 0;
        cur = next;
    }

    if (numPtds < M) return; // Should not happen

    int32_t midIdx = numPtds / 2;
    
    int32_t midPtd = ptds[midIdx];

    // Disconnect the left half from the median
    if (midIdx > 0) {
        int32_t leftLast = ptds[midIdx - 1];
        BtreeBuffor *leftLastBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(leftLast), btreeBuffors);
        if (leftLastBuf) {
            marshal_int32(leftLastBuf->buf + (leftLast % BLOCK_SIZE), -1);
            leftLastBuf->isDirty = 1;
            leftLastBuf->pinCount = 0;
        }
    } else {
        nodeBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(nodeOffset), btreeBuffors);
        if (nodeBuf) {
            marshal_int32(nodeBuf->buf + (nodeOffset % BLOCK_SIZE), -1);
            nodeBuf->isDirty = 1;
            nodeBuf->pinCount = 0;
        }
    }

    // Isolate the right half
    int32_t rightHead = -1;
    if (midIdx + 1 < numPtds) {
        rightHead = ptds[midIdx + 1];
    }
    BtreeBuffor *midBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(midPtd), btreeBuffors);
    if (midBuf) {
        marshal_int32(midBuf->buf + (midPtd % BLOCK_SIZE), -1);
        midBuf->isDirty = 1;
        midBuf->pinCount = 0;
    }

    // Read old child pointers if internal node
    int32_t childPtrs[M + 3];
    int32_t numChildren = 0;
    if (nextLevelPtr != -1) {
        for (int i = 0; i <= numPtds; i++) {
            int32_t slot = nextLevelPtr + (int32_t)(i * sizeof(int32_t));
            BtreeBuffor *slotBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(slot), btreeBuffors);
            if (slotBuf) {
                int32_t ptr = -1;
                unmarshal_int32(&ptr, slotBuf->buf + (slot % BLOCK_SIZE));
                childPtrs[numChildren++] = ptr;
                slotBuf->pinCount = 0;
            }
        }
    }

    // Allocate new nodes if it's the root
    if (parentOffset == -1) {
        // Root splitting!
        // We create two new children, and the root stays at nodeOffset (0).
        int32_t leftChildOffset = findFreeSpace(tableId, columnIndex, 8, btreeBuffors, 0, fsmMap);
        BtreeBuffor *lcb = getBtreeBuffor(tableId, columnIndex, calculateBlock(leftChildOffset), btreeBuffors);
        if (lcb) {
            marshal_int32(lcb->buf + (leftChildOffset % BLOCK_SIZE), ptdHead); // Left keys
            marshal_int32(lcb->buf + (leftChildOffset % BLOCK_SIZE) + 4, -1);
            lcb->isDirty = 1;
            lcb->pinCount = 0;
            fsm_btree_append_to_block(fsmMap, tableId, columnIndex, calculateBlock(leftChildOffset), 8);
        }

        int32_t rightChildOffset = findFreeSpace(tableId, columnIndex, 8, btreeBuffors, 0, fsmMap);
        BtreeBuffor *rcb = getBtreeBuffor(tableId, columnIndex, calculateBlock(rightChildOffset), btreeBuffors);
        if (rcb) {
            marshal_int32(rcb->buf + (rightChildOffset % BLOCK_SIZE), rightHead); // Right keys
            marshal_int32(rcb->buf + (rightChildOffset % BLOCK_SIZE) + 4, -1);
            rcb->isDirty = 1;
            rcb->pinCount = 0;
            fsm_btree_append_to_block(fsmMap, tableId, columnIndex, calculateBlock(rightChildOffset), 8);
        }

        // Deal with child pointers for the new children
        if (numChildren > 0) {
            int32_t leftPointersOffset = findFreeSpace(tableId, columnIndex, (M + 1) * 4, btreeBuffors, 0, fsmMap);
            for (int k = 0; k <= M; k++) {
                int32_t slot = leftPointersOffset + k * 4;
                BtreeBuffor *sb = getBtreeBuffor(tableId, columnIndex, calculateBlock(slot), btreeBuffors);
                if (sb) { marshal_int32(sb->buf + (slot % BLOCK_SIZE), -1); sb->isDirty=1; sb->pinCount=0; }
            }
            for (int i = 0; i <= midIdx; i++) {
                int32_t slot = leftPointersOffset + i * 4;
                BtreeBuffor *sb = getBtreeBuffor(tableId, columnIndex, calculateBlock(slot), btreeBuffors);
                if (sb) {
                    marshal_int32(sb->buf + (slot % BLOCK_SIZE), childPtrs[i]);
                    sb->isDirty = 1;
                    sb->pinCount = 0;
                }
            }
            fsm_btree_append_to_block(fsmMap, tableId, columnIndex, calculateBlock(leftPointersOffset), (M + 1) * 4);
            lcb = getBtreeBuffor(tableId, columnIndex, calculateBlock(leftChildOffset), btreeBuffors);
            if (lcb) {
                marshal_int32(lcb->buf + (leftChildOffset % BLOCK_SIZE) + 4, leftPointersOffset);
                lcb->isDirty = 1;
                lcb->pinCount = 0;
            }

            int32_t rightChildrenCount = numChildren - (midIdx + 1);
            int32_t rightPointersOffset = findFreeSpace(tableId, columnIndex, (M + 1) * 4, btreeBuffors, 0, fsmMap);
            for (int k = 0; k <= M; k++) {
                int32_t slot = rightPointersOffset + k * 4;
                BtreeBuffor *sb = getBtreeBuffor(tableId, columnIndex, calculateBlock(slot), btreeBuffors);
                if (sb) { marshal_int32(sb->buf + (slot % BLOCK_SIZE), -1); sb->isDirty=1; sb->pinCount=0; }
            }
            for (int i = 0; i < rightChildrenCount; i++) {
                int32_t slot = rightPointersOffset + i * 4;
                BtreeBuffor *sb = getBtreeBuffor(tableId, columnIndex, calculateBlock(slot), btreeBuffors);
                if (sb) {
                    marshal_int32(sb->buf + (slot % BLOCK_SIZE), childPtrs[midIdx + 1 + i]);
                    sb->isDirty = 1;
                    sb->pinCount = 0;
                }
            }
            fsm_btree_append_to_block(fsmMap, tableId, columnIndex, calculateBlock(rightPointersOffset), (M + 1) * 4);
            rcb = getBtreeBuffor(tableId, columnIndex, calculateBlock(rightChildOffset), btreeBuffors);
            if (rcb) {
                marshal_int32(rcb->buf + (rightChildOffset % BLOCK_SIZE) + 4, rightPointersOffset);
                rcb->isDirty = 1;
                rcb->pinCount = 0;
            }
        }

        // Set up the root with median key and two child pointers
        nodeBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(nodeOffset), btreeBuffors);
        if (nodeBuf) {
            marshal_int32(nodeBuf->buf + (nodeOffset % BLOCK_SIZE), midPtd);
            nodeBuf->isDirty = 1;
            nodeBuf->pinCount = 0;
        }

        int32_t rootPointersOffset = findFreeSpace(tableId, columnIndex, (M + 1) * 4, btreeBuffors, 0, fsmMap);
        BtreeBuffor *rpb = getBtreeBuffor(tableId, columnIndex, calculateBlock(rootPointersOffset), btreeBuffors);
        if (rpb) {
            for (int k = 0; k <= M; k++) {
                marshal_int32(rpb->buf + (rootPointersOffset % BLOCK_SIZE) + k * 4, -1);
            }
            marshal_int32(rpb->buf + (rootPointersOffset % BLOCK_SIZE), leftChildOffset);
            marshal_int32(rpb->buf + (rootPointersOffset % BLOCK_SIZE) + 4, rightChildOffset);
            rpb->isDirty = 1;
            rpb->pinCount = 0;
            fsm_btree_append_to_block(fsmMap, tableId, columnIndex, calculateBlock(rootPointersOffset), (M + 1) * 4);
        }

        nodeBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(nodeOffset), btreeBuffors);
        if (nodeBuf) {
            marshal_int32(nodeBuf->buf + (nodeOffset % BLOCK_SIZE) + 4, rootPointersOffset);
            nodeBuf->isDirty = 1;
            nodeBuf->pinCount = 0;
        }

    } else {
        // Internal node or leaf splitting (not root)
        int32_t newNodeOffset = findFreeSpace(tableId, columnIndex, 8, btreeBuffors, 0, fsmMap);
        BtreeBuffor *nnb = getBtreeBuffor(tableId, columnIndex, calculateBlock(newNodeOffset), btreeBuffors);
        if (nnb) {
            marshal_int32(nnb->buf + (newNodeOffset % BLOCK_SIZE), rightHead);
            marshal_int32(nnb->buf + (newNodeOffset % BLOCK_SIZE) + 4, -1);
            nnb->isDirty = 1;
            nnb->pinCount = 0;
            fsm_btree_append_to_block(fsmMap, tableId, columnIndex, calculateBlock(newNodeOffset), 8);
        }

        if (numChildren > 0) {
            int32_t leftChildrenCount = midIdx + 1;
            int32_t rightChildrenCount = numChildren - leftChildrenCount;

            int32_t rightPointersOffset = findFreeSpace(tableId, columnIndex, (M + 1) * 4, btreeBuffors, 0, fsmMap);
            for (int k = 0; k <= M; k++) {
                int32_t slot = rightPointersOffset + k * 4;
                BtreeBuffor *sb = getBtreeBuffor(tableId, columnIndex, calculateBlock(slot), btreeBuffors);
                if (sb) { marshal_int32(sb->buf + (slot % BLOCK_SIZE), -1); sb->isDirty=1; sb->pinCount=0; }
            }
            for (int i = 0; i < rightChildrenCount; i++) {
                int32_t slot = rightPointersOffset + i * 4;
                BtreeBuffor *sb = getBtreeBuffor(tableId, columnIndex, calculateBlock(slot), btreeBuffors);
                if (sb) {
                    marshal_int32(sb->buf + (slot % BLOCK_SIZE), childPtrs[leftChildrenCount + i]);
                    sb->isDirty = 1;
                    sb->pinCount = 0;
                }
            }
            fsm_btree_append_to_block(fsmMap, tableId, columnIndex, calculateBlock(rightPointersOffset), (M + 1) * 4);
            nnb = getBtreeBuffor(tableId, columnIndex, calculateBlock(newNodeOffset), btreeBuffors);
            if (nnb) {
                marshal_int32(nnb->buf + (newNodeOffset % BLOCK_SIZE) + 4, rightPointersOffset);
                nnb->isDirty = 1;
                nnb->pinCount = 0;
            }
            // Note: we leave left children in the original array (just ignoring the shifted ones)
        }

        // Now push median up to parent
        BtreeBuffor *parentBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(parentOffset), btreeBuffors);
        if (!parentBuf) return;

        int32_t parentHead = -1;
        unmarshal_int32(&parentHead, parentBuf->buf + (parentOffset % BLOCK_SIZE));
        parentBuf->pinCount = 0;

        // Insert midPtd into parent's linked list (we must find the right spot)
        // For simplicity, find the spot by comparing keys.
        int32_t curParent = parentHead;
        int32_t prevParent = -1;
        int32_t insertIndex = 0;

        // Read the median key value
        AllVar midVal;
        midBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(midPtd), btreeBuffors);
        if (midBuf) {
            int32_t dPtr = -1;
            unmarshal_int32(&dPtr, midBuf->buf + (midPtd % BLOCK_SIZE) + sizeof(int32_t));
            midBuf->pinCount = 0;
            BtreeBuffor *dBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(dPtr), btreeBuffors);
            if (dBuf) {
                int16_t type = 0; int32_t length = 0;
                unmarshal_int16(&type, dBuf->buf + (dPtr % BLOCK_SIZE));
                unmarshal_int32(&length, dBuf->buf + (dPtr % BLOCK_SIZE) + sizeof(int16_t));
                all_var_unmarshal(&midVal, type, dBuf->buf + (dPtr % BLOCK_SIZE) + sizeof(int16_t) + sizeof(int32_t), length);
                dBuf->pinCount = 0;
            }
        }

        while (curParent != -1) {
            BtreeBuffor *pBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(curParent), btreeBuffors);
            if (!pBuf) break;
            int32_t nxt = -1, dPtr = -1;
            unmarshal_int32(&nxt, pBuf->buf + (curParent % BLOCK_SIZE));
            unmarshal_int32(&dPtr, pBuf->buf + (curParent % BLOCK_SIZE) + sizeof(int32_t));
            pBuf->pinCount = 0;

            BtreeBuffor *dBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(dPtr), btreeBuffors);
            if (dBuf) {
                int16_t type = 0; int32_t length = 0;
                unmarshal_int16(&type, dBuf->buf + (dPtr % BLOCK_SIZE));
                unmarshal_int32(&length, dBuf->buf + (dPtr % BLOCK_SIZE) + sizeof(int16_t));
                AllVar cVal;
                all_var_unmarshal(&cVal, type, dBuf->buf + (dPtr % BLOCK_SIZE) + sizeof(int16_t) + sizeof(int32_t), length);
                dBuf->pinCount = 0;
                if (all_var_cmp(&cVal, &midVal) > 0) {
                    break;
                }
            }
            prevParent = curParent;
            curParent = nxt;
            insertIndex++;
        }

        // Link midPtd
        midBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(midPtd), btreeBuffors);
        if (midBuf) {
            marshal_int32(midBuf->buf + (midPtd % BLOCK_SIZE), curParent);
            midBuf->isDirty = 1;
            midBuf->pinCount = 0;
        }

        if (prevParent != -1) {
            BtreeBuffor *pb = getBtreeBuffor(tableId, columnIndex, calculateBlock(prevParent), btreeBuffors);
            if (pb) {
                marshal_int32(pb->buf + (prevParent % BLOCK_SIZE), midPtd);
                pb->isDirty = 1;
                pb->pinCount = 0;
            }
        } else {
            parentBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(parentOffset), btreeBuffors);
            if (parentBuf) {
                marshal_int32(parentBuf->buf + (parentOffset % BLOCK_SIZE), midPtd);
                parentBuf->isDirty = 1;
                parentBuf->pinCount = 0;
            }
        }

        // Update parent's child pointers
        parentBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(parentOffset), btreeBuffors);
        int32_t parentNextLvl = -1;
        if (parentBuf) {
            unmarshal_int32(&parentNextLvl, parentBuf->buf + (parentOffset % BLOCK_SIZE) + 4);
            parentBuf->pinCount = 0;
        }

        // We must shift pointers right of insertIndex by 1
        int32_t parentKeys = countNodeKeys(parentOffset, tableId, columnIndex, btreeBuffors);
        // Current children count = parentKeys
        // New children count = parentKeys + 1
        // (because parentKeys already includes the newly inserted midPtd)
        for (int i = parentKeys; i > insertIndex + 1; i--) {
            int32_t srcSlot = parentNextLvl + (i - 1) * 4;
            int32_t destSlot = parentNextLvl + i * 4;

            int32_t ptr = -1;
            BtreeBuffor *sb = getBtreeBuffor(tableId, columnIndex, calculateBlock(srcSlot), btreeBuffors);
            if (sb) {
                unmarshal_int32(&ptr, sb->buf + (srcSlot % BLOCK_SIZE));
                sb->pinCount = 0;
            }
            BtreeBuffor *db = getBtreeBuffor(tableId, columnIndex, calculateBlock(destSlot), btreeBuffors);
            if (db) {
                marshal_int32(db->buf + (destSlot % BLOCK_SIZE), ptr);
                db->isDirty = 1;
                db->pinCount = 0;
            }
        }

        // Insert newNodeOffset at insertIndex + 1
        int32_t destSlot = parentNextLvl + (insertIndex + 1) * 4;
        BtreeBuffor *db = getBtreeBuffor(tableId, columnIndex, calculateBlock(destSlot), btreeBuffors);
        if (db) {
            marshal_int32(db->buf + (destSlot % BLOCK_SIZE), newNodeOffset);
            db->isDirty = 1;
            db->pinCount = 0;
        }
    }
}

static inline void addToBtree(AllVar val, int32_t blockIdVal, BtreeBuffors *btreeBuffors, int32_t tableId,
                              int32_t columnIndex, FSMMapBtree *fsmMap) {
    if (btreeBuffors == NULL || fsmMap == NULL) return;

    BtreeTableEntry *table = fsm_btree_get_table(fsmMap, tableId);
    if (table == NULL) return;

    BtreeColumnIndex *col = fsm_btree_get_column(table, columnIndex);
    if (col == NULL) return;

    int32_t path[100];
    int32_t pathDepth = 0;
    int32_t offset = 0;

    while (offset != -1) {
        
        path[pathDepth++] = offset;
        int32_t block = calculateBlock(offset);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor == NULL) break;

        int32_t nextLevelPtr = -1;
        unmarshal_int32(&nextLevelPtr, buffor->buf + (offset % BLOCK_SIZE) + sizeof(int32_t));
        buffor->pinCount = 0;
        
        if (nextLevelPtr == -1) break;

        DataBtree *dataAllvar = getData(offset, tableId, columnIndex, btreeBuffors);
        if (dataAllvar == NULL) break;

        if (checkForEqualAndAdd(val, blockIdVal, btreeBuffors, tableId, columnIndex, fsmMap, dataAllvar)) {
            free(dataAllvar);
            return;
        }

        int32_t idx = findSmaller(dataAllvar->data, dataAllvar->size, val);
        int32_t slot = nextLevelPtr + (int32_t)(idx * sizeof(int32_t));
        int32_t childOffset = -1;
        BtreeBuffor *slotBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(slot), btreeBuffors);
        if (slotBuf) {
            unmarshal_int32(&childOffset, slotBuf->buf + (slot % BLOCK_SIZE));
            slotBuf->pinCount = 0;
        }
        offset = childOffset;
        free(dataAllvar);
    }

    int32_t leafOffset = path[pathDepth - 1];
    

    /* Check for duplicate key in the leaf before creating a new PTD entry */
    DataBtree *leafData = getData(leafOffset, tableId, columnIndex, btreeBuffors);
    if (leafData != NULL) {
        if (checkForEqualAndAdd(val, blockIdVal, btreeBuffors, tableId, columnIndex, fsmMap, leafData)) {
            free(leafData);
            return;
        }
        free(leafData);
    }

    insertSortedToNode(val, blockIdVal, leafOffset, tableId, columnIndex, btreeBuffors, fsmMap);

    // Split bottom-up if needed
    for (int i = pathDepth - 1; i >= 0; i--) {
        int32_t curOffset = path[i];
        if (countNodeKeys(curOffset, tableId, columnIndex, btreeBuffors) >= M) {
            int32_t parentOffset = (i > 0) ? path[i - 1] : -1;
            
            splitNode(curOffset, parentOffset, tableId, columnIndex, btreeBuffors, fsmMap);
        } else {
            break;
        }
    }
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
            if (dataBuffor != NULL) {
                dataBuffor->pinCount = 0;
            }
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
            buffor->pinCount = 0;
            if (nextLevelPtr == -1) {
                break;
            }

            DataBtree *dataAllvar = getData(offset, tableId, columnIndex, btreeBuffors);
            if (dataAllvar == NULL) break;

            if (operator == 0) { // equal operator
                for (int i = 0; i < dataAllvar->size; i++) {
                    if (dataAllvar->data[i].type > 0 && all_var_cmp(&dataAllvar->data[i], &val) == 0) {
                        int32_t ptrToBlocks = dataAllvar->dataOffsets[i];
                        free(dataAllvar);

                        if (ptrToBlocks != -1) {
                            int32_t curBlk = calculateBlock(ptrToBlocks);
                            BtreeBuffor *curBuffor = getBtreeBuffor(tableId, columnIndex, curBlk, btreeBuffors);
                            if (curBuffor != NULL) {
                                int32_t bId = -1;
                                unmarshal_int32(&bId, curBuffor->buf + (ptrToBlocks % BLOCK_SIZE));
                                curBuffor->pinCount = 0;
                                return bId;
                            }
                        }
                        return -1;
                    }
                }
            }

            int32_t idx = findSmaller(dataAllvar->data, dataAllvar->size, val);
            int32_t slot = nextLevelPtr + (int32_t)(idx * sizeof(int32_t));
            int32_t childOffset = -1;
            BtreeBuffor *slotBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(slot), btreeBuffors);
            if (slotBuf) {
                unmarshal_int32(&childOffset, slotBuf->buf + (slot % BLOCK_SIZE));
                slotBuf->pinCount = 0;
            }
            offset = childOffset;
            free(dataAllvar);
        }

        if (offset == -1) return -1;

        DataBtree *dataAllvar = getData(offset, tableId, columnIndex, btreeBuffors);
        if (dataAllvar == NULL) return -1;

        for (int i = 0; i < dataAllvar->size; i++) {
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
                            curBuffor->pinCount = 0;
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
    if (buffor == NULL) return;
    unmarshal_int32(&blockEach,buffor->buf + (offset % BLOCK_SIZE));
    buffor->pinCount = 0;
    while (blockEach!=blockId) {
        prevOffset = offset;
        unmarshal_int32(&offset,buffor->buf + (offset % BLOCK_SIZE) + sizeof(int32_t));
        block = calculateBlock(offset);
        buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor == NULL) return;
        unmarshal_int32(&blockEach,buffor->buf + (offset % BLOCK_SIZE) + sizeof(int32_t));
        buffor->pinCount = 0;
    }
    if (prevOffset != -1) {
        int32_t nextOffset;
        block = calculateBlock(offset);
        buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor != NULL) {
            unmarshal_int32(&nextOffset,buffor->buf + (offset % BLOCK_SIZE) + sizeof(int32_t));
            memset(buffor->buf + offset, 0, 8);
            buffor->isDirty = 1;
            buffor->pinCount = 0;
            deleteElementUpdateSpace(fsm,tableId,columnIndex,block,offset,8);
        }

        int32_t prevBlock = calculateBlock(prevOffset);
        BtreeBuffor *prevBuf = getBtreeBuffor(tableId, columnIndex, prevBlock, btreeBuffors);
        if (prevBuf != NULL) {
            marshal_int32(prevBuf->buf+prevOffset+sizeof(int32_t), nextOffset );
            prevBuf->isDirty = 1;
            prevBuf->pinCount = 0;
        }
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
            buffor->pinCount = 0;
            deleteElementUpdateSpace(fsm, tableId, columnIndex, block, curOffset, 8);

            if (prevOffset != -1) {
                // Removing from middle/end — rewire prev->next
                int32_t prevBlock = calculateBlock(prevOffset);
                BtreeBuffor *prevBuf = getBtreeBuffor(tableId, columnIndex, prevBlock, btreeBuffors);
                if (prevBuf != NULL) {
                    marshal_int32(prevBuf->buf + (prevOffset % BLOCK_SIZE) + sizeof(int32_t), nextOffset);
                    prevBuf->isDirty = 1;
                    prevBuf->pinCount = 0;
                }
                return headOffset;
            } else {
                // Removing head — new head is nextOffset (-1 if list is empty)
                return nextOffset;
            }
        }

        buffor->pinCount = 0;
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
    buffor->pinCount = 0;
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
    buffor->pinCount = 0;
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
    buffor->pinCount = 0;
    deleteElementUpdateSpace(fsm, tableId, columnIndex, block, ptdOffset, 8);

    if (prevPtdOffset != -1) {
        // Rewire prev->next to our next
        int32_t prevBlock = calculateBlock(prevPtdOffset);
        BtreeBuffor *prevBuf = getBtreeBuffor(tableId, columnIndex, prevBlock, btreeBuffors);
        if (prevBuf != NULL) {
            marshal_int32(prevBuf->buf + (prevPtdOffset % BLOCK_SIZE), nextPtd);
            prevBuf->isDirty = 1;
            prevBuf->pinCount = 0;
        }
    } else {
        // Removing PTD list head — update pointer in node (Block 0)
        int32_t nodeBlock = calculateBlock(nodeOffset);
        BtreeBuffor *nodeBuf = getBtreeBuffor(tableId, columnIndex, nodeBlock, btreeBuffors);
        if (nodeBuf != NULL) {
            marshal_int32(nodeBuf->buf + (nodeOffset % BLOCK_SIZE), nextPtd);
            nodeBuf->isDirty = 1;
            nodeBuf->pinCount = 0;
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
    nodeBuf->pinCount = 0;

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
        buffor->pinCount = 0;

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
                        dataBuf->pinCount = 0;

                        info.ptdOffset     = curPtd;
                        info.prevPtdOffset = prevPtd;
                        info.dataOffset    = dataPtr;
                        info.ptrToBlocks   = ptrToBlocks;
                        info.found         = 1;
                        return info;
                    }
                }
                dataBuf->pinCount = 0;
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
    buffor->pinCount = 0;
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
    buffor->pinCount = 0;

    int32_t count = 0;
    int32_t curPtd = ptdHead;
    while (curPtd != -1) {

        count++;
        int32_t ptdBlock = calculateBlock(curPtd);
        BtreeBuffor *ptdBuf = getBtreeBuffor(tableId, columnIndex, ptdBlock, btreeBuffors);
        if (ptdBuf == NULL) break;
        unmarshal_int32(&curPtd, ptdBuf->buf + (curPtd % BLOCK_SIZE));
        ptdBuf->pinCount = 0;
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
    (void)fsm;
    int32_t srcBlock = calculateBlock(srcNodeOffset);
    BtreeBuffor *srcBuf = getBtreeBuffor(tableId, columnIndex, srcBlock, btreeBuffors);
    if (srcBuf == NULL) return;

    int32_t ptdHead = -1;
    unmarshal_int32(&ptdHead, srcBuf->buf + (srcNodeOffset % BLOCK_SIZE));
    srcBuf->pinCount = 0;
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
        ptdBuf->pinCount = 0;
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
            prevBuf->pinCount = 0;
        }
    } else {
        srcBuf = getBtreeBuffor(tableId, columnIndex, srcBlock, btreeBuffors);
        if (srcBuf != NULL) {
            marshal_int32(srcBuf->buf + (srcNodeOffset % BLOCK_SIZE), -1);
            srcBuf->isDirty = 1;
            srcBuf->pinCount = 0;
        }
    }

    // Insert the moved PTD at the beginning of destNode list
    int32_t destBlock = calculateBlock(destNodeOffset);
    BtreeBuffor *destBuf = getBtreeBuffor(tableId, columnIndex, destBlock, btreeBuffors);
    if (destBuf == NULL) return;

    int32_t destHead = -1;
    unmarshal_int32(&destHead, destBuf->buf + (destNodeOffset % BLOCK_SIZE));
    destBuf->pinCount = 0;

    int32_t movedBlock = calculateBlock(curPtd);
    BtreeBuffor *movedBuf = getBtreeBuffor(tableId, columnIndex, movedBlock, btreeBuffors);
    if (movedBuf != NULL) {
        marshal_int32(movedBuf->buf + (curPtd % BLOCK_SIZE), destHead);
        movedBuf->isDirty = 1;
        movedBuf->pinCount = 0;
    }

    destBuf = getBtreeBuffor(tableId, columnIndex, destBlock, btreeBuffors);
    if (destBuf != NULL) {
        marshal_int32(destBuf->buf + (destNodeOffset % BLOCK_SIZE), curPtd);
        destBuf->isDirty = 1;
        destBuf->pinCount = 0;
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
    (void)fsm;
    int32_t srcBlock = calculateBlock(srcNodeOffset);
    BtreeBuffor *srcBuf = getBtreeBuffor(tableId, columnIndex, srcBlock, btreeBuffors);
    if (srcBuf == NULL) return;

    int32_t srcHead = -1;
    unmarshal_int32(&srcHead, srcBuf->buf + (srcNodeOffset % BLOCK_SIZE));
    srcBuf->pinCount = 0;
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
        destBuf->pinCount = 0;
    } else {
        destBuf->pinCount = 0;
        // Search for the last PTD in destNode
        int32_t lastPtd = destHead;
        int32_t nextPtd = -1;
        while (1) {
            int32_t ptdBlock = calculateBlock(lastPtd);
            BtreeBuffor *ptdBuf = getBtreeBuffor(tableId, columnIndex, ptdBlock, btreeBuffors);
            if (ptdBuf == NULL) return;

            unmarshal_int32(&nextPtd, ptdBuf->buf + (lastPtd % BLOCK_SIZE));
            ptdBuf->pinCount = 0;
            if (nextPtd == -1) break;
            lastPtd = nextPtd;
        }

        // Attach srcHead to the end of destNode
        int32_t lastBlock = calculateBlock(lastPtd);
        BtreeBuffor *lastBuf = getBtreeBuffor(tableId, columnIndex, lastBlock, btreeBuffors);
        if (lastBuf != NULL) {
            marshal_int32(lastBuf->buf + (lastPtd % BLOCK_SIZE), srcHead);
            lastBuf->isDirty = 1;
            lastBuf->pinCount = 0;
        }
    }

    // Zero out head of srcNode
    srcBuf = getBtreeBuffor(tableId, columnIndex, srcBlock, btreeBuffors);
    if (srcBuf != NULL) {
        marshal_int32(srcBuf->buf + (srcNodeOffset % BLOCK_SIZE), -1);
        srcBuf->isDirty = 1;
        srcBuf->pinCount = 0;
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
    (void)fsm;
    if (parentOffset == -1 || childOffset == -1) return;

    int32_t parentBlock = calculateBlock(parentOffset);
    BtreeBuffor *parentBuf = getBtreeBuffor(tableId, columnIndex, parentBlock, btreeBuffors);
    if (parentBuf == NULL) return;

    int32_t nextLevelPtr = -1;
    unmarshal_int32(&nextLevelPtr, parentBuf->buf + (parentOffset % BLOCK_SIZE) + sizeof(int32_t));
    parentBuf->pinCount = 0;
    if (nextLevelPtr == -1) return;

    for (int i = 0; i <= M; i++) {
        int32_t slotOffset = nextLevelPtr + (int32_t)(i * sizeof(int32_t));
        int32_t slotBlock = calculateBlock(slotOffset);
        BtreeBuffor *slotBuf = getBtreeBuffor(tableId, columnIndex, slotBlock, btreeBuffors);
        if (slotBuf == NULL) return;

        int32_t childPtr = -1;
        unmarshal_int32(&childPtr, slotBuf->buf + (slotOffset % BLOCK_SIZE));
        slotBuf->pinCount = 0;
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
                nextSlotBuf->pinCount = 0;

                int32_t curSlot = nextLevelPtr + (int32_t)(j * sizeof(int32_t));
                int32_t curSlotBlock = calculateBlock(curSlot);
                BtreeBuffor *curSlotBuf = getBtreeBuffor(tableId, columnIndex, curSlotBlock, btreeBuffors);
                if (curSlotBuf != NULL) {
                    marshal_int32(curSlotBuf->buf + (curSlot % BLOCK_SIZE), nextChild);
                    curSlotBuf->isDirty = 1;
                    curSlotBuf->pinCount = 0;
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
        buffor->pinCount = 0;
        if (nextLevelPtr == -1) {
            break;
        }

        DataBtree *dataAllvar = getData(offset, tableId, columnIndex, btreeBuffors);
        if (dataAllvar == NULL) return;

        /* Check for exact match in internal node */
        int8_t foundInInternal = 0;
        for (int i = 0; i < dataAllvar->size; i++) {
            if (dataAllvar->data[i].type > 0 && all_var_cmp(&dataAllvar->data[i], &val) == 0) {
                foundInInternal = 1;
                break;
            }
        }
        if (foundInInternal) {
            free(dataAllvar);
            break; /* Use this internal node as the target */
        }

        int32_t idx = findSmaller(dataAllvar->data, dataAllvar->size, val);
        parentOffset = offset;
        int32_t slot = nextLevelPtr + (int32_t)(idx * sizeof(int32_t));
        int32_t childOffset = -1;
        BtreeBuffor *slotBuf = getBtreeBuffor(tableId, columnIndex, calculateBlock(slot), btreeBuffors);
        if (slotBuf) {
            unmarshal_int32(&childOffset, slotBuf->buf + (slot % BLOCK_SIZE));
            slotBuf->pinCount = 0;
        }
        offset = childOffset;
        free(dataAllvar);
    }

    /* ── 2. Search for value in the target node ── */
    if (offset == -1) return;
    BtreeDeleteInfo info = findValueInLeaf(offset, &val, tableId, columnIndex, btreeBuffors);
    if (!info.found) return;

    /* Check if target node is an internal node (has children) */
    int8_t isInternal = 0;
    int32_t nodeNextLevelPtr = -1;
    int32_t keyIdx = 0;
    {
        int32_t nb = calculateBlock(offset);
        BtreeBuffor *nBuf = getBtreeBuffor(tableId, columnIndex, nb, btreeBuffors);
        if (nBuf) {
            unmarshal_int32(&nodeNextLevelPtr, nBuf->buf + (offset % BLOCK_SIZE) + sizeof(int32_t));
            nBuf->pinCount = 0;
            isInternal = (nodeNextLevelPtr != -1) ? 1 : 0;
        }
    }

    /* If internal, find the index of the key being deleted (before deletion) */
    if (isInternal) {
        int32_t nb = calculateBlock(offset);
        BtreeBuffor *nBuf = getBtreeBuffor(tableId, columnIndex, nb, btreeBuffors);
        if (nBuf) {
            int32_t ptdHead = -1;
            unmarshal_int32(&ptdHead, nBuf->buf + (offset % BLOCK_SIZE));
            nBuf->pinCount = 0;
            int32_t cur = ptdHead;
            keyIdx = 0;
            while (cur != -1 && cur != info.ptdOffset) {
                keyIdx++;
                int32_t cb = calculateBlock(cur);
                BtreeBuffor *cBuf = getBtreeBuffor(tableId, columnIndex, cb, btreeBuffors);
                if (!cBuf) break;
                unmarshal_int32(&cur, cBuf->buf + (cur % BLOCK_SIZE));
                cBuf->pinCount = 0;
            }
        }
    }

    /* ── 3. Remove blockId from block list (Block 3) ── */
    int32_t newHead = deleteBlockEntry(fsm, btreeBuffors, tableId, columnIndex,
                                       blockId, info.ptrToBlocks);

    if (newHead != info.ptrToBlocks) {
        if (newHead != -1) {
            /* Head changed but list is not empty — update pointer */
            updateDataEntryBlocksPtr(btreeBuffors, tableId, columnIndex,
                                     info.dataOffset, &val, newHead);
        } else {
            /* ── 4. Block list empty ── */
            if (isInternal && nodeNextLevelPtr != -1) {
                /* Internal node — lazy deletion:
                 *
                 * Removing a separator key and merging children is broken when
                 * children are themselves internal nodes (mergeNodes only copies
                 * PTD lists, not child-pointer arrays, orphaning whole subtrees).
                 *
                 * Instead we keep K in the PTD list so it continues to work as
                 * a navigation separator, but set ptrToBlocks = -1 in its
                 * DataEntry so that any search for K correctly returns "not found".
                 * Tree structure (children, key order) is left fully intact.
                 */
                updateDataEntryBlocksPtr(btreeBuffors, tableId, columnIndex,
                                         info.dataOffset, &val, -1);
                /* Do NOT call deleteDataEntry or deletePointerToDataEntry here. */
            } else {
                /* ── Leaf deletion: fully remove DataEntry and PTD ── */
                deleteDataEntry(fsm, btreeBuffors, tableId, columnIndex,
                                info.dataOffset, &val);
                deletePointerToDataEntry(fsm, btreeBuffors, tableId, columnIndex,
                                         offset, info.ptdOffset, info.prevPtdOffset);
            }
        }
    }
}


#endif //QUAKEDB3_0_BTREEFILEOPERATION_H
