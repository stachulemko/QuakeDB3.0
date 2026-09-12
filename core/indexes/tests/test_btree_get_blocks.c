/*
 * test_btree_get_blocks.c
 *
 * Tests for getBlocksBtree — returns all blockIds where a value exists.
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "../../bufforing-stm/bufforing-stm/dataBuffor.h"
#include "../../memory-mgmt/memory-mgmt/all_var.h"
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

static int has_block(BtreeBlocksResult *r, int32_t blockId) {
    for (int i = 0; i < r->count; i++) {
        if (r->blocks[i] == blockId) return 1;
    }
    return 0;
}

/* ============================================================================
 *  1. BASIC — single value, single block
 * ============================================================================ */

void test_get_blocks_single(void) {
    TEST_START("single value in one block");
    Env e;
    env_init(&e, 700, 0);

    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);

    BtreeBlocksResult res = getBlocksBtree(all_var_from_int32(10), &e.bufs, e.tableId, e.colIdx, &e.fsm, 0, 0);
    TEST_ASSERT(res.count == 1, "should find 1 block");
    TEST_ASSERT(res.blocks[0] == 5, "blockId should be 5");
    free(res.blocks);
    TEST_PASS();
}

void test_get_blocks_not_found(void) {
    TEST_START("value not in tree returns empty");
    Env e;
    env_init(&e, 701, 0);

    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);

    BtreeBlocksResult res = getBlocksBtree(all_var_from_int32(99), &e.bufs, e.tableId, e.colIdx, &e.fsm, 0, 0);
    TEST_ASSERT(res.count == 0, "should find 0 blocks");
    free(res.blocks);
    TEST_PASS();
}

void test_get_blocks_empty_tree(void) {
    TEST_START("empty tree returns empty");
    Env e;
    env_init(&e, 702, 0);

    BtreeBlocksResult res = getBlocksBtree(all_var_from_int32(10), &e.bufs, e.tableId, e.colIdx, &e.fsm, 0, 0);
    TEST_ASSERT(res.count == 0, "should find 0 blocks");
    free(res.blocks);
    TEST_PASS();
}

/* ============================================================================
 *  2. MULTIPLE BLOCKS — same value in different blocks
 * ============================================================================ */

void test_get_blocks_two_different_blocks(void) {
    TEST_START("same value in 2 different blocks");
    Env e;
    env_init(&e, 710, 0);

    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 7, &e.bufs, e.tableId, e.colIdx, &e.fsm);

    BtreeBlocksResult res = getBlocksBtree(all_var_from_int32(10), &e.bufs, e.tableId, e.colIdx, &e.fsm, 0, 0);
    TEST_ASSERT(res.count == 2, "should find 2 blocks");
    TEST_ASSERT(has_block(&res, 5), "should contain blockId 5");
    TEST_ASSERT(has_block(&res, 7), "should contain blockId 7");
    free(res.blocks);
    TEST_PASS();
}

void test_get_blocks_three_different_blocks(void) {
    TEST_START("same value in 3 different blocks");
    Env e;
    env_init(&e, 711, 0);

    addToBtree(all_var_from_int32(10), 2, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 9, &e.bufs, e.tableId, e.colIdx, &e.fsm);

    BtreeBlocksResult res = getBlocksBtree(all_var_from_int32(10), &e.bufs, e.tableId, e.colIdx, &e.fsm, 0, 0);
    TEST_ASSERT(res.count == 3, "should find 3 blocks");
    TEST_ASSERT(has_block(&res, 2), "should contain blockId 2");
    TEST_ASSERT(has_block(&res, 5), "should contain blockId 5");
    TEST_ASSERT(has_block(&res, 9), "should contain blockId 9");
    free(res.blocks);
    TEST_PASS();
}

/* ============================================================================
 *  3. AFTER DELETE — blocks removed from result
 * ============================================================================ */

void test_get_blocks_after_delete_one(void) {
    TEST_START("delete one block, other remains");
    Env e;
    env_init(&e, 720, 0);

    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 7, &e.bufs, e.tableId, e.colIdx, &e.fsm);

    deleteVal(&e.fsm, &e.bufs, e.tableId, e.colIdx, 7, all_var_from_int32(10));

    BtreeBlocksResult res = getBlocksBtree(all_var_from_int32(10), &e.bufs, e.tableId, e.colIdx, &e.fsm, 0, 0);
    TEST_ASSERT(res.count == 1, "should find 1 block after delete");
    TEST_ASSERT(res.blocks[0] == 5, "remaining block should be 5");
    free(res.blocks);
    TEST_PASS();
}

void test_get_blocks_after_delete_all(void) {
    TEST_START("delete all blocks, result empty");
    Env e;
    env_init(&e, 721, 0);

    addToBtree(all_var_from_int32(10), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 7, &e.bufs, e.tableId, e.colIdx, &e.fsm);

    deleteVal(&e.fsm, &e.bufs, e.tableId, e.colIdx, 5, all_var_from_int32(10));
    deleteVal(&e.fsm, &e.bufs, e.tableId, e.colIdx, 7, all_var_from_int32(10));

    BtreeBlocksResult res = getBlocksBtree(all_var_from_int32(10), &e.bufs, e.tableId, e.colIdx, &e.fsm, 0, 0);
    TEST_ASSERT(res.count == 0, "should find 0 blocks after deleting all");
    free(res.blocks);
    TEST_PASS();
}

/* ============================================================================
 *  4. MULTIPLE VALUES — getBlocksBtree returns only blocks for queried value
 * ============================================================================ */

void test_get_blocks_different_values_isolated(void) {
    TEST_START("different values have separate block lists");
    Env e;
    env_init(&e, 730, 0);

    addToBtree(all_var_from_int32(10), 1, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(10), 2, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(20), 3, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(20), 4, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    addToBtree(all_var_from_int32(20), 5, &e.bufs, e.tableId, e.colIdx, &e.fsm);

    BtreeBlocksResult res10 = getBlocksBtree(all_var_from_int32(10), &e.bufs, e.tableId, e.colIdx, &e.fsm, 0, 0);
    TEST_ASSERT(res10.count == 2, "val=10 should have 2 blocks");
    TEST_ASSERT(has_block(&res10, 1), "val=10 should have block 1");
    TEST_ASSERT(has_block(&res10, 2), "val=10 should have block 2");
    free(res10.blocks);

    BtreeBlocksResult res20 = getBlocksBtree(all_var_from_int32(20), &e.bufs, e.tableId, e.colIdx, &e.fsm, 0, 0);
    TEST_ASSERT(res20.count == 3, "val=20 should have 3 blocks");
    TEST_ASSERT(has_block(&res20, 3), "val=20 should have block 3");
    TEST_ASSERT(has_block(&res20, 4), "val=20 should have block 4");
    TEST_ASSERT(has_block(&res20, 5), "val=20 should have block 5");
    free(res20.blocks);

    TEST_PASS();
}

/* ============================================================================
 *  5. MANY VALUES — stress test with splits
 * ============================================================================ */

void test_get_blocks_many_values_stress(void) {
    TEST_START("stress: 50 values, each in unique block, query each");
    Env e;
    env_init(&e, 740, 0);

    for (int i = 0; i < 50; i++) {
        addToBtree(all_var_from_int32(i), i + 100, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    }

    int ok = 1;
    for (int i = 0; i < 50; i++) {
        BtreeBlocksResult res = getBlocksBtree(all_var_from_int32(i), &e.bufs, e.tableId, e.colIdx, &e.fsm, 0, 0);
        if (res.count != 1 || res.blocks[0] != i + 100) {
            ok = 0;
            free(res.blocks);
            break;
        }
        free(res.blocks);
    }
    TEST_ASSERT(ok, "each value should map to its unique block");
    TEST_PASS();
}

void test_get_blocks_same_value_many_blocks(void) {
    TEST_START("stress: 1 value in 10 different blocks");
    Env e;
    env_init(&e, 741, 0);

    for (int i = 0; i < 10; i++) {
        addToBtree(all_var_from_int32(42), i + 1, &e.bufs, e.tableId, e.colIdx, &e.fsm);
    }

    BtreeBlocksResult res = getBlocksBtree(all_var_from_int32(42), &e.bufs, e.tableId, e.colIdx, &e.fsm, 0, 0);
    TEST_ASSERT(res.count == 10, "should find 10 blocks");
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT(has_block(&res, i + 1), "should contain each blockId");
    }
    free(res.blocks);
    TEST_PASS();
}

/* ============================================================================
 *  Main
 * ============================================================================ */

int main(void) {
    SECTION("1. BASIC");
    test_get_blocks_single();
    test_get_blocks_not_found();
    test_get_blocks_empty_tree();

    SECTION("2. MULTIPLE BLOCKS");
    test_get_blocks_two_different_blocks();
    test_get_blocks_three_different_blocks();

    SECTION("3. AFTER DELETE");
    test_get_blocks_after_delete_one();
    test_get_blocks_after_delete_all();

    SECTION("4. ISOLATION");
    test_get_blocks_different_values_isolated();

    SECTION("5. STRESS");
    test_get_blocks_many_values_stress();
    test_get_blocks_same_value_many_blocks();

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
