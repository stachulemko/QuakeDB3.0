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
 * Helpers to construct test tuples and blocks
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

static Tuple make_tuple_3col(int32_t xmin, int32_t xmax, int64_t opt_oid, int32_t val1, const char *val2, int64_t val3) {
    Tuple t = {0};
    tuple_init(&t);
    AllVar vals[3] = {
        all_var_from_int32(val1),
        all_var_from_string(val2),
        all_var_from_int64(val3)
    };
    int8_t bm[3] = {0, 0, 0};
    tuple_set(&t, xmin, xmax, 0, 0, 0, 0, opt_oid, bm, 3, vals, 3);
    return t;
}

static void free_test_buffors(Buffors *b) {
    for (int i = 0; i < b->count; i++) {
        if (b->buffors[i].isUsed && b->buffors[i].universalBlock) {
            if (b->buffors[i].universalBlock->block) {
                free(b->buffors[i].universalBlock->block);
            }
            if (b->buffors[i].universalBlock->header) {
                free(b->buffors[i].universalBlock->header);
            }
            free(b->buffors[i].universalBlock);
        }
    }
    if (b->buffors) {
        free(b->buffors);
        b->buffors = NULL;
    }
}

/* -------------------------------------------------------------------------
 * 1. NextData struct tests
 * ------------------------------------------------------------------------- */

static void test_nextData_struct(void **state) {
    (void)state;
    NextData nd;
    nd.blockId = 42;
    nd.i = 7;

    assert_int_equal(nd.blockId, 42);
    assert_int_equal(nd.i, 7);
}

/* -------------------------------------------------------------------------
 * 2. passIsolation tests
 * ------------------------------------------------------------------------- */

static void test_passIsolation_visible_created_in_past(void **state) {
    (void)state;
    Transaction txn = {.xid = 5};
    QueryExecutor qe = {0};
    qe.transaction = &txn;

    Tuple t = make_tuple_2col(3, 0, -1, 1, "test");
    // t_xmin (3) <= xid (5) and t_xmax (0) == 0 -> visible (returns 0)
    assert_int_equal(passIsolation(t, &qe), 0);
}

static void test_passIsolation_visible_created_in_current_txn(void **state) {
    (void)state;
    Transaction txn = {.xid = 5};
    QueryExecutor qe = {0};
    qe.transaction = &txn;

    Tuple t = make_tuple_2col(5, 0, -1, 1, "test");
    // t_xmin (5) <= xid (5) and t_xmax (0) == 0 -> visible (returns 0)
    assert_int_equal(passIsolation(t, &qe), 0);
}

static void test_passIsolation_invisible_created_in_future(void **state) {
    (void)state;
    Transaction txn = {.xid = 5};
    QueryExecutor qe = {0};
    qe.transaction = &txn;

    Tuple t = make_tuple_2col(6, 0, -1, 1, "test");
    // t_xmin (6) > xid (5) -> invisible (returns 1)
    assert_int_equal(passIsolation(t, &qe), 1);
}

static void test_passIsolation_invisible_deleted_in_past(void **state) {
    (void)state;
    Transaction txn = {.xid = 5};
    QueryExecutor qe = {0};
    qe.transaction = &txn;

    Tuple t = make_tuple_2col(2, 4, -1, 1, "test");
    // t_xmin (2) <= xid (5) but t_xmax (4) <= xid (5) -> deleted, invisible (returns 1)
    assert_int_equal(passIsolation(t, &qe), 1);
}

static void test_passIsolation_invisible_deleted_in_current_txn(void **state) {
    (void)state;
    Transaction txn = {.xid = 5};
    QueryExecutor qe = {0};
    qe.transaction = &txn;

    Tuple t = make_tuple_2col(2, 5, -1, 1, "test");
    // t_xmin (2) <= xid (5) and t_xmax (5) <= xid (5) -> deleted by current txn, invisible (returns 1)
    assert_int_equal(passIsolation(t, &qe), 1);
}

static void test_passIsolation_visible_deleted_in_future(void **state) {
    (void)state;
    Transaction txn = {.xid = 5};
    QueryExecutor qe = {0};
    qe.transaction = &txn;

    Tuple t = make_tuple_2col(2, 6, -1, 1, "test");
    // t_xmin (2) <= xid (5) and t_xmax (6) > xid (5) -> not deleted yet at xid 5, visible (returns 0)
    assert_int_equal(passIsolation(t, &qe), 0);
}

/* -------------------------------------------------------------------------
 * 3. whereIf tests
 * ------------------------------------------------------------------------- */

static void test_whereIf_int32_match(void **state) {
    (void)state;
    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {0};
    AllVar vals[MAX_COLUMNS] = {all_var_from_int32(100)};
    Qewhere(&qe, cols, vals, 1);

    ResultTuple result = {0};
    Tuple t1 = make_tuple_2col(1, 0, -1, 100, "Alice");
    Tuple t2 = make_tuple_2col(1, 0, -1, 200, "Bob");

    whereIf(NULL, &qe, &result, t1);
    assert_int_equal(result.tuple_count, 1);
    assert_int_equal(result.tuples[0].dnb.data[0].val.i32, 100);

    whereIf(NULL, &qe, &result, t2);
    // t2 does not match, count should still be 1
    assert_int_equal(result.tuple_count, 1);
}

static void test_whereIf_string_match(void **state) {
    (void)state;
    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {1};
    AllVar vals[MAX_COLUMNS] = {all_var_from_string("Alice")};
    Qewhere(&qe, cols, vals, 1);

    ResultTuple result = {0};
    Tuple t1 = make_tuple_2col(1, 0, -1, 1, "Alice");
    Tuple t2 = make_tuple_2col(1, 0, -1, 2, "Bob");

    whereIf(NULL, &qe, &result, t1);
    assert_int_equal(result.tuple_count, 1);
    assert_string_equal(result.tuples[0].dnb.data[1].val.str, "Alice");

    whereIf(NULL, &qe, &result, t2);
    assert_int_equal(result.tuple_count, 1);
}

static void test_whereIf_int64_match(void **state) {
    (void)state;
    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {2};
    AllVar vals[MAX_COLUMNS] = {all_var_from_int64(9876543210LL)};
    Qewhere(&qe, cols, vals, 1);

    ResultTuple result = {0};
    Tuple t1 = make_tuple_3col(1, 0, -1, 1, "Item", 9876543210LL);
    Tuple t2 = make_tuple_3col(1, 0, -1, 2, "Item", 12345LL);

    whereIf(NULL, &qe, &result, t1);
    assert_int_equal(result.tuple_count, 1);
    assert_int_equal(result.tuples[0].dnb.data[2].val.i64, 9876543210LL);

    whereIf(NULL, &qe, &result, t2);
    assert_int_equal(result.tuple_count, 1);
}

static void test_whereIf_type_mismatch(void **state) {
    (void)state;
    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {1};
    // Query condition expects INT32 at column 1, but tuple has STRING
    AllVar vals[MAX_COLUMNS] = {all_var_from_int32(42)};
    Qewhere(&qe, cols, vals, 1);

    ResultTuple result = {0};
    Tuple t = make_tuple_2col(1, 0, -1, 1, "Alice");

    whereIf(NULL, &qe, &result, t);
    assert_int_equal(result.tuple_count, 0);
}

static void test_whereIf_result_space_limit(void **state) {
    (void)state;
    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {0};
    AllVar vals[MAX_COLUMNS] = {all_var_from_int32(1)};
    Qewhere(&qe, cols, vals, 1);

    ResultTuple result = {0};
    Tuple t = make_tuple_2col(1, 0, -1, 1, "Alice");

    // Add RESULT_SPACE (10) tuples
    for (int i = 0; i < RESULT_SPACE; i++) {
        whereIf(NULL, &qe, &result, t);
    }
    assert_int_equal(result.tuple_count, RESULT_SPACE);

    // 11th match should not overflow result array
    whereIf(NULL, &qe, &result, t);
    assert_int_equal(result.tuple_count, RESULT_SPACE);
}

static void test_whereIf_multiple_conditions(void **state) {
    (void)state;
    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {0, 1};
    AllVar vals[MAX_COLUMNS] = {all_var_from_int32(10), all_var_from_string("target")};
    Qewhere(&qe, cols, vals, 2);

    ResultTuple result = {0};
    // t1 matches col 0
    Tuple t1 = make_tuple_2col(1, 0, -1, 10, "other");
    // t2 matches col 1
    Tuple t2 = make_tuple_2col(1, 0, -1, 20, "target");
    // t3 matches neither
    Tuple t3 = make_tuple_2col(1, 0, -1, 30, "other");

    whereIf(NULL, &qe, &result, t1);
    assert_int_equal(result.tuple_count, 1);

    whereIf(NULL, &qe, &result, t2);
    assert_int_equal(result.tuple_count, 2);

    whereIf(NULL, &qe, &result, t3);
    assert_int_equal(result.tuple_count, 2);
}

/* -------------------------------------------------------------------------
 * 4. selectIf tests
 * ------------------------------------------------------------------------- */

static void test_selectIf_all_columns(void **state) {
    (void)state;
    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {0, 1, 2};
    Qeselect(&qe, cols, 3);

    ResultTuple result = {0};
    Tuple t = make_tuple_3col(1, 0, -1, 42, "Bob", 123456789LL);

    selectIf(NULL, &qe, &result, t);
    assert_int_equal(result.tuple_count, 1);
    assert_int_equal(result.tuples[0].dnb.data_count, 3);
    assert_int_equal(result.tuples[0].dnb.data[0].val.i32, 42);
    assert_string_equal(result.tuples[0].dnb.data[1].val.str, "Bob");
    assert_int_equal(result.tuples[0].dnb.data[2].val.i64, 123456789LL);
}

static void test_selectIf_subset_columns(void **state) {
    (void)state;
    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {0, 2}; // Select only col 0 and col 2
    Qeselect(&qe, cols, 2);

    ResultTuple result = {0};
    Tuple t = make_tuple_3col(1, 0, -1, 99, "SkipMe", 888LL);

    selectIf(NULL, &qe, &result, t);
    assert_int_equal(result.tuple_count, 1);
    assert_int_equal(result.tuples[0].dnb.data_count, 2);
    assert_int_equal(result.tuples[0].dnb.data[0].val.i32, 99);
    assert_int_equal(result.tuples[0].dnb.data[1].val.i64, 888LL);
}

static void test_selectIf_reordered_columns(void **state) {
    (void)state;
    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {1, 0}; // Reorder col 1 then col 0
    Qeselect(&qe, cols, 2);

    ResultTuple result = {0};
    Tuple t = make_tuple_2col(1, 0, -1, 10, "Hello");

    selectIf(NULL, &qe, &result, t);
    assert_int_equal(result.tuple_count, 1);
    assert_int_equal(result.tuples[0].dnb.data_count, 2);
    assert_string_equal(result.tuples[0].dnb.data[0].val.str, "Hello");
    assert_int_equal(result.tuples[0].dnb.data[1].val.i32, 10);
}

static void test_selectIf_multiple_calls(void **state) {
    (void)state;
    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {0};
    Qeselect(&qe, cols, 1);

    ResultTuple result = {0};
    Tuple t1 = make_tuple_2col(1, 0, -1, 1, "A");
    Tuple t2 = make_tuple_2col(1, 0, -1, 2, "B");
    Tuple t3 = make_tuple_2col(1, 0, -1, 3, "C");

    selectIf(NULL, &qe, &result, t1);
    selectIf(NULL, &qe, &result, t2);
    selectIf(NULL, &qe, &result, t3);

    assert_int_equal(result.tuple_count, 3);
    assert_int_equal(result.tuples[0].dnb.data[0].val.i32, 1);
    assert_int_equal(result.tuples[1].dnb.data[0].val.i32, 2);
    assert_int_equal(result.tuples[2].dnb.data[0].val.i32, 3);
}

/* -------------------------------------------------------------------------
 * 5. parserCommands tests
 * ------------------------------------------------------------------------- */

static void test_parserCommands_where_mode(void **state) {
    (void)state;
    Transaction txn = {.xid = 1};
    QueryExecutor qe = {0};
    qe.transaction = &txn;
    int32_t cols[MAX_COLUMNS] = {0};
    AllVar vals[MAX_COLUMNS] = {all_var_from_int32(20)};
    Qewhere(&qe, cols, vals, 1);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    Tuple t1 = make_tuple_2col(1, 0, -1, 10, "A");
    Tuple t2 = make_tuple_2col(1, 0, -1, 20, "B");
    Tuple t3 = make_tuple_2col(1, 0, -1, 20, "C");
    Tuple t4 = make_tuple_2col(1, 0, -1, 30, "D");

    block8kb_add(block, &t1);
    block8kb_add(block, &t2);
    block8kb_add(block, &t3);
    block8kb_add(block, &t4);

    ResultTuple result = {0};
    NextData nd = parserCommands(block, &qe, &result, NULL, 1, 1, 0, 0);

    assert_int_equal(nd.blockId, 1);
    assert_int_equal(nd.i, 4);
    assert_int_equal(result.tuple_count, 2);
    assert_string_equal(result.tuples[0].dnb.data[1].val.str, "B");
    assert_string_equal(result.tuples[1].dnb.data[1].val.str, "C");

    free(block);
}

static void test_parserCommands_select_mode(void **state) {
    (void)state;
    Transaction txn = {.xid = 1};
    QueryExecutor qe = {0};
    qe.transaction = &txn;
    int32_t cols[MAX_COLUMNS] = {0, 1};
    Qeselect(&qe, cols, 2);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    Tuple t1 = make_tuple_2col(1, 0, -1, 10, "X");
    Tuple t2 = make_tuple_2col(1, 0, -1, 20, "Y");
    Tuple t3 = make_tuple_2col(1, 0, -1, 30, "Z");

    block8kb_add(block, &t1);
    block8kb_add(block, &t2);
    block8kb_add(block, &t3);

    ResultTuple result = {0};
    NextData nd = parserCommands(block, &qe, &result, NULL, 1, 1, 1, 0);

    assert_int_equal(nd.blockId, 1);
    assert_int_equal(nd.i, 3);
    assert_int_equal(result.tuple_count, 3);
    assert_int_equal(result.tuples[0].dnb.data[0].val.i32, 10);
    assert_int_equal(result.tuples[1].dnb.data[0].val.i32, 20);
    assert_int_equal(result.tuples[2].dnb.data[0].val.i32, 30);

    free(block);
}

static void test_parserCommands_with_isolation(void **state) {
    (void)state;
    Transaction txn = {.xid = 2};
    QueryExecutor qe = {0};
    qe.transaction = &txn;
    int32_t cols[MAX_COLUMNS] = {0, 1};
    Qeselect(&qe, cols, 2);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    // t1: visible (created xmin=1, not deleted)
    Tuple t1 = make_tuple_2col(1, 0, -1, 10, "Visible1");
    // t2: invisible (created in future xmin=5)
    Tuple t2 = make_tuple_2col(5, 0, -1, 20, "FutureInvisible");
    // t3: invisible (deleted in past xmin=1, xmax=2)
    Tuple t3 = make_tuple_2col(1, 2, -1, 30, "DeletedInvisible");
    // t4: visible (created xmin=2, not deleted)
    Tuple t4 = make_tuple_2col(2, 0, -1, 40, "Visible2");

    block8kb_add(block, &t1);
    block8kb_add(block, &t2);
    block8kb_add(block, &t3);
    block8kb_add(block, &t4);

    ResultTuple result = {0};
    NextData nd = parserCommands(block, &qe, &result, NULL, 1, 1, 1, 0);

    assert_int_equal(nd.blockId, 1);
    assert_int_equal(nd.i, 4);
    assert_int_equal(result.tuple_count, 2);
    assert_int_equal(result.tuples[0].dnb.data[0].val.i32, 10);
    assert_int_equal(result.tuples[1].dnb.data[0].val.i32, 40);

    free(block);
}

static void test_parserCommands_starti_offset(void **state) {
    (void)state;
    Transaction txn = {.xid = 1};
    QueryExecutor qe = {0};
    qe.transaction = &txn;
    int32_t cols[MAX_COLUMNS] = {0, 1};
    Qeselect(&qe, cols, 2);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    Tuple t1 = make_tuple_2col(1, 0, -1, 1, "One");
    Tuple t2 = make_tuple_2col(1, 0, -1, 2, "Two");
    Tuple t3 = make_tuple_2col(1, 0, -1, 3, "Three");
    Tuple t4 = make_tuple_2col(1, 0, -1, 4, "Four");

    block8kb_add(block, &t1);
    block8kb_add(block, &t2);
    block8kb_add(block, &t3);
    block8kb_add(block, &t4);

    ResultTuple result = {0};
    // Start from index 2 (t3 and t4)
    NextData nd = parserCommands(block, &qe, &result, NULL, 1, 1, 1, 2);

    assert_int_equal(nd.blockId, 1);
    assert_int_equal(nd.i, 4);
    assert_int_equal(result.tuple_count, 2);
    assert_int_equal(result.tuples[0].dnb.data[0].val.i32, 3);
    assert_int_equal(result.tuples[1].dnb.data[0].val.i32, 4);

    free(block);
}

static void test_parserCommands_empty_block(void **state) {
    (void)state;
    Transaction txn = {.xid = 1};
    QueryExecutor qe = {0};
    qe.transaction = &txn;
    int32_t cols[MAX_COLUMNS] = {0, 1};
    Qeselect(&qe, cols, 2);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    ResultTuple result = {0};
    NextData nd = parserCommands(block, &qe, &result, NULL, 1, 1, 1, 0);

    assert_int_equal(nd.blockId, 1);
    assert_int_equal(nd.i, 0);
    assert_int_equal(result.tuple_count, 0);

    free(block);
}

/* -------------------------------------------------------------------------
 * 6. parserWithUpdate tests
 * ------------------------------------------------------------------------- */

static void test_parserWithUpdate_where_mode(void **state) {
    (void)state;
    Transaction txn = {.xid = 1};
    QueryExecutor qe = {0};
    qe.transaction = &txn;
    Qefrom(&qe, 10);
    int32_t cols[MAX_COLUMNS] = {0};
    AllVar vals[MAX_COLUMNS] = {all_var_from_int32(77)};
    Qewhere(&qe, cols, vals, 1);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    Tuple t1 = make_tuple_2col(1, 0, -1, 77, "Lucky");
    Tuple t2 = make_tuple_2col(1, 0, -1, 88, "NotLucky");
    block8kb_add(block, &t1);
    block8kb_add(block, &t2);

    ResultTuple result = {0};
    NextData nd = parserWithUpdate(&qe, block, &result, 1, NULL, 10, 0);

    assert_int_equal(nd.blockId, 1);
    assert_int_equal(nd.i, 2);
    assert_int_equal(result.tuple_count, 1);
    assert_string_equal(result.tuples[0].dnb.data[1].val.str, "Lucky");

    free(block);
}

static void

test_parserWithUpdate_select_mode(void **state) {
    (void)state;
    Transaction txn = {.xid = 1};
    QueryExecutor qe = {0};
    qe.transaction = &txn;
    Qefrom(&qe, 10);
    int32_t cols[MAX_COLUMNS] = {1};
    Qeselect(&qe, cols, 1);

    Block8kb *block = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(block, 2000, -1, 1, 0, 0, 0, 0);

    Tuple t1 = make_tuple_2col(1, 0, -1, 1, "First");
    Tuple t2 = make_tuple_2col(1, 0, -1, 2, "Second");
    block8kb_add(block, &t1);
    block8kb_add(block, &t2);

    ResultTuple result = {0};
    NextData nd = parserWithUpdate(&qe, block, &result, 1, NULL, 10, 0);

    assert_int_equal(nd.blockId, 1);
    assert_int_equal(nd.i, 2);
    assert_int_equal(result.tuple_count, 2);
    assert_string_equal(result.tuples[0].dnb.data[0].val.str, "First");
    assert_string_equal(result.tuples[1].dnb.data[0].val.str, "Second");

    free(block);
}

/* -------------------------------------------------------------------------
 * 7. fullScan & fullScanWithUpdate tests
 * ------------------------------------------------------------------------- */

typedef struct {
    Buffors buffors;
    FSMCache fsmCache;
    FSMMapAll fsmMapAll;
    MVCC mvcc;
} TestScanEnv;

static TestScanEnv setup_scan_env(int32_t tableId) {
    TestScanEnv env;
    init_FSMMapAll(&env.fsmMapAll);
    fsm_cache_init(&env.fsmCache);
    mvcc_init(&env.mvcc);
    initializeBuffors(&env.buffors, 5);

    addTable(&env.fsmMapAll, &env.buffors, &env.fsmCache, &env.mvcc, tableId,
             (int8_t[]){ID_INT32, ID_STRING}, (int8_t[]){0, 0},
             (char[2][MAX_COL_NAME_LEN]){{"id"}, {"name"}});

    addTuple(&env.buffors, &env.fsmCache, &env.fsmMapAll, &env.mvcc, tableId,
             (AllVar[]){all_var_from_int32(1), all_var_from_string("Alpha")},
             2, (int8_t[]){0, 0}, 2);
    addTuple(&env.buffors, &env.fsmCache, &env.fsmMapAll, &env.mvcc, tableId,
             (AllVar[]){all_var_from_int32(2), all_var_from_string("Beta")},
             2, (int8_t[]){0, 0}, 2);
    addTuple(&env.buffors, &env.fsmCache, &env.fsmMapAll, &env.mvcc, tableId,
             (AllVar[]){all_var_from_int32(3), all_var_from_string("Gamma")},
             2, (int8_t[]){0, 0}, 2);

    return env;
}

static void test_fullScan_select(void **state) {
    (void)state;
    int32_t tableId = 30;
    TestScanEnv env = setup_scan_env(tableId);

    Transaction txn;
    beginTransaction(&env.mvcc, &txn);

    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {0, 1};
    Qeselect(&qe, cols, 2);
    Qefrom(&qe, tableId);
    Transaction *ptxn = &txn;
    Qeend(&qe, &ptxn, &env.fsmCache);

    ResultTuple result = {0};
    FullScan fs = {.qe = &qe, .rt = &result};
    fullScan(&fs, &env.buffors);

    assert_int_equal(result.tuple_count, 3);
    assert_int_equal(result.tuples[0].dnb.data[0].val.i32, 1);
    assert_string_equal(result.tuples[0].dnb.data[1].val.str, "Alpha");
    assert_int_equal(result.tuples[1].dnb.data[0].val.i32, 2);
    assert_string_equal(result.tuples[1].dnb.data[1].val.str, "Beta");
    assert_int_equal(result.tuples[2].dnb.data[0].val.i32, 3);
    assert_string_equal(result.tuples[2].dnb.data[1].val.str, "Gamma");

    free_test_buffors(&env.buffors);
    fsm_cache_free(&env.fsmCache);
}

static void test_fullScan_where(void **state) {
    (void)state;
    int32_t tableId = 31;
    TestScanEnv env = setup_scan_env(tableId);

    Transaction txn;
    beginTransaction(&env.mvcc, &txn);

    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {0};
    AllVar vals[MAX_COLUMNS] = {all_var_from_int32(2)};
    Qewhere(&qe, cols, vals, 1);
    Qefrom(&qe, tableId);
    Transaction *ptxn = &txn;
    Qeend(&qe, &ptxn, &env.fsmCache);

    ResultTuple result = {0};
    FullScan fs = {.qe = &qe, .rt = &result};
    fullScan(&fs, &env.buffors);

    assert_int_equal(result.tuple_count, 1);
    assert_int_equal(result.tuples[0].dnb.data[0].val.i32, 2);
    assert_string_equal(result.tuples[0].dnb.data[1].val.str, "Beta");

    free_test_buffors(&env.buffors);
    fsm_cache_free(&env.fsmCache);
}

static void test_fullScanWithUpdate_select(void **state) {
    (void)state;
    int32_t tableId = 32;
    TestScanEnv env = setup_scan_env(tableId);

    Transaction txn;
    beginTransaction(&env.mvcc, &txn);

    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {0, 1};
    Qeselect(&qe, cols, 2);
    Qefrom(&qe, tableId);
    Transaction *ptxn = &txn;
    Qeend(&qe, &ptxn, &env.fsmCache);

    ResultTuple result = {0};
    FullScan fs = {.qe = &qe, .rt = &result};
    fullScanWithUpdate(&fs, &env.buffors);

    assert_int_equal(result.tuple_count, 3);
    assert_int_equal(result.tuples[0].dnb.data[0].val.i32, 1);
    assert_string_equal(result.tuples[0].dnb.data[1].val.str, "Alpha");
    assert_int_equal(result.tuples[1].dnb.data[0].val.i32, 2);
    assert_string_equal(result.tuples[1].dnb.data[1].val.str, "Beta");
    assert_int_equal(result.tuples[2].dnb.data[0].val.i32, 3);
    assert_string_equal(result.tuples[2].dnb.data[1].val.str, "Gamma");

    free_test_buffors(&env.buffors);
    fsm_cache_free(&env.fsmCache);
}

static void test_fullScanWithUpdate_where(void **state) {
    (void)state;
    int32_t tableId = 33;
    TestScanEnv env = setup_scan_env(tableId);

    Transaction txn;
    beginTransaction(&env.mvcc, &txn);

    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {1};
    AllVar vals[MAX_COLUMNS] = {all_var_from_string("Gamma")};
    Qewhere(&qe, cols, vals, 1);
    Qefrom(&qe, tableId);
    Transaction *ptxn = &txn;
    Qeend(&qe, &ptxn, &env.fsmCache);

    ResultTuple result = {0};
    FullScan fs = {.qe = &qe, .rt = &result};
    fullScanWithUpdate(&fs, &env.buffors);

    assert_int_equal(result.tuple_count, 1);
    assert_int_equal(result.tuples[0].dnb.data[0].val.i32, 3);
    assert_string_equal(result.tuples[0].dnb.data[1].val.str, "Gamma");

    free_test_buffors(&env.buffors);
    fsm_cache_free(&env.fsmCache);
}

/* -------------------------------------------------------------------------
 * Main test runner
 * ------------------------------------------------------------------------- */

int main(void) {
    const struct CMUnitTest tests[] = {
        // NextData struct test
        cmocka_unit_test(test_nextData_struct),

        // passIsolation tests
        cmocka_unit_test(test_passIsolation_visible_created_in_past),
        cmocka_unit_test(test_passIsolation_visible_created_in_current_txn),
        cmocka_unit_test(test_passIsolation_invisible_created_in_future),
        cmocka_unit_test(test_passIsolation_invisible_deleted_in_past),
        cmocka_unit_test(test_passIsolation_invisible_deleted_in_current_txn),
        cmocka_unit_test(test_passIsolation_visible_deleted_in_future),

        // whereIf tests
        cmocka_unit_test(test_whereIf_int32_match),
        cmocka_unit_test(test_whereIf_string_match),
        cmocka_unit_test(test_whereIf_int64_match),
        cmocka_unit_test(test_whereIf_type_mismatch),
        cmocka_unit_test(test_whereIf_result_space_limit),
        cmocka_unit_test(test_whereIf_multiple_conditions),

        // selectIf tests
        cmocka_unit_test(test_selectIf_all_columns),
        cmocka_unit_test(test_selectIf_subset_columns),
        cmocka_unit_test(test_selectIf_reordered_columns),
        cmocka_unit_test(test_selectIf_multiple_calls),

        // parserCommands tests
        cmocka_unit_test(test_parserCommands_where_mode),
        cmocka_unit_test(test_parserCommands_select_mode),
        cmocka_unit_test(test_parserCommands_with_isolation),
        cmocka_unit_test(test_parserCommands_starti_offset),
        cmocka_unit_test(test_parserCommands_empty_block),

        // parserWithUpdate tests
        cmocka_unit_test(test_parserWithUpdate_where_mode),
        cmocka_unit_test(test_parserWithUpdate_select_mode),

        // fullScan & fullScanWithUpdate tests
        cmocka_unit_test(test_fullScan_select),
        cmocka_unit_test(test_fullScan_where),
        cmocka_unit_test(test_fullScanWithUpdate_select),
        cmocka_unit_test(test_fullScanWithUpdate_where),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
