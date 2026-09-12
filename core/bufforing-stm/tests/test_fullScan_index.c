/*
 * test_fullScan_index.c
 *
 * Tests for sql_fullScan using B-tree index vs sequential scan.
 * Verifies that index-based scan returns correct results and
 * skips blocks that don't contain matching values.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../bufforing-stm/sqlExecutor.h"
#include "../bufforing-stm/fsmMap.h"
#include "../bufforing-stm/mvcc.h"
#include "../bufforing-stm/transaction.h"
#include "../../memory-mgmt/memory-mgmt/all_var.h"
#include "../../memory-mgmt/memory-mgmt/block8kb.h"
#include "../../memory-mgmt/memory-mgmt/tuple.h"

/* =========================================================================
 * Infrastructure
 * ========================================================================= */

#define TEST_TABLE 1

typedef struct {
    Buffors       buffors;
    FSMCache     *c;
    FSMMapAll     fsmMapAll;
    MVCC         *mvcc;
    FSMMapBtree   fsmBtree;
    BtreeBuffors  btreeBuffors;
} TEnv;

static void tenv_setup(TEnv *env) {
    initializeBuffors(&env->buffors, 30);
    FSMCacheCreateC(&env->c);
    fsm_cache_set(env->c, TEST_TABLE);
    init_FSMMapAll(&env->fsmMapAll);
    addTableToFSMMapAll(&env->fsmMapAll, TEST_TABLE);
    create_MVCC(&env->mvcc);
    for (int i = 0; i < MAX_TRANSACTIONS; i++)
        env->mvcc->txn_status[i] = TXN_COMMITED;

    fsm_btree_init(&env->fsmBtree);
    initBtreeBuffors(&env->btreeBuffors, 100);
}

/* Setup with btree index on column colIdx */
static void tenv_setup_with_index(TEnv *env, int32_t colIdx) {
    tenv_setup(env);
    createBtree(&env->fsmBtree, TEST_TABLE, colIdx);
}

static void tenv_teardown(TEnv *env) {
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
}

static int32_t tenv_endblock(TEnv *env) {
    BlockCounterEntry *e = fsm_cache_get(env->c, TEST_TABLE);
    return e ? e->maxBlock : 0;
}

/* Insert tuple with 1 column (int32), also add to btree index on col 0 */
static void tenv_insert(TEnv *env, int32_t xmin, int32_t val) {
    AllVar vals[1] = {all_var_from_int32(val)};
    int8_t bm[1]  = {0};
    addTupleToOtherFunction(&env->buffors, env->c, &env->fsmMapAll, env->mvcc,
        TEST_TABLE, vals, 1, bm, 1,
        xmin, 0, 0, 0, 0, 0, -1, &env->fsmBtree, &env->btreeBuffors);
}

/* Insert tuple with 2 columns (int32, int32), also add to btree index */
static void tenv_insert2(TEnv *env, int32_t xmin, int32_t col0, int32_t col1) {
    AllVar vals[2] = {all_var_from_int32(col0), all_var_from_int32(col1)};
    int8_t bm[2]  = {0, 0};
    addTupleToOtherFunction(&env->buffors, env->c, &env->fsmMapAll, env->mvcc,
        TEST_TABLE, vals, 2, bm, 2,
        xmin, 0, 0, 0, 0, 0, -1, &env->fsmBtree, &env->btreeBuffors);
}

/* Insert tuple WITHOUT adding to btree (for comparison tests) */
static void tenv_insert_no_index(TEnv *env, int32_t xmin, int32_t val) {
    AllVar vals[1] = {all_var_from_int32(val)};
    int8_t bm[1]  = {0};
    addTupleToOtherFunction(&env->buffors, env->c, &env->fsmMapAll, env->mvcc,
        TEST_TABLE, vals, 1, bm, 1,
        xmin, 0, 0, 0, 0, 0, -1, NULL, NULL);
}

/* SELECT col0 with WHERE col0 == val, using index */
static ResultTuple tenv_select_eq_indexed(TEnv *env, int32_t txn_xid, int32_t val) {
    if (txn_xid >= 0 && txn_xid < MAX_TRANSACTIONS)
        env->mvcc->txn_status[txn_xid] = TXN_ACTIVE;
    Transaction txn = {.xid = txn_xid};
    SqlExecutor se  = {0};
    se.transaction  = &txn;
    se.tableId      = TEST_TABLE;
    se.endBlock     = tenv_endblock(env);
    se.fsmMapBtree  = &env->fsmBtree;
    se.btreeBuffors = &env->btreeBuffors;
    sql_addWhere(&se, 0, all_var_from_int32(val), SQL_EQ, 0);
    int32_t cols[]  = {0};
    sql_setSelect(&se, cols, 1);
    ResultTuple result = {0};
    sql_fullScan(&se, &result, &env->buffors, env->c, &env->fsmMapAll, env->mvcc);
    if (txn_xid >= 0 && txn_xid < MAX_TRANSACTIONS)
        env->mvcc->txn_status[txn_xid] = TXN_COMMITED;
    return result;
}

/* SELECT col0 without index (no fsmMapBtree set) */
static ResultTuple tenv_select_eq_seqscan(TEnv *env, int32_t txn_xid, int32_t val) {
    if (txn_xid >= 0 && txn_xid < MAX_TRANSACTIONS)
        env->mvcc->txn_status[txn_xid] = TXN_ACTIVE;
    Transaction txn = {.xid = txn_xid};
    SqlExecutor se  = {0};
    se.transaction  = &txn;
    se.tableId      = TEST_TABLE;
    se.endBlock     = tenv_endblock(env);
    /* no se.fsmMapBtree — forces sequential scan */
    sql_addWhere(&se, 0, all_var_from_int32(val), SQL_EQ, 0);
    int32_t cols[]  = {0};
    sql_setSelect(&se, cols, 1);
    ResultTuple result = {0};
    sql_fullScan(&se, &result, &env->buffors, env->c, &env->fsmMapAll, env->mvcc);
    if (txn_xid >= 0 && txn_xid < MAX_TRANSACTIONS)
        env->mvcc->txn_status[txn_xid] = TXN_COMMITED;
    return result;
}

/* SELECT all (no WHERE), using index env but no condition matches index path */
static ResultTuple tenv_select_all(TEnv *env, int32_t txn_xid) {
    if (txn_xid >= 0 && txn_xid < MAX_TRANSACTIONS)
        env->mvcc->txn_status[txn_xid] = TXN_ACTIVE;
    Transaction txn = {.xid = txn_xid};
    SqlExecutor se  = {0};
    se.transaction  = &txn;
    se.tableId      = TEST_TABLE;
    se.endBlock     = tenv_endblock(env);
    se.fsmMapBtree  = &env->fsmBtree;
    se.btreeBuffors = &env->btreeBuffors;
    int32_t cols[]  = {0};
    sql_setSelect(&se, cols, 1);
    ResultTuple result = {0};
    sql_fullScan(&se, &result, &env->buffors, env->c, &env->fsmMapAll, env->mvcc);
    if (txn_xid >= 0 && txn_xid < MAX_TRANSACTIONS)
        env->mvcc->txn_status[txn_xid] = TXN_COMMITED;
    return result;
}

static void free_result(ResultTuple *r) {
    for (int i = 0; i < r->tuple_count; i++)
        free(r->tuples[i]);
}

/* =========================================================================
 * 1. BASIC — index scan finds the right tuple
 * ========================================================================= */

static void test_index_select_single_match(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    tenv_insert(&env, 1, 42);
    tenv_insert(&env, 1, 99);
    tenv_insert(&env, 1, 77);

    ResultTuple r = tenv_select_eq_indexed(&env, 5, 42);
    assert_int_equal(r.tuple_count, 1);
    assert_int_equal(r.tuples[0]->dnb.data[0].val.i32, 42);
    free_result(&r);

    tenv_teardown(&env);
}

static void test_index_select_no_match(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    tenv_insert(&env, 1, 10);
    tenv_insert(&env, 1, 20);

    ResultTuple r = tenv_select_eq_indexed(&env, 5, 999);
    assert_int_equal(r.tuple_count, 0);

    tenv_teardown(&env);
}

static void test_index_select_multiple_matches(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    /* Same value in multiple tuples */
    tenv_insert(&env, 1, 42);
    tenv_insert(&env, 1, 42);
    tenv_insert(&env, 1, 42);
    tenv_insert(&env, 1, 99);

    ResultTuple r = tenv_select_eq_indexed(&env, 5, 42);
    assert_true(r.tuple_count >= 1);
    for (int i = 0; i < r.tuple_count; i++)
        assert_int_equal(r.tuples[i]->dnb.data[0].val.i32, 42);
    free_result(&r);

    tenv_teardown(&env);
}

/* =========================================================================
 * 2. INDEX vs SEQSCAN — same results
 * ========================================================================= */

static void test_index_vs_seqscan_same_results(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    tenv_insert(&env, 1, 10);
    tenv_insert(&env, 1, 20);
    tenv_insert(&env, 1, 30);
    tenv_insert(&env, 1, 20);
    tenv_insert(&env, 1, 40);

    ResultTuple ridx = tenv_select_eq_indexed(&env, 5, 20);
    ResultTuple rseq = tenv_select_eq_seqscan(&env, 6, 20);

    assert_int_equal(ridx.tuple_count, rseq.tuple_count);
    for (int i = 0; i < ridx.tuple_count; i++)
        assert_int_equal(ridx.tuples[i]->dnb.data[0].val.i32, 20);

    free_result(&ridx);
    free_result(&rseq);
    tenv_teardown(&env);
}

static void test_no_match_index_vs_seqscan(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    tenv_insert(&env, 1, 10);
    tenv_insert(&env, 1, 20);

    ResultTuple ridx = tenv_select_eq_indexed(&env, 5, 999);
    ResultTuple rseq = tenv_select_eq_seqscan(&env, 6, 999);

    assert_int_equal(ridx.tuple_count, 0);
    assert_int_equal(rseq.tuple_count, 0);

    tenv_teardown(&env);
}

/* =========================================================================
 * 3. NO WHERE — falls back to seqscan even with index
 * ========================================================================= */

static void test_no_where_returns_all(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    tenv_insert(&env, 1, 10);
    tenv_insert(&env, 1, 20);
    tenv_insert(&env, 1, 30);

    ResultTuple r = tenv_select_all(&env, 5);
    assert_int_equal(r.tuple_count, 3);
    free_result(&r);

    tenv_teardown(&env);
}

/* =========================================================================
 * 4. MANY VALUES — stress test with index
 * ========================================================================= */

static void test_index_many_unique_values(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    for (int i = 0; i < 50; i++)
        tenv_insert(&env, 1, i * 10);

    /* Each value should be findable */
    for (int i = 0; i < 50; i++) {
        ResultTuple r = tenv_select_eq_indexed(&env, 5, i * 10);
        assert_int_equal(r.tuple_count, 1);
        assert_int_equal(r.tuples[0]->dnb.data[0].val.i32, i * 10);
        free_result(&r);
    }

    /* Non-existing value */
    ResultTuple r = tenv_select_eq_indexed(&env, 5, 9999);
    assert_int_equal(r.tuple_count, 0);

    tenv_teardown(&env);
}

static void test_index_many_duplicates(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    /* 20 tuples with val=42, 10 with val=99 */
    for (int i = 0; i < 20; i++)
        tenv_insert(&env, 1, 42);
    for (int i = 0; i < 10; i++)
        tenv_insert(&env, 1, 99);

    ResultTuple r42 = tenv_select_eq_indexed(&env, 5, 42);
    /* RESULT_SPACE=10 limits max results */
    assert_int_equal(r42.tuple_count, RESULT_SPACE);
    for (int i = 0; i < r42.tuple_count; i++)
        assert_int_equal(r42.tuples[i]->dnb.data[0].val.i32, 42);
    free_result(&r42);

    ResultTuple r99 = tenv_select_eq_indexed(&env, 5, 99);
    assert_int_equal(r99.tuple_count, 10);
    for (int i = 0; i < r99.tuple_count; i++)
        assert_int_equal(r99.tuples[i]->dnb.data[0].val.i32, 99);
    free_result(&r99);

    tenv_teardown(&env);
}

/* =========================================================================
 * 5. MVCC VISIBILITY with index
 * ========================================================================= */

static void test_index_invisible_future_xmin(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    tenv_insert(&env, 10, 42); /* xmin=10 */

    /* txn 5 (xid < xmin=10): should not see it */
    ResultTuple r = tenv_select_eq_indexed(&env, 5, 42);
    assert_int_equal(r.tuple_count, 0);

    /* txn 15 (xid >= xmin=10): should see it */
    ResultTuple r2 = tenv_select_eq_indexed(&env, 15, 42);
    assert_int_equal(r2.tuple_count, 1);
    assert_int_equal(r2.tuples[0]->dnb.data[0].val.i32, 42);
    free_result(&r2);

    tenv_teardown(&env);
}

static void test_index_deleted_tuple_invisible(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    tenv_insert(&env, 1, 42);

    /* Manually set xmax to simulate deletion by txn 5 */
    for (int i = 0; i < env.buffors.count; i++) {
        if (env.buffors.buffors[i].isUsed && env.buffors.buffors[i].universalBlock &&
            env.buffors.buffors[i].universalBlock->block) {
            Block8kb *blk = env.buffors.buffors[i].universalBlock->block;
            for (int j = 0; j < blk->tuple_count; j++) {
                if (blk->tuples[j].dnb.data[0].val.i32 == 42) {
                    blk->tuples[j].header.t_xmax = 5;
                }
            }
        }
    }

    /* txn 10 (xid > xmax=5): should not see deleted tuple */
    ResultTuple r = tenv_select_eq_indexed(&env, 10, 42);
    assert_int_equal(r.tuple_count, 0);

    /* txn 3 (xid < xmax=5): should still see it (before delete) */
    ResultTuple r2 = tenv_select_eq_indexed(&env, 3, 42);
    assert_int_equal(r2.tuple_count, 1);
    free_result(&r2);

    tenv_teardown(&env);
}

/* =========================================================================
 * 6. MIXED — some values indexed, query non-indexed column falls to seqscan
 * ========================================================================= */

static void test_index_on_col0_query_col1_seqscan(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0); /* index on col0 */

    tenv_insert2(&env, 1, 10, 100);
    tenv_insert2(&env, 1, 20, 200);
    tenv_insert2(&env, 1, 30, 100);

    /* Query WHERE col1==100 — col1 has no index, should fallback to seqscan */
    Transaction txn = {.xid = 5};
    SqlExecutor se  = {0};
    se.transaction  = &txn;
    se.tableId      = TEST_TABLE;
    se.endBlock     = tenv_endblock(&env);
    se.fsmMapBtree  = &env.fsmBtree;
    se.btreeBuffors = &env.btreeBuffors;
    sql_addWhere(&se, 1, all_var_from_int32(100), SQL_EQ, 0);
    int32_t cols[] = {0, 1};
    sql_setSelect(&se, cols, 2);
    ResultTuple result = {0};
    env.mvcc->txn_status[5] = TXN_ACTIVE;
    sql_fullScan(&se, &result, &env.buffors, env.c, &env.fsmMapAll, env.mvcc);
    env.mvcc->txn_status[5] = TXN_COMMITED;

    assert_int_equal(result.tuple_count, 2);
    /* Both results should have col1==100 */
    assert_int_equal(result.tuples[0]->dnb.data[1].val.i32, 100);
    assert_int_equal(result.tuples[1]->dnb.data[1].val.i32, 100);
    free_result(&result);

    tenv_teardown(&env);
}

/* =========================================================================
 * 7. EMPTY TABLE
 * ========================================================================= */

static void test_index_empty_table(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    ResultTuple r = tenv_select_eq_indexed(&env, 5, 42);
    assert_int_equal(r.tuple_count, 0);

    tenv_teardown(&env);
}

/* =========================================================================
 * 8. MANY BLOCKS — values spread across blocks, index narrows scan
 * ========================================================================= */

static void test_index_cross_block_search(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    /* Insert enough to span multiple blocks */
    for (int i = 0; i < 100; i++)
        tenv_insert(&env, 1, i);

    /* Verify we have multiple blocks */
    assert_true(tenv_endblock(&env) >= 1);

    /* Search for specific values */
    ResultTuple r0 = tenv_select_eq_indexed(&env, 5, 0);
    assert_int_equal(r0.tuple_count, 1);
    assert_int_equal(r0.tuples[0]->dnb.data[0].val.i32, 0);
    free_result(&r0);

    ResultTuple r50 = tenv_select_eq_indexed(&env, 5, 50);
    assert_int_equal(r50.tuple_count, 1);
    assert_int_equal(r50.tuples[0]->dnb.data[0].val.i32, 50);
    free_result(&r50);

    ResultTuple r99 = tenv_select_eq_indexed(&env, 5, 99);
    assert_int_equal(r99.tuple_count, 1);
    assert_int_equal(r99.tuples[0]->dnb.data[0].val.i32, 99);
    free_result(&r99);

    /* Non-existing */
    ResultTuple rn = tenv_select_eq_indexed(&env, 5, 200);
    assert_int_equal(rn.tuple_count, 0);

    tenv_teardown(&env);
}

/* =========================================================================
 * 9. INDEX + UPDATE — after update, index should still find new value
 * ========================================================================= */

static void test_index_after_update_finds_new_value(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    tenv_insert(&env, 1, 100);
    tenv_insert(&env, 1, 200);

    /* UPDATE val=100 -> 999, by txn 5 */
    Transaction txn = {.xid = 5};
    SqlExecutor se  = {0};
    se.transaction  = &txn;
    se.tableId      = TEST_TABLE;
    se.endBlock     = tenv_endblock(&env);
    se.fsmMapBtree  = &env.fsmBtree;
    se.btreeBuffors = &env.btreeBuffors;
    sql_addWhere(&se, 0, all_var_from_int32(100), SQL_EQ, 0);
    int32_t upd_cols[] = {0};
    AllVar upd_vals[]  = {all_var_from_int32(999)};
    sql_setUpdate(&se, upd_cols, upd_vals, 1);
    ResultTuple uresult = {0};
    sql_fullScan(&se, &uresult, &env.buffors, env.c, &env.fsmMapAll, env.mvcc);

    /* After update, txn 10 should see val=999 and val=200 */
    ResultTuple r999 = tenv_select_eq_indexed(&env, 10, 999);
    assert_true(r999.tuple_count >= 1);
    assert_int_equal(r999.tuples[0]->dnb.data[0].val.i32, 999);
    free_result(&r999);

    /* val=200 should still be there */
    ResultTuple r200 = tenv_select_eq_indexed(&env, 10, 200);
    assert_int_equal(r200.tuple_count, 1);
    assert_int_equal(r200.tuples[0]->dnb.data[0].val.i32, 200);
    free_result(&r200);

    tenv_teardown(&env);
}

/* =========================================================================
 * 10. STRESS — many inserts, random queries
 * ========================================================================= */

static void test_index_stress_insert_and_query(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    /* Insert 10 tuples with unique values (fits in RESULT_SPACE) */
    for (int i = 0; i < 10; i++)
        tenv_insert(&env, 1, i * 100);

    /* Each value should be findable via index */
    for (int i = 0; i < 10; i++) {
        ResultTuple r = tenv_select_eq_indexed(&env, 5, i * 100);
        assert_int_equal(r.tuple_count, 1);
        assert_int_equal(r.tuples[0]->dnb.data[0].val.i32, i * 100);
        free_result(&r);
    }

    /* Non-existing should return 0 */
    ResultTuple rn = tenv_select_eq_indexed(&env, 5, 9999);
    assert_int_equal(rn.tuple_count, 0);

    tenv_teardown(&env);
}

static void test_index_stress_all_same_value(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    /* 10 tuples all with val=42 (RESULT_SPACE=10) */
    for (int i = 0; i < 10; i++)
        tenv_insert(&env, 1, 42);

    ResultTuple r = tenv_select_eq_indexed(&env, 5, 42);
    assert_int_equal(r.tuple_count, 10);
    for (int i = 0; i < r.tuple_count; i++)
        assert_int_equal(r.tuples[i]->dnb.data[0].val.i32, 42);
    free_result(&r);

    /* val=99 should return 0 */
    ResultTuple rn = tenv_select_eq_indexed(&env, 5, 99);
    assert_int_equal(rn.tuple_count, 0);

    tenv_teardown(&env);
}

/* =========================================================================
 * 11. CORRECTNESS — index scan + seqscan return same counts for many values
 * ========================================================================= */

static void test_index_vs_seqscan_many_values(void **state) {
    (void)state;
    TEnv env;
    tenv_setup_with_index(&env, 0);

    /* Insert pattern: val=1 x5, val=2 x3, val=3 x7, val=4 x1 */
    for (int i = 0; i < 5; i++) tenv_insert(&env, 1, 1);
    for (int i = 0; i < 3; i++) tenv_insert(&env, 1, 2);
    for (int i = 0; i < 7; i++) tenv_insert(&env, 1, 3);
    tenv_insert(&env, 1, 4);

    int expected[] = {5, 3, 7, 1};
    for (int v = 1; v <= 4; v++) {
        ResultTuple ridx = tenv_select_eq_indexed(&env, 5, v);
        ResultTuple rseq = tenv_select_eq_seqscan(&env, 6, v);
        assert_int_equal(ridx.tuple_count, expected[v-1]);
        assert_int_equal(rseq.tuple_count, expected[v-1]);
        free_result(&ridx);
        free_result(&rseq);
    }

    tenv_teardown(&env);
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void) {
    const struct CMUnitTest tests[] = {
        /* 1. Basic */
        cmocka_unit_test(test_index_select_single_match),
        cmocka_unit_test(test_index_select_no_match),
        cmocka_unit_test(test_index_select_multiple_matches),

        /* 2. Index vs SeqScan */
        cmocka_unit_test(test_index_vs_seqscan_same_results),
        cmocka_unit_test(test_no_match_index_vs_seqscan),

        /* 3. No WHERE */
        cmocka_unit_test(test_no_where_returns_all),

        /* 4. Many values */
        cmocka_unit_test(test_index_many_unique_values),
        cmocka_unit_test(test_index_many_duplicates),

        /* 5. MVCC */
        cmocka_unit_test(test_index_invisible_future_xmin),
        cmocka_unit_test(test_index_deleted_tuple_invisible),

        /* 6. Mixed columns */
        cmocka_unit_test(test_index_on_col0_query_col1_seqscan),

        /* 7. Empty */
        cmocka_unit_test(test_index_empty_table),

        /* 8. Cross-block */
        cmocka_unit_test(test_index_cross_block_search),

        /* 9. Update */
        cmocka_unit_test(test_index_after_update_finds_new_value),

        /* 10. Stress */
        cmocka_unit_test(test_index_stress_insert_and_query),
        cmocka_unit_test(test_index_stress_all_same_value),

        /* 11. Correctness */
        cmocka_unit_test(test_index_vs_seqscan_many_values),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
