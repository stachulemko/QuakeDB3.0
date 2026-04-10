#ifndef fsmMap_H
#define fsmMap_H

#include <stdint.h>
#include "../../memory-mgmt/memory-mgmt/tuple.h"
#include "../../memory-mgmt/memory-mgmt/block8kb.h"
#include "../../memory-mgmt/memory-mgmt/log.h"
#include "dataBuffor.h"
#include "uthash.h"

#define FSM_MAX_SPACE        256    // max number of spaces per table in FSMMap
#define FSM_MAX_BLOCKS_PER_SPACE 64 // max number of block IDs per space entry it is automaticly increased if more blocks are added to space entry
#define MAX_FSM 255 

/*
*   Hash map (using uthash) to track the maximum block ID ;
*   for each tableId , we store the maximum block ID that has been added to the FSMMap.
*   This allows us to assign new block IDs sequentially without scanning the entire FSMMap.
*/

// ==============================================================================
typedef struct {
    int32_t tableId;       
    int32_t maxBlock;      
    UT_hash_handle hh;
} BlockCounterEntry;

typedef struct {
    BlockCounterEntry *blockCounter;
} FSMCache;

static void fsm_cache_init(FSMCache *c) {
    c->blockCounter = NULL;
}

static void fsm_cache_set(FSMCache *c, int32_t tableId) {
    BlockCounterEntry *entry = NULL;
    HASH_FIND_INT(c->blockCounter, &tableId, entry);
    if (entry) {
        entry->maxBlock++;
    } else {
        entry = (BlockCounterEntry *)malloc(sizeof(BlockCounterEntry));
        entry->tableId  = tableId;
        entry->maxBlock = 1;
        HASH_ADD_INT(c->blockCounter, tableId, entry);
    }
}

static BlockCounterEntry *fsm_cache_get(FSMCache *c, int32_t tableId) {
    BlockCounterEntry *entry = NULL;
    HASH_FIND_INT(c->blockCounter, &tableId, entry);
    return entry;
}

static void fsm_cache_free(FSMCache *c) {
    BlockCounterEntry *cur, *tmp;
    HASH_ITER(hh, c->blockCounter, cur, tmp) {
        HASH_DEL(c->blockCounter, cur);
        free(cur);
    }
}
// ==============================================================================





/*
*  FSMMap structure to track free space in blocks for each table.
*  For each tableId, we have an array of FSMSpaceEntry, where each entry having block which have similar level of fullnes(possible space to use) 
*  It is helpfull and saves a lot of space filling the gaps very efficient
*  we are using dnamic table which expand when to much block is in one entrie 
*  fsm_space_entry_add - for adding block with possiblity to expand
*  fsm_space_entry_remove - for removing block   
*  THE MOST IMPORTANT is function addDataToFSMMapAllAndReturnBufforToAdd purpose is for finding the best fiting block for tuple we starting from the best with effiecient and staring ascending with loop with var(int j).
*  if we dont find anything it means averything its full and we have to create new block and dataBuffer to then return .
*  ! we are trying to get the most effiecient block to add
*/

// ==============================================================================
typedef struct {
    int32_t *block_ids;
    int32_t count;
    int32_t capacity;
} FSMSpaceEntry;

static void fsm_space_entry_add(FSMSpaceEntry *e, int32_t block_id) {
    if (e->block_ids == NULL) {
        e->capacity = FSM_MAX_BLOCKS_PER_SPACE;
        e->block_ids = (int32_t *)calloc(e->capacity, sizeof(int32_t));
    }
    if (e->count >= e->capacity) {
        e->capacity += FSM_MAX_BLOCKS_PER_SPACE;
        e->block_ids = (int32_t *)realloc(e->block_ids, e->capacity * sizeof(int32_t));
    }
    e->block_ids[e->count++] = block_id;
}

static void fsm_space_entry_remove(FSMSpaceEntry *e, int32_t idx) {
    if (idx < e->count - 1) {
        e->block_ids[idx] = e->block_ids[e->count - 1];
    }
    e->count--;
}

typedef struct {
    int32_t      tableId;
    FSMSpaceEntry entries[FSM_MAX_SPACE];  
} FSMMap;

typedef struct {
    FSMMap  *maps;
    int32_t  count;
} FSMMapAll;


void addToFSMMapAll(FSMMapAll *fsmMapAll, int32_t tableId, int32_t block_id) {
    for (int i = 0; i < fsmMapAll->count; i++) {
        if (fsmMapAll->maps[i].tableId == tableId) {
            fsm_space_entry_add(&fsmMapAll->maps[i].entries[MAX_FSM], block_id);
            return;
        }
    }
}

DataBuffor* addDataToFSMMapAllAndReturnBufforToAdd(Buffors *buffors, FSMCache *c, FSMMapAll *fsmMapAll, int32_t tableId, Tuple *tuple, int32_t usable_size) {
    int32_t f = usable_size / MAX_FSM;
    int32_t space = usable_size - tuple_size(tuple);
    int32_t spaceEntrie = space / f;
    int32_t tupleEntrie = tuple_size(tuple) / f;
    for (int i = 0; i < fsmMapAll->count; i++) {
        if (fsmMapAll->maps[i].tableId == tableId) {
            if(spaceEntrie >= MAX_FSM) {
                spaceEntrie = MAX_FSM - 1;
            }
            for (int j = spaceEntrie; j>0;j--){
                FSMSpaceEntry *entry = &fsmMapAll->maps[i].entries[j];
                for (int k = 0; k < entry->count; k++) {
                    DataBuffor *b = NULL;
                    b = getBuffor(tableId, entry->block_ids[k], buffors);
                    if (block8kb_full(b->universalBlock->block, tuple) == 1) {
                        int32_t tmp = entry->block_ids[k];
                        fsm_space_entry_remove(entry, k);
                        int32_t newSpaceEntry = tupleEntrie + j;
                        if(newSpaceEntry >= MAX_FSM) {
                            newSpaceEntry = MAX_FSM - 1;
                        }
                        fsm_space_entry_add(&fsmMapAll->maps[i].entries[newSpaceEntry], tmp);
                        return b;
                    }
                    free(b);
                }
            }
            
            
            DataBuffor *dataBuffor = (DataBuffor *)malloc(sizeof(DataBuffor));
            dataBuffor->tableId = tableId;
            dataBuffor->pinCount = 1;
            dataBuffor->isUsed = 1;
            dataBuffor->isDirty = 1;

            UniversalBlock *universalBlock = (UniversalBlock *)malloc(sizeof(UniversalBlock));
            Block8kb *newBlock = (Block8kb *)malloc(sizeof(Block8kb));
            block8kb_init(newBlock, usable_size, 0, fsm_cache_get(c, tableId)->maxBlock, 0, 0, 0, 0);
            universalBlock->block = newBlock;

            if (addNewBlock(buffors, dataBuffor) == 0) {
                LOG_DEBUG("Failed to add new block to buffor.");
                free(newBlock);
                free(universalBlock);
                free(dataBuffor);
                return NULL;
            }
            dataBuffor->universalBlock = universalBlock;
            LOG_DEBUG("New block added to buffor with tableId %d", tableId);
            return dataBuffor;
        }
    }
    return NULL;
}
// ==============================================================================





// if 1 - enough space, if 0 - not enough space
int8_t getEstimatedFreeSpace(FSMMapAll *fsmMapAll, int32_t tableId, Tuple tuple){


}

#endif
