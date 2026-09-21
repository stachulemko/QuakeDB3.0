#ifndef dataBuffor_H
#define dataBuffor_H

#include <stdint.h>
#include <stdio.h>
#include "../../memory-mgmt/memory-mgmt/block8kb.h"
#include "../../memory-mgmt/memory-mgmt/universal_block.h"
#include "../../memory-mgmt/memory-mgmt/log.h"
#include "../../memory-mgmt/memory-mgmt/file_manager_c.h"


/*
 * Databuffor uses tableId and pinCount to offer in future paraller aproach. isUsed being used to tell if block buffor is clear and there is no data.
 * It uses universal block which can be extended to conatin diffrent structure .
*/
typedef struct {
    int32_t tableId;
    int32_t pinCount;
    UniversalBlock *universalBlock;
    int8_t isUsed ;
    int8_t isDirty ;
} DataBuffor;

static inline void createDataBufforM(DataBuffor **buffor) {
    *buffor = (DataBuffor *)malloc(sizeof(DataBuffor));
}

typedef struct{
    DataBuffor *buffors;
    int32_t count;
} Buffors;

static inline void freeBuffor(Buffors *buffors) {
    for (int i = 0; i < buffors->count; i++) {
        if (buffors->buffors[i].isUsed && buffors->buffors[i].universalBlock) {
            freeUniversalBlock(buffors->buffors[i].universalBlock);
        }
    }
    free(buffors->buffors);
}

static inline void createBufforsM(Buffors **buffors) {
    *buffors = (Buffors *)malloc(sizeof(Buffors));
}

static inline void createBufforC(Buffors **buffors) {
    *buffors = (Buffors *)malloc(sizeof(Buffors));
}

static inline void initializeBuffors(Buffors *buffors, int32_t numberOfBuffors) {
    LOG_DEBUG("Initializing buffors...");
    buffors->buffors = (DataBuffor *)calloc(numberOfBuffors, sizeof(DataBuffor));

    if (buffors->buffors != NULL) {
        buffors->count = numberOfBuffors;
    } else {
        buffors->count = 0;
    }
}


static inline DataBuffor* getIfExisting(int32_t tableId, int32_t block_id, Buffors *buffors) {
    for(int i=0;i<buffors->count;i++) {
        if(buffors->buffors[i].isUsed == 1 && buffors->buffors[i].tableId == tableId && getBlockId(buffors->buffors[i].universalBlock) == block_id) {
            LOG_DEBUG("Block already exists in buffor.");
            buffors->buffors[i].pinCount=1;

            return &buffors->buffors[i];
        }
    }
    LOG_DEBUG("Block does not exist in buffor.");
    return NULL;
}




static inline DataBuffor* loadIfSpace(int32_t tableId ,int32_t block_id,Buffors *buffors) {
    for (int i = 0; i < buffors->count; i++) {
        if (buffors->buffors[i].isUsed == 0) {
            LOG_DEBUG("Loading block into buffor...");
            buffors->buffors[i].pinCount = 1;
            buffors->buffors[i].tableId = tableId;
            buffors->buffors[i].isUsed = 1;
            buffors->buffors[i].isDirty = 0;
            uint8_t* buf = fm_get_block(DATA_TABLE_PATH, tableId, block_id);
            buffors->buffors[i].universalBlock = createUniversalBlock(buf);
            free(buf);
            return &buffors->buffors[i];
        }
    }
    return NULL;

}

static inline DataBuffor* evict(Buffors *buffors,int32_t tableId,int32_t block_id) {
    // find block until pinCount = 0 then blocking and returning it to next work
    while(1){
        for (int i = 0; i < buffors->count; i++) {
            if (buffors->buffors[i].isUsed == 1 && buffors->buffors[i].pinCount == 0) {
                buffors->buffors[i].pinCount = 1;
                LOG_DEBUG("Evicting block from buffor...");
                if (buffors->buffors[i].isDirty == 1) {
                    LOG_DEBUG("Block is dirty, writing back to disk...");
                    uint8_t buf[BLOCK_SIZE];
                    marshalUniversalBlock(buf, buffors->buffors[i].universalBlock);
                    fm_save_block_at(DATA_TABLE_PATH, buffors->buffors[i].tableId, buf, getBlockId(buffors->buffors[i].universalBlock));
                    //fm_save_block_at("data", buffors->buffors[i].tableId, buf, buffors->buffors[i].universalBlock.block->header.block_id);
                }
                freeUniversalBlock(buffors->buffors[i].universalBlock);
                uint8_t* buf = fm_get_block(DATA_TABLE_PATH, tableId, block_id); // createmmap

                buffors->buffors[i].universalBlock = createUniversalBlock(buf);
                free(buf);
                return &buffors->buffors[i];
            }
        }
    }
}

/* main Function to get new block using 3 stage verification system .
*First it checks is buffor existing already in buffors if its than it gets if not then it goes check further .
*Than is trying to find any clear empty place if finds it loading from disk
*evict version onyl work by at first evict any of the buffors if possible .Than do second step
*/

static inline DataBuffor* getBuffor(int32_t tableId,int32_t block_id, Buffors *buffors) {
    DataBuffor* existingBuffor = getIfExisting(tableId, block_id, buffors);
    if (existingBuffor != NULL) {
        return existingBuffor;
    }
    DataBuffor* loadedBuffor = loadIfSpace(tableId, block_id, buffors);
    if (loadedBuffor != NULL) {
        return loadedBuffor;
    }
    return evict(buffors,tableId,block_id);
}

static inline DataBuffor* loadIfSpaceAny(Buffors *buffors) {
    for (int i = 0; i < buffors->count; i++) {
        if (buffors->buffors[i].isUsed == 0) {
            LOG_DEBUG("Loading block into buffor...");
            buffors->buffors[i].pinCount = 1;
            buffors->buffors[i].isUsed = 1;
            buffors->buffors[i].isDirty = 0;
            return &buffors->buffors[i];
        }
    }
    return NULL;
}

DataBuffor* evictAny(Buffors *buffors) {
    while(1){
        for (int i = 0; i < buffors->count; i++) {
            if (buffors->buffors[i].isUsed == 1 && buffors->buffors[i].pinCount == 0) {
                buffors->buffors[i].pinCount = 1;
                LOG_DEBUG("Evicting block from buffor...");
                if (buffors->buffors[i].isDirty == 1) {
                    LOG_DEBUG("Block is dirty, writing back to disk...");
                    uint8_t buf[BLOCK_SIZE];
                    marshalUniversalBlock(buf, buffors->buffors[i].universalBlock);
                    fm_save_block_at(DATA_TABLE_PATH, buffors->buffors[i].tableId, buf, getBlockId(buffors->buffors[i].universalBlock));
                }
                // free previous one
                freeUniversalBlock(buffors->buffors[i].universalBlock);

                buffors->buffors[i].isDirty = 0;

                buffors->buffors[i].universalBlock = NULL;

                return &buffors->buffors[i];
            }
        }
    }
}

DataBuffor* getBufforAny(Buffors *buffors) {
    DataBuffor* loadedBuffor = loadIfSpaceAny(buffors);
    if (loadedBuffor != NULL) {
        return loadedBuffor;
    }
    return evictAny(buffors);
}


/* It is used to add new block in case when block is not able to fit tuple so we need to add next
 *
*/
DataBuffor* addNewBlock(Buffors *buffors , DataBuffor *newBuffor){
    if(buffors->count == 0){
        LOG_DEBUG("No buffor space available to add new block.");
        return NULL;
    }
    DataBuffor* buffor = getBufforAny(buffors);
    if(buffor == NULL) {
        LOG_DEBUG("Failed to get a buffor for new block.");
        return NULL;
    }
    else{
        *buffor = *newBuffor;
        LOG_DEBUG("New block added to buffor with tableId %d", newBuffor->tableId);
        return buffor;
    }

}

#include "fsmMap.h"
#include "mvcc.h"
#include "../../indexes/indexes/btreeFileOperation.h"


// it is adding tuple cu

// it was onaly create in tests reason to check mvcc process .Currenlty being stored to sustains test adn structure slowly will go out !
void addTuple(Buffors *buffors,FSMCache *c,FSMMapAll *fsmMapAll,MVCC *mvcc,int32_t tableId , AllVar *data, int32_t data_count, int8_t *bit_map, int32_t bit_map_count, FSMMapBtree *fsmMapBtree, BtreeBuffors *btreeBuffors){
    Tuple tuple;
    tuple_set(&tuple, getAndIcrement(mvcc), 0, 0, 0, 0, 0, -1, bit_map, bit_map_count, data, data_count);
    DataBuffor* buffor = addDataToFSMMapAllAndReturnBufforToAdd(buffors, c, fsmMapAll, tableId, &tuple, BLOCK_USABLE_SIZE);
    buffor->pinCount++;
    block8kb_add(buffor->universalBlock->block, &tuple);
    buffor->isDirty = 1;
    buffor->isUsed = 1;
    buffor->pinCount = 0;
    buffor->tableId = tableId;
    if (fsmMapBtree != NULL && btreeBuffors != NULL) {
        int32_t blockId = (int32_t)buffor->universalBlock->block->header.block_id;
        int32_t tupleIdx = buffor->universalBlock->block->tuple_count - 1;
        btree_insert_tuple_indexes(fsmMapBtree, btreeBuffors, tableId,
                                   &buffor->universalBlock->block->tuples[tupleIdx], blockId);
    }
}



// this fuction is usefull in Case of update after we udapte we adding new tuple that why we need to return this to get block and tuple number to set pointer in updated Tuple
// not in case of test but in real cases
DataBuffor* addTupleToOtherFunction(Buffors *buffors,FSMCache *c,FSMMapAll *fsmMapAll,int32_t tableId , AllVar *data, int32_t data_count, int8_t *bit_map, int32_t bit_map_count,int32_t xmin,int32_t xmax,int32_t cid,int16_t infomaks,int16_t hoff,int8_t bitmap,int64_t oid, FSMMapBtree *fsmMapBtree, BtreeBuffors *btreeBuffors) {
    Tuple tuple;
    tuple_set(&tuple, xmin, xmax, cid, infomaks, hoff, bitmap, oid, bit_map, bit_map_count, data, data_count);
    DataBuffor* buffor = addDataToFSMMapAllAndReturnBufforToAdd(buffors, c, fsmMapAll, tableId, &tuple, BLOCK_USABLE_SIZE);
    buffor->pinCount++;
    block8kb_add(buffor->universalBlock->block, &tuple);
    buffor->isDirty = 1;
    buffor->isUsed = 1;
    buffor->tableId = tableId;
    // checking ig endigs exist than is staring to only increment
    if (fsmMapBtree != NULL && btreeBuffors != NULL) {
        int32_t blockId = (int32_t)buffor->universalBlock->block->header.block_id;
        int32_t tupleIdx = buffor->universalBlock->block->tuple_count - 1;
        btree_insert_tuple_indexes(fsmMapBtree, btreeBuffors, tableId,
                                   &buffor->universalBlock->block->tuples[tupleIdx], blockId);
    }
    return buffor;
}


void addTupleToSqlExecutor(Buffors *buffors,FSMCache *c,FSMMapAll *fsmMapAll,int32_t tableId , AllVar *data, int32_t data_count, int8_t *bit_map, int32_t bit_map_count,int32_t xmin,int32_t xmax,int32_t cid,int16_t infomaks,int16_t hoff,int8_t bitmap,int64_t oid, FSMMapBtree *fsmMapBtree, BtreeBuffors *btreeBuffors) {
    Tuple tuple;
    size_t countNum = sizeof((AllVar[]){ all_var_from_int32(1), all_var_from_string("Alice") })
           / sizeof(AllVar);
    if (countNum != data_count) {
        LOG_ERROR("data number exceed the expected one place correct one");
        return;
    }

    tuple_set(&tuple, xmin, xmax, cid, infomaks, hoff, bitmap, oid, bit_map, bit_map_count, data, data_count);
    DataBuffor* buffor = addDataToFSMMapAllAndReturnBufforToAdd(buffors, c, fsmMapAll, tableId, &tuple, BLOCK_USABLE_SIZE);
    buffor->pinCount++;
    block8kb_add(buffor->universalBlock->block, &tuple);
    buffor->isDirty = 1;
    buffor->isUsed = 1;
    buffor->tableId = tableId;
    if (fsmMapBtree != NULL && btreeBuffors != NULL) {
        int32_t blockId = (int32_t)buffor->universalBlock->block->header.block_id;
        int32_t tupleIdx = buffor->universalBlock->block->tuple_count - 1;
        btree_insert_tuple_indexes(fsmMapBtree, btreeBuffors, tableId,
                                   &buffor->universalBlock->block->tuples[tupleIdx], blockId);
    }
}

/* Column names — fixed-length char array instead of std::string */

void addTable(FSMMapAll *fsmMapAll,Buffors *buffors,FSMCache *c,MVCC *mvcc,int32_t tableId,int8_t  types[MAX_COLUMNS],int8_t  types_allow_null[MAX_COLUMNS],char col_names[MAX_COLUMNS][MAX_COL_NAME_LEN]){

    DataBuffor* dataBuffor = getBufforAny(buffors);
    if (dataBuffor == NULL) {
        LOG_DEBUG("Failed to get buffor in addTable.");
        return;
    }

    if (dataBuffor->universalBlock == NULL) {
        createUniversalBlockC(&dataBuffor->universalBlock);
    }

    TableHeader *tableHeader = NULL;
    create_table_headerM(&tableHeader);
    if (tableHeader == NULL) {
        LOG_DEBUG("Failed to allocate tableHeader in addTable.");
        return;
    }

    table_header_init(tableHeader);
    fsm_cache_set(c,tableId);

    BlockCounterEntry *counter = fsm_cache_get(c, tableId);
    if (counter == NULL) {
        LOG_DEBUG("Failed to read FSM counter in addTable.");
        free(tableHeader);
        return;
    }

    table_header_set(tableHeader,-1,counter->maxBlock,getAndIcrement(mvcc),-1,-1,-1,-1,-1,-1,-1,BLOCK_FREE_SPACE,types,types_allow_null,(const char (*)[MAX_COL_NAME_LEN])col_names);

    if (dataBuffor->universalBlock->header != NULL) {
        free(dataBuffor->universalBlock->header);
    }
    if (dataBuffor->universalBlock->block != NULL) {
        free(dataBuffor->universalBlock->block);
        dataBuffor->universalBlock->block = NULL;
    }

    char tableName[32];
    snprintf(tableName, sizeof(tableName), "%d", tableId);
    createBinFile(DATA_TABLE_PATH, tableName);

    addTableToFSMMapAll(fsmMapAll, tableId);
    dataBuffor->universalBlock->header = tableHeader;
    dataBuffor->isDirty = 1;
    dataBuffor->isUsed = 1;
    dataBuffor->pinCount = 0;
    dataBuffor->tableId = tableId;

}

void showBuffors(Buffors *buffors) {
    printf("=== Buffors [count: %d] ===\n", buffors->count);
    for (int i = 0; i < buffors->count; i++) {
        DataBuffor *b = &buffors->buffors[i];
        printf("  [%d] tableId=%-4d  used=%d  dirty=%d  pinCount=%d",
               i, b->tableId, b->isUsed, b->isDirty, b->pinCount);
        if (b->isUsed && b->universalBlock != NULL) {
            int32_t blockId = getBlockId(b->universalBlock);
            if (b->universalBlock->block != NULL) {
                printf("  type=DATA   blockId=%d  tuples=%d  freeSpace=%d",
                       blockId,
                       b->universalBlock->block->tuple_count,
                       b->universalBlock->block->free_space);
            } else if (b->universalBlock->header != NULL) {
                printf("  type=HEADER blockId=%d", blockId);
            }
        } else {
            printf("  (empty)");
        }
        printf("\n");
    }

    printf("==========================\n");
}

// update space -----------------



#endif