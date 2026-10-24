//
// Buffered flatMap tests
// Tests: probing table (insert/search/remove with collisions), buffered int32 -> int64 map,
//        buffered int64 -> int64 map (init, insert, update, remove, eviction, persistence, page full, stress)
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "../../memory-mgmt/memory-mgmt/config.h"
#include "../indexes/flatMap.h"

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

#define TEST_FILE_32 INDEX_TABLE_PATH "/flatmap_test_32.bin"
#define TEST_FILE_64 INDEX_TABLE_PATH "/flatmap_test_64.bin"

static long file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    return size;
}

// deterministic random numbers, the same run every time
static uint64_t rng_state = 42;
static uint64_t rng_next(void) {
    rng_state = splitmix64(rng_state);
    return rng_state;
}

// ============================================================================
//  1. PROBING TABLE (one page, no buffering)
// ============================================================================

void test_table_insert_search(void) {
    TEST_START("insert64x64 / search64x64 basic");
    struct Entry64x64 table[16];
    initialize_table64x64(table, 16);
    TEST_ASSERT(insert64x64(table, 16, 5, 500) == 0, "insert should succeed");
    TEST_ASSERT(insert64x64(table, 16, 7, 700) == 0, "insert should succeed");
    int found = 0;
    TEST_ASSERT(search64x64(table, 16, 5, &found) == 500 && found, "key 5");
    TEST_ASSERT(search64x64(table, 16, 7, &found) == 700 && found, "key 7");
    search64x64(table, 16, 6, &found);
    TEST_ASSERT(!found, "key 6 is not there");
    TEST_PASS();
}

void test_table_empty_key_rejected(void) {
    TEST_START("EMPTY_KEY (-1) cannot be inserted, found or removed");
    struct Entry64x64 t64[8];
    struct Entry64 t32[8];
    initialize_table64x64(t64, 8);
    initialize_table64(t32, 8);
    TEST_ASSERT(insert64x64(t64, 8, EMPTY_KEY, 1) == -1, "64: insert -1");
    TEST_ASSERT(insert64(t32, 8, EMPTY_KEY, 1) == -1, "32: insert -1");
    int found = 1;
    search64x64(t64, 8, EMPTY_KEY, &found);
    TEST_ASSERT(!found, "64: search -1 on an empty table");
    found = 1;
    search64(t32, 8, EMPTY_KEY, &found);
    TEST_ASSERT(!found, "32: search -1 on an empty table");
    TEST_ASSERT(remove64x64(t64, 8, EMPTY_KEY) == 0, "64: remove -1");
    TEST_ASSERT(remove64(t32, 8, EMPTY_KEY) == 0, "32: remove -1");
    TEST_PASS();
}

void test_table_full(void) {
    TEST_START("full table: insert of a new key fails, update still works");
    struct Entry64x64 table[8];
    initialize_table64x64(table, 8);
    for (int64_t k = 0; k < 8; k++) {
        TEST_ASSERT(insert64x64(table, 8, k, k * 10) == 0, "fill");
    }
    TEST_ASSERT(insert64x64(table, 8, 100, 1) == -1, "9th key should fail");
    TEST_ASSERT(insert64x64(table, 8, 3, 333) == 0, "update in a full table");
    int found = 0;
    TEST_ASSERT(search64x64(table, 8, 3, &found) == 333 && found, "updated value");
    search64x64(table, 8, 100, &found);
    TEST_ASSERT(!found, "missing key in a full table terminates");
    TEST_PASS();
}

void test_table_remove_keeps_chain(void) {
    TEST_START("remove64x64 in a collision chain keeps the other keys");
    // small table — many keys share a chain
    struct Entry64x64 table[8];
    initialize_table64x64(table, 8);
    for (int64_t k = 0; k < 7; k++) {
        TEST_ASSERT(insert64x64(table, 8, k, k + 100) == 0, "fill");
    }
    for (int64_t r = 0; r < 7; r++) {
        TEST_ASSERT(remove64x64(table, 8, r) == 1, "remove");
        int found = 0;
        search64x64(table, 8, r, &found);
        TEST_ASSERT(!found, "removed key is gone");
        for (int64_t k = r + 1; k < 7; k++) {
            TEST_ASSERT(search64x64(table, 8, k, &found) == k + 100 && found, "remaining key lost");
        }
    }
    TEST_ASSERT(remove64x64(table, 8, 3) == 0, "remove of a missing key");
    TEST_PASS();
}

void test_table_remove_full_table(void) {
    TEST_START("remove from a 100% full table (no empty slot)");
    struct Entry64x64 table[8];
    initialize_table64x64(table, 8);
    for (int64_t k = 10; k < 18; k++) insert64x64(table, 8, k, k);
    TEST_ASSERT(remove64x64(table, 8, 13) == 1, "remove");
    int found = 0;
    for (int64_t k = 10; k < 18; k++) {
        search64x64(table, 8, k, &found);
        TEST_ASSERT(found == (k != 13), "only key 13 is gone");
    }
    TEST_ASSERT(insert64x64(table, 8, 99, 99) == 0, "the freed slot is reused");
    TEST_PASS();
}

void test_table_remove_random_64(void) {
    TEST_START("remove64x64 random: 20000 ops on 32 slots vs reference");
    enum { SIZE = 32, KEYS = 48 };
    struct Entry64x64 table[SIZE];
    initialize_table64x64(table, SIZE);
    int64_t ref[KEYS];
    int present[KEYS] = {0};
    int count = 0;
    for (int op = 0; op < 20000; op++) {
        int k = (int)(rng_next() % KEYS);
        int64_t key = (int64_t)k * 1000003 - 7; // spread, includes negatives
        if (rng_next() % 2 == 0) {
            int64_t val = (int64_t)rng_next();
            int r = insert64x64(table, SIZE, key, val);
            if (present[k] || count < SIZE) {
                TEST_ASSERT(r == 0, "insert should succeed");
                if (!present[k]) count++;
                present[k] = 1;
                ref[k] = val;
            } else {
                TEST_ASSERT(r == -1, "insert into a full table");
            }
        } else {
            TEST_ASSERT(remove64x64(table, SIZE, key) == present[k], "remove result");
            if (present[k]) count--;
            present[k] = 0;
        }
        for (int c = 0; c < KEYS; c++) {
            int found = 0;
            int64_t v = search64x64(table, SIZE, (int64_t)c * 1000003 - 7, &found);
            TEST_ASSERT(found == present[c], "presence differs from reference");
            if (found) TEST_ASSERT(v == ref[c], "value differs from reference");
        }
    }
    TEST_PASS();
}

void test_table_remove_random_32(void) {
    TEST_START("remove64 (int32 key) random: 20000 ops on 32 slots vs reference");
    enum { SIZE = 32, KEYS = 48 };
    struct Entry64 table[SIZE];
    initialize_table64(table, SIZE);
    int64_t ref[KEYS];
    int present[KEYS] = {0};
    int count = 0;
    for (int op = 0; op < 20000; op++) {
        int k = (int)(rng_next() % KEYS);
        int32_t key = k * 7919 - 100;
        if (rng_next() % 2 == 0) {
            int64_t val = (int64_t)rng_next();
            int r = insert64(table, SIZE, key, val);
            if (present[k] || count < SIZE) {
                TEST_ASSERT(r == 0, "insert should succeed");
                if (!present[k]) count++;
                present[k] = 1;
                ref[k] = val;
            } else {
                TEST_ASSERT(r == -1, "insert into a full table");
            }
        } else {
            TEST_ASSERT(remove64(table, SIZE, key) == present[k], "remove result");
            if (present[k]) count--;
            present[k] = 0;
        }
        for (int c = 0; c < KEYS; c++) {
            int found = 0;
            int64_t v = search64(table, SIZE, c * 7919 - 100, &found);
            TEST_ASSERT(found == present[c], "presence differs from reference");
            if (found) TEST_ASSERT(v == ref[c], "value differs from reference");
        }
    }
    TEST_PASS();
}

// ============================================================================
//  2. BUFFERED FLATMAP 64x64 (int64 key, int64 value)
// ============================================================================

void test_fm64_page_layout(void) {
    TEST_START("flatMap64: 512 entries of 16 bytes = one block");
    TEST_ASSERT(sizeof(struct Entry64x64) == 16, "entry size");
    TEST_ASSERT(ENTRIES_PER_PAGE64 == 512, "entries per page");
    TEST_PASS();
}

void test_fm64_init_invalid(void) {
    TEST_START("flatmap64_init rejects NULL / bad sizes");
    FlatMapBuffered64 fm;
    TEST_ASSERT(flatmap64_init(NULL, 2, 4, TEST_FILE_64) == -1, "NULL fm");
    TEST_ASSERT(flatmap64_init(&fm, 0, 4, TEST_FILE_64) == -1, "poolSize 0");
    TEST_ASSERT(flatmap64_init(&fm, 2, 0, TEST_FILE_64) == -1, "totalPages 0");
    TEST_ASSERT(flatmap64_init(&fm, 2, 4, "/nonexistent-dir/x.bin") == -1, "bad path");
    TEST_ASSERT(fm.buffors == NULL, "no buffers after a failed init");
    flatmap64_free(&fm); // must not crash
    TEST_PASS();
}

void test_fm64_init_creates_file(void) {
    TEST_START("flatmap64_init creates a file of totalPages blocks");
    remove(TEST_FILE_64);
    FlatMapBuffered64 fm;
    TEST_ASSERT(flatmap64_init(&fm, 2, 5, TEST_FILE_64) == 0, "init");
    TEST_ASSERT(file_size(TEST_FILE_64) == 5L * BLOCK_SIZE, "file size");
    flatmap64_free(&fm);
    remove(TEST_FILE_64);
    TEST_PASS();
}

void test_fm64_insert_search_update(void) {
    TEST_START("flatmap64 insert / search / update, int64 keys and values");
    remove(TEST_FILE_64);
    FlatMapBuffered64 fm;
    TEST_ASSERT(flatmap64_init(&fm, 2, 4, TEST_FILE_64) == 0, "init");
    int64_t keys[] = {0, 1, -2, INT64_MAX, INT64_MIN, (int64_t)1 << 40, -((int64_t)1 << 50),
                      create_key(3, 12345), create_key(1, 0)};
    int n = (int)(sizeof(keys) / sizeof(keys[0]));
    for (int i = 0; i < n; i++) {
        TEST_ASSERT(flatmap64_insert(&fm, keys[i], INT64_MAX - i) == 0, "insert");
    }
    int found = 0;
    for (int i = 0; i < n; i++) {
        TEST_ASSERT(flatmap64_search(&fm, keys[i], &found) == INT64_MAX - i && found, "search");
    }
    TEST_ASSERT(flatmap64_insert(&fm, (int64_t)1 << 40, INT64_MIN) == 0, "update");
    TEST_ASSERT(flatmap64_search(&fm, (int64_t)1 << 40, &found) == INT64_MIN && found, "updated value");
    flatmap64_search(&fm, 777, &found);
    TEST_ASSERT(!found, "missing key");
    TEST_ASSERT(flatmap64_insert(&fm, EMPTY_KEY, 1) == -1, "EMPTY_KEY rejected");
    flatmap64_search(&fm, EMPTY_KEY, &found);
    TEST_ASSERT(!found, "EMPTY_KEY never found");
    flatmap64_free(&fm);
    remove(TEST_FILE_64);
    TEST_PASS();
}

// sum of entryCount over all pages, -1 if an entryCount differs from the entries really in its page
static int32_t fm64_total_entries(FlatMapBuffered64 *fm) {
    int32_t total = 0;
    for (int32_t p = 0; p < fm->totalPages; p++) {
        FlatMapBuffor64 *page = flatmap64_get_page(fm, p);
        int8_t inSync = page->entryCount == flatmap64_count_entries(page->table);
        total += page->entryCount;
        page->pinCount = 0;
        if (!inSync) return -1;
    }
    return total;
}

void test_fm64_entry_count(void) {
    TEST_START("flatmap64 entryCount: update does not count, remove does");
    remove(TEST_FILE_64);
    FlatMapBuffered64 fm;
    TEST_ASSERT(flatmap64_init(&fm, 1, 3, TEST_FILE_64) == 0, "init");
    for (int64_t k = 0; k < 100; k++) flatmap64_insert(&fm, k, k);
    for (int64_t k = 0; k < 100; k++) flatmap64_insert(&fm, k, k + 1); // updates
    TEST_ASSERT(fm64_total_entries(&fm) == 100, "100 entries");
    for (int64_t k = 0; k < 40; k++) flatmap64_remove(&fm, k);
    flatmap64_remove(&fm, 5); // already gone
    TEST_ASSERT(fm64_total_entries(&fm) == 60, "60 entries after remove");
    flatmap64_free(&fm);
    remove(TEST_FILE_64);
    TEST_PASS();
}

void test_fm64_remove(void) {
    TEST_START("flatmap64_remove: removed key gone, others stay");
    remove(TEST_FILE_64);
    FlatMapBuffered64 fm;
    TEST_ASSERT(flatmap64_init(&fm, 2, 2, TEST_FILE_64) == 0, "init");
    for (int64_t k = 0; k < 800; k++) flatmap64_insert(&fm, k * 31, k);
    for (int64_t k = 0; k < 800; k += 2) {
        TEST_ASSERT(flatmap64_remove(&fm, k * 31) == 1, "remove existing");
    }
    TEST_ASSERT(flatmap64_remove(&fm, 0) == 0, "second remove");
    TEST_ASSERT(flatmap64_remove(&fm, 123456789) == 0, "remove missing");
    TEST_ASSERT(flatmap64_remove(&fm, EMPTY_KEY) == 0, "remove EMPTY_KEY");
    TEST_ASSERT(flatmap64_remove(NULL, 1) == 0, "remove on NULL");
    int found = 0;
    for (int64_t k = 0; k < 800; k++) {
        int64_t v = flatmap64_search(&fm, k * 31, &found);
        TEST_ASSERT(found == (k % 2 == 1), "presence after remove");
        if (found) TEST_ASSERT(v == k, "value after remove");
    }
    flatmap64_free(&fm);
    remove(TEST_FILE_64);
    TEST_PASS();
}

void test_fm64_eviction(void) {
    TEST_START("flatmap64 pool of 1 page, 16 pages: every key survives eviction");
    remove(TEST_FILE_64);
    FlatMapBuffered64 fm;
    TEST_ASSERT(flatmap64_init(&fm, 1, 16, TEST_FILE_64) == 0, "init");
    for (int64_t k = 0; k < 4000; k++) {
        TEST_ASSERT(flatmap64_insert(&fm, k * 1000000007LL, -k) == 0, "insert");
    }
    int found = 0;
    for (int64_t k = 0; k < 4000; k++) {
        TEST_ASSERT(flatmap64_search(&fm, k * 1000000007LL, &found) == -k && found, "search");
    }
    flatmap64_free(&fm);
    remove(TEST_FILE_64);
    TEST_PASS();
}

void test_fm64_persistence(void) {
    TEST_START("flatmap64 free + init on the same file keeps entries");
    remove(TEST_FILE_64);
    FlatMapBuffered64 fm;
    TEST_ASSERT(flatmap64_init(&fm, 2, 8, TEST_FILE_64) == 0, "init");
    for (int64_t k = 1; k <= 2000; k++) flatmap64_insert(&fm, k << 33, k * k);
    for (int64_t k = 1; k <= 2000; k += 3) flatmap64_remove(&fm, k << 33);
    flatmap64_free(&fm);

    TEST_ASSERT(flatmap64_init(&fm, 3, 8, TEST_FILE_64) == 0, "reopen");
    TEST_ASSERT(file_size(TEST_FILE_64) == 8L * BLOCK_SIZE, "file not grown");
    int found = 0;
    for (int64_t k = 1; k <= 2000; k++) {
        int64_t v = flatmap64_search(&fm, k << 33, &found);
        TEST_ASSERT(found == ((k - 1) % 3 != 0), "presence after reopen");
        if (found) TEST_ASSERT(v == k * k, "value after reopen");
    }
    flatmap64_free(&fm);
    remove(TEST_FILE_64);
    TEST_PASS();
}

void test_fm64_flush(void) {
    TEST_START("flatmap64_flush writes dirty pages, a second map sees them");
    remove(TEST_FILE_64);
    FlatMapBuffered64 fm, other;
    TEST_ASSERT(flatmap64_init(&fm, 4, 4, TEST_FILE_64) == 0, "init");
    for (int64_t k = 0; k < 300; k++) flatmap64_insert(&fm, k, k + 7);
    flatmap64_flush(&fm);
    TEST_ASSERT(flatmap64_init(&other, 1, 4, TEST_FILE_64) == 0, "second map");
    int found = 0;
    for (int64_t k = 0; k < 300; k++) {
        TEST_ASSERT(flatmap64_search(&other, k, &found) == k + 7 && found, "flushed value");
    }
    free(other.buffors);
    flatmap64_free(&fm);
    remove(TEST_FILE_64);
    TEST_PASS();
}

void test_fm64_extend_file_no_ghost_keys(void) {
    TEST_START("flatmap64 growing totalPages: new pages are empty, no key 0");
    remove(TEST_FILE_64);
    FlatMapBuffered64 fm;
    TEST_ASSERT(flatmap64_init(&fm, 1, 1, TEST_FILE_64) == 0, "init 1 page");
    flatmap64_free(&fm);
    TEST_ASSERT(flatmap64_init(&fm, 1, 6, TEST_FILE_64) == 0, "init 6 pages");
    TEST_ASSERT(file_size(TEST_FILE_64) == 6L * BLOCK_SIZE, "file grown");
    int found = 1;
    flatmap64_search(&fm, 0, &found);
    TEST_ASSERT(!found, "key 0 must not appear in a new page");
    TEST_ASSERT(fm64_total_entries(&fm) == 0, "all pages empty");
    flatmap64_free(&fm);
    remove(TEST_FILE_64);
    TEST_PASS();
}

void test_fm64_page_full(void) {
    TEST_START("flatmap64 one page: 512 keys fit, the 513th fails");
    remove(TEST_FILE_64);
    FlatMapBuffered64 fm;
    TEST_ASSERT(flatmap64_init(&fm, 1, 1, TEST_FILE_64) == 0, "init");
    for (int64_t k = 0; k < ENTRIES_PER_PAGE64; k++) {
        TEST_ASSERT(flatmap64_insert(&fm, k, k) == 0, "fill");
    }
    TEST_ASSERT(flatmap64_insert(&fm, 100000, 1) == -1, "page full");
    TEST_ASSERT(flatmap64_remove(&fm, 17) == 1, "remove one");
    TEST_ASSERT(flatmap64_insert(&fm, 100000, 1) == 0, "fits after remove");
    flatmap64_free(&fm);
    remove(TEST_FILE_64);
    TEST_PASS();
}

void test_fm64_stress(void) {
    TEST_START("flatmap64 stress: 50000 random ops, pool 2 / 16 pages, reopen");
    enum { KEYS = 3000 };
    static int64_t ref[KEYS];
    static int present[KEYS];
    memset(present, 0, sizeof(present));
    remove(TEST_FILE_64);
    FlatMapBuffered64 fm;
    TEST_ASSERT(flatmap64_init(&fm, 2, 16, TEST_FILE_64) == 0, "init");
    for (int op = 0; op < 50000; op++) {
        int k = (int)(rng_next() % KEYS);
        int64_t key = (int64_t)k * 0x100000001LL;
        uint64_t what = rng_next() % 10;
        int found = 0;
        if (what < 5) {
            int64_t val = (int64_t)rng_next();
            TEST_ASSERT(flatmap64_insert(&fm, key, val) == 0, "insert");
            ref[k] = val;
            present[k] = 1;
        } else if (what < 8) {
            TEST_ASSERT(flatmap64_remove(&fm, key) == present[k], "remove result");
            present[k] = 0;
        } else {
            int64_t v = flatmap64_search(&fm, key, &found);
            TEST_ASSERT(found == present[k], "search presence");
            if (found) TEST_ASSERT(v == ref[k], "search value");
        }
    }
    flatmap64_free(&fm);
    TEST_ASSERT(flatmap64_init(&fm, 2, 16, TEST_FILE_64) == 0, "reopen");
    for (int k = 0; k < KEYS; k++) {
        int found = 0;
        int64_t v = flatmap64_search(&fm, (int64_t)k * 0x100000001LL, &found);
        TEST_ASSERT(found == present[k], "presence after reopen");
        if (found) TEST_ASSERT(v == ref[k], "value after reopen");
    }
    flatmap64_free(&fm);
    remove(TEST_FILE_64);
    TEST_PASS();
}

// ============================================================================
//  3. BUFFERED FLATMAP (int32 key, int64 value)
// ============================================================================

void test_fm32_page_layout(void) {
    TEST_START("flatMap: 682 entries of 12 bytes in one block");
    TEST_ASSERT(sizeof(struct Entry64) == 12, "entry size");
    TEST_ASSERT(ENTRIES_PER_PAGE == 682, "entries per page");
    TEST_PASS();
}

void test_fm32_insert_search_remove(void) {
    TEST_START("flatmap insert / search / update / remove, int32 keys");
    remove(TEST_FILE_32);
    FlatMapBuffered fm;
    TEST_ASSERT(flatmap_init(&fm, 2, 4, TEST_FILE_32) == 0, "init");
    TEST_ASSERT(file_size(TEST_FILE_32) == 4L * BLOCK_SIZE, "file size");
    int32_t keys[] = {0, 1, -2, INT32_MAX, INT32_MIN, 123456};
    int n = (int)(sizeof(keys) / sizeof(keys[0]));
    for (int i = 0; i < n; i++) {
        TEST_ASSERT(flatmap_insert(&fm, keys[i], INT64_MIN + i) == 0, "insert");
    }
    int found = 0;
    for (int i = 0; i < n; i++) {
        TEST_ASSERT(flatmap_search(&fm, keys[i], &found) == INT64_MIN + i && found, "search");
    }
    TEST_ASSERT(flatmap_insert(&fm, 0, 42) == 0, "update");
    TEST_ASSERT(flatmap_search(&fm, 0, &found) == 42 && found, "updated value");
    TEST_ASSERT(flatmap_insert(&fm, EMPTY_KEY, 1) == -1, "EMPTY_KEY rejected");
    TEST_ASSERT(flatmap_remove(&fm, INT32_MAX) == 1, "remove");
    TEST_ASSERT(flatmap_remove(&fm, INT32_MAX) == 0, "second remove");
    flatmap_search(&fm, INT32_MAX, &found);
    TEST_ASSERT(!found, "removed key gone");
    TEST_ASSERT(flatmap_search(&fm, INT32_MIN, &found) == INT64_MIN + 4 && found, "other key stays");
    flatmap_free(&fm);
    remove(TEST_FILE_32);
    TEST_PASS();
}

void test_fm32_page_full(void) {
    TEST_START("flatmap one page: 682 keys fit, the 683rd fails");
    remove(TEST_FILE_32);
    FlatMapBuffered fm;
    TEST_ASSERT(flatmap_init(&fm, 1, 1, TEST_FILE_32) == 0, "init");
    for (int32_t k = 0; k < ENTRIES_PER_PAGE; k++) {
        TEST_ASSERT(flatmap_insert(&fm, k, k) == 0, "fill");
    }
    TEST_ASSERT(flatmap_insert(&fm, 100000, 1) == -1, "page full");
    flatmap_free(&fm);
    // a full page read back from disk
    TEST_ASSERT(flatmap_init(&fm, 1, 1, TEST_FILE_32) == 0, "reopen");
    int found = 0;
    for (int32_t k = 0; k < ENTRIES_PER_PAGE; k++) {
        TEST_ASSERT(flatmap_search(&fm, k, &found) == k && found, "value after reopen");
    }
    flatmap_free(&fm);
    remove(TEST_FILE_32);
    TEST_PASS();
}

void test_fm32_stress(void) {
    TEST_START("flatmap stress: 50000 random ops, pool 1 / 12 pages, reopen");
    enum { KEYS = 3000 };
    static int64_t ref[KEYS];
    static int present[KEYS];
    memset(present, 0, sizeof(present));
    remove(TEST_FILE_32);
    FlatMapBuffered fm;
    TEST_ASSERT(flatmap_init(&fm, 1, 12, TEST_FILE_32) == 0, "init");
    for (int op = 0; op < 50000; op++) {
        int k = (int)(rng_next() % KEYS);
        int32_t key = k * 65537;
        uint64_t what = rng_next() % 10;
        int found = 0;
        if (what < 5) {
            int64_t val = (int64_t)rng_next();
            TEST_ASSERT(flatmap_insert(&fm, key, val) == 0, "insert");
            ref[k] = val;
            present[k] = 1;
        } else if (what < 8) {
            TEST_ASSERT(flatmap_remove(&fm, key) == present[k], "remove result");
            present[k] = 0;
        } else {
            int64_t v = flatmap_search(&fm, key, &found);
            TEST_ASSERT(found == present[k], "search presence");
            if (found) TEST_ASSERT(v == ref[k], "search value");
        }
    }
    flatmap_free(&fm);
    TEST_ASSERT(flatmap_init(&fm, 3, 12, TEST_FILE_32) == 0, "reopen");
    for (int k = 0; k < KEYS; k++) {
        int found = 0;
        int64_t v = flatmap_search(&fm, k * 65537, &found);
        TEST_ASSERT(found == present[k], "presence after reopen");
        if (found) TEST_ASSERT(v == ref[k], "value after reopen");
    }
    flatmap_free(&fm);
    remove(TEST_FILE_32);
    TEST_PASS();
}

int main(void) {
    printf("\n\033[1;33m╔══════════════════════════════════════════════════════════════╗\033[0m\n");
    printf("\033[1;33m║       BUFFERED FLATMAP — TEST SUITE                          ║\033[0m\n");
    printf("\033[1;33m╚══════════════════════════════════════════════════════════════╝\033[0m\n");

    SECTION("1. PROBING TABLE");
    test_table_insert_search();
    test_table_empty_key_rejected();
    test_table_full();
    test_table_remove_keeps_chain();
    test_table_remove_full_table();
    test_table_remove_random_64();
    test_table_remove_random_32();

    SECTION("2. BUFFERED FLATMAP 64x64");
    test_fm64_page_layout();
    test_fm64_init_invalid();
    test_fm64_init_creates_file();
    test_fm64_insert_search_update();
    test_fm64_entry_count();
    test_fm64_remove();
    test_fm64_eviction();
    test_fm64_persistence();
    test_fm64_flush();
    test_fm64_extend_file_no_ghost_keys();
    test_fm64_page_full();
    test_fm64_stress();

    SECTION("3. BUFFERED FLATMAP (int32 key)");
    test_fm32_page_layout();
    test_fm32_insert_search_remove();
    test_fm32_page_full();
    test_fm32_stress();

    printf("\n\033[1;33m══════════════════════════════════════════════════════════════\033[0m\n");
    printf("  Total: %d  |  \033[32mPassed: %d\033[0m  |  \033[31mFailed: %d\033[0m\n",
           tests_passed + tests_failed, tests_passed, tests_failed);
    printf("\033[1;33m══════════════════════════════════════════════════════════════\033[0m\n\n");

    return tests_failed > 0 ? 1 : 0;
}
