//
// Created by stas on 9.09.2026.
//

#ifndef QUAKEDB3_0_VACUUM_H
#define QUAKEDB3_0_VACUUM_H
#include "../../bufforing-stm/bufforing-stm/sqlExecutor.h"
#include "bq.h"

typedef struct{
    int64_t sumOfAllDeadTupleInsideTable;
    int32_t densityDeadTuplesPerBlockMax;
    int8_t manual;
}Vacuum;

/*
autoVacuumScan checks if exitst bq for this table then if yes it starting from the biggest block while number of dead Tuples will be 2 times less than densityDeadTuplesPerBlockMax .
if not existing then it will fullscan by block also using each block if have existirng any dead tuples to fasten process
 */
void autoVacuumScan(Vacuum *vacuum, int32_t tableId, FSMCache *fsmCache,
                    Buffors *buffors, BqManager *bq_mgr, MVCC *mvcc) {
    BlockCounterEntry *counter = fsm_cache_get(fsmCache, tableId);
    if (counter == NULL || counter->maxBlock <= 0) return;

    /* ----- sciezka z bq: od najgorszych blokow w dol ----- */
    if (bq_mgr_exists(bq_mgr, tableId)) {
        bq_t *q = bq_mgr_get(bq_mgr, tableId);
        if (bq_size(q) == 0) return;

        int32_t belowCount = 0;
        int32_t bucketVal;
        int8_t done = 0;
        if (bq_last_bucket(q, &bucketVal) != BQ_OK) return;

        while (!done) {
            uint32_t node = bq_bucket_head(q, bucketVal);
            while (node != BQ_NIL) {
                /* sprawdz density po kazdym bloku */
                int32_t numberOfBlocks = counter->maxBlock;
                if (numberOfBlocks > 0 &&
                    vacuum->sumOfAllDeadTupleInsideTable / numberOfBlocks < vacuum->densityDeadTuplesPerBlockMax) {
                    belowCount++;
                    if (belowCount >= 2) { done = 1; break; }
                } else {
                    belowCount = 0;
                }

                int32_t blockId = bq_node_key(q, node);
                uint32_t nextNode = bq_node_next(q, node);

                DataBuffor *buf = getBuffor(tableId, blockId, buffors);
                if (buf == NULL) { node = nextNode; continue; }
                if (buf->universalBlock == NULL) { buf->pinCount = 0; node = nextNode; continue; }

                Block8kb *block = buf->universalBlock->block;
                for (int i = 0; i < block->tuple_count; i++) {
                    Tuple *t = &block->tuples[i];
                    if (t->header.t_infomask & INFOMASK_DEAD) continue;
                    if (canVacuum(t, mvcc)) {
                        t->header.t_infomask |= INFOMASK_DEAD;
                        vacuum->sumOfAllDeadTupleInsideTable--;
                        buf->isDirty = 1;
                    }
                }
                block->header.dead_count = 0;
                buf->isDirty = 1;
                buf->pinCount = 0;
                bq_remove(q, blockId);
                node = nextNode;
            }
            if (done) break;

            int32_t prevVal;
            if (bq_prev_bucket(q, bucketVal, &prevVal) != BQ_OK) break;
            bucketVal = prevVal;
        }
        return;
    }

    /* ----- fallback: fullscan wszystkich blokow ----- */
    for (int32_t i = 1; i <= counter->maxBlock; i++) {
        DataBuffor *buf = getBuffor(tableId, i, buffors);
        if (buf == NULL) continue;
        if (buf->universalBlock == NULL) { buf->pinCount = 0; continue; }

        Block8kb *block = buf->universalBlock->block;
        if (block->header.dead_count <= 0) { buf->pinCount = 0; continue; }
        for (int j = 0; j < block->tuple_count; j++) {
            Tuple *t = &block->tuples[j];
            if (t->header.t_infomask & INFOMASK_DEAD) continue;
            if (canVacuum(t, mvcc)) {
                t->header.t_infomask |= INFOMASK_DEAD;
                vacuum->sumOfAllDeadTupleInsideTable--;
                buf->isDirty = 1;
            }
        }
        block->header.dead_count = 0;
        buf->isDirty = 1;
        buf->pinCount = 0;
    }
}








#endif //QUAKEDB3_0_VACUUM_H
