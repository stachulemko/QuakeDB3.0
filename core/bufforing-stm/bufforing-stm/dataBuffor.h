#ifndef dataBuffor_H
#define dataBuffor_H

#include <stdint.h>
#include <stdio.h>
#include "../../memory-mgmt/memory-mgmt/block8kb.h"
#include "../../memory-mgmt/memory-mgmt/universal_block.h"
#include "../../memory-mgmt/memory-mgmt/log.h"
#include "../../memory-mgmt/memory-mgmt/file_manager_c.h"

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

void freeBuffor(Buffors *buffors) {
    for (int i = 0; i < buffors->count; i++) {
        if (buffors->buffors[i].isUsed && buffors->buffors[i].universalBlock) {
            freeUniversalBlock(buffors->buffors[i].universalBlock);
        }
    }
    free(buffors->buffors);
}

void initializeBuffors(Buffors *buffors, int32_t numberOfBuffors) {
    LOG_DEBUG("Initializing buffors...");
    buffors->buffors = (DataBuffor *)calloc(numberOfBuffors, sizeof(DataBuffor));
    
    if (buffors->buffors != NULL) {
        buffors->count = numberOfBuffors; 
    } else {
        buffors->count = 0;
    }
}


DataBuffor* getIfExisting(int32_t tableId, int32_t block_id, Buffors *buffors) {
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




DataBuffor* loadIfSpace(int32_t tableId ,int32_t block_id,Buffors *buffors) {
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

DataBuffor* evict(Buffors *buffors,int32_t tableId,int32_t block_id) {
    while(1){
        for (int i = 0; i < buffors->count; i++) {
            if (buffors->buffors[i].isUsed == 1 && buffors->buffors[i].pinCount == 0) {
                buffors->buffors[i].pinCount = 1;
                buffors->buffors[i].isDirty = 0;
                LOG_DEBUG("Evicting block from buffor...");
                if (buffors->buffors[i].isDirty == 1) {
                    LOG_DEBUG("Block is dirty, writing back to disk...");
                    uint8_t buf[BLOCK_SIZE];
                    marshalUniversalBlock(buf, buffors->buffors[i].universalBlock);
                    fm_save_block_at(DATA_TABLE_PATH, buffors->buffors[i].tableId, buf, getBlockId(buffors->buffors[i].universalBlock));
                    //fm_save_block_at("data", buffors->buffors[i].tableId, buf, buffors->buffors[i].universalBlock.block->header.block_id);
                }
                freeUniversalBlock(buffors->buffors[i].universalBlock);
                uint8_t* buf = fm_get_block("data", tableId, block_id);
                buffors->buffors[i].universalBlock = createUniversalBlock(buf);
                free(buf);
                return &buffors->buffors[i];
            }
        }
    }
}


DataBuffor* getBuffor(int32_t tableId,int32_t block_id, Buffors *buffors) {
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

DataBuffor* loadIfSpaceAny(Buffors *buffors) {
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
                    fm_save_block_at("data", buffors->buffors[i].tableId, buf, getBlockId(buffors->buffors[i].universalBlock));
                }
                // free previous one
                free(buffors->buffors[i].universalBlock);

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


Buffors* addNewBlock(Buffors *buffors , DataBuffor *newBuffor){
    
    if(buffors->count == 0){
        LOG_DEBUG("No buffor space available to add new block.");
        return 0;
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

void addTuple(Buffors *buffors,FSMCache *c,FSMMapAll *fsmMapAll,int32_t tableId , AllVar *data, int32_t data_count, int8_t *bit_map, int32_t bit_map_count){
    Tuple tuple;
    tuple_set(&tuple, 0, 0, 0, 0, 0, 0, 0, bit_map, bit_map_count, data, data_count);
    DataBuffor* buffor = addDataToFSMMapAllAndReturnBufforToAdd(buffors, c, fsmMapAll, tableId, &tuple, BLOCK_USABLE_SIZE);
    block8kb_add(buffor->universalBlock->block, &tuple);
    buffor->isDirty = 1;
    buffor->isUsed = 1;
    buffor->pinCount = 0;
    buffor->tableId = tableId;
}



    /* Column names — fixed-length char array instead of std::string */
    
void addTable(FSMMapAll *fsmMapAll,Buffors *buffors,FSMCache *c,int32_t tableId,int8_t  types[MAX_COLUMNS],int8_t  types_allow_null[MAX_COLUMNS],char col_names[MAX_COLUMNS][MAX_COL_NAME_LEN]){

    DataBuffor* dataBuffor = getBufforAny(buffors);
    if (dataBuffor == NULL) {
        LOG_DEBUG("Failed to get buffor in addTable.");
        return;
    }

    if (dataBuffor->universalBlock == NULL) {
        createUniversalBlockM(&dataBuffor->universalBlock);
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

    table_header_set(tableHeader,-1,counter->maxBlock,-1,-1,-1,-1,-1,-1,-1,-1,BLOCK_FREE_SPACE,types,types_allow_null,(const char (*)[MAX_COL_NAME_LEN])col_names);

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







#endif

