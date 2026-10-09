#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "../bufforing-stm/mainLoop.h"

/*
 * Unit tests for lost update handling:
 *   check_update_conflict, sql_checkTupleConflict, TabOfBlockedTransaction,
 *   checkIfTransactionBlocked, mainLoop_handle / mainLoop_tick (without a real scan).
 * Built twice: test_lostUpdate (VIEW_MODE=1, Repeatable Read) and test_lostUpdate_rc (VIEW_MODE=2).
 */

#define OWN_XID       5
#define ACTIVE_XID    3
#define COMMITED_XID  2
#define ABORTED_XID   1
#define UNKNOWN_XID   50   /* >= txn_counter, no status file */

static MVCC       mvcc;
static MVCCBuffor mvccBuffor;

static int setup(void **state) {
    (void)state;
    mvcc_init(&mvcc);
    mvcc_buffor_init(&mvccBuffor);
    mvcc.txn_counter = 10;
    for (int i = 0; i < 10; i++) mvcc.txn_status[i] = TXN_COMMITED;
    mvcc.txn_status[ACTIVE_XID]  = TXN_ACTIVE;
    mvcc.txn_status[OWN_XID]     = TXN_ACTIVE;
    mvcc.txn_status[ABORTED_XID] = TXN_ABORTED;
    return 0;
}

static Tuple tupleWith(int32_t xmin, int32_t xmax) {
    Tuple t;
    memset(&t, 0, sizeof(Tuple));
    t.header.t_xmin = xmin;
    t.header.t_xmax = xmax;
    return t;
}

/* =========================================================================
 * 1. check_update_conflict — decision based on xmax
 * ========================================================================= */

static void test_conflict_xmaxNone_proceeds(void **state) {
    (void)state;
    assert_int_equal(check_update_conflict(0, OWN_XID, &mvcc, &mvccBuffor), ACTION_PROCEED);
    assert_int_equal(check_update_conflict(-1, OWN_XID, &mvcc, &mvccBuffor), ACTION_PROCEED);
}

static void test_conflict_ownXmax_proceeds(void **state) {
    (void)state;
    assert_int_equal(check_update_conflict(OWN_XID, OWN_XID, &mvcc, &mvccBuffor), ACTION_PROCEED);
}

static void test_conflict_activeXmax_waits(void **state) {
    (void)state;
    /* both isolation levels wait for a running transaction */
    assert_int_equal(check_update_conflict(ACTIVE_XID, OWN_XID, &mvcc, &mvccBuffor), ACTION_WAIT);
}

static void test_conflict_commitedXmax(void **state) {
    (void)state;
#if VIEW_MODE == 1
    /* RR: somebody committed a change of our row — serialization error */
    assert_int_equal(check_update_conflict(COMMITED_XID, OWN_XID, &mvcc, &mvccBuffor),
                     ACTION_ERROR_CONCURRENT_UPDATE);
#else
    /* RC: continue on the newest version */
    assert_int_equal(check_update_conflict(COMMITED_XID, OWN_XID, &mvcc, &mvccBuffor),
                     ACTION_RE_EVALUATE_NEW_VERSION);
#endif
}

static void test_conflict_abortedXmax_proceeds(void **state) {
    (void)state;
    assert_int_equal(check_update_conflict(ABORTED_XID, OWN_XID, &mvcc, &mvccBuffor), ACTION_PROCEED);
}

static void test_conflict_unknownXmax_proceeds(void **state) {
    (void)state;
    assert_int_equal(check_update_conflict(UNKNOWN_XID, OWN_XID, &mvcc, &mvccBuffor), ACTION_PROCEED);
}

static void test_conflict_nullMvcc_proceeds(void **state) {
    (void)state;
    assert_int_equal(check_update_conflict(ACTIVE_XID, OWN_XID, NULL, NULL), ACTION_PROCEED);
}

static void test_conflict_nullMvccBuffor_noCrash(void **state) {
    (void)state;
    /* xid outside the in-memory window with a file path set — must not touch a NULL buffer */
    mvcc.filepath = "/nonexistent/mvcc.bin";
    assert_int_equal(check_update_conflict(UNKNOWN_XID, OWN_XID, &mvcc, NULL), ACTION_PROCEED);
    mvcc.filepath = NULL;
}

/* =========================================================================
 * 2. sql_checkTupleConflict — xmax first, then an uncommitted xmin
 * ========================================================================= */

static void test_tuple_free_proceeds(void **state) {
    (void)state;
    Tuple t = tupleWith(COMMITED_XID, 0);
    int32_t blocker = -7;
    assert_int_equal(sql_checkTupleConflict(&t, OWN_XID, &mvcc, &mvccBuffor, &blocker), ACTION_PROCEED);
    assert_int_equal(blocker, -7);
}

static void test_tuple_activeXmax_waitsOnXmax(void **state) {
    (void)state;
    Tuple t = tupleWith(COMMITED_XID, ACTIVE_XID);
    int32_t blocker = -1;
    assert_int_equal(sql_checkTupleConflict(&t, OWN_XID, &mvcc, &mvccBuffor, &blocker), ACTION_WAIT);
    assert_int_equal(blocker, ACTIVE_XID);
}

static void test_tuple_activeXmin_waitsOnXmin(void **state) {
    (void)state;
    /* version created by a running transaction (its UPDATE is not committed yet) */
    Tuple t = tupleWith(ACTIVE_XID, 0);
    int32_t blocker = -1;
    assert_int_equal(sql_checkTupleConflict(&t, OWN_XID, &mvcc, &mvccBuffor, &blocker), ACTION_WAIT);
    assert_int_equal(blocker, ACTIVE_XID);
}

static void test_tuple_ownXmin_proceeds(void **state) {
    (void)state;
    /* our own new version — second UPDATE in the same transaction */
    Tuple t = tupleWith(OWN_XID, 0);
    int32_t blocker = -1;
    assert_int_equal(sql_checkTupleConflict(&t, OWN_XID, &mvcc, &mvccBuffor, &blocker), ACTION_PROCEED);
}

static void test_tuple_abortedXmin_proceeds(void **state) {
    (void)state;
    Tuple t = tupleWith(ABORTED_XID, 0);
    int32_t blocker = -1;
    assert_int_equal(sql_checkTupleConflict(&t, OWN_XID, &mvcc, &mvccBuffor, &blocker), ACTION_PROCEED);
}

static void test_tuple_xmaxHasPriority(void **state) {
    (void)state;
    /* xmin running, xmax committed — the decision comes from xmax */
    Tuple t = tupleWith(ACTIVE_XID, COMMITED_XID);
    int32_t blocker = -1;
    Action a = sql_checkTupleConflict(&t, OWN_XID, &mvcc, &mvccBuffor, &blocker);
#if VIEW_MODE == 1
    assert_int_equal(a, ACTION_ERROR_CONCURRENT_UPDATE);
#else
    assert_int_equal(a, ACTION_RE_EVALUATE_NEW_VERSION);
#endif
    assert_int_equal(blocker, COMMITED_XID);
}

static void test_tuple_ownXmax_activeXmin_waits(void **state) {
    (void)state;
    /* xmax is ours, but the version itself comes from a running transaction */
    Tuple t = tupleWith(ACTIVE_XID, OWN_XID);
    int32_t blocker = -1;
    assert_int_equal(sql_checkTupleConflict(&t, OWN_XID, &mvcc, &mvccBuffor, &blocker), ACTION_WAIT);
    assert_int_equal(blocker, ACTIVE_XID);
}

/* =========================================================================
 * 3. TabOfBlockedTransaction / checkIfTransactionBlocked
 * ========================================================================= */

static SqlExecutor waiting(int32_t blockingXid) {
    SqlExecutor se;
    memset(&se, 0, sizeof(SqlExecutor));
    se.action = ACTION_WAIT;
    se.xidOfBlockingTransaction = blockingXid;
    return se;
}

static void test_tab_addUntilFull(void **state) {
    (void)state;
    TabOfBlockedTransaction tab = {0};
    SqlExecutor se[mvccBufforSize + 1];
    for (int i = 0; i < mvccBufforSize; i++) {
        assert_int_equal(addToTabOfBlockedTransaction(&se[i], &tab), 0);
        assert_ptr_equal(tab.operation[i], &se[i]);
    }
    assert_int_equal(addToTabOfBlockedTransaction(&se[mvccBufforSize], &tab), -1);
}

static void test_tab_reusesFreedSlot(void **state) {
    (void)state;
    TabOfBlockedTransaction tab = {0};
    SqlExecutor a = waiting(COMMITED_XID), b = waiting(ACTIVE_XID), c = waiting(ACTIVE_XID);
    addToTabOfBlockedTransaction(&a, &tab);
    addToTabOfBlockedTransaction(&b, &tab);
    assert_ptr_equal(checkIfTransactionBlocked(&tab, &mvcc, &mvccBuffor), &a);
    assert_int_equal(addToTabOfBlockedTransaction(&c, &tab), 0);
    assert_ptr_equal(tab.operation[0], &c);
}

static void test_blocked_emptyTable_null(void **state) {
    (void)state;
    TabOfBlockedTransaction tab = {0};
    assert_null(checkIfTransactionBlocked(&tab, &mvcc, &mvccBuffor));
}

static void test_blocked_stillActive_staysInTable(void **state) {
    (void)state;
    TabOfBlockedTransaction tab = {0};
    SqlExecutor se = waiting(ACTIVE_XID);
    addToTabOfBlockedTransaction(&se, &tab);
    assert_null(checkIfTransactionBlocked(&tab, &mvcc, &mvccBuffor));
    assert_ptr_equal(tab.operation[0], &se);
}

static void test_blocked_commited_returnedAndRemoved(void **state) {
    (void)state;
    TabOfBlockedTransaction tab = {0};
    SqlExecutor se = waiting(ACTIVE_XID);
    addToTabOfBlockedTransaction(&se, &tab);
    mvcc.txn_status[ACTIVE_XID] = TXN_COMMITED;
    assert_ptr_equal(checkIfTransactionBlocked(&tab, &mvcc, &mvccBuffor), &se);
    assert_null(tab.operation[0]);
    assert_null(checkIfTransactionBlocked(&tab, &mvcc, &mvccBuffor));
}

static void test_blocked_aborted_returned(void **state) {
    (void)state;
    TabOfBlockedTransaction tab = {0};
    SqlExecutor se = waiting(ACTIVE_XID);
    addToTabOfBlockedTransaction(&se, &tab);
    mvcc.txn_status[ACTIVE_XID] = TXN_ABORTED;
    assert_ptr_equal(checkIfTransactionBlocked(&tab, &mvcc, &mvccBuffor), &se);
}

static void test_blocked_unknownStatus_returned(void **state) {
    (void)state;
    /* status cannot be read — waiting would never end */
    TabOfBlockedTransaction tab = {0};
    SqlExecutor se = waiting(UNKNOWN_XID);
    addToTabOfBlockedTransaction(&se, &tab);
    assert_ptr_equal(checkIfTransactionBlocked(&tab, &mvcc, &mvccBuffor), &se);
}

static void test_blocked_notWaiting_dropped(void **state) {
    (void)state;
    TabOfBlockedTransaction tab = {0};
    SqlExecutor done = waiting(ACTIVE_XID);
    done.action = ACTION_PROCEED;
    addToTabOfBlockedTransaction(&done, &tab);
    assert_null(checkIfTransactionBlocked(&tab, &mvcc, &mvccBuffor));
    assert_null(tab.operation[0]);
}

static void test_blocked_onlyReadyOnesReturned(void **state) {
    (void)state;
    TabOfBlockedTransaction tab = {0};
    SqlExecutor w1 = waiting(ACTIVE_XID), r1 = waiting(COMMITED_XID),
                w2 = waiting(OWN_XID),    r2 = waiting(ABORTED_XID);
    addToTabOfBlockedTransaction(&w1, &tab);
    addToTabOfBlockedTransaction(&r1, &tab);
    addToTabOfBlockedTransaction(&w2, &tab);
    addToTabOfBlockedTransaction(&r2, &tab);
    assert_ptr_equal(checkIfTransactionBlocked(&tab, &mvcc, &mvccBuffor), &r1);
    assert_ptr_equal(checkIfTransactionBlocked(&tab, &mvcc, &mvccBuffor), &r2);
    assert_null(checkIfTransactionBlocked(&tab, &mvcc, &mvccBuffor));
    assert_ptr_equal(tab.operation[0], &w1);
    assert_ptr_equal(tab.operation[2], &w2);
}

/* =========================================================================
 * 4. Main loop bookkeeping (no scan)
 * ========================================================================= */

static void test_mainLoop_waitGoesToTable(void **state) {
    (void)state;
    MainLoop ml;
    mainLoop_init(&ml, NULL, NULL, NULL, &mvcc, &mvccBuffor);
    Transaction txn = {.xid = OWN_XID};
    SqlExecutor se = waiting(ACTIVE_XID);
    se.transaction = &txn;
    assert_int_equal(mainLoop_handle(&ml, &se, ACTION_WAIT), ACTION_WAIT);
    assert_ptr_equal(ml.blocked.operation[0], &se);
    assert_true(se.waitingSince != 0);
    assert_int_equal(mvcc.txn_status[OWN_XID], TXN_ACTIVE);
}

static void test_mainLoop_errorAbortsTransaction(void **state) {
    (void)state;
    MainLoop ml;
    mainLoop_init(&ml, NULL, NULL, NULL, &mvcc, &mvccBuffor);
    Transaction txn = {.xid = OWN_XID};
    SqlExecutor se = waiting(COMMITED_XID);
    se.transaction = &txn;
    se.action = ACTION_ERROR_CONCURRENT_UPDATE;
    assert_int_equal(mainLoop_handle(&ml, &se, ACTION_ERROR_CONCURRENT_UPDATE), ACTION_ERROR_CONCURRENT_UPDATE);
    assert_int_equal(mvcc.txn_status[OWN_XID], TXN_ABORTED);
    assert_null(ml.blocked.operation[0]);
}

static void test_mainLoop_fullTableAborts(void **state) {
    (void)state;
    MainLoop ml;
    mainLoop_init(&ml, NULL, NULL, NULL, &mvcc, &mvccBuffor);
    SqlExecutor others[mvccBufforSize];
    for (int i = 0; i < mvccBufforSize; i++) {
        others[i] = waiting(ACTIVE_XID);
        addToTabOfBlockedTransaction(&others[i], &ml.blocked);
    }
    Transaction txn = {.xid = OWN_XID};
    SqlExecutor se = waiting(ACTIVE_XID);
    se.transaction = &txn;
    assert_int_equal(mainLoop_handle(&ml, &se, ACTION_WAIT), ACTION_ERROR_CONCURRENT_UPDATE);
    assert_int_equal(se.action, ACTION_ERROR_CONCURRENT_UPDATE);
    assert_int_equal(mvcc.txn_status[OWN_XID], TXN_ABORTED);
}

static void test_mainLoop_tick_noBlocked(void **state) {
    (void)state;
    MainLoop ml;
    mainLoop_init(&ml, NULL, NULL, NULL, &mvcc, &mvccBuffor);
    assert_int_equal(mainLoop_tick(&ml), 0);
}

static void test_mainLoop_tick_stillWaiting(void **state) {
    (void)state;
    MainLoop ml;
    mainLoop_init(&ml, NULL, NULL, NULL, &mvcc, &mvccBuffor);
    Transaction txn = {.xid = OWN_XID};
    SqlExecutor se = waiting(ACTIVE_XID);
    se.transaction = &txn;
    mainLoop_handle(&ml, &se, ACTION_WAIT);
    assert_int_equal(mainLoop_tick(&ml), 1);
    assert_int_equal(mainLoop_tick(&ml), 1);
    assert_ptr_equal(ml.blocked.operation[0], &se);
}

static void test_mainLoop_tick_timeoutAborts(void **state) {
    (void)state;
    MainLoop ml;
    mainLoop_init(&ml, NULL, NULL, NULL, &mvcc, &mvccBuffor);
    Transaction txn = {.xid = OWN_XID};
    SqlExecutor se = waiting(ACTIVE_XID);
    se.transaction = &txn;
    mainLoop_handle(&ml, &se, ACTION_WAIT);
    se.waitingSince = nowTime() - ((int64_t)timeExceedTransaction * 1000 + 1);
    assert_int_equal(mainLoop_tick(&ml), 0);
    assert_null(ml.blocked.operation[0]);
    assert_int_equal(se.action, ACTION_ERROR_CONCURRENT_UPDATE);
    assert_int_equal(mvcc.txn_status[OWN_XID], TXN_ABORTED);
    /* the transaction it waited for is not touched */
    assert_int_equal(mvcc.txn_status[ACTIVE_XID], TXN_ACTIVE);
}

static void test_mainLoop_tick_notYetTimedOut(void **state) {
    (void)state;
    MainLoop ml;
    mainLoop_init(&ml, NULL, NULL, NULL, &mvcc, &mvccBuffor);
    Transaction txn = {.xid = OWN_XID};
    SqlExecutor se = waiting(ACTIVE_XID);
    se.transaction = &txn;
    mainLoop_handle(&ml, &se, ACTION_WAIT);
    se.waitingSince = nowTime() - ((int64_t)timeExceedTransaction * 1000 - 1000);
    assert_int_equal(mainLoop_tick(&ml), 1);
    assert_int_equal(mvcc.txn_status[OWN_XID], TXN_ACTIVE);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_conflict_xmaxNone_proceeds, setup),
        cmocka_unit_test_setup(test_conflict_ownXmax_proceeds, setup),
        cmocka_unit_test_setup(test_conflict_activeXmax_waits, setup),
        cmocka_unit_test_setup(test_conflict_commitedXmax, setup),
        cmocka_unit_test_setup(test_conflict_abortedXmax_proceeds, setup),
        cmocka_unit_test_setup(test_conflict_unknownXmax_proceeds, setup),
        cmocka_unit_test_setup(test_conflict_nullMvcc_proceeds, setup),
        cmocka_unit_test_setup(test_conflict_nullMvccBuffor_noCrash, setup),

        cmocka_unit_test_setup(test_tuple_free_proceeds, setup),
        cmocka_unit_test_setup(test_tuple_activeXmax_waitsOnXmax, setup),
        cmocka_unit_test_setup(test_tuple_activeXmin_waitsOnXmin, setup),
        cmocka_unit_test_setup(test_tuple_ownXmin_proceeds, setup),
        cmocka_unit_test_setup(test_tuple_abortedXmin_proceeds, setup),
        cmocka_unit_test_setup(test_tuple_xmaxHasPriority, setup),
        cmocka_unit_test_setup(test_tuple_ownXmax_activeXmin_waits, setup),

        cmocka_unit_test_setup(test_tab_addUntilFull, setup),
        cmocka_unit_test_setup(test_tab_reusesFreedSlot, setup),
        cmocka_unit_test_setup(test_blocked_emptyTable_null, setup),
        cmocka_unit_test_setup(test_blocked_stillActive_staysInTable, setup),
        cmocka_unit_test_setup(test_blocked_commited_returnedAndRemoved, setup),
        cmocka_unit_test_setup(test_blocked_aborted_returned, setup),
        cmocka_unit_test_setup(test_blocked_unknownStatus_returned, setup),
        cmocka_unit_test_setup(test_blocked_notWaiting_dropped, setup),
        cmocka_unit_test_setup(test_blocked_onlyReadyOnesReturned, setup),

        cmocka_unit_test_setup(test_mainLoop_waitGoesToTable, setup),
        cmocka_unit_test_setup(test_mainLoop_errorAbortsTransaction, setup),
        cmocka_unit_test_setup(test_mainLoop_fullTableAborts, setup),
        cmocka_unit_test_setup(test_mainLoop_tick_noBlocked, setup),
        cmocka_unit_test_setup(test_mainLoop_tick_stillWaiting, setup),
        cmocka_unit_test_setup(test_mainLoop_tick_timeoutAborts, setup),
        cmocka_unit_test_setup(test_mainLoop_tick_notYetTimedOut, setup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
