//
// Created by stas on 16.04.2026.
//

#ifndef QUAKEDB3_0_SQLEXECUTOR_H
#define QUAKEDB3_0_SQLEXECUTOR_H

#include "dataBuffor.h"
#include "block8kb.h"
#include "transaction.h"
#include "fsmMap.h"
#include "../../memory-mgmt/memory-mgmt/all_var.h"
#include "../../indexes/indexes/btreeFileOperation.h"
#include "../../vacuum/vacuum/bq.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * ResultTuple — result of a SELECT query
 * ========================================================================= */

typedef struct {
    Tuple *tuples[RESULT_SPACE];
    int32_t  tuple_count;
} ResultTuple;

/* =========================================================================
 * Operators (passed to evaluateAllVar)
 *   SQL_EQ  = 0   ( =  )
 *   SQL_NEQ = 1   ( != )
 *   SQL_LT  = 2   ( <  )
 *   SQL_GT  = 3   ( >  )
 *   SQL_LE  = 4   ( <= )
 *   SQL_GE  = 5   ( >= )
 * ========================================================================= */

#define SQL_EQ  0
#define SQL_NEQ 1
#define SQL_LT  2
#define SQL_GT  3
#define SQL_LE  4
#define SQL_GE  5

/* =========================================================================
 * WHERE condition: column OP value
 * ========================================================================= */




typedef struct {
    int32_t column;   /* column index in the tuple */
    AllVar  value;    /* value to compare with   */
    int8_t  op;       /* SQL_EQ / SQL_NEQ / SQL_LT / SQL_GT / SQL_LE / SQL_GE */
    int16_t method;   /* string comparison method: 0=len+lex, 1=lex */
} SqlCondition;

/* =========================================================================
 * SqlExecutor — opis zapytania
 * ========================================================================= */

typedef struct {
    /* SELECT */
    int8_t  select;
    int32_t selColumns[MAX_COLUMNS];
    int32_t selCount;

    /* WHERE (ANDed conditions) */
    int8_t        where;
    SqlCondition  conditions[MAX_COLUMNS];
    int32_t       condCount;

    /* UPDATE */
    int8_t  update;
    int32_t updColumns[MAX_COLUMNS];
    AllVar  updValues[MAX_COLUMNS];
    int32_t updCount;

    /* FROM */
    int32_t tableId;
    int32_t endBlock;

    Transaction *transaction;

    /* B-tree indexes (optional — NULL when there are no indexes) */
    FSMMapBtree  *fsmMapBtree;
    BtreeBuffors *btreeBuffors;

    // inset func
    int8_t insert;
    AllVar *val;

    // locked on transaction - in case of lost update or other anomalies
    int32_t xidOfBlockingTransaction;

    /* vacuum queue (optional — NULL if none): UPDATE increments the root block's counter */
    BqManager *bqMgr;

} SqlExecutor;


/*
 * created in case of lost update

*/

typedef struct {
    SqlExecutor* operation[mvccBufforSize];
}TabOfBlockedTransaction;

// returns 0 on success, -1 when the table is full
int8_t addToTabOfBlockedTransaction(SqlExecutor* se,TabOfBlockedTransaction *tab) {
    for (int i = 0; i < mvccBufforSize; i++) {
        if (tab->operation[i] == NULL) {
            tab->operation[i] = se;
            return 0;
        }
    }
    return -1;
}

/*
 * Returns the first blocked operation whose blocking transaction has finished
 * (commit or rollback) and removes it from the table — it can be resumed.
 * Returns NULL when every operation is still waiting.
 */
SqlExecutor* checkIfTransactionBlocked(TabOfBlockedTransaction *tab_of_blocked_transaction,
                                       MVCC *mvcc, MVCCBuffor *mvccBuffor) {
    for (int i = 0; i < mvccBufforSize; i++) {
        SqlExecutor *se = tab_of_blocked_transaction->operation[i];
        if (se == NULL) continue;
        if (mvcc_is_txn_active(mvcc, mvccBuffor, NULL, se->xidOfBlockingTransaction) == 0) {
            tab_of_blocked_transaction->operation[i] = NULL;
            return se;
        }
    }
    return NULL;
}
/*
 * end
*/

/* t_infomask bit: tuple inserted by UPDATE — skipped by scans, reachable only through chain traversal */
#define NORMAL_INFOMAKS ((int32_t)0x0000)
#define INFOMASK_CHAIN_MEMBER ((int32_t)0x0001)
#define INFOMASK_DEAD         ((int32_t)0x0002)

/* =========================================================================
 * Builder helpers
 * ========================================================================= */

// adding directly

void sql_addTuple(SqlExecutor *se,int32_t tableId,AllVar *val,Buffors *buffors,FSMCache *c,FSMMapAll *fsmMapAll,Transaction *txn,int32_t dataCount,int8_t *bit_map, int32_t bit_map_count) {
    if (se->select!= 1 && se->where!=1 && se->update!=1) {
        addTupleToSqlExecutor(buffors,c,fsmMapAll,tableId,val,dataCount,bit_map,bit_map_count,txn->xid,0,0,NORMAL_INFOMAKS,0,0,0,se->fsmMapBtree,se->btreeBuffors);
    }
}
void sql_setSelect(SqlExecutor *se, int32_t columns[], int32_t count) {
    se->select = 1;
    se->selCount = count;
    for (int i = 0; i < count; i++) se->selColumns[i] = columns[i];
}

void sql_addWhere(SqlExecutor *se, int32_t column, AllVar value, int8_t op, int16_t method) {
    se->where = 1;
    int idx = se->condCount++;
    se->conditions[idx].column = column;
    se->conditions[idx].value  = value;
    se->conditions[idx].op     = op;
    se->conditions[idx].method = method;
}

void sql_setUpdate(SqlExecutor *se, int32_t columns[], AllVar values[], int32_t count) {
    se->update = 1;
    se->updCount = count;
    for (int i = 0; i < count; i++) {
        se->updColumns[i] = columns[i];
        se->updValues[i]  = values[i];
    }
}

void sql_setFrom(SqlExecutor *se, int32_t tableId, int32_t endBlock, Transaction *txn) {
    se->tableId  = tableId;
    se->endBlock = endBlock;
    se->transaction = txn;
}

/* =========================================================================
 * 1. WHERE — all conditions ANDed (uses evaluateAllVar)
 * ========================================================================= */

static int8_t sql_matchWhere(SqlExecutor *se, Tuple *t) {
    for (int j = 0; j < se->condCount; j++) {
        SqlCondition *c = &se->conditions[j];
        AllVar *v = &t->dnb.data[c->column];
        if (!evaluateAllVar(v, &c->value, c->op, c->method)) return 0;
    }
    return 1;
}

/* =========================================================================
 * 2. SELECT — project columns into a new tuple (malloc)
 * ========================================================================= */

static Tuple *sql_doSelect(SqlExecutor *se, Tuple *t) {
    Tuple *r = (Tuple *)malloc(sizeof(Tuple));
    tuple_init(r);
    r->dnb.data_count    = se->selCount;
    r->dnb.bit_map_count = se->selCount;
    for (int j = 0; j < se->selCount; j++) {
        r->dnb.data[j]    = t->dnb.data[se->selColumns[j]];
        r->dnb.bit_map[j] = t->dnb.bit_map[se->selColumns[j]];
    }
    return r;
}

/* =========================================================================
 * 3. UPDATE — modyfikacja in-place
 * ========================================================================= */

uint32_t pack(int16_t a, int16_t b) {
    return ((uint32_t)(uint16_t)a << 16) | (uint16_t)b;
}
void unpack(uint32_t p, int16_t *a, int16_t *b) {
    *a = (int16_t)(p >> 16);
    *b = (int16_t)(p & 0xFFFF);
}

/* xmax == 0 or xmax < 0 means "not deleted" */
static inline int8_t xmax_is_none(int32_t xmax) {
    return xmax == 0 || xmax < 0;
}

static inline int8_t isVisible(Tuple *tuple, int32_t xid) {
    if (tuple->header.t_xmin > xid) return 0;
    if (!xmax_is_none(tuple->header.t_xmax) && tuple->header.t_xmax <= xid) return 0;
    return 1;
}

static inline int8_t canVacuum(Tuple *tuple, MVCC *mvcc) {
    if (xmax_is_none(tuple->header.t_xmax)) return 0;
    int32_t oldest = getOldestXid(mvcc);
    /* oldest == -1 → no active transactions → everything with xmax can be removed */
    if (oldest == -1) return 1;
    /* xmax < oldest → every active transaction (xid >= oldest) sees the tuple as dead */
    return tuple->header.t_xmax < oldest;
}

/*
 * Tombstone: frees the tuple data, only the header is kept.
 *   keepChain = 0 → free slot (DEAD|UNUSED, t_cid = 0) — nothing points to it anymore,
 *                   block8kb_add may reuse it.
 *   keepChain = 1 → redirect (DEAD, t_cid kept) — for a root, where a chain scan
 *                   starts; like LP_REDIRECT in Postgres.
 * Indexes are not touched: sql_doUpdate already removes the old version's entries on UPDATE.
 */
static inline void sql_makeTombstone(Tuple *t, DataBuffor *buf, int8_t keepChain) {
    if (keepChain) {
        t->header.t_infomask = INFOMASK_DEAD;
    } else {
        t->header.t_infomask = INFOMASK_DEAD | INFOMASK_UNUSED;
        t->header.t_cid = 0;
    }
    dnb_init(&t->dnb);
    buf->isDirty = 1;
}

/*
 * Vacuum a single version in a chain. `currTuple` and `prevTuple` must be in the same block (`buf`),
 * so the relinked prev->t_cid reaches disk together with currTuple's tombstone.
 * Returns 1 if currTuple was removed.
 */
static inline int8_t vacumingTupleIfDead(Tuple *currTuple, Tuple *prevTuple, MVCC *mvcc, DataBuffor *buf) {
    if (currTuple == NULL || prevTuple == NULL || mvcc == NULL || buf == NULL) return 0;
    if (!canVacuum(currTuple, mvcc)) return 0;

    /* relink the chain: prevTuple skips over currTuple */
    prevTuple->header.t_cid = currTuple->header.t_cid;
    sql_makeTombstone(currTuple, buf, 0);
    return 1;
}

/*
 * One step along the chain for followChainRR/RC: vacuum `*curr` (if the previous version is in the
 * same block), unpin the current buffer and load the next version.
 * Positions are kept as (block, index), not pointers — the buffer may get replaced.
 * `rootBlockId` — the root's block; the caller holds its buffer, so we do not unpin it.
 * Returns 1 if `*curr` was removed; `*curr` = next version or NULL.
 */
static inline int8_t sql_chainStep(Tuple **curr, DataBuffor **currBuf, int32_t *currBlockId, int32_t *currIdx,
                                   int32_t *prevIdx, int32_t rootBlockId, Buffors *buffors, int32_t tableId,
                                   MVCC *mvcc) {
    int16_t nextBlockId, nextIdx;
    unpack((uint32_t)(*curr)->header.t_cid, &nextBlockId, &nextIdx);

    int8_t removed = 0;
    if (*currBuf != NULL && *prevIdx >= 0) {
        removed = vacumingTupleIfDead(*curr, &(*currBuf)->universalBlock->block->tuples[*prevIdx], mvcc,
                                      *currBuf);
    }
    /* after removing curr, the current prev stays the predecessor of the next version */
    int32_t newPrevIdx  = removed ? *prevIdx : *currIdx;
    int32_t prevBlockId = *currBlockId;

    if (*currBuf != NULL && *currBlockId != rootBlockId) (*currBuf)->pinCount = 0;
    *currBuf = NULL;
    *curr = NULL;

    DataBuffor *buf = getBuffor(tableId, (int32_t)nextBlockId, buffors);
    if (buf == NULL) return removed;
    if (buf->universalBlock == NULL) {
        if ((int32_t)nextBlockId != rootBlockId) buf->pinCount = 0;
        return removed;
    }
    *currBuf     = buf;
    *currBlockId = (int32_t)nextBlockId;
    *currIdx     = (int32_t)nextIdx;
    *prevIdx     = ((int32_t)nextBlockId == prevBlockId) ? newPrevIdx : -1;
    *curr        = &buf->universalBlock->block->tuples[(int32_t)nextIdx];
    return removed;
}

static inline void sql_chainRelease(DataBuffor *currBuf, int32_t currBlockId, int32_t rootBlockId) {
    if (currBuf != NULL && currBlockId != rootBlockId) currBuf->pinCount = 0;
}

/* Follow chain for Repeatable Read: returns the version visible at xid.
 * rootBlockId/rootIdx — position of `start` in the table (needed to vacuum the version right after the root).
 * outBlockId/outIdx (may be NULL) — position of the returned version. */
static Tuple *sql_followChainRR(Tuple *start, int32_t rootBlockId, int32_t rootIdx, Buffors *buffors,
                                int32_t tableId, int32_t xid, MVCC *mvcc,
                                int32_t *outBlockId, int32_t *outIdx) {
    Tuple      *curr        = start;
    DataBuffor *currBuf     = NULL;   /* the caller holds the root's buffer */
    int32_t     currBlockId = rootBlockId;
    int32_t     currIdx     = rootIdx;
    int32_t     prevIdx     = -1;
    while (curr != NULL) {
        if (curr->header.t_xmin <= xid &&
            (xmax_is_none(curr->header.t_xmax) || curr->header.t_xmax > xid)) {
            if (outBlockId != NULL) *outBlockId = currBlockId;
            if (outIdx != NULL)     *outIdx     = currIdx;
            sql_chainRelease(currBuf, currBlockId, rootBlockId);
            return curr;
        }
        if (curr->header.t_cid == 0) break;
        sql_chainStep(&curr, &currBuf, &currBlockId, &currIdx, &prevIdx, rootBlockId,
                      buffors, tableId, mvcc);
    }
    sql_chainRelease(currBuf, currBlockId, rootBlockId);
    return NULL;
}

/* Follow chain for Read Committed: returns the latest version with xmin <= xid */
static Tuple *sql_followChainRC(Tuple *start, int32_t rootBlockId, int32_t rootIdx, Buffors *buffors,
                                int32_t tableId, int32_t xid, MVCC *mvcc,
                                int32_t *outBlockId, int32_t *outIdx) {
    Tuple      *cur         = start;
    DataBuffor *currBuf     = NULL;   /* the caller holds the root's buffer */
    int32_t     currBlockId = rootBlockId;
    int32_t     currIdx     = rootIdx;
    int32_t     prevIdx     = -1;
    Tuple      *lastVisible = NULL;
    int32_t     lastBlockId = -1, lastIdx = -1;
    while (cur != NULL) {
        /* a tombstone (e.g. a root redirect after vacuum) has no data — it cannot be a result */
        if (cur->header.t_xmin <= xid && !(cur->header.t_infomask & INFOMASK_DEAD)) {
            lastVisible = cur;
            lastBlockId = currBlockId;
            lastIdx     = currIdx;
        }
        if (cur->header.t_cid == 0) break;
        Tuple *before = cur;
        if (sql_chainStep(&cur, &currBuf, &currBlockId, &currIdx, &prevIdx, rootBlockId,
                          buffors, tableId, mvcc) && lastVisible == before) {
            /* a removed version cannot be a result — a newer version will overwrite lastVisible */
            lastVisible = NULL;
        }
    }
    sql_chainRelease(currBuf, currBlockId, rootBlockId);
    if (lastVisible != NULL) {
        if (outBlockId != NULL) *outBlockId = lastBlockId;
        if (outIdx != NULL)     *outIdx     = lastIdx;
    }
    return lastVisible;
}

/*
 * UPDATE leaves the old version for vacuum to remove. The counter lives in the ROOT's block
 * (header.dead_count + bq queue), because vacuum walks from roots — only then does it know the
 * predecessor and can relink the chain. The counter is a hint: vacuum recomputes it per block.
 */
static inline void sql_noteDeadVersion(SqlExecutor *se, Block8kb *rootBlock) {
    if (rootBlock->header.dead_count < INT16_MAX) rootBlock->header.dead_count++;
    if (se->bqMgr == NULL) return;
    bq_t *q = bq_mgr_get(se->bqMgr, se->tableId);
    if (q == NULL) return;
    int32_t key = (int32_t)rootBlock->header.block_id;
    int32_t cur;
    if (bq_get(q, key, &cur) == BQ_OK) {
        bq_incr(q, key, NULL);   /* BQ_ERANGE at the maximum — stays at the maximum */
    } else {
        bq_insert(q, key, 1);
    }
}

/*
 * t — the visible version (at visBlockId/visIdx), rootBlock/rootIdx — root of its chain.
 * The caller holds the root's block; if t is in another block, it is re-fetched by TID after
 * inserting the new version (the insert may have evicted that block).
 */
static void sql_doUpdate(SqlExecutor *se, Tuple *t, int32_t visBlockId, int32_t visIdx,
                         Block8kb *rootBlock, int32_t rootIdx,
                         Buffors *buffors, FSMCache *c, FSMMapAll *fsmMapAll) {
    Tuple newTuple = *t;
    int32_t rootBlockId = (int32_t)rootBlock->header.block_id;
    int8_t  isRoot = (visBlockId == rootBlockId && visIdx == rootIdx);

    newTuple.header.t_xmin = se->transaction->xid;

    for (int j = 0; j < se->updCount; j++) {
        newTuple.dnb.data[se->updColumns[j]] = se->updValues[j];
    }

    /* new version first — if it fails, the old one stays untouched */
    int32_t newIdx = -1;
    DataBuffor *dataBuffor = addTupleToOtherFunction(buffors, c, fsmMapAll, se->tableId,
        newTuple.dnb.data, newTuple.dnb.data_count,
        newTuple.dnb.bit_map, newTuple.dnb.bit_map_count,
        se->transaction->xid, -1, -1, newTuple.header.t_infomask,
        newTuple.header.t_hoff, newTuple.header.null_bitmap, newTuple.header.optional_oid,
        se->fsmMapBtree, se->btreeBuffors, &newIdx);
    if (dataBuffor == NULL) {
        LOG_ERROR("sql_doUpdate: failed to insert new tuple version");
        return;
    }
    int32_t newBlockId = (int32_t)dataBuffor->universalBlock->block->header.block_id;
    /* mark the new tuple as a chain member via infomask — t_cid=0 means end of chain */
    dataBuffor->universalBlock->block->tuples[newIdx].header.t_infomask |= INFOMASK_CHAIN_MEMBER;
    dataBuffor->universalBlock->block->tuples[newIdx].header.t_cid = 0;
    dataBuffor->isDirty = 1;


    DataBuffor *tBuf = NULL;
    if (isRoot) {
        t = &rootBlock->tuples[rootIdx];
    } else {
        tBuf = getBuffor(se->tableId, visBlockId, buffors);
        if (tBuf == NULL || tBuf->universalBlock == NULL) {
            LOG_ERROR("sql_doUpdate: cannot reload updated tuple block %d", visBlockId);
            if (tBuf != NULL && visBlockId != rootBlockId) tBuf->pinCount = 0;
            dataBuffor->pinCount = 0;
            return;
        }
        t = &tBuf->universalBlock->block->tuples[visIdx];
    }

    if (se->fsmMapBtree != NULL && se->btreeBuffors != NULL) {
        btree_delete_tuple_indexes(se->fsmMapBtree, se->btreeBuffors, se->tableId, t, visBlockId);
    }

    t->header.t_xmax = se->transaction->xid;
    t->header.t_cid  = pack((int16_t)newBlockId, (int16_t)newIdx);
    if (tBuf != NULL) {
        tBuf->isDirty = 1;
        if (visBlockId != rootBlockId) tBuf->pinCount = 0;
    }
    if (newBlockId != rootBlockId) dataBuffor->pinCount = 0;

    sql_noteDeadVersion(se, rootBlock);
}

/* =========================================================================
 * 4. Transaction isolation
 *    VIEW_MODE 1 = Repeatable Read  — visible if xmin <= xid and not deleted
 *    VIEW_MODE 2 = Read Committed   — tracks the newest xid in the block
 * ========================================================================= */

// Repeatable Read: returns 1 if the tuple is visible
static int8_t sql_isVisibleRR(SqlExecutor *se, Tuple *t) {
    if (t->header.t_xmin > se->transaction->xid) return 0;
    if (t->header.t_xmax != 0 && t->header.t_xmax <= se->transaction->xid) return 0;
    return 1;
}

// Read Committed: updates the tracked newest xid
static void sql_trackRC(Tuple *t, int32_t index, int32_t *xidMax, int32_t *bestIndex) {
    int32_t xid = (t->header.t_xmin > t->header.t_xmax)
                  ? t->header.t_xmin
                  : t->header.t_xmax;
    if (xid > *xidMax) {
        *xidMax    = xid;
        *bestIndex = index;
    }
}

/* =========================================================================
 * 5. Single pass over one block (Repeatable Read only)
 *    order: isolation → WHERE → UPDATE → SELECT/result
 * ========================================================================= */

/* Returns the number of updated tuples — the caller then marks the block isDirty */
int32_t sql_execBlock(Block8kb *block, SqlExecutor *se, ResultTuple *result,
                      Buffors *buffors, FSMCache *c, FSMMapAll *fsmMapAll, MVCC *mvcc) {
    int32_t updated = 0;
    for (int i = 0; i < block->tuple_count; i++) {
        if (result->tuple_count >= RESULT_SPACE) break;

        Tuple *t = &block->tuples[i];

        /* skip chain members — reachable only by chain traversal from the root; skip free slots */
        if (t->header.t_infomask & (INFOMASK_CHAIN_MEMBER | INFOMASK_UNUSED)) continue;

        /* RR: walk the chain and find the version visible at this transaction's xid */
        int32_t visBlockId = -1, visIdx = -1;
        Tuple *visible = sql_followChainRR(t, (int32_t)block->header.block_id, i, buffors, se->tableId,
                                           se->transaction->xid, mvcc, &visBlockId, &visIdx);
        if (visible == NULL) continue;
        if (se->where && !sql_matchWhere(se, visible)) continue;
        if (se->update) {
            sql_doUpdate(se, visible, visBlockId, visIdx, block, i, buffors, c, fsmMapAll);
            updated++;
        }

        if (se->select) {
            result->tuples[result->tuple_count++] = sql_doSelect(se, visible);
        } else {
            result->tuples[result->tuple_count++] = visible;
        }
    }
    return updated;
}


/* resolve block deciding on whetever to use indexes block or use full scan blocks
 * it returns structure with block numbers .
*/
static inline BtreeBlocksResult sql_resolveBlocks(SqlExecutor *se) {
    BtreeBlocksResult res = {NULL, 0};

    // we are checking if there any where coindtion and number of tem and also of index structure was created .
    if (se->where && se->condCount > 0 && se->fsmMapBtree != NULL && se->btreeBuffors != NULL) {
        for (int c = 0; c < se->condCount; c++) {
            SqlCondition *cond = &se->conditions[c];
            BtreeTableEntry *table = fsm_btree_get_table(se->fsmMapBtree, se->tableId);
            if (table != NULL && fsm_btree_get_column(table, cond->column) != NULL) {
                res = getBlocksBtree(cond->value, se->btreeBuffors, se->tableId,
                                     cond->column, se->fsmMapBtree, cond->op, cond->method);
                return res;
            }
        }
    }

    /* No index — sequential scan over all blocks */
    if (se->endBlock > 0) {
        res.blocks = (int32_t *)malloc(se->endBlock * sizeof(int32_t));
        res.count = se->endBlock;
        for (int i = 0; i < se->endBlock; i++) {
            res.blocks[i] = i + 1;
        }
    }
    return res;
}

void sql_fullScan(SqlExecutor *se, ResultTuple *result, Buffors *buffors,
                  FSMCache *c, FSMMapAll *fsmMapAll, MVCC *mvcc) {
    BtreeBlocksResult blocksToScan = sql_resolveBlocks(se);

    if (VIEW_MODE == 2) {
        // Read Committed — chain traversal
        for (int idx = 0; idx < blocksToScan.count; idx++) {
            int32_t i = blocksToScan.blocks[idx];
            DataBuffor *buf = getBuffor(se->tableId, i, buffors);
            if (buf == NULL) continue;
            if (buf->universalBlock == NULL) { buf->pinCount = 0; continue; }
            Block8kb   *block = buf->universalBlock->block;
            for (int j = 0; j < block->tuple_count; j++) {
                if (result->tuple_count >= RESULT_SPACE) break;
                Tuple *t = &block->tuples[j];

                /* skip chain members and free slots */
                if (t->header.t_infomask & (INFOMASK_CHAIN_MEMBER | INFOMASK_UNUSED)) continue;

                /* RC: walk the chain to the newest version with xmin <= xid */
                int32_t visBlockId = -1, visIdx = -1;
                Tuple *visible = sql_followChainRC(t, i, j, buffors, se->tableId, se->transaction->xid, mvcc,
                                                   &visBlockId, &visIdx);

                if (visible == NULL) continue;

                if (se->where && !sql_matchWhere(se, visible)) continue;
                if (se->update) {
                    sql_doUpdate(se, visible, visBlockId, visIdx, block, j, buffors, c, fsmMapAll);
                    buf->isDirty = 1;   /* the root got a new counter / t_xmax, t_cid */
                }
                if (se->select) {
                    result->tuples[result->tuple_count++] = sql_doSelect(se, visible);
                } else {
                    result->tuples[result->tuple_count++] = visible;
                }
            }
            buf->isUsed   = 1;
            buf->pinCount = 0;
        }
        free(blocksToScan.blocks);
        return;
    }

    // Repeatable Read — scan the blocks
    for (int idx = 0; idx < blocksToScan.count; idx++) {
        int32_t i = blocksToScan.blocks[idx];
        DataBuffor *buf = getBuffor(se->tableId, i, buffors);
        if (buf == NULL) continue;
        if (buf->universalBlock == NULL) { buf->pinCount = 0; continue; }
        if (sql_execBlock(buf->universalBlock->block, se, result, buffors, c, fsmMapAll, mvcc) > 0) {
            buf->isDirty = 1;   /* the root got a new counter / t_xmax, t_cid */
        }
        buf->isUsed   = 1;
        buf->pinCount = 0;
    }
    free(blocksToScan.blocks);
}

#endif //QUAKEDB3_0_SQLEXECUTOR_H