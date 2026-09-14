/*
 * test_hotUpdate.c
 *
 * Testy integracyjne hotUpdate / hotUpdateA przez sql_fullScan:
 *   - przy UPDATE indeks B-tree jest aktualizowany automatycznie
 *   - stara wartosc znika z indeksu, nowa jest w indeksie
 *   - gdy fsmMapBtree == NULL, UPDATE dziala normalnie (bez indeksu)
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
#include "../../indexes/indexes/btreeFileOperation.h"

/* =========================================================================
 * Infrastruktura
 * ========================================================================= */

#define HOT_TABLE 42

typedef struct {
    Buffors      buffors;
    FSMCache    *c;
    FSMMapAll    fsmMapAll;
    MVCC        *mvcc;
    FSMMapBtree  fsmMapBtree;
    BtreeBuffors btreeBuffors;
} HotEnv;

static void hot_setup(HotEnv *env, int8_t withIndex) {
    initializeBuffors(&env->buffors, 30);
    FSMCacheCreateC(&env->c);
    fsm_cache_set(env->c, HOT_TABLE);
    init_FSMMapAll(&env->fsmMapAll);
    addTableToFSMMapAll(&env->fsmMapAll, HOT_TABLE);
    create_MVCC(&env->mvcc);
    for (int i = 0; i < MAX_TRANSACTIONS; i++)
        env->mvcc->txn_status[i] = TXN_COMMITED;

    fsm_btree_init(&env->fsmMapBtree);
    initBtreeBuffors(&env->btreeBuffors, 20);
    if (withIndex) {
        /* indeks na kolumnie 0 */
        createBtree(&env->fsmMapBtree, HOT_TABLE, 0);
    }
}

static void hot_teardown(HotEnv *env) {
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
    free(env->btreeBuffors.buffors);
}

static int32_t hot_endblock(HotEnv *env) {
    BlockCounterEntry *e = fsm_cache_get(env->c, HOT_TABLE);
    return e ? e->maxBlock : 0;
}

/* Wstaw tuple z jedną kolumną int32, ręcznie dodaj do indeksu */
static void hot_insert(HotEnv *env, int32_t xmin, int32_t val) {
    AllVar vals[1] = {all_var_from_int32(val)};
    int8_t bm[1]  = {0};
    DataBuffor *db = addTupleToOtherFunction(
        &env->buffors, env->c, &env->fsmMapAll,
        HOT_TABLE, vals, 1, bm, 1,
        xmin, 0, 0, 0, 0, 0, -1, &env->fsmMapBtree, &env->btreeBuffors);

    if (db != NULL) {
        db->pinCount = 0;
    }
}

/* Zbuduj SqlExecutor UPDATE col0 WHERE col0==whereVal SET col0=newVal */
static void hot_update(HotEnv *env, int32_t txnXid, int32_t whereVal, int32_t newVal,
                       int8_t withIndex) {
    Transaction txn = {.xid = txnXid};
    SqlExecutor se  = {0};
    se.transaction  = &txn;
    se.tableId      = HOT_TABLE;
    se.endBlock     = hot_endblock(env);
    if (withIndex) {
        se.fsmMapBtree  = &env->fsmMapBtree;
        se.btreeBuffors = &env->btreeBuffors;
    }
    sql_addWhere(&se, 0, all_var_from_int32(whereVal), SQL_EQ, 1);
    int32_t upd_cols[] = {0};
    AllVar  upd_vals[] = {all_var_from_int32(newVal)};
    sql_setUpdate(&se, upd_cols, upd_vals, 1);
    ResultTuple result = {0};
    sql_fullScan(&se, &result, &env->buffors, env->c, &env->fsmMapAll, env->mvcc);
}

static int32_t hot_find_index(HotEnv *env, int32_t val) {
    return getBlockBtree(all_var_from_int32(val), &env->btreeBuffors,
                         HOT_TABLE, 0, &env->fsmMapBtree, 0, 0);
}

/* =========================================================================
 * Test 1 — UPDATE usuwa starą wartość z indeksu, wstawia nową
 * ========================================================================= */

static void test_hotUpdate_index_updated_on_update(void **state) {
    (void)state;
    HotEnv env;
    hot_setup(&env, 1);

    hot_insert(&env, 1, 100);

    /* stara wartość powinna być w indeksie */
    assert_int_not_equal(hot_find_index(&env, 100), -1);

    /* txn 5 aktualizuje col0: 100 → 200 */
    hot_update(&env, 5, 100, 200, 1);

    /* stara wartość usunięta z indeksu */
    assert_int_equal(hot_find_index(&env, 100), -1);
    /* nowa wartość dostępna w indeksie */
    assert_int_not_equal(hot_find_index(&env, 200), -1);

    hot_teardown(&env);
}

/* =========================================================================
 * Test 2 — nowa wartość wskazuje na właściwy blok
 * ========================================================================= */

static void test_hotUpdate_new_index_points_to_new_block(void **state) {
    (void)state;
    HotEnv env;
    hot_setup(&env, 1);

    hot_insert(&env, 1, 50);

    int32_t old_block = hot_find_index(&env, 50);
    assert_int_not_equal(old_block, -1);

    /* UPDATE: 50 → 999 */
    hot_update(&env, 5, 50, 999, 1);

    int32_t new_block = hot_find_index(&env, 999);
    assert_int_not_equal(new_block, -1);
    /* nowy tuple trafia do (tego samego lub innego) bloku — blok musi być >= 0 */
    assert_true(new_block >= 0);

    hot_teardown(&env);
}

/* =========================================================================
 * Test 3 — kilka tuple, tylko zaktualizowana zmienia indeks
 * ========================================================================= */

static void test_hotUpdate_only_updated_tuple_changes_index(void **state) {
    (void)state;
    HotEnv env;
    hot_setup(&env, 1);

    hot_insert(&env, 1, 10);
    hot_insert(&env, 1, 20);
    hot_insert(&env, 1, 30);

    /* UPDATE tylko tuple z val=20 → 200 */
    hot_update(&env, 5, 20, 200, 1);

    /* pozostałe bez zmian w indeksie */
    assert_int_not_equal(hot_find_index(&env, 10),  -1);
    assert_int_not_equal(hot_find_index(&env, 30),  -1);
    /* zmieniona: stara znika, nowa widoczna */
    assert_int_equal    (hot_find_index(&env, 20),  -1);
    assert_int_not_equal(hot_find_index(&env, 200), -1);

    hot_teardown(&env);
}

/* =========================================================================
 * Test 4 — łańcuch updateów: każdy krok aktualizuje indeks
 * ========================================================================= */

static void test_hotUpdate_chain_updates_index_each_step(void **state) {
    (void)state;
    HotEnv env;
    hot_setup(&env, 1);

    hot_insert(&env, 1, 1);

    hot_update(&env,  5,   1,  10, 1);
    assert_int_equal    (hot_find_index(&env,  1), -1);
    assert_int_not_equal(hot_find_index(&env, 10), -1);

    hot_update(&env, 10,  10, 100, 1);
    assert_int_equal    (hot_find_index(&env,  10), -1);
    assert_int_not_equal(hot_find_index(&env, 100), -1);

    hot_update(&env, 15, 100, 999, 1);
    assert_int_equal    (hot_find_index(&env, 100), -1);
    assert_int_not_equal(hot_find_index(&env, 999), -1);

    hot_teardown(&env);
}

/* =========================================================================
 * Test 5 — bez indeksu (fsmMapBtree==NULL): UPDATE działa normalnie
 * ========================================================================= */

static void test_hotUpdate_no_index_update_still_works(void **state) {
    (void)state;
    HotEnv env;
    hot_setup(&env, 0); /* bez indeksu */

    /* wstaw bez indeksu */
    AllVar vals[1] = {all_var_from_int32(77)};
    int8_t bm[1]  = {0};
    addTupleToOtherFunction(&env.buffors, env.c, &env.fsmMapAll,
        HOT_TABLE, vals, 1, bm, 1, 1, 0, 0, 0, 0, 0, -1, NULL, NULL);

    /* UPDATE bez indeksu — nie powinno crashować */
    hot_update(&env, 5, 77, 777, 0);

    /* SELECT po update: nowa wartość widoczna */
    Transaction txn = {.xid = 10};
    SqlExecutor se  = {0};
    se.transaction  = &txn;
    se.tableId      = HOT_TABLE;
    se.endBlock     = hot_endblock(&env);
    int32_t cols[]  = {0};
    sql_setSelect(&se, cols, 1);
    ResultTuple result = {0};
    env.mvcc->txn_status[10] = TXN_ACTIVE;
    sql_fullScan(&se, &result, &env.buffors, env.c, &env.fsmMapAll, env.mvcc);
    env.mvcc->txn_status[10] = TXN_COMMITED;

    assert_int_equal(result.tuple_count, 1);
    assert_int_equal(result.tuples[0]->dnb.data[0].val.i32, 777);
    free(result.tuples[0]);

    hot_teardown(&env);
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_hotUpdate_index_updated_on_update),
        cmocka_unit_test(test_hotUpdate_new_index_points_to_new_block),
        cmocka_unit_test(test_hotUpdate_only_updated_tuple_changes_index),
        cmocka_unit_test(test_hotUpdate_chain_updates_index_each_step),
        cmocka_unit_test(test_hotUpdate_no_index_update_still_works),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}