#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "../bufforing-stm/mvccBuffor.h"

/* ── helpers ──────────────────────────────────────────────────────────── */

#define TEST_FILE "/tmp/test_mvcc_buffor.bin"

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

/* ── mvcc_buffor_init ────────────────────────────────────────────────── */

static void test_init_zeroes(void **state) {
    (void)state;
    MVCCBuffor buf;
    buf.start_txn = 999;
    memset(buf.txn_status, 0xFF, sizeof(buf.txn_status));

    mvcc_buffor_init(&buf);

    assert_int_equal(0, buf.start_txn);
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        assert_int_equal(0, buf.txn_status[i]);
    }
}

/* ── create_MVCCBuffor ───────────────────────────────────────────────── */

static void test_create_allocates(void **state) {
    (void)state;
    MVCCBuffor *buf = NULL;
    create_MVCCBuffor(&buf);
    assert_non_null(buf);
    assert_int_equal(0, buf->start_txn);
    free(buf);
}

/* ── mvcc_buffor_save / load round-trip ──────────────────────────────── */

static void test_save_and_load_basic(void **state) {
    (void)state;
    int8_t statuses[MAX_TRANSACTIONS];
    memset(statuses, 0, sizeof(statuses));
    statuses[0] = 1; // committed
    statuses[1] = 2; // aborted
    statuses[2] = 0; // active

    int rc = mvcc_buffor_save(statuses, TEST_FILE, 0, MAX_TRANSACTIONS);
    assert_int_equal(0, rc);

    MVCCBuffor buf;
    rc = mvcc_buffor_load(&buf, TEST_FILE, MAX_TRANSACTIONS - 1);
    assert_int_equal(0, rc);
    assert_int_equal(0, buf.start_txn);
    assert_int_equal(1, buf.txn_status[0]);
    assert_int_equal(2, buf.txn_status[1]);
    assert_int_equal(0, buf.txn_status[2]);
}

/* ── mvcc_buffor_save at non-zero offset ─────────────────────────────── */

static void test_save_at_offset(void **state) {
    (void)state;
    // First batch: 0..MAX_TRANSACTIONS-1
    int8_t batch1[MAX_TRANSACTIONS];
    memset(batch1, 1, sizeof(batch1)); // all committed
    assert_int_equal(0, mvcc_buffor_save(batch1, TEST_FILE, 0, MAX_TRANSACTIONS));

    // Second batch: MAX_TRANSACTIONS..2*MAX_TRANSACTIONS-1
    int8_t batch2[MAX_TRANSACTIONS];
    memset(batch2, 2, sizeof(batch2)); // all aborted
    assert_int_equal(0, mvcc_buffor_save(batch2, TEST_FILE, MAX_TRANSACTIONS, MAX_TRANSACTIONS));

    // Load window ending at MAX_TRANSACTIONS + 5
    MVCCBuffor buf;
    int rc = mvcc_buffor_load(&buf, TEST_FILE, MAX_TRANSACTIONS + 5);
    assert_int_equal(0, rc);
    assert_int_equal(6, buf.start_txn); // MAX_TRANSACTIONS + 5 - MAX_TRANSACTIONS + 1 = 6

    // txn 6 was in batch1 -> committed (1)
    assert_int_equal(1, mvcc_buffor_get_status(&buf, 6));
    // txn MAX_TRANSACTIONS was in batch2 -> aborted (2)
    assert_int_equal(2, mvcc_buffor_get_status(&buf, MAX_TRANSACTIONS));
    // txn MAX_TRANSACTIONS + 5 -> aborted (2)
    assert_int_equal(2, mvcc_buffor_get_status(&buf, MAX_TRANSACTIONS + 5));
}

/* ── mvcc_buffor_load positions target_xid as last ───────────────────── */

static void test_load_target_is_last(void **state) {
    (void)state;
    int8_t statuses[MAX_TRANSACTIONS * 2];
    for (int i = 0; i < MAX_TRANSACTIONS * 2; i++) {
        statuses[i] = (int8_t)(i % 3); // cycle 0,1,2
    }
    assert_int_equal(0, mvcc_buffor_save(statuses, TEST_FILE, 0, MAX_TRANSACTIONS * 2));

    int32_t target = MAX_TRANSACTIONS * 2 - 1;
    MVCCBuffor buf;
    int rc = mvcc_buffor_load(&buf, TEST_FILE, target);
    assert_int_equal(0, rc);
    assert_int_equal(target - MAX_TRANSACTIONS + 1, buf.start_txn);

    // Last entry in buffer should be the target
    int8_t expected = (int8_t)(target % 3);
    assert_int_equal(expected, mvcc_buffor_get_status(&buf, target));
}

/* ── mvcc_buffor_load with small target (< MAX_TRANSACTIONS) ─────────── */

static void test_load_small_target(void **state) {
    (void)state;
    int8_t statuses[10];
    for (int i = 0; i < 10; i++) statuses[i] = (int8_t)(i + 1);
    assert_int_equal(0, mvcc_buffor_save(statuses, TEST_FILE, 0, 10));

    MVCCBuffor buf;
    int rc = mvcc_buffor_load(&buf, TEST_FILE, 5);
    assert_int_equal(0, rc);
    assert_int_equal(0, buf.start_txn); // max(0, 5 - 255) = 0
    assert_int_equal(6, mvcc_buffor_get_status(&buf, 5)); // statuses[5] = 6
}

/* ── mvcc_buffor_get_status out of range ─────────────────────────────── */

static void test_get_status_out_of_range(void **state) {
    (void)state;
    MVCCBuffor buf;
    mvcc_buffor_init(&buf);
    buf.start_txn = 100;

    assert_int_equal(-1, mvcc_buffor_get_status(&buf, 99));  // below range
    assert_int_equal(-1, mvcc_buffor_get_status(&buf, 100 + MAX_TRANSACTIONS)); // above range
    assert_int_equal(0,  mvcc_buffor_get_status(&buf, 100)); // in range, value=0
}

/* ── mvcc_buffor_load fails on missing file ──────────────────────────── */

static void test_load_missing_file(void **state) {
    (void)state;
    MVCCBuffor buf;
    int rc = mvcc_buffor_load(&buf, "/tmp/nonexistent_mvcc_test.bin", 10);
    assert_int_equal(-1, rc);
}

/* ── mvcc_buffor_load fails when target beyond file ──────────────────── */

static void test_load_target_beyond_file(void **state) {
    (void)state;
    int8_t statuses[10];
    memset(statuses, 1, sizeof(statuses));
    assert_int_equal(0, mvcc_buffor_save(statuses, TEST_FILE, 0, 10));

    MVCCBuffor buf;
    // target_xid = 500 -> start = 500-255 = 245, file has only 10 bytes
    int rc = mvcc_buffor_load(&buf, TEST_FILE, 500);
    assert_int_equal(-1, rc);
}

/* ── mvcc_buffor_flush ───────────────────────────────────────────────── */

static void test_flush_and_reload(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init(&mvcc);
    mvcc.txn_status[0] = 1;
    mvcc.txn_status[1] = 2;
    mvcc.txn_status[2] = 1;
    mvcc.txn_counter = 3;

    int rc = mvcc_buffor_flush(&mvcc, TEST_FILE, 0);
    assert_int_equal(0, rc);

    MVCCBuffor buf;
    rc = mvcc_buffor_load(&buf, TEST_FILE, 2);
    assert_int_equal(0, rc);
    assert_int_equal(1, mvcc_buffor_get_status(&buf, 0));
    assert_int_equal(2, mvcc_buffor_get_status(&buf, 1));
    assert_int_equal(1, mvcc_buffor_get_status(&buf, 2));
}

/* ── mvcc_get_txn_status: in-memory path ─────────────────────────────── */

static void test_unified_getter_in_memory(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init(&mvcc);
    mvcc.txn_status[0] = 1;
    mvcc.txn_status[5] = 2;
    mvcc.txn_counter = 10;

    MVCCBuffor buf;
    mvcc_buffor_init(&buf);

    assert_int_equal(1, mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, 0));
    assert_int_equal(2, mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, 5));
    assert_int_equal(0, mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, 9));
}

/* ── mvcc_get_txn_status: file path ──────────────────────────────────── */

static void test_unified_getter_from_file(void **state) {
    (void)state;
    // Write 300 statuses to file (beyond MAX_TRANSACTIONS)
    int8_t all[300];
    for (int i = 0; i < 300; i++) all[i] = (int8_t)((i % 2) + 1);
    assert_int_equal(0, mvcc_buffor_save(all, TEST_FILE, 0, 300));

    MVCC mvcc;
    mvcc_init(&mvcc);
    mvcc.txn_counter = 300;

    MVCCBuffor buf;
    mvcc_buffor_init(&buf);

    // xid=260 is beyond MAX_TRANSACTIONS -> loaded from file
    int8_t status = mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, 260);
    assert_int_equal((260 % 2) + 1, status);
}

/* ── mvcc_get_txn_status: negative xid ───────────────────────────────── */

static void test_unified_getter_negative_xid(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init(&mvcc);
    MVCCBuffor buf;
    mvcc_buffor_init(&buf);

    assert_int_equal(-1, mvcc_get_txn_status(&mvcc, &buf, TEST_FILE, -1));
}

/* ── multiple flushes accumulate in file ─────────────────────────────── */

static void test_multiple_flushes(void **state) {
    (void)state;
    MVCC mvcc;
    mvcc_init(&mvcc);

    // First batch: fill and flush
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        mvcc.txn_status[i] = 1; // committed
    }
    mvcc.txn_counter = MAX_TRANSACTIONS;
    assert_int_equal(0, mvcc_buffor_flush(&mvcc, TEST_FILE, 0));

    // Second batch: reset, fill, flush at offset MAX_TRANSACTIONS
    memset(mvcc.txn_status, 0, sizeof(mvcc.txn_status));
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        mvcc.txn_status[i] = 2; // aborted
    }
    mvcc.txn_counter = MAX_TRANSACTIONS * 2;
    assert_int_equal(0, mvcc_buffor_save(mvcc.txn_status, TEST_FILE,
                                          MAX_TRANSACTIONS, MAX_TRANSACTIONS));

    // Read back: txn 10 should be 1 (first batch), txn 260 should be 2 (second batch)
    MVCCBuffor buf;
    assert_int_equal(0, mvcc_buffor_load(&buf, TEST_FILE, 10));
    assert_int_equal(1, mvcc_buffor_get_status(&buf, 10));

    assert_int_equal(0, mvcc_buffor_load(&buf, TEST_FILE, MAX_TRANSACTIONS + 4));
    assert_int_equal(2, mvcc_buffor_get_status(&buf, MAX_TRANSACTIONS + 4));
}

/* ── runner ───────────────────────────────────────────────────────────── */

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_init_zeroes),
        cmocka_unit_test(test_create_allocates),
        cmocka_unit_test_setup_teardown(test_save_and_load_basic, setup, teardown),
        cmocka_unit_test_setup_teardown(test_save_at_offset, setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_target_is_last, setup, teardown),
        cmocka_unit_test_setup_teardown(test_load_small_target, setup, teardown),
        cmocka_unit_test(test_get_status_out_of_range),
        cmocka_unit_test(test_load_missing_file),
        cmocka_unit_test_setup_teardown(test_load_target_beyond_file, setup, teardown),
        cmocka_unit_test_setup_teardown(test_flush_and_reload, setup, teardown),
        cmocka_unit_test_setup_teardown(test_unified_getter_in_memory, setup, teardown),
        cmocka_unit_test_setup_teardown(test_unified_getter_from_file, setup, teardown),
        cmocka_unit_test(test_unified_getter_negative_xid),
        cmocka_unit_test_setup_teardown(test_multiple_flushes, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
