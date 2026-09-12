//
// Created by stas on 14.04.2026.
//

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "../bufforing-stm/dataBuffor.h"

/* ── helpers ──────────────────────────────────────────────────────────── */

/* Prepare a fresh env with one table already added */
static void setup_table(Buffors *buffors, FSMCache *c, FSMMapAll *fsmMapAll, MVCC *mvcc,
                        int32_t tableId, int32_t buffor_count,
                        int8_t *types, int8_t *nulls, int col_count) {
    initializeBuffors(buffors, buffor_count);
    fsm_cache_init(c);
    init_FSMMapAll(fsmMapAll);
    mvcc_init(mvcc);

    char col_names[MAX_COLUMNS][MAX_COL_NAME_LEN];
    for (int i = 0; i < col_count; i++) {
        snprintf(col_names[i], MAX_COL_NAME_LEN, "col%d", i);
    }
    addTable(fsmMapAll, buffors, c, mvcc, tableId, types, nulls,
             (char (*)[MAX_COL_NAME_LEN])col_names);
}

/* Return the first data-block buffor for a given tableId (NULL if not found) */
static DataBuffor *find_data_buffor(Buffors *buffors, int32_t tableId) {
    for (int i = 0; i < buffors->count; i++) {
        DataBuffor *b = &buffors->buffors[i];
        if (b->isUsed && b->tableId == tableId &&
            b->universalBlock != NULL && b->universalBlock->block != NULL) {
            return b;
        }
    }
    return NULL;
}

/* ── tests ────────────────────────────────────────────────────────────── */

/* 1. After one addTuple the block has exactly one tuple */
void test_addTuple_tuple_count_is_one(void **state) {
    (void)state;
    Buffors b;  FSMCache c;  FSMMapAll f;  MVCC mvcc;
    int8_t types[] = {ID_INT32, ID_STRING};
    int8_t nulls[] = {0, 0};
    setup_table(&b, &c, &f, &mvcc, 20, 3, types, nulls, 2);

    addTuple(&b, &c, &f, &mvcc, 20,
             (AllVar[]){all_var_from_int32(1), all_var_from_string("hello")},
             2, (int8_t[]){0, 0}, 2, NULL, NULL);

    DataBuffor *db = find_data_buffor(&b, 20);
    assert_non_null(db);
    assert_int_equal(1, db->universalBlock->block->tuple_count);
}

/* 2. int32 value is stored correctly */
void test_addTuple_int32_value_correct(void **state) {
    (void)state;
    Buffors b;  FSMCache c;  FSMMapAll f;  MVCC mvcc;
    int8_t types[] = {ID_INT32};
    int8_t nulls[] = {0};
    setup_table(&b, &c, &f, &mvcc, 21, 3, types, nulls, 1);

    addTuple(&b, &c, &f, &mvcc, 21,
             (AllVar[]){all_var_from_int32(42)},
             1, (int8_t[]){0}, 1, NULL, NULL);

    DataBuffor *db = find_data_buffor(&b, 21);
    assert_non_null(db);
    assert_int_equal(42, db->universalBlock->block->tuples[0].dnb.data[0].val.i32);
}

/* 3. string value is stored correctly */
void test_addTuple_string_value_correct(void **state) {
    (void)state;
    Buffors b;  FSMCache c;  FSMMapAll f;  MVCC mvcc;
    int8_t types[] = {ID_INT32, ID_STRING};
    int8_t nulls[] = {0, 0};
    setup_table(&b, &c, &f, &mvcc, 22, 3, types, nulls, 2);

    addTuple(&b, &c, &f, &mvcc, 22,
             (AllVar[]){all_var_from_int32(7), all_var_from_string("World")},
             2, (int8_t[]){0, 0}, 2, NULL, NULL);

    DataBuffor *db = find_data_buffor(&b, 22);
    assert_non_null(db);
    assert_string_equal("World",
        db->universalBlock->block->tuples[0].dnb.data[1].val.str);
}

/* 4. int64 value is stored correctly */
void test_addTuple_int64_value_correct(void **state) {
    (void)state;
    Buffors b;  FSMCache c;  FSMMapAll f;  MVCC mvcc;
    int8_t types[] = {ID_INT64};
    int8_t nulls[] = {0};
    setup_table(&b, &c, &f, &mvcc, 23, 3, types, nulls, 1);

    addTuple(&b, &c, &f, &mvcc, 23,
             (AllVar[]){all_var_from_int64(1234567890123LL)},
             1, (int8_t[]){0}, 1, NULL, NULL);

    DataBuffor *db = find_data_buffor(&b, 23);
    assert_non_null(db);
    assert_int_equal(1234567890123LL,
        db->universalBlock->block->tuples[0].dnb.data[0].val.i64);
}

/* 5. block is marked dirty after addTuple */
void test_addTuple_block_is_dirty(void **state) {
    (void)state;
    Buffors b;  FSMCache c;  FSMMapAll f;  MVCC mvcc;
    int8_t types[] = {ID_INT32};
    int8_t nulls[] = {0};
    setup_table(&b, &c, &f, &mvcc, 24, 3, types, nulls, 1);

    addTuple(&b, &c, &f, &mvcc, 24,
             (AllVar[]){all_var_from_int32(99)},
             1, (int8_t[]){0}, 1, NULL, NULL);

    DataBuffor *db = find_data_buffor(&b, 24);
    assert_non_null(db);
    assert_int_equal(1, db->isDirty);
}

/* 6. tableId is set correctly on the data buffor */
void test_addTuple_tableId_correct(void **state) {
    (void)state;
    Buffors b;  FSMCache c;  FSMMapAll f;  MVCC mvcc;
    int8_t types[] = {ID_INT32};
    int8_t nulls[] = {0};
    setup_table(&b, &c, &f, &mvcc, 25, 3, types, nulls, 1);

    addTuple(&b, &c, &f, &mvcc, 25,
             (AllVar[]){all_var_from_int32(5)},
             1, (int8_t[]){0}, 1, NULL, NULL);

    DataBuffor *db = find_data_buffor(&b, 25);
    assert_non_null(db);
    assert_int_equal(25, db->tableId);
}

/* 7. null bitmap is stored correctly */
void test_addTuple_null_bitmap_stored(void **state) {
    (void)state;
    Buffors b;  FSMCache c;  FSMMapAll f;  MVCC mvcc;
    int8_t types[] = {ID_INT32, ID_STRING};
    int8_t nulls[] = {0, 1};
    setup_table(&b, &c, &f, &mvcc, 26, 3, types, nulls, 2);

    /* bitmap: col0 not-null (0), col1 null (1) */
    addTuple(&b, &c, &f, &mvcc, 26,
             (AllVar[]){all_var_from_int32(3), all_var_from_string("x")},
             2, (int8_t[]){0, 1}, 2, NULL, NULL);

    DataBuffor *db = find_data_buffor(&b, 26);
    assert_non_null(db);
    assert_int_equal(0, db->universalBlock->block->tuples[0].dnb.bit_map[0]);
    assert_int_equal(1, db->universalBlock->block->tuples[0].dnb.bit_map[1]);
}

/* 8. multiple tuples land in the same block and order is preserved */
void test_addTuple_multiple_tuples_same_block(void **state) {
    (void)state;
    Buffors b;  FSMCache c;  FSMMapAll f;  MVCC mvcc;
    int8_t types[] = {ID_INT32, ID_STRING};
    int8_t nulls[] = {0, 0};
    setup_table(&b, &c, &f, &mvcc, 27, 3, types, nulls, 2);

    addTuple(&b, &c, &f, &mvcc, 27,
             (AllVar[]){all_var_from_int32(10), all_var_from_string("First")},
             2, (int8_t[]){0, 0}, 2, NULL, NULL);
    addTuple(&b, &c, &f, &mvcc, 27,
             (AllVar[]){all_var_from_int32(20), all_var_from_string("Second")},
             2, (int8_t[]){0, 0}, 2, NULL, NULL);
    addTuple(&b, &c, &f, &mvcc, 27,
             (AllVar[]){all_var_from_int32(30), all_var_from_string("Third")},
             2, (int8_t[]){0, 0}, 2, NULL, NULL);

    DataBuffor *db = find_data_buffor(&b, 27);
    assert_non_null(db);
    assert_int_equal(3, db->universalBlock->block->tuple_count);
    assert_int_equal(10,  db->universalBlock->block->tuples[0].dnb.data[0].val.i32);
    assert_string_equal("First",  db->universalBlock->block->tuples[0].dnb.data[1].val.str);
    assert_int_equal(20,  db->universalBlock->block->tuples[1].dnb.data[0].val.i32);
    assert_string_equal("Second", db->universalBlock->block->tuples[1].dnb.data[1].val.str);
    assert_int_equal(30,  db->universalBlock->block->tuples[2].dnb.data[0].val.i32);
    assert_string_equal("Third",  db->universalBlock->block->tuples[2].dnb.data[1].val.str);
}

/* 9. tuples for two different tables land in separate blocks */
void test_addTuple_two_tables_independent(void **state) {
    (void)state;
    Buffors b;  FSMCache c;  FSMMapAll f;  MVCC mvcc;
    initializeBuffors(&b, 6);
    fsm_cache_init(&c);
    init_FSMMapAll(&f);
    mvcc_init(&mvcc);

    char col28[1][MAX_COL_NAME_LEN]; snprintf(col28[0], MAX_COL_NAME_LEN, "id");
    char col29[1][MAX_COL_NAME_LEN]; snprintf(col29[0], MAX_COL_NAME_LEN, "id");
    addTable(&f, &b, &c, &mvcc, 28, (int8_t[]){ID_INT32}, (int8_t[]){0}, col28);
    addTable(&f, &b, &c, &mvcc, 29, (int8_t[]){ID_INT32}, (int8_t[]){0}, col29);

    addTuple(&b, &c, &f, &mvcc, 28,
             (AllVar[]){all_var_from_int32(100)}, 1, (int8_t[]){0}, 1, NULL, NULL);
    addTuple(&b, &c, &f, &mvcc, 29,
             (AllVar[]){all_var_from_int32(200)}, 1, (int8_t[]){0}, 1, NULL, NULL);

    DataBuffor *db28 = find_data_buffor(&b, 28);
    DataBuffor *db29 = find_data_buffor(&b, 29);
    assert_non_null(db28);
    assert_non_null(db29);
    assert_int_equal(100, db28->universalBlock->block->tuples[0].dnb.data[0].val.i32);
    assert_int_equal(200, db29->universalBlock->block->tuples[0].dnb.data[0].val.i32);
    assert_int_equal(1, db28->universalBlock->block->tuple_count);
    assert_int_equal(1, db29->universalBlock->block->tuple_count);
}

/* 10. block8kb_used grows after adding a tuple (more than empty block) */
void test_addTuple_used_space_grows(void **state) {
    (void)state;
    /* empty block baseline: 2 (ID_ALL_BLOCK tag) + 17 (BLOCK_HEADER_SIZE) */
    const int32_t empty_used = 2 + BLOCK_HEADER_SIZE;

    Buffors b;  FSMCache c;  FSMMapAll f;  MVCC mvcc;
    int8_t types[] = {ID_INT32};
    int8_t nulls[] = {0};
    setup_table(&b, &c, &f, &mvcc, 30, 3, types, nulls, 1);

    addTuple(&b, &c, &f, &mvcc, 30,
             (AllVar[]){all_var_from_int32(1)},
             1, (int8_t[]){0}, 1, NULL, NULL);

    DataBuffor *db = find_data_buffor(&b, 30);
    assert_non_null(db);
    assert_true(block8kb_used(db->universalBlock->block) > empty_used);
}

/* 11. negative int32 value is stored correctly */
void test_addTuple_negative_int32(void **state) {
    (void)state;
    Buffors b;  FSMCache c;  FSMMapAll f;  MVCC mvcc;
    int8_t types[] = {ID_INT32};
    int8_t nulls[] = {0};
    setup_table(&b, &c, &f, &mvcc, 31, 3, types, nulls, 1);

    addTuple(&b, &c, &f, &mvcc, 31,
             (AllVar[]){all_var_from_int32(-9999)},
             1, (int8_t[]){0}, 1, NULL, NULL);

    DataBuffor *db = find_data_buffor(&b, 31);
    assert_non_null(db);
    assert_int_equal(-9999, db->universalBlock->block->tuples[0].dnb.data[0].val.i32);
}

/* 12. empty string value is stored without corruption */
void test_addTuple_empty_string(void **state) {
    (void)state;
    Buffors b;  FSMCache c;  FSMMapAll f;  MVCC mvcc;
    int8_t types[] = {ID_INT32, ID_STRING};
    int8_t nulls[] = {0, 0};
    setup_table(&b, &c, &f, &mvcc, 32, 3, types, nulls, 2);

    addTuple(&b, &c, &f, &mvcc, 32,
             (AllVar[]){all_var_from_int32(0), all_var_from_string("")},
             2, (int8_t[]){0, 0}, 2, NULL, NULL);

    DataBuffor *db = find_data_buffor(&b, 32);
    assert_non_null(db);
    assert_string_equal("", db->universalBlock->block->tuples[0].dnb.data[1].val.str);
}

/* 13. pinCount is 0 after addTuple (block released for eviction) */
void test_addTuple_pinCount_zero_after_add(void **state) {
    (void)state;
    Buffors b;  FSMCache c;  FSMMapAll f;  MVCC mvcc;
    int8_t types[] = {ID_INT32};
    int8_t nulls[] = {0};
    setup_table(&b, &c, &f, &mvcc, 33, 3, types, nulls, 1);

    addTuple(&b, &c, &f, &mvcc, 33,
             (AllVar[]){all_var_from_int32(77)},
             1, (int8_t[]){0}, 1, NULL, NULL);

    DataBuffor *db = find_data_buffor(&b, 33);
    assert_non_null(db);
    assert_int_equal(0, db->pinCount);
}

/* 14. three-column tuple: all values correct */
void test_addTuple_three_columns(void **state) {
    (void)state;
    Buffors b;  FSMCache c;  FSMMapAll f;  MVCC mvcc;
    int8_t types[] = {ID_INT32, ID_STRING, ID_INT64};
    int8_t nulls[] = {0, 0, 0};
    setup_table(&b, &c, &f, &mvcc, 34, 3, types, nulls, 3);

    addTuple(&b, &c, &f, &mvcc, 34,
             (AllVar[]){all_var_from_int32(5),
                        all_var_from_string("test"),
                        all_var_from_int64(999999999999LL)},
             3, (int8_t[]){0, 0, 0}, 3, NULL, NULL);

    DataBuffor *db = find_data_buffor(&b, 34);
    assert_non_null(db);
    assert_int_equal(5,             db->universalBlock->block->tuples[0].dnb.data[0].val.i32);
    assert_string_equal("test",     db->universalBlock->block->tuples[0].dnb.data[1].val.str);
    assert_int_equal(999999999999LL,db->universalBlock->block->tuples[0].dnb.data[2].val.i64);
}

/* ── runner ────────────────────────────────────────────────────────────── */

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_addTuple_tuple_count_is_one),
        cmocka_unit_test(test_addTuple_int32_value_correct),
        cmocka_unit_test(test_addTuple_string_value_correct),
        cmocka_unit_test(test_addTuple_int64_value_correct),
        cmocka_unit_test(test_addTuple_block_is_dirty),
        cmocka_unit_test(test_addTuple_tableId_correct),
        cmocka_unit_test(test_addTuple_null_bitmap_stored),
        cmocka_unit_test(test_addTuple_multiple_tuples_same_block),
        cmocka_unit_test(test_addTuple_two_tables_independent),
        cmocka_unit_test(test_addTuple_used_space_grows),
        cmocka_unit_test(test_addTuple_negative_int32),
        cmocka_unit_test(test_addTuple_empty_string),
        cmocka_unit_test(test_addTuple_pinCount_zero_after_add),
        cmocka_unit_test(test_addTuple_three_columns),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
