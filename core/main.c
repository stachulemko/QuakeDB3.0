#include <stdio.h>
#include "bufforing-stm/bufforing-stm/dataBuffor.h"
#include "memory-mgmt/memory-mgmt/all_var.h"
#include "bufforing-stm/bufforing-stm/transaction.h"
#include "bufforing-stm/bufforing-stm/fullScan.h"
#include "bufforing-stm/bufforing-stm/queryExecutor.h"
#include "indexes/indexes/btree.h"
int main(void) {
    /*
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
    showBuffors(&buffors);
    addTuple(&buffors, &fsmCache, &fsmMapAll, &mvcc, 20,
         (AllVar[]){all_var_from_int32(1), all_var_from_string("Alice")},
         2,
         (int8_t[]){0, 0},
         2);
    showBuffors(&buffors);
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
    showBuffors(&buffors);
    Transaction *transaction = (Transaction *)malloc(sizeof(Transaction));

    beginTransaction(&mvcc, transaction);
    QueryExecutor qe ={0};
    int32_t cols[MAX_COLUMNS] = {0, 1};
    Qeselect(&qe, cols,2);
    Qefrom(&qe, 20);
    Qeend(&qe, &transaction, &fsmCache);


    ResultTuple result_tuple = {0};
    FullScan full_scan = {0};
    full_scan.rt = &result_tuple;
    full_scan.qe = &qe;
    showBuffors(&buffors);
    fullScan(&full_scan,&buffors);
    showBuffors(&buffors);
    printResultTuple(&result_tuple);
     //force_eviction(&buffors, &fsmCache, &fsmMapAll, 21);
     */

    Node* node;
    createNodeC(&node);

    /* seed random for quickSort pivot selection */
    srand((unsigned)time(NULL));

    /* insert many elements (ints) to exercise splitting and ordering */
    int32_t vals[] = {10, 2, 100, 14, 9, 17, 5, 9, 23, 1, 7, 8, 50, 45, 33,
                      3, 4, 6, 11, 12, 13, 99, -1, 0, 42, 27, 27, 18};
    int nvals = sizeof(vals) / sizeof(vals[0]);
    for (int i = 0; i < nvals; ++i) {
        if (i==6) {
            printf("\n");
        }
        type t;
        t.val = all_var_from_int32(vals[i]);
        t.blockId = i;
        addElement(node, t, 0, 0, all_var_from_int32(0));
        printBtree(node);
        printf("===============================================\n");
    }

    printBtree(node);

    printf("\nIn-order traversal (should be sorted):\n");
    //printBtreeInOrder(node);
    printf("\n\n");

    int64_t expected[] = {-1,0,1,2,3,4,5,6,7,8,9,9,10,11,12,13,14,17,18,23,27,27,33,42,45,50,99,100};
    int expected_count = sizeof(expected) / sizeof(expected[0]);
    int ok = verifyInOrder(node, expected, expected_count);
    printf("verifyInOrder returned: %d\n", ok);

    int32_t val = getBlock(node, all_var_from_int32(-1), 0, 0, all_var_from_int32(0));
    printf("getBlock returned: %d\n", val);
    return 0;
}
