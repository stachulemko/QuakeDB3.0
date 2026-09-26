//
// Created by stas on 13.04.2026.
//
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdlib.h>
#include "../bufforing-stm/dataBuffor.h"

// ── helpers ───────────────────────────────────────────────────────────────────

static void setup(Buffors *b, FSMCache *c, FSMMapAll *m, MVCC *mvcc, int32_t slots) {
    init_FSMMapAll(m);
    fsm_cache_init(c);
    mvcc_init(mvcc);
    initializeBuffors(b, slots);
}

/* read a data block back from disk — caller must freeUniversalBlock */
static UniversalBlock *read_block(int32_t tableId, int32_t blockNum) {
    uint8_t *raw = fm_get_block(DATA_TABLE_PATH, tableId, blockNum);
    if (!raw) return NULL;
    UniversalBlock *ub = createUniversalBlock(raw);
    free(raw);
    return ub;
}

/* force eviction by adding a dummy table */
static void force_eviction(Buffors *b, FSMCache *c, FSMMapAll *m, MVCC *mvcc, int32_t dummyTableId) {
    addTable(m, b, c, mvcc, dummyTableId,
             (int8_t[]){ID_INT32}, (int8_t[]){0},
             (char[1][MAX_COL_NAME_LEN]){{"x"}});
}

// ── stress tests ──────────────────────────────────────────────────────────────

/* 30 tuples in one table — all must survive eviction */
void test_add30TuplesToOneTable(void **state) {
    (void)state;
    Buffors b; FSMCache c; FSMMapAll m; MVCC mvcc;
    setup(&b, &c, &m, &mvcc, 1);

    addTable(&m, &b, &c, &mvcc, 20,
             (int8_t[]){ID_INT32, ID_STRING}, (int8_t[]){0, 0},
             (char[2][MAX_COL_NAME_LEN]){{"id"}, {"name"}});

    for (int i = 1; i <= 30; i++) {
        char name[32];
        snprintf(name, sizeof(name), "user_%d", i);
        addTuple(&b, &c, &m, &mvcc, 20,
                 (AllVar[]){all_var_from_int32(i), all_var_from_string(name)}, 2,
                 (int8_t[]){0, 0}, 2, NULL, NULL);
    }

    force_eviction(&b, &c, &m, &mvcc, 21);

    UniversalBlock *ub = read_block(20, 1);
    assert_non_null(ub);
    assert_non_null(ub->block);
    assert_int_equal(30, ub->block->tuple_count);
    assert_int_equal(1,  ub->block->tuples[0].dnb.data[0].val.i32);
    assert_int_equal(30, ub->block->tuples[29].dnb.data[0].val.i32);
    freeUniversalBlock(ub);
}

/* the order of int32 values 1..20 must be preserved after eviction */
void test_sequentialInt32ValuesPreserved(void **state) {
    (void)state;
    Buffors b; FSMCache c; FSMMapAll m; MVCC mvcc;
    setup(&b, &c, &m, &mvcc, 1);

    addTable(&m, &b, &c, &mvcc, 22,
             (int8_t[]){ID_INT32}, (int8_t[]){0},
             (char[1][MAX_COL_NAME_LEN]){{"v"}});

    for (int i = 1; i <= 20; i++) {
        addTuple(&b, &c, &m, &mvcc, 22,
                 (AllVar[]){all_var_from_int32(i)}, 1,
                 (int8_t[]){0}, 1, NULL, NULL);
    }

    force_eviction(&b, &c, &m, &mvcc, 23);

    UniversalBlock *ub = read_block(22, 1);
    assert_non_null(ub);
    assert_non_null(ub->block);
    assert_int_equal(20, ub->block->tuple_count);
    for (int i = 0; i < 20; i++) {
        assert_int_equal(i + 1, ub->block->tuples[i].dnb.data[0].val.i32);
    }
    freeUniversalBlock(ub);
}

/* 10 different tables, 3 tuples each — data does not get mixed up */
void test_10TablesEach3TuplesNoMixup(void **state) {
    (void)state;
    Buffors b; FSMCache c; FSMMapAll m; MVCC mvcc;
    setup(&b, &c, &m, &mvcc, 1);

    for (int t = 0; t < 10; t++) {
        int32_t tableId = 30 + t * 2;       /* 30, 32, 34, ... 48 */
        int32_t dummyId = 30 + t * 2 + 1;  /* 31, 33, 35, ... 49 */

        addTable(&m, &b, &c, &mvcc, tableId,
                 (int8_t[]){ID_INT32, ID_STRING}, (int8_t[]){0, 0},
                 (char[2][MAX_COL_NAME_LEN]){{"id"}, {"src"}});

        for (int i = 0; i < 3; i++) {
            addTuple(&b, &c, &m, &mvcc, tableId,
                     (AllVar[]){all_var_from_int32(tableId * 100 + i),
                                all_var_from_string("data")}, 2,
                     (int8_t[]){0, 0}, 2, NULL, NULL);
        }
        force_eviction(&b, &c, &m, &mvcc, dummyId);
    }

    for (int t = 0; t < 10; t++) {
        int32_t tableId = 30 + t * 2;
        UniversalBlock *ub = read_block(tableId, 1);
        assert_non_null(ub);
        assert_non_null(ub->block);
        assert_int_equal(3, ub->block->tuple_count);
        assert_int_equal(tableId * 100 + 0, ub->block->tuples[0].dnb.data[0].val.i32);
        assert_int_equal(tableId * 100 + 1, ub->block->tuples[1].dnb.data[0].val.i32);
        assert_int_equal(tableId * 100 + 2, ub->block->tuples[2].dnb.data[0].val.i32);
        freeUniversalBlock(ub);
    }
}

/* stress: int32 + int64 + string in one table */
void test_mixedTypesStressEviction(void **state) {
    (void)state;
    Buffors b; FSMCache c; FSMMapAll m; MVCC mvcc;
    setup(&b, &c, &m, &mvcc, 1);

    addTable(&m, &b, &c, &mvcc, 60,
             (int8_t[]){ID_INT32, ID_INT64, ID_STRING}, (int8_t[]){0, 0, 0},
             (char[3][MAX_COL_NAME_LEN]){{"i32"}, {"i64"}, {"str"}});

    for (int i = 1; i <= 15; i++) {
        char s[32];
        snprintf(s, sizeof(s), "str_%d", i);
        addTuple(&b, &c, &m, &mvcc, 60,
                 (AllVar[]){all_var_from_int32(i),
                            all_var_from_int64((int64_t)i * 1000000000LL),
                            all_var_from_string(s)}, 3,
                 (int8_t[]){0, 0, 0}, 3, NULL, NULL);
    }

    force_eviction(&b, &c, &m, &mvcc, 61);

    UniversalBlock *ub = read_block(60, 1);
    assert_non_null(ub);
    assert_non_null(ub->block);
    assert_int_equal(15, ub->block->tuple_count);
    for (int i = 0; i < 15; i++) {
        assert_int_equal(i + 1, ub->block->tuples[i].dnb.data[0].val.i32);
        assert_int_equal((int64_t)(i + 1) * 1000000000LL,
                         ub->block->tuples[i].dnb.data[1].val.i64);
    }
    assert_string_equal("str_1",  ub->block->tuples[0].dnb.data[2].val.str);
    assert_string_equal("str_15", ub->block->tuples[14].dnb.data[2].val.str);
    freeUniversalBlock(ub);
}

/* 1 buffer, 8 tables with constant eviction — the latest data is always correct */
void test_singleBufforManyEvictions(void **state) {
    (void)state;
    Buffors b; FSMCache c; FSMMapAll m; MVCC mvcc;
    setup(&b, &c, &m, &mvcc, 1);

    for (int t = 0; t < 8; t++) {
        int32_t tableId = 70 + t * 2;
        int32_t dummyId = 70 + t * 2 + 1;

        addTable(&m, &b, &c, &mvcc, tableId,
                 (int8_t[]){ID_INT32}, (int8_t[]){0},
                 (char[1][MAX_COL_NAME_LEN]){{"v"}});
        addTuple(&b, &c, &m, &mvcc, tableId,
                 (AllVar[]){all_var_from_int32(tableId)}, 1, (int8_t[]){0}, 1, NULL, NULL);
        addTuple(&b, &c, &m, &mvcc, tableId,
                 (AllVar[]){all_var_from_int32(tableId + 1)}, 1, (int8_t[]){0}, 1, NULL, NULL);
        force_eviction(&b, &c, &m, &mvcc, dummyId);
    }

    for (int t = 0; t < 8; t++) {
        int32_t tableId = 70 + t * 2;
        UniversalBlock *ub = read_block(tableId, 1);
        assert_non_null(ub);
        assert_non_null(ub->block);
        assert_int_equal(2, ub->block->tuple_count);
        assert_int_equal(tableId,     ub->block->tuples[0].dnb.data[0].val.i32);
        assert_int_equal(tableId + 1, ub->block->tuples[1].dnb.data[0].val.i32);
        freeUniversalBlock(ub);
    }
}

/* strings close to MAX_STR_LEN (120 chars) survive eviction */
void test_largeStringValuesSurviveEviction(void **state) {
    (void)state;
    Buffors b; FSMCache c; FSMMapAll m; MVCC mvcc;
    setup(&b, &c, &m, &mvcc, 1);

    char big[121];
    memset(big, 'A', 120);
    big[120] = '\0';

    addTable(&m, &b, &c, &mvcc, 90,
             (int8_t[]){ID_INT32, ID_STRING}, (int8_t[]){0, 0},
             (char[2][MAX_COL_NAME_LEN]){{"id"}, {"payload"}});

    for (int i = 0; i < 5; i++) {
        big[0] = 'A' + i;  /* a different first char for every record */
        addTuple(&b, &c, &m, &mvcc, 90,
                 (AllVar[]){all_var_from_int32(i), all_var_from_string(big)}, 2,
                 (int8_t[]){0, 0}, 2, NULL, NULL);
    }

    force_eviction(&b, &c, &m, &mvcc, 91);

    UniversalBlock *ub = read_block(90, 1);
    assert_non_null(ub);
    assert_non_null(ub->block);
    assert_int_equal(5, ub->block->tuple_count);
    for (int i = 0; i < 5; i++) {
        assert_int_equal(i, ub->block->tuples[i].dnb.data[0].val.i32);
        assert_int_equal('A' + i,
                         (unsigned char)ub->block->tuples[i].dnb.data[1].val.str[0]);
        assert_int_equal(120,
                         (int)strlen(ub->block->tuples[i].dnb.data[1].val.str));
    }
    freeUniversalBlock(ub);
}

/* int32 boundary values: INT32_MIN, -1, 0, 1, INT32_MAX */
void test_boundaryInt32ValuesStress(void **state) {
    (void)state;
    Buffors b; FSMCache c; FSMMapAll m; MVCC mvcc;
    setup(&b, &c, &m, &mvcc, 1);

    int32_t vals[] = {-2147483648, -1, 0, 1, 2147483647};

    addTable(&m, &b, &c, &mvcc, 92,
             (int8_t[]){ID_INT32}, (int8_t[]){0},
             (char[1][MAX_COL_NAME_LEN]){{"v"}});

    for (int i = 0; i < 5; i++) {
        addTuple(&b, &c, &m, &mvcc, 92,
                 (AllVar[]){all_var_from_int32(vals[i])}, 1, (int8_t[]){0}, 1, NULL, NULL);
    }

    force_eviction(&b, &c, &m, &mvcc, 93);

    UniversalBlock *ub = read_block(92, 1);
    assert_non_null(ub);
    assert_non_null(ub->block);
    assert_int_equal(5, ub->block->tuple_count);
    for (int i = 0; i < 5; i++) {
        assert_int_equal(vals[i], ub->block->tuples[i].dnb.data[0].val.i32);
    }
    freeUniversalBlock(ub);
}

/* 2 buffers — tuples interleaved between two tables */
void test_twoBufforsDataIntegrity(void **state) {
    (void)state;
    Buffors b; FSMCache c; FSMMapAll m; MVCC mvcc;
    setup(&b, &c, &m, &mvcc, 2);

    addTable(&m, &b, &c, &mvcc, 94,
             (int8_t[]){ID_INT32}, (int8_t[]){0},
             (char[1][MAX_COL_NAME_LEN]){{"a"}});
    addTable(&m, &b, &c, &mvcc, 95,
             (int8_t[]){ID_INT32}, (int8_t[]){0},
             (char[1][MAX_COL_NAME_LEN]){{"b"}});

    for (int i = 0; i < 10; i++) {
        addTuple(&b, &c, &m, &mvcc, 94,
                 (AllVar[]){all_var_from_int32(i)}, 1, (int8_t[]){0}, 1, NULL, NULL);
        addTuple(&b, &c, &m, &mvcc, 95,
                 (AllVar[]){all_var_from_int32(i * 10)}, 1, (int8_t[]){0}, 1, NULL, NULL);
    }

    force_eviction(&b, &c, &m, &mvcc, 96);
    force_eviction(&b, &c, &m, &mvcc, 97);

    UniversalBlock *ub94 = read_block(94, 1);
    UniversalBlock *ub95 = read_block(95, 1);
    assert_non_null(ub94); assert_non_null(ub94->block);
    assert_non_null(ub95); assert_non_null(ub95->block);
    assert_int_equal(10, ub94->block->tuple_count);
    assert_int_equal(10, ub95->block->tuple_count);
    for (int i = 0; i < 10; i++) {
        assert_int_equal(i,      ub94->block->tuples[i].dnb.data[0].val.i32);
        assert_int_equal(i * 10, ub95->block->tuples[i].dnb.data[0].val.i32);
    }
    freeUniversalBlock(ub94);
    freeUniversalBlock(ub95);
}

/* the same value added 10 times — all copies kept */
void test_duplicateValuesAllStored(void **state) {
    (void)state;
    Buffors b; FSMCache c; FSMMapAll m; MVCC mvcc;
    setup(&b, &c, &m, &mvcc, 1);

    addTable(&m, &b, &c, &mvcc, 98,
             (int8_t[]){ID_INT32, ID_STRING}, (int8_t[]){0, 0},
             (char[2][MAX_COL_NAME_LEN]){{"id"}, {"tag"}});

    for (int i = 0; i < 10; i++) {
        addTuple(&b, &c, &m, &mvcc, 98,
                 (AllVar[]){all_var_from_int32(42), all_var_from_string("dup")}, 2,
                 (int8_t[]){0, 0}, 2, NULL, NULL);
    }

    force_eviction(&b, &c, &m, &mvcc, 99);

    UniversalBlock *ub = read_block(98, 1);
    assert_non_null(ub);
    assert_non_null(ub->block);
    assert_int_equal(10, ub->block->tuple_count);
    for (int i = 0; i < 10; i++) {
        assert_int_equal(42, ub->block->tuples[i].dnb.data[0].val.i32);
        assert_string_equal("dup", ub->block->tuples[i].dnb.data[1].val.str);
    }
    freeUniversalBlock(ub);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_add30TuplesToOneTable),
        cmocka_unit_test(test_sequentialInt32ValuesPreserved),
        cmocka_unit_test(test_10TablesEach3TuplesNoMixup),
        cmocka_unit_test(test_mixedTypesStressEviction),
        cmocka_unit_test(test_singleBufforManyEvictions),
        cmocka_unit_test(test_largeStringValuesSurviveEviction),
        cmocka_unit_test(test_boundaryInt32ValuesStress),
        cmocka_unit_test(test_twoBufforsDataIntegrity),
        cmocka_unit_test(test_duplicateValuesAllStored),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
