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
    assert_int_equal(1, e->maxBlock);

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
    assert_int_equal(3, e->maxBlock);

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
    assert_int_equal(2, e1->maxBlock);
    assert_int_equal(1, e2->maxBlock);

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

    FSMMapAll all = {.maps = &map, .count = 1};

    addToFSMMapAll(&all, 1, 77);
    assert_int_equal(1, map.entries[MAX_FSM].count);
    assert_int_equal(77, map.entries[MAX_FSM].block_ids[0]);

    free(map.entries[MAX_FSM].block_ids);
}

static void test_addToFSMMapAll_ignores_wrong_table(void **state) {
    (void)state;
    FSMMap map;
    memset(&map, 0, sizeof(map));
    map.tableId = 1;

    FSMMapAll all = {.maps = &map, .count = 1};

    addToFSMMapAll(&all, 999, 77);
    assert_int_equal(0, map.entries[MAX_FSM].count);
}

static void test_addToFSMMapAll_multiple_blocks(void **state) {
    (void)state;
    FSMMap map;
    memset(&map, 0, sizeof(map));
    map.tableId = 3;

    FSMMapAll all = {.maps = &map, .count = 1};

    addToFSMMapAll(&all, 3, 10);
    addToFSMMapAll(&all, 3, 20);
    addToFSMMapAll(&all, 3, 30);
    assert_int_equal(3, map.entries[MAX_FSM].count);
    assert_int_equal(10, map.entries[MAX_FSM].block_ids[0]);
    assert_int_equal(20, map.entries[MAX_FSM].block_ids[1]);
    assert_int_equal(30, map.entries[MAX_FSM].block_ids[2]);

    free(map.entries[MAX_FSM].block_ids);
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
        cmocka_unit_test(test_addToFSMMapAll_multiple_blocks),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
