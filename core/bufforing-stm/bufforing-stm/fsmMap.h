#ifndef fsmMap_H
#define fsmMap_H

#include <stdint.h>
#include "../../memory-mgmt/memory-mgmt/tuple.h"
#include "../../memory-mgmt/memory-mgmt/block8kb.h"
#include "../../memory-mgmt/memory-mgmt/log.h"
#include "dataBuffor.h"
#include "uthash.h"

#define FSM_MAX_SPACE        256  /* zakres space: 0-255 */
#define FSM_MAX_BLOCKS_PER_SPACE 64
#define MAX_FSM 255

/* Dict (uthash): klucz = tableId, wartość = maxBlock */
typedef struct {
    int32_t tableId;       /* klucz */
    int32_t maxBlock;      /* wartość */
    UT_hash_handle hh;
} BlockCounterEntry;

typedef struct {
    BlockCounterEntry *blockCounter; /* uthash: NULL = pusta mapa */
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



//------------------------------------------------------------------------
//------------------------------------------------------------------------
//------------------------------------------------------------------------
//------------------------------------------------------------------------
//------------------------------------------------------------------------


/* Dla danego poziomu zapełnienia (space) — lista block_id o tym poziomie */
typedef struct {
    int32_t block_ids[FSM_MAX_BLOCKS_PER_SPACE];
    int32_t count;
} FSMSpaceEntry;

/* Mapa FSM jednej tabeli: klucz = space, wartość = lista block_id */
typedef struct {
    int32_t      tableId;
    FSMSpaceEntry entries[FSM_MAX_SPACE];  /* indeksowane przez space */
} FSMMap;

/* Zbiór map FSM dla wszystkich tabel */
typedef struct {
    FSMMap  *maps;
    int32_t  count;
} FSMMapAll;



void addToFSMMapAll(FSMMapAll *fsmMapAll, int32_t tableId, int32_t block_id) {
    for (int i=0;i<fsmMapAll->count;i++){
        if(fsmMapAll->maps[i].tableId == tableId) {
            for (int j=0;j<FSM_MAX_BLOCKS_PER_SPACE;j++){
                if(fsmMapAll->maps[i].entries[MAX_FSM].block_ids[j] == 0) {
                    fsmMapAll->maps[i].entries[MAX_FSM].block_ids[j] = block_id;
                    fsmMapAll->maps[i].entries[MAX_FSM].count++;
                    return;
                }
                else{
                    LOG_DEBUG("FSMMap is full for space %d, cannot add block_id %d", MAX_FSM, block_id);
                    return;
                }
            }
        }
    }
}


Buffors* addDataToFSMMapAllAndReturnBufforToAdd(Buffors *buffors,FSMCache *c,FSMMapAll *fsmMapAll, int32_t tableId,Tuple * tuple,Block8kb* block) {
    int32_t f = block->usable_size / MAX_FSM;
    int32_t space =  (block->usable_size) -tuple_size(tuple);
    int32_t spaceEntrie = space / f;
    for (int i=0;i<fsmMapAll->count;i++){
        if(fsmMapAll->maps[i].tableId == tableId) {
            for (int j=spaceEntrie;j>0;j--){
                for (int k=0;k<FSM_MAX_BLOCKS_PER_SPACE;k++){
                    if(fsmMapAll->maps[i].entries[j].block_ids[k]!=0){
                        Buffors* b = NULL;
                        getBuffor(tableId,fsmMapAll->maps[i].entries[j].block_ids[k],b);
                        if(block8kb_full(b->buffors->universalBlock->block,tuple) == 1){
                            int32_t tmp = fsmMapAll->maps[i].entries[j].block_ids[k];
                            fsmMapAll->maps[i].entries[j].block_ids[k] = 0;
                            int32_t newSpaceEntry = j + spaceEntrie;

                            for (int w=0;w<FSM_MAX_BLOCKS_PER_SPACE;w++){
                                if(fsmMapAll->maps[i].entries[newSpaceEntry].block_ids[w] == 0){
                                    fsmMapAll->maps[i].entries[newSpaceEntry].block_ids[w] = tmp;
                                    return b;
                                }
                            }

                            //e->block_ids = realloc(e->block_ids, e->capacity * sizeof(int32_t));

                            LOG_DEBUG("not enough space in this entrie");

                            LOG_DEBUG("REALLOCKING BLOCK ADD ADDITIONAL"<<FSM_MAX_BLOCKS_PER_SPACE<<" ENTRIES");

                            DataBuffor* dataBuffor = NULL;
                            
                            malloc(sizeof(DataBuffor));

                            dataBuffor->tableId = tableId;

                            dataBuffor->pinCount = 1;

                            dataBuffor->isUsed = 1;

                            dataBuffor->isDirty = 1;


                            UniversalBlock* universalBlock = NULL;
                            malloc(sizeof(UniversalBlock));

                            Block8kb* newBlock = (Block8kb *)malloc(sizeof(Block8kb));

                            block8kb_init(newBlock,block->free_space,0,fsm_cache_get(c,tableId)->maxBlock,0,0,0,0);
                            universalBlock->block = newBlock;

                            if(addNewBlock(buffors,dataBuffor) == 0){
                                LOG_DEBUG("Failed to add new block to buffor.");
                                return NULL;
                            }
                            else{
                                dataBuffor->universalBlock = universalBlock;
                                LOG_DEBUG("New block added to buffor with tableId %d", tableId);
                                return buffors;
                            }
                        }
                        free(b);
                    }
                }
            }
        }
    }
}




// if 1 - enough space, if 0 - not enough space
int8_t getEstimatedFreeSpace(FSMMapAll *fsmMapAll, int32_t tableId, Tuple tuple){


}

#endif
