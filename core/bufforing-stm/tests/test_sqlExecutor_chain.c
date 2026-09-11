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

#define CHAIN_TABLE 1

typedef struct {
    Buffors    buffors;
    FSMCache  *c;
    FSMMapAll  fsmMapAll;
    MVCC      *mvcc;
} TestEnv;

static void env_setup(TestEnv *env) {
    initializeBuffors(&env->buffors, 30);
    FSMCacheCreateC(&env->c);
    fsm_cache_set(env->c, CHAIN_TABLE);
    init_FSMMapAll(&env->fsmMapAll);
    addTableToFSMMapAll(&env->fsmMapAll, CHAIN_TABLE);
    create_MVCC(&env->mvcc);
    /* domyślnie wszystkie sloty = TXN_COMMITED — brak aktywnych transakcji */
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        env->mvcc->txn_status[i] = TXN_COMMITED;
    }
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
}

/* Oznacz snapshot jako aktywny (przed updateami) — vacuum go nie tknie */
static void env_hold_snapshot(TestEnv *env, int32_t xid) {
    if (xid >= 0 && xid < MAX_TRANSACTIONS)
        env->mvcc->txn_status[xid] = TXN_ACTIVE;
}

/* Zwolnij snapshot (po odczycie) */
static void env_release_snapshot(TestEnv *env, int32_t xid) {
    if (xid >= 0 && xid < MAX_TRANSACTIONS)
        env->mvcc->txn_status[xid] = TXN_COMMITED;
}

static int32_t env_endblock(TestEnv *env) {
    BlockCounterEntry *e = fsm_cache_get(env->c, CHAIN_TABLE);
    return e ? e->maxBlock : 0;
}

/* Insert one tuple with explicit xmin and one int32 column */
static void env_insert(TestEnv *env, int32_t xmin, int32_t val) {
    AllVar vals[1] = {all_var_from_int32(val)};
    int8_t bm[1]  = {0};
    addTupleToOtherFunction(&env->buffors, env->c, &env->fsmMapAll, env->mvcc,
        CHAIN_TABLE, vals, 1, bm, 1,
        xmin, 0, 0, 0, 0, 0, -1);
}

/* INSERT with 2 columns: int32 id + int32 value */
static void env_insert2(TestEnv *env, int32_t xmin, int32_t id, int32_t val) {
    AllVar vals[2] = {all_var_from_int32(id), all_var_from_int32(val)};
    int8_t bm[2]  = {0, 0};
    addTupleToOtherFunction(&env->buffors, env->c, &env->fsmMapAll, env->mvcc,
        CHAIN_TABLE, vals, 2, bm, 2,
        xmin, 0, 0, 0, 0, 0, -1);
}

/* UPDATE col0 WHERE col0 == where_val, SET col0 = new_val, by transaction xid */
static void env_update(TestEnv *env, int32_t txn_xid, int32_t where_val, int32_t new_val) {
    Transaction txn = {.xid = txn_xid};
    SqlExecutor se  = {0};
    se.transaction  = &txn;
    se.tableId      = CHAIN_TABLE;
    se.endBlock     = env_endblock(env);
    sql_addWhere(&se, 0, all_var_from_int32(where_val), SQL_EQ, 1);
    int32_t upd_cols[] = {0};
    AllVar  upd_vals[] = {all_var_from_int32(new_val)};
    sql_setUpdate(&se, upd_cols, upd_vals, 1);
    ResultTuple result = {0};
    sql_fullScan(&se, &result, &env->buffors, env->c, &env->fsmMapAll, env->mvcc);
}

/* UPDATE col1 (value) WHERE col0 (id) == id_val, by transaction xid (2-column variant) */
static void env_update_by_id(TestEnv *env, int32_t txn_xid, int32_t id_val, int32_t new_val) {
    Transaction txn = {.xid = txn_xid};
    SqlExecutor se  = {0};
    se.transaction  = &txn;
    se.tableId      = CHAIN_TABLE;
    se.endBlock     = env_endblock(env);
    sql_addWhere(&se, 0, all_var_from_int32(id_val), SQL_EQ, 1);
    int32_t upd_cols[] = {1};
    AllVar  upd_vals[] = {all_var_from_int32(new_val)};
    sql_setUpdate(&se, upd_cols, upd_vals, 1);
    ResultTuple result = {0};
    sql_fullScan(&se, &result, &env->buffors, env->c, &env->fsmMapAll, env->mvcc);
}

/* SELECT col0 for all tuples, for given transaction xid */
static ResultTuple env_select(TestEnv *env, int32_t txn_xid) {
    if (txn_xid >= 0 && txn_xid < MAX_TRANSACTIONS)
        env->mvcc->txn_status[txn_xid] = TXN_ACTIVE;
    Transaction txn = {.xid = txn_xid};
    SqlExecutor se  = {0};
    se.transaction  = &txn;
    se.tableId      = CHAIN_TABLE;
    se.endBlock     = env_endblock(env);
    int32_t cols[]  = {0};
    sql_setSelect(&se, cols, 1);
    ResultTuple result = {0};
    sql_fullScan(&se, &result, &env->buffors, env->c, &env->fsmMapAll, env->mvcc);
    if (txn_xid >= 0 && txn_xid < MAX_TRANSACTIONS)
        env->mvcc->txn_status[txn_xid] = TXN_COMMITED;
    return result;
}

/* SELECT col1 WHERE col0==id_val, for given transaction xid (2-column) */
static ResultTuple env_select_by_id(TestEnv *env, int32_t txn_xid, int32_t id_val) {
    if (txn_xid >= 0 && txn_xid < MAX_TRANSACTIONS)
        env->mvcc->txn_status[txn_xid] = TXN_ACTIVE;
    Transaction txn = {.xid = txn_xid};
    SqlExecutor se  = {0};
    se.transaction  = &txn;
    se.tableId      = CHAIN_TABLE;
    se.endBlock     = env_endblock(env);
    sql_addWhere(&se, 0, all_var_from_int32(id_val), SQL_EQ, 1);
    int32_t cols[]  = {1};
    sql_setSelect(&se, cols, 1);
    ResultTuple result = {0};
    sql_fullScan(&se, &result, &env->buffors, env->c, &env->fsmMapAll, env->mvcc);
    if (txn_xid >= 0 && txn_xid < MAX_TRANSACTIONS)
        env->mvcc->txn_status[txn_xid] = TXN_COMMITED;
    return result;
}

/* =========================================================================
 * Test 1 — Pojedynczy UPDATE, dwa snapshoty RR
 *   INSERT (xmin=1, val=100)
 *   txn 5: UPDATE val=100 → 200
 *   txn 2 (xid=2, RR):  widzi val=100  (stara wersja)
 *   txn 7 (xid=7, RR):  widzi val=200  (nowa wersja)
 * ========================================================================= */

static void test_chain_single_update_rr_two_snapshots(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    env_hold_snapshot(&env, 2);    /* txn 2 startuje przed updateem */
    env_hold_snapshot(&env, 7);    /* txn 7 startuje przed updateem */

    env_insert(&env, 1, 100);      /* v1: xmin=1, val=100 */
    env_update(&env, 5, 100, 200); /* txn 5: 100 → 200 */

    /* txn 2 started before update — widzi starą wersję */
    ResultTuple r2 = env_select(&env, 2);
    assert_int_equal(r2.tuple_count, 1);
    assert_int_equal(r2.tuples[0]->dnb.data[0].val.i32, 100);
    free(r2.tuples[0]);

    /* txn 7 started after update — widzi nową wersję */
    ResultTuple r7 = env_select(&env, 7);
    assert_int_equal(r7.tuple_count, 1);
    assert_int_equal(r7.tuples[0]->dnb.data[0].val.i32, 200);
    free(r7.tuples[0]);

    env_teardown(&env);
}

/* =========================================================================
 * Test 2 — Trzy UPDATE, cztery wersje, cztery snapshoty RR
 *   INSERT (xmin=1, val=10)
 *   txn 5:  10 → 20
 *   txn 10: 20 → 30
 *   txn 15: 30 → 40
 *   Łańcuch: v1 → v2 → v3 → v4
 *
 *   txn 3:  widzi val=10
 *   txn 7:  widzi val=20
 *   txn 12: widzi val=30
 *   txn 20: widzi val=40
 * ========================================================================= */

static void test_chain_triple_update_rr_versioning(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    env_hold_snapshot(&env,  3);
    env_hold_snapshot(&env,  7);
    env_hold_snapshot(&env, 12);
    env_hold_snapshot(&env, 20);

    env_insert(&env,  1, 10);
    env_update(&env,  5, 10, 20);
    env_update(&env, 10, 20, 30);
    env_update(&env, 15, 30, 40);

    ResultTuple r3  = env_select(&env, 3);
    assert_int_equal(r3.tuple_count,  1);
    assert_int_equal(r3.tuples[0]->dnb.data[0].val.i32,  10);
    free(r3.tuples[0]);

    ResultTuple r7  = env_select(&env, 7);
    assert_int_equal(r7.tuple_count,  1);
    assert_int_equal(r7.tuples[0]->dnb.data[0].val.i32,  20);
    free(r7.tuples[0]);

    ResultTuple r12 = env_select(&env, 12);
    assert_int_equal(r12.tuple_count, 1);
    assert_int_equal(r12.tuples[0]->dnb.data[0].val.i32, 30);
    free(r12.tuples[0]);

    ResultTuple r20 = env_select(&env, 20);
    assert_int_equal(r20.tuple_count, 1);
    assert_int_equal(r20.tuples[0]->dnb.data[0].val.i32, 40);
    free(r20.tuples[0]);

    env_teardown(&env);
}

/* =========================================================================
 * Test 3 — Wiele tuple, częściowy UPDATE, RR
 *   Wstaw 8 tuple (val=10..80 co 10)
 *   txn 5: update val=20 → 200,  val=40 → 400,  val=60 → 600
 *   txn 2 (przed update): widzi oryginalne wartości dla wszystkich
 *   txn 7 (po  update): widzi 200,400,600 dla zmienionych; 10,30,50,70,80 niezmienione
 * ========================================================================= */

static void test_chain_many_tuples_partial_update_rr(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    env_hold_snapshot(&env, 2);   /* widzi oryginały */
    env_hold_snapshot(&env, 7);   /* widzi po updateach */

    /* INSERT 8 tuple (val = 10,20,30,40,50,60,70,80), xmin=1 */
    for (int v = 10; v <= 80; v += 10) {
        env_insert(&env, 1, v);
    }

    /* txn 5: zaktualizuj trzy z nich */
    env_update(&env, 5, 20, 200);
    env_update(&env, 5, 40, 400);
    env_update(&env, 5, 60, 600);

    /* txn 2: widzi oryginalne 8 wartości */
    ResultTuple r2 = env_select(&env, 2);
    assert_int_equal(r2.tuple_count, 8);
    int sum2 = 0;
    for (int i = 0; i < r2.tuple_count; i++) {
        sum2 += r2.tuples[i]->dnb.data[0].val.i32;
        free(r2.tuples[i]);
    }
    /* suma 10+20+30+40+50+60+70+80 = 360 */
    assert_int_equal(sum2, 360);

    /* txn 7: 5 oryginalnych (10,30,50,70,80) + 3 zaktualizowane (200,400,600) */
    ResultTuple r7 = env_select(&env, 7);
    assert_int_equal(r7.tuple_count, 8);
    int sum7 = 0;
    for (int i = 0; i < r7.tuple_count; i++) {
        sum7 += r7.tuples[i]->dnb.data[0].val.i32;
        free(r7.tuples[i]);
    }
    /* 10+30+50+70+80 + 200+400+600 = 1440 */
    assert_int_equal(sum7, 1440);

    env_teardown(&env);
}

/* =========================================================================
 * Test 4 — Wiele niezależnych łańcuchów, różne transakcje widzą różne wersje
 *   3 różne tuple (id=1,2,3), każda updatowana przez inną transakcję
 *   txn 5  → id=1: val 100→500
 *   txn 10 → id=2: val 200→2000
 *   txn 20 → id=3: val 300→3000
 *
 *   txn 3:  widzi 100,200,300
 *   txn 7:  widzi 500,200,300
 *   txn 15: widzi 500,2000,300
 *   txn 25: widzi 500,2000,3000
 * ========================================================================= */

static void test_chain_independent_chains_snapshot_isolation(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    env_hold_snapshot(&env,  3);
    env_hold_snapshot(&env,  7);
    env_hold_snapshot(&env, 15);
    env_hold_snapshot(&env, 25);

    env_insert2(&env, 1, 1, 100);
    env_insert2(&env, 1, 2, 200);
    env_insert2(&env, 1, 3, 300);

    env_update_by_id(&env,  5, 1, 500);
    env_update_by_id(&env, 10, 2, 2000);
    env_update_by_id(&env, 20, 3, 3000);

    /* txn 3: przed wszystkimi updates */
    ResultTuple r3_1 = env_select_by_id(&env, 3, 1);
    assert_int_equal(r3_1.tuple_count, 1);
    assert_int_equal(r3_1.tuples[0]->dnb.data[0].val.i32, 100);
    free(r3_1.tuples[0]);

    ResultTuple r3_2 = env_select_by_id(&env, 3, 2);
    assert_int_equal(r3_2.tuple_count, 1);
    assert_int_equal(r3_2.tuples[0]->dnb.data[0].val.i32, 200);
    free(r3_2.tuples[0]);

    ResultTuple r3_3 = env_select_by_id(&env, 3, 3);
    assert_int_equal(r3_3.tuple_count, 1);
    assert_int_equal(r3_3.tuples[0]->dnb.data[0].val.i32, 300);
    free(r3_3.tuples[0]);

    /* txn 7: po update id=1 */
    ResultTuple r7_1 = env_select_by_id(&env, 7, 1);
    assert_int_equal(r7_1.tuple_count, 1);
    assert_int_equal(r7_1.tuples[0]->dnb.data[0].val.i32, 500);
    free(r7_1.tuples[0]);

    ResultTuple r7_2 = env_select_by_id(&env, 7, 2);
    assert_int_equal(r7_2.tuple_count, 1);
    assert_int_equal(r7_2.tuples[0]->dnb.data[0].val.i32, 200);
    free(r7_2.tuples[0]);

    ResultTuple r7_3 = env_select_by_id(&env, 7, 3);
    assert_int_equal(r7_3.tuple_count, 1);
    assert_int_equal(r7_3.tuples[0]->dnb.data[0].val.i32, 300);
    free(r7_3.tuples[0]);

    /* txn 15: po update id=1 i id=2 */
    ResultTuple r15_1 = env_select_by_id(&env, 15, 1);
    assert_int_equal(r15_1.tuple_count, 1);
    assert_int_equal(r15_1.tuples[0]->dnb.data[0].val.i32, 500);
    free(r15_1.tuples[0]);

    ResultTuple r15_2 = env_select_by_id(&env, 15, 2);
    assert_int_equal(r15_2.tuple_count, 1);
    assert_int_equal(r15_2.tuples[0]->dnb.data[0].val.i32, 2000);
    free(r15_2.tuples[0]);

    ResultTuple r15_3 = env_select_by_id(&env, 15, 3);
    assert_int_equal(r15_3.tuple_count, 1);
    assert_int_equal(r15_3.tuples[0]->dnb.data[0].val.i32, 300);
    free(r15_3.tuples[0]);

    /* txn 25: po wszystkich updates */
    ResultTuple r25_1 = env_select_by_id(&env, 25, 1);
    assert_int_equal(r25_1.tuple_count, 1);
    assert_int_equal(r25_1.tuples[0]->dnb.data[0].val.i32, 500);
    free(r25_1.tuples[0]);

    ResultTuple r25_2 = env_select_by_id(&env, 25, 2);
    assert_int_equal(r25_2.tuple_count, 1);
    assert_int_equal(r25_2.tuples[0]->dnb.data[0].val.i32, 2000);
    free(r25_2.tuples[0]);

    ResultTuple r25_3 = env_select_by_id(&env, 25, 3);
    assert_int_equal(r25_3.tuple_count, 1);
    assert_int_equal(r25_3.tuples[0]->dnb.data[0].val.i32, 3000);
    free(r25_3.tuples[0]);

    env_teardown(&env);
}

/* =========================================================================
 * Test 5 — Duże dane: 9 tuple, każda 4x updatowana, weryfikacja dla 5 transakcji
 *   INSERT 9 tuple: val = 1..9, xmin=1
 *   txn 5:  każda val → val*10     (1→10, 2→20, ..., 9→90)
 *   txn 10: każda val → val+1000   (10→1010, 20→1020, ..., 90→1090)
 *   txn 15: każda val → val*2      (1010→2020, ..., 1090→2180)
 *
 *   xid=2:  suma = 1+2+...+9 = 45
 *   xid=7:  suma = 10+20+...+90 = 450
 *   xid=12: suma = 1010+1020+...+1090 = 9450
 *   xid=20: suma = 2020+2040+...+2180 = 18900
 * ========================================================================= */

static void test_chain_large_multi_update_sum_check(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    env_hold_snapshot(&env,  2);
    env_hold_snapshot(&env,  7);
    env_hold_snapshot(&env, 12);
    env_hold_snapshot(&env, 20);

    /* Wstaw 9 tuple: val = 1..9 */
    for (int v = 1; v <= 9; v++) {
        env_insert(&env, 1, v);
    }

    /* txn 5: val → val*10 */
    for (int v = 1; v <= 9; v++) {
        env_update(&env, 5, v, v * 10);
    }

    /* txn 10: val → val+1000 (po txn 5 wartości to 10..90) */
    for (int v = 1; v <= 9; v++) {
        env_update(&env, 10, v * 10, v * 10 + 1000);
    }

    /* txn 15: val → val*2 (po txn 10 wartości to 1010..1090) */
    for (int v = 1; v <= 9; v++) {
        env_update(&env, 15, v * 10 + 1000, (v * 10 + 1000) * 2);
    }

    /* xid=2: oryginały 1..9, suma=45 */
    ResultTuple r2 = env_select(&env, 2);
    assert_int_equal(r2.tuple_count, 9);
    int sum2 = 0;
    for (int i = 0; i < r2.tuple_count; i++) {
        sum2 += r2.tuples[i]->dnb.data[0].val.i32;
        free(r2.tuples[i]);
    }
    assert_int_equal(sum2, 45);

    /* xid=7: po txn 5: 10+20+...+90 = 450 */
    ResultTuple r7 = env_select(&env, 7);
    assert_int_equal(r7.tuple_count, 9);
    int sum7 = 0;
    for (int i = 0; i < r7.tuple_count; i++) {
        sum7 += r7.tuples[i]->dnb.data[0].val.i32;
        free(r7.tuples[i]);
    }
    assert_int_equal(sum7, 450);

    /* xid=12: po txn 10: 1010+1020+...+1090 = 9450 */
    ResultTuple r12 = env_select(&env, 12);
    assert_int_equal(r12.tuple_count, 9);
    int sum12 = 0;
    for (int i = 0; i < r12.tuple_count; i++) {
        sum12 += r12.tuples[i]->dnb.data[0].val.i32;
        free(r12.tuples[i]);
    }
    assert_int_equal(sum12, 9450);

    /* xid=20: po txn 15: 2020+2040+...+2180 = 18900 */
    ResultTuple r20 = env_select(&env, 20);
    assert_int_equal(r20.tuple_count, 9);
    int sum20 = 0;
    for (int i = 0; i < r20.tuple_count; i++) {
        sum20 += r20.tuples[i]->dnb.data[0].val.i32;
        free(r20.tuples[i]);
    }
    assert_int_equal(sum20, 18900);

    env_teardown(&env);
}

/* =========================================================================
 * Test 6 — Równoczesne transakcje: stara transakcja trzyma stary snapshot
 *   txn A (xid=1) insertuje tuple val=999
 *   txn B (xid=5) robi UPDATE → val=1000
 *   txn C (xid=10) robi UPDATE → val=9999
 *
 *   txn A snapshot (xid=1): widzi val=999
 *   txn B snapshot (xid=5): widzi val=999 (update własny był po START)
 *                            ale xid=5 >= xmin=1 i xmax=5... xmax=5 <= 5 → niewidoczna!
 *                            właściwie txn B widzi starą bo xmax=5 <= xid=5
 *   txn middle (xid=6): widzi val=1000 (po txn B, przed txn C)
 *   txn D (xid=15): widzi val=9999
 * ========================================================================= */

static void test_chain_concurrent_transactions_hold_snapshots(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    env_hold_snapshot(&env,  1);
    env_hold_snapshot(&env,  6);
    env_hold_snapshot(&env, 15);

    env_insert(&env,  1, 999);
    env_update(&env,  5,  999, 1000);
    env_update(&env, 10, 1000, 9999);

    /* txn przed insertem (niemożliwe normalnie, ale xid < xmin=1 nic nie widzi) */
    /* Sprawdzamy xid=1: xmin=1 <= 1, xmax=5 > 1 → val=999 widoczna */
    ResultTuple r1 = env_select(&env, 1);
    assert_int_equal(r1.tuple_count, 1);
    assert_int_equal(r1.tuples[0]->dnb.data[0].val.i32, 999);
    free(r1.tuples[0]);

    /* xid=6: po txn 5 (xmax=5 <= 6 → v1 niewidoczna), v2 xmin=5 <= 6, xmax=10 > 6 → val=1000 */
    ResultTuple r6 = env_select(&env, 6);
    assert_int_equal(r6.tuple_count, 1);
    assert_int_equal(r6.tuples[0]->dnb.data[0].val.i32, 1000);
    free(r6.tuples[0]);

    /* xid=15: po obu updates → val=9999 */
    ResultTuple r15 = env_select(&env, 15);
    assert_int_equal(r15.tuple_count, 1);
    assert_int_equal(r15.tuples[0]->dnb.data[0].val.i32, 9999);
    free(r15.tuples[0]);

    env_teardown(&env);
}

/* =========================================================================
 * Test 7 — chain member nie jest widoczny bezpośrednio w żadnym snapshоcie
 *   INSERT, UPDATE × 3 → łańcuch 4 wersji
 *   Żaden snapshot nie powinien widzieć więcej niż 1 wynik dla tej tuple
 * ========================================================================= */

static void test_chain_no_duplicate_versions_in_result(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    int32_t xids[] = {2, 6, 11, 16, 20};
    for (int i = 0; i < 5; i++) env_hold_snapshot(&env, xids[i]);

    env_insert(&env,  1,  1);
    env_update(&env,  5,  1, 10);
    env_update(&env, 10, 10, 100);
    env_update(&env, 15,100, 1000);

    /* Każdy snapshot widzi dokładnie 1 tuple — nigdy duplikat */
    int32_t expected[] = {1, 10, 100, 1000, 1000};

    for (int i = 0; i < 5; i++) {
        ResultTuple r = env_select(&env, xids[i]);
        assert_int_equal(r.tuple_count, 1);
        assert_int_equal(r.tuples[0]->dnb.data[0].val.i32, expected[i]);
        free(r.tuples[0]);
    }

    env_teardown(&env);
}

/* =========================================================================
 * Test 8 — Mieszane: część tuple zaktualizowana, część nie
 *   5 tuple: val=1,2,3,4,5
 *   txn 5: UPDATE val=2 → 20, val=4 → 40
 *   txn 10: UPDATE val=20 → 200  (drugi UPDATE na tej samej tuple)
 *
 *   xid=3:  widzi 1,2,3,4,5  (oryginały)
 *   xid=7:  widzi 1,20,3,40,5
 *   xid=15: widzi 1,200,3,40,5  (val=2 był 2x updatowany, val=4 raz)
 * ========================================================================= */

static void test_chain_mixed_updated_and_original(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env);

    env_hold_snapshot(&env,  3);
    env_hold_snapshot(&env,  7);
    env_hold_snapshot(&env, 15);

    for (int v = 1; v <= 5; v++) {
        env_insert(&env, 1, v);
    }

    env_update(&env,  5, 2, 20);
    env_update(&env,  5, 4, 40);
    env_update(&env, 10, 20, 200);

    /* xid=3: oryginały */
    ResultTuple r3 = env_select(&env, 3);
    assert_int_equal(r3.tuple_count, 5);
    int sum3 = 0;
    for (int i = 0; i < r3.tuple_count; i++) {
        sum3 += r3.tuples[i]->dnb.data[0].val.i32;
        free(r3.tuples[i]);
    }
    assert_int_equal(sum3, 15); /* 1+2+3+4+5 */

    /* xid=7: 1+20+3+40+5=69 */
    ResultTuple r7 = env_select(&env, 7);
    assert_int_equal(r7.tuple_count, 5);
    int sum7 = 0;
    for (int i = 0; i < r7.tuple_count; i++) {
        sum7 += r7.tuples[i]->dnb.data[0].val.i32;
        free(r7.tuples[i]);
    }
    assert_int_equal(sum7, 69);

    /* xid=15: 1+200+3+40+5=249 */
    ResultTuple r15 = env_select(&env, 15);
    assert_int_equal(r15.tuple_count, 5);
    int sum15 = 0;
    for (int i = 0; i < r15.tuple_count; i++) {
        sum15 += r15.tuples[i]->dnb.data[0].val.i32;
        free(r15.tuples[i]);
    }
    assert_int_equal(sum15, 249);

    env_teardown(&env);
}

/* =========================================================================
 * Helper: zbuduj minimalny Tuple z podanym xmin/xmax (bez danych)
 * ========================================================================= */

static Tuple make_tuple(int32_t xmin, int32_t xmax) {
    Tuple t = {0};
    t.header.t_xmin = xmin;
    t.header.t_xmax = xmax;
    return t;
}

/* =========================================================================
 * canVacuum — testy komponentowe
 * ========================================================================= */

/* 1. xmax == 0 → żywa tupla, nie można vacuumować */
static void test_canVacuum_xmax_zero_returns_false(void **state) {
    (void)state;
    MVCC *mvcc;
    create_MVCC(&mvcc);
    /* brak aktywnych transakcji */
    for (int i = 0; i < MAX_TRANSACTIONS; i++)
        mvcc->txn_status[i] = TXN_COMMITED;

    Tuple t = make_tuple(1, 0);
    assert_int_equal(canVacuum(&t, mvcc), 0);
    free(mvcc);
}

/* 2. xmax < 0 → żywa tupla (xmax sentinel < 0), nie można vacuumować */
static void test_canVacuum_xmax_negative_returns_false(void **state) {
    (void)state;
    MVCC *mvcc;
    create_MVCC(&mvcc);
    for (int i = 0; i < MAX_TRANSACTIONS; i++)
        mvcc->txn_status[i] = TXN_COMMITED;

    Tuple t = make_tuple(1, -1);
    assert_int_equal(canVacuum(&t, mvcc), 0);
    free(mvcc);
}

/* 3. xmax ustawiony, brak aktywnych transakcji (oldest==-1) → można vacuumować */
static void test_canVacuum_no_active_txns_returns_true(void **state) {
    (void)state;
    MVCC *mvcc;
    create_MVCC(&mvcc);
    for (int i = 0; i < MAX_TRANSACTIONS; i++)
        mvcc->txn_status[i] = TXN_COMMITED;

    Tuple t = make_tuple(1, 5);
    assert_int_equal(canVacuum(&t, mvcc), 1);
    free(mvcc);
}

/* 4. xmax < oldest aktywnej transakcji → każda aktywna widzi tuplę jako martwą → można vacuumować */
static void test_canVacuum_xmax_less_than_oldest_returns_true(void **state) {
    (void)state;
    MVCC *mvcc;
    create_MVCC(&mvcc);
    for (int i = 0; i < MAX_TRANSACTIONS; i++)
        mvcc->txn_status[i] = TXN_COMMITED;
    /* najstarsza aktywna to xid=10 */
    mvcc->txn_status[10] = TXN_ACTIVE;
    mvcc->txn_status[15] = TXN_ACTIVE;

    Tuple t = make_tuple(1, 5); /* xmax=5 < oldest=10 */
    assert_int_equal(canVacuum(&t, mvcc), 1);
    free(mvcc);
}

/* 5. xmax == oldest → aktywna transakcja mogła jeszcze widzieć tuplę → nie można vacuumować */
static void test_canVacuum_xmax_equal_oldest_returns_false(void **state) {
    (void)state;
    MVCC *mvcc;
    create_MVCC(&mvcc);
    for (int i = 0; i < MAX_TRANSACTIONS; i++)
        mvcc->txn_status[i] = TXN_COMMITED;
    mvcc->txn_status[10] = TXN_ACTIVE;

    Tuple t = make_tuple(1, 10); /* xmax=10 == oldest=10 */
    assert_int_equal(canVacuum(&t, mvcc), 0);
    free(mvcc);
}

/* 6. xmax > oldest → jakaś aktywna transakcja nadal widzi tuplę jako żywą → nie można vacuumować */
static void test_canVacuum_xmax_greater_than_oldest_returns_false(void **state) {
    (void)state;
    MVCC *mvcc;
    create_MVCC(&mvcc);
    for (int i = 0; i < MAX_TRANSACTIONS; i++)
        mvcc->txn_status[i] = TXN_COMMITED;
    mvcc->txn_status[5] = TXN_ACTIVE; /* oldest=5 */

    Tuple t = make_tuple(1, 20); /* xmax=20 > oldest=5 */
    assert_int_equal(canVacuum(&t, mvcc), 0);
    free(mvcc);
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_chain_single_update_rr_two_snapshots),
        cmocka_unit_test(test_chain_triple_update_rr_versioning),
        cmocka_unit_test(test_chain_many_tuples_partial_update_rr),
        cmocka_unit_test(test_chain_independent_chains_snapshot_isolation),
        cmocka_unit_test(test_chain_large_multi_update_sum_check),
        cmocka_unit_test(test_chain_concurrent_transactions_hold_snapshots),
        cmocka_unit_test(test_chain_no_duplicate_versions_in_result),
        cmocka_unit_test(test_chain_mixed_updated_and_original),
        /* canVacuum */
        cmocka_unit_test(test_canVacuum_xmax_zero_returns_false),
        cmocka_unit_test(test_canVacuum_xmax_negative_returns_false),
        cmocka_unit_test(test_canVacuum_no_active_txns_returns_true),
        cmocka_unit_test(test_canVacuum_xmax_less_than_oldest_returns_true),
        cmocka_unit_test(test_canVacuum_xmax_equal_oldest_returns_false),
        cmocka_unit_test(test_canVacuum_xmax_greater_than_oldest_returns_false),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}