#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdlib.h>
#include "../bufforing-stm/dataBuffor.h"


// ── in-memory buffor tests ────────────────────────────────────────────────

void test_ifAddingBufforsWorks(void **state) {
    (void)state;
    Buffors buffors;
    FSMCache fsmCache;
    FSMMapAll fsmMapAll;
    MVCC mvcc;
    init_FSMMapAll(&fsmMapAll);
    fsm_cache_init(&fsmCache);
    mvcc_init(&mvcc);
    initializeBuffors(&buffors, 3);

    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,1,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 1},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"name"}});
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,1,(AllVar[]){all_var_from_int32(41), all_var_from_string("Alice")},2,(int8_t[]){0, 0},2, NULL, NULL);
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,1,(AllVar[]){all_var_from_int32(42), all_var_from_string("Alice")},2,(int8_t[]){0, 0},2, NULL, NULL);
    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,2,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 1},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"name"}});

    // slot 0: table 1 header
    assert_non_null(buffors.buffors[0].universalBlock->header);
    assert_null(buffors.buffors[0].universalBlock->block);
    // slot 1: table 1 data block (both tuples)
    assert_non_null(buffors.buffors[1].universalBlock->block);
    assert_null(buffors.buffors[1].universalBlock->header);
    // slot 2: table 2 header
    assert_non_null(buffors.buffors[2].universalBlock->header);
    assert_null(buffors.buffors[2].universalBlock->block);
}

void test_ifEvictionToDiskWork(void **state) {
    (void)state;
    Buffors buffors;
    FSMCache fsmCache;
    FSMMapAll fsmMapAll;
    MVCC mvcc;
    init_FSMMapAll(&fsmMapAll);
    fsm_cache_init(&fsmCache);
    mvcc_init(&mvcc);
    initializeBuffors(&buffors, 1);

    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,1,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 1},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"name"}});
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,1,(AllVar[]){all_var_from_int32(41), all_var_from_string("Alice")},2,(int8_t[]){0, 0},2, NULL, NULL);

    // header evicted, slot 0 now holds the data block
    assert_non_null(buffors.buffors[0].universalBlock->block);
    assert_null(buffors.buffors[0].universalBlock->header);
}

void test_ifAddingBufforsToDifferentTablesWorks(void **state) {
    (void)state;
    Buffors buffors;
    FSMCache fsmCache;
    FSMMapAll fsmMapAll;
    MVCC mvcc;
    init_FSMMapAll(&fsmMapAll);
    fsm_cache_init(&fsmCache);
    mvcc_init(&mvcc);
    initializeBuffors(&buffors, 4);

    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,1,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 1},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"name"}});
    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,2,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 1},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"name"}});
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,1,(AllVar[]){all_var_from_int32(41), all_var_from_string("Alice")},2,(int8_t[]){0, 0},2, NULL, NULL);
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,2,(AllVar[]){all_var_from_int32(42), all_var_from_string("Alice")},2,(int8_t[]){0, 0},2, NULL, NULL);
    // second addTable(2): no free slots → evicts slot 0 (header table 1), replaces with header table 2
    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,2,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 1},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"name"}});

    // slot 0: evicted, now holds header for table 2
    assert_non_null(buffors.buffors[0].universalBlock->header);
    assert_null(buffors.buffors[0].universalBlock->block);
    assert_int_equal(2, buffors.buffors[0].tableId);

    // slot 1: original header for table 2
    assert_non_null(buffors.buffors[1].universalBlock->header);
    assert_null(buffors.buffors[1].universalBlock->block);
    assert_int_equal(2, buffors.buffors[1].tableId);

    // slot 2: data block for table 1
    assert_non_null(buffors.buffors[2].universalBlock->block);
    assert_null(buffors.buffors[2].universalBlock->header);
    assert_int_equal(1, buffors.buffors[2].tableId);

    // slot 3: data block for table 2
    assert_non_null(buffors.buffors[3].universalBlock->block);
    assert_null(buffors.buffors[3].universalBlock->header);
    assert_int_equal(2, buffors.buffors[3].tableId);
}

// ── disk eviction tests ───────────────────────────────────────────────────
//
// Strategy (1 slot):
//   addTable(N)       → slot 0 = header N  (dirty)
//   addTuple(N, ..., NULL, NULL)  → evicts header to disk, slot 0 = data block  (dirty)
//   addTable(N+1)     → evicts data block to disk, slot 0 = header N+1
//   fm_get_block(N,1) → read the evicted data block back and verify

void test_singleTupleCorrectOnDisk(void **state) {
    (void)state;
    Buffors buffors;
    FSMCache fsmCache;
    FSMMapAll fsmMapAll;
    MVCC mvcc;
    init_FSMMapAll(&fsmMapAll);
    fsm_cache_init(&fsmCache);
    mvcc_init(&mvcc);
    initializeBuffors(&buffors, 1);

    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,3,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 1},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"name"}});
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,3,(AllVar[]){all_var_from_int32(77), all_var_from_string("Zara")},2,(int8_t[]){0, 0},2, NULL, NULL);
    // force eviction of data block to disk
    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,4,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 1},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"name"}});

    uint8_t *raw = fm_get_block(DATA_TABLE_PATH, 3, 1);
    assert_non_null(raw);
    UniversalBlock *ub = createUniversalBlock(raw);
    free(raw);

    assert_non_null(ub);
    assert_non_null(ub->block);
    assert_int_equal(1, ub->block->tuple_count);
    assert_int_equal(77, ub->block->tuples[0].dnb.data[0].val.i32);
    assert_string_equal("Zara", ub->block->tuples[0].dnb.data[1].val.str);

    freeUniversalBlock(ub);
}

void test_multipleTuplesSurviveEviction(void **state) {
    (void)state;
    Buffors buffors;
    FSMCache fsmCache;
    FSMMapAll fsmMapAll;
    MVCC mvcc;
    init_FSMMapAll(&fsmMapAll);
    fsm_cache_init(&fsmCache);
    mvcc_init(&mvcc);
    initializeBuffors(&buffors, 1);

    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,5,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 1},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"val"}});
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,5,(AllVar[]){all_var_from_int32(10), all_var_from_string("Alpha")},2,(int8_t[]){0, 0},2, NULL, NULL);
    // second tuple goes into the same data block (already in slot 0)
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,5,(AllVar[]){all_var_from_int32(20), all_var_from_string("Beta")},2,(int8_t[]){0, 0},2, NULL, NULL);
    // evict 2-tuple block to disk
    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,6,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 1},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"val"}});

    uint8_t *raw = fm_get_block(DATA_TABLE_PATH, 5, 1);
    assert_non_null(raw);
    UniversalBlock *ub = createUniversalBlock(raw);
    free(raw);

    assert_non_null(ub);
    assert_non_null(ub->block);
    assert_int_equal(2, ub->block->tuple_count);
    assert_int_equal(10, ub->block->tuples[0].dnb.data[0].val.i32);
    assert_string_equal("Alpha", ub->block->tuples[0].dnb.data[1].val.str);
    assert_int_equal(20, ub->block->tuples[1].dnb.data[0].val.i32);
    assert_string_equal("Beta", ub->block->tuples[1].dnb.data[1].val.str);

    freeUniversalBlock(ub);
}

void test_blockIdPreservedAfterEviction(void **state) {
    (void)state;
    Buffors buffors;
    FSMCache fsmCache;
    FSMMapAll fsmMapAll;
    MVCC mvcc;
    init_FSMMapAll(&fsmMapAll);
    fsm_cache_init(&fsmCache);
    mvcc_init(&mvcc);
    initializeBuffors(&buffors, 1);

    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,7,(int8_t[]){ID_INT32},(int8_t[]){0},(char[1][MAX_COL_NAME_LEN]){{"id"}});
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,7,(AllVar[]){all_var_from_int32(99)},1,(int8_t[]){0},1, NULL, NULL);
    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,8,(int8_t[]){ID_INT32},(int8_t[]){0},(char[1][MAX_COL_NAME_LEN]){{"id"}});

    uint8_t *raw = fm_get_block(DATA_TABLE_PATH, 7, 1);
    assert_non_null(raw);
    UniversalBlock *ub = createUniversalBlock(raw);
    free(raw);

    assert_non_null(ub);
    assert_non_null(ub->block);
    assert_int_equal(1, ub->block->header.block_id);
    assert_int_equal(99, ub->block->tuples[0].dnb.data[0].val.i32);

    freeUniversalBlock(ub);
}

// ── more disk eviction tests ──────────────────────────────────────────────

void test_int64ValueSurvivesEviction(void **state) {
    (void)state;
    Buffors buffors;
    FSMCache fsmCache;
    FSMMapAll fsmMapAll;
    MVCC mvcc;
    init_FSMMapAll(&fsmMapAll);
    fsm_cache_init(&fsmCache);
    mvcc_init(&mvcc);
    initializeBuffors(&buffors, 1);

    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,9,(int8_t[]){ID_INT64},(int8_t[]){0},(char[1][MAX_COL_NAME_LEN]){{"big"}});
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,9,(AllVar[]){all_var_from_int64(9876543210LL)},1,(int8_t[]){0},1, NULL, NULL);
    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,10,(int8_t[]){ID_INT64},(int8_t[]){0},(char[1][MAX_COL_NAME_LEN]){{"big"}});

    uint8_t *raw = fm_get_block(DATA_TABLE_PATH, 9, 1);
    assert_non_null(raw);
    UniversalBlock *ub = createUniversalBlock(raw);
    free(raw);

    assert_non_null(ub);
    assert_non_null(ub->block);
    assert_int_equal(1, ub->block->tuple_count);
    assert_int_equal(9876543210LL, ub->block->tuples[0].dnb.data[0].val.i64);

    freeUniversalBlock(ub);
}

void test_negativeAndMaxInt32SurviveEviction(void **state) {
    (void)state;
    Buffors buffors;
    FSMCache fsmCache;
    FSMMapAll fsmMapAll;
    MVCC mvcc;
    init_FSMMapAll(&fsmMapAll);
    fsm_cache_init(&fsmCache);
    mvcc_init(&mvcc);
    initializeBuffors(&buffors, 1);

    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,11,(int8_t[]){ID_INT32, ID_INT32},(int8_t[]){0, 0},(char[2][MAX_COL_NAME_LEN]){{"neg"}, {"max"}});
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,11,(AllVar[]){all_var_from_int32(-1), all_var_from_int32(2147483647)},2,(int8_t[]){0, 0},2, NULL, NULL);
    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,12,(int8_t[]){ID_INT32},(int8_t[]){0},(char[1][MAX_COL_NAME_LEN]){{"x"}});

    uint8_t *raw = fm_get_block(DATA_TABLE_PATH, 11, 1);
    assert_non_null(raw);
    UniversalBlock *ub = createUniversalBlock(raw);
    free(raw);

    assert_non_null(ub);
    assert_non_null(ub->block);
    assert_int_equal(1, ub->block->tuple_count);
    assert_int_equal(-1,         ub->block->tuples[0].dnb.data[0].val.i32);
    assert_int_equal(2147483647, ub->block->tuples[0].dnb.data[1].val.i32);

    freeUniversalBlock(ub);
}

void test_threeTuplesInBlockSurviveEviction(void **state) {
    (void)state;
    Buffors buffors;
    FSMCache fsmCache;
    FSMMapAll fsmMapAll;
    MVCC mvcc;
    init_FSMMapAll(&fsmMapAll);
    fsm_cache_init(&fsmCache);
    mvcc_init(&mvcc);
    initializeBuffors(&buffors, 1);

    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,13,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 0},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"name"}});
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,13,(AllVar[]){all_var_from_int32(1), all_var_from_string("One")},  2,(int8_t[]){0, 0},2, NULL, NULL);
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,13,(AllVar[]){all_var_from_int32(2), all_var_from_string("Two")},  2,(int8_t[]){0, 0},2, NULL, NULL);
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,13,(AllVar[]){all_var_from_int32(3), all_var_from_string("Three")},2,(int8_t[]){0, 0},2, NULL, NULL);
    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,14,(int8_t[]){ID_INT32},(int8_t[]){0},(char[1][MAX_COL_NAME_LEN]){{"x"}});

    uint8_t *raw = fm_get_block(DATA_TABLE_PATH, 13, 1);
    assert_non_null(raw);
    UniversalBlock *ub = createUniversalBlock(raw);
    free(raw);

    assert_non_null(ub);
    assert_non_null(ub->block);
    assert_int_equal(3, ub->block->tuple_count);
    assert_int_equal(1, ub->block->tuples[0].dnb.data[0].val.i32);
    assert_string_equal("One",   ub->block->tuples[0].dnb.data[1].val.str);
    assert_int_equal(2, ub->block->tuples[1].dnb.data[0].val.i32);
    assert_string_equal("Two",   ub->block->tuples[1].dnb.data[1].val.str);
    assert_int_equal(3, ub->block->tuples[2].dnb.data[0].val.i32);
    assert_string_equal("Three", ub->block->tuples[2].dnb.data[1].val.str);

    freeUniversalBlock(ub);
}

void test_twoTablesStoredSeparatelyOnDisk(void **state) {
    (void)state;
    Buffors buffors;
    FSMCache fsmCache;
    FSMMapAll fsmMapAll;
    MVCC mvcc;
    init_FSMMapAll(&fsmMapAll);
    fsm_cache_init(&fsmCache);
    mvcc_init(&mvcc);
    initializeBuffors(&buffors, 1);

    /* table 15 data evicted to 15.bin */
    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,15,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 0},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"src"}});
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,15,(AllVar[]){all_var_from_int32(150), all_var_from_string("TableA")},2,(int8_t[]){0, 0},2, NULL, NULL);
    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,16,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 0},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"src"}});

    /* table 17 data evicted to 17.bin */
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,16,(AllVar[]){all_var_from_int32(160), all_var_from_string("TableB")},2,(int8_t[]){0, 0},2, NULL, NULL);
    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,17,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 0},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"src"}});
    addTuple(&buffors,&fsmCache,&fsmMapAll,&mvcc,17,(AllVar[]){all_var_from_int32(170), all_var_from_string("TableC")},2,(int8_t[]){0, 0},2, NULL, NULL);
    addTable(&fsmMapAll,&buffors,&fsmCache,&mvcc,18,(int8_t[]){ID_INT32},(int8_t[]){0},(char[1][MAX_COL_NAME_LEN]){{"x"}});

    /* 15.bin must have TableA, not overwritten by TableB or TableC */
    uint8_t *raw15 = fm_get_block(DATA_TABLE_PATH, 15, 1);
    assert_non_null(raw15);
    UniversalBlock *ub15 = createUniversalBlock(raw15);
    free(raw15);
    assert_non_null(ub15->block);
    assert_int_equal(150, ub15->block->tuples[0].dnb.data[0].val.i32);
    assert_string_equal("TableA", ub15->block->tuples[0].dnb.data[1].val.str);
    freeUniversalBlock(ub15);

    /* 17.bin must have TableC */
    uint8_t *raw17 = fm_get_block(DATA_TABLE_PATH, 17, 1);
    assert_non_null(raw17);
    UniversalBlock *ub17 = createUniversalBlock(raw17);
    free(raw17);
    assert_non_null(ub17->block);
    assert_int_equal(170, ub17->block->tuples[0].dnb.data[0].val.i32);
    assert_string_equal("TableC", ub17->block->tuples[0].dnb.data[1].val.str);
    freeUniversalBlock(ub17);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ifAddingBufforsWorks),
        cmocka_unit_test(test_ifEvictionToDiskWork),
        cmocka_unit_test(test_ifAddingBufforsToDifferentTablesWorks),
        cmocka_unit_test(test_singleTupleCorrectOnDisk),
        cmocka_unit_test(test_multipleTuplesSurviveEviction),
        cmocka_unit_test(test_blockIdPreservedAfterEviction),
        cmocka_unit_test(test_int64ValueSurvivesEviction),
        cmocka_unit_test(test_negativeAndMaxInt32SurviveEviction),
        cmocka_unit_test(test_threeTuplesInBlockSurviveEviction),
        cmocka_unit_test(test_twoTablesStoredSeparatelyOnDisk),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
