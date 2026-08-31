//
// Created by stas on 12.05.2026.
//
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "../bufforing-stm//dataBuffor.h"
#include "../memory-mgmt//all_var.h"
#include "../bufforing-stm//transaction.h"
#include "../bufforing-stm//fullScan.h"
#include "../bufforing-stm//queryExecutor.h"


typedef struct {
    Buffors buffors;

    FSMCache fsmCache;

    FSMMapAll fsmMapAll;

    MVCC mvcc;
}SetedUpEnv;

int8_t evaluate(ResultTuple result_tuple, Buffors* buffors, int32_t tableId) {
    if (result_tuple.tuple_count == 0) return 0;

    DataBuffor *buffor = getBuffor(tableId, 1, buffors);
    if (buffor == NULL || buffor->universalBlock->block == NULL) return 0;

    if (buffor->universalBlock->block->tuple_count != result_tuple.tuple_count) {
        return 0;
    }

    for (int i = 0; i < result_tuple.tuple_count; i++) {
        if (evaluateTuples(buffor->universalBlock->block->tuples[i], result_tuple.tuples[i]) == 0) {
            return 0;
        }
    }
    return 1;
}


SetedUpEnv setUpTablesSimple() {
    Buffors buffors;

    FSMCache fsmCache;

    FSMMapAll fsmMapAll;

    MVCC mvcc;

    init_FSMMapAll(&fsmMapAll);

    fsm_cache_init(&fsmCache);

    mvcc_init(&mvcc);

    initializeBuffors(&buffors, 3);

    addTable(&fsmMapAll, &buffors, &fsmCache, &mvcc, 20,
             (int8_t[]){ID_INT32, ID_STRING}, (int8_t[]){0, 0},
             (char[2][MAX_COL_NAME_LEN]){{"id"}, {"name"}});
    // showBuffors(&buffors);
    addTuple(&buffors, &fsmCache, &fsmMapAll, &mvcc, 20,
         (AllVar[]){all_var_from_int32(1), all_var_from_string("Alice")},
         2,
         (int8_t[]){0, 0},
         2);
    // showBuffors(&buffors);
    addTuple(&buffors, &fsmCache, &fsmMapAll, &mvcc, 20,
             (AllVar[]){all_var_from_int32(2), all_var_from_string("Patrick")},
             2,
             (int8_t[]){0, 0},
             2);
    addTuple(&buffors, &fsmCache, &fsmMapAll, &mvcc, 20,
             (AllVar[]){all_var_from_int32(3), all_var_from_string("alan")},
             2,
             (int8_t[]){0, 0},
             2);
    addTuple(&buffors, &fsmCache, &fsmMapAll, &mvcc, 20,
             (AllVar[]){all_var_from_int32(4), all_var_from_string("adssdadasd")},
             2,
             (int8_t[]){0, 0},
             2);
    SetedUpEnv seted_up_env;
    seted_up_env.buffors = buffors;
    seted_up_env.fsmCache = fsmCache;
    seted_up_env.fsmMapAll = fsmMapAll;
    seted_up_env.mvcc = mvcc;
    return seted_up_env;
}


SetedUpEnv setUpTablesDemanding() {
    Buffors buffors;

    FSMCache fsmCache;

    FSMMapAll fsmMapAll;

    MVCC mvcc;

    init_FSMMapAll(&fsmMapAll);

    fsm_cache_init(&fsmCache);

    mvcc_init(&mvcc);

    initializeBuffors(&buffors, 5);

    addTable(&fsmMapAll, &buffors, &fsmCache, &mvcc, 21,
             (int8_t[]){ID_INT32, ID_STRING, ID_STRING},
             (int8_t[]){0, 0, 0},
             (char[3][MAX_COL_NAME_LEN]){{"product_id"}, {"product_name"}, {"category"}});

    addTuple(&buffors, &fsmCache, &fsmMapAll, &mvcc, 21,
             (AllVar[]){all_var_from_int32(101), all_var_from_string("Laptop"),
                       all_var_from_string("Electronics")},
             3, (int8_t[]){0, 0, 0}, 3);

    addTuple(&buffors, &fsmCache, &fsmMapAll, &mvcc, 21,
             (AllVar[]){all_var_from_int32(102), all_var_from_string("Phone"),
                       all_var_from_string("Electronics")},
             3, (int8_t[]){0, 0, 0}, 3);

    addTuple(&buffors, &fsmCache, &fsmMapAll, &mvcc, 21,
             (AllVar[]){all_var_from_int32(103), all_var_from_string("Desk"),
                       all_var_from_string("Furniture")},
             3, (int8_t[]){0, 0, 0}, 3);

    addTuple(&buffors, &fsmCache, &fsmMapAll, &mvcc, 21,
             (AllVar[]){all_var_from_int32(104), all_var_from_string("Chair"),
                       all_var_from_string("Furniture")},
             3, (int8_t[]){0, 0, 0}, 3);
    SetedUpEnv seted_up_env;
    seted_up_env.buffors = buffors;
    seted_up_env.fsmCache = fsmCache;
    seted_up_env.fsmMapAll = fsmMapAll;
    seted_up_env.mvcc = mvcc;
    return seted_up_env;
}


void test_selectAll(void **state){
    SetedUpEnv seted_up_en = setUpTablesSimple();
    Transaction *transaction = (Transaction *)malloc(sizeof(Transaction));

    beginTransaction(&(seted_up_en.mvcc), transaction);
    QueryExecutor qe ={0};
    int32_t cols[MAX_COLUMNS] = {0, 1};
    Qeselect(&qe, cols,2);
    Qefrom(&qe, 20);
    Qeend(&qe, &transaction, &(seted_up_en.fsmCache));

    ResultTuple result_tuple = {0};
    FullScan full_scan = {0};
    full_scan.rt = &result_tuple;
    full_scan.qe = &qe;
    fullScan(&full_scan,&(seted_up_en.buffors));

    assert_int_equal(evaluate(result_tuple, &(seted_up_en.buffors), 20), 1);

    free(transaction);

}
void test_selectAlld(void **state) {
    SetedUpEnv seted_up_en = setUpTablesDemanding();

    Transaction *transaction = (Transaction *)malloc(sizeof(Transaction));

    beginTransaction(&(seted_up_en.mvcc), transaction);
    QueryExecutor qe ={0};
    int32_t cols[MAX_COLUMNS] = {0, 1, 2};
    Qeselect(&qe, cols, 3);
    Qefrom(&qe, 21);
    Qeend(&qe, &transaction, &(seted_up_en.fsmCache));

    ResultTuple result_tuple = {0};
    FullScan full_scan = {0};
    full_scan.rt = &result_tuple;
    full_scan.qe = &qe;
    fullScan(&full_scan,&(seted_up_en.buffors));

    assert_int_equal(evaluate(result_tuple, &(seted_up_en.buffors), 21), 1);

    free(transaction);

}

void test_fullScanWithUpdate_simple(void **state) {
    SetedUpEnv seted_up_en = setUpTablesSimple();
    Transaction *transaction = (Transaction *)malloc(sizeof(Transaction));

    beginTransaction(&(seted_up_en.mvcc), transaction);
    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {0, 1};
    Qeselect(&qe, cols, 2);
    Qefrom(&qe, 20);
    Qeend(&qe, &transaction, &(seted_up_en.fsmCache));

    ResultTuple result_tuple = {0};
    FullScan full_scan = {0};
    full_scan.rt = &result_tuple;
    full_scan.qe = &qe;
    fullScanWithUpdate(&full_scan, &(seted_up_en.buffors));

    assert_int_equal(evaluate(result_tuple, &(seted_up_en.buffors), 20), 1);

    free(transaction);
}

void test_fullScanWithUpdate_demanding(void **state) {
    SetedUpEnv seted_up_en = setUpTablesDemanding();
    Transaction *transaction = (Transaction *)malloc(sizeof(Transaction));

    beginTransaction(&(seted_up_en.mvcc), transaction);
    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {0, 1, 2};
    Qeselect(&qe, cols, 3);
    Qefrom(&qe, 21);
    Qeend(&qe, &transaction, &(seted_up_en.fsmCache));

    ResultTuple result_tuple = {0};
    FullScan full_scan = {0};
    full_scan.rt = &result_tuple;
    full_scan.qe = &qe;
    fullScanWithUpdate(&full_scan, &(seted_up_en.buffors));

    assert_int_equal(evaluate(result_tuple, &(seted_up_en.buffors), 21), 1);

    free(transaction);
}

void test_where_fullScanWithUpdate(void **state) {
    SetedUpEnv seted_up_en = setUpTablesDemanding();
    Transaction *transaction = (Transaction *)malloc(sizeof(Transaction));

    beginTransaction(&(seted_up_en.mvcc), transaction);
    QueryExecutor qe = {0};
    int32_t cols[MAX_COLUMNS] = {2};
    AllVar vals[MAX_COLUMNS] = {all_var_from_string("Furniture")};
    Qewhere(&qe, cols, vals, 1);
    Qefrom(&qe, 21);
    Qeend(&qe, &transaction, &(seted_up_en.fsmCache));

    ResultTuple result_tuple = {0};
    FullScan full_scan = {0};
    full_scan.rt = &result_tuple;
    full_scan.qe = &qe;
    fullScanWithUpdate(&full_scan, &(seted_up_en.buffors));

    assert_int_equal(result_tuple.tuple_count, 2);
    assert_int_equal(result_tuple.tuples[0].dnb.data[0].val.i32, 103);
    assert_int_equal(result_tuple.tuples[1].dnb.data[0].val.i32, 104);

    free(transaction);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_selectAll),
        cmocka_unit_test(test_selectAlld),
        cmocka_unit_test(test_fullScanWithUpdate_simple),
        cmocka_unit_test(test_fullScanWithUpdate_demanding),
        cmocka_unit_test(test_where_fullScanWithUpdate),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}