#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "../xid32Recycler.h"
#include "../../bufforing-stm/bufforing-stm/mvcc.h"
#include "../../bufforing-stm/bufforing-stm/transaction.h"

/* sparse file — offsets up to 4 GB, only the touched pages take disk space */
#define WRAP_FILE "/tmp/test_xid32Recycler_status.bin"

/* =========================================================================
 * 1. xidBefore — comparison modulo 2^32
 * ========================================================================= */

static void test_before_normal_order(void **state) {
    (void)state;
    assert_true(xidBefore(5, 10));
    assert_false(xidBefore(10, 5));
    assert_false(xidBefore(7, 7));
}

static void test_before_across_wraparound(void **state) {
    (void)state;
    /* 0xFFFFFFF0 was given out before the counter wrapped to 5 */
    assert_true(xidBefore((int32_t)0xFFFFFFF0u, 5));
    assert_false(xidBefore(5, (int32_t)0xFFFFFFF0u));
    /* INT32_MAX and the xids after it (negative as int32) */
    assert_true(xidBefore(INT32_MAX, (int32_t)0x80000005u));
}

static void test_before_half_of_the_circle(void **state) {
    (void)state;
    /* up to 2^31 - 1 ahead is still the future */
    assert_true(xidBefore(100, (int32_t)(100u + 0x7FFFFFFFu)));
    /* 2^31 + 1 ahead is already the past */
    assert_false(xidBefore(100, (int32_t)(100u + 0x80000001u)));
}

static void test_frozen_is_older_than_everything(void **state) {
    (void)state;
    assert_true(xidBefore(XID_FROZEN, 0));
    assert_true(xidBefore(XID_FROZEN, INT32_MAX));
    assert_true(xidBefore(XID_FROZEN, (int32_t)0xFFFFFF00u));
    assert_false(xidBefore(0, XID_FROZEN));
    assert_false(xidBefore(INT32_MAX, XID_FROZEN));
    assert_false(xidBefore(XID_FROZEN, XID_FROZEN));
}

/* =========================================================================
 * 2. xidNext — the counter wraps and skips reserved values
 * ========================================================================= */

static void test_next_simple(void **state) {
    (void)state;
    assert_int_equal(1, xidNext(0));
    assert_int_equal(101, xidNext(100));
}

static void test_next_skips_frozen(void **state) {
    (void)state;
    /* INT32_MAX + 1 == XID_FROZEN — skipped */
    assert_int_equal((int32_t)0x80000001u, xidNext(INT32_MAX));
}

static void test_next_skips_none_and_wraps(void **state) {
    (void)state;
    /* 0xFFFFFFFE + 1 == -1 (no xmax), then 0 (also "no xmax") — both skipped, the counter wraps to 1 */
    assert_int_equal(1, xidNext((int32_t)0xFFFFFFFEu));
    assert_false(xidIsNormal(XID_NONE_NEG));
    assert_false(xidIsNormal(XID_FROZEN));
    assert_true(xidIsNormal(0));
}

static void test_distance(void **state) {
    (void)state;
    assert_int_equal(10, xidDistance(5, 15));
    assert_int_equal(21, xidDistance((int32_t)0xFFFFFFF0u, 5));
}

/* =========================================================================
 * 3. MVCC counter wraps around
 * ========================================================================= */

static void test_mvcc_counter_wraps(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init(&mvcc);
    mvcc_set_next_xid(&mvcc, (int32_t)0xFFFFFFFDu);
    assert_int_equal((int32_t)0xFFFFFFFDu, getAndIcrement(&mvcc));
    assert_int_equal((int32_t)0xFFFFFFFEu, getAndIcrement(&mvcc));
    assert_int_equal(1, getAndIcrement(&mvcc));           /* -1 and 0 skipped */
    incrementTxnCounter(&mvcc);
    assert_int_equal(3, mvcc.txn_counter);
}

static void test_mvcc_counter_skips_frozen(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init(&mvcc);
    mvcc_set_next_xid(&mvcc, INT32_MAX);
    assert_int_equal(INT32_MAX, getAndIcrement(&mvcc));
    assert_int_equal((int32_t)0x80000001u, mvcc.txn_counter);   /* XID_FROZEN skipped */
}

/* =========================================================================
 * 4. MVCC status window and status file across the wraparound
 * ========================================================================= */

static void test_mvcc_statuses_across_wraparound(void **state) {
    (void)state;
    remove(WRAP_FILE);
    MVCC mvcc;
    mvcc_init_with_file(&mvcc, WRAP_FILE);
    mvcc_set_next_xid(&mvcc, (int32_t)0xFFFFFE00u);
    MVCCBuffor buf;
    mvcc_buffor_init(&buf);

    int32_t xids[600];
    for (int i = 0; i < 600; i++) {
        Transaction t;
        beginTransaction(&mvcc, &t);
        incrementTxnCounter(&mvcc);
        xids[i] = t.xid;
        if (i % 3 == 0) abortTransaction(&mvcc, &t);
        else            commitTransaction(&mvcc, &t);
    }
    /* 511 xids before the wraparound, -1 and 0 skipped, then from 1 */
    assert_int_equal((int32_t)0xFFFFFE00u, xids[0]);
    assert_int_equal((int32_t)0xFFFFFFFEu, xids[510]);
    assert_int_equal(1, xids[511]);
    assert_int_equal(89, xids[599]);
    assert_int_equal(0, mvcc.base_txn);       /* the window after 0xFFFFFF00 starts at 0 */

    /* old windows come from the file (offsets near 4 GB), the current one from memory */
    for (int i = 0; i < 600; i++) {
        int8_t expected = (i % 3 == 0) ? TXN_ABORTED : TXN_COMMITED;
        assert_int_equal(expected, mvcc_get_txn_status(&mvcc, &buf, NULL, xids[i]));
    }
    remove(WRAP_FILE);
}

/* a transaction started before the wraparound commits after its window went to the file */
static void test_mvcc_late_commit_across_wraparound(void **state) {
    (void)state;
    remove(WRAP_FILE);
    MVCC mvcc;
    mvcc_init_with_file(&mvcc, WRAP_FILE);
    mvcc_set_next_xid(&mvcc, (int32_t)0xFFFFFF80u);
    MVCCBuffor buf;
    mvcc_buffor_init(&buf);

    Transaction longTxn;
    beginTransaction(&mvcc, &longTxn);
    incrementTxnCounter(&mvcc);
    for (int i = 0; i < 300; i++) {
        Transaction t;
        beginTransaction(&mvcc, &t);
        incrementTxnCounter(&mvcc);
        commitTransaction(&mvcc, &t);
    }
    assert_true(xidBefore(longTxn.xid, mvcc.base_txn));     /* its window is on disk */
    assert_int_equal(1, mvcc_is_txn_active(&mvcc, &buf, NULL, longTxn.xid));

    commitTransaction(&mvcc, &longTxn);
    assert_int_equal(TXN_COMMITED, mvcc_get_txn_status(&mvcc, &buf, NULL, longTxn.xid));
    remove(WRAP_FILE);
}

static void test_mvcc_frozen_is_committed(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init(&mvcc);
    assert_int_equal(TXN_COMMITED, mvcc_get_txn_status(&mvcc, NULL, NULL, XID_FROZEN));
    assert_int_equal(-1, mvcc_get_txn_status(&mvcc, NULL, NULL, XID_NONE_NEG));
}

/* getOldestXid skips the reserved -1 slot of the last window before the wraparound */
static void test_oldest_skips_reserved_slot(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init(&mvcc);
    mvcc_set_next_xid(&mvcc, (int32_t)0xFFFFFF00u);
    for (int i = 0; i < MAX_TRANSACTIONS; i++) mvcc.txn_status[i] = TXN_COMMITED;
    mvcc.txn_status[MAX_TRANSACTIONS - 1] = TXN_ACTIVE;      /* slot of xid -1 */
    assert_int_equal(-1, getOldestXid(&mvcc));
    mvcc.txn_status[10] = TXN_ACTIVE;
    assert_int_equal((int32_t)0xFFFFFF0Au, getOldestXid(&mvcc));
}

/* after the wraparound the skipped xid 0 must not look like the oldest running transaction */
static void test_oldest_not_stuck_on_skipped_zero(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init(&mvcc);
    mvcc_set_next_xid(&mvcc, (int32_t)0xFFFFFFF0u);
    for (int i = 0; i < 20; i++) {
        Transaction t;
        beginTransaction(&mvcc, &t);
        incrementTxnCounter(&mvcc);
        commitTransaction(&mvcc, &t);
    }
    assert_int_equal(0, mvcc.base_txn);
    assert_int_equal(TXN_COMMITED, mvcc_get_txn_status(&mvcc, NULL, NULL, 0));
    /* nothing runs — the first free slot (the next xid) is the horizon */
    assert_int_equal(mvcc.txn_counter, getOldestXid(&mvcc));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_oldest_not_stuck_on_skipped_zero),
        cmocka_unit_test(test_mvcc_statuses_across_wraparound),
        cmocka_unit_test(test_mvcc_late_commit_across_wraparound),
        cmocka_unit_test(test_mvcc_frozen_is_committed),
        cmocka_unit_test(test_oldest_skips_reserved_slot),
        cmocka_unit_test(test_mvcc_counter_wraps),
        cmocka_unit_test(test_mvcc_counter_skips_frozen),
        cmocka_unit_test(test_before_normal_order),
        cmocka_unit_test(test_before_across_wraparound),
        cmocka_unit_test(test_before_half_of_the_circle),
        cmocka_unit_test(test_frozen_is_older_than_everything),
        cmocka_unit_test(test_next_simple),
        cmocka_unit_test(test_next_skips_frozen),
        cmocka_unit_test(test_next_skips_none_and_wraps),
        cmocka_unit_test(test_distance),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}