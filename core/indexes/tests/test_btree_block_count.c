/*
 * test_btree_block_count.c
 *
 * Tests for block entry count functionality:
 *   - adding same blockId increments count instead of creating new entry
 *   - deleting decrements count, only removes entry when count == 0
 *   - mixed scenarios with multiple values and blockIds
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "../../bufforing-stm/bufforing-stm/dataBuffor.h"
#include "../../memory-mgmt/memory-mgmt/all_var.h"
#include "../../memory-mgmt/memory-mgmt/tuple.h"
#include "../indexes/btreeFileOperation.h"

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

typedef struct {
    FSMMapBtree  fsm;
    BtreeBuffors bufs;
    int32_t      tableId;
    int32_t      colIdx;
} Env;

static void env_init(Env *e, int32_t tableId, int32_t col) {
    memset(e, 0, sizeof(Env));
    fsm_btree_init(&e->fsm);
    initBtreeBuffors(&e->bufs, 20);
    e->tableId = tableId;
    e->colIdx = col;
    createBtree(&e->fsm, tableId, col);
}

static int32_t env_find(Env *e, int32_t val) {
    return getBlockBtree(all_var_from_int32(val), &e->bufs, e->tableId, e->colIdx, &e->fsm, 0, 0);
}

/* Helper: read the count of a block entry for a given value's first block entry */
static int32_t read_block_count(Env *e, int32_t val) {
    AllVar v = all_var_from_int32(val);
    BtreeDeleteInfo info = findValueInLeaf(0, &v, e->tableId, e->colIdx, &e->bufs);
    if (!info.found || info.ptrToBlocks == -1) return -1;

    int32_t block = calculateBlock(info.ptrToBlocks);
    BtreeBuffor *buffor = getBtreeBuffor(e->tableId, e->colIdx, block, &e->bufs);
    if (buffor == NULL) return -1;

    int32_t inBlock = info.ptrToBlocks % BLOCK_SIZE;
    int32_t count = 0;
    unmarshal_int32(&count, buffor->buf + inBlock + sizeof(int32_t));
    buffor->pinCount = 0;
    return count;
}

/* ============================================================================
 *  1. COUNT INCREMENT TESTS
 * ============================================================================ */

void test_single_add_count_is_1(void) {
    TEST_START("single add: count == 1");
    Env e;
    env_init(&e, 500, 0);

    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);

    TEST_ASSERT(env_find(&e, 10) == 5, "blockId 5 should be found");
    TEST_ASSERT(read_block_count(&e, 10) == 1, "count should be 1 after single add");
    TEST_PASS();
}

void test_double_add_same_block_count_is_2(void) {
    TEST_START("double add same blockId: count == 2");
    Env e;
    env_init(&e, 501, 0);

    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);

    TEST_ASSERT(env_find(&e, 10) == 5, "blockId 5 should be found");
    TEST_ASSERT(read_block_count(&e, 10) == 2, "count should be 2 after double add");
    TEST_PASS();
}

void test_triple_add_same_block_count_is_3(void) {
    TEST_START("triple add same blockId: count == 3");
    Env e;
    env_init(&e, 502, 0);

    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);

    TEST_ASSERT(env_find(&e, 10) == 5, "blockId 5 should be found");
    TEST_ASSERT(read_block_count(&e, 10) == 3, "count should be 3 after triple add");
    TEST_PASS();
}

void test_add_different_blocks_separate_counts(void) {
    TEST_START("add different blockIds: separate counts");
    Env e;
    env_init(&e, 503, 0);

    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 7, &e.bufs, e.tableId, e.colIdx, &e.fsm);

    TEST_ASSERT(env_find(&e, 10) == 5, "first blockId should be found");
    TEST_ASSERT(read_block_count(&e, 10) == 1, "first block count should be 1");
    TEST_PASS();
}

/* ============================================================================
 *  2. COUNT DECREMENT TESTS
 * ============================================================================ */

void test_delete_from_count_2_keeps_entry(void) {
    TEST_START("delete from count=2: entry survives with count=1");
    Env e;
    env_init(&e, 510, 0);

    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    TEST_ASSERT(read_block_count(&e, 10) == 2, "count should be 2 before delete");

    deleteVal(&e.fsm, &e.bufs, e.tableId, e.colIdx, 5, all_var_from_int32(10));

    TEST_ASSERT(env_find(&e, 10) == 5, "blockId 5 should still be findable");
    TEST_ASSERT(read_block_count(&e, 10) == 1, "count should be 1 after one delete");
    TEST_PASS();
}

void test_delete_from_count_3_step_by_step(void) {
    TEST_START("delete from count=3: step-by-step decrement");
    Env e;
    env_init(&e, 511, 0);

    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    TEST_ASSERT(read_block_count(&e, 10) == 3, "count should be 3");

    deleteVal(&e.fsm, &e.bufs, e.tableId, e.colIdx, 5, all_var_from_int32(10));
    TEST_ASSERT(read_block_count(&e, 10) == 2, "count should be 2 after first delete");
    TEST_ASSERT(env_find(&e, 10) == 5, "blockId 5 still findable");

    deleteVal(&e.fsm, &e.bufs, e.tableId, e.colIdx, 5, all_var_from_int32(10));
    TEST_ASSERT(read_block_count(&e, 10) == 1, "count should be 1 after second delete");
    TEST_ASSERT(env_find(&e, 10) == 5, "blockId 5 still findable");

    deleteVal(&e.fsm, &e.bufs, e.tableId, e.colIdx, 5, all_var_from_int32(10));
    TEST_ASSERT(env_find(&e, 10) == -1, "value should be gone after third delete (count=0)");
    TEST_PASS();
}

void test_delete_count_1_removes_entry(void) {
    TEST_START("delete from count=1: entry removed completely");
    Env e;
    env_init(&e, 512, 0);

    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    TEST_ASSERT(read_block_count(&e, 10) == 1, "count should be 1");

    deleteVal(&e.fsm, &e.bufs, e.tableId, e.colIdx, 5, all_var_from_int32(10));
    TEST_ASSERT(env_find(&e, 10) == -1, "value should be gone");
    TEST_PASS();
}

/* ============================================================================
 *  3. MIXED SCENARIOS
 * ============================================================================ */

void test_two_blocks_one_counted(void) {
    TEST_START("two blocks: one with count=2, delete one keeps other");
    Env e;
    env_init(&e, 520, 0);

    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 7, &e.bufs, e.tableId, e.colIdx, &e.fsm);

    /* Delete blockId=7 (count=1) -> removed, blockId=5 (count=2) remains */
    deleteVal(&e.fsm, &e.bufs, e.tableId, e.colIdx, 7, all_var_from_int32(10));
    TEST_ASSERT(env_find(&e, 10) == 5, "blockId 5 should remain");
    TEST_ASSERT(read_block_count(&e, 10) == 2, "blockId 5 count should still be 2");
    TEST_PASS();
}

void test_insert_tuple_indexes_increments_count(void) {
    TEST_START("btree_insert_tuple_indexes: same block increments count");
    Env e;
    env_init(&e, 530, 0);

    Tuple t1 = {0};
    t1.dnb.data_count = 1;
    t1.dnb.data[0] = all_var_from_int32(42);

    Tuple t2 = {0};
    t2.dnb.data_count = 1;
    t2.dnb.data[0] = all_var_from_int32(42);

    btree_insert_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &t1, 3);
    TEST_ASSERT(read_block_count(&e, 42) == 1, "count 1 after first insert");

    btree_insert_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &t2, 3);
    TEST_ASSERT(read_block_count(&e, 42) == 2, "count 2 after second insert same block");
    TEST_PASS();
}

void test_delete_tuple_indexes_decrements_count(void) {
    TEST_START("btree_delete_tuple_indexes: decrements count, removes at 0");
    Env e;
    env_init(&e, 531, 0);

    Tuple t = {0};
    t.dnb.data_count = 1;
    t.dnb.data[0] = all_var_from_int32(42);

    btree_insert_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &t, 3);
    btree_insert_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &t, 3);
    TEST_ASSERT(read_block_count(&e, 42) == 2, "count 2 before delete");

    btree_delete_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &t, 3);
    TEST_ASSERT(env_find(&e, 42) == 3, "still findable after first delete");
    TEST_ASSERT(read_block_count(&e, 42) == 1, "count 1 after first delete");

    btree_delete_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &t, 3);
    TEST_ASSERT(env_find(&e, 42) == -1, "gone after second delete");
    TEST_PASS();
}

void test_update_with_counted_blocks(void) {
    TEST_START("update: count survives update correctly");
    Env e;
    env_init(&e, 532, 0);

    /* Two tuples with val=10 in block 5 */
    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    TEST_ASSERT(read_block_count(&e, 10) == 2, "count 2 before update");

    /* Update one of them: old val=10 block=5, new val=10 block=7 */
    Tuple oldT = {0};
    oldT.dnb.data_count = 1;
    oldT.dnb.data[0] = all_var_from_int32(10);

    Tuple newT = {0};
    newT.dnb.data_count = 1;
    newT.dnb.data[0] = all_var_from_int32(10);

    btree_update_tuple_indexes(&e.fsm, &e.bufs, e.tableId, &oldT, 5, &newT, 7);

    /* block 5 count should be 1, block 7 should exist */
    TEST_ASSERT(env_find(&e, 10) == 5, "blockId 5 still findable (count was 2, now 1)");
    TEST_ASSERT(read_block_count(&e, 10) == 1, "blockId 5 count should be 1");
    TEST_PASS();
}

void test_many_adds_and_deletes_same_block(void) {
    TEST_START("stress: 10 adds then 10 deletes same blockId");
    Env e;
    env_init(&e, 540, 0);

    for (int i = 0; i < 10; i++) {
        addToBtree(all_var_from_int32(99), 3, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    }
    TEST_ASSERT(read_block_count(&e, 99) == 10, "count should be 10");

    for (int i = 0; i < 9; i++) {
        deleteVal(&e.fsm, &e.bufs, e.tableId, e.colIdx, 3, all_var_from_int32(99));
        TEST_ASSERT(env_find(&e, 99) == 3, "should still be findable");
    }
    TEST_ASSERT(read_block_count(&e, 99) == 1, "count should be 1 after 9 deletes");

    deleteVal(&e.fsm, &e.bufs, e.tableId, e.colIdx, 3, all_var_from_int32(99));
    TEST_ASSERT(env_find(&e, 99) == -1, "should be gone after 10th delete");
    TEST_PASS();
}

/* ============================================================================
 *  Main
 * ============================================================================ */

int main(void) {
    SECTION("1. COUNT INCREMENT");
    test_single_add_count_is_1();
    test_double_add_same_block_count_is_2();
    test_triple_add_same_block_count_is_3();
    test_add_different_blocks_separate_counts();

    SECTION("2. COUNT DECREMENT");
    test_delete_from_count_2_keeps_entry();
    test_delete_from_count_3_step_by_step();
    test_delete_count_1_removes_entry();

    SECTION("3. MIXED SCENARIOS");
    test_two_blocks_one_counted();
    test_insert_tuple_indexes_increments_count();
    test_delete_tuple_indexes_decrements_count();
    test_update_with_counted_blocks();
    test_many_adds_and_deletes_same_block();

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
