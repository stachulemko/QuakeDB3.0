#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdlib.h>
#include "../bufforing-stm/dataBuffor.h"

/* ---- helpers ---- */

static void make_buffors(Buffors *b, int32_t n) {
    initializeBuffors(b, n);
}

static void free_buffors(Buffors *b) {
    for (int i = 0; i < b->count; i++) {
        if (b->buffors[i].isUsed && b->buffors[i].universalBlock) {
            if (b->buffors[i].universalBlock->block)
                free(b->buffors[i].universalBlock->block);
            if (b->buffors[i].universalBlock->header)
                free(b->buffors[i].universalBlock->header);
            free(b->buffors[i].universalBlock);
        }
    }
    free(b->buffors);
}

/* ---- initializeBuffors ---- */

static void test_initializeBuffors_sets_count(void **state) {
    (void)state;
    Buffors b;
    make_buffors(&b, 4);
    assert_int_equal(4, b.count);
    assert_non_null(b.buffors);
    for (int i = 0; i < 4; i++) {
        assert_int_equal(0, b.buffors[i].isUsed);
    }
    free(b.buffors);
}

static void test_initializeBuffors_zero(void **state) {
    (void)state;
    Buffors b;
    make_buffors(&b, 0);
    assert_int_equal(0, b.count);
}

/* ---- loadIfSpaceAny ---- */

static void test_loadIfSpaceAny_returns_free_slot(void **state) {
    (void)state;
    Buffors b;
    make_buffors(&b, 3);

    DataBuffor *got = loadIfSpaceAny(&b);
    assert_non_null(got);
    assert_int_equal(1, got->pinCount);
    assert_int_equal(1, got->isUsed);
    assert_int_equal(0, got->isDirty);

    free(b.buffors);
}

static void test_loadIfSpaceAny_returns_null_when_full(void **state) {
    (void)state;
    Buffors b;
    make_buffors(&b, 2);
    b.buffors[0].isUsed = 1;
    b.buffors[1].isUsed = 1;

    DataBuffor *got = loadIfSpaceAny(&b);
    assert_null(got);

    free(b.buffors);
}

static void test_loadIfSpaceAny_fills_first_free(void **state) {
    (void)state;
    Buffors b;
    make_buffors(&b, 3);
    b.buffors[0].isUsed = 1;

    DataBuffor *got = loadIfSpaceAny(&b);
    assert_non_null(got);
    assert_ptr_equal(&b.buffors[1], got);

    free(b.buffors);
}

/* ---- evictAny ---- */

static void test_evictAny_evicts_unpinned(void **state) {
    (void)state;
    Buffors b;
    make_buffors(&b, 2);

    /* slot 0: used, pinned */
    b.buffors[0].isUsed = 1;
    b.buffors[0].pinCount = 1;
    b.buffors[0].tableId = 1;
    b.buffors[0].universalBlock = NULL;

    /* slot 1: used, unpinned, not dirty */
    b.buffors[1].isUsed = 1;
    b.buffors[1].pinCount = 0;
    b.buffors[1].isDirty = 0;
    b.buffors[1].tableId = 2;
    b.buffors[1].universalBlock = NULL;

    DataBuffor *got = evictAny(&b);
    assert_non_null(got);
    assert_ptr_equal(&b.buffors[1], got);
    assert_int_equal(1, got->pinCount);
    assert_int_equal(0, got->isDirty);
    assert_null(got->universalBlock);

    free(b.buffors);
}

/* ---- getBufforAny ---- */

static void test_getBufforAny_prefers_free_slot(void **state) {
    (void)state;
    Buffors b;
    make_buffors(&b, 3);

    DataBuffor *got = getBufforAny(&b);
    assert_non_null(got);
    assert_ptr_equal(&b.buffors[0], got);
    assert_int_equal(1, got->isUsed);

    free(b.buffors);
}

static void test_getBufforAny_evicts_when_full(void **state) {
    (void)state;
    Buffors b;
    make_buffors(&b, 1);
    b.buffors[0].isUsed = 1;
    b.buffors[0].pinCount = 0;
    b.buffors[0].isDirty = 0;
    b.buffors[0].tableId = 1;
    b.buffors[0].universalBlock = NULL;

    DataBuffor *got = getBufforAny(&b);
    assert_non_null(got);

    free(b.buffors);
}

/* ---- addNewBlock ---- */

static void test_addNewBlock_returns_0_when_no_buffors(void **state) {
    (void)state;
    Buffors b;
    make_buffors(&b, 0);

    DataBuffor newB = {.tableId = 1};
    assert_int_equal(0, addNewBlock(&b, &newB));
}

static void test_addNewBlock_returns_1_on_success(void **state) {
    (void)state;
    Buffors b;
    make_buffors(&b, 2);

    DataBuffor newB = {.tableId = 5, .pinCount = 1, .isUsed = 1, .isDirty = 1};
    int8_t rc = addNewBlock(&b, &newB);
    assert_int_equal(1, rc);

    free(b.buffors);
}

/* ---- getIfExisting ---- */

static void test_getIfExisting_returns_null_when_not_found(void **state) {
    (void)state;
    Buffors b;
    make_buffors(&b, 2);

    DataBuffor *got = getIfExisting(1, 0, &b);
    assert_null(got);

    free(b.buffors);
}

static void test_getIfExisting_finds_matching_block(void **state) {
    (void)state;
    Buffors b;
    make_buffors(&b, 2);

    /* set up slot 0 with a real block */
    Block8kb *blk = (Block8kb *)calloc(1, sizeof(Block8kb));
    block8kb_init(blk, 0, -1, 42, 0, 0, 0, 0);
    UniversalBlock *ub = (UniversalBlock *)calloc(1, sizeof(UniversalBlock));
    ub->block = blk;
    ub->header = NULL;

    b.buffors[0].isUsed = 1;
    b.buffors[0].tableId = 10;
    b.buffors[0].pinCount = 0;
    b.buffors[0].universalBlock = ub;

    DataBuffor *got = getIfExisting(10, 42, &b);
    assert_non_null(got);
    assert_ptr_equal(&b.buffors[0], got);
    assert_int_equal(1, got->pinCount);

    free(blk);
    free(ub);
    free(b.buffors);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_initializeBuffors_sets_count),
        cmocka_unit_test(test_initializeBuffors_zero),
        cmocka_unit_test(test_loadIfSpaceAny_returns_free_slot),
        cmocka_unit_test(test_loadIfSpaceAny_returns_null_when_full),
        cmocka_unit_test(test_loadIfSpaceAny_fills_first_free),
        cmocka_unit_test(test_evictAny_evicts_unpinned),
        cmocka_unit_test(test_getBufforAny_prefers_free_slot),
        cmocka_unit_test(test_getBufforAny_evicts_when_full),
        cmocka_unit_test(test_addNewBlock_returns_0_when_no_buffors),
        cmocka_unit_test(test_addNewBlock_returns_1_on_success),
        cmocka_unit_test(test_getIfExisting_returns_null_when_not_found),
        cmocka_unit_test(test_getIfExisting_finds_matching_block),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
