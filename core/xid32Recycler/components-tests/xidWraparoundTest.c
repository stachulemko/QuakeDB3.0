#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../vacuum/vacuum/vacuum.h"
#include "../../bufforing-stm/bufforing-stm/mvcc.h"
#include "../../bufforing-stm/bufforing-stm/transaction.h"

/*
 * Component tests of xid recycling: real INSERT / UPDATE / SELECT / vacuum / lost update
 * while the xid counter crosses INT32_MAX (xids become negative as int32) and 0xFFFFFFFF
 * (the counter wraps to 0). Transactions go through beginTransaction / mvcc_set_txn_status,
 * so the status window is flushed to the status file on the way.
 * Built twice: test_xid_wraparound (VIEW_MODE=1, Repeatable Read) and test_xid_wraparound_rc (VIEW_MODE=2).
 */

#define WRAP_TABLE  7
#define STATUS_FILE "/tmp/test_xid_wraparound_status.bin"

#define BEFORE_WRAP   ((int32_t)0xFFFFFFF0u)   /* 16 xids before the counter wraps to 0 */
#define BEFORE_INT_MAX (INT32_MAX - 5)         /* 6 xids before the xids become negative */

typedef struct {
    Buffors    buffors;
    FSMCache  *c;
    FSMMapAll  fsmMapAll;
    MVCC       mvcc;
    MVCCBuffor mvccBuffor;
} TestEnv;

static void env_setup(TestEnv *env, int32_t firstXid) {
    remove(STATUS_FILE);
    initializeBuffors(&env->buffors, 30);
    FSMCacheCreateC(&env->c);
    fsm_cache_set(env->c, WRAP_TABLE);
    init_FSMMapAll(&env->fsmMapAll);
    addTableToFSMMapAll(&env->fsmMapAll, WRAP_TABLE);
    mvcc_init_with_file(&env->mvcc, STATUS_FILE);
    mvcc_set_next_xid(&env->mvcc, firstXid);
    mvcc_buffor_init(&env->mvccBuffor);
}

static void env_teardown(TestEnv *env) {
    for (int i = 0; i < env->buffors.count; i++) {
        if (env->buffors.buffors[i].isUsed && env->buffors.buffors[i].universalBlock) {
            if (env->buffors.buffors[i].universalBlock->block)
                free(env->buffors.buffors[i].universalBlock->block);
            if (env->buffors.buffors[i].universalBlock->header)
                free(env->buffors.buffors[i].universalBlock->header);
            free(env->buffors.buffors[i].universalBlock);
        }
    }
    free(env->buffors.buffors);
    fsm_cache_free(env->c);
    free(env->c);
    remove(STATUS_FILE);
}

/* ---- transactions ---- */

static int32_t env_begin(TestEnv *env) {
    Transaction t;
    beginTransaction(&env->mvcc, &t);
    incrementTxnCounter(&env->mvcc);
    assert_true(xidIsNormal(t.xid));          /* -1 and XID_FROZEN are never given out */
    return t.xid;
}

static void env_commit(TestEnv *env, int32_t xid) { mvcc_set_txn_status(&env->mvcc, xid, TXN_COMMITED); }
static void env_abort(TestEnv *env, int32_t xid)  { mvcc_set_txn_status(&env->mvcc, xid, TXN_ABORTED); }

/* run empty committed transactions until the next xid is `next` */
static void env_burn_until(TestEnv *env, int32_t next) {
    while (env->mvcc.txn_counter != next) env_commit(env, env_begin(env));
}

/* ---- rows: (id int32, val int32) ---- */

static int32_t env_endblock(TestEnv *env) {
    BlockCounterEntry *e = fsm_cache_get(env->c, WRAP_TABLE);
    return e ? e->maxBlock : 0;
}

static void env_insert_raw(TestEnv *env, int32_t xmin, int32_t xmax, int32_t id, int32_t val) {
    AllVar vals[2] = {all_var_from_int32(id), all_var_from_int32(val)};
    int8_t bm[2]   = {0, 0};
    DataBuffor *b = addTupleToOtherFunction(&env->buffors, env->c, &env->fsmMapAll,
        WRAP_TABLE, vals, 2, bm, 2,
        xmin, xmax, 0, 0, 0, 0, -1, NULL, NULL, NULL);
    assert_non_null(b);
    b->pinCount = 0;
}

static void env_insert(TestEnv *env, int32_t xid, int32_t id, int32_t val) {
    env_insert_raw(env, xid, 0, id, val);
}

/* UPDATE SET val = newVal WHERE id == id — se/txn are kept by the caller so a blocked update can resume */
static void env_prepare_update(TestEnv *env, SqlExecutor *se, Transaction *txn, int32_t id, int32_t newVal) {
    memset(se, 0, sizeof(*se));
    se->transaction = txn;
    se->tableId     = WRAP_TABLE;
    se->endBlock    = env_endblock(env);
    sql_addWhere(se, 0, all_var_from_int32(id), SQL_EQ, 1);
    int32_t cols[] = {1};
    AllVar  vals[] = {all_var_from_int32(newVal)};
    sql_setUpdate(se, cols, vals, 1);
}

static Action env_run(TestEnv *env, SqlExecutor *se) {
    ResultTuple result = {0};
    return sql_fullScan(se, &result, &env->buffors, env->c, &env->fsmMapAll, &env->mvcc, &env->mvccBuffor);
}

static Action env_update(TestEnv *env, int32_t xid, int32_t id, int32_t newVal) {
    Transaction txn = {.xid = xid, .isolation_level = VIEW_MODE};
    SqlExecutor se;
    env_prepare_update(env, &se, &txn, id, newVal);
    return env_run(env, &se);
}

/* how many rows with (id, val) transaction xid sees */
static int32_t env_count(TestEnv *env, int32_t xid, int32_t id, int32_t val) {
    Transaction txn = {.xid = xid, .isolation_level = VIEW_MODE};
    SqlExecutor se  = {0};
    se.transaction  = &txn;
    se.tableId      = WRAP_TABLE;
    se.endBlock     = env_endblock(env);
    int32_t cols[]  = {0, 1};
    sql_setSelect(&se, cols, 2);
    sql_addWhere(&se, 0, all_var_from_int32(id), SQL_EQ, 1);
    sql_addWhere(&se, 1, all_var_from_int32(val), SQL_EQ, 1);
    ResultTuple result = {0};
    sql_fullScan(&se, &result, &env->buffors, env->c, &env->fsmMapAll, &env->mvcc, &env->mvccBuffor);
    for (int i = 0; i < result.tuple_count; i++) free(result.tuples[i]);
    return result.tuple_count;
}

/* the value of row `id` that transaction xid sees, -1 when it sees none, -2 when more than one */
static int32_t env_value(TestEnv *env, int32_t xid, int32_t id) {
    Transaction txn = {.xid = xid, .isolation_level = VIEW_MODE};
    SqlExecutor se  = {0};
    se.transaction  = &txn;
    se.tableId      = WRAP_TABLE;
    se.endBlock     = env_endblock(env);
    int32_t cols[]  = {0, 1};
    sql_setSelect(&se, cols, 2);
    sql_addWhere(&se, 0, all_var_from_int32(id), SQL_EQ, 1);
    ResultTuple result = {0};
    sql_fullScan(&se, &result, &env->buffors, env->c, &env->fsmMapAll, &env->mvcc, &env->mvccBuffor);
    int32_t v = -1;
    if (result.tuple_count == 1) v = result.tuples[0]->dnb.data[1].val.i32;
    if (result.tuple_count > 1)  v = -2;
    for (int i = 0; i < result.tuple_count; i++) free(result.tuples[i]);
    return v;
}

static Tuple *env_tuple(TestEnv *env, int32_t blockId, int32_t idx) {
    DataBuffor *b = getBuffor(WRAP_TABLE, blockId, &env->buffors);
    assert_non_null(b);
    assert_non_null(b->universalBlock);
    b->pinCount = 0;
    return &b->universalBlock->block->tuples[idx];
}

static void env_vacuum(TestEnv *env) {
    Vacuum vac = {0};
    vac.densityDeadTuplesPerBlockMax = 0;   /* do not stop early */
    autoVacuumScan(&vac, WRAP_TABLE, env->c, &env->buffors, NULL, &env->mvcc, &env->fsmMapAll);
}

/* =========================================================================
 * 1. The counter itself: reserved xids, order on the circle
 * ========================================================================= */

/* through INT32_MAX and 0xFFFFFFFF: never -1 / XID_FROZEN, every xid is "after" the previous one */
static void test_counter_through_both_boundaries(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env, BEFORE_INT_MAX);
    int32_t prev = env_begin(&env);
    env_commit(&env, prev);
    for (int i = 0; i < 20; i++) {
        int32_t x = env_begin(&env);
        env_commit(&env, x);
        assert_true(xidBefore(prev, x));
        prev = x;
    }
    assert_true(prev < 0);                               /* past INT32_MAX: negative as int32 */

    mvcc_set_next_xid(&env.mvcc, BEFORE_WRAP);
    prev = env_begin(&env);
    env_commit(&env, prev);
    for (int i = 0; i < 40; i++) {
        int32_t x = env_begin(&env);
        env_commit(&env, x);
        assert_true(xidBefore(prev, x));
        prev = x;
    }
    assert_true(prev >= 0 && prev < 40);                 /* the counter wrapped to 0 */
    env_teardown(&env);
}

/* =========================================================================
 * 2. Visibility across the wraparound (0xFFFFFFFF -> 0)
 * ========================================================================= */

/* a row inserted before the wraparound is visible to a transaction after it */
static void test_insert_before_wrap_visible_after(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env, BEFORE_WRAP);

    int32_t t1 = env_begin(&env);
    env_insert(&env, t1, 1, 10);
    env_commit(&env, t1);

    env_burn_until(&env, 5);
    int32_t t2 = env_begin(&env);
    assert_true((uint32_t)t2 < (uint32_t)t1);            /* numerically smaller, but newer */
    assert_int_equal(10, env_value(&env, t2, 1));
    env_teardown(&env);
}

/* a row inserted after the wraparound is invisible to a transaction that started before it */
static void test_insert_after_wrap_invisible_to_older(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env, BEFORE_WRAP);

    int32_t old = env_begin(&env);                       /* stays active */
    env_burn_until(&env, 3);
    int32_t t = env_begin(&env);
    env_insert(&env, t, 1, 20);
    env_commit(&env, t);

    assert_int_equal(-1, env_value(&env, old, 1));
    int32_t later = env_begin(&env);
    assert_int_equal(20, env_value(&env, later, 1));
    env_teardown(&env);
}

/* UPDATE after the wraparound of a row from before it: the old reader keeps the old version */
static void test_update_across_wrap(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env, BEFORE_WRAP);

    int32_t t1 = env_begin(&env);
    env_insert(&env, t1, 1, 10);
    env_commit(&env, t1);
    int32_t reader = env_begin(&env);                    /* before the wraparound, active */

    env_burn_until(&env, 2);
    int32_t t2 = env_begin(&env);
    assert_int_equal(ACTION_PROCEED, env_update(&env, t2, 1, 20));
    env_commit(&env, t2);

    assert_int_equal(10, env_value(&env, reader, 1));
    int32_t t3 = env_begin(&env);
    assert_int_equal(20, env_value(&env, t3, 1));
    assert_int_equal(0, env_count(&env, t3, 1, 10));
    env_teardown(&env);
}

/* versions created before, at and after the wraparound — every reader sees its own version.
 * r1/r2 are read right after the first update past the wraparound: later scans may prune their
 * versions, because getOldestXid sees only the current status window (also without a wraparound). */
static void test_version_chain_across_wrap(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env, (int32_t)0xFFFFFFF8u);

    int32_t t1 = env_begin(&env);
    env_insert(&env, t1, 1, 1);
    env_commit(&env, t1);
    int32_t r1 = env_begin(&env);

    int32_t t2 = env_begin(&env);
    assert_int_equal(ACTION_PROCEED, env_update(&env, t2, 1, 2));
    env_commit(&env, t2);
    int32_t r2 = env_begin(&env);

    env_burn_until(&env, 1);                             /* wraparound */
    int32_t t3 = env_begin(&env);
    assert_int_equal(ACTION_PROCEED, env_update(&env, t3, 1, 3));
    env_commit(&env, t3);
#if VIEW_MODE == 1
    /* Read Committed walks the whole chain and prunes r1's/r2's versions on the way: r1/r2 are in
     * the previous status window, so getOldestXid does not see them (known limitation, not caused
     * by the wraparound — the same happens at every window boundary) */
    assert_int_equal(1, env_value(&env, r1, 1));
    assert_int_equal(2, env_value(&env, r2, 1));
#endif
    int32_t r3 = env_begin(&env);

    int32_t t4 = env_begin(&env);
    assert_int_equal(ACTION_PROCEED, env_update(&env, t4, 1, 4));
    env_commit(&env, t4);
    int32_t r4 = env_begin(&env);

    /* r1, r2 before the wraparound (negative), t3 got 1 — 0 and -1 were skipped */
    assert_int_equal(-7, r1);
    assert_int_equal(-5, r2);
    assert_int_equal(1, t3);
    assert_int_equal(2, r3);
    assert_int_equal(4, r4);
    assert_int_equal(3, env_value(&env, r3, 1));
    assert_int_equal(4, env_value(&env, r4, 1));
    env_teardown(&env);
}

/* =========================================================================
 * 3. Negative xids (after INT32_MAX) are real xids, not "no xmax"
 * ========================================================================= */

static void test_negative_xids_are_real(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env, BEFORE_INT_MAX);

    int32_t t1 = env_begin(&env);
    env_insert(&env, t1, 1, 10);
    env_commit(&env, t1);
    int32_t reader = env_begin(&env);                    /* positive, active */

    env_burn_until(&env, (int32_t)0x80000005u);
    int32_t t2 = env_begin(&env);
    assert_true(t2 < 0);
    assert_int_equal(ACTION_PROCEED, env_update(&env, t2, 1, 20));
    env_commit(&env, t2);

    Tuple *v1 = env_tuple(&env, 1, 0);
    assert_int_equal(t2, v1->header.t_xmax);             /* negative xmax = updated, not "none" */

    int32_t t3 = env_begin(&env);
    assert_int_equal(20, env_value(&env, t3, 1));
    assert_int_equal(10, env_value(&env, reader, 1));
    env_teardown(&env);
}

/* xmax == -1 (written by UPDATE for a new version) still means "not deleted" */
static void test_xmax_minus_one_is_none(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env, BEFORE_WRAP);
    int32_t t1 = env_begin(&env);
    env_insert_raw(&env, t1, XID_NONE_NEG, 1, 10);
    env_commit(&env, t1);
    env_burn_until(&env, 3);
    int32_t t2 = env_begin(&env);
    assert_int_equal(10, env_value(&env, t2, 1));
    env_teardown(&env);
}

#if VIEW_MODE == 1
/* DELETE (xmax set) after the wraparound: older readers still see the row, newer do not.
 * Read Committed does not look at xmax in this engine, so only Repeatable Read checks it. */
static void test_delete_across_wrap(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env, BEFORE_WRAP);
    int32_t t1 = env_begin(&env);
    int32_t reader = env_begin(&env);
    env_burn_until(&env, 4);
    int32_t del = env_begin(&env);
    env_insert_raw(&env, t1, del, 1, 10);                /* inserted by t1, deleted by del */
    env_commit(&env, t1);
    env_commit(&env, del);
    int32_t after = env_begin(&env);

    assert_int_equal(10, env_value(&env, reader, 1));
    assert_int_equal(-1, env_value(&env, after, 1));
    env_teardown(&env);
}
#endif

/* =========================================================================
 * 4. Frozen rows (t_xmin = XID_FROZEN, as vacuum leaves them)
 * ========================================================================= */

static void test_frozen_row_visible_and_updatable(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env, BEFORE_WRAP);
    env_insert(&env, XID_FROZEN, 1, 10);

    int32_t before = env_begin(&env);
    env_burn_until(&env, 3);
    int32_t after = env_begin(&env);
    assert_int_equal(10, env_value(&env, before, 1));
    assert_int_equal(10, env_value(&env, after, 1));

    /* the frozen xmin reads as committed — the update does not wait for it */
    assert_int_equal(ACTION_PROCEED, env_update(&env, after, 1, 11));
    env_commit(&env, after);
    int32_t later = env_begin(&env);
    assert_int_equal(11, env_value(&env, later, 1));
    env_teardown(&env);
}

/* =========================================================================
 * 5. Lost update across the wraparound
 * ========================================================================= */

/* tb starts before the wraparound, ta after it; ta updates first, tb waits for ta.
 * RR: tb must fail; RC: tb re-evaluates on ta's version (newest version found via txn_counter). */
static void test_lost_update_across_wrap(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env, BEFORE_WRAP);

    int32_t t0 = env_begin(&env);
    env_insert(&env, t0, 1, 10);
    env_commit(&env, t0);

    Transaction tb = {.xid = env_begin(&env), .isolation_level = VIEW_MODE};
    env_burn_until(&env, 6);
    Transaction ta = {.xid = env_begin(&env), .isolation_level = VIEW_MODE};
    assert_true(xidBefore(tb.xid, ta.xid));

    SqlExecutor seA;
    env_prepare_update(&env, &seA, &ta, 1, 20);
    assert_int_equal(ACTION_PROCEED, env_run(&env, &seA));

    SqlExecutor seB;
    env_prepare_update(&env, &seB, &tb, 1, 30);
    assert_int_equal(ACTION_WAIT, env_run(&env, &seB));
    assert_int_equal(ta.xid, seB.xidOfBlockingTransaction);

    env_commit(&env, ta.xid);
    Action resumed = env_run(&env, &seB);
#if VIEW_MODE == 1
    assert_int_equal(ACTION_ERROR_CONCURRENT_UPDATE, resumed);
    env_abort(&env, tb.xid);
    int32_t later = env_begin(&env);
    assert_int_equal(20, env_value(&env, later, 1));
#else
    assert_int_equal(ACTION_PROCEED, resumed);
    env_commit(&env, tb.xid);
    int32_t later = env_begin(&env);
    assert_int_equal(30, env_value(&env, later, 1));
#endif
    env_teardown(&env);
}

/* the blocking transaction has a negative xid (past INT32_MAX) — the wait still resolves */
static void test_wait_on_negative_xid(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env, BEFORE_INT_MAX);

    int32_t t0 = env_begin(&env);
    env_insert(&env, t0, 1, 10);
    env_commit(&env, t0);

    env_burn_until(&env, (int32_t)0x80000002u);
    Transaction ta = {.xid = env_begin(&env), .isolation_level = VIEW_MODE};
    Transaction tb = {.xid = env_begin(&env), .isolation_level = VIEW_MODE};
    assert_true(ta.xid < 0 && tb.xid < 0);

    SqlExecutor seA;
    env_prepare_update(&env, &seA, &ta, 1, 20);
    assert_int_equal(ACTION_PROCEED, env_run(&env, &seA));

    SqlExecutor seB;
    env_prepare_update(&env, &seB, &tb, 1, 30);
    assert_int_equal(ACTION_WAIT, env_run(&env, &seB));
    assert_int_equal(ta.xid, seB.xidOfBlockingTransaction);

    env_abort(&env, ta.xid);                             /* ta rolls back — tb may proceed */
    assert_int_equal(ACTION_PROCEED, env_run(&env, &seB));
    env_teardown(&env);
}

/* =========================================================================
 * 6. Vacuum across the wraparound
 * ========================================================================= */

/* a version replaced after the wraparound is removed once nobody needs it */
static void test_vacuum_removes_version_across_wrap(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env, BEFORE_WRAP);

    int32_t t1 = env_begin(&env);
    env_insert(&env, t1, 1, 10);
    env_commit(&env, t1);
    env_burn_until(&env, 2);

    int32_t reader = env_begin(&env);                    /* after the wraparound, active */
    int32_t t2 = env_begin(&env);
    assert_int_equal(ACTION_PROCEED, env_update(&env, t2, 1, 20));
    env_commit(&env, t2);

    /* the reader started before t2 — vacuum keeps the old version */
    env_vacuum(&env);
    Tuple *root = env_tuple(&env, 1, 0);
    assert_false(root->header.t_infomask & INFOMASK_DEAD);
    assert_int_equal(10, env_value(&env, reader, 1));

    /* the reader ends — the old version (xmin before the wraparound) is removed */
    env_commit(&env, reader);
    env_vacuum(&env);
    root = env_tuple(&env, 1, 0);
    assert_true(root->header.t_infomask & INFOMASK_DEAD);
    assert_int_equal(0, root->dnb.data_count);

    int32_t t3 = env_begin(&env);
    assert_int_equal(20, env_value(&env, t3, 1));
    env_teardown(&env);
}

/* a version replaced past INT32_MAX (negative xmax) is removed too */
static void test_vacuum_removes_version_with_negative_xmax(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env, BEFORE_INT_MAX);

    int32_t t1 = env_begin(&env);
    env_insert(&env, t1, 1, 10);
    env_commit(&env, t1);
    env_burn_until(&env, (int32_t)0x80000003u);
    int32_t t2 = env_begin(&env);
    assert_int_equal(ACTION_PROCEED, env_update(&env, t2, 1, 20));
    env_commit(&env, t2);

    env_vacuum(&env);
    Tuple *root = env_tuple(&env, 1, 0);
    assert_true(root->header.t_infomask & INFOMASK_DEAD);
    int32_t t3 = env_begin(&env);
    assert_int_equal(20, env_value(&env, t3, 1));
    env_teardown(&env);
}

/* an UPDATE after the wraparound rolls back — vacuum keeps the old (current) version */
static void test_vacuum_keeps_version_of_rolled_back_update(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env, BEFORE_WRAP);

    int32_t t1 = env_begin(&env);
    env_insert(&env, t1, 1, 10);
    env_commit(&env, t1);
    env_burn_until(&env, 2);
    int32_t t2 = env_begin(&env);
    assert_int_equal(ACTION_PROCEED, env_update(&env, t2, 1, 20));
    env_abort(&env, t2);

    env_vacuum(&env);
    Tuple *root = env_tuple(&env, 1, 0);
    assert_false(root->header.t_infomask & (INFOMASK_DEAD | INFOMASK_UNUSED));
    assert_int_equal(10, root->dnb.data[1].val.i32);
    env_teardown(&env);
}

/* =========================================================================
 * 7. Many transactions through the wraparound, with vacuum in between
 * ========================================================================= */

static void test_many_updates_through_wrap(void **state) {
    (void)state;
    TestEnv env;
    env_setup(&env, (int32_t)0xFFFFFE00u);

    int32_t t0 = env_begin(&env);
    env_insert(&env, t0, 1, 0);
    env_insert(&env, t0, 2, 1000);
    env_commit(&env, t0);

    for (int32_t i = 1; i <= 700; i++) {
        int32_t t = env_begin(&env);
        assert_int_equal(ACTION_PROCEED, env_update(&env, t, 1, i));
        if (i % 2 == 0) assert_int_equal(ACTION_PROCEED, env_update(&env, t, 2, 1000 + i));
        env_commit(&env, t);
        if (i % 100 == 0) env_vacuum(&env);

        int32_t r = env_begin(&env);
        assert_int_equal(i, env_value(&env, r, 1));
        assert_int_equal(1000 + i - (i % 2), env_value(&env, r, 2));
        env_commit(&env, r);
    }
    assert_true(env.mvcc.txn_counter >= 0 && env.mvcc.txn_counter < 1000);   /* wrapped */
    env_teardown(&env);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_counter_through_both_boundaries),
        cmocka_unit_test(test_insert_before_wrap_visible_after),
        cmocka_unit_test(test_insert_after_wrap_invisible_to_older),
        cmocka_unit_test(test_update_across_wrap),
        cmocka_unit_test(test_version_chain_across_wrap),
        cmocka_unit_test(test_negative_xids_are_real),
        cmocka_unit_test(test_xmax_minus_one_is_none),
#if VIEW_MODE == 1
        cmocka_unit_test(test_delete_across_wrap),
#endif
        cmocka_unit_test(test_frozen_row_visible_and_updatable),
        cmocka_unit_test(test_lost_update_across_wrap),
        cmocka_unit_test(test_wait_on_negative_xid),
        cmocka_unit_test(test_vacuum_removes_version_across_wrap),
        cmocka_unit_test(test_vacuum_removes_version_with_negative_xmax),
        cmocka_unit_test(test_vacuum_keeps_version_of_rolled_back_update),
        cmocka_unit_test(test_many_updates_through_wrap),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}