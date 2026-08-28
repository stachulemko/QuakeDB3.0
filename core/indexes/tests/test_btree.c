//
// Comprehensive in-memory B-tree tests (btree.h)
// Tests: createNode, addElement, getBlock, sort, split, traversal, edge cases
//

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "../../memory-mgmt/memory-mgmt/all_var.h"
#include "../indexes/btree.h"

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

// Helper: create a type value from int32
static type make_type_i32(int32_t val, int32_t blockId) {
    type t;
    t.val = all_var_from_int32(val);
    t.blockId = blockId;
    return t;
}

// Helper: create a type value from string
static type make_type_str(const char *s, int32_t blockId) {
    type t;
    t.val = all_var_from_string(s);
    t.blockId = blockId;
    return t;
}

// Helper: free all nodes recursively
static void freeTree(Node *node) {
    if (node == NULL) return;
    for (int i = 0; i < node->countNodesNext; i++) {
        freeTree(node->nodesNext[i]);
    }
    for (int i = 0; i < node->countNodes; i++) {
        free(node->nodes[i]);
    }
    free(node);
}

// ============================================================================
//  1. NODE CREATION
// ============================================================================

void test_createNodeM(void) {
    TEST_START("createNodeM allocates non-NULL node");
    Node *n = NULL;
    createNodeM(&n);
    TEST_ASSERT(n != NULL, "Node should be allocated");
    free(n);
    TEST_PASS();
}

void test_createNodeC(void) {
    TEST_START("createNodeC allocates zeroed node");
    Node *n = NULL;
    createNodeC(&n);
    TEST_ASSERT(n != NULL, "Node should be allocated");
    TEST_ASSERT(n->countNodes == 0, "countNodes should be 0");
    TEST_ASSERT(n->countNodesNext == 0, "countNodesNext should be 0");
    for (int i = 0; i < M; i++) {
        TEST_ASSERT(n->nodes[i] == NULL, "nodes[i] should be NULL");
        TEST_ASSERT(n->nodesNext[i] == NULL, "nodesNext[i] should be NULL");
    }
    free(n);
    TEST_PASS();
}

// ============================================================================
//  2. SINGLE ELEMENT INSERT
// ============================================================================

void test_add_single_element(void) {
    TEST_START("Add single element to empty node");
    Node *root = NULL;
    createNodeC(&root);

    type t = make_type_i32(42, 1);
    addElement(root, t, 0, 0, all_var_from_int32(0));

    TEST_ASSERT(root->countNodes == 1, "countNodes should be 1");
    TEST_ASSERT(root->nodes[0] != NULL, "nodes[0] should not be NULL");
    TEST_ASSERT(root->nodes[0]->val.val.i32 == 42, "Value should be 42");
    TEST_ASSERT(root->nodes[0]->blockId == 1, "blockId should be 1");
    freeTree(root);
    TEST_PASS();
}

void test_add_two_elements_sorted(void) {
    TEST_START("Add two elements — should be sorted");
    Node *root = NULL;
    createNodeC(&root);

    type t1 = make_type_i32(20, 1);
    type t2 = make_type_i32(10, 2);
    addElement(root, t1, 0, 0, all_var_from_int32(0));
    addElement(root, t2, 0, 0, all_var_from_int32(0));

    TEST_ASSERT(root->countNodes == 2, "countNodes should be 2");
    TEST_ASSERT(root->nodes[0]->val.val.i32 == 10, "First should be 10");
    TEST_ASSERT(root->nodes[1]->val.val.i32 == 20, "Second should be 20");
    freeTree(root);
    TEST_PASS();
}

void test_add_three_elements_sorted(void) {
    TEST_START("Add three elements — all sorted");
    Node *root = NULL;
    createNodeC(&root);

    type t1 = make_type_i32(30, 3);
    type t2 = make_type_i32(10, 1);
    type t3 = make_type_i32(20, 2);
    addElement(root, t1, 0, 0, all_var_from_int32(0));
    addElement(root, t2, 0, 0, all_var_from_int32(0));
    addElement(root, t3, 0, 0, all_var_from_int32(0));

    TEST_ASSERT(root->countNodes == 3, "countNodes should be 3");
    TEST_ASSERT(root->nodes[0]->val.val.i32 == 10, "1st should be 10");
    TEST_ASSERT(root->nodes[1]->val.val.i32 == 20, "2nd should be 20");
    TEST_ASSERT(root->nodes[2]->val.val.i32 == 30, "3rd should be 30");
    freeTree(root);
    TEST_PASS();
}

void test_add_four_elements_sorted(void) {
    TEST_START("Add M=4 elements — all sorted, no split yet");
    Node *root = NULL;
    createNodeC(&root);

    int vals[] = {40, 10, 30, 20};
    for (int i = 0; i < 4; i++) {
        type t = make_type_i32(vals[i], i + 1);
        addElement(root, t, 0, 0, all_var_from_int32(0));
    }

    TEST_ASSERT(root->countNodes == 4, "countNodes should be 4");
    TEST_ASSERT(root->nodes[0]->val.val.i32 == 10, "1st should be 10");
    TEST_ASSERT(root->nodes[1]->val.val.i32 == 20, "2nd should be 20");
    TEST_ASSERT(root->nodes[2]->val.val.i32 == 30, "3rd should be 30");
    TEST_ASSERT(root->nodes[3]->val.val.i32 == 40, "4th should be 40");
    freeTree(root);
    TEST_PASS();
}

// ============================================================================
//  3. NODE SPLIT (5th element triggers split)
// ============================================================================

void test_split_on_fifth_element(void) {
    TEST_START("5th element triggers split — root becomes internal");
    Node *root = NULL;
    createNodeC(&root);

    for (int i = 1; i <= 5; i++) {
        type t = make_type_i32(i * 10, i);
        addElement(root, t, 0, 0, all_var_from_int32(0));
    }

    // After split: root has 1 key (median), 2 children
    TEST_ASSERT(root->countNodes == 1, "Root should have 1 key after split");
    TEST_ASSERT(root->countNodesNext == 2, "Root should have 2 children");
    TEST_ASSERT(root->nodesNext[0] != NULL, "Left child should exist");
    TEST_ASSERT(root->nodesNext[1] != NULL, "Right child should exist");

    // All 5 values should be retrievable via in-order traversal
    int64_t expected[] = {10, 20, 30, 40, 50};
    TEST_ASSERT(verifyInOrder(root, expected, 5) == 1, "In-order should match");
    freeTree(root);
    TEST_PASS();
}

void test_split_preserves_all_keys(void) {
    TEST_START("Split preserves all keys (7 elements)");
    Node *root = NULL;
    createNodeC(&root);

    for (int i = 7; i >= 1; i--) {
        type t = make_type_i32(i * 5, i);
        addElement(root, t, 0, 0, all_var_from_int32(0));
    }

    int64_t expected[] = {5, 10, 15, 20, 25, 30, 35};
    int64_t collected[10];
    int count = collectKeysInOrder(root, collected, 10);
    TEST_ASSERT(count == 7, "Should have 7 keys");

    // Verify all keys present (sorted)
    for (int i = 0; i < 7; i++) {
        TEST_ASSERT(collected[i] == (i + 1) * 5, "Key mismatch");
    }
    freeTree(root);
    TEST_PASS();
}

void test_split_with_8_elements(void) {
    TEST_START("Insert 8 elements — multi-level tree possible");
    Node *root = NULL;
    createNodeC(&root);

    for (int i = 1; i <= 8; i++) {
        type t = make_type_i32(i, i + 100);
        addElement(root, t, 0, 0, all_var_from_int32(0));
    }

    int64_t collected[10];
    int count = collectKeysInOrder(root, collected, 10);
    TEST_ASSERT(count == 8, "Should have 8 keys");

    for (int i = 0; i < 8; i++) {
        TEST_ASSERT(collected[i] == i + 1, "In-order key mismatch");
    }
    freeTree(root);
    TEST_PASS();
}

// ============================================================================
//  4. getBlock (SEARCH)
// ============================================================================

void test_getBlock_single_value(void) {
    TEST_START("getBlock finds single value in leaf");
    Node *root = NULL;
    createNodeC(&root);

    type t = make_type_i32(42, 7);
    addElement(root, t, 0, 0, all_var_from_int32(0));

    int32_t result = getBlock(root, all_var_from_int32(42), 0, 0, all_var_from_int32(0));
    TEST_ASSERT(result == 7, "Should find blockId=7");
    freeTree(root);
    TEST_PASS();
}

void test_getBlock_multiple_values(void) {
    TEST_START("getBlock finds each of 4 values (no split)");
    Node *root = NULL;
    createNodeC(&root);

    int vals[] = {10, 20, 30, 40};
    for (int i = 0; i < 4; i++) {
        type t = make_type_i32(vals[i], i + 1);
        addElement(root, t, 0, 0, all_var_from_int32(0));
    }

    for (int i = 0; i < 4; i++) {
        int32_t result = getBlock(root, all_var_from_int32(vals[i]), 0, 0, all_var_from_int32(0));
        TEST_ASSERT(result == i + 1, "blockId mismatch");
    }
    freeTree(root);
    TEST_PASS();
}

void test_getBlock_null_root(void) {
    TEST_START("getBlock returns -1 on NULL root");
    int32_t result = getBlock(NULL, all_var_from_int32(42), 0, 0, all_var_from_int32(0));
    TEST_ASSERT(result == -1, "Should be -1");
    TEST_PASS();
}

void test_getBlock_empty_node(void) {
    TEST_START("getBlock returns appropriate result on empty node");
    Node *root = NULL;
    createNodeC(&root);
    // No elements added — getBlock should not crash
    getBlock(root, all_var_from_int32(42), 0, 0, all_var_from_int32(0));
    freeTree(root);
    TEST_PASS();
}

// ============================================================================
//  5. SORTING (quickSort)
// ============================================================================

void test_quickSort_already_sorted(void) {
    TEST_START("quickSort on already sorted array");
    type *arr[4];
    for (int i = 0; i < 4; i++) {
        arr[i] = malloc(sizeof(type));
        arr[i]->val = all_var_from_int32((i + 1) * 10);
        arr[i]->blockId = i;
    }
    quickSort(arr, 4);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT(arr[i]->val.val.i32 == (i + 1) * 10, "Should remain sorted");
    }
    for (int i = 0; i < 4; i++) free(arr[i]);
    TEST_PASS();
}

void test_quickSort_reverse_sorted(void) {
    TEST_START("quickSort on reverse sorted array");
    type *arr[4];
    for (int i = 0; i < 4; i++) {
        arr[i] = malloc(sizeof(type));
        arr[i]->val = all_var_from_int32((4 - i) * 10);
        arr[i]->blockId = i;
    }
    quickSort(arr, 4);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT(arr[i]->val.val.i32 == (i + 1) * 10, "Should be sorted ascending");
    }
    for (int i = 0; i < 4; i++) free(arr[i]);
    TEST_PASS();
}

void test_quickSort_single_element(void) {
    TEST_START("quickSort on single element");
    type *arr[1];
    arr[0] = malloc(sizeof(type));
    arr[0]->val = all_var_from_int32(42);
    arr[0]->blockId = 0;
    quickSort(arr, 1);
    TEST_ASSERT(arr[0]->val.val.i32 == 42, "Should remain 42");
    free(arr[0]);
    TEST_PASS();
}

void test_quickSort_duplicates(void) {
    TEST_START("quickSort with duplicate values");
    type *arr[4];
    int vals[] = {30, 10, 30, 10};
    for (int i = 0; i < 4; i++) {
        arr[i] = malloc(sizeof(type));
        arr[i]->val = all_var_from_int32(vals[i]);
        arr[i]->blockId = i;
    }
    quickSort(arr, 4);
    // Should be non-decreasing
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT(arr[i]->val.val.i32 <= arr[i + 1]->val.val.i32, "Should be non-decreasing");
    }
    for (int i = 0; i < 4; i++) free(arr[i]);
    TEST_PASS();
}

// ============================================================================
//  6. IN-ORDER TRAVERSAL (collectKeysInOrder / verifyInOrder)
// ============================================================================

void test_collectKeys_empty(void) {
    TEST_START("collectKeysInOrder returns 0 on empty node");
    Node *root = NULL;
    createNodeC(&root);
    int64_t out[10];
    int count = collectKeysInOrder(root, out, 10);
    TEST_ASSERT(count == 0, "Should be 0");
    freeTree(root);
    TEST_PASS();
}

void test_collectKeys_null(void) {
    TEST_START("collectKeysInOrder returns 0 on NULL");
    int64_t out[10];
    int count = collectKeysInOrder(NULL, out, 10);
    TEST_ASSERT(count == 0, "Should be 0");
    TEST_PASS();
}

void test_collectKeys_three(void) {
    TEST_START("collectKeysInOrder returns 3 sorted keys");
    Node *root = NULL;
    createNodeC(&root);

    type t1 = make_type_i32(30, 3);
    type t2 = make_type_i32(10, 1);
    type t3 = make_type_i32(20, 2);
    addElement(root, t1, 0, 0, all_var_from_int32(0));
    addElement(root, t2, 0, 0, all_var_from_int32(0));
    addElement(root, t3, 0, 0, all_var_from_int32(0));

    int64_t out[10];
    int count = collectKeysInOrder(root, out, 10);
    TEST_ASSERT(count == 3, "Should have 3 keys");
    TEST_ASSERT(out[0] == 10 && out[1] == 20 && out[2] == 30, "Should be sorted");
    freeTree(root);
    TEST_PASS();
}

void test_verifyInOrder_correct(void) {
    TEST_START("verifyInOrder returns 1 on correct order");
    Node *root = NULL;
    createNodeC(&root);
    for (int i = 1; i <= 4; i++) {
        type t = make_type_i32(i * 10, i);
        addElement(root, t, 0, 0, all_var_from_int32(0));
    }
    int64_t expected[] = {10, 20, 30, 40};
    TEST_ASSERT(verifyInOrder(root, expected, 4) == 1, "Should verify correctly");
    freeTree(root);
    TEST_PASS();
}

void test_verifyInOrder_wrong(void) {
    TEST_START("verifyInOrder returns 0 on wrong order");
    Node *root = NULL;
    createNodeC(&root);
    type t = make_type_i32(42, 1);
    addElement(root, t, 0, 0, all_var_from_int32(0));

    int64_t expected[] = {99};
    TEST_ASSERT(verifyInOrder(root, expected, 1) == 0, "Should detect mismatch");
    freeTree(root);
    TEST_PASS();
}

// ============================================================================
//  7. clearNodesTab
// ============================================================================

void test_clearNodesTab(void) {
    TEST_START("clearNodesTab sets all nodes to NULL");
    Node *root = NULL;
    createNodeC(&root);
    for (int i = 0; i < M; i++) {
        root->nodes[i] = malloc(sizeof(type));
    }
    clearNodesTab(root, M);
    for (int i = 0; i < M; i++) {
        TEST_ASSERT(root->nodes[i] == NULL, "Should be NULL");
    }
    free(root);
    TEST_PASS();
}

// ============================================================================
//  8. STRING VALUES
// ============================================================================

void test_add_string_elements(void) {
    TEST_START("Add string elements — sorted alphabetically");
    Node *root = NULL;
    createNodeC(&root);

    type t1 = make_type_str("charlie", 3);
    type t2 = make_type_str("alice", 1);
    type t3 = make_type_str("bob", 2);
    addElement(root, t1, 0, 0, all_var_from_int32(0));
    addElement(root, t2, 0, 0, all_var_from_int32(0));
    addElement(root, t3, 0, 0, all_var_from_int32(0));

    TEST_ASSERT(root->countNodes == 3, "Should have 3 nodes");
    // Strings should be sorted: alice < bob < charlie
    TEST_ASSERT(strcmp(root->nodes[0]->val.val.str, "alice") == 0, "1st should be alice");
    TEST_ASSERT(strcmp(root->nodes[1]->val.val.str, "bob") == 0, "2nd should be bob");
    TEST_ASSERT(strcmp(root->nodes[2]->val.val.str, "charlie") == 0, "3rd should be charlie");
    freeTree(root);
    TEST_PASS();
}

void test_getBlock_string(void) {
    TEST_START("getBlock finds string value");
    Node *root = NULL;
    createNodeC(&root);

    type t1 = make_type_str("hello", 10);
    type t2 = make_type_str("world", 20);
    addElement(root, t1, 0, 0, all_var_from_int32(0));
    addElement(root, t2, 0, 0, all_var_from_int32(0));

    int32_t r1 = getBlock(root, all_var_from_string("hello"), 0, 0, all_var_from_int32(0));
    TEST_ASSERT(r1 == 10, "Should find hello -> 10");

    int32_t r2 = getBlock(root, all_var_from_string("world"), 0, 0, all_var_from_int32(0));
    TEST_ASSERT(r2 == 20, "Should find world -> 20");
    freeTree(root);
    TEST_PASS();
}

// ============================================================================
//  9. NEGATIVE / EDGE VALUES
// ============================================================================

void test_add_negative_values(void) {
    TEST_START("Add negative int32 values — sorted correctly");
    Node *root = NULL;
    createNodeC(&root);

    type t1 = make_type_i32(-5, 1);
    type t2 = make_type_i32(-100, 2);
    type t3 = make_type_i32(0, 3);
    type t4 = make_type_i32(-50, 4);
    addElement(root, t1, 0, 0, all_var_from_int32(0));
    addElement(root, t2, 0, 0, all_var_from_int32(0));
    addElement(root, t3, 0, 0, all_var_from_int32(0));
    addElement(root, t4, 0, 0, all_var_from_int32(0));

    TEST_ASSERT(root->nodes[0]->val.val.i32 == -100, "1st should be -100");
    TEST_ASSERT(root->nodes[1]->val.val.i32 == -50, "2nd should be -50");
    TEST_ASSERT(root->nodes[2]->val.val.i32 == -5, "3rd should be -5");
    TEST_ASSERT(root->nodes[3]->val.val.i32 == 0, "4th should be 0");
    freeTree(root);
    TEST_PASS();
}

void test_add_same_values(void) {
    TEST_START("Add same value 4 times (different blockIds)");
    Node *root = NULL;
    createNodeC(&root);

    for (int i = 0; i < 4; i++) {
        type t = make_type_i32(42, i + 1);
        addElement(root, t, 0, 0, all_var_from_int32(0));
    }
    TEST_ASSERT(root->countNodes == 4, "Should have 4 nodes");
    // All values are 42
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT(root->nodes[i]->val.val.i32 == 42, "All should be 42");
    }
    freeTree(root);
    TEST_PASS();
}

// ============================================================================
//  10. LARGER TREE (10+ elements)
// ============================================================================

void test_insert_10_ascending(void) {
    TEST_START("Insert 10 ascending values — in-order correct");
    Node *root = NULL;
    createNodeC(&root);
    srand(12345);

    for (int i = 1; i <= 10; i++) {
        type t = make_type_i32(i, i);
        addElement(root, t, 0, 0, all_var_from_int32(0));
    }

    int64_t expected[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    TEST_ASSERT(verifyInOrder(root, expected, 10) == 1, "In-order should match");
    freeTree(root);
    TEST_PASS();
}

void test_insert_10_descending(void) {
    TEST_START("Insert 10 descending values — in-order correct");
    Node *root = NULL;
    createNodeC(&root);
    srand(12345);

    for (int i = 10; i >= 1; i--) {
        type t = make_type_i32(i, i);
        addElement(root, t, 0, 0, all_var_from_int32(0));
    }

    int64_t expected[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    TEST_ASSERT(verifyInOrder(root, expected, 10) == 1, "In-order should match");
    freeTree(root);
    TEST_PASS();
}

void test_insert_15_random_order(void) {
    TEST_START("Insert 15 values in random order — all present");
    Node *root = NULL;
    createNodeC(&root);
    srand(99999);

    int vals[] = {12, 5, 19, 3, 8, 15, 1, 7, 14, 20, 11, 6, 18, 2, 9};
    for (int i = 0; i < 15; i++) {
        type t = make_type_i32(vals[i], vals[i]);
        addElement(root, t, 0, 0, all_var_from_int32(0));
    }

    int64_t collected[20];
    int count = collectKeysInOrder(root, collected, 20);
    TEST_ASSERT(count == 15, "Should have 15 keys");

    // Verify sorted
    for (int i = 0; i < count - 1; i++) {
        TEST_ASSERT(collected[i] <= collected[i + 1], "Keys should be non-decreasing");
    }
    freeTree(root);
    TEST_PASS();
}

void test_insert_20_values(void) {
    TEST_START("Insert 20 values — all present in-order");
    Node *root = NULL;
    createNodeC(&root);
    srand(42);

    for (int i = 1; i <= 20; i++) {
        type t = make_type_i32(i * 3, i);
        addElement(root, t, 0, 0, all_var_from_int32(0));
    }

    int64_t collected[25];
    int count = collectKeysInOrder(root, collected, 25);
    TEST_ASSERT(count == 20, "Should have 20 keys");

    for (int i = 0; i < count - 1; i++) {
        TEST_ASSERT(collected[i] < collected[i + 1], "Should be strictly increasing");
    }
    freeTree(root);
    TEST_PASS();
}

// ============================================================================
//  11. PRINT FUNCTIONS (no crash)
// ============================================================================

void test_printBtree_null(void) {
    TEST_START("printBtree(NULL) does not crash");
    printBtree(NULL);
    TEST_PASS();
}

void test_printBtree_nonempty(void) {
    TEST_START("printBtree on 5-element tree does not crash");
    Node *root = NULL;
    createNodeC(&root);
    srand(111);
    for (int i = 1; i <= 5; i++) {
        type t = make_type_i32(i * 10, i);
        addElement(root, t, 0, 0, all_var_from_int32(0));
    }
    printBtree(root);
    freeTree(root);
    TEST_PASS();
}

// ============================================================================
//  MAIN
// ============================================================================

int main(void) {
    srand((unsigned)time(NULL));

    printf("\n\033[1;33m╔══════════════════════════════════════════════════════════════╗\033[0m\n");
    printf("\033[1;33m║      IN-MEMORY B-TREE — COMPREHENSIVE TEST SUITE            ║\033[0m\n");
    printf("\033[1;33m╚══════════════════════════════════════════════════════════════╝\033[0m\n");

    SECTION("1. NODE CREATION");
    test_createNodeM();
    test_createNodeC();

    SECTION("2. SINGLE ELEMENT INSERT");
    test_add_single_element();
    test_add_two_elements_sorted();
    test_add_three_elements_sorted();
    test_add_four_elements_sorted();

    SECTION("3. NODE SPLIT");
    test_split_on_fifth_element();
    test_split_preserves_all_keys();
    test_split_with_8_elements();

    SECTION("4. getBlock (SEARCH)");
    test_getBlock_single_value();
    test_getBlock_multiple_values();
    test_getBlock_null_root();
    test_getBlock_empty_node();

    SECTION("5. SORTING (quickSort)");
    test_quickSort_already_sorted();
    test_quickSort_reverse_sorted();
    test_quickSort_single_element();
    test_quickSort_duplicates();

    SECTION("6. IN-ORDER TRAVERSAL");
    test_collectKeys_empty();
    test_collectKeys_null();
    test_collectKeys_three();
    test_verifyInOrder_correct();
    test_verifyInOrder_wrong();

    SECTION("7. clearNodesTab");
    test_clearNodesTab();

    SECTION("8. STRING VALUES");
    test_add_string_elements();
    test_getBlock_string();

    SECTION("9. NEGATIVE / EDGE VALUES");
    test_add_negative_values();
    test_add_same_values();

    SECTION("10. LARGER TREE (10+ elements)");
    test_insert_10_ascending();
    test_insert_10_descending();
    test_insert_15_random_order();
    test_insert_20_values();

    SECTION("11. PRINT FUNCTIONS (no crash)");
    test_printBtree_null();
    test_printBtree_nonempty();

    printf("\n\033[1;33m══════════════════════════════════════════════════════════════\033[0m\n");
    printf("  Total: %d  |  \033[32mPassed: %d\033[0m  |  \033[31mFailed: %d\033[0m\n",
           tests_passed + tests_failed, tests_passed, tests_failed);
    printf("\033[1;33m══════════════════════════════════════════════════════════════\033[0m\n\n");

    return tests_failed > 0 ? 1 : 0;
}
