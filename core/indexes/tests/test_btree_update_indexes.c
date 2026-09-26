/*
 * test_btree_update_indexes.c
 *
 * Component tests for btree_update_tuple_indexes():
 *   - removes the old tuple's old values from the indexes
 *   - inserts the new tuple's new values into the indexes
 *   - skips columns without an index
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "../../bufforing-stm/bufforing-stm/dataBuffor.h"
#include "../../memory-mgmt/memory-mgmt/all_var.h"
#include "../../memory-mgmt/memory-mgmt/tuple.h"
#include "../indexes/btreeFileOperation.h"

/* ============================================================================
 *  Test infrastructure (same style as test_btree_operations.c)
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) \
    printf("  [TEST] %-65s ", name); fflush(stdout);

#define TEST_PASS() \
    do { printf("\033[32mPASS\033[0m\n"); tests_passed++; } while(0)

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("\033[31mFAIL\033[0m  (%s, line %d)\n", msg, __LINE__); \
            tests_failed++; \
            return; \
        } \
    } while(0)

#define SECTION(name) \
    printf("\n\033[1;36m══════ %s ══════\033[0m\n", name);

/* ============================================================================
 *  Helpers
 * ============================================================================ */

typedef struct {
    FSMMapBtree  fsm;
    BtreeBuffors bufs;
    int32_t      tableId;
} Env;

/* Environment setup with an index on column col */
static void env_init1(Env *e, int32_t tableId, int32_t col) {
    memset(e, 0, sizeof(Env));
    fsm_btree_init(&e->fsm);
    initBtreeBuffors(&e->bufs, 20);
    e->tableId = tableId;
    createBtree(&e->fsm, tableId, col);
}

/* Setup with indexes on two columns */
static void env_init2(Env *e, int32_t tableId) {
    memset(e, 0, sizeof(Env));
    fsm_btree_init(&e->fsm);
    initBtreeBuffors(&e->bufs, 30);
    e->tableId = tableId;
    createBtree(&e->fsm, tableId, 0);
    createBtree(&e->fsm, tableId, 1);
}

static int32_t env_find(Env *e, int32_t col, int32_t val) {
    return getBlockBtree(all_var_from_int32(val), &e->bufs, e->tableId, col, &e->fsm, 0, 0);
}

/* Builds a single-column Tuple with an int32 value */
static Tuple make_tuple1(int32_t v0) {
    Tuple t = {0};
    t.dnb.data_count = 1;
    t.dnb.data[0]    = all_var_from_int32(v0);
    return t;
}

/* Builds a two-column Tuple with int32 values */
static Tuple make_tuple2(int32_t v0, int32_t v1) {
    Tuple t = {0};
    t.dnb.data_count = 2;
    t.dnb.data[0]    = all_var_from_int32(v0);
    t.dnb.data[1]    = all_var_from_int32(v1);
    return t;
}

/* ============================================================================
 *  btree_delete_tuple_indexes — testy
 * ============================================================================ */

void test_delete_removes_all_indexed_columns(void) {
    TEST_START("delete_tuple_indexes: usuwa wszystkie indeksowane kolumny");
    Env e;
    env_init2(&e, 300);

    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, 0, &e.fsm);
    addToBtree(all_var_from_int32(20), 5, &e.bufs, e.tableId, 1, &e.fsm);

    Tuple t = make_tuple2(10, 20);
    btree_delete_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &t, 5);

    TEST_ASSERT(env_find(&e, 0, 10) == -1, "col0: val=10 powinna byc usunieta");
    TEST_ASSERT(env_find(&e, 1, 20) == -1, "col1: val=20 powinna byc usunieta");
    TEST_PASS();
}

void test_delete_skips_non_indexed_column(void) {
    TEST_START("delete_tuple_indexes: pomija kolumne bez indeksu");
    Env e;
    /* only col=0 has an index */
    env_init1(&e, 301, 0);

    addToBtree(all_var_from_int32(77), 3, &e.bufs, e.tableId, 0, &e.fsm);

    /* 2-column tuple, col=1 without an index */
    Tuple t = make_tuple2(77, 88);
    btree_delete_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &t, 3);

    TEST_ASSERT(env_find(&e, 0, 77) == -1, "col0: val=77 usunieta");
    /* col=1 — no index, no crash */
    TEST_PASS();
}

void test_delete_null_guards(void) {
    TEST_START("delete_tuple_indexes: NULL args bez crasha");
    Env e;
    env_init1(&e, 302, 0);
    Tuple t = make_tuple1(5);

    btree_delete_tuple_indexes(NULL,   &e.bufs, e.tableId, &t, 1);
    btree_delete_tuple_indexes(&e.fsm, NULL,    e.tableId, &t, 1);
    btree_delete_tuple_indexes(&e.fsm, &e.bufs, e.tableId, NULL, 1);
    TEST_PASS();
}

/* ============================================================================
 *  btree_insert_tuple_indexes — testy
 * ============================================================================ */

void test_insert_adds_all_indexed_columns(void) {
    TEST_START("insert_tuple_indexes: wstawia wszystkie indeksowane kolumny");
    Env e;
    env_init2(&e, 310);

    Tuple t = make_tuple2(50, 60);
    btree_insert_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &t, 8);

    TEST_ASSERT(env_find(&e, 0, 50) == 8, "col0: val=50 w bloku 8");
    TEST_ASSERT(env_find(&e, 1, 60) == 8, "col1: val=60 w bloku 8");
    TEST_PASS();
}

void test_insert_skips_non_indexed_column(void) {
    TEST_START("insert_tuple_indexes: pomija kolumne bez indeksu");
    Env e;
    /* only col=0 has an index */
    env_init1(&e, 311, 0);

    Tuple t = make_tuple2(33, 44);
    btree_insert_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &t, 2);

    TEST_ASSERT(env_find(&e, 0, 33) == 2, "col0: val=33 wstawiona");
    /* col=1 without an index — no crash */
    TEST_PASS();
}

void test_insert_null_guards(void) {
    TEST_START("insert_tuple_indexes: NULL args bez crasha");
    Env e;
    env_init1(&e, 312, 0);
    Tuple t = make_tuple1(5);

    btree_insert_tuple_indexes(NULL,   &e.bufs, e.tableId, &t, 1);
    btree_insert_tuple_indexes(&e.fsm, NULL,    e.tableId, &t, 1);
    btree_insert_tuple_indexes(&e.fsm, &e.bufs, e.tableId, NULL, 1);
    TEST_PASS();
}

/* ============================================================================
 *  Test 1 — value change: old value removed, new one available
 * ============================================================================ */

void test_update_value_old_removed_new_found(void) {
    TEST_START("update value: old removed from index, new found");
    Env e;
    env_init1(&e, 200, 0);

    /* insert val=10 in block 5 */
    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, 0, &e.fsm);
    TEST_ASSERT(env_find(&e, 0, 10) == 5, "przed update: val=10 powinno byc w bloku 5");

    Tuple oldT = make_tuple1(10);
    Tuple newT = make_tuple1(99);
    btree_update_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &oldT, 5, &newT, 5);

    TEST_ASSERT(env_find(&e, 0, 10) == -1, "po update: val=10 nie powinno istniec");
    TEST_ASSERT(env_find(&e, 0, 99) ==  5, "po update: val=99 powinno byc w bloku 5");
    TEST_PASS();
}

/* ============================================================================
 *  Test 2 — block change: same value, different block
 * ============================================================================ */

void test_update_block_same_value_new_block(void) {
    TEST_START("update block: same value moved to new block");
    Env e;
    env_init1(&e, 201, 0);

    addToBtree(all_var_from_int32(42), 5, &e.bufs, e.tableId, 0, &e.fsm);
    TEST_ASSERT(env_find(&e, 0, 42) == 5, "przed update: val=42 w bloku 5");

    Tuple oldT = make_tuple1(42);
    Tuple newT = make_tuple1(42);
    btree_update_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &oldT, 5, &newT, 7);

    TEST_ASSERT(env_find(&e, 0, 42) == 7, "po update: val=42 powinno byc w bloku 7");
    TEST_PASS();
}

/* ============================================================================
 *  Test 3 — column without an index: no crash, the indexed column works
 * ============================================================================ */

void test_update_column_without_index_no_crash(void) {
    TEST_START("update tuple with non-indexed column: no crash, indexed col works");
    Env e;
    /* only column 0 has an index, column 1 does not */
    env_init1(&e, 202, 0);

    /* insert the old tuple (col0=10, col1=20) manually into the col0 index only */
    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, 0, &e.fsm);

    Tuple oldT = make_tuple2(10, 20);
    Tuple newT = make_tuple2(55, 88);
    /* should not crash despite the missing index on col=1 */
    btree_update_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &oldT, 5, &newT, 5);

    TEST_ASSERT(env_find(&e, 0, 10) == -1, "col0: stara wartosc usunieta");
    TEST_ASSERT(env_find(&e, 0, 55) ==  5, "col0: nowa wartosc dostepna");
    TEST_PASS();
}

/* ============================================================================
 *  Test 4 — two indexed columns: both updated correctly
 * ============================================================================ */

void test_update_two_indexed_columns(void) {
    TEST_START("update two indexed columns: both old removed, both new found");
    Env e;
    env_init2(&e, 203);

    /* insert the old values into both indexes */
    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, 0, &e.fsm);
    addToBtree(all_var_from_int32(20), 5, &e.bufs, e.tableId, 1, &e.fsm);

    Tuple oldT = make_tuple2(10, 20);
    Tuple newT = make_tuple2(100, 200);
    btree_update_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &oldT, 5, &newT, 9);

    TEST_ASSERT(env_find(&e, 0, 10)  == -1,  "col0: stara val=10 usunieta");
    TEST_ASSERT(env_find(&e, 0, 100) ==  9,  "col0: nowa val=100 w bloku 9");
    TEST_ASSERT(env_find(&e, 1, 20)  == -1,  "col1: stara val=20 usunieta");
    TEST_ASSERT(env_find(&e, 1, 200) ==  9,  "col1: nowa val=200 w bloku 9");
    TEST_PASS();
}

/* ============================================================================
 *  Test 5 — repeated updates of the same tuple (chain of changes)
 * ============================================================================ */

void test_update_chain_of_updates(void) {
    TEST_START("chain of updates: each version replaces the previous");
    Env e;
    env_init1(&e, 204, 0);

    addToBtree(all_var_from_int32(1), 1, &e.bufs, e.tableId, 0, &e.fsm);

    /* 1→10 */
    { Tuple o = make_tuple1(1);  Tuple n = make_tuple1(10);
      btree_update_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &o, 1, &n, 2); }
    TEST_ASSERT(env_find(&e, 0,  1) == -1, "po 1->10: val=1  niewidoczna");
    TEST_ASSERT(env_find(&e, 0, 10) ==  2, "po 1->10: val=10 w bloku 2");

    /* 10→100 */
    { Tuple o = make_tuple1(10);  Tuple n = make_tuple1(100);
      btree_update_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &o, 2, &n, 3); }
    TEST_ASSERT(env_find(&e, 0,  10) == -1, "po 10->100: val=10  niewidoczna");
    TEST_ASSERT(env_find(&e, 0, 100) ==  3, "po 10->100: val=100 w bloku 3");

    /* 100→1000 */
    { Tuple o = make_tuple1(100);  Tuple n = make_tuple1(1000);
      btree_update_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &o, 3, &n, 4); }
    TEST_ASSERT(env_find(&e, 0,  100) == -1, "po 100->1000: val=100  niewidoczna");
    TEST_ASSERT(env_find(&e, 0, 1000) ==  4, "po 100->1000: val=1000 w bloku 4");

    TEST_PASS();
}

/* ============================================================================
 *  Test 6 — NULL guard: no crashes with NULL pointers
 * ============================================================================ */

void test_update_null_guards(void) {
    TEST_START("null guards: NULL args handled without crash");
    Env e;
    env_init1(&e, 205, 0);

    Tuple t = make_tuple1(10);

    /* all NULL combinations — none should crash */
    btree_update_tuple_indexes(NULL,     &e.bufs, e.tableId, &t, 1, &t, 2);
    btree_update_tuple_indexes(&e.fsm,   NULL,    e.tableId, &t, 1, &t, 2);
    btree_update_tuple_indexes(&e.fsm,   &e.bufs, e.tableId, NULL, 1, &t, 2);
    btree_update_tuple_indexes(&e.fsm,   &e.bufs, e.tableId, &t, 1, NULL, 2);

    TEST_PASS();
}

/* ============================================================================
 *  Test 7 — table missing in FSM: no crash
 * ============================================================================ */

void test_update_table_not_in_fsm(void) {
    TEST_START("table not in FSM: no crash");
    Env e;
    env_init1(&e, 206, 0);

    Tuple oldT = make_tuple1(5);
    Tuple newT = make_tuple1(50);

    /* tableId=999 has no index — should return silently */
    btree_update_tuple_indexes(&e.fsm, &e.bufs, 999, &oldT, 1, &newT, 2);

    TEST_PASS();
}

/* ============================================================================
 *  Main
 * ============================================================================ */

int main(void) {
    SECTION("btree_delete_tuple_indexes");
    test_delete_removes_all_indexed_columns();
    test_delete_skips_non_indexed_column();
    test_delete_null_guards();

    SECTION("btree_insert_tuple_indexes");
    test_insert_adds_all_indexed_columns();
    test_insert_skips_non_indexed_column();
    test_insert_null_guards();

    SECTION("btree_update_tuple_indexes");
    test_update_value_old_removed_new_found();
    test_update_block_same_value_new_block();
    test_update_column_without_index_no_crash();
    test_update_two_indexed_columns();
    test_update_chain_of_updates();
    test_update_null_guards();
    test_update_table_not_in_fsm();

    printf("\n");
    if (tests_failed == 0) {
        printf("\033[32m[OK] Wszystkie %d testy przeszly.\033[0m\n", tests_passed);
        return 0;
    } else {
        printf("\033[31m[FAIL] %d/%d testow nieudanych.\033[0m\n",
               tests_failed, tests_passed + tests_failed);
        return 1;
    }
}