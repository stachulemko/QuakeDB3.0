//
// Comprehensive fsmMapBtree tests
// Tests: init, table/column/block CRUD, free space tracking, edge cases
//

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "../../memory-mgmt/memory-mgmt/config.h"
#include "../indexes/fsmMapBtree.h"

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
//  1. INIT & BASIC STATE
// ============================================================================

void test_fsm_init(void) {
    TEST_START("fsm_btree_init sets tables=NULL and count=0");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    TEST_ASSERT(fsm.tables == NULL, "tables should be NULL");
    TEST_ASSERT(fsm.tableCount == 0, "tableCount should be 0");
    TEST_PASS();
}

void test_fsm_init_null(void) {
    TEST_START("fsm_btree_init(NULL) does not crash");
    fsm_btree_init(NULL);
    TEST_PASS();
}

// ============================================================================
//  2. TABLE OPERATIONS
// ============================================================================

void test_get_table_empty(void) {
    TEST_START("get_table returns NULL on empty FSM");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    TEST_ASSERT(fsm_btree_get_table(&fsm, 1) == NULL, "Should be NULL");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_get_or_create_table(void) {
    TEST_START("get_or_create_table creates and retrieves table");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    BtreeTableEntry *t = fsm_btree_get_or_create_table(&fsm, 10);
    TEST_ASSERT(t != NULL, "Should create table");
    TEST_ASSERT(t->tableId == 10, "tableId should be 10");
    TEST_ASSERT(fsm.tableCount == 1, "tableCount should be 1");

    BtreeTableEntry *t2 = fsm_btree_get_or_create_table(&fsm, 10);
    TEST_ASSERT(t2 == t, "Should return same table");
    TEST_ASSERT(fsm.tableCount == 1, "tableCount should still be 1");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_create_multiple_tables(void) {
    TEST_START("Create 5 distinct tables");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    for (int i = 1; i <= 5; i++) {
        BtreeTableEntry *t = fsm_btree_get_or_create_table(&fsm, i * 10);
        TEST_ASSERT(t != NULL, "Table should be created");
        TEST_ASSERT(t->tableId == i * 10, "tableId mismatch");
    }
    TEST_ASSERT(fsm.tableCount == 5, "Should have 5 tables");

    for (int i = 1; i <= 5; i++) {
        TEST_ASSERT(fsm_btree_get_table(&fsm, i * 10) != NULL, "Should find table");
    }
    TEST_ASSERT(fsm_btree_get_table(&fsm, 999) == NULL, "Non-existent table should be NULL");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

// ============================================================================
//  3. COLUMN OPERATIONS
// ============================================================================

void test_get_column_empty(void) {
    TEST_START("get_column returns NULL on table with no columns");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    BtreeTableEntry *t = fsm_btree_get_or_create_table(&fsm, 1);
    TEST_ASSERT(fsm_btree_get_column(t, 0) == NULL, "Should be NULL");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_get_or_create_column(void) {
    TEST_START("get_or_create_column creates and retrieves column");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    BtreeTableEntry *t = fsm_btree_get_or_create_table(&fsm, 1);
    BtreeColumnIndex *c = fsm_btree_get_or_create_column(t, 0);
    TEST_ASSERT(c != NULL, "Should create column");
    TEST_ASSERT(c->columnIndex == 0, "columnIndex should be 0");
    TEST_ASSERT(c->currentBlockId == -1, "currentBlockId should be -1");
    TEST_ASSERT(t->columnCount == 1, "columnCount should be 1");

    BtreeColumnIndex *c2 = fsm_btree_get_or_create_column(t, 0);
    TEST_ASSERT(c2 == c, "Should return same column");
    TEST_ASSERT(t->columnCount == 1, "columnCount should still be 1");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_create_multiple_columns(void) {
    TEST_START("Create 4 columns on same table");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    BtreeTableEntry *t = fsm_btree_get_or_create_table(&fsm, 1);
    for (int i = 0; i < 4; i++) {
        BtreeColumnIndex *c = fsm_btree_get_or_create_column(t, i);
        TEST_ASSERT(c != NULL, "Column should be created");
        TEST_ASSERT(c->columnIndex == i, "columnIndex mismatch");
    }
    TEST_ASSERT(t->columnCount == 4, "Should have 4 columns");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_create_index(void) {
    TEST_START("fsm_btree_create_index creates column if not exists");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    int8_t r = fsm_btree_create_index(&fsm, 20, 0);
    TEST_ASSERT(r == 1, "Should succeed");
    TEST_ASSERT(fsm.tableCount == 1, "Should have 1 table");

    BtreeTableEntry *t = fsm_btree_get_table(&fsm, 20);
    TEST_ASSERT(t != NULL, "Table should exist");
    BtreeColumnIndex *c = fsm_btree_get_column(t, 0);
    TEST_ASSERT(c != NULL, "Column should exist");
    TEST_ASSERT(c->columnIndex == 0, "columnIndex should be 0");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_create_index_duplicate(void) {
    TEST_START("fsm_btree_create_index returns 0 if column already exists");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    TEST_ASSERT(fsm_btree_create_index(&fsm, 20, 0) == 1, "First create should succeed");
    TEST_ASSERT(fsm_btree_create_index(&fsm, 20, 0) == 0, "Duplicate create should fail");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_create_index_null_fsm(void) {
    TEST_START("fsm_btree_create_index(NULL, ...) returns 0");
    TEST_ASSERT(fsm_btree_create_index(NULL, 1, 0) == 0, "Should return 0");
    TEST_PASS();
}

// ============================================================================
//  4. BLOCK OPERATIONS
// ============================================================================

void test_add_block(void) {
    TEST_START("fsm_btree_add_block adds block to column");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    int8_t r = fsm_btree_add_block(&fsm, 1, 0, 0, 8);
    TEST_ASSERT(r == 1, "Should succeed");

    BtreeBlockEntry *b = fsm_btree_get_block(&fsm, 1, 0, 0);
    TEST_ASSERT(b != NULL, "Block should exist");
    TEST_ASSERT(b->blockId == 0, "blockId should be 0");
    TEST_ASSERT(b->blockSize == 8, "blockSize should be 8");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_add_block_duplicate(void) {
    TEST_START("fsm_btree_add_block returns 0 for duplicate blockId");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    TEST_ASSERT(fsm_btree_add_block(&fsm, 1, 0, 0, 8) == 1, "First add");
    TEST_ASSERT(fsm_btree_add_block(&fsm, 1, 0, 0, 16) == 0, "Duplicate add");

    BtreeBlockEntry *b = fsm_btree_get_block(&fsm, 1, 0, 0);
    TEST_ASSERT(b->blockSize == 8, "blockSize should remain 8");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_add_block_null_fsm(void) {
    TEST_START("fsm_btree_add_block(NULL, ...) returns 0");
    TEST_ASSERT(fsm_btree_add_block(NULL, 1, 0, 0, 8) == 0, "Should return 0");
    TEST_PASS();
}

void test_add_multiple_blocks(void) {
    TEST_START("Add 4 blocks (0,1,2,3) to column and verify each");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT(fsm_btree_add_block(&fsm, 1, 0, i, i * 10) == 1, "Should add block");
    }
    for (int i = 0; i < 4; i++) {
        BtreeBlockEntry *b = fsm_btree_get_block(&fsm, 1, 0, i);
        TEST_ASSERT(b != NULL, "Block should exist");
        TEST_ASSERT(b->blockId == i, "blockId mismatch");
        TEST_ASSERT(b->blockSize == i * 10, "blockSize mismatch");
    }
    TEST_ASSERT(fsm_btree_get_block(&fsm, 1, 0, 99) == NULL, "Non-existent block");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_get_block_nonexistent(void) {
    TEST_START("get_block returns NULL for non-existent table/col/block");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    TEST_ASSERT(fsm_btree_get_block(&fsm, 1, 0, 0) == NULL, "No table");

    fsm_btree_add_block(&fsm, 1, 0, 0, 8);
    TEST_ASSERT(fsm_btree_get_block(&fsm, 2, 0, 0) == NULL, "Wrong table");
    TEST_ASSERT(fsm_btree_get_block(&fsm, 1, 1, 0) == NULL, "Wrong column");
    TEST_ASSERT(fsm_btree_get_block(&fsm, 1, 0, 5) == NULL, "Wrong blockId");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_get_block_count(void) {
    TEST_START("get_block_count tracks number of blocks");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    TEST_ASSERT(fsm_btree_get_block_count(&fsm, 1, 0) == -1, "No table = -1");

    fsm_btree_create_index(&fsm, 1, 0);
    TEST_ASSERT(fsm_btree_get_block_count(&fsm, 1, 0) == 0, "Empty column = 0");

    fsm_btree_add_block(&fsm, 1, 0, 0, 8);
    TEST_ASSERT(fsm_btree_get_block_count(&fsm, 1, 0) == 1, "After 1 add = 1");

    fsm_btree_add_block(&fsm, 1, 0, 1, 0);
    fsm_btree_add_block(&fsm, 1, 0, 2, 0);
    TEST_ASSERT(fsm_btree_get_block_count(&fsm, 1, 0) == 3, "After 3 adds = 3");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

// ============================================================================
//  5. BLOCK SIZE UPDATES
// ============================================================================

void test_update_block_size(void) {
    TEST_START("update_block_size changes blockSize");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, 8);
    TEST_ASSERT(fsm_btree_update_block_size(&fsm, 1, 0, 0, 100) == 1, "Should succeed");

    BtreeBlockEntry *b = fsm_btree_get_block(&fsm, 1, 0, 0);
    TEST_ASSERT(b->blockSize == 100, "blockSize should be 100");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_update_block_size_nonexistent(void) {
    TEST_START("update_block_size returns 0 for non-existent block");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    TEST_ASSERT(fsm_btree_update_block_size(&fsm, 1, 0, 0, 100) == 0, "Should fail");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_update_block_size_negative(void) {
    TEST_START("update_block_size rejects negative size");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, 8);
    TEST_ASSERT(fsm_btree_update_block_size(&fsm, 1, 0, 0, -1) == 0, "Should reject negative");

    BtreeBlockEntry *b = fsm_btree_get_block(&fsm, 1, 0, 0);
    TEST_ASSERT(b->blockSize == 8, "blockSize should remain 8");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_update_block_size_over_limit(void) {
    TEST_START("update_block_size rejects size > btreeFreeSpace");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, 8);
    TEST_ASSERT(fsm_btree_update_block_size(&fsm, 1, 0, 0, btreeFreeSpace + 1) == 0, "Should reject over limit");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_update_block_size_to_max(void) {
    TEST_START("update_block_size accepts btreeFreeSpace exactly");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, 0);
    TEST_ASSERT(fsm_btree_update_block_size(&fsm, 1, 0, 0, btreeFreeSpace) == 1, "Should accept max");
    BtreeBlockEntry *b = fsm_btree_get_block(&fsm, 1, 0, 0);
    TEST_ASSERT(b->blockSize == btreeFreeSpace, "blockSize should be btreeFreeSpace");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

// ============================================================================
//  6. APPEND TO BLOCK
// ============================================================================

void test_append_to_block(void) {
    TEST_START("append_to_block increases blockSize");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, 100);
    TEST_ASSERT(fsm_btree_append_to_block(&fsm, 1, 0, 0, 50) == 1, "Should succeed");

    BtreeBlockEntry *b = fsm_btree_get_block(&fsm, 1, 0, 0);
    TEST_ASSERT(b->blockSize == 150, "blockSize should be 150");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_append_to_block_overflow(void) {
    TEST_START("append_to_block rejects overflow past btreeFreeSpace");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, btreeFreeSpace - 10);
    TEST_ASSERT(fsm_btree_append_to_block(&fsm, 1, 0, 0, 11) == 0, "Should reject overflow");

    BtreeBlockEntry *b = fsm_btree_get_block(&fsm, 1, 0, 0);
    TEST_ASSERT(b->blockSize == btreeFreeSpace - 10, "blockSize should not change");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_append_to_block_exact_fit(void) {
    TEST_START("append_to_block fills block to exactly btreeFreeSpace");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, btreeFreeSpace - 8);
    TEST_ASSERT(fsm_btree_append_to_block(&fsm, 1, 0, 0, 8) == 1, "Exact fit should succeed");

    BtreeBlockEntry *b = fsm_btree_get_block(&fsm, 1, 0, 0);
    TEST_ASSERT(b->blockSize == btreeFreeSpace, "blockSize should be btreeFreeSpace");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_append_to_block_zero_bytes(void) {
    TEST_START("append_to_block rejects 0 bytes");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, 100);
    TEST_ASSERT(fsm_btree_append_to_block(&fsm, 1, 0, 0, 0) == 0, "Should reject 0 bytes");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_append_to_block_negative_bytes(void) {
    TEST_START("append_to_block rejects negative bytes");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, 100);
    TEST_ASSERT(fsm_btree_append_to_block(&fsm, 1, 0, 0, -5) == 0, "Should reject negative");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_append_updates_currentBlockId(void) {
    TEST_START("append_to_block updates currentBlockId");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, 0);
    fsm_btree_add_block(&fsm, 1, 0, 4, 0);

    fsm_btree_append_to_block(&fsm, 1, 0, 4, 8);
    BtreeTableEntry *t = fsm_btree_get_table(&fsm, 1);
    BtreeColumnIndex *c = fsm_btree_get_column(t, 0);
    TEST_ASSERT(c->currentBlockId == 4, "currentBlockId should be updated to 4");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

// ============================================================================
//  7. REMOVE BLOCK
// ============================================================================

void test_remove_block(void) {
    TEST_START("remove_block removes and frees block");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, 8);
    fsm_btree_add_block(&fsm, 1, 0, 1, 0);

    TEST_ASSERT(fsm_btree_remove_block(&fsm, 1, 0, 0) == 1, "Should succeed");
    TEST_ASSERT(fsm_btree_get_block(&fsm, 1, 0, 0) == NULL, "Block 0 should be gone");
    TEST_ASSERT(fsm_btree_get_block(&fsm, 1, 0, 1) != NULL, "Block 1 should remain");
    TEST_ASSERT(fsm_btree_get_block_count(&fsm, 1, 0) == 1, "blockCount should be 1");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_remove_block_nonexistent(void) {
    TEST_START("remove_block returns 0 for non-existent block");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    TEST_ASSERT(fsm_btree_remove_block(&fsm, 1, 0, 0) == 0, "No table");

    fsm_btree_add_block(&fsm, 1, 0, 0, 8);
    TEST_ASSERT(fsm_btree_remove_block(&fsm, 1, 0, 99) == 0, "Wrong blockId");
    TEST_ASSERT(fsm_btree_remove_block(&fsm, 2, 0, 0) == 0, "Wrong table");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_remove_block_resets_currentBlockId(void) {
    TEST_START("remove_block resets currentBlockId when removing current");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 5, 8);

    BtreeTableEntry *t = fsm_btree_get_table(&fsm, 1);
    BtreeColumnIndex *c = fsm_btree_get_column(t, 0);
    TEST_ASSERT(c->currentBlockId == 5, "currentBlockId should be 5");

    fsm_btree_remove_block(&fsm, 1, 0, 5);
    TEST_ASSERT(c->currentBlockId == -1, "currentBlockId should reset to -1");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_remove_all_blocks(void) {
    TEST_START("Remove all blocks one by one");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    for (int i = 0; i < 4; i++) {
        fsm_btree_add_block(&fsm, 1, 0, i, 0);
    }
    TEST_ASSERT(fsm_btree_get_block_count(&fsm, 1, 0) == 4, "Should have 4 blocks");

    for (int i = 0; i < 4; i++) {
        TEST_ASSERT(fsm_btree_remove_block(&fsm, 1, 0, i) == 1, "Should remove");
    }
    TEST_ASSERT(fsm_btree_get_block_count(&fsm, 1, 0) == 0, "Should have 0 blocks");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

// ============================================================================
//  8. FREE SPACE TRACKING
// ============================================================================

void test_get_free_space(void) {
    TEST_START("get_free_space = btreeFreeSpace - blockSize");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, 100);
    int32_t free_space = fsm_btree_get_free_space(&fsm, 1, 0, 0);
    TEST_ASSERT(free_space == btreeFreeSpace - 100, "Free space mismatch");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_get_free_space_empty_block(void) {
    TEST_START("get_free_space on empty block = btreeFreeSpace");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, 0);
    TEST_ASSERT(fsm_btree_get_free_space(&fsm, 1, 0, 0) == btreeFreeSpace, "Should be btreeFreeSpace");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_get_free_space_full_block(void) {
    TEST_START("get_free_space on full block = 0");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, btreeFreeSpace);
    TEST_ASSERT(fsm_btree_get_free_space(&fsm, 1, 0, 0) == 0, "Should be 0");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_get_free_space_nonexistent(void) {
    TEST_START("get_free_space returns -1 for non-existent block");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    TEST_ASSERT(fsm_btree_get_free_space(&fsm, 1, 0, 0) == -1, "Should be -1");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_free_space_after_append(void) {
    TEST_START("Free space decreases after append_to_block");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, 0);
    TEST_ASSERT(fsm_btree_get_free_space(&fsm, 1, 0, 0) == btreeFreeSpace, "Initially full free");

    fsm_btree_append_to_block(&fsm, 1, 0, 0, 200);
    TEST_ASSERT(fsm_btree_get_free_space(&fsm, 1, 0, 0) == btreeFreeSpace - 200, "After append");

    fsm_btree_append_to_block(&fsm, 1, 0, 0, 300);
    TEST_ASSERT(fsm_btree_get_free_space(&fsm, 1, 0, 0) == btreeFreeSpace - 500, "After second append");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

// ============================================================================
//  9. FSM FREE (CLEANUP)
// ============================================================================

void test_fsm_free_empty(void) {
    TEST_START("fsm_btree_free on empty FSM does not crash");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_free(&fsm);
    TEST_ASSERT(fsm.tables == NULL, "tables should be NULL after free");
    TEST_ASSERT(fsm.tableCount == 0, "tableCount should be 0 after free");
    TEST_PASS();
}

void test_fsm_free_null(void) {
    TEST_START("fsm_btree_free(NULL) does not crash");
    fsm_btree_free(NULL);
    TEST_PASS();
}

void test_fsm_free_complex(void) {
    TEST_START("fsm_btree_free cleans up multi-table/column/block FSM");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    // Create complex structure: 3 tables x 2 columns x 4 blocks each
    for (int t = 1; t <= 3; t++) {
        for (int c = 0; c < 2; c++) {
            fsm_btree_create_index(&fsm, t, c);
            for (int b = 0; b < 4; b++) {
                fsm_btree_add_block(&fsm, t, c, b, b * 10);
            }
        }
    }
    TEST_ASSERT(fsm.tableCount == 3, "Should have 3 tables");

    fsm_btree_free(&fsm);
    TEST_ASSERT(fsm.tables == NULL, "tables should be NULL");
    TEST_ASSERT(fsm.tableCount == 0, "tableCount should be 0");
    TEST_PASS();
}

// ============================================================================
//  10. MULTI-TABLE / MULTI-COLUMN ISOLATION
// ============================================================================

void test_different_tables_isolated(void) {
    TEST_START("Blocks in different tables are isolated");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 10, 0, 0, 100);
    fsm_btree_add_block(&fsm, 20, 0, 0, 200);

    BtreeBlockEntry *b1 = fsm_btree_get_block(&fsm, 10, 0, 0);
    BtreeBlockEntry *b2 = fsm_btree_get_block(&fsm, 20, 0, 0);
    TEST_ASSERT(b1 != b2, "Different tables should have different blocks");
    TEST_ASSERT(b1->blockSize == 100, "Table 10 blockSize");
    TEST_ASSERT(b2->blockSize == 200, "Table 20 blockSize");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_different_columns_isolated(void) {
    TEST_START("Blocks in different columns are isolated");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, 100);
    fsm_btree_add_block(&fsm, 1, 1, 0, 200);

    BtreeBlockEntry *b1 = fsm_btree_get_block(&fsm, 1, 0, 0);
    BtreeBlockEntry *b2 = fsm_btree_get_block(&fsm, 1, 1, 0);
    TEST_ASSERT(b1 != b2, "Different columns should have different blocks");
    TEST_ASSERT(b1->blockSize == 100, "Column 0 blockSize");
    TEST_ASSERT(b2->blockSize == 200, "Column 1 blockSize");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

void test_remove_block_doesnt_affect_other_columns(void) {
    TEST_START("Removing block from col 0 doesn't affect col 1");
    FSMMapBtree fsm;
    fsm_btree_init(&fsm);
    fsm_btree_add_block(&fsm, 1, 0, 0, 100);
    fsm_btree_add_block(&fsm, 1, 1, 0, 200);

    fsm_btree_remove_block(&fsm, 1, 0, 0);
    TEST_ASSERT(fsm_btree_get_block(&fsm, 1, 0, 0) == NULL, "Col 0 block removed");
    TEST_ASSERT(fsm_btree_get_block(&fsm, 1, 1, 0) != NULL, "Col 1 block should remain");
    fsm_btree_free(&fsm);
    TEST_PASS();
}

// ============================================================================
//  MAIN
// ============================================================================

int main(void) {
    printf("\n\033[1;33m╔══════════════════════════════════════════════════════════════╗\033[0m\n");
    printf("\033[1;33m║       FSM MAP B-TREE — COMPREHENSIVE TEST SUITE             ║\033[0m\n");
    printf("\033[1;33m╚══════════════════════════════════════════════════════════════╝\033[0m\n");

    SECTION("1. INIT & BASIC STATE");
    test_fsm_init();
    test_fsm_init_null();

    SECTION("2. TABLE OPERATIONS");
    test_get_table_empty();
    test_get_or_create_table();
    test_create_multiple_tables();

    SECTION("3. COLUMN OPERATIONS");
    test_get_column_empty();
    test_get_or_create_column();
    test_create_multiple_columns();
    test_create_index();
    test_create_index_duplicate();
    test_create_index_null_fsm();

    SECTION("4. BLOCK OPERATIONS");
    test_add_block();
    test_add_block_duplicate();
    test_add_block_null_fsm();
    test_add_multiple_blocks();
    test_get_block_nonexistent();
    test_get_block_count();

    SECTION("5. BLOCK SIZE UPDATES");
    test_update_block_size();
    test_update_block_size_nonexistent();
    test_update_block_size_negative();
    test_update_block_size_over_limit();
    test_update_block_size_to_max();

    SECTION("6. APPEND TO BLOCK");
    test_append_to_block();
    test_append_to_block_overflow();
    test_append_to_block_exact_fit();
    test_append_to_block_zero_bytes();
    test_append_to_block_negative_bytes();
    test_append_updates_currentBlockId();

    SECTION("7. REMOVE BLOCK");
    test_remove_block();
    test_remove_block_nonexistent();
    test_remove_block_resets_currentBlockId();
    test_remove_all_blocks();

    SECTION("8. FREE SPACE TRACKING");
    test_get_free_space();
    test_get_free_space_empty_block();
    test_get_free_space_full_block();
    test_get_free_space_nonexistent();
    test_free_space_after_append();

    SECTION("9. FSM FREE (CLEANUP)");
    test_fsm_free_empty();
    test_fsm_free_null();
    test_fsm_free_complex();

    SECTION("10. MULTI-TABLE / MULTI-COLUMN ISOLATION");
    test_different_tables_isolated();
    test_different_columns_isolated();
    test_remove_block_doesnt_affect_other_columns();

    printf("\n\033[1;33m══════════════════════════════════════════════════════════════\033[0m\n");
    printf("  Total: %d  |  \033[32mPassed: %d\033[0m  |  \033[31mFailed: %d\033[0m\n",
           tests_passed + tests_failed, tests_passed, tests_failed);
    printf("\033[1;33m══════════════════════════════════════════════════════════════\033[0m\n\n");

    return tests_failed > 0 ? 1 : 0;
}
