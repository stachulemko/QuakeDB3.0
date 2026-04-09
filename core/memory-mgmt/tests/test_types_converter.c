#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include "../memory-mgmt/types_converter.h"

/* ---- marshal / unmarshal int8 ---- */

static void test_marshal_unmarshal_int8(void **state) {
    (void)state;
    uint8_t buf[1];
    int8_t  out = 0;

    marshal_int8(buf, 42);
    unmarshal_int8(&out, buf);
    assert_int_equal(42, out);

    marshal_int8(buf, -1);
    unmarshal_int8(&out, buf);
    assert_int_equal(-1, out);
}

/* ---- marshal / unmarshal int16 ---- */

static void test_marshal_unmarshal_int16(void **state) {
    (void)state;
    uint8_t  buf[2];
    int16_t  out = 0;

    marshal_int16(buf, 0x1234);
    /* little-endian: buf[0]=0x34, buf[1]=0x12 */
    assert_int_equal(0x34, buf[0]);
    assert_int_equal(0x12, buf[1]);
    unmarshal_int16(&out, buf);
    assert_int_equal(0x1234, out);

    marshal_int16(buf, -1);
    unmarshal_int16(&out, buf);
    assert_int_equal(-1, out);
}

/* ---- marshal / unmarshal int32 ---- */

static void test_marshal_unmarshal_int32(void **state) {
    (void)state;
    uint8_t buf[4];
    int32_t out = 0;

    marshal_int32(buf, 0x12345678);
    assert_int_equal(0x78, buf[0]);
    assert_int_equal(0x56, buf[1]);
    assert_int_equal(0x34, buf[2]);
    assert_int_equal(0x12, buf[3]);
    unmarshal_int32(&out, buf);
    assert_int_equal(0x12345678, out);

    marshal_int32(buf, -100);
    unmarshal_int32(&out, buf);
    assert_int_equal(-100, out);
}

/* ---- marshal / unmarshal int64 ---- */

static void test_marshal_unmarshal_int64(void **state) {
    (void)state;
    uint8_t buf[8];
    int64_t out = 0;
    int64_t val = 0x0102030405060708LL;

    marshal_int64(buf, val);
    assert_int_equal(0x08, buf[0]);
    assert_int_equal(0x07, buf[1]);
    unmarshal_int64(&out, buf);
    assert_int_equal(val, out);

    marshal_int64(buf, -1LL);
    unmarshal_int64(&out, buf);
    assert_int_equal(-1LL, out);
}

/* ---- marshal / unmarshal bool ---- */

static void test_marshal_unmarshal_bool(void **state) {
    (void)state;
    uint8_t buf[1];
    int8_t  out = 0;

    marshal_bool(buf, 1);
    assert_int_equal(1, buf[0]);
    unmarshal_bool(&out, buf);
    assert_int_equal(1, out);

    marshal_bool(buf, 0);
    unmarshal_bool(&out, buf);
    assert_int_equal(0, out);

    marshal_bool(buf, 99);   /* any non-zero → 1 */
    unmarshal_bool(&out, buf);
    assert_int_equal(1, out);
}

/* ---- marshal / unmarshal string ---- */

static void test_marshal_unmarshal_string(void **state) {
    (void)state;
    uint8_t buf[64];
    char    out[64];

    int n = marshal_string(buf, "hello", 5);
    assert_int_equal(5, n);

    unmarshal_string(out, sizeof(out), buf, 5);
    assert_string_equal("hello", out);
}

static void test_marshal_string_truncates(void **state) {
    (void)state;
    uint8_t buf[64];
    char    out[4]; /* max_len = 4, so max 3 chars + \0 */

    marshal_string(buf, "abcdef", 6);
    unmarshal_string(out, 4, buf, 6);
    assert_string_equal("abc", out);
}

/* ---- return values ---- */

static void test_marshal_returns_correct_sizes(void **state) {
    (void)state;
    uint8_t buf[64];
    assert_int_equal(1, marshal_int8 (buf, 0));
    assert_int_equal(2, marshal_int16(buf, 0));
    assert_int_equal(4, marshal_int32(buf, 0));
    assert_int_equal(8, marshal_int64(buf, 0));
    assert_int_equal(1, marshal_bool (buf, 0));
    assert_int_equal(3, marshal_string(buf, "abc", 3));
}

/* ---- test suite ---- */

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_marshal_unmarshal_int8),
        cmocka_unit_test(test_marshal_unmarshal_int16),
        cmocka_unit_test(test_marshal_unmarshal_int32),
        cmocka_unit_test(test_marshal_unmarshal_int64),
        cmocka_unit_test(test_marshal_unmarshal_bool),
        cmocka_unit_test(test_marshal_unmarshal_string),
        cmocka_unit_test(test_marshal_string_truncates),
        cmocka_unit_test(test_marshal_returns_correct_sizes),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
