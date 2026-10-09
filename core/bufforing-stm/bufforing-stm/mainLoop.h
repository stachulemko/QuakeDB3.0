//
// Created by stas on 21.09.2026.
//

#ifndef QUAKEDB3_0_MAINLOOP_H
#define QUAKEDB3_0_MAINLOOP_H

#include "sqlExecutor.h"

/*
 * here in the main loop we will add cheking all the staying trnsaction if transaction they are blocked on is already commited or rollback or timeout
 *
 * Lost update handling:
 *   1. mainLoop_execute runs a query. An UPDATE that meets a row changed by a running transaction
 *      returns ACTION_WAIT and goes to `blocked` (TabOfBlockedTransaction).
 *   2. every mainLoop_tick resumes blocked queries whose blocking transaction ended:
 *        commit   — REPEATABLE READ: error, the transaction is aborted
 *                   READ COMMITTED: the query continues on the newest version of the row
 *        rollback — the query continues on the version it saw
 *      and aborts queries that waited longer than timeExceedTransaction seconds.
 */
typedef struct {
    Buffors    *buffors;
    FSMCache   *c;
    FSMMapAll  *fsmMapAll;
    MVCC       *mvcc;
    MVCCBuffor *mvccBuffor;
    TabOfBlockedTransaction blocked;
} MainLoop;

static inline void mainLoop_init(MainLoop *ml, Buffors *buffors, FSMCache *c, FSMMapAll *fsmMapAll,
                                 MVCC *mvcc, MVCCBuffor *mvccBuffor) {
    memset(ml, 0, sizeof(MainLoop));
    ml->buffors    = buffors;
    ml->c          = c;
    ml->fsmMapAll  = fsmMapAll;
    ml->mvcc       = mvcc;
    ml->mvccBuffor = mvccBuffor;
}

/* Handles the result of sql_fullScan: WAIT → blocked table, ERROR → abort the transaction */
static Action mainLoop_handle(MainLoop *ml, SqlExecutor *se, Action action) {
    if (action == ACTION_WAIT) {
        if (se->waitingSince == 0) se->waitingSince = nowTime();
        if (addToTabOfBlockedTransaction(se, &ml->blocked) == 0) return ACTION_WAIT;
        LOG_ERROR("too many blocked operations, transaction %d aborted\n", se->transaction->xid);
        se->action = action = ACTION_ERROR_CONCURRENT_UPDATE;
    }
    if (action == ACTION_ERROR_CONCURRENT_UPDATE) {
        abortTransaction(ml->mvcc, se->transaction);
    }
    se->waitingSince = 0;
    return action;
}

/* Runs a query. The SqlExecutor and the ResultTuple must live until it stops waiting. */
Action mainLoop_execute(MainLoop *ml, SqlExecutor *se, ResultTuple *result) {
    Action action = sql_fullScan(se, result, ml->buffors, ml->c, ml->fsmMapAll, ml->mvcc, ml->mvccBuffor);
    return mainLoop_handle(ml, se, action);
}

/* 1 when the operation waits longer than timeExceedTransaction seconds */
static inline int8_t mainLoop_waitTimedOut(SqlExecutor *se) {
    return se->waitingSince != 0 && nowTime() - se->waitingSince > (int64_t)timeExceedTransaction * 1000;
}

/* One iteration of the main loop. Returns the number of operations that are still blocked. */
int32_t mainLoop_tick(MainLoop *ml) {
    /* waits that took too long — abort, like lock_timeout in Postgres (also breaks deadlocks) */
    for (int i = 0; i < mvccBufforSize; i++) {
        SqlExecutor *se = ml->blocked.operation[i];
        if (se == NULL || !mainLoop_waitTimedOut(se)) continue;
        ml->blocked.operation[i] = NULL;
        LOG_ERROR("transaction %d aborted: waiting for transaction %d timed out\n",
                  se->transaction->xid, se->xidOfBlockingTransaction);
        se->action = ACTION_ERROR_CONCURRENT_UPDATE;
        mainLoop_handle(ml, se, se->action);
    }

    /* resume operations whose blocking transaction ended; an operation that hits another running
     * transaction goes back to the table with the new blocker, so each one runs at most once per tick */
    SqlExecutor *ready[mvccBufforSize];
    int32_t readyCount = 0;
    SqlExecutor *se;
    while ((se = checkIfTransactionBlocked(&ml->blocked, ml->mvcc, ml->mvccBuffor)) != NULL) {
        ready[readyCount++] = se;
    }
    for (int i = 0; i < readyCount; i++) {
        mainLoop_execute(ml, ready[i], ready[i]->result);
    }

    int32_t stillBlocked = 0;
    for (int i = 0; i < mvccBufforSize; i++) {
        if (ml->blocked.operation[i] != NULL) stillBlocked++;
    }
    return stillBlocked;
}

/* Runs while *running != 0 — new queries come through mainLoop_execute between ticks */
void mainLoop(MainLoop *ml, volatile int8_t *running) {
    while (*running) {
        mainLoop_tick(ml);
    }
}


#endif //QUAKEDB3_0_MAINLOOP_H
