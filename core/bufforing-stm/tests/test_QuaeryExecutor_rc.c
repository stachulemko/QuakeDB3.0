#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../bufforing-stm/queryExecutor.h"
#include "../bufforing-stm/fullScan.h"
#include "../bufforing-stm/dataBuffor.h"
#include "../bufforing-stm/transaction.h"
#include "../bufforing-stm/fsmMap.h"
#include "../../memory-mgmt/memory-mgmt/all_var.h"
#include "../../memory-mgmt/memory-mgmt/block8kb.h"
#include "../../memory-mgmt/memory-mgmt/tuple.h"

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static Tuple make_tuple_2col(int32_t xmin, int32_t xmax, int64_t opt_oid, int32_t val1, const char *val2) {
    Tuple t = {0};
    tuple_init(&t);
    AllVar vals[2] = {
        all_var_from_int32(val1),
        all_var_from_string(val2)
    };
    int8_t bm[2] = {0, 0};
    tuple_set(&t, xmin, xmax, 0, 0, 0, 0, opt_oid, bm, 2, vals, 2);
    return t;
}

/* -------------------------------------------------------------------------
 * passIsolation VIEW_MODE==2 tests
 * (compiled with -DVIEW_MODE=2, so the readCommited path is active)
 * ------------------------------------------------------------------------- */

static void test_passIsolation_rc_always_skips(void **state) {
    (void)state;
    Transaction txn = {.xid = 5};
    QueryExecutor qe = {0};
    qe.transaction = &txn;
    int32_t xidMax = 0;
    int32_t blockTupleIndex = -1;

    // VIEW_MODE==2: passIsolation always returns 1 (skip) i deleguje do readCommited
    Tuple t = make_tuple_2col(3, 0, -1, 1, "test");
    int8_t result = passIsolation(t, &qe, 0, &xidMax, &blockTupleIndex);
    assert_int_equal(result, 1);
}

static void test_passIsolation_rc_updates_xidMax(void **state) {
    (void)state;
    Transaction txn = {.xid = 5};
    QueryExecutor qe = {0};
    qe.transaction = &txn;
    int32_t xidMax = 0;
    int32_t blockTupleIndex = -1;

    // passIsolation wywoluje readCommited -> xidMax powinien byc zaktualizowany
    Tuple t = make_tuple_2col(10, 0, -1, 42, "val");
    passIsolation(t, &qe, 3, &xidMax, &blockTupleIndex);
    assert_int_equal(xidMax, 10);
    assert_int_equal(blockTupleIndex, 3);
}

static void test_passIsolation_rc_tracks_best_index(void **state) {
    (void)state;
    Transaction txn = {.xid = 5};
    QueryExecutor qe = {0};
    qe.transaction = &txn;
    int32_t xidMax = 0;
    int32_t blockTupleIndex = -1;

    // tuple 0: xmin=5 -> xidMax=5, idx=0
    Tuple t0 = make_tuple_2col(5, 0, -1, 10, "v1");
    passIsolation(t0, &qe, 0, &xidMax, &blockTupleIndex);
    assert_int_equal(xidMax, 5);
    assert_int_equal(blockTupleIndex, 0);

    // tuple 1: xmin=8 -> xidMax=8, idx=1
    Tuple t1 = make_tuple_2col(8, 0, -1, 20, "v2");
    passIsolation(t1, &qe, 1, &xidMax, &blockTupleIndex);
    assert_int_equal(xidMax, 8);
    assert_int_equal(blockTupleIndex, 1);

    // tuple 2: xmin=3 -> brak aktualizacji, idx zostaje 1
    Tuple t2 = make_tuple_2col(3, 0, -1, 30, "v3");
    passIsolation(t2, &qe, 2, &xidMax, &blockTupleIndex);
    assert_int_equal(xidMax, 8);
    assert_int_equal(blockTupleIndex, 1);
}

/* -------------------------------------------------------------------------
 * parserCommands VIEW_MODE==2 tests
 * ------------------------------------------------------------------------- */

static void test_parserCommands_rc_select_returns_best(void **state) {
    (void)state;
    Transaction txn = {.xid = 5};
    QueryExecutor qe = {0};
    qe.transaction = &txn;
    int32_t cols[MAX_COLUMNS] = {0, 1};
    Qeselect(&qe, cols, 2);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    // Trzy wersje tego samego wiersza: xmin=3, xmin=5, xmin=7
    // readCommited powinien wybrac xmin=7 (index=2)
    Tuple t0 = make_tuple_2col(3, 0, -1, 10, "v1");
    Tuple t1 = make_tuple_2col(5, 0, -1, 10, "v2");
    Tuple t2 = make_tuple_2col(7, 0, -1, 10, "v3");

    block8kb_add(block, &t0);
    block8kb_add(block, &t1);
    block8kb_add(block, &t2);

    ResultTuple result = {0};
    parserCommands(block, &qe, &result, NULL, 1, 1, 1, 0);

    // Powinien zwrocic dokladnie 1 tuple — najnowsza wersje
    assert_int_equal(result.tuple_count, 1);
    assert_string_equal(result.tuples[0].dnb.data[1].val.str, "v3");

    free(block);
}

static void test_parserCommands_rc_where_returns_best(void **state) {
    (void)state;
    Transaction txn = {.xid = 5};
    QueryExecutor qe = {0};
    qe.transaction = &txn;
    int32_t cols[MAX_COLUMNS] = {0};
    AllVar vals[MAX_COLUMNS] = {all_var_from_int32(10)};
    Qewhere(&qe, cols, vals, 1);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    // xmin=4 i xmin=9 — powinien zostac zwrocony tylko jeden (xmin=9, najnowszy)
    Tuple t0 = make_tuple_2col(4, 0, -1, 10, "old");
    Tuple t1 = make_tuple_2col(9, 0, -1, 10, "new");

    block8kb_add(block, &t0);
    block8kb_add(block, &t1);

    ResultTuple result = {0};
    parserCommands(block, &qe, &result, NULL, 1, 1, 0, 0);

    assert_int_equal(result.tuple_count, 1);
    assert_string_equal(result.tuples[0].dnb.data[1].val.str, "new");

    free(block);
}

static void test_parserCommands_rc_empty_block(void **state) {
    (void)state;
    Transaction txn = {.xid = 5};
    QueryExecutor qe = {0};
    qe.transaction = &txn;
    int32_t cols[MAX_COLUMNS] = {0, 1};
    Qeselect(&qe, cols, 2);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    ResultTuple result = {0};
    parserCommands(block, &qe, &result, NULL, 1, 1, 1, 0);

    assert_int_equal(result.tuple_count, 0);

    free(block);
}

/* -------------------------------------------------------------------------
 * Main test runner
 * ------------------------------------------------------------------------- */

int main(void) {
    const struct CMUnitTest tests[] = {
        // passIsolation VIEW_MODE==2
        cmocka_unit_test(test_passIsolation_rc_always_skips),
        cmocka_unit_test(test_passIsolation_rc_updates_xidMax),
        cmocka_unit_test(test_passIsolation_rc_tracks_best_index),

        // parserCommands VIEW_MODE==2
        cmocka_unit_test(test_parserCommands_rc_select_returns_best),
        cmocka_unit_test(test_parserCommands_rc_where_returns_best),
        cmocka_unit_test(test_parserCommands_rc_empty_block),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}