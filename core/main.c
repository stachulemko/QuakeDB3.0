#include <stdio.h>
#include "bufforing-stm/bufforing-stm/dataBuffor.h"
#include "memory-mgmt/memory-mgmt/all_var.h"
#include "bufforing-stm/bufforing-stm/transaction.h"
#include "bufforing-stm/bufforing-stm/fullScan.h"
#include "bufforing-stm/bufforing-stm/queryExecutor.h"
#include "indexes/indexes/btree.h"
#include "indexes/indexes/btreeFileOperation.h"
int main(void) {

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
    //showBuffors(&buffors);
    addTuple(&buffors, &fsmCache, &fsmMapAll, &mvcc, 20,
         (AllVar[]){all_var_from_int32(1), all_var_from_string("Alice")},
         2,
         (int8_t[]){0, 0},
         2);
    //showBuffors(&buffors);
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
    //showBuffors(&buffors);
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
    //showBuffors(&buffors);
    fullScan(&full_scan,&buffors);
    showBuffors(&buffors);
    printResultTuple(&result_tuple);
     //force_eviction(&buffors, &fsmCache, &fsmMapAll, 21);
    addExistingValues();


}
