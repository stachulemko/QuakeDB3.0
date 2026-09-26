
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vacuum/vacuum.h"
#include "../../bufforing-stm/bufforing-stm/mvcc.h"
#include "../../bufforing-stm/bufforing-stm/transaction.h"

/* =========================================================================
 * Infrastructure — as in test_sqlExecutor_chain.c
 * ========================================================================= */

#define VAC_TABLE 1

typedef struct {
    Buffors    buffors;
    FSMCache  *c;
    FSMMapAll  fsmMapAll;
    MVCC      *mvcc;
    BqManager  bq;
} TestEnv;

static void env_setup(TestEnv *env) {
    initializeBuffors(&env->buffors, 30);
    FSMCacheCreateC(&env->c);
    fsm_cache_set(env->c, VAC_TABLE);
    init_FSMMapAll(&env->fsmMapAll);
    addTableToFSMMapAll(&env->fsmMapAll, VAC_TABLE);
    create_MVCC(&env->mvcc);
    /* no active transactions */
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        env->mvcc->txn_status[i] = TXN_COMMITED;
    }
    bq_mgr_init(&env->bq, 256, 0);
    bq_mgr_add_table(&env->bq, VAC_TABLE);
}

static void env_teardown(TestEnv *env) {
    for (int i = 0; i < env->buffors.count; i++) {
        if (env->buffors.buffors[i].isUsed && env->buffors.buffors[i].universalBlock) {
            if (env->buffors.buffors[i].universalBlock->block)
                free(env->buffors.buffors[i].universalBlock->block);
            if (env->buffors.buffors[i].universalBlock->header)
                free(env->buffors.buffors[i].universalBlock->header);
            free(env->buffors.buffors[i].universalBlock);
        }
    }
    free(env->buffors.buffors);
    free(env->c);
    free(env->mvcc);
    bq_mgr_free(&env->bq);
}

static int32_t env_endblock(TestEnv *env) {
    BlockCounterEntry *e = fsm_cache_get(env->c, VAC_TABLE);
    return e ? e->maxBlock : 0;
}

static void env_insert(TestEnv *env, int32_t xmin, int32_t val) {
    AllVar vals[1] = {all_var_from_int32(val)};
    int8_t bm[1]  = {0};
    DataBuffor *b = addTupleToOtherFunction(&env->buffors, env->c, &env->fsmMapAll,
        VAC_TABLE, vals, 1, bm, 1,
        xmin, 0, 0, 0, 0, 0, -1, NULL, NULL, NULL);
    if (b != NULL) b->pinCount = 0;
}

/* UPDATE col0 WHERE col0 == where_val SET col0 = new_val; useBq → the counter goes to bq */
static void env_update(TestEnv *env, int32_t txn_xid, int32_t where_val, int32_t new_val, int8_t useBq) {
    Transaction txn = {.xid = txn_xid};
    SqlExecutor se  = {0};
    se.transaction  = &txn;
    se.tableId      = VAC_TABLE;
    se.endBlock     = env_endblock(env);
    se.bqMgr        = useBq ? &env->bq : NULL;
    sql_addWhere(&se, 0, all_var_from_int32(where_val), SQL_EQ, 1);
    int32_t upd_cols[] = {0};
    AllVar  upd_vals[] = {all_var_from_int32(new_val)};
    sql_setUpdate(&se, upd_cols, upd_vals, 1);
    ResultTuple result = {0};
    sql_fullScan(&se, &result, &env->buffors, env->c, &env->fsmMapAll, env->mvcc);
}

/* SELECT col0 WHERE col0 == val — how many rows transaction xid sees */
static int32_t env_count(TestEnv *env, int32_t txn_xid, int32_t val) {
    env->mvcc->txn_status[txn_xid] = TXN_ACTIVE;
    Transaction txn = {.xid = txn_xid};
    SqlExecutor se  = {0};
    se.transaction  = &txn;
    se.tableId      = VAC_TABLE;
    se.endBlock     = env_endblock(env);
    int32_t cols[]  = {0};
    sql_setSelect(&se, cols, 1);
    sql_addWhere(&se, 0, all_var_from_int32(val), SQL_EQ, 1);
    ResultTuple result = {0};
    sql_fullScan(&se, &result, &env->buffors, env->c, &env->fsmMapAll, env->mvcc);
    env->mvcc->txn_status[txn_xid] = TXN_COMMITED;
    for (int i = 0; i < result.tuple_count; i++) free(result.tuples[i]);
    return result.tuple_count;
}

static Block8kb *env_block(TestEnv *env, int32_t blockId) {
    DataBuffor *b = getBuffor(VAC_TABLE, blockId, &env->buffors);
    assert_non_null(b);
    assert_non_null(b->universalBlock);
    b->pinCount = 0;
    return b->universalBlock->block;
}

static Tuple *env_tuple(TestEnv *env, int32_t blockId, int32_t idx) {
    return &env_block(env, blockId)->tuples[idx];
}

static Tuple *env_follow(TestEnv *env, Tuple *t, int32_t *blockId, int32_t *idx) {
    int16_t b, i;
    unpack((uint32_t)t->header.t_cid, &b, &i);
    if (blockId) *blockId = b;
    if (idx)     *idx = i;
    return env_tuple(env, b, i);
}

static Vacuum make_vacuum(void) {
    Vacuum v = {0};
    v.densityDeadTuplesPerBlockMax = 0;   /* do not stop early */
    return v;
}

/* =========================================================================
 * 1. Chain within one block: root → v1 → v2, vacuum relinks root → v2
 * ========================================================================= */

static void test_vacuum_relinks_chain_same_block(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    env_insert(&env, 1, 10);            /* slot 0 */
    env_update(&env, 2, 10, 20, 0);     /* slot 1 */
    env_update(&env, 3, 20, 30, 0);     /* slot 2 */

    Block8kb *blk = env_block(&env, 1);
    assert_int_equal(3, blk->tuple_count);
    assert_int_equal(2, blk->header.dead_count);   /* counter in the root's block */

    Vacuum vac = make_vacuum();
    vac.sumOfAllDeadTupleInsideTable = 2;
    autoVacuumScan(&vac, VAC_TABLE, env.c, &env.buffors, NULL, env.mvcc);

    Tuple *root = env_tuple(&env, 1, 0);
    /* root: redirect — data freed, the chain stays */
    assert_true(root->header.t_infomask & INFOMASK_DEAD);
    assert_false(root->header.t_infomask & INFOMASK_UNUSED);
    assert_int_equal(0, root->dnb.data_count);

    int32_t b, i;
    Tuple *next = env_follow(&env, root, &b, &i);
    assert_int_equal(1, b);
    assert_int_equal(2, i);                         /* root → v2, v1 skipped */
    assert_int_equal(30, next->dnb.data[0].val.i32);

    Tuple *v1 = env_tuple(&env, 1, 1);
    assert_true(v1->header.t_infomask & INFOMASK_UNUSED);
    assert_int_equal(0, v1->dnb.data_count);

    assert_int_equal(0, env_block(&env, 1)->header.dead_count);
    assert_int_equal(0, vac.sumOfAllDeadTupleInsideTable);

    /* a read after vacuum still sees the current version */
    assert_int_equal(1, env_count(&env, 10, 30));
    assert_int_equal(0, env_count(&env, 10, 20));
    assert_int_equal(0, env_count(&env, 10, 10));

    env_teardown(&env);
}

/* =========================================================================
 * 2. After vacuum an insert goes into the freed slot
 * ========================================================================= */

static void test_insert_reuses_slot_after_vacuum(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    env_insert(&env, 1, 10);
    env_update(&env, 2, 10, 20, 0);
    env_update(&env, 3, 20, 30, 0);

    Vacuum vac = make_vacuum();
    autoVacuumScan(&vac, VAC_TABLE, env.c, &env.buffors, NULL, env.mvcc);

    env_insert(&env, 4, 99);
    Block8kb *blk = env_block(&env, 1);
    assert_int_equal(3, blk->tuple_count);          /* not appended at the end */
    assert_int_equal(99, blk->tuples[1].dnb.data[0].val.i32);
    assert_false(blk->tuples[1].header.t_infomask & INFOMASK_UNUSED);

    /* both rows visible, chain intact */
    assert_int_equal(1, env_count(&env, 10, 30));
    assert_int_equal(1, env_count(&env, 10, 99));

    env_teardown(&env);
}

/* =========================================================================
 * 3. Chain across two blocks: root in block 1, versions in block 2
 * ========================================================================= */

static void test_vacuum_relinks_chain_across_blocks(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    /* fill block 1 until block 2 is created — the next versions go to block 2 */
    for (int32_t v = 0; env_endblock(&env) < 2 && v < 10000; v++) env_insert(&env, 1, 1000 + v);
    assert_int_equal(2, env_endblock(&env));
    assert_int_equal(1000, env_tuple(&env, 1, 0)->dnb.data[0].val.i32);

    env_update(&env, 2, 1000, 20, 0);   /* v1 → block 2 */
    env_update(&env, 3, 20, 30, 0);     /* v2 → block 2 */
    env_update(&env, 4, 30, 40, 0);     /* v3 → block 2 */
    assert_int_equal(2, env_endblock(&env));

    Tuple *root = env_tuple(&env, 1, 0);
    int32_t v1Block, v1Idx;
    env_follow(&env, root, &v1Block, &v1Idx);
    assert_int_equal(2, v1Block);
    assert_int_equal(3, env_block(&env, 1)->header.dead_count);  /* counter in the root's block */
    assert_int_equal(0, env_block(&env, 2)->header.dead_count);

    Vacuum vac = make_vacuum();
    autoVacuumScan(&vac, VAC_TABLE, env.c, &env.buffors, NULL, env.mvcc);

    /* root (block 1) points directly to v3 (block 2) */
    int32_t b, i;
    Tuple *last = env_follow(&env, env_tuple(&env, 1, 0), &b, &i);
    assert_int_equal(2, b);
    assert_int_equal(40, last->dnb.data[0].val.i32);
    assert_int_equal(0, last->header.t_cid);

    /* v1 and v2 in block 2 are free */
    Block8kb *b2 = env_block(&env, 2);
    int32_t unused = 0;
    for (int32_t k = 0; k < b2->tuple_count; k++)
        if (b2->tuples[k].header.t_infomask & INFOMASK_UNUSED) unused++;
    assert_int_equal(2, unused);
    assert_int_equal(0, env_block(&env, 1)->header.dead_count);

    assert_int_equal(1, env_count(&env, 10, 40));
    assert_int_equal(1, env_count(&env, 10, 1001));

    env_teardown(&env);
}

/* =========================================================================
 * 4. Versions visible to an active transaction are not removed
 * ========================================================================= */

static void test_vacuum_keeps_versions_for_active_txn(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    env_insert(&env, 1, 10);
    env_update(&env, 2, 10, 20, 0);
    env_update(&env, 3, 20, 30, 0);

    /* transaction 1 still active — it sees the root */
    env.mvcc->txn_status[1] = TXN_ACTIVE;

    Vacuum vac = make_vacuum();
    autoVacuumScan(&vac, VAC_TABLE, env.c, &env.buffors, NULL, env.mvcc);

    Block8kb *blk = env_block(&env, 1);
    for (int32_t k = 0; k < 3; k++) {
        assert_false(blk->tuples[k].header.t_infomask & (INFOMASK_UNUSED | INFOMASK_DEAD));
    }
    assert_int_equal(10, blk->tuples[0].dnb.data[0].val.i32);
    assert_int_equal(2, blk->header.dead_count);    /* still waiting */

    /* after transaction 1 ends, vacuum cleans up */
    env.mvcc->txn_status[1] = TXN_COMMITED;
    autoVacuumScan(&vac, VAC_TABLE, env.c, &env.buffors, NULL, env.mvcc);
    blk = env_block(&env, 1);
    assert_true(blk->tuples[1].header.t_infomask & INFOMASK_UNUSED);
    assert_int_equal(0, blk->header.dead_count);

    env_teardown(&env);
}

/* =========================================================================
 * 5. bq path: the bq counter is keyed by the root's block
 * ========================================================================= */

static void test_vacuum_bq_path(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    for (int32_t v = 0; v < MAX_TUPLES_PER_BLOCK; v++) env_insert(&env, 1, 1000 + v);
    env_update(&env, 2, 1000, 20, 1);   /* versions in block 2, root in block 1 */
    env_update(&env, 3, 20, 30, 1);

    bq_t *q = bq_mgr_get(&env.bq, VAC_TABLE);
    int32_t cnt = -1;
    assert_int_equal(BQ_OK, bq_get(q, 1, &cnt));
    assert_int_equal(2, cnt);
    assert_int_equal(BQ_ENOTFOUND, bq_get(q, 2, &cnt));

    Vacuum vac = make_vacuum();
    autoVacuumScan(&vac, VAC_TABLE, env.c, &env.buffors, &env.bq, env.mvcc);

    assert_int_equal(0, bq_size(q));
    assert_int_equal(0, vac.sumOfAllDeadTupleInsideTable);
    int32_t b, i;
    Tuple *last = env_follow(&env, env_tuple(&env, 1, 0), &b, &i);
    assert_int_equal(30, last->dnb.data[0].val.i32);
    assert_int_equal(1, env_count(&env, 10, 30));

    env_teardown(&env);
}

/* =========================================================================
 * 6. bq: a block that cannot be fully cleaned yet goes back to the queue with the remainder
 * ========================================================================= */

static void test_vacuum_bq_keeps_remaining(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    env_insert(&env, 1, 10);
    env_update(&env, 2, 10, 20, 1);
    env_update(&env, 3, 20, 30, 1);

    /* active transaction 2 — v1 (xmax 3) still needed, root (xmax 2) too */
    env.mvcc->txn_status[2] = TXN_ACTIVE;

    Vacuum vac = make_vacuum();
    autoVacuumScan(&vac, VAC_TABLE, env.c, &env.buffors, &env.bq, env.mvcc);

    bq_t *q = bq_mgr_get(&env.bq, VAC_TABLE);
    int32_t cnt = -1;
    assert_int_equal(BQ_OK, bq_get(q, 1, &cnt));
    assert_int_equal(2, cnt);        /* root (xmax 2) and v1 (xmax 3) wait for transaction 2 to end */

    env.mvcc->txn_status[2] = TXN_COMMITED;
    autoVacuumScan(&vac, VAC_TABLE, env.c, &env.buffors, &env.bq, env.mvcc);
    assert_int_equal(0, bq_size(q));

    env_teardown(&env);
}

/* =========================================================================
 * 7. Scan-time pruning (same block) leaves a consistent chain for vacuum
 * ========================================================================= */

static void test_scan_prune_then_vacuum(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    env_insert(&env, 1, 10);
    env_update(&env, 2, 10, 20, 0);
    env_update(&env, 3, 20, 30, 0);
    env_update(&env, 4, 30, 40, 0);

    /* the scan of txn 4's UPDATE removed v1 (predecessor root in the same block),
     * and v3 went into the freed slot — the block did not grow to 4 tuples */
    Block8kb *blk = env_block(&env, 1);
    assert_int_equal(3, blk->tuple_count);

    /* SELECT removes another dead version — the root points directly to the current one */
    assert_int_equal(1, env_count(&env, 10, 40));
    blk = env_block(&env, 1);
    int32_t unused = 0;
    for (int32_t k = 0; k < blk->tuple_count; k++)
        if (blk->tuples[k].header.t_infomask & INFOMASK_UNUSED) unused++;
    assert_int_equal(1, unused);
    int32_t b, i;
    Tuple *last = env_follow(&env, &blk->tuples[0], &b, &i);
    assert_int_equal(40, last->dnb.data[0].val.i32);
    assert_int_equal(3, blk->header.dead_count);    /* the counter is a hint — vacuum will recompute it */

    Vacuum vac = make_vacuum();
    autoVacuumScan(&vac, VAC_TABLE, env.c, &env.buffors, NULL, env.mvcc);

    blk = env_block(&env, 1);
    assert_true(blk->tuples[0].header.t_infomask & INFOMASK_DEAD);   /* root → redirect */
    assert_int_equal(0, blk->tuples[0].dnb.data_count);
    assert_int_equal(0, blk->header.dead_count);
    last = env_follow(&env, &blk->tuples[0], &b, &i);
    assert_int_equal(40, last->dnb.data[0].val.i32);
    assert_int_equal(1, env_count(&env, 10, 40));

    env_teardown(&env);
}

/* =========================================================================
 * 8. Many rows and repeated updates with vacuum — the data stays correct
 * ========================================================================= */

static void test_many_rows_update_vacuum_cycles(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    for (int32_t r = 0; r < 20; r++) env_insert(&env, 1, r * 100);

    int32_t xid = 2;
    Vacuum vac = make_vacuum();
    for (int32_t round = 1; round <= 5; round++) {
        for (int32_t r = 0; r < 20; r++) {
            env_update(&env, xid++, r * 100 + round - 1, r * 100 + round, 1);
        }
        autoVacuumScan(&vac, VAC_TABLE, env.c, &env.buffors, &env.bq, env.mvcc);
    }

    for (int32_t r = 0; r < 20; r++) {
        assert_int_equal(1, env_count(&env, 200, r * 100 + 5));
        assert_int_equal(0, env_count(&env, 200, r * 100 + 4));
    }
    /* slots are reused — the table does not grow by 100 versions */
    int32_t total = 0;
    for (int32_t b = 1; b <= env_endblock(&env); b++) total += env_block(&env, b)->tuple_count;
    assert_true(total <= 60);
    assert_int_equal(0, bq_size(bq_mgr_get(&env.bq, VAC_TABLE)));

    env_teardown(&env);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_vacuum_relinks_chain_same_block),
        cmocka_unit_test(test_insert_reuses_slot_after_vacuum),
        cmocka_unit_test(test_vacuum_relinks_chain_across_blocks),
        cmocka_unit_test(test_vacuum_keeps_versions_for_active_txn),
        cmocka_unit_test(test_vacuum_bq_path),
        cmocka_unit_test(test_vacuum_bq_keeps_remaining),
        cmocka_unit_test(test_scan_prune_then_vacuum),
        cmocka_unit_test(test_many_rows_update_vacuum_cycles),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}