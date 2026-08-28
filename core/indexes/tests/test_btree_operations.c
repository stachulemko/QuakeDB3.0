
//
// Comprehensive B-tree file operation tests
// Tests: insert, search, delete (all layers), duplicate keys, edge cases
//

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "../../bufforing-stm/bufforing-stm/dataBuffor.h"
#include "../../memory-mgmt/memory-mgmt/all_var.h"
#include "../indexes/btreeFileOperation.h"

// ============================================================================
//  TEST INFRASTRUCTURE
// ============================================================================

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) \
    printf("  [TEST] %-60s ", name); fflush(stdout);

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

// ============================================================================
//  HELPER: fresh btree environment
// ============================================================================

typedef struct {
    FSMMapBtree fsm;
    BtreeBuffors bufs;
    int32_t tableId;
    int32_t colIdx;
} TestEnv;

static void env_init(TestEnv *e, int32_t tableId, int32_t colIdx, int32_t bufCount) {
    memset(e, 0, sizeof(TestEnv));
    fsm_btree_init(&e->fsm);
    initBtreeBuffors(&e->bufs, bufCount);
    e->tableId = tableId;
    e->colIdx = colIdx;
    createBtree(&e->fsm, tableId, colIdx);
}

static void env_add(TestEnv *e, int32_t intVal, int32_t blockId) {
    addToBtree(all_var_from_int32(intVal), blockId, &e->bufs, e->tableId, e->colIdx, &e->fsm);
}

static int32_t env_find(TestEnv *e, int32_t intVal) {
    return getBlockBtree(all_var_from_int32(intVal), &e->bufs, e->tableId, e->colIdx, &e->fsm, 0, 0);
}

static void env_delete(TestEnv *e, int32_t intVal, int32_t blockId) {
    deleteVal(&e->fsm, &e->bufs, e->tableId, e->colIdx, blockId, all_var_from_int32(intVal));
}

// ============================================================================
//  1. BASIC INSERT & SEARCH
// ============================================================================

void test_insert_single_value(void) {
    TEST_START("Insert single value and find it");
    TestEnv e;
    env_init(&e, 100, 0, 10);
    env_add(&e, 42, 5);
    int32_t result = env_find(&e, 42);
    TEST_ASSERT(result == 5, "Expected blockId=5");
    TEST_PASS();
}

void test_insert_multiple_unique_values(void) {
    TEST_START("Insert 10 unique values and find each");
    TestEnv e;
    env_init(&e, 101, 0, 20);
    for (int i = 1; i <= 10; i++) {
        env_add(&e, i * 10, i);
    }
    for (int i = 1; i <= 10; i++) {
        int32_t result = env_find(&e, i * 10);
        TEST_ASSERT(result == i, "Mismatch for value");
    }
    TEST_PASS();
}

void test_search_nonexistent_value(void) {
    TEST_START("Search for value that was never inserted");
    TestEnv e;
    env_init(&e, 102, 0, 10);
    env_add(&e, 10, 1);
    env_add(&e, 20, 2);
    int32_t result = env_find(&e, 999);
    TEST_ASSERT(result == -1, "Expected -1 for non-existent value");
    TEST_PASS();
}

void test_search_empty_tree(void) {
    TEST_START("Search in completely empty tree");
    TestEnv e;
    env_init(&e, 103, 0, 10);
    int32_t result = env_find(&e, 1);
    TEST_ASSERT(result == -1, "Expected -1 for empty tree");
    TEST_PASS();
}

void test_insert_duplicate_key_different_blocks(void) {
    TEST_START("Insert same key with different blockIds");
    TestEnv e;
    env_init(&e, 104, 0, 10);
    env_add(&e, 42, 10);
    env_add(&e, 42, 20);
    env_add(&e, 42, 30);
    // Should find the first block in the chain
    int32_t result = env_find(&e, 42);
    TEST_ASSERT(result != -1, "Expected to find the value");
    TEST_PASS();
}

void test_insert_negative_values(void) {
    TEST_START("Insert and find negative int32 values");
    TestEnv e;
    env_init(&e, 105, 0, 10);
    env_add(&e, -100, 1);
    env_add(&e, -50, 2);
    env_add(&e, 0, 3);
    env_add(&e, 50, 4);
    int32_t r1 = env_find(&e, -100);
    int32_t r2 = env_find(&e, -50);
    int32_t r3 = env_find(&e, 0);
    int32_t r4 = env_find(&e, 50);
    TEST_ASSERT(r1 == 1, "Expected blockId=1 for -100");
    TEST_ASSERT(r2 == 2, "Expected blockId=2 for -50");
    TEST_ASSERT(r3 == 3, "Expected blockId=3 for 0");
    TEST_ASSERT(r4 == 4, "Expected blockId=4 for 50");
    TEST_PASS();
}

void test_insert_descending_order(void) {
    TEST_START("Insert values in descending order");
    TestEnv e;
    env_init(&e, 106, 0, 20);
    for (int i = 20; i >= 1; i--) {
        env_add(&e, i, i + 100);
    }
    for (int i = 1; i <= 20; i++) {
        int32_t result = env_find(&e, i);
        TEST_ASSERT(result == i + 100, "Mismatch in descending insert");
    }
    TEST_PASS();
}

void test_insert_ascending_order(void) {
    TEST_START("Insert values in ascending order");
    TestEnv e;
    env_init(&e, 107, 0, 20);
    for (int i = 1; i <= 20; i++) {
        env_add(&e, i, i);
    }
    for (int i = 1; i <= 20; i++) {
        int32_t result = env_find(&e, i);
        TEST_ASSERT(result == i, "Mismatch in ascending insert");
    }
    TEST_PASS();
}

// ============================================================================
//  2. BASIC DELETE
// ============================================================================

void test_delete_single_value(void) {
    TEST_START("Delete the only value in tree");
    TestEnv e;
    env_init(&e, 200, 0, 10);
    env_add(&e, 42, 5);
    TEST_ASSERT(env_find(&e, 42) == 5, "Value should exist before delete");
    env_delete(&e, 42, 5);
    TEST_ASSERT(env_find(&e, 42) == -1, "Value should be gone after delete");
    TEST_PASS();
}

void test_delete_first_of_many_values(void) {
    TEST_START("Delete first value, others remain");
    TestEnv e;
    env_init(&e, 201, 0, 10);
    env_add(&e, 10, 1);
    env_add(&e, 20, 2);
    env_add(&e, 30, 3);
    env_delete(&e, 10, 1);
    TEST_ASSERT(env_find(&e, 10) == -1, "Deleted value should be gone");
    TEST_ASSERT(env_find(&e, 20) == 2, "Value 20 should remain");
    TEST_ASSERT(env_find(&e, 30) == 3, "Value 30 should remain");
    TEST_PASS();
}

void test_delete_last_of_many_values(void) {
    TEST_START("Delete last value, others remain");
    TestEnv e;
    env_init(&e, 202, 0, 10);
    env_add(&e, 10, 1);
    env_add(&e, 20, 2);
    env_add(&e, 30, 3);
    env_delete(&e, 30, 3);
    TEST_ASSERT(env_find(&e, 30) == -1, "Deleted value should be gone");
    TEST_ASSERT(env_find(&e, 10) == 1, "Value 10 should remain");
    TEST_ASSERT(env_find(&e, 20) == 2, "Value 20 should remain");
    TEST_PASS();
}

void test_delete_middle_of_many_values(void) {
    TEST_START("Delete middle value, others remain");
    TestEnv e;
    env_init(&e, 203, 0, 10);
    env_add(&e, 10, 1);
    env_add(&e, 20, 2);
    env_add(&e, 30, 3);
    env_delete(&e, 20, 2);
    TEST_ASSERT(env_find(&e, 20) == -1, "Deleted value should be gone");
    TEST_ASSERT(env_find(&e, 10) == 1, "Value 10 should remain");
    TEST_ASSERT(env_find(&e, 30) == 3, "Value 30 should remain");
    TEST_PASS();
}

void test_delete_nonexistent_value(void) {
    TEST_START("Delete value that doesn't exist (no crash)");
    TestEnv e;
    env_init(&e, 204, 0, 10);
    env_add(&e, 10, 1);
    // Should not crash
    env_delete(&e, 999, 1);
    TEST_ASSERT(env_find(&e, 10) == 1, "Original value should remain");
    TEST_PASS();
}

void test_delete_wrong_blockid(void) {
    TEST_START("Delete with wrong blockId (value survives)");
    TestEnv e;
    env_init(&e, 205, 0, 10);
    env_add(&e, 42, 5);
    env_delete(&e, 42, 999); // wrong blockId
    TEST_ASSERT(env_find(&e, 42) == 5, "Value should survive wrong blockId delete");
    TEST_PASS();
}

void test_delete_from_empty_tree(void) {
    TEST_START("Delete from empty tree (no crash)");
    TestEnv e;
    env_init(&e, 206, 0, 10);
    // Should not crash
    env_delete(&e, 42, 1);
    TEST_ASSERT(env_find(&e, 42) == -1, "Empty tree should return -1");
    TEST_PASS();
}

// ============================================================================
//  3. DUPLICATE KEY (BLOCK LIST) DELETION
// ============================================================================

void test_delete_one_block_from_duplicate_key(void) {
    TEST_START("Duplicate key: delete one block, others remain");
    TestEnv e;
    env_init(&e, 300, 0, 10);
    env_add(&e, 42, 10);
    env_add(&e, 42, 20);
    env_add(&e, 42, 30);
    // The first block in chain
    int32_t first = env_find(&e, 42);
    env_delete(&e, 42, first);
    // Value should still exist (other blocks remain)
    int32_t after = env_find(&e, 42);
    TEST_ASSERT(after != -1, "Value should still exist with remaining blocks");
    TEST_ASSERT(after != first, "Should return a different blockId now");
    TEST_PASS();
}

void test_delete_all_blocks_from_duplicate_key(void) {
    TEST_START("Duplicate key: delete all blocks one by one");
    TestEnv e;
    env_init(&e, 301, 0, 10);
    env_add(&e, 42, 10);
    env_add(&e, 42, 20);

    // Delete first
    int32_t b1 = env_find(&e, 42);
    TEST_ASSERT(b1 != -1, "Should find value");
    env_delete(&e, 42, b1);

    // Delete second
    int32_t b2 = env_find(&e, 42);
    TEST_ASSERT(b2 != -1, "Should still find value after first delete");
    env_delete(&e, 42, b2);

    // Now gone
    TEST_ASSERT(env_find(&e, 42) == -1, "Value should be gone after all blocks deleted");
    TEST_PASS();
}

void test_delete_three_blocks_from_duplicate_key(void) {
    TEST_START("Duplicate key: delete 3 blocks one by one");
    TestEnv e;
    env_init(&e, 302, 0, 10);
    env_add(&e, 100, 1);
    env_add(&e, 100, 2);
    env_add(&e, 100, 3);

    for (int i = 0; i < 3; i++) {
        int32_t b = env_find(&e, 100);
        TEST_ASSERT(b != -1, "Should find value in iteration");
        env_delete(&e, 100, b);
    }
    TEST_ASSERT(env_find(&e, 100) == -1, "All blocks deleted");
    TEST_PASS();
}

// ============================================================================
//  4. DELETE ALL VALUES
// ============================================================================

void test_delete_all_values_one_by_one(void) {
    TEST_START("Delete all 5 values one by one");
    TestEnv e;
    env_init(&e, 400, 0, 15);
    int vals[] = {10, 20, 30, 40, 50};
    for (int i = 0; i < 5; i++) {
        env_add(&e, vals[i], i + 1);
    }
    // Delete in order
    for (int i = 0; i < 5; i++) {
        int32_t b = env_find(&e, vals[i]);
        TEST_ASSERT(b == i + 1, "Should find before delete");
        env_delete(&e, vals[i], b);
        TEST_ASSERT(env_find(&e, vals[i]) == -1, "Should be gone after delete");
    }
    // All should be gone
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT(env_find(&e, vals[i]) == -1, "All should be gone at end");
    }
    TEST_PASS();
}

void test_delete_all_in_reverse_order(void) {
    TEST_START("Delete all values in reverse insertion order");
    TestEnv e;
    env_init(&e, 401, 0, 15);
    for (int i = 1; i <= 8; i++) {
        env_add(&e, i * 10, i);
    }
    // Delete in reverse
    for (int i = 8; i >= 1; i--) {
        env_delete(&e, i * 10, i);
        TEST_ASSERT(env_find(&e, i * 10) == -1, "Deleted value should be gone");
    }
    TEST_PASS();
}

void test_delete_alternating_values(void) {
    TEST_START("Delete every other value (even indices)");
    TestEnv e;
    env_init(&e, 402, 0, 15);
    for (int i = 1; i <= 10; i++) {
        env_add(&e, i, i);
    }
    // Delete even values: 2, 4, 6, 8, 10
    for (int i = 2; i <= 10; i += 2) {
        env_delete(&e, i, i);
    }
    // Odd values should remain
    for (int i = 1; i <= 10; i++) {
        int32_t result = env_find(&e, i);
        if (i % 2 == 0) {
            TEST_ASSERT(result == -1, "Even value should be deleted");
        } else {
            TEST_ASSERT(result == i, "Odd value should remain");
        }
    }
    TEST_PASS();
}

// ============================================================================
//  5. INSERT AFTER DELETE (REUSE)
// ============================================================================

void test_insert_after_delete_same_value(void) {
    TEST_START("Insert value, delete it, insert same value again");
    TestEnv e;
    env_init(&e, 500, 0, 10);
    env_add(&e, 42, 5);
    env_delete(&e, 42, 5);
    TEST_ASSERT(env_find(&e, 42) == -1, "Should be gone after delete");

    env_add(&e, 42, 99);
    TEST_ASSERT(env_find(&e, 42) == 99, "Should find re-inserted value with new blockId");
    TEST_PASS();
}

void test_insert_after_delete_different_value(void) {
    TEST_START("Delete a value, then insert a different value");
    TestEnv e;
    env_init(&e, 501, 0, 10);
    env_add(&e, 10, 1);
    env_add(&e, 20, 2);
    env_delete(&e, 10, 1);

    env_add(&e, 30, 3);
    TEST_ASSERT(env_find(&e, 10) == -1, "Deleted value should remain gone");
    TEST_ASSERT(env_find(&e, 20) == 2, "Existing value should remain");
    TEST_ASSERT(env_find(&e, 30) == 3, "New value should be findable");
    TEST_PASS();
}

void test_repeated_insert_delete_cycles(void) {
    TEST_START("Repeated insert-delete-insert cycles (5 cycles)");
    TestEnv e;
    env_init(&e, 502, 0, 15);

    for (int cycle = 0; cycle < 5; cycle++) {
        env_add(&e, 100, cycle + 1);
        TEST_ASSERT(env_find(&e, 100) != -1, "Should find during cycle");
        env_delete(&e, 100, cycle + 1);
        TEST_ASSERT(env_find(&e, 100) == -1, "Should be gone during cycle");
    }
    TEST_PASS();
}

// ============================================================================
//  6. STRESS TEST — MANY VALUES
// ============================================================================

void test_insert_50_values(void) {
    TEST_START("Insert and find 50 unique values");
    TestEnv e;
    env_init(&e, 600, 0, 30);
    for (int i = 1; i <= 50; i++) {
        env_add(&e, i * 7, i); // 7, 14, 21, ..., 350
    }
    for (int i = 1; i <= 50; i++) {
        int32_t result = env_find(&e, i * 7);
        TEST_ASSERT(result == i, "Mismatch in 50-value test");
    }
    TEST_PASS();
}

void test_insert_50_then_delete_25(void) {
    TEST_START("Insert 50 values, delete first 25, verify remaining");
    TestEnv e;
    env_init(&e, 601, 0, 30);
    for (int i = 1; i <= 50; i++) {
        env_add(&e, i, i);
    }
    // Delete values 1..25
    for (int i = 1; i <= 25; i++) {
        env_delete(&e, i, i);
    }
    // Values 1..25 should be gone
    for (int i = 1; i <= 25; i++) {
        TEST_ASSERT(env_find(&e, i) == -1, "Deleted value should be gone");
    }
    // Values 26..50 should remain
    for (int i = 26; i <= 50; i++) {
        TEST_ASSERT(env_find(&e, i) == i, "Remaining value should be found");
    }
    TEST_PASS();
}

void test_insert_delete_interleaved_50(void) {
    TEST_START("Interleaved insert/delete: add 2, delete 1 (50 ops)");
    TestEnv e;
    env_init(&e, 602, 0, 30);
    int next_val = 1;
    int last_deleted = -1;

    for (int round = 0; round < 25; round++) {
        // Insert two
        env_add(&e, next_val, next_val);
        next_val++;
        env_add(&e, next_val, next_val);
        next_val++;

        // Delete the first of the two just inserted
        int to_del = next_val - 2;
        env_delete(&e, to_del, to_del);
        last_deleted = to_del;
    }

    // The deleted ones (1, 3, 5, ..., 49) should be gone
    for (int i = 1; i <= 50; i += 2) {
        TEST_ASSERT(env_find(&e, i) == -1, "Interleaved deleted value should be gone");
    }
    // The kept ones (2, 4, 6, ..., 50) should remain
    for (int i = 2; i <= 50; i += 2) {
        TEST_ASSERT(env_find(&e, i) == i, "Interleaved kept value should remain");
    }
    TEST_PASS();
}

// ============================================================================
//  7. EDGE CASES — INTERNAL DATA STRUCTURES
// ============================================================================

void test_getData_returns_correct_size(void) {
    TEST_START("getData returns correct size after inserts");
    TestEnv e;
    env_init(&e, 700, 0, 10);
    env_add(&e, 10, 1);
    env_add(&e, 20, 2);
    env_add(&e, 30, 3);

    DataBtree *data = getData(0, e.tableId, e.colIdx, &e.bufs);
    TEST_ASSERT(data != NULL, "getData should not return NULL");
    TEST_ASSERT(data->size == 3, "Expected size=3");
    free(data);
    TEST_PASS();
}

void test_getData_returns_zero_after_all_deleted(void) {
    TEST_START("getData returns size=0 after all values deleted");
    TestEnv e;
    env_init(&e, 701, 0, 10);
    env_add(&e, 10, 1);
    env_add(&e, 20, 2);

    env_delete(&e, 10, 1);
    env_delete(&e, 20, 2);

    DataBtree *data = getData(0, e.tableId, e.colIdx, &e.bufs);
    TEST_ASSERT(data != NULL, "getData should not return NULL");
    TEST_ASSERT(data->size == 0, "Expected size=0 after all deleted");
    free(data);
    TEST_PASS();
}

void test_findValueInLeaf_found(void) {
    TEST_START("findValueInLeaf finds inserted value");
    TestEnv e;
    env_init(&e, 702, 0, 10);
    env_add(&e, 42, 7);

    AllVar val = all_var_from_int32(42);
    BtreeDeleteInfo info = findValueInLeaf(0, &val, e.tableId, e.colIdx, &e.bufs);
    TEST_ASSERT(info.found == 1, "Should find the value");
    TEST_ASSERT(info.ptdOffset != -1, "ptdOffset should be valid");
    TEST_ASSERT(info.dataOffset != -1, "dataOffset should be valid");
    TEST_ASSERT(info.ptrToBlocks != -1, "ptrToBlocks should be valid");
    TEST_PASS();
}

void test_findValueInLeaf_not_found(void) {
    TEST_START("findValueInLeaf returns not found for missing value");
    TestEnv e;
    env_init(&e, 703, 0, 10);
    env_add(&e, 42, 7);

    AllVar val = all_var_from_int32(999);
    BtreeDeleteInfo info = findValueInLeaf(0, &val, e.tableId, e.colIdx, &e.bufs);
    TEST_ASSERT(info.found == 0, "Should not find missing value");
    TEST_PASS();
}

void test_isNodeEmpty_on_empty_tree(void) {
    TEST_START("isNodeEmpty returns 1 on empty tree");
    TestEnv e;
    env_init(&e, 704, 0, 10);
    TEST_ASSERT(isNodeEmpty(0, e.tableId, e.colIdx, &e.bufs) == 1, "Empty tree node should be empty");
    TEST_PASS();
}

void test_isNodeEmpty_after_insert(void) {
    TEST_START("isNodeEmpty returns 0 after insert");
    TestEnv e;
    env_init(&e, 705, 0, 10);
    env_add(&e, 42, 1);
    TEST_ASSERT(isNodeEmpty(0, e.tableId, e.colIdx, &e.bufs) == 0, "Node should not be empty after insert");
    TEST_PASS();
}

void test_isNodeEmpty_after_delete_all(void) {
    TEST_START("isNodeEmpty returns 1 after deleting all values");
    TestEnv e;
    env_init(&e, 706, 0, 10);
    env_add(&e, 42, 1);
    env_delete(&e, 42, 1);
    TEST_ASSERT(isNodeEmpty(0, e.tableId, e.colIdx, &e.bufs) == 1, "Node should be empty after delete all");
    TEST_PASS();
}

void test_countNodeKeys_various(void) {
    TEST_START("countNodeKeys: 0, 1, 2, 3 keys");
    TestEnv e;
    env_init(&e, 707, 0, 10);

    TEST_ASSERT(countNodeKeys(0, e.tableId, e.colIdx, &e.bufs) == 0, "Expected 0 keys");

    env_add(&e, 10, 1);
    TEST_ASSERT(countNodeKeys(0, e.tableId, e.colIdx, &e.bufs) == 1, "Expected 1 key");

    env_add(&e, 20, 2);
    TEST_ASSERT(countNodeKeys(0, e.tableId, e.colIdx, &e.bufs) == 2, "Expected 2 keys");

    env_add(&e, 30, 3);
    TEST_ASSERT(countNodeKeys(0, e.tableId, e.colIdx, &e.bufs) == 3, "Expected 3 keys");
    TEST_PASS();
}

void test_countNodeKeys_after_delete(void) {
    TEST_START("countNodeKeys decreases after delete");
    TestEnv e;
    env_init(&e, 708, 0, 10);
    env_add(&e, 10, 1);
    env_add(&e, 20, 2);
    env_add(&e, 30, 3);

    env_delete(&e, 20, 2);
    TEST_ASSERT(countNodeKeys(0, e.tableId, e.colIdx, &e.bufs) == 2, "Expected 2 keys after delete");

    env_delete(&e, 10, 1);
    TEST_ASSERT(countNodeKeys(0, e.tableId, e.colIdx, &e.bufs) == 1, "Expected 1 key after delete");

    env_delete(&e, 30, 3);
    TEST_ASSERT(countNodeKeys(0, e.tableId, e.colIdx, &e.bufs) == 0, "Expected 0 keys after delete all");
    TEST_PASS();
}

// ============================================================================
//  8. BLOCK ENTRY LAYER TESTS (deleteBlockEntry directly)
// ============================================================================

void test_deleteBlockEntry_head_only(void) {
    TEST_START("deleteBlockEntry: remove only block (head)");
    TestEnv e;
    env_init(&e, 800, 0, 10);
    env_add(&e, 42, 5);

    AllVar val = all_var_from_int32(42);
    BtreeDeleteInfo info = findValueInLeaf(0, &val, e.tableId, e.colIdx, &e.bufs);
    TEST_ASSERT(info.found == 1, "Value should be found");

    int32_t newHead = deleteBlockEntry(&e.fsm, &e.bufs, e.tableId, e.colIdx, 5, info.ptrToBlocks);
    TEST_ASSERT(newHead == -1, "New head should be -1 (empty list)");
    TEST_PASS();
}

void test_deleteBlockEntry_head_with_next(void) {
    TEST_START("deleteBlockEntry: remove head when 2 blocks exist");
    TestEnv e;
    env_init(&e, 801, 0, 10);
    env_add(&e, 42, 10);
    env_add(&e, 42, 20);

    AllVar val = all_var_from_int32(42);
    BtreeDeleteInfo info = findValueInLeaf(0, &val, e.tableId, e.colIdx, &e.bufs);
    TEST_ASSERT(info.found == 1, "Value should be found");

    int32_t newHead = deleteBlockEntry(&e.fsm, &e.bufs, e.tableId, e.colIdx, 10, info.ptrToBlocks);
    TEST_ASSERT(newHead != -1, "New head should not be -1 (second block remains)");
    TEST_ASSERT(newHead != info.ptrToBlocks, "New head should differ from old head");
    TEST_PASS();
}

void test_deleteBlockEntry_not_found(void) {
    TEST_START("deleteBlockEntry: blockId not in list (no change)");
    TestEnv e;
    env_init(&e, 802, 0, 10);
    env_add(&e, 42, 5);

    AllVar val = all_var_from_int32(42);
    BtreeDeleteInfo info = findValueInLeaf(0, &val, e.tableId, e.colIdx, &e.bufs);
    TEST_ASSERT(info.found == 1, "Value should be found");

    int32_t newHead = deleteBlockEntry(&e.fsm, &e.bufs, e.tableId, e.colIdx, 999, info.ptrToBlocks);
    TEST_ASSERT(newHead == info.ptrToBlocks, "Head should remain unchanged when blockId not found");
    TEST_PASS();
}

// ============================================================================
//  9. MIXED OPERATIONS — COMPLEX SCENARIOS
// ============================================================================

void test_multiple_keys_delete_one_reinsert(void) {
    TEST_START("Multi-key: delete one key, reinsert it, verify all");
    TestEnv e;
    env_init(&e, 900, 0, 15);
    env_add(&e, 10, 1);
    env_add(&e, 20, 2);
    env_add(&e, 30, 3);

    env_delete(&e, 20, 2);
    TEST_ASSERT(env_find(&e, 20) == -1, "20 should be gone");

    env_add(&e, 20, 99);
    TEST_ASSERT(env_find(&e, 10) == 1, "10 should remain");
    TEST_ASSERT(env_find(&e, 20) == 99, "20 should be re-inserted with blockId=99");
    TEST_ASSERT(env_find(&e, 30) == 3, "30 should remain");
    TEST_PASS();
}

void test_delete_all_then_rebuild(void) {
    TEST_START("Delete everything, then rebuild tree with new values");
    TestEnv e;
    env_init(&e, 901, 0, 15);
    for (int i = 1; i <= 5; i++) {
        env_add(&e, i * 10, i);
    }
    for (int i = 1; i <= 5; i++) {
        env_delete(&e, i * 10, i);
    }
    // Tree should be empty
    for (int i = 1; i <= 5; i++) {
        TEST_ASSERT(env_find(&e, i * 10) == -1, "All should be gone");
    }
    // Rebuild with different values
    for (int i = 1; i <= 5; i++) {
        env_add(&e, i * 100, i + 50);
    }
    for (int i = 1; i <= 5; i++) {
        TEST_ASSERT(env_find(&e, i * 100) == i + 50, "Rebuilt value should be found");
    }
    TEST_PASS();
}

void test_zigzag_insert_delete_pattern(void) {
    TEST_START("Zigzag: insert 1-10, delete odd, insert 11-15, verify");
    TestEnv e;
    env_init(&e, 902, 0, 20);

    // Insert 1..10
    for (int i = 1; i <= 10; i++) {
        env_add(&e, i, i);
    }
    // Delete odd: 1, 3, 5, 7, 9
    for (int i = 1; i <= 10; i += 2) {
        env_delete(&e, i, i);
    }
    // Insert 11..15
    for (int i = 11; i <= 15; i++) {
        env_add(&e, i, i);
    }
    // Verify
    for (int i = 1; i <= 10; i += 2) {
        TEST_ASSERT(env_find(&e, i) == -1, "Odd should be deleted");
    }
    for (int i = 2; i <= 10; i += 2) {
        TEST_ASSERT(env_find(&e, i) == i, "Even should remain");
    }
    for (int i = 11; i <= 15; i++) {
        TEST_ASSERT(env_find(&e, i) == i, "New values should be found");
    }
    TEST_PASS();
}

void test_same_value_many_blocks_delete_all(void) {
    TEST_START("Same value with 5 blocks — delete all one by one");
    TestEnv e;
    env_init(&e, 903, 0, 15);

    // Insert same key 5 times with different blocks
    for (int i = 1; i <= 5; i++) {
        env_add(&e, 77, i * 10);
    }

    // Delete all blocks one by one
    for (int i = 0; i < 5; i++) {
        int32_t b = env_find(&e, 77);
        TEST_ASSERT(b != -1, "Should still find value");
        env_delete(&e, 77, b);
    }

    TEST_ASSERT(env_find(&e, 77) == -1, "Value should be completely gone");
    TEST_PASS();
}

void test_large_scale_insert_selective_delete(void) {
    TEST_START("Insert 30 values, delete every 3rd, verify all");
    TestEnv e;
    env_init(&e, 904, 0, 30);

    for (int i = 1; i <= 30; i++) {
        env_add(&e, i * 3, i);
    }
    // Delete every 3rd: values 9, 18, 27, 36, ...
    for (int i = 3; i <= 30; i += 3) {
        env_delete(&e, i * 3, i);
    }
    for (int i = 1; i <= 30; i++) {
        int32_t result = env_find(&e, i * 3);
        if (i % 3 == 0) {
            TEST_ASSERT(result == -1, "Every 3rd should be deleted");
        } else {
            TEST_ASSERT(result == i, "Non-3rd should remain");
        }
    }
    TEST_PASS();
}

// ============================================================================
//  10. BOUNDARY / EXTREME VALUES
// ============================================================================

void test_value_zero(void) {
    TEST_START("Insert and delete value 0");
    TestEnv e;
    env_init(&e, 1000, 0, 10);
    env_add(&e, 0, 1);
    TEST_ASSERT(env_find(&e, 0) == 1, "Value 0 should be found");
    env_delete(&e, 0, 1);
    TEST_ASSERT(env_find(&e, 0) == -1, "Value 0 should be deleted");
    TEST_PASS();
}

void test_value_int32_max(void) {
    TEST_START("Insert and delete INT32_MAX");
    TestEnv e;
    env_init(&e, 1001, 0, 10);
    env_add(&e, 2147483647, 1);
    TEST_ASSERT(env_find(&e, 2147483647) == 1, "INT32_MAX should be found");
    env_delete(&e, 2147483647, 1);
    TEST_ASSERT(env_find(&e, 2147483647) == -1, "INT32_MAX should be deleted");
    TEST_PASS();
}

void test_value_int32_min(void) {
    TEST_START("Insert and delete INT32_MIN");
    TestEnv e;
    env_init(&e, 1002, 0, 10);
    int32_t minval = -2147483647 - 1;
    env_add(&e, minval, 1);
    TEST_ASSERT(env_find(&e, minval) == 1, "INT32_MIN should be found");
    env_delete(&e, minval, 1);
    TEST_ASSERT(env_find(&e, minval) == -1, "INT32_MIN should be deleted");
    TEST_PASS();
}

void test_consecutive_values_1_to_1(void) {
    TEST_START("Insert values 1..1 (single element edge case)");
    TestEnv e;
    env_init(&e, 1003, 0, 10);
    env_add(&e, 1, 1);
    TEST_ASSERT(env_find(&e, 1) == 1, "Single element should be found");
    env_delete(&e, 1, 1);
    TEST_ASSERT(env_find(&e, 1) == -1, "Single element should be deleted");
    TEST_PASS();
}

void test_blockid_zero(void) {
    TEST_START("Insert and delete with blockId = 0");
    TestEnv e;
    env_init(&e, 1004, 0, 10);
    env_add(&e, 42, 0);
    TEST_ASSERT(env_find(&e, 42) == 0, "BlockId 0 should be found");
    env_delete(&e, 42, 0);
    TEST_ASSERT(env_find(&e, 42) == -1, "BlockId 0 should be deleted");
    TEST_PASS();
}

// ============================================================================
//  MAIN — RUN ALL TESTS
// ============================================================================

int main(void) {
    printf("\n\033[1;33m╔══════════════════════════════════════════════════════════════╗\033[0m\n");
    printf("\033[1;33m║     B-TREE FILE OPERATION — COMPREHENSIVE TEST SUITE        ║\033[0m\n");
    printf("\033[1;33m╚══════════════════════════════════════════════════════════════╝\033[0m\n");

    SECTION("1. BASIC INSERT & SEARCH");
    test_insert_single_value();
    test_insert_multiple_unique_values();
    test_search_nonexistent_value();
    test_search_empty_tree();
    test_insert_duplicate_key_different_blocks();
    test_insert_negative_values();
    test_insert_descending_order();
    test_insert_ascending_order();

    SECTION("2. BASIC DELETE");
    test_delete_single_value();
    test_delete_first_of_many_values();
    test_delete_last_of_many_values();
    test_delete_middle_of_many_values();
    test_delete_nonexistent_value();
    test_delete_wrong_blockid();
    test_delete_from_empty_tree();

    SECTION("3. DUPLICATE KEY (BLOCK LIST) DELETION");
    test_delete_one_block_from_duplicate_key();
    test_delete_all_blocks_from_duplicate_key();
    test_delete_three_blocks_from_duplicate_key();

    SECTION("4. DELETE ALL VALUES");
    test_delete_all_values_one_by_one();
    test_delete_all_in_reverse_order();
    test_delete_alternating_values();

    SECTION("5. INSERT AFTER DELETE (REUSE)");
    test_insert_after_delete_same_value();
    test_insert_after_delete_different_value();
    test_repeated_insert_delete_cycles();

    SECTION("6. STRESS TEST — MANY VALUES");
    test_insert_50_values();
    test_insert_50_then_delete_25();
    test_insert_delete_interleaved_50();

    SECTION("7. EDGE CASES — INTERNAL DATA STRUCTURES");
    test_getData_returns_correct_size();
    test_getData_returns_zero_after_all_deleted();
    test_findValueInLeaf_found();
    test_findValueInLeaf_not_found();
    test_isNodeEmpty_on_empty_tree();
    test_isNodeEmpty_after_insert();
    test_isNodeEmpty_after_delete_all();
    test_countNodeKeys_various();
    test_countNodeKeys_after_delete();

    SECTION("8. BLOCK ENTRY LAYER (deleteBlockEntry)");
    test_deleteBlockEntry_head_only();
    test_deleteBlockEntry_head_with_next();
    test_deleteBlockEntry_not_found();

    SECTION("9. MIXED OPERATIONS — COMPLEX SCENARIOS");
    test_multiple_keys_delete_one_reinsert();
    test_delete_all_then_rebuild();
    test_zigzag_insert_delete_pattern();
    test_same_value_many_blocks_delete_all();
    test_large_scale_insert_selective_delete();

    SECTION("10. BOUNDARY / EXTREME VALUES");
    test_value_zero();
    test_value_int32_max();
    test_value_int32_min();
    test_consecutive_values_1_to_1();
    test_blockid_zero();

    // Summary
    printf("\n\033[1;33m══════════════════════════════════════════════════════════════\033[0m\n");
    printf("  Total: %d  |  \033[32mPassed: %d\033[0m  |  \033[31mFailed: %d\033[0m\n",
           tests_passed + tests_failed, tests_passed, tests_failed);
    printf("\033[1;33m══════════════════════════════════════════════════════════════\033[0m\n\n");

    return tests_failed > 0 ? 1 : 0;
}
