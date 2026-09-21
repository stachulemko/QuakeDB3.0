#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "../../bufforing-stm/bufforing-stm/sqlExecutor.h"


typedef struct {
    Buffors       buffors;
    FSMCache      fsmCache;
    FSMMapAll     fsmMapAll;
    MVCC          mvcc;
    FSMMapBtree   fsmMapBtree;
    BtreeBuffors  btreeBuffors;
    Transaction   txn;
    SqlExecutor   se;
} SetedUpEnv;


typedef struct {
    int32_t step;
}Task;

int8_t checkingLostUpdateTask(Task *task,) {
    if (task->step == 0) {
        // perform first operation
        task->step++;
        return 1; // continue
    } else if (task->step == 1) {
        // perform second operation
        task->step++;
        return 0; // stop
    }
    return 0; // stop
}

/*
 *  we will be checking all anomalies which can occur in mvcc sytem at first we will be testing lost update .
*/

static void initWithoutBtree(SetedUpEnv *env, int32_t bufforCount, int32_t tableId, int32_t xid) {
    initializeBuffors(&env->buffors, bufforCount);
    fsm_cache_init(&env->fsmCache);
    init_FSMMapAll(&env->fsmMapAll);
    mvcc_init(&env->mvcc);

    env->txn = (Transaction){.xid = xid};

    memset(&env->se, 0, sizeof(SqlExecutor));
    env->se.transaction   = &env->txn;
    env->se.tableId       = tableId;
    env->se.fsmMapBtree   = NULL;
    env->se.btreeBuffors  = NULL;
}

static void initWithBtree(SetedUpEnv *env, int32_t bufforCount, int32_t tableId, int32_t xid, int32_t btreeBufforCount) {
    initWithoutBtree(env, bufforCount, tableId, xid);
    fsm_btree_init(&env->fsmMapBtree);
    initBtreeBuffors(&env->btreeBuffors, btreeBufforCount);

    env->se.fsmMapBtree  = &env->fsmMapBtree;
    env->se.btreeBuffors = &env->btreeBuffors;
}
//
void testLostUpdate(void **state) {


    int8_t still = true;
    while (still) {

    }

}

