#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../../memory-mgmt/memory-mgmt/config.h"
#include "../bufforing-stm/mvccBuffor.h"
#include "../bufforing-stm/transaction.h"

/* ── helpers ──────────────────────────────────────────────────────────── */

#define COMP_TEST_FILE "/tmp/test_mvcc_buffor_component.bin"

static int setup(void **state) {
    (void)state;
    remove(COMP_TEST_FILE);
    return 0;
}

static int teardown(void **state) {
    (void)state;
    remove(COMP_TEST_FILE);
    return 0;
}

/* ── 1. overflow scenario: transactions beyond MAX_TRANSACTIONS ──────── */

static void test_overflow_begin_flush_read(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init(&mvcc);

    // Begin MAX_TRANSACTIONS transactions and commit them all
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        Transaction txn;
        beginTransaction(&mvcc, &txn);
        incrementTxnCounter(&mvcc);
        commitTransaction(&mvcc, &txn);
    }
    assert_int_equal(MAX_TRANSACTIONS, mvcc.txn_counter);

    // Flush to file
    assert_int_equal(0, mvcc_buffor_flush(&mvcc, COMP_TEST_FILE, 0));

    // All should be committed (1)
    MVCCBuffor buf;
    mvcc_buffor_init(&buf);
    assert_int_equal(0, mvcc_buffor_load(&buf, COMP_TEST_FILE, MAX_TRANSACTIONS - 1));
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        assert_int_equal(TXN_COMMITED, mvcc_buffor_get_status(&buf, i));
    }
}

/* ── 2. mixed commit/abort then flush and verify ─────────────────────── */

static void test_mixed_commit_abort(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init(&mvcc);

    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        Transaction txn;
        beginTransaction(&mvcc, &txn);
        incrementTxnCounter(&mvcc);
        if (i % 3 == 0) {
            abortTransaction(&mvcc, &txn);
        } else {
            commitTransaction(&mvcc, &txn);
        }
    }

    assert_int_equal(0, mvcc_buffor_flush(&mvcc, COMP_TEST_FILE, 0));

    MVCCBuffor buf;
    assert_int_equal(0, mvcc_buffor_load(&buf, COMP_TEST_FILE, MAX_TRANSACTIONS - 1));

    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        if (i % 3 == 0) {
            assert_int_equal(TXN_ABORTED, mvcc_buffor_get_status(&buf, i));
        } else {
            assert_int_equal(TXN_COMMITED, mvcc_buffor_get_status(&buf, i));
        }
    }
}

/* ── 3. two batches: flush, reset, new batch, flush, read both ───────── */

static void test_two_batches(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init(&mvcc);

    // Batch 1: all committed
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        Transaction txn;
        beginTransaction(&mvcc, &txn);
        incrementTxnCounter(&mvcc);
        commitTransaction(&mvcc, &txn);
    }
    assert_int_equal(0, mvcc_buffor_flush(&mvcc, COMP_TEST_FILE, 0));

    // Reset in-memory status for batch 2
    memset(mvcc.txn_status, 0, sizeof(mvcc.txn_status));

    // Batch 2: all aborted (stored at indices 0..MAX_TRANSACTIONS-1 in-memory,
    // but represent xids MAX_TRANSACTIONS..2*MAX_TRANSACTIONS-1)
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        mvcc.txn_status[i] = TXN_ABORTED;
    }
    mvcc.txn_counter = MAX_TRANSACTIONS * 2;
    assert_int_equal(0, mvcc_buffor_save(mvcc.txn_status, COMP_TEST_FILE,
                                          MAX_TRANSACTIONS, MAX_TRANSACTIONS));

    // Read batch 1 entry
    MVCCBuffor buf;
    assert_int_equal(0, mvcc_buffor_load(&buf, COMP_TEST_FILE, 50));
    assert_int_equal(TXN_COMMITED, mvcc_buffor_get_status(&buf, 50));

    // Read batch 2 entry
    assert_int_equal(0, mvcc_buffor_load(&buf, COMP_TEST_FILE, MAX_TRANSACTIONS + 50));
    assert_int_equal(TXN_ABORTED, mvcc_buffor_get_status(&buf, MAX_TRANSACTIONS + 50));
}

/* ── 4. unified getter picks correct source ──────────────────────────── */

static void test_unified_getter_integration(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init(&mvcc);

    // First 10 transactions in memory
    for (int i = 0; i < 10; i++) {
        Transaction txn;
        beginTransaction(&mvcc, &txn);
        incrementTxnCounter(&mvcc);
        commitTransaction(&mvcc, &txn);
    }

    // Save 300 statuses to file (simulating overflow)
    int8_t file_data[300];
    for (int i = 0; i < 300; i++) file_data[i] = TXN_COMMITED;
    file_data[270] = TXN_ABORTED;
    assert_int_equal(0, mvcc_buffor_save(file_data, COMP_TEST_FILE, 0, 300));

    MVCCBuffor buf;
    mvcc_buffor_init(&buf);

    // In-memory: xid=5 -> committed
    assert_int_equal(TXN_COMMITED, mvcc_get_txn_status(&mvcc, &buf, COMP_TEST_FILE, 5));

    // From file: xid=270 -> aborted
    assert_int_equal(TXN_ABORTED, mvcc_get_txn_status(&mvcc, &buf, COMP_TEST_FILE, 270));

    // From file: xid=290 -> committed
    assert_int_equal(TXN_COMMITED, mvcc_get_txn_status(&mvcc, &buf, COMP_TEST_FILE, 290));
}

/* ── 5. getOldestXid still works with in-memory MVCC ─────────────────── */

static void test_oldest_xid_with_buffor(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init(&mvcc);

    Transaction t0, t1, t2;
    beginTransaction(&mvcc, &t0); incrementTxnCounter(&mvcc);
    beginTransaction(&mvcc, &t1); incrementTxnCounter(&mvcc);
    beginTransaction(&mvcc, &t2); incrementTxnCounter(&mvcc);

    commitTransaction(&mvcc, &t0);
    // t1 still active, t2 still active

    assert_int_equal(1, getOldestXid(&mvcc));
}

/* ── 6. load window slides correctly for large xid ───────────────────── */

static void test_window_sliding(void **state) {
    (void)state;
    int32_t total = MAX_TRANSACTIONS * 3;
    int8_t *all = (int8_t *)malloc(total);
    for (int i = 0; i < total; i++) {
        all[i] = (int8_t)(i % 3); // 0=active, 1=committed, 2=aborted cycle
    }
    assert_int_equal(0, mvcc_buffor_save(all, COMP_TEST_FILE, 0, total));

    MVCCBuffor buf;

    // Load window ending at last transaction
    int32_t target = total - 1;
    assert_int_equal(0, mvcc_buffor_load(&buf, COMP_TEST_FILE, target));
    assert_int_equal(target - MAX_TRANSACTIONS + 1, buf.start_txn);

    // Check first entry in window
    int32_t first_xid = buf.start_txn;
    assert_int_equal(all[first_xid], mvcc_buffor_get_status(&buf, first_xid));

    // Check last entry in window
    assert_int_equal(all[target], mvcc_buffor_get_status(&buf, target));

    // Out of window should return -1
    assert_int_equal(-1, mvcc_buffor_get_status(&buf, buf.start_txn - 1));

    free(all);
}

/* ── runner ───────────────────────────────────────────────────────────── */

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_overflow_begin_flush_read, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mixed_commit_abort, setup, teardown),
        cmocka_unit_test_setup_teardown(test_two_batches, setup, teardown),
        cmocka_unit_test_setup_teardown(test_unified_getter_integration, setup, teardown),
        cmocka_unit_test_setup_teardown(test_oldest_xid_with_buffor, setup, teardown),
        cmocka_unit_test_setup_teardown(test_window_sliding, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}