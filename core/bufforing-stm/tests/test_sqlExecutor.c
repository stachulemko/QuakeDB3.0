#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../bufforing-stm/sqlExecutor.h"
#include "../../memory-mgmt/memory-mgmt/all_var.h"
#include "../../memory-mgmt/memory-mgmt/block8kb.h"
#include "../../memory-mgmt/memory-mgmt/tuple.h"

/* =========================================================================
 * Helpers
 * ========================================================================= */

static Tuple make_tuple(int32_t xmin, int32_t xmax, int32_t val0, const char *val1, int64_t val2) {
    Tuple t = {0};
    tuple_init(&t);
    AllVar vals[3] = {
        all_var_from_int32(val0),
        all_var_from_string(val1),
        all_var_from_int64(val2)
    };
    int8_t bm[3] = {0, 0, 0};
    tuple_set(&t, xmin, xmax, 0, 0, 0, 0, -1, bm, 3, vals, 3);
    return t;
}

static SqlExecutor make_se(int32_t xid) {
    SqlExecutor se = {0};
    static Transaction txn;
    txn.xid = xid;
    se.transaction = &txn;
    return se;
}

/* =========================================================================
 * 1. sql_matchWhere — operatory
 * ========================================================================= */

static void test_where_eq(void **state) {
    (void)state;
    SqlExecutor se = make_se(5);
    sql_addWhere(&se, 0, all_var_from_int32(42), SQL_EQ, 1);

    Tuple t = make_tuple(1, 0, 42, "x", 0);
    assert_int_equal(sql_matchWhere(&se, &t), 1);

    Tuple t2 = make_tuple(1, 0, 99, "x", 0);
    assert_int_equal(sql_matchWhere(&se, &t2), 0);
}

static void test_where_neq(void **state) {
    (void)state;
    SqlExecutor se = make_se(5);
    sql_addWhere(&se, 0, all_var_from_int32(42), SQL_NEQ, 1);

    Tuple t = make_tuple(1, 0, 99, "x", 0);
    assert_int_equal(sql_matchWhere(&se, &t), 1);

    Tuple t2 = make_tuple(1, 0, 42, "x", 0);
    assert_int_equal(sql_matchWhere(&se, &t2), 0);
}

static void test_where_lt(void **state) {
    (void)state;
    SqlExecutor se = make_se(5);
    sql_addWhere(&se, 0, all_var_from_int32(50), SQL_LT, 1);

    Tuple t = make_tuple(1, 0, 30, "x", 0);  // 30 < 50 -> true
    assert_int_equal(sql_matchWhere(&se, &t), 1);

    Tuple t2 = make_tuple(1, 0, 70, "x", 0); // 70 < 50 -> false
    assert_int_equal(sql_matchWhere(&se, &t2), 0);
}

static void test_where_gt(void **state) {
    (void)state;
    SqlExecutor se = make_se(5);
    sql_addWhere(&se, 0, all_var_from_int32(50), SQL_GT, 1);

    Tuple t = make_tuple(1, 0, 80, "x", 0);  // 80 > 50 -> true
    assert_int_equal(sql_matchWhere(&se, &t), 1);

    Tuple t2 = make_tuple(1, 0, 20, "x", 0); // 20 > 50 -> false
    assert_int_equal(sql_matchWhere(&se, &t2), 0);
}

static void test_where_le(void **state) {
    (void)state;
    SqlExecutor se = make_se(5);
    sql_addWhere(&se, 0, all_var_from_int32(50), SQL_LE, 1);

    Tuple t1 = make_tuple(1, 0, 50, "x", 0); // 50 <= 50 -> true
    Tuple t2 = make_tuple(1, 0, 30, "x", 0); // 30 <= 50 -> true
    Tuple t3 = make_tuple(1, 0, 70, "x", 0); // 70 <= 50 -> false

    assert_int_equal(sql_matchWhere(&se, &t1), 1);
    assert_int_equal(sql_matchWhere(&se, &t2), 1);
    assert_int_equal(sql_matchWhere(&se, &t3), 0);
}

static void test_where_ge(void **state) {
    (void)state;
    SqlExecutor se = make_se(5);
    sql_addWhere(&se, 0, all_var_from_int32(50), SQL_GE, 1);

    Tuple t1 = make_tuple(1, 0, 50, "x", 0); // 50 >= 50 -> true
    Tuple t2 = make_tuple(1, 0, 80, "x", 0); // 80 >= 50 -> true
    Tuple t3 = make_tuple(1, 0, 20, "x", 0); // 20 >= 50 -> false

    assert_int_equal(sql_matchWhere(&se, &t1), 1);
    assert_int_equal(sql_matchWhere(&se, &t2), 1);
    assert_int_equal(sql_matchWhere(&se, &t3), 0);
}

static void test_where_string_eq(void **state) {
    (void)state;
    SqlExecutor se = make_se(5);
    sql_addWhere(&se, 1, all_var_from_string("Alice"), SQL_EQ, 1);

    Tuple t1 = make_tuple(1, 0, 1, "Alice", 0);
    Tuple t2 = make_tuple(1, 0, 2, "Bob",   0);

    assert_int_equal(sql_matchWhere(&se, &t1), 1);
    assert_int_equal(sql_matchWhere(&se, &t2), 0);
}

static void test_where_multiple_conditions_and(void **state) {
    (void)state;
    SqlExecutor se = make_se(5);
    sql_addWhere(&se, 0, all_var_from_int32(10), SQL_GT, 1);  // col0 > 10
    sql_addWhere(&se, 0, all_var_from_int32(50), SQL_LT, 1);  // col0 < 50

    Tuple t1 = make_tuple(1, 0, 30, "x", 0); // 10 < 30 < 50 -> true
    Tuple t2 = make_tuple(1, 0,  5, "x", 0); // 5  <= 10     -> false
    Tuple t3 = make_tuple(1, 0, 60, "x", 0); // 60 >= 50     -> false

    assert_int_equal(sql_matchWhere(&se, &t1), 1);
    assert_int_equal(sql_matchWhere(&se, &t2), 0);
    assert_int_equal(sql_matchWhere(&se, &t3), 0);
}

/* =========================================================================
 * 2. sql_execBlock — SELECT
 * ========================================================================= */

static void test_execBlock_select_all(void **state) {
    (void)state;
    SqlExecutor se = make_se(1);
    int32_t cols[] = {0, 1};
    sql_setSelect(&se, cols, 2);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    Tuple t1 = make_tuple(1, 0, 10, "A", 0);
    Tuple t2 = make_tuple(1, 0, 20, "B", 0);
    block8kb_add(block, &t1);
    block8kb_add(block, &t2);

    ResultTuple result = {0};
    sql_execBlock(block, &se, &result);

    assert_int_equal(result.tuple_count, 2);
    assert_int_equal(result.tuples[0]->dnb.data[0].val.i32, 10);
    assert_string_equal(result.tuples[0]->dnb.data[1].val.str, "A");
    assert_int_equal(result.tuples[1]->dnb.data[0].val.i32, 20);

    free(result.tuples[0]);
    free(result.tuples[1]);
    free(block);
}

static void test_execBlock_select_subset_columns(void **state) {
    (void)state;
    SqlExecutor se = make_se(1);
    int32_t cols[] = {2}; // tylko col2 (int64)
    sql_setSelect(&se, cols, 1);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    Tuple t = make_tuple(1, 0, 99, "skip", 12345LL);
    block8kb_add(block, &t);

    ResultTuple result = {0};
    sql_execBlock(block, &se, &result);

    assert_int_equal(result.tuple_count, 1);
    assert_int_equal(result.tuples[0]->dnb.data_count, 1);
    assert_int_equal(result.tuples[0]->dnb.data[0].val.i64, 12345LL);

    free(result.tuples[0]);
    free(block);
}

/* =========================================================================
 * 3. sql_execBlock — WHERE + SELECT
 * ========================================================================= */

static void test_execBlock_where_select(void **state) {
    (void)state;
    SqlExecutor se = make_se(1);
    sql_addWhere(&se, 0, all_var_from_int32(20), SQL_GE, 1); // col0 >= 20
    int32_t cols[] = {0, 1};
    sql_setSelect(&se, cols, 2);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    Tuple t1 = make_tuple(1, 0, 10, "A", 0); // 10 >= 20 -> false
    Tuple t2 = make_tuple(1, 0, 20, "B", 0); // 20 >= 20 -> true
    Tuple t3 = make_tuple(1, 0, 30, "C", 0); // 30 >= 20 -> true
    block8kb_add(block, &t1);
    block8kb_add(block, &t2);
    block8kb_add(block, &t3);

    ResultTuple result = {0};
    sql_execBlock(block, &se, &result);

    assert_int_equal(result.tuple_count, 2);
    assert_int_equal(result.tuples[0]->dnb.data[0].val.i32, 20);
    assert_int_equal(result.tuples[1]->dnb.data[0].val.i32, 30);

    free(result.tuples[0]);
    free(result.tuples[1]);
    free(block);
}

static void test_execBlock_where_no_match(void **state) {
    (void)state;
    SqlExecutor se = make_se(1);
    sql_addWhere(&se, 0, all_var_from_int32(999), SQL_EQ, 1);
    int32_t cols[] = {0};
    sql_setSelect(&se, cols, 1);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    Tuple t = make_tuple(1, 0, 1, "x", 0);
    block8kb_add(block, &t);

    ResultTuple result = {0};
    sql_execBlock(block, &se, &result);

    assert_int_equal(result.tuple_count, 0);
    free(block);
}

/* =========================================================================
 * 4. sql_execBlock — UPDATE
 * ========================================================================= */

static void test_execBlock_update_where(void **state) {
    (void)state;
    SqlExecutor se = make_se(1);
    sql_addWhere(&se, 0, all_var_from_int32(10), SQL_EQ, 1);
    int32_t upd_cols[] = {0};
    AllVar  upd_vals[] = {all_var_from_int32(999)};
    sql_setUpdate(&se, upd_cols, upd_vals, 1);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    Tuple t1 = make_tuple(1, 0, 10, "target", 0);
    Tuple t2 = make_tuple(1, 0, 20, "other",  0);
    block8kb_add(block, &t1);
    block8kb_add(block, &t2);

    ResultTuple result = {0};
    sql_execBlock(block, &se, &result);

    // tylko t1 pasuje — jej col0 zmieniony na 999
    assert_int_equal(result.tuple_count, 1);
    assert_int_equal(result.tuples[0]->dnb.data[0].val.i32, 999);
    // t2 niezmieniona
    assert_int_equal(block->tuples[1].dnb.data[0].val.i32, 20);

    free(block);
}

/* =========================================================================
 * 5. Izolacja — Repeatable Read (VIEW_MODE == 1)
 * ========================================================================= */

static void test_execBlock_isolation_rr(void **state) {
    (void)state;
    SqlExecutor se = make_se(5); // xid = 5
    int32_t cols[] = {0};
    sql_setSelect(&se, cols, 1);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    Tuple t1 = make_tuple(3, 0,  1, "x", 0); // xmin=3 <= 5, xmax=0  -> widoczny
    Tuple t2 = make_tuple(7, 0,  2, "x", 0); // xmin=7 >  5          -> niewidoczny
    Tuple t3 = make_tuple(1, 4,  3, "x", 0); // xmin=1, xmax=4 <= 5  -> usunięty
    Tuple t4 = make_tuple(2, 0,  4, "x", 0); // xmin=2 <= 5, xmax=0  -> widoczny
    block8kb_add(block, &t1);
    block8kb_add(block, &t2);
    block8kb_add(block, &t3);
    block8kb_add(block, &t4);

    ResultTuple result = {0};
    sql_execBlock(block, &se, &result);

    assert_int_equal(result.tuple_count, 2);
    assert_int_equal(result.tuples[0]->dnb.data[0].val.i32, 1);
    assert_int_equal(result.tuples[1]->dnb.data[0].val.i32, 4);

    free(result.tuples[0]);
    free(result.tuples[1]);
    free(block);
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void) {
    const struct CMUnitTest tests[] = {
        // WHERE operatory
        cmocka_unit_test(test_where_eq),
        cmocka_unit_test(test_where_neq),
        cmocka_unit_test(test_where_lt),
        cmocka_unit_test(test_where_gt),
        cmocka_unit_test(test_where_le),
        cmocka_unit_test(test_where_ge),
        cmocka_unit_test(test_where_string_eq),
        cmocka_unit_test(test_where_multiple_conditions_and),

        // SELECT
        cmocka_unit_test(test_execBlock_select_all),
        cmocka_unit_test(test_execBlock_select_subset_columns),

        // WHERE + SELECT
        cmocka_unit_test(test_execBlock_where_select),
        cmocka_unit_test(test_execBlock_where_no_match),

        // UPDATE
        cmocka_unit_test(test_execBlock_update_where),

        // Izolacja RR
        cmocka_unit_test(test_execBlock_isolation_rr),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}