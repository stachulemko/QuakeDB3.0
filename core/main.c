#include <stdio.h>
#include <stdlib.h>
#include "vacuum/vacuum/vacuum.h"
#include "bufforing-stm/bufforing-stm/transaction.h"

/*
 * Example program: a table (id INT, name STRING) and a full MVCC cycle.
 *   1. INSERT a few rows
 *   2. SELECT
 *   3. UPDATE — the old version stays, the new one is appended to the chain
 *   4. Repeatable Read isolation — an older transaction sees the old data
 *   5. VACUUM — relinking the chain, freeing slots
 *   6. INSERT goes into a freed slot
 *
 * The buffer pool is large enough that nothing is evicted to disk.
 */

#define TABLE_ID 20

typedef struct {
    Buffors    buffors;
    FSMCache   fsmCache;
    FSMMapAll  fsmMapAll;
    MVCC       mvcc;
    BqManager  bq;
} Db;

static void db_init(Db *db) {
    initializeBuffors(&db->buffors, 16);
    fsm_cache_init(&db->fsmCache);
    fsm_cache_set(&db->fsmCache, TABLE_ID);
    init_FSMMapAll(&db->fsmMapAll);
    addTableToFSMMapAll(&db->fsmMapAll, TABLE_ID);
    mvcc_init(&db->mvcc);
    bq_mgr_init(&db->bq, 256, 0);
    bq_mgr_add_table(&db->bq, TABLE_ID);
}

static void db_free(Db *db) {
    for (int i = 0; i < db->buffors.count; i++) {
        DataBuffor *b = &db->buffors.buffors[i];
        if (b->isUsed && b->universalBlock) {
            free(b->universalBlock->block);
            free(b->universalBlock->header);
            free(b->universalBlock);
        }
    }
    free(db->buffors.buffors);
    fsm_cache_free(&db->fsmCache);
    free_FSMMapAll(&db->fsmMapAll);
    bq_mgr_free(&db->bq);
}

static void db_begin(Db *db, Transaction *txn) {
    beginTransaction(&db->mvcc, txn);
    incrementTxnCounter(&db->mvcc);
}

static SqlExecutor db_executor(Db *db, Transaction *txn) {
    SqlExecutor se = {0};
    se.transaction = txn;
    se.tableId     = TABLE_ID;
    se.endBlock    = fsm_cache_get(&db->fsmCache, TABLE_ID)->maxBlock;
    se.bqMgr       = &db->bq;
    return se;
}

static void db_insert(Db *db, Transaction *txn, int32_t id, const char *name) {
    SqlExecutor se = db_executor(db, txn);
    AllVar vals[2] = {all_var_from_int32(id), all_var_from_string(name)};
    int8_t bm[2]   = {0, 0};
    sql_addTuple(&se, TABLE_ID, vals, &db->buffors, &db->fsmCache, &db->fsmMapAll, txn, 2, bm, 2);
}

static void db_update_name(Db *db, Transaction *txn, int32_t id, const char *name) {
    SqlExecutor se = db_executor(db, txn);
    sql_addWhere(&se, 0, all_var_from_int32(id), SQL_EQ, 1);
    int32_t cols[] = {1};
    AllVar  vals[] = {all_var_from_string(name)};
    sql_setUpdate(&se, cols, vals, 1);
    ResultTuple result = {0};
    sql_fullScan(&se, &result, &db->buffors, &db->fsmCache, &db->fsmMapAll, &db->mvcc);
}

static void db_select_all(Db *db, Transaction *txn, const char *title) {
    SqlExecutor se = db_executor(db, txn);
    int32_t cols[] = {0, 1};
    sql_setSelect(&se, cols, 2);
    ResultTuple result = {0};
    sql_fullScan(&se, &result, &db->buffors, &db->fsmCache, &db->fsmMapAll, &db->mvcc);

    printf("%s (xid %d):\n", title, txn->xid);
    for (int i = 0; i < result.tuple_count; i++) {
        Tuple *t = result.tuples[i];
        printf("    id=%d  name=%s\n", t->dnb.data[0].val.i32, t->dnb.data[1].val.str);
        free(t);
    }
}

/* Block contents: slot, flags, xmin/xmax and the next version in the chain */
static void db_show_block(Db *db, int32_t blockId, const char *title) {
    DataBuffor *buf = getBuffor(TABLE_ID, blockId, &db->buffors);
    Block8kb *blk = buf->universalBlock->block;
    printf("%s — blok %d, slotów %d, dead_count %d:\n",
           title, blockId, blk->tuple_count, blk->header.dead_count);
    for (int i = 0; i < blk->tuple_count; i++) {
        Tuple *t = &blk->tuples[i];
        int32_t mask = t->header.t_infomask;
        const char *kind = (mask & INFOMASK_UNUSED)       ? "WOLNY"
                         : (mask & INFOMASK_DEAD)         ? "PRZEKIEROWANIE"
                         : (mask & INFOMASK_CHAIN_MEMBER) ? "wersja"
                                                          : "root";
        printf("    [%d] %-14s xmin=%-2d xmax=%-2d", i, kind, t->header.t_xmin, t->header.t_xmax);
        if (t->dnb.data_count == 2) {
            printf(" id=%d name=%-8s", t->dnb.data[0].val.i32, t->dnb.data[1].val.str);
        }
        if (t->header.t_cid != 0) {
            int16_t nb, ni;
            unpack((uint32_t)t->header.t_cid, &nb, &ni);
            printf(" -> %d/%d", nb, ni);
        }
        printf("\n");
    }
    buf->pinCount = 0;
}

int main(void) {
    Db db;
    db_init(&db);

    /* 1. INSERT */
    Transaction t1;
    db_begin(&db, &t1);
    db_insert(&db, &t1, 1, "Alice");
    db_insert(&db, &t1, 2, "Patrick");
    db_insert(&db, &t1, 3, "Alan");
    db_insert(&db, &t1, 4, "Eve");
    commitTransaction(&db.mvcc, &t1);

    /* 2. SELECT */
    Transaction t2;
    db_begin(&db, &t2);
    db_select_all(&db, &t2, "\n[1] Po INSERT");
    commitTransaction(&db.mvcc, &t2);

    /* 3. UPDATE — the same row twice, creating the chain root -> v1 -> v2 */
    Transaction t3;
    db_begin(&db, &t3);
    db_update_name(&db, &t3, 2, "Pat");
    commitTransaction(&db.mvcc, &t3);

    Transaction t4;
    db_begin(&db, &t4);
    db_update_name(&db, &t4, 2, "Patryk");
    commitTransaction(&db.mvcc, &t4);

    printf("\n");
    db_show_block(&db, 1, "[2] Po dwóch UPDATE wiersza id=2");

    /* 4. Repeatable Read: t5 starts before t6's change and still sees the old data.
     *    The scan of t6's UPDATE prunes the "Pat" version on the way (nobody sees it anymore),
     *    and the new version "Alan2" immediately takes its slot. */
    Transaction t5, t6;
    db_begin(&db, &t5);
    db_begin(&db, &t6);
    db_update_name(&db, &t6, 3, "Alan2");
    commitTransaction(&db.mvcc, &t6);

    db_select_all(&db, &t5, "\n[3] Starsza transakcja t5 — nie widzi zmiany z t6");
    Transaction t7;
    db_begin(&db, &t7);
    db_select_all(&db, &t7, "[3] Nowa transakcja t7 — widzi zmianę");
    commitTransaction(&db.mvcc, &t7);
    printf("\n");
    db_show_block(&db, 1, "[3] Slot 4: \"Pat\" usunięte przy skanie, na jego miejscu \"Alan2\"");

    /* 5. VACUUM — t5 is still active, so the versions it sees are kept */
    Vacuum vacuum = {0};
    autoVacuumScan(&vacuum, TABLE_ID, &db.fsmCache, &db.buffors, &db.bq, &db.mvcc);
    printf("\n");
    db_show_block(&db, 1, "[4] VACUUM przy aktywnej t5");

    commitTransaction(&db.mvcc, &t5);

    /* two UPDATEs of row id=4: root -> "Ewa" -> "Ewelina" */
    Transaction t8, t9;
    db_begin(&db, &t8);
    db_update_name(&db, &t8, 4, "Ewa");
    commitTransaction(&db.mvcc, &t8);
    db_begin(&db, &t9);
    db_update_name(&db, &t9, 4, "Ewelina");
    commitTransaction(&db.mvcc, &t9);

    autoVacuumScan(&vacuum, TABLE_ID, &db.fsmCache, &db.buffors, &db.bq, &db.mvcc);
    printf("\n");
    db_show_block(&db, 1, "[5] VACUUM po zakończeniu t5 — root id=4 -> \"Ewelina\", \"Ewa\" usunięta");

    /* 6. INSERT goes into the first free slot, not at the end */
    Transaction t10;
    db_begin(&db, &t10);
    db_insert(&db, &t10, 5, "Bob");
    commitTransaction(&db.mvcc, &t10);
    printf("\n");
    db_show_block(&db, 1, "[6] INSERT id=5 — zajmuje wolny slot");

    Transaction t11;
    db_begin(&db, &t11);
    db_select_all(&db, &t11, "\n[7] Końcowy stan tabeli");
    commitTransaction(&db.mvcc, &t11);

    db_free(&db);
    return 0;
}