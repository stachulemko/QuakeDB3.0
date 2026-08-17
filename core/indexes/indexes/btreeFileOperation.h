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
    int32_t sizeData;
}pointerToData;

// data
// tlv

/*
void addNodeMetaData(int32_t pointerDataToNextPointerData, int32_t pointerToNextLabel,uint8_t *buf,int32_t offset) {
    int32_t pointerToNextLabel 

}

void addPointerToData(int32_t pointerDataToNextPointerData, int32_t pointerToData,uint8_t *buf,int32_t offset) {
    m
}
*/

void createBtreeFile(int32_t tableId , int32_t columnIndex) {
    char name[64];
    snprintf(name, sizeof(name), "%d.%d", tableId, columnIndex);
    createBinFile("INDEX_TABLE_PATH", name);
}

void createBtree(FSMMapBtree *fsm,
int32_t tableId,int32_t columnIndex) {
    uint8_t buffer[BLOCK_SIZE*3];
    int32_t offset = 0;
    nodeMetaData metaData ={-1,-1};
    metaData.pointerToPointerWithData = BLOCK_SIZE;
    marshal_int32(buffer + offset, metaData.pointerToPointerWithData);
    offset+=4;
    marshal_int32(buffer + offset, metaData.pointerToNextLabel);
    offset+=4;

    offset=BLOCK_SIZE;
    //second Block
    pointerToData ptd ={-1,-1};
    ptd.pointerToData = offset+4+BLOCK_SIZE;
    marshal_int32(buffer + offset, ptd.pointerDataToNextPointerData);
    offset+=4;
    marshal_int32(buffer + offset, ptd.pointerToData);
    fsm_btree_create_index(fsm, tableId, columnIndex);

    //----------------------
    fsm_btree_add_block(fsm, tableId, columnIndex, 0,8);
    fsm_btree_add_block(fsm, tableId, columnIndex, 1, 8);
    fsm_btree_add_block(fsm, tableId, columnIndex, 2, 0);

}

/*
void addToBtree(FSMMapBtree *fsm, int32_t tableId, int32_t columnIndex, AllVar value, int32_t blockId) {
    BtreeTableEntry *table = fsm_btree_get_table(fsm, tableId);
    if (table == NULL) return;
    BtreeColumnIndex *col = fsm_btree_get_column(table, columnIndex);
    if (col == NULL) return;

    // For simplicity, we add to the first block. In a real implementation, we would need to find the correct block.
    BtreeBlockEntry *block = fsm_btree_get_block(fsm, tableId, columnIndex, 0);
    if (block == NULL) return;
    int32_t offset = 0;

    // Build full path to btree file: INDEX_TABLE_PATH/{tableId}.{columnIndex}.bin
    char path[512];
    snprintf(path, sizeof(path), "%s/%d.%d.bin", "INDEX_TABLE_PATH", tableId, columnIndex);

    for (int i = 0; i < col->blockCount / 3; i++) {
        uint8_t *b0 = fm_get_blockU(path, i);
        uint8_t *b1 = fm_get_blockU(path, i+1);
        uint8_t *b2 = fm_get_blockU(path, i+2);
        if (buffer == NULL) continue;

        nodeMetaData metaData;
        metaData.pointerToPointerWithData = unmarshal_int32(&metaData.pointerToPointerWithData, b0 + offset);
        offset+=4;
        metaData.pointerToNextLabel = unmarshal_int32(&metaData.pointerToNextLabel, b0 + offset );
        bool split = false;
        if (metaData.pointerToPointerWithData == -1) {
            // No more data blocks, we can add to this block
            pointerToData ptd;
            ptd.pointerDataToNextPointerData = -1; // No next pointer
            ptd.pointerToData = offset + 4 + BLOCK_SIZE; // Point to the new data
            marshal_int32( b1 + offset, ptd.pointerDataToNextPointerData);
            offset+=4;
            marshal_int32(b1 + offset, ptd.pointerToData);

            all_var_marshal(b2,&value);
            // Here we would also need to write the actual value to the data block pointed by ptd.pointerToData
            fm_save_block_at(path, tableId, b0, i); // Save the updated block back to disk
            fm_save_block_at(path,tableId, b1, i+1);
            fm_save_block_at(path,tableId, b2, i+2);
        }
        else {
            f
        }
        free(buffer);
    }
}


void addToBtree(FSMMapBtree *fsm, int32_t tableId, int32_t columnIndex, AllVar value) {

    BtreeTableEntry *table = fsm_btree_get_table(fsm, tableId);
    if (table == NULL) return;
    BtreeColumnIndex *col = fsm_btree_get_column(table, columnIndex);
    if (col == NULL) return;

    for (int i=0;i<col->blockCount;i+=3) {
        BtreeBlockEntry *block = fsm_btree_get_block(fsm, tableId, columnIndex, 0);
        nodeMetaData metaData;
        int32_t offset = 0;
        offset+=4;
        unmarshal_int32(&metaData.pointerToPointerWithData,block[offset]);
        offset+=4;
        unmarshal_int32(&metaData.pointerToNextLabel,block[offset]);
        int32_t tabData[M];
        int32_t offsettmp = metaData.pointerToPointerWithData;
        int32_t nextoffset = 0;
        int32_t dataOffset = 0;
        unmarshal_int32(&nextoffset,block[offsettmp]);
        int32_t i=0;
        while (nextoffset != -1) {
            unmarshal_int32(&nextoffset,block[offsettmp]);
            unmarshal_int32(&dataOffset,&block[offsettmp]);
            unmarshal_int32(&tabData[i],&block[dataOffset]);
            i++;
        }

    }


}


int32_t getNextNodeIndex(int32_t pointerDataToNextPointerData,uint8_t *buffer,uint8_t) {
    pointerToData ptd;
    unmarshal_int32( &ptd.pointerDataToNextPointerData, buffer+pointerDataToNextPointerData);
}
*/
//--------------------------------------------------

//------------------------------------------------
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
    Allvar *data[M];
    int32_t size;
}DataBtree;

void createBtreeBufforM(BtreeBuffor **buf) {
    *buf = malloc(sizeof(BtreeBuffor));
}
void createBtreeBufforC(BtreeBuffor **buf) {
    *buf = calloc(1,sizeof(BtreeBuffor));
}

void initBtreeBuffors(BtreeBuffors *buffors, int32_t numberOfBuffors) {
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
    return written == BLOCK_SIZE;
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
int32_t calculateBlock(int32_t pointer ) {
    return pointer/BLOCK_SIZE;
}
AllVar* getData(int32_t pointerToPointerWithData,BtreeBuffors *btreeBuffors ) {
    int32_t offset = pointerToPointerWithData;
    int32_t block = calculateBlock(offset);
    BtreeBuffor *Buffor = getBtreeBuffor(btreeBuffor.tableId,btreeBuffor.columnIndex,block,&btreeBuffors);
    int32_t i = 0;
    AllVar *data[M] = malloc(sizeof(AllVar)*M);
    int32_t pointerToData;
    unmarshal_int32(&pointerToData, &buffor+offset);
    int16_t type ;
    int32_t lenght;
    while (offset != -1) {
        block = calculateBlock(pointerToData);
        Buffor = getBtreeBuffor(btreeBuffor.tableId,btreeBuffor.columnIndex,block,&btreeBuffors);
        unmarshal_int16(&type,buffor->buf+pointerToData);
        unmarshal_int32(&lenght,buffor->buf+pointerToData+2);
        all_var_unmarshal(&data[i],buffor->buf+pointerToData+6,lenght);
        unmarshal_int32(&offset,buffor->buf+offset+4);
        unmarshal_int32(&pointerToData, &buffor+offset);
        block = calculateBlock(offset);
        Buffor = getBtreeBuffor(btreeBuffor.tableId,btreeBuffor.columnIndex,block,&btreeBuffors);
        i++;

    }

    return data;

}


int32_t findSmaller(AllVar *data[M],Allvar val,int32_t number) {
    for (int i=0;i<number;i++) {
        if (data[i]>val) {
            return i;
        }
    }
    return number
}


void addValDirectly(int32_t offset,Allvar val,BtreeBuffors *btreeBuffors) {

}

void addToBtree(AllVar val,BtreeBuffors *btreeBuffors,int32_t tableId,int32_t columnIndex,int32_t blockId,FSMMapBtree *fsmMap) {
    if (btreeBuffors == NULL || fsmMap == NULL) {
        LOG_ERROR("addToBtree: Buffors or FSMMap is NULL");
        return;
    }
    if (columnIndex exists in fsmTree in tableId ) {
        int32_t offset = 0;
        DataBtree data ;
        Allvars dataAllvar[M];
        int32_t nextLevel ;
        while (offset != -1) {
            data = getData(offset,btreeBuffors);
            dataAllvar = data.data;
            nextLevel = data.size + 1;
            int32_t idx = findSmaller(dataAllvar,val,nextLevel);
            offset = offset + idx *8;
        }
        addValDirectly(offset,val,btreeBuffors);
    }

}
*/
//--------------------------------------------------------------

static inline int32_t calculateBlock(int32_t pointer) {
    return pointer / BLOCK_SIZE;
}

static inline AllVar* getData(int32_t pointerToPointerWithData,
                              int32_t tableId,
                              int32_t columnIndex,
                              BtreeBuffors *btreeBuffors) {
    int32_t offset = pointerToPointerWithData;
    int32_t block = calculateBlock(offset);
    BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
    if (buffor == NULL) return NULL;

    int32_t i = 0;
    AllVar *data = (AllVar *)malloc(sizeof(AllVar) * M);
    if (data == NULL) return NULL;

    int32_t pointerToData = -1;
    int16_t type = 0;
    int32_t length = 0;

    unmarshal_int32(&pointerToData, buffor->buf + offset);

    while (offset != -1 && i < M) {
        block = calculateBlock(pointerToData);
        buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor == NULL) break;

        unmarshal_int16(&type, buffor->buf + pointerToData);
        unmarshal_int32(&length, buffor->buf + pointerToData + sizeof(int16_t));
        all_var_unmarshal(&data[i], type, buffor->buf + pointerToData + sizeof(int16_t) + sizeof(int32_t), length);

        unmarshal_int32(&offset, buffor->buf + offset + sizeof(int32_t));
        unmarshal_int32(&pointerToData, buffor->buf + offset);
        i++;
    }

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

static inline void addValDirectly(int32_t offset, AllVar val, BtreeBuffors *btreeBuffors,
                                  int32_t tableId, int32_t columnIndex) {
    if (btreeBuffors == NULL) return;

    int32_t blockId = calculateBlock(offset);
    BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, blockId, btreeBuffors);
    if (buffor == NULL) return;

    int32_t blockOffset = offset % BLOCK_SIZE;
    marshal_int16(buffor->buf + blockOffset, val.type);
    marshal_int32(buffor->buf + blockOffset + sizeof(int16_t), all_var_size(&val));
    all_var_marshal(buffor->buf + blockOffset + sizeof(int16_t) + sizeof(int32_t), &val);

    buffor->isDirty = 1;
}

static inline void addToBtree(AllVar val, BtreeBuffors *btreeBuffors, int32_t tableId,
                              int32_t columnIndex, FSMMapBtree *fsmMap) {
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
        AllVar *dataAllvar = getData(offset, tableId, columnIndex, btreeBuffors);
        if (dataAllvar == NULL) break;

        int32_t idx = findSmaller(dataAllvar, nextLevel, val);
        offset = offset + idx * sizeof(int32_t);
        free(dataAllvar);
    }

    addValDirectly(offset, val, btreeBuffors, tableId, columnIndex);
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

            addToBtree(tuple->dnb.data[columnIndex], btreeBuffors, tableId, columnIndex, fsm);
        }

        dataBuffor->pinCount = 0;
    }
}
#endif //QUAKEDB3_0_BTREEFILEOPERATION_H
