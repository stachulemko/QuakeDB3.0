#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "../xid32Recycler.h"

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
    /* 0xFFFFFFFE + 1 == -1 (no xmax) — skipped, the counter wraps to 0 */
    assert_int_equal(0, xidNext((int32_t)0xFFFFFFFEu));
    assert_false(xidIsNormal(XID_NONE_NEG));
    assert_false(xidIsNormal(XID_FROZEN));
    assert_true(xidIsNormal(0));
}

static void test_distance(void **state) {
    (void)state;
    assert_int_equal(10, xidDistance(5, 15));
    assert_int_equal(21, xidDistance((int32_t)0xFFFFFFF0u, 5));
}

int main(void) {
    const struct CMUnitTest tests[] = {
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