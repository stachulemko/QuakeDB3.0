#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdlib.h>
#include "../bufforing-stm/dataBuffor.h"

/* ---- FSMCache tests ---- */

static void test_fsm_cache_init(void **state) {
    (void)state;
    FSMCache c;
    fsm_cache_init(&c);
    assert_null(c.blockCounter);
}

static void test_fsm_cache_set_new_entry(void **state) {
    (void)state;
    FSMCache c;
    fsm_cache_init(&c);

    fsm_cache_set(&c, 10);
    BlockCounterEntry *e = fsm_cache_get(&c, 10);
    assert_non_null(e);
    assert_int_equal(10, e->tableId);
    assert_int_equal(0, e->maxBlock);  /* starts at 0 */

    fsm_cache_free(&c);
}

static void test_fsm_cache_set_increments(void **state) {
    (void)state;
    FSMCache c;
    fsm_cache_init(&c);

    fsm_cache_set(&c, 5);
    fsm_cache_set(&c, 5);
    fsm_cache_set(&c, 5);

    BlockCounterEntry *e = fsm_cache_get(&c, 5);
    assert_non_null(e);
    assert_int_equal(2, e->maxBlock);  /* 0 → 1 → 2 */

    fsm_cache_free(&c);
}

static void test_fsm_cache_get_missing(void **state) {
    (void)state;
    FSMCache c;
    fsm_cache_init(&c);

    assert_null(fsm_cache_get(&c, 999));

    fsm_cache_free(&c);
}

static void test_fsm_cache_multiple_tables(void **state) {
    (void)state;
    FSMCache c;
    fsm_cache_init(&c);

    fsm_cache_set(&c, 1);
    fsm_cache_set(&c, 2);
    fsm_cache_set(&c, 1);

    BlockCounterEntry *e1 = fsm_cache_get(&c, 1);
    BlockCounterEntry *e2 = fsm_cache_get(&c, 2);
    assert_int_equal(1, e1->maxBlock);  /* 0 → 1 (two set calls) */
    assert_int_equal(0, e2->maxBlock);  /* 0 (one set call) */

    fsm_cache_free(&c);
}

/* ---- FSMSpaceEntry tests ---- */

static void test_fsm_space_entry_add_single(void **state) {
    (void)state;
    FSMSpaceEntry e = {0};

    fsm_space_entry_add(&e, 42);
    assert_int_equal(1, e.count);
    assert_int_equal(42, e.block_ids[0]);

    free(e.block_ids);
}

static void test_fsm_space_entry_add_multiple(void **state) {
    (void)state;
    FSMSpaceEntry e = {0};

    for (int i = 0; i < 10; i++) {
        fsm_space_entry_add(&e, i * 100);
    }
    assert_int_equal(10, e.count);
    assert_int_equal(0, e.block_ids[0]);
    assert_int_equal(900, e.block_ids[9]);

    free(e.block_ids);
}

static void test_fsm_space_entry_remove_last(void **state) {
    (void)state;
    FSMSpaceEntry e = {0};
    fsm_space_entry_add(&e, 10);
    fsm_space_entry_add(&e, 20);
    fsm_space_entry_add(&e, 30);

    fsm_space_entry_remove(&e, 2);
    assert_int_equal(2, e.count);
    assert_int_equal(10, e.block_ids[0]);
    assert_int_equal(20, e.block_ids[1]);

    free(e.block_ids);
}

static void test_fsm_space_entry_remove_middle(void **state) {
    (void)state;
    FSMSpaceEntry e = {0};
    fsm_space_entry_add(&e, 10);
    fsm_space_entry_add(&e, 20);
    fsm_space_entry_add(&e, 30);

    fsm_space_entry_remove(&e, 1);
    assert_int_equal(2, e.count);
    assert_int_equal(10, e.block_ids[0]);
    assert_int_equal(30, e.block_ids[1]);

    free(e.block_ids);
}

static void test_fsm_space_entry_grow_capacity(void **state) {
    (void)state;
    FSMSpaceEntry e = {0};

    for (int i = 0; i < FSM_MAX_BLOCKS_PER_SPACE + 5; i++) {
        fsm_space_entry_add(&e, i);
    }
    assert_int_equal(FSM_MAX_BLOCKS_PER_SPACE + 5, e.count);
    assert_true(e.capacity > FSM_MAX_BLOCKS_PER_SPACE);
    assert_int_equal(FSM_MAX_BLOCKS_PER_SPACE + 4, e.block_ids[FSM_MAX_BLOCKS_PER_SPACE + 4]);

    free(e.block_ids);
}

/* ---- addToFSMMapAll ---- */

static void test_addToFSMMapAll_adds_block(void **state) {
    (void)state;
    FSMMap map;
    memset(&map, 0, sizeof(map));
    map.tableId = 1;
    map.isUsed  = 1;

    FSMMapAll all = {.maps = &map, .count = 1};

    addToFSMMapAll(&all, 1, 77);
    assert_int_equal(1, map.entries[FSM_EMPTY_BLOCK_ENTRY].count);
    assert_int_equal(77, map.entries[FSM_EMPTY_BLOCK_ENTRY].block_ids[0]);

    free(map.entries[FSM_EMPTY_BLOCK_ENTRY].block_ids);
}

static void test_addToFSMMapAll_ignores_wrong_table(void **state) {
    (void)state;
    FSMMap map;
    memset(&map, 0, sizeof(map));
    map.tableId = 1;
    map.isUsed  = 1;

    FSMMapAll all = {.maps = &map, .count = 1};

    addToFSMMapAll(&all, 999, 77);
    assert_int_equal(0, map.entries[FSM_EMPTY_BLOCK_ENTRY].count);
}

static void test_addToFSMMapAll_ignores_unused_map(void **state) {
    (void)state;
    FSMMap map;
    memset(&map, 0, sizeof(map));
    map.tableId = 1;
    map.isUsed  = 0;  /* not active */

    FSMMapAll all = {.maps = &map, .count = 1};

    addToFSMMapAll(&all, 1, 99);
    assert_int_equal(0, map.entries[FSM_EMPTY_BLOCK_ENTRY].count);
}

/* ---- addTableToFSMMapAll ---- */

static void test_addTableToFSMMapAll_allocates_maps(void **state) {
    (void)state;
    FSMMapAll all;
    init_FSMMapAll(&all);

    addTableToFSMMapAll(&all, 42);

    assert_non_null(all.maps);
    assert_int_equal(1, all.count);
    assert_int_equal(42, all.maps[0].tableId);
    assert_int_equal(1, all.maps[0].isUsed);

    free_FSMMapAll(&all);
}

static void test_addTableToFSMMapAll_second_table(void **state) {
    (void)state;
    FSMMapAll all;
    init_FSMMapAll(&all);

    addTableToFSMMapAll(&all, 1);
    addTableToFSMMapAll(&all, 2);

    assert_int_equal(2, all.count);
    assert_int_equal(1, all.maps[0].tableId);
    assert_int_equal(2, all.maps[1].tableId);

    free_FSMMapAll(&all);
}

/* ---- create_FSMMapC / createDataBufforM ---- */

static void test_create_FSMMapC_allocates(void **state) {
    (void)state;
    FSMMap *maps = NULL;
    create_FSMMapC(&maps, 4);
    assert_non_null(maps);
    free(maps);
}

static void test_createDataBufforM_allocates(void **state) {
    (void)state;
    DataBuffor *b = NULL;
    createDataBufforM(&b);
    assert_non_null(b);
    free(b);
}

static void test_addToFSMMapAll_multiple_blocks(void **state) {
    (void)state;
    FSMMap map;
    memset(&map, 0, sizeof(map));
    map.tableId = 3;
    map.isUsed  = 1;

    FSMMapAll all = {.maps = &map, .count = 1};

    addToFSMMapAll(&all, 3, 10);
    addToFSMMapAll(&all, 3, 20);
    addToFSMMapAll(&all, 3, 30);
    assert_int_equal(3, map.entries[FSM_EMPTY_BLOCK_ENTRY].count);
    assert_int_equal(10, map.entries[FSM_EMPTY_BLOCK_ENTRY].block_ids[0]);
    assert_int_equal(20, map.entries[FSM_EMPTY_BLOCK_ENTRY].block_ids[1]);
    assert_int_equal(30, map.entries[FSM_EMPTY_BLOCK_ENTRY].block_ids[2]);

    free(map.entries[FSM_EMPTY_BLOCK_ENTRY].block_ids);
}

/* ---- tuples larger than one FSM step still reuse the existing block ---- */
static void test_fsm_large_tuples_share_one_block(void **state) {
    (void)state;
    Buffors   b;
    FSMCache *c = NULL;
    FSMMapAll all;
    initializeBuffors(&b, 4);
    FSMCacheCreateC(&c);
    fsm_cache_set(c, 5);
    init_FSMMapAll(&all);
    addTableToFSMMapAll(&all, 5);

    char big[121];
    memset(big, 'x', 120);
    big[120] = '\0';
    for (int i = 0; i < 5; i++) {
        AllVar vals[2] = {all_var_from_int32(i), all_var_from_string(big)};
        int8_t bm[2]   = {0, 0};
        DataBuffor *d = addTupleToOtherFunction(&b, c, &all, 5, vals, 2, bm, 2,
                                                1, 0, 0, 0, 0, 0, -1, NULL, NULL, NULL);
        assert_non_null(d);
        d->pinCount = 0;
    }

    assert_int_equal(1, fsm_cache_get(c, 5)->maxBlock);
    DataBuffor *d = getBuffor(5, 1, &b);
    assert_int_equal(5, d->universalBlock->block->tuple_count);
    d->pinCount = 0;

    for (int i = 0; i < b.count; i++) {
        if (b.buffors[i].isUsed && b.buffors[i].universalBlock) {
            free(b.buffors[i].universalBlock->block);
            free(b.buffors[i].universalBlock->header);
            free(b.buffors[i].universalBlock);
        }
    }
    free(b.buffors);
    free_FSMMapAll(&all);
    fsm_cache_free(c);
    free(c);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        /* FSMCache */
        cmocka_unit_test(test_fsm_cache_init),
        cmocka_unit_test(test_fsm_cache_set_new_entry),
        cmocka_unit_test(test_fsm_cache_set_increments),
        cmocka_unit_test(test_fsm_cache_get_missing),
        cmocka_unit_test(test_fsm_cache_multiple_tables),
        /* FSMSpaceEntry */
        cmocka_unit_test(test_fsm_space_entry_add_single),
        cmocka_unit_test(test_fsm_space_entry_add_multiple),
        cmocka_unit_test(test_fsm_space_entry_remove_last),
        cmocka_unit_test(test_fsm_space_entry_remove_middle),
        cmocka_unit_test(test_fsm_space_entry_grow_capacity),
        /* addToFSMMapAll */
        cmocka_unit_test(test_addToFSMMapAll_adds_block),
        cmocka_unit_test(test_addToFSMMapAll_ignores_wrong_table),
        cmocka_unit_test(test_addToFSMMapAll_ignores_unused_map),
        cmocka_unit_test(test_addToFSMMapAll_multiple_blocks),
        /* addTableToFSMMapAll */
        cmocka_unit_test(test_addTableToFSMMapAll_allocates_maps),
        cmocka_unit_test(test_addTableToFSMMapAll_second_table),
        /* create helpers */
        cmocka_unit_test(test_create_FSMMapC_allocates),
        cmocka_unit_test(test_createDataBufforM_allocates),
        cmocka_unit_test(test_fsm_large_tuples_share_one_block),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
