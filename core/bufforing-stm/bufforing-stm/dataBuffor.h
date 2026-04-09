#ifndef dataBuffor_H
#define dataBuffor_H

#include <stdint.h>
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


typedef struct{
    DataBuffor *buffors;
    int32_t count;
} Buffors;


void initializeBuffors(Buffors *buffors, int32_t numberOfBuffors) {
    LOG_DEBUG("Initializing buffors...");
    buffors->buffors = (DataBuffor *)calloc(numberOfBuffors, sizeof(DataBuffor));
    
    if (buffors->buffors != NULL) {
        buffors->count = numberOfBuffors; 
    } else {
        buffors->count = 0;
    }
}


Buffors* getIfExisting(int32_t tableId, int32_t block_id, Buffors *buffors) {
    for(int i=0;i<buffors->count;i++) {
        if(buffors->buffors[i].isUsed == 1 && buffors->buffors[i].tableId == tableId && getBlockId(buffors->buffors[i].universalBlock) == block_id) {
            LOG_DEBUG("Block already exists in buffor.");
            buffors->buffors[i].pinCount=1;
            
            return buffors;
        }
    }
    LOG_DEBUG("Block does not exist in buffor.");
    return NULL;
}

Buffors* loadIfSpace(int32_t tableId ,int32_t block_id,Buffors *buffors) {
    for (int i = 0; i < buffors->count; i++) {
        if (buffors->buffors[i].isUsed == 0) {
            LOG_DEBUG("Loading block into buffor...");
            buffors->buffors[i].pinCount = 1;
            buffors->buffors[i].tableId = tableId;
            buffors->buffors[i].isUsed = 1;
            buffors->buffors[i].isDirty = 0;
            uint8_t* buf = fm_get_block("data", tableId, block_id);
            buffors->buffors[i].universalBlock = createUniversalBlock(buf);
            free(buf);
        }
    }
    return buffors;
}

Buffors* evict(Buffors *buffors) {
    for (int i = 0; i < buffors->count; i++) {
        if (buffors->buffors[i].isUsed == 1 && buffors->buffors[i].pinCount == 0) {
            LOG_DEBUG("Evicting block from buffor...");
            if (buffors->buffors[i].isDirty == 1) {
                LOG_DEBUG("Block is dirty, writing back to disk...");
                uint8_t buf[BLOCK_SIZE];
                marshalUniversalBlock(buf, buffors->buffors[i].universalBlock);
                fm_save_block_at("data", buffors->buffors[i].tableId, buf, getBlockId(buffors->buffors[i].universalBlock));
                //fm_save_block_at("data", buffors->buffors[i].tableId, buf, buffors->buffors[i].universalBlock.block->header.block_id);
            }
            free(buffors->buffors[i].universalBlock);
            buffors->buffors[i].isUsed = 1;
            buffors->buffors[i].pinCount = 1;
            break;
        }
    }
    return buffors;

}
Buffors* getBuffor(int32_t tableId,int32_t block_id, Buffors *buffors) {
    Buffors* existingBuffor = getIfExisting(tableId, block_id, buffors);
    if (existingBuffor != NULL) {
        return existingBuffor;
    }
    Buffors* loadedBuffor = loadIfSpace(tableId, block_id, buffors);
    if (loadedBuffor != NULL) {
        return loadedBuffor;
    }
    return evict(buffors);
}









#endif