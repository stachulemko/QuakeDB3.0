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
 * Vacuum walks the version chain starting from ROOTS — only then does it know the predecessor
 * of a dead version and can relink the chain (prev->t_cid = curr->t_cid). A removed version gets
 * INFOMASK_UNUSED and its slot can be reused by the next insert.
 *
 * The predecessor is kept as a TID (block, index), not a pointer — its buffer is re-fetched
 * at every step, so at most 2 buffers (prev and curr) are pinned at once.
 *
 * The dead-version counter lives in the ROOT's block (header.dead_count + bq), set by sql_doUpdate.
 */

/*
 * Vacuum the chain starting at (rootBlockId, rootIdx).
 * Returns the number of removed versions; *remaining += superseded versions that cannot be
 * removed yet (some active transaction still sees them).
 */
static int32_t vacuumChain(int32_t tableId, int32_t rootBlockId, int32_t rootIdx,
                           Buffors *buffors, MVCC *mvcc, int32_t *remaining) {
    int32_t removed   = 0;
    int32_t prevBlock = rootBlockId;
    int32_t prevIdx   = rootIdx;

    while (1) {
        DataBuffor *prevBuf = getBuffor(tableId, prevBlock, buffors);
        if (prevBuf == NULL) break;
        if (prevBuf->universalBlock == NULL) { prevBuf->pinCount = 0; break; }
        Tuple *prev = &prevBuf->universalBlock->block->tuples[prevIdx];
        if (prev->header.t_cid == 0) { prevBuf->pinCount = 0; break; }

        int16_t currBlock16, currIdx16;
        unpack((uint32_t)prev->header.t_cid, &currBlock16, &currIdx16);
        int32_t currBlock = (int32_t)currBlock16;
        int32_t currIdx   = (int32_t)currIdx16;

        /* prevBuf is pinned, so getBuffor will not evict it; same block → same buffer */
        DataBuffor *currBuf = getBuffor(tableId, currBlock, buffors);
        if (currBuf == NULL || currBuf->universalBlock == NULL) {
            if (currBuf != NULL) currBuf->pinCount = 0;
            prevBuf->pinCount = 0;
            break;
        }
        Tuple *curr = &currBuf->universalBlock->block->tuples[currIdx];

        if (curr->header.t_infomask & INFOMASK_UNUSED) {
            /* should not happen — the chain points to a free slot */
            LOG_ERROR("vacuumChain: broken chain %d/%d -> %d/%d", prevBlock, prevIdx, currBlock, currIdx);
            currBuf->pinCount = 0;
            prevBuf->pinCount = 0;
            break;
        }

        if (canVacuum(curr, mvcc)) {
            /* relink and tombstone — both blocks will reach disk (isDirty) */
            prev->header.t_cid = curr->header.t_cid;
            prevBuf->isDirty = 1;
            sql_makeTombstone(curr, currBuf, 0);
            removed++;
            /* prev does not move — its successor is now curr's successor */
        } else {
            if (!xmax_is_none(curr->header.t_xmax)) (*remaining)++;
            prevBlock = currBlock;
            prevIdx   = currIdx;
        }
        currBuf->pinCount = 0;
        prevBuf->pinCount = 0;
    }
    return removed;
}

/*
 * Vacuum all chains whose root lies in block blockId.
 * A root is not removed (scans start from it), but when it is dead:
 *   - t_cid != 0 → redirect: data freed, t_cid kept,
 *   - t_cid == 0 → row deleted (DELETE) with no newer version → slot freed.
 * At the end the block's dead_count = versions that cannot be removed yet.
 * Returns the number of removed versions; *remainingOut (may be NULL) = the remaining ones.
 */
static int32_t vacuumBlock(int32_t tableId, int32_t blockId, Buffors *buffors, MVCC *mvcc,
                           int32_t *remainingOut) {
    int32_t removed   = 0;
    int32_t remaining = 0;

    DataBuffor *buf = getBuffor(tableId, blockId, buffors);
    if (buf == NULL) return 0;
    if (buf->universalBlock == NULL) { buf->pinCount = 0; return 0; }
    int32_t tupleCount = buf->universalBlock->block->tuple_count;
    buf->pinCount = 0;

    for (int32_t i = 0; i < tupleCount; i++) {
        buf = getBuffor(tableId, blockId, buffors);
        if (buf == NULL) break;
        if (buf->universalBlock == NULL) { buf->pinCount = 0; break; }
        Tuple *root = &buf->universalBlock->block->tuples[i];

        if (root->header.t_infomask & (INFOMASK_CHAIN_MEMBER | INFOMASK_UNUSED)) {
            buf->pinCount = 0;
            continue;
        }
        if (canVacuum(root, mvcc)) {
            if (root->header.t_cid == 0) {
                sql_makeTombstone(root, buf, 0);
                removed++;
            } else if (!(root->header.t_infomask & INFOMASK_DEAD)) {
                sql_makeTombstone(root, buf, 1);
            }
        } else if (!xmax_is_none(root->header.t_xmax)) {
            remaining++;   /* root with a newer version — its data is freed once nobody sees it */
        }
        int8_t hasChain = root->header.t_cid != 0;
        buf->pinCount = 0;

        if (hasChain) removed += vacuumChain(tableId, blockId, i, buffors, mvcc, &remaining);
    }

    buf = getBuffor(tableId, blockId, buffors);
    if (buf != NULL) {
        if (buf->universalBlock != NULL) {
            buf->universalBlock->block->header.dead_count =
                (int16_t)(remaining < INT16_MAX ? remaining : INT16_MAX);
            buf->isDirty = 1;
        }
        buf->pinCount = 0;
    }
    if (remainingOut != NULL) *remainingOut = remaining;
    return removed;
}

/* Sum of the bq counters — how many dead versions are waiting in the whole table */
static int64_t vacuumSumFromBq(bq_t *q) {
    int64_t sum = 0;
    int32_t bucketVal;
    if (bq_first_bucket(q, &bucketVal) != BQ_OK) return 0;
    while (1) {
        for (uint32_t node = bq_bucket_head(q, bucketVal); node != BQ_NIL; node = bq_node_next(q, node)) {
            sum += bucketVal;
        }
        int32_t next;
        if (bq_next_bucket(q, bucketVal, &next) != BQ_OK) break;
        bucketVal = next;
    }
    return sum;
}

/*
autoVacuumScan checks if exitst bq for this table then if yes it starting from the biggest block while number of dead Tuples will be 2 times less than densityDeadTuplesPerBlockMax .
if not existing then it will fullscan by block also using each block if have existirng any dead tuples to fasten process
bq and dead_count are keyed by the ROOT's block — vacuumBlock walks the chains from that block's roots.
 */
void autoVacuumScan(Vacuum *vacuum, int32_t tableId, FSMCache *fsmCache,
                    Buffors *buffors, BqManager *bq_mgr, MVCC *mvcc) {
    BlockCounterEntry *counter = fsm_cache_get(fsmCache, tableId);
    if (counter == NULL || counter->maxBlock <= 0) return;

    /* ----- bq path: from the worst blocks downwards ----- */
    if (bq_mgr != NULL && bq_mgr_exists(bq_mgr, tableId)) {
        bq_t *q = bq_mgr_get(bq_mgr, tableId);
        if (bq_size(q) == 0) return;
        vacuum->sumOfAllDeadTupleInsideTable = vacuumSumFromBq(q);

        /* collect the block list first, worst first — bq changes during vacuum */
        uint32_t count = bq_size(q);
        int32_t *keys = (int32_t *)malloc(count * sizeof(int32_t));
        int32_t *vals = (int32_t *)malloc(count * sizeof(int32_t));
        if (keys == NULL || vals == NULL) { free(keys); free(vals); return; }
        uint32_t n = 0;
        int32_t bucketVal;
        if (bq_last_bucket(q, &bucketVal) == BQ_OK) {
            while (n < count) {
                for (uint32_t node = bq_bucket_head(q, bucketVal); node != BQ_NIL && n < count;
                     node = bq_node_next(q, node)) {
                    keys[n] = bq_node_key(q, node);
                    vals[n] = bucketVal;
                    n++;
                }
                int32_t prevVal;
                if (bq_prev_bucket(q, bucketVal, &prevVal) != BQ_OK) break;
                bucketVal = prevVal;
            }
        }

        int32_t belowCount = 0;
        for (uint32_t k = 0; k < n; k++) {
            /* check density after every block */
            int32_t numberOfBlocks = counter->maxBlock;
            if (numberOfBlocks > 0 &&
                vacuum->sumOfAllDeadTupleInsideTable / numberOfBlocks < vacuum->densityDeadTuplesPerBlockMax) {
                belowCount++;
                if (belowCount >= 2) break;
            } else {
                belowCount = 0;
            }

            int32_t remaining = 0;
            vacuumBlock(tableId, keys[k], buffors, mvcc, &remaining);

            /* the bq counter = what could not be removed yet */
            vacuum->sumOfAllDeadTupleInsideTable -= vals[k] - remaining;
            if (vacuum->sumOfAllDeadTupleInsideTable < 0) vacuum->sumOfAllDeadTupleInsideTable = 0;
            bq_remove(q, keys[k]);
            if (remaining > 0) {
                int32_t maxVal = (int32_t)q->nbuckets - 1;
                bq_insert(q, keys[k], remaining < maxVal ? remaining : maxVal);
            }
        }
        free(keys);
        free(vals);
        return;
    }

    /* ----- fallback: full scan of all blocks ----- */
    for (int32_t i = 1; i <= counter->maxBlock; i++) {
        DataBuffor *buf = getBuffor(tableId, i, buffors);
        if (buf == NULL) continue;
        if (buf->universalBlock == NULL) { buf->pinCount = 0; continue; }
        int16_t deadCount = buf->universalBlock->block->header.dead_count;
        buf->pinCount = 0;
        if (deadCount <= 0) continue;

        int32_t remaining = 0;
        vacuumBlock(tableId, i, buffors, mvcc, &remaining);
        vacuum->sumOfAllDeadTupleInsideTable -= deadCount - remaining;
        if (vacuum->sumOfAllDeadTupleInsideTable < 0) vacuum->sumOfAllDeadTupleInsideTable = 0;
    }
}








#endif //QUAKEDB3_0_VACUUM_H
