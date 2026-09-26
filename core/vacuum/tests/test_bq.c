/*
 * test_bq.c - tests for the bucket queue (bq_t) and BqManager
 *
 * Tests:
 *   1. Basic operations: insert, get, remove
 *   2. Incr / decr moving between buckets
 *   3. Min / max
 *   4. Bucket iteration
 *   5. Configurable size (nbuckets)
 *   6. Buffer limit (max_entries)
 *   7. BqManager - per-table queues
 *   8. Edge cases and guard clauses
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../vacuum/bq.h"

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
 *  1. BASIC INSERT / GET / REMOVE
 * ============================================================================ */

void test_insert_and_get(void) {
    TEST_START("insert and get single value");
    bq_t q;
    TEST_ASSERT(bq_init(&q, 256, 16, 0) == BQ_OK, "init");

    TEST_ASSERT(bq_insert(&q, 10, 5) == BQ_OK, "insert key=10 val=5");
    int32_t out = -1;
    TEST_ASSERT(bq_get(&q, 10, &out) == BQ_OK, "get key=10");
    TEST_ASSERT(out == 5, "val should be 5");
    TEST_ASSERT(bq_size(&q) == 1, "size should be 1");

    bq_free(&q);
    TEST_PASS();
}

void test_insert_multiple(void) {
    TEST_START("insert multiple keys");
    bq_t q;
    bq_init(&q, 256, 16, 0);

    for (int i = 0; i < 50; i++) {
        TEST_ASSERT(bq_insert(&q, i, i % 10) == BQ_OK, "insert");
    }
    TEST_ASSERT(bq_size(&q) == 50, "size 50");

    for (int i = 0; i < 50; i++) {
        int32_t out;
        TEST_ASSERT(bq_get(&q, i, &out) == BQ_OK, "get");
        TEST_ASSERT(out == i % 10, "val matches");
    }

    bq_free(&q);
    TEST_PASS();
}

void test_insert_duplicate(void) {
    TEST_START("insert duplicate returns BQ_EEXISTS");
    bq_t q;
    bq_init(&q, 256, 16, 0);

    TEST_ASSERT(bq_insert(&q, 10, 5) == BQ_OK, "first insert");
    TEST_ASSERT(bq_insert(&q, 10, 3) == BQ_EEXISTS, "duplicate insert");
    TEST_ASSERT(bq_size(&q) == 1, "size still 1");

    bq_free(&q);
    TEST_PASS();
}

void test_remove(void) {
    TEST_START("remove key");
    bq_t q;
    bq_init(&q, 256, 16, 0);

    bq_insert(&q, 10, 5);
    bq_insert(&q, 20, 3);
    TEST_ASSERT(bq_size(&q) == 2, "size 2");

    TEST_ASSERT(bq_remove(&q, 10) == BQ_OK, "remove key=10");
    TEST_ASSERT(bq_size(&q) == 1, "size 1");
    TEST_ASSERT(bq_get(&q, 10, NULL) == BQ_ENOTFOUND, "key=10 gone");
    int32_t out;
    TEST_ASSERT(bq_get(&q, 20, &out) == BQ_OK, "key=20 still there");
    TEST_ASSERT(out == 3, "val still 3");

    bq_free(&q);
    TEST_PASS();
}

void test_remove_not_found(void) {
    TEST_START("remove non-existent key");
    bq_t q;
    bq_init(&q, 256, 16, 0);
    TEST_ASSERT(bq_remove(&q, 999) == BQ_ENOTFOUND, "not found");
    bq_free(&q);
    TEST_PASS();
}

void test_get_not_found(void) {
    TEST_START("get non-existent key");
    bq_t q;
    bq_init(&q, 256, 16, 0);
    TEST_ASSERT(bq_get(&q, 42, NULL) == BQ_ENOTFOUND, "not found");
    bq_free(&q);
    TEST_PASS();
}

/* ============================================================================
 *  2. INCR / DECR
 * ============================================================================ */

void test_incr(void) {
    TEST_START("incr moves value up");
    bq_t q;
    bq_init(&q, 256, 16, 0);
    bq_insert(&q, 10, 5);

    int32_t out;
    TEST_ASSERT(bq_incr(&q, 10, &out) == BQ_OK, "incr");
    TEST_ASSERT(out == 6, "val should be 6");

    bq_get(&q, 10, &out);
    TEST_ASSERT(out == 6, "get confirms 6");

    bq_free(&q);
    TEST_PASS();
}

void test_decr(void) {
    TEST_START("decr moves value down");
    bq_t q;
    bq_init(&q, 256, 16, 0);
    bq_insert(&q, 10, 5);

    int32_t out;
    TEST_ASSERT(bq_decr(&q, 10, &out) == BQ_OK, "decr");
    TEST_ASSERT(out == 4, "val should be 4");

    bq_free(&q);
    TEST_PASS();
}

void test_incr_at_max_returns_erange(void) {
    TEST_START("incr at nbuckets-1 returns BQ_ERANGE");
    bq_t q;
    bq_init(&q, 10, 16, 0);
    bq_insert(&q, 1, 9); /* max val = nbuckets-1 = 9 */

    TEST_ASSERT(bq_incr(&q, 1, NULL) == BQ_ERANGE, "incr beyond max");

    bq_free(&q);
    TEST_PASS();
}

void test_decr_at_zero_returns_erange(void) {
    TEST_START("decr at 0 returns BQ_ERANGE");
    bq_t q;
    bq_init(&q, 256, 16, 0);
    bq_insert(&q, 1, 0);

    TEST_ASSERT(bq_decr(&q, 1, NULL) == BQ_ERANGE, "decr below 0");

    bq_free(&q);
    TEST_PASS();
}

void test_incr_decr_chain(void) {
    TEST_START("incr 5x then decr 5x returns to original");
    bq_t q;
    bq_init(&q, 256, 16, 0);
    bq_insert(&q, 1, 10);

    for (int i = 0; i < 5; i++) bq_incr(&q, 1, NULL);
    for (int i = 0; i < 5; i++) bq_decr(&q, 1, NULL);

    int32_t out;
    bq_get(&q, 1, &out);
    TEST_ASSERT(out == 10, "val back to 10");

    bq_free(&q);
    TEST_PASS();
}

/* ============================================================================
 *  3. MIN / MAX
 * ============================================================================ */

void test_min_max_single(void) {
    TEST_START("min/max with single element");
    bq_t q;
    bq_init(&q, 256, 16, 0);
    bq_insert(&q, 1, 42);

    int32_t mn, mx;
    TEST_ASSERT(bq_min(&q, &mn) == BQ_OK, "min ok");
    TEST_ASSERT(bq_max(&q, &mx) == BQ_OK, "max ok");
    TEST_ASSERT(mn == 42, "min == 42");
    TEST_ASSERT(mx == 42, "max == 42");

    bq_free(&q);
    TEST_PASS();
}

void test_min_max_multiple(void) {
    TEST_START("min/max with multiple elements");
    bq_t q;
    bq_init(&q, 256, 16, 0);
    bq_insert(&q, 1, 10);
    bq_insert(&q, 2, 50);
    bq_insert(&q, 3, 5);
    bq_insert(&q, 4, 200);

    int32_t mn, mx;
    bq_min(&q, &mn);
    bq_max(&q, &mx);
    TEST_ASSERT(mn == 5, "min == 5");
    TEST_ASSERT(mx == 200, "max == 200");

    bq_free(&q);
    TEST_PASS();
}

void test_min_max_empty(void) {
    TEST_START("min/max on empty queue returns ENOTFOUND");
    bq_t q;
    bq_init(&q, 256, 16, 0);

    TEST_ASSERT(bq_min(&q, NULL) == BQ_ENOTFOUND, "min empty");
    TEST_ASSERT(bq_max(&q, NULL) == BQ_ENOTFOUND, "max empty");

    bq_free(&q);
    TEST_PASS();
}

void test_min_max_after_remove(void) {
    TEST_START("min/max updates after remove");
    bq_t q;
    bq_init(&q, 256, 16, 0);
    bq_insert(&q, 1, 5);
    bq_insert(&q, 2, 50);
    bq_insert(&q, 3, 100);

    bq_remove(&q, 1); /* remove min */
    int32_t mn;
    bq_min(&q, &mn);
    TEST_ASSERT(mn == 50, "new min == 50");

    bq_remove(&q, 3); /* remove max */
    int32_t mx;
    bq_max(&q, &mx);
    TEST_ASSERT(mx == 50, "new max == 50");

    bq_free(&q);
    TEST_PASS();
}

/* ============================================================================
 *  4. BUCKET ITERATION
 * ============================================================================ */

void test_bucket_iteration(void) {
    TEST_START("iterate buckets in order");
    bq_t q;
    bq_init(&q, 256, 16, 0);
    bq_insert(&q, 1, 10);
    bq_insert(&q, 2, 20);
    bq_insert(&q, 3, 10); /* same bucket as key=1 */
    bq_insert(&q, 4, 30);

    int32_t val;
    TEST_ASSERT(bq_first_bucket(&q, &val) == BQ_OK, "first bucket");
    TEST_ASSERT(val == 10, "first bucket == 10");

    TEST_ASSERT(bq_next_bucket(&q, val, &val) == BQ_OK, "next bucket");
    TEST_ASSERT(val == 20, "second bucket == 20");

    TEST_ASSERT(bq_next_bucket(&q, val, &val) == BQ_OK, "next bucket");
    TEST_ASSERT(val == 30, "third bucket == 30");

    TEST_ASSERT(bq_next_bucket(&q, val, &val) == BQ_ENOTFOUND, "no more buckets");

    bq_free(&q);
    TEST_PASS();
}

void test_bucket_elements(void) {
    TEST_START("list elements in a bucket");
    bq_t q;
    bq_init(&q, 256, 16, 0);
    bq_insert(&q, 100, 5);
    bq_insert(&q, 200, 5);
    bq_insert(&q, 300, 5);

    int count = 0;
    uint32_t node = bq_bucket_head(&q, 5);
    while (node != BQ_NIL) {
        count++;
        node = bq_node_next(&q, node);
    }
    TEST_ASSERT(count == 3, "3 elements in bucket val=5");

    bq_free(&q);
    TEST_PASS();
}

/* ============================================================================
 *  5. CONFIGURABLE NBUCKETS
 * ============================================================================ */

void test_small_nbuckets(void) {
    TEST_START("small nbuckets (8): works within range");
    bq_t q;
    TEST_ASSERT(bq_init(&q, 8, 16, 0) == BQ_OK, "init nbuckets=8");

    TEST_ASSERT(bq_insert(&q, 1, 0) == BQ_OK, "val=0 ok");
    TEST_ASSERT(bq_insert(&q, 2, 7) == BQ_OK, "val=7 ok");
    TEST_ASSERT(bq_insert(&q, 3, 8) == BQ_ERANGE, "val=8 out of range");

    int32_t mn, mx;
    bq_min(&q, &mn);
    bq_max(&q, &mx);
    TEST_ASSERT(mn == 0, "min=0");
    TEST_ASSERT(mx == 7, "max=7");

    bq_free(&q);
    TEST_PASS();
}

void test_large_nbuckets(void) {
    TEST_START("large nbuckets (100000): basic operations");
    bq_t q;
    TEST_ASSERT(bq_init(&q, 100000, 16, 0) == BQ_OK, "init nbuckets=100000");

    bq_insert(&q, 1, 0);
    bq_insert(&q, 2, 99999);
    bq_insert(&q, 3, 50000);

    int32_t mn, mx;
    bq_min(&q, &mn);
    bq_max(&q, &mx);
    TEST_ASSERT(mn == 0, "min=0");
    TEST_ASSERT(mx == 99999, "max=99999");

    bq_free(&q);
    TEST_PASS();
}

void test_zero_nbuckets(void) {
    TEST_START("nbuckets=0 returns ERANGE");
    bq_t q;
    TEST_ASSERT(bq_init(&q, 0, 16, 0) == BQ_ERANGE, "init fails with 0");
    TEST_PASS();
}

/* ============================================================================
 *  6. BUFFER LIMIT (max_entries)
 * ============================================================================ */

void test_max_entries_limit(void) {
    TEST_START("max_entries limits inserts");
    bq_t q;
    bq_init(&q, 256, 4, 5);

    for (int i = 0; i < 5; i++) {
        TEST_ASSERT(bq_insert(&q, i, i) == BQ_OK, "insert within limit");
    }
    TEST_ASSERT(bq_insert(&q, 99, 0) == BQ_ENOMEM, "insert beyond limit fails");
    TEST_ASSERT(bq_size(&q) == 5, "size stays at 5");

    bq_free(&q);
    TEST_PASS();
}

void test_max_entries_remove_and_reinsert(void) {
    TEST_START("max_entries: remove frees slot for new insert");
    bq_t q;
    bq_init(&q, 256, 4, 3);

    bq_insert(&q, 1, 10);
    bq_insert(&q, 2, 20);
    bq_insert(&q, 3, 30);
    TEST_ASSERT(bq_insert(&q, 4, 40) == BQ_ENOMEM, "full");

    bq_remove(&q, 2);
    TEST_ASSERT(bq_insert(&q, 4, 40) == BQ_OK, "insert after remove ok");
    TEST_ASSERT(bq_size(&q) == 3, "size 3");

    bq_free(&q);
    TEST_PASS();
}

void test_no_max_entries_unlimited(void) {
    TEST_START("max_entries=0: no limit");
    bq_t q;
    bq_init(&q, 256, 4, 0);

    for (int i = 0; i < 100; i++) {
        TEST_ASSERT(bq_insert(&q, i, i % 256) == BQ_OK, "insert unlimited");
    }
    TEST_ASSERT(bq_size(&q) == 100, "size 100");

    bq_free(&q);
    TEST_PASS();
}

/* ============================================================================
 *  7. BqManager
 * ============================================================================ */

void test_mgr_add_and_get(void) {
    TEST_START("BqManager: add table and get queue");
    BqManager mgr;
    bq_mgr_init(&mgr, 128, 0);

    TEST_ASSERT(bq_mgr_add_table(&mgr, 10) == BQ_OK, "add table 10");
    bq_t *q = bq_mgr_get(&mgr, 10);
    TEST_ASSERT(q != NULL, "get returns non-null");
    TEST_ASSERT(q->nbuckets == 128, "nbuckets matches default");

    bq_insert(q, 5, 10);
    int32_t out;
    bq_get(q, 5, &out);
    TEST_ASSERT(out == 10, "val correct");

    bq_mgr_free(&mgr);
    TEST_PASS();
}

void test_mgr_multiple_tables(void) {
    TEST_START("BqManager: multiple independent tables");
    BqManager mgr;
    bq_mgr_init(&mgr, 64, 0);

    bq_mgr_add_table(&mgr, 1);
    bq_mgr_add_table(&mgr, 2);

    bq_t *q1 = bq_mgr_get(&mgr, 1);
    bq_t *q2 = bq_mgr_get(&mgr, 2);
    TEST_ASSERT(q1 != q2, "different queues");

    bq_insert(q1, 100, 5);
    bq_insert(q2, 100, 50);

    int32_t v1, v2;
    bq_get(q1, 100, &v1);
    bq_get(q2, 100, &v2);
    TEST_ASSERT(v1 == 5, "table1 val=5");
    TEST_ASSERT(v2 == 50, "table2 val=50");

    bq_mgr_free(&mgr);
    TEST_PASS();
}

void test_mgr_duplicate_table(void) {
    TEST_START("BqManager: duplicate table returns EEXISTS");
    BqManager mgr;
    bq_mgr_init(&mgr, 64, 0);

    TEST_ASSERT(bq_mgr_add_table(&mgr, 1) == BQ_OK, "first add");
    TEST_ASSERT(bq_mgr_add_table(&mgr, 1) == BQ_EEXISTS, "duplicate");

    bq_mgr_free(&mgr);
    TEST_PASS();
}

void test_mgr_remove_table(void) {
    TEST_START("BqManager: remove table frees slot");
    BqManager mgr;
    bq_mgr_init(&mgr, 64, 0);

    bq_mgr_add_table(&mgr, 1);
    TEST_ASSERT(bq_mgr_get(&mgr, 1) != NULL, "table exists");

    TEST_ASSERT(bq_mgr_remove_table(&mgr, 1) == BQ_OK, "remove ok");
    TEST_ASSERT(bq_mgr_get(&mgr, 1) == NULL, "table gone");

    /* can be added again */
    TEST_ASSERT(bq_mgr_add_table(&mgr, 1) == BQ_OK, "re-add ok");

    bq_mgr_free(&mgr);
    TEST_PASS();
}

void test_mgr_get_nonexistent(void) {
    TEST_START("BqManager: get nonexistent table returns NULL");
    BqManager mgr;
    bq_mgr_init(&mgr, 64, 0);
    TEST_ASSERT(bq_mgr_get(&mgr, 999) == NULL, "NULL for missing table");
    bq_mgr_free(&mgr);
    TEST_PASS();
}

void test_mgr_with_max_entries(void) {
    TEST_START("BqManager: default_max_entries propagates to tables");
    BqManager mgr;
    bq_mgr_init(&mgr, 64, 10);

    bq_mgr_add_table(&mgr, 1);
    bq_t *q = bq_mgr_get(&mgr, 1);
    TEST_ASSERT(q->max_entries == 10, "max_entries set");

    for (int i = 0; i < 10; i++) bq_insert(q, i, 0);
    TEST_ASSERT(bq_insert(q, 99, 0) == BQ_ENOMEM, "limited by max_entries");

    bq_mgr_free(&mgr);
    TEST_PASS();
}

/* ============================================================================
 *  8. STRESS / EDGE CASES
 * ============================================================================ */

void test_insert_remove_reinsert(void) {
    TEST_START("insert, remove, reinsert same key");
    bq_t q;
    bq_init(&q, 256, 16, 0);

    bq_insert(&q, 1, 10);
    bq_remove(&q, 1);
    TEST_ASSERT(bq_get(&q, 1, NULL) == BQ_ENOTFOUND, "removed");

    TEST_ASSERT(bq_insert(&q, 1, 20) == BQ_OK, "reinsert ok");
    int32_t out;
    bq_get(&q, 1, &out);
    TEST_ASSERT(out == 20, "new val=20");

    bq_free(&q);
    TEST_PASS();
}

void test_stress_100_elements(void) {
    TEST_START("stress: 100 inserts, incr all, check min/max");
    bq_t q;
    bq_init(&q, 1024, 16, 0);

    for (int i = 0; i < 100; i++) {
        bq_insert(&q, i, 0);
    }
    /* incr each to its index value */
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < i; j++) bq_incr(&q, i, NULL);
    }

    int32_t mn, mx;
    bq_min(&q, &mn);
    bq_max(&q, &mx);
    TEST_ASSERT(mn == 0, "min=0 (key=0 was never incr'd)");
    TEST_ASSERT(mx == 99, "max=99 (key=99 incr'd 99 times)");

    /* verify each */
    for (int i = 0; i < 100; i++) {
        int32_t v;
        bq_get(&q, i, &v);
        TEST_ASSERT(v == i, "val matches index");
    }

    bq_free(&q);
    TEST_PASS();
}

void test_value_zero(void) {
    TEST_START("value 0 works correctly");
    bq_t q;
    bq_init(&q, 256, 16, 0);

    bq_insert(&q, 1, 0);
    int32_t out;
    bq_get(&q, 1, &out);
    TEST_ASSERT(out == 0, "val=0");

    int32_t mn;
    bq_min(&q, &mn);
    TEST_ASSERT(mn == 0, "min=0");

    bq_free(&q);
    TEST_PASS();
}

void test_negative_val_rejected(void) {
    TEST_START("negative value rejected");
    bq_t q;
    bq_init(&q, 256, 16, 0);
    TEST_ASSERT(bq_insert(&q, 1, -1) == BQ_ERANGE, "negative val rejected");
    bq_free(&q);
    TEST_PASS();
}

/* ============================================================================
 *  9. SAVE / LOAD (persistence)
 * ============================================================================ */

#include <unistd.h>

static const char *TEST_BQ_FILE = "/tmp/test_bq_save.bin";

static void cleanup_file(void) { unlink(TEST_BQ_FILE); }

void test_save_and_load_basic(void) {
    TEST_START("save and load: basic round-trip");
    cleanup_file();
    bq_t q;
    bq_init(&q, 256, 16, 0);
    bq_insert(&q, 1, 10);
    bq_insert(&q, 2, 20);
    bq_insert(&q, 3, 30);

    TEST_ASSERT(bq_save(&q, TEST_BQ_FILE) == BQ_OK, "save ok");
    bq_free(&q);

    bq_t loaded;
    TEST_ASSERT(bq_load(&loaded, TEST_BQ_FILE) == BQ_OK, "load ok");
    TEST_ASSERT(bq_size(&loaded) == 3, "size 3");

    int32_t v;
    bq_get(&loaded, 1, &v); TEST_ASSERT(v == 10, "key=1 val=10");
    bq_get(&loaded, 2, &v); TEST_ASSERT(v == 20, "key=2 val=20");
    bq_get(&loaded, 3, &v); TEST_ASSERT(v == 30, "key=3 val=30");

    bq_free(&loaded);
    cleanup_file();
    TEST_PASS();
}

void test_save_and_load_preserves_config(void) {
    TEST_START("save/load: preserves nbuckets and max_entries");
    cleanup_file();
    bq_t q;
    bq_init(&q, 64, 16, 20);
    bq_insert(&q, 100, 50);

    bq_save(&q, TEST_BQ_FILE);
    bq_free(&q);

    bq_t loaded;
    bq_load(&loaded, TEST_BQ_FILE);
    TEST_ASSERT(loaded.nbuckets == 64, "nbuckets preserved");
    TEST_ASSERT(loaded.max_entries == 20, "max_entries preserved");

    int32_t v;
    bq_get(&loaded, 100, &v);
    TEST_ASSERT(v == 50, "val preserved");

    bq_free(&loaded);
    cleanup_file();
    TEST_PASS();
}

void test_save_and_load_empty(void) {
    TEST_START("save/load: empty queue");
    cleanup_file();
    bq_t q;
    bq_init(&q, 128, 16, 0);

    bq_save(&q, TEST_BQ_FILE);
    bq_free(&q);

    bq_t loaded;
    TEST_ASSERT(bq_load(&loaded, TEST_BQ_FILE) == BQ_OK, "load empty ok");
    TEST_ASSERT(bq_size(&loaded) == 0, "size 0");
    TEST_ASSERT(bq_min(&loaded, NULL) == BQ_ENOTFOUND, "min not found");

    bq_free(&loaded);
    cleanup_file();
    TEST_PASS();
}

void test_save_and_load_min_max(void) {
    TEST_START("save/load: min/max correct after load");
    cleanup_file();
    bq_t q;
    bq_init(&q, 1024, 16, 0);
    bq_insert(&q, 1, 5);
    bq_insert(&q, 2, 500);
    bq_insert(&q, 3, 100);

    bq_save(&q, TEST_BQ_FILE);
    bq_free(&q);

    bq_t loaded;
    bq_load(&loaded, TEST_BQ_FILE);

    int32_t mn, mx;
    bq_min(&loaded, &mn);
    bq_max(&loaded, &mx);
    TEST_ASSERT(mn == 5, "min=5");
    TEST_ASSERT(mx == 500, "max=500");

    bq_free(&loaded);
    cleanup_file();
    TEST_PASS();
}

void test_save_and_load_many_entries(void) {
    TEST_START("save/load: 200 entries round-trip");
    cleanup_file();
    bq_t q;
    bq_init(&q, 1024, 16, 0);

    for (int i = 0; i < 200; i++) {
        bq_insert(&q, i * 10, i % 1024);
    }

    bq_save(&q, TEST_BQ_FILE);
    bq_free(&q);

    bq_t loaded;
    bq_load(&loaded, TEST_BQ_FILE);
    TEST_ASSERT(bq_size(&loaded) == 200, "size 200");

    for (int i = 0; i < 200; i++) {
        int32_t v;
        TEST_ASSERT(bq_get(&loaded, i * 10, &v) == BQ_OK, "key found");
        TEST_ASSERT(v == i % 1024, "val matches");
    }

    bq_free(&loaded);
    cleanup_file();
    TEST_PASS();
}

void test_load_nonexistent_file(void) {
    TEST_START("load: nonexistent file returns error");
    bq_t q;
    TEST_ASSERT(bq_load(&q, "/tmp/bq_nonexistent_42.bin") == BQ_ENOTFOUND, "file not found");
    TEST_PASS();
}

void test_save_load_then_modify(void) {
    TEST_START("save/load: modify after load works");
    cleanup_file();
    bq_t q;
    bq_init(&q, 256, 16, 0);
    bq_insert(&q, 1, 10);
    bq_save(&q, TEST_BQ_FILE);
    bq_free(&q);

    bq_t loaded;
    bq_load(&loaded, TEST_BQ_FILE);

    /* modify loaded queue */
    bq_incr(&loaded, 1, NULL);
    bq_insert(&loaded, 2, 50);
    bq_remove(&loaded, 1);

    TEST_ASSERT(bq_size(&loaded) == 1, "size 1");
    TEST_ASSERT(bq_get(&loaded, 1, NULL) == BQ_ENOTFOUND, "key=1 removed");
    int32_t v;
    bq_get(&loaded, 2, &v);
    TEST_ASSERT(v == 50, "key=2 val=50");

    bq_free(&loaded);
    cleanup_file();
    TEST_PASS();
}

/* ============================================================================
 *  Main
 * ============================================================================ */

int main(void) {
    SECTION("1. BASIC INSERT / GET / REMOVE");
    test_insert_and_get();
    test_insert_multiple();
    test_insert_duplicate();
    test_remove();
    test_remove_not_found();
    test_get_not_found();

    SECTION("2. INCR / DECR");
    test_incr();
    test_decr();
    test_incr_at_max_returns_erange();
    test_decr_at_zero_returns_erange();
    test_incr_decr_chain();

    SECTION("3. MIN / MAX");
    test_min_max_single();
    test_min_max_multiple();
    test_min_max_empty();
    test_min_max_after_remove();

    SECTION("4. BUCKET ITERATION");
    test_bucket_iteration();
    test_bucket_elements();

    SECTION("5. CONFIGURABLE NBUCKETS");
    test_small_nbuckets();
    test_large_nbuckets();
    test_zero_nbuckets();

    SECTION("6. BUFFER LIMIT (max_entries)");
    test_max_entries_limit();
    test_max_entries_remove_and_reinsert();
    test_no_max_entries_unlimited();

    SECTION("7. BqManager");
    test_mgr_add_and_get();
    test_mgr_multiple_tables();
    test_mgr_duplicate_table();
    test_mgr_remove_table();
    test_mgr_get_nonexistent();
    test_mgr_with_max_entries();

    SECTION("8. STRESS / EDGE CASES");
    test_insert_remove_reinsert();
    test_stress_100_elements();
    test_value_zero();
    test_negative_val_rejected();

    SECTION("9. SAVE / LOAD (persistence)");
    test_save_and_load_basic();
    test_save_and_load_preserves_config();
    test_save_and_load_empty();
    test_save_and_load_min_max();
    test_save_and_load_many_entries();
    test_load_nonexistent_file();
    test_save_load_then_modify();

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