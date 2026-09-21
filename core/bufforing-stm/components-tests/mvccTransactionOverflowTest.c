#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../../memory-mgmt/memory-mgmt/config.h"
#include "../bufforing-stm/transaction.h"

/* ── helpers ──────────────────────────────────────────────────────────── */

#define TEST_FILE "/tmp/test_mvcc_txn_overflow.bin"

static int setup(void **state) {
    (void)state;
    remove(TEST_FILE);
    return 0;
}

static int teardown(void **state) {
    (void)state;
    remove(TEST_FILE);
    return 0;
}

/* ── 1. Full cycle: begin+commit MAX_TRANSACTIONS*2 transactions ─────── */

static void test_two_full_batches_commit(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init_with_file(&mvcc, TEST_FILE);

    Transaction txns[MAX_TRANSACTIONS * 2];

    // Begin and commit 2*MAX_TRANSACTIONS transactions
    for (int i = 0; i < MAX_TRANSACTIONS * 2; i++) {
        beginTransaction(&mvcc, &txns[i]);
        incrementTxnCounter(&mvcc);
        commitTransaction(&mvcc, &txns[i]);
    }

    assert_int_equal(MAX_TRANSACTIONS * 2, mvcc.txn_counter);
    assert_int_equal(MAX_TRANSACTIONS, mvcc.base_txn);

    // Verify first batch (flushed to file) via buffor
    MVCCBuffor buf;
    mvcc_buffor_init(&buf);
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        int8_t s = mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, i);
        assert_int_equal(TXN_COMMITED, s);
    }

    // Verify second batch (still in memory)
    for (int i = MAX_TRANSACTIONS; i < MAX_TRANSACTIONS * 2; i++) {
        int8_t s = mvcc.txn_status[i - mvcc.base_txn];
        assert_int_equal(TXN_COMMITED, s);
    }
}

/* ── 2. Mixed commit/abort across batch boundary ────────────────────── */

static void test_mixed_across_boundary(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init_with_file(&mvcc, TEST_FILE);

    int32_t total = MAX_TRANSACTIONS + 50;

    for (int i = 0; i < total; i++) {
        Transaction txn;
        beginTransaction(&mvcc, &txn);
        incrementTxnCounter(&mvcc);
        if (i % 2 == 0) {
            commitTransaction(&mvcc, &txn);
        } else {
            abortTransaction(&mvcc, &txn);
        }
    }

    MVCCBuffor buf;
    mvcc_buffor_init(&buf);

    // Check old batch from file
    assert_int_equal(TXN_COMMITED, mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, 0));
    assert_int_equal(TXN_ABORTED,  mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, 1));
    assert_int_equal(TXN_COMMITED, mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, 100));
    assert_int_equal(TXN_ABORTED,  mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, 101));

    // Check new batch in memory
    int32_t last = total - 1;
    int8_t expected = (last % 2 == 0) ? TXN_COMMITED : TXN_ABORTED;
    assert_int_equal(expected, mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, last));
}

/* ── 3. Commit old transaction AFTER flush ───────────────────────────── */

static void test_late_commit_after_flush(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init_with_file(&mvcc, TEST_FILE);

    // Start a transaction early (xid=5), leave it active
    Transaction early_txn;
    for (int i = 0; i < 6; i++) {
        Transaction txn;
        beginTransaction(&mvcc, &txn);
        incrementTxnCounter(&mvcc);
        if (i == 5) {
            early_txn = txn; // save reference, don't commit yet
        } else {
            commitTransaction(&mvcc, &txn);
        }
    }
    // early_txn.xid == 5, status == TXN_ACTIVE

    // Fill up the rest of the batch to trigger flush
    for (int i = 6; i < MAX_TRANSACTIONS + 10; i++) {
        Transaction txn;
        beginTransaction(&mvcc, &txn);
        incrementTxnCounter(&mvcc);
        commitTransaction(&mvcc, &txn);
    }

    // Now base_txn == MAX_TRANSACTIONS, early_txn.xid==5 is in the old batch on file
    assert_int_equal(MAX_TRANSACTIONS, mvcc.base_txn);

    // Commit the old transaction — should write directly to file
    commitTransaction(&mvcc, &early_txn);

    // Verify via buffer load
    MVCCBuffor buf;
    mvcc_buffor_init(&buf);
    assert_int_equal(TXN_COMMITED, mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, 5));
}

/* ── 4. Abort old transaction AFTER flush ────────────────────────────── */

static void test_late_abort_after_flush(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init_with_file(&mvcc, TEST_FILE);

    Transaction early_txn;
    beginTransaction(&mvcc, &early_txn); // xid=0
    incrementTxnCounter(&mvcc);

    // Fill rest + overflow
    for (int i = 1; i < MAX_TRANSACTIONS + 5; i++) {
        Transaction txn;
        beginTransaction(&mvcc, &txn);
        incrementTxnCounter(&mvcc);
        commitTransaction(&mvcc, &txn);
    }

    // Abort old xid=0
    abortTransaction(&mvcc, &early_txn);

    MVCCBuffor buf;
    mvcc_buffor_init(&buf);
    assert_int_equal(TXN_ABORTED, mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, 0));
}

/* ── 5. Three full batches ───────────────────────────────────────────── */

static void test_three_batches(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init_with_file(&mvcc, TEST_FILE);

    int32_t total = MAX_TRANSACTIONS * 3;

    for (int i = 0; i < total; i++) {
        Transaction txn;
        beginTransaction(&mvcc, &txn);
        incrementTxnCounter(&mvcc);
        commitTransaction(&mvcc, &txn);
    }

    assert_int_equal(total, mvcc.txn_counter);
    assert_int_equal(MAX_TRANSACTIONS * 2, mvcc.base_txn);

    // Flush current window to file for full verification
    mvcc_buffor_save(mvcc.txn_status, TEST_FILE, mvcc.base_txn, MAX_TRANSACTIONS);

    MVCCBuffor buf;
    mvcc_buffor_init(&buf);

    // Check from each batch
    assert_int_equal(TXN_COMMITED, mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, 10));
    assert_int_equal(TXN_COMMITED, mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, MAX_TRANSACTIONS + 10));
    assert_int_equal(TXN_COMMITED, mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, MAX_TRANSACTIONS * 2 + 10));
}

/* ── 6. getOldestXid returns correct xid after flush ─────────────────── */

static void test_getOldestXid_after_overflow(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init_with_file(&mvcc, TEST_FILE);

    // Fill first batch, all committed
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        Transaction txn;
        beginTransaction(&mvcc, &txn);
        incrementTxnCounter(&mvcc);
        commitTransaction(&mvcc, &txn);
    }

    // Start second batch, leave txn at index 3 active
    for (int i = 0; i < 10; i++) {
        Transaction txn;
        beginTransaction(&mvcc, &txn);
        incrementTxnCounter(&mvcc);
        if (i != 3) {
            commitTransaction(&mvcc, &txn);
        }
    }

    // getOldestXid should return base_txn + 3 = MAX_TRANSACTIONS + 3
    int32_t oldest = getOldestXid(&mvcc);
    assert_int_equal(MAX_TRANSACTIONS + 3, oldest);
}

/* ── 7. beginTransactionWithIsolation works across overflow ──────────── */

static void test_beginWithIsolation_overflow(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init_with_file(&mvcc, TEST_FILE);

    // Fill first batch
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        Transaction txn;
        beginTransaction(&mvcc, &txn);
        incrementTxnCounter(&mvcc);
        commitTransaction(&mvcc, &txn);
    }

    // Begin with isolation in second batch
    Transaction txn;
    beginTransactionWithIsolation(&mvcc, &txn, ISOLATION_READ_COMMITTED);
    incrementTxnCounter(&mvcc);

    assert_int_equal(MAX_TRANSACTIONS, txn.xid);
    assert_int_equal(ISOLATION_READ_COMMITTED, txn.isolation_level);
    assert_int_equal(TXN_ACTIVE, mvcc.txn_status[txn.xid - mvcc.base_txn]);
}

/* ── 8. No filepath: overflow still works (no crash, no file I/O) ────── */

static void test_overflow_without_file(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init(&mvcc); // no filepath

    for (int i = 0; i < MAX_TRANSACTIONS + 10; i++) {
        Transaction txn;
        beginTransaction(&mvcc, &txn);
        incrementTxnCounter(&mvcc);
        commitTransaction(&mvcc, &txn);
    }

    // Should not crash, base_txn advanced
    assert_int_equal(MAX_TRANSACTIONS, mvcc.base_txn);
    assert_int_equal(MAX_TRANSACTIONS + 10, mvcc.txn_counter);
}

/* ── runner ───────────────────────────────────────────────────────────── */

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_two_full_batches_commit, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mixed_across_boundary, setup, teardown),
        cmocka_unit_test_setup_teardown(test_late_commit_after_flush, setup, teardown),
        cmocka_unit_test_setup_teardown(test_late_abort_after_flush, setup, teardown),
        cmocka_unit_test_setup_teardown(test_three_batches, setup, teardown),
        cmocka_unit_test_setup_teardown(test_getOldestXid_after_overflow, setup, teardown),
        cmocka_unit_test_setup_teardown(test_beginWithIsolation_overflow, setup, teardown),
        cmocka_unit_test(test_overflow_without_file),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}