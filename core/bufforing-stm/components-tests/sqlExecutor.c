//
// Created by stas on 21.09.2026.
//
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include "../../bufforing-stm/bufforing-stm/mainLoop.h"

/*
 *  we will be checking all anomalies which can occur in mvcc sytem at first we will be testing lost update .
 *
 *  Component tests: real buffers, blocks, version chains, MVCC statuses and the main loop.
 *  Table: (id INT, a INT, b INT). Built twice:
 *    test_lost_update    — VIEW_MODE=1 (Repeatable Read)
 *    test_lost_update_rc — VIEW_MODE=2 (Read Committed)
 */

#define LU_TABLE 7
#define COL_ID   0
#define COL_A    1
#define COL_B    2

typedef struct {
    Buffors     buffors;
    FSMCache   *c;
    FSMMapAll   fsmMapAll;
    MVCC        mvcc;
    MVCCBuffor  mvccBuffor;
    MainLoop    ml;
} SetedUpEnv;

/* one query: it must live until it stops waiting, like a client connection */
typedef struct {
    SqlExecutor se;
    ResultTuple result;
} Query;

static void env_setup(SetedUpEnv *env) {
    memset(env, 0, sizeof(SetedUpEnv));
    initializeBuffors(&env->buffors, 30);
    FSMCacheCreateC(&env->c);
    fsm_cache_set(env->c, LU_TABLE);
    init_FSMMapAll(&env->fsmMapAll);
    addTableToFSMMapAll(&env->fsmMapAll, LU_TABLE);
    mvcc_init(&env->mvcc);
    mvcc_buffor_init(&env->mvccBuffor);
    mainLoop_init(&env->ml, &env->buffors, env->c, &env->fsmMapAll, &env->mvcc, &env->mvccBuffor);
}

static void env_teardown(SetedUpEnv *env) {
    for (int i = 0; i < env->buffors.count; i++) {
        DataBuffor *b = &env->buffors.buffors[i];
        if (b->isUsed && b->universalBlock) {
            free(b->universalBlock->block);
            free(b->universalBlock->header);
            free(b->universalBlock);
        }
    }
    free(env->buffors.buffors);
    free(env->c);
}

static void env_begin(SetedUpEnv *env, Transaction *txn) {
    beginTransaction(&env->mvcc, txn);
    incrementTxnCounter(&env->mvcc);
}

static int8_t env_status(SetedUpEnv *env, Transaction *txn) {
    return mvcc_get_txn_status(&env->mvcc, &env->mvccBuffor, NULL, txn->xid);
}

static int32_t env_endblock(SetedUpEnv *env) {
    BlockCounterEntry *e = fsm_cache_get(env->c, LU_TABLE);
    return e ? e->maxBlock : 0;
}

/* rows 1..count with a = 0, b = 0, inserted and committed by one transaction */
static void env_load(SetedUpEnv *env, int32_t count) {
    Transaction loader;
    env_begin(env, &loader);
    for (int32_t id = 1; id <= count; id++) {
        AllVar vals[3] = {all_var_from_int32(id), all_var_from_int32(0), all_var_from_int32(0)};
        int8_t bm[3]   = {0, 0, 0};
        addTupleToOtherFunction(&env->buffors, env->c, &env->fsmMapAll, LU_TABLE, vals, 3, bm, 3,
                                loader.xid, 0, 0, 0, 0, 0, -1, NULL, NULL, NULL);
    }
    commitTransaction(&env->mvcc, &loader);
}

/* UPDATE SET col = val WHERE whereCol op whereVal */
static Action env_update(SetedUpEnv *env, Query *q, Transaction *txn, int32_t whereCol, int8_t op,
                         int32_t whereVal, int32_t col, int32_t val) {
    memset(q, 0, sizeof(Query));
    q->se.transaction = txn;
    q->se.tableId     = LU_TABLE;
    q->se.endBlock    = env_endblock(env);
    sql_addWhere(&q->se, whereCol, all_var_from_int32(whereVal), op, 1);
    int32_t cols[] = {col};
    AllVar  vals[] = {all_var_from_int32(val)};
    sql_setUpdate(&q->se, cols, vals, 1);
    return mainLoop_execute(&env->ml, &q->se, &q->result);
}

static Action env_updateById(SetedUpEnv *env, Query *q, Transaction *txn, int32_t id, int32_t col, int32_t val) {
    return env_update(env, q, txn, COL_ID, SQL_EQ, id, col, val);
}

/* SELECT * WHERE id = id by an already started transaction; returns 0 when not found */
static int8_t env_selectAs(SetedUpEnv *env, Transaction *txn, int32_t id, int32_t *a, int32_t *b, Action *action) {
    Query q;
    memset(&q, 0, sizeof(Query));
    q.se.transaction = txn;
    q.se.tableId     = LU_TABLE;
    q.se.endBlock    = env_endblock(env);
    sql_addWhere(&q.se, COL_ID, all_var_from_int32(id), SQL_EQ, 1);
    int32_t cols[] = {COL_ID, COL_A, COL_B};
    sql_setSelect(&q.se, cols, 3);
    Action act = mainLoop_execute(&env->ml, &q.se, &q.result);
    if (action != NULL) *action = act;
    int8_t found = q.result.tuple_count == 1;
    if (found) {
        *a = q.result.tuples[0]->dnb.data[COL_A].val.i32;
        *b = q.result.tuples[0]->dnb.data[COL_B].val.i32;
    }
    for (int i = 0; i < q.result.tuple_count; i++) free(q.result.tuples[i]);
    return found;
}

/* the newest committed state of a row, read by a new transaction */
static void env_latest(SetedUpEnv *env, int32_t id, int32_t *a, int32_t *b) {
    Transaction reader;
    env_begin(env, &reader);
    assert_true(env_selectAs(env, &reader, id, a, b, NULL));
    commitTransaction(&env->mvcc, &reader);
}

static void assert_row(SetedUpEnv *env, int32_t id, int32_t a, int32_t b) {
    int32_t gotA = -1, gotB = -1;
    env_latest(env, id, &gotA, &gotB);
    assert_int_equal(gotA, a);
    assert_int_equal(gotB, b);
}

static int32_t blockedCount(SetedUpEnv *env) {
    int32_t n = 0;
    for (int i = 0; i < mvccBufforSize; i++) {
        if (env->ml.blocked.operation[i] != NULL) n++;
    }
    return n;
}

static int8_t isBlocked(SetedUpEnv *env, SqlExecutor *se) {
    for (int i = 0; i < mvccBufforSize; i++) {
        if (env->ml.blocked.operation[i] == se) return 1;
    }
    return 0;
}

/* =========================================================================
 * Cases that are the same in both isolation levels
 * ========================================================================= */

/* no concurrency: the second transaction starts after the first committed */
static void test_sequentialUpdates_noWait(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 1);

    Transaction a, b;
    Query qa, qb;
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &qa, &a, 1, COL_A, 100), ACTION_PROCEED);
    commitTransaction(&env.mvcc, &a);

    env_begin(&env, &b);
    assert_int_equal(env_updateById(&env, &qb, &b, 1, COL_B, 200), ACTION_PROCEED);
    commitTransaction(&env.mvcc, &b);

    assert_int_equal(blockedCount(&env), 0);
    assert_row(&env, 1, 100, 200);
    env_teardown(&env);
}

/* the same transaction updates its own new version — no waiting on itself */
static void test_sameTransactionTwice_noWait(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 1);

    Transaction a;
    Query q1, q2;
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &q1, &a, 1, COL_A, 100), ACTION_PROCEED);
    assert_int_equal(env_updateById(&env, &q2, &a, 1, COL_B, 200), ACTION_PROCEED);
    assert_int_equal(blockedCount(&env), 0);
    commitTransaction(&env.mvcc, &a);

    assert_row(&env, 1, 100, 200);
    env_teardown(&env);
}

/* a running transaction blocks only the rows it changed */
static void test_otherRow_noWait(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 2);

    Transaction b, a;
    Query qa, qb;
    env_begin(&env, &b);
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &qa, &a, 1, COL_A, 100), ACTION_PROCEED);
    assert_int_equal(env_updateById(&env, &qb, &b, 2, COL_B, 200), ACTION_PROCEED);
    assert_int_equal(blockedCount(&env), 0);
    commitTransaction(&env.mvcc, &a);
    commitTransaction(&env.mvcc, &b);

    assert_row(&env, 1, 100, 0);
    assert_row(&env, 2, 0, 200);
    env_teardown(&env);
}

/* readers never wait, even on a row locked by a running UPDATE */
static void test_selectNeverWaits(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 1);

    Transaction b, a;
    Query qa;
    env_begin(&env, &b);
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &qa, &a, 1, COL_A, 100), ACTION_PROCEED);

    int32_t va = -1, vb = -1;
    Action act;
    assert_true(env_selectAs(&env, &b, 1, &va, &vb, &act));
    assert_int_equal(act, ACTION_PROCEED);
    assert_int_equal(va, 0);   /* b started before a — sees the old version */
    assert_int_equal(blockedCount(&env), 0);
    commitTransaction(&env.mvcc, &a);
    commitTransaction(&env.mvcc, &b);
    env_teardown(&env);
}

/* the waiting UPDATE saves where it stopped and stays blocked while the other transaction runs */
static void test_wait_savesPositionAndStaysBlocked(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 3);

    Transaction b, a;
    Query qa, qb;
    env_begin(&env, &b);
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &qa, &a, 2, COL_A, 100), ACTION_PROCEED);

    assert_int_equal(env_updateById(&env, &qb, &b, 2, COL_B, 200), ACTION_WAIT);
    assert_int_equal(qb.se.action, ACTION_WAIT);
    assert_int_equal(qb.se.xidOfBlockingTransaction, a.xid);
    assert_int_equal(qb.se.blockIdOfLastTransaction, 1);
    assert_int_equal(qb.se.posInBlock, 1);           /* row id = 2 is the second slot */
    assert_ptr_equal(qb.se.result, &qb.result);
    assert_true(qb.se.waitingSince != 0);
    assert_true(isBlocked(&env, &qb.se));

    for (int i = 0; i < 3; i++) assert_int_equal(mainLoop_tick(&env.ml), 1);
    assert_int_equal(env_status(&env, &b), TXN_ACTIVE);
    assert_int_equal(qb.se.action, ACTION_WAIT);

    abortTransaction(&env.mvcc, &a);
    assert_int_equal(mainLoop_tick(&env.ml), 0);
    assert_int_equal(qb.se.action, ACTION_PROCEED);
    assert_int_equal(qb.se.blockIdOfLastTransaction, -1);
    assert_int_equal(qb.se.posInBlock, -1);
    assert_int_equal(qb.se.waitingSince, 0);
    env_teardown(&env);
}

/* the rollback of the blocking transaction lets the waiting one update the version it saw */
static void test_blockerAborts_waiterProceeds(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 1);

    Transaction b, a;
    Query qa, qb;
    env_begin(&env, &b);
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &qa, &a, 1, COL_A, 100), ACTION_PROCEED);
    assert_int_equal(env_updateById(&env, &qb, &b, 1, COL_B, 200), ACTION_WAIT);

    abortTransaction(&env.mvcc, &a);
    assert_int_equal(mainLoop_tick(&env.ml), 0);
    assert_int_equal(qb.se.action, ACTION_PROCEED);
    assert_int_equal(qb.result.tuple_count, 1);
    assert_int_equal(env_status(&env, &b), TXN_ACTIVE);
    commitTransaction(&env.mvcc, &b);

    assert_row(&env, 1, 0, 200);   /* a's change was rolled back */
    env_teardown(&env);
}

/* the scan stops in the middle and resumes from the saved row: every row is updated exactly once */
static void test_waitInMiddle_resumesWithoutDoubleUpdate(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 3);

    Transaction b, a;
    Query qa, qb;
    env_begin(&env, &b);
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &qa, &a, 2, COL_A, 100), ACTION_PROCEED);

    /* UPDATE ... SET b = 200 WHERE id >= 1 — all rows */
    assert_int_equal(env_update(&env, &qb, &b, COL_ID, SQL_GE, 1, COL_B, 200), ACTION_WAIT);
    assert_int_equal(qb.result.tuple_count, 1);      /* row 1 done before the wait */
    assert_int_equal(qb.se.posInBlock, 1);

    abortTransaction(&env.mvcc, &a);
    assert_int_equal(mainLoop_tick(&env.ml), 0);
    assert_int_equal(qb.se.action, ACTION_PROCEED);
    assert_int_equal(qb.result.tuple_count, 3);      /* rows 2 and 3 added, row 1 not again */
    commitTransaction(&env.mvcc, &b);

    assert_row(&env, 1, 0, 200);
    assert_row(&env, 2, 0, 200);
    assert_row(&env, 3, 0, 200);
    env_teardown(&env);
}

/* a waiting operation that exceeds timeExceedTransaction is aborted; the blocker keeps working */
static void test_waitTimeout_abortsWaiter(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 1);

    Transaction b, a;
    Query qa, qb;
    env_begin(&env, &b);
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &qa, &a, 1, COL_A, 100), ACTION_PROCEED);
    assert_int_equal(env_updateById(&env, &qb, &b, 1, COL_B, 200), ACTION_WAIT);

    qb.se.waitingSince = nowTime() - ((int64_t)timeExceedTransaction * 1000 + 1);
    assert_int_equal(mainLoop_tick(&env.ml), 0);
    assert_int_equal(qb.se.action, ACTION_ERROR_CONCURRENT_UPDATE);
    assert_int_equal(env_status(&env, &b), TXN_ABORTED);
    assert_int_equal(env_status(&env, &a), TXN_ACTIVE);

    commitTransaction(&env.mvcc, &a);
    assert_int_equal(mainLoop_tick(&env.ml), 0);
    assert_row(&env, 1, 100, 0);
    env_teardown(&env);
}

#if VIEW_MODE == 1
/* =========================================================================
 * REPEATABLE READ — a concurrent committed change is a serialization error
 * ========================================================================= */

/* lost update: b (older) waits for a; a commits → b is aborted, a's change is kept */
static void test_rr_olderWaits_blockerCommits_error(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 1);

    Transaction b, a;
    Query qa, qb;
    env_begin(&env, &b);
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &qa, &a, 1, COL_A, 100), ACTION_PROCEED);
    assert_int_equal(env_updateById(&env, &qb, &b, 1, COL_B, 200), ACTION_WAIT);
    assert_int_equal(qb.se.xidOfBlockingTransaction, a.xid);

    commitTransaction(&env.mvcc, &a);
    assert_int_equal(mainLoop_tick(&env.ml), 0);
    assert_int_equal(qb.se.action, ACTION_ERROR_CONCURRENT_UPDATE);
    assert_int_equal(qb.result.tuple_count, 0);
    assert_int_equal(env_status(&env, &b), TXN_ABORTED);
    assert_false(isBlocked(&env, &qb.se));

    assert_row(&env, 1, 100, 0);   /* a's update is not lost, b changed nothing */
    env_teardown(&env);
}

/* b (newer) meets a's uncommitted version (xmin running) → waits; a commits → error */
static void test_rr_newerWaitsOnUncommittedVersion_error(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 1);

    Transaction a, b;
    Query qa, qb;
    env_begin(&env, &a);
    env_begin(&env, &b);
    assert_int_equal(env_updateById(&env, &qa, &a, 1, COL_A, 100), ACTION_PROCEED);
    assert_int_equal(env_updateById(&env, &qb, &b, 1, COL_B, 200), ACTION_WAIT);
    assert_int_equal(qb.se.xidOfBlockingTransaction, a.xid);

    commitTransaction(&env.mvcc, &a);
    assert_int_equal(mainLoop_tick(&env.ml), 0);
    assert_int_equal(qb.se.action, ACTION_ERROR_CONCURRENT_UPDATE);
    assert_int_equal(env_status(&env, &b), TXN_ABORTED);

    assert_row(&env, 1, 100, 0);
    env_teardown(&env);
}

/* the row was changed and committed by a newer transaction before b even looked at it */
static void test_rr_alreadyCommittedByNewer_errorImmediately(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 1);

    Transaction b, a;
    Query qa, qb;
    env_begin(&env, &b);
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &qa, &a, 1, COL_A, 100), ACTION_PROCEED);
    commitTransaction(&env.mvcc, &a);

    assert_int_equal(env_updateById(&env, &qb, &b, 1, COL_B, 200), ACTION_ERROR_CONCURRENT_UPDATE);
    assert_int_equal(qb.se.xidOfBlockingTransaction, a.xid);
    assert_int_equal(env_status(&env, &b), TXN_ABORTED);
    assert_int_equal(blockedCount(&env), 0);

    assert_row(&env, 1, 100, 0);
    env_teardown(&env);
}

/* two transactions wait for the same one; its commit aborts both */
static void test_rr_twoWaiters_blockerCommits_bothError(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 1);

    Transaction b, c, a;
    Query qa, qb, qc;
    env_begin(&env, &b);
    env_begin(&env, &c);
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &qa, &a, 1, COL_A, 100), ACTION_PROCEED);
    assert_int_equal(env_updateById(&env, &qb, &b, 1, COL_B, 200), ACTION_WAIT);
    assert_int_equal(env_updateById(&env, &qc, &c, 1, COL_B, 300), ACTION_WAIT);
    assert_int_equal(blockedCount(&env), 2);

    commitTransaction(&env.mvcc, &a);
    assert_int_equal(mainLoop_tick(&env.ml), 0);
    assert_int_equal(qb.se.action, ACTION_ERROR_CONCURRENT_UPDATE);
    assert_int_equal(qc.se.action, ACTION_ERROR_CONCURRENT_UPDATE);
    assert_int_equal(env_status(&env, &b), TXN_ABORTED);
    assert_int_equal(env_status(&env, &c), TXN_ABORTED);

    assert_row(&env, 1, 100, 0);
    env_teardown(&env);
}

/* rows changed before the wait stay changed, but the transaction is aborted on the conflict */
static void test_rr_errorInMiddle_stopsScan(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 3);

    Transaction b, a;
    Query qa, qb;
    env_begin(&env, &b);
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &qa, &a, 2, COL_A, 100), ACTION_PROCEED);
    assert_int_equal(env_update(&env, &qb, &b, COL_ID, SQL_GE, 1, COL_B, 200), ACTION_WAIT);

    commitTransaction(&env.mvcc, &a);
    assert_int_equal(mainLoop_tick(&env.ml), 0);
    assert_int_equal(qb.se.action, ACTION_ERROR_CONCURRENT_UPDATE);
    assert_int_equal(qb.result.tuple_count, 1);      /* row 3 was never reached */
    assert_int_equal(qb.se.posInBlock, 1);
    assert_int_equal(env_status(&env, &b), TXN_ABORTED);
    env_teardown(&env);
}

#else
/* =========================================================================
 * READ COMMITTED — after the commit the update continues on the newest version
 * ========================================================================= */

/* b (older) waits for a; a commits → b updates a's version: both changes survive */
static void test_rc_olderWaits_blockerCommits_reEvaluates(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 1);

    Transaction b, a;
    Query qa, qb;
    env_begin(&env, &b);
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &qa, &a, 1, COL_A, 100), ACTION_PROCEED);
    assert_int_equal(env_updateById(&env, &qb, &b, 1, COL_B, 200), ACTION_WAIT);
    assert_int_equal(qb.se.xidOfBlockingTransaction, a.xid);
    assert_int_equal(mainLoop_tick(&env.ml), 1);

    commitTransaction(&env.mvcc, &a);
    assert_int_equal(mainLoop_tick(&env.ml), 0);
    assert_int_equal(qb.se.action, ACTION_PROCEED);
    assert_int_equal(qb.result.tuple_count, 1);
    assert_int_equal(env_status(&env, &b), TXN_ACTIVE);
    commitTransaction(&env.mvcc, &b);

    assert_row(&env, 1, 100, 200);   /* no lost update: a = 100 from a, b = 200 from b */
    env_teardown(&env);
}

/* b (newer) meets a's uncommitted version → waits; a commits → b updates it */
static void test_rc_newerWaitsOnUncommittedVersion_proceeds(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 1);

    Transaction a, b;
    Query qa, qb;
    env_begin(&env, &a);
    env_begin(&env, &b);
    assert_int_equal(env_updateById(&env, &qa, &a, 1, COL_A, 100), ACTION_PROCEED);
    assert_int_equal(env_updateById(&env, &qb, &b, 1, COL_B, 200), ACTION_WAIT);
    assert_int_equal(qb.se.xidOfBlockingTransaction, a.xid);

    commitTransaction(&env.mvcc, &a);
    assert_int_equal(mainLoop_tick(&env.ml), 0);
    assert_int_equal(qb.se.action, ACTION_PROCEED);
    commitTransaction(&env.mvcc, &b);

    assert_row(&env, 1, 100, 200);
    env_teardown(&env);
}

/* the row was changed and committed by a newer transaction before b looked at it — no wait */
static void test_rc_alreadyCommittedByNewer_reEvaluatesImmediately(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 1);

    Transaction b, a;
    Query qa, qb;
    env_begin(&env, &b);
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &qa, &a, 1, COL_A, 100), ACTION_PROCEED);
    commitTransaction(&env.mvcc, &a);

    assert_int_equal(env_updateById(&env, &qb, &b, 1, COL_B, 200), ACTION_PROCEED);
    assert_int_equal(blockedCount(&env), 0);
    commitTransaction(&env.mvcc, &b);

    assert_row(&env, 1, 100, 200);
    env_teardown(&env);
}

/* after the commit the new version no longer matches WHERE — b skips the row */
static void test_rc_reEvaluate_whereNoLongerMatches(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 1);

    Transaction b, a;
    Query qa, qb;
    env_begin(&env, &b);
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &qa, &a, 1, COL_A, 100), ACTION_PROCEED);
    /* UPDATE ... SET b = 200 WHERE a = 0 */
    assert_int_equal(env_update(&env, &qb, &b, COL_A, SQL_EQ, 0, COL_B, 200), ACTION_WAIT);

    commitTransaction(&env.mvcc, &a);
    assert_int_equal(mainLoop_tick(&env.ml), 0);
    assert_int_equal(qb.se.action, ACTION_PROCEED);
    assert_int_equal(qb.result.tuple_count, 0);
    commitTransaction(&env.mvcc, &b);

    assert_row(&env, 1, 100, 0);
    env_teardown(&env);
}

/* two transactions wait for the same one; after its commit both apply their change in turn */
static void test_rc_twoWaiters_blockerCommits_allChangesKept(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 1);

    Transaction b, c, a;
    Query qa, qb, qc;
    env_begin(&env, &b);
    env_begin(&env, &c);
    env_begin(&env, &a);
    assert_int_equal(env_updateById(&env, &qa, &a, 1, COL_A, 100), ACTION_PROCEED);
    assert_int_equal(env_updateById(&env, &qb, &b, 1, COL_B, 200), ACTION_WAIT);
    assert_int_equal(env_updateById(&env, &qc, &c, 1, COL_A, 300), ACTION_WAIT);

    commitTransaction(&env.mvcc, &a);
    /* b resumes first and updates a's version; c now waits for b */
    assert_int_equal(mainLoop_tick(&env.ml), 1);
    assert_int_equal(qb.se.action, ACTION_PROCEED);
    assert_int_equal(qc.se.action, ACTION_WAIT);
    assert_int_equal(qc.se.xidOfBlockingTransaction, b.xid);

    commitTransaction(&env.mvcc, &b);
    assert_int_equal(mainLoop_tick(&env.ml), 0);
    assert_int_equal(qc.se.action, ACTION_PROCEED);
    commitTransaction(&env.mvcc, &c);

    assert_row(&env, 1, 300, 200);
    env_teardown(&env);
}

/* after the first wait the scan hits a row of another running transaction and waits again */
static void test_rc_chainedWait_newBlocker(void **state) {
    (void)state;
    SetedUpEnv env;
    env_setup(&env);
    env_load(&env, 2);

    Transaction b, a, c;
    Query qa, qb, qc;
    env_begin(&env, &b);
    env_begin(&env, &a);
    env_begin(&env, &c);
    assert_int_equal(env_updateById(&env, &qa, &a, 1, COL_A, 100), ACTION_PROCEED);
    assert_int_equal(env_updateById(&env, &qc, &c, 2, COL_A, 300), ACTION_PROCEED);

    assert_int_equal(env_update(&env, &qb, &b, COL_ID, SQL_GE, 1, COL_B, 200), ACTION_WAIT);
    assert_int_equal(qb.se.xidOfBlockingTransaction, a.xid);
    assert_int_equal(qb.se.posInBlock, 0);

    commitTransaction(&env.mvcc, &a);
    assert_int_equal(mainLoop_tick(&env.ml), 1);
    assert_int_equal(qb.se.action, ACTION_WAIT);
    assert_int_equal(qb.se.xidOfBlockingTransaction, c.xid);
    assert_int_equal(qb.se.posInBlock, 1);
    assert_int_equal(qb.result.tuple_count, 1);

    commitTransaction(&env.mvcc, &c);
    assert_int_equal(mainLoop_tick(&env.ml), 0);
    assert_int_equal(qb.se.action, ACTION_PROCEED);
    assert_int_equal(qb.result.tuple_count, 2);
    commitTransaction(&env.mvcc, &b);

    assert_row(&env, 1, 100, 200);
    assert_row(&env, 2, 300, 200);
    env_teardown(&env);
}
#endif

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sequentialUpdates_noWait),
        cmocka_unit_test(test_sameTransactionTwice_noWait),
        cmocka_unit_test(test_otherRow_noWait),
        cmocka_unit_test(test_selectNeverWaits),
        cmocka_unit_test(test_wait_savesPositionAndStaysBlocked),
        cmocka_unit_test(test_blockerAborts_waiterProceeds),
        cmocka_unit_test(test_waitInMiddle_resumesWithoutDoubleUpdate),
        cmocka_unit_test(test_waitTimeout_abortsWaiter),
#if VIEW_MODE == 1
        cmocka_unit_test(test_rr_olderWaits_blockerCommits_error),
        cmocka_unit_test(test_rr_newerWaitsOnUncommittedVersion_error),
        cmocka_unit_test(test_rr_alreadyCommittedByNewer_errorImmediately),
        cmocka_unit_test(test_rr_twoWaiters_blockerCommits_bothError),
        cmocka_unit_test(test_rr_errorInMiddle_stopsScan),
#else
        cmocka_unit_test(test_rc_olderWaits_blockerCommits_reEvaluates),
        cmocka_unit_test(test_rc_newerWaitsOnUncommittedVersion_proceeds),
        cmocka_unit_test(test_rc_alreadyCommittedByNewer_reEvaluatesImmediately),
        cmocka_unit_test(test_rc_reEvaluate_whereNoLongerMatches),
        cmocka_unit_test(test_rc_twoWaiters_blockerCommits_allChangesKept),
        cmocka_unit_test(test_rc_chainedWait_newBlocker),
#endif
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
