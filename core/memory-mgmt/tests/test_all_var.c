#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include "../memory-mgmt/all_var.h"

static void test_from_int32(void **state) {
    (void)state;
    AllVar v = all_var_from_int32(42);
    assert_int_equal(ID_INT32, v.type);
    assert_int_equal(42, v.val.i32);
    assert_int_equal(4, all_var_size(&v));
}

static void test_from_int64(void **state) {
    (void)state;
    AllVar v = all_var_from_int64(0x123456789ABCLL);
    assert_int_equal(ID_INT64, v.type);
    assert_int_equal(0x123456789ABCLL, v.val.i64);
    assert_int_equal(8, all_var_size(&v));
}

static void test_from_string(void **state) {
    (void)state;
    AllVar v = all_var_from_string("hello");
    assert_int_equal(ID_STRING, v.type);
    assert_int_equal(5, v.str_len);
    assert_string_equal("hello", v.val.str);
    assert_int_equal(5, all_var_size(&v));
}

static void test_string_truncation(void **state) {
    (void)state;
    /* MAX_STR_LEN - 1 characters is the max */
    char long_str[MAX_STR_LEN + 10];
    memset(long_str, 'x', sizeof(long_str) - 1);
    long_str[sizeof(long_str) - 1] = '\0';

    AllVar v = all_var_from_string(long_str);
    assert_int_equal(MAX_STR_LEN - 1, v.str_len);
    assert_int_equal('\0', v.val.str[MAX_STR_LEN - 1]);
}

static void test_marshal_int32_roundtrip(void **state) {
    (void)state;
    AllVar src = all_var_from_int32(-999);
    AllVar dst;
    uint8_t buf[16];

    int n = all_var_marshal(buf, &src);
    assert_int_equal(4, n);
    all_var_unmarshal(&dst, ID_INT32, buf, 4);
    assert_int_equal(-999, dst.val.i32);
}

static void test_marshal_int64_roundtrip(void **state) {
    (void)state;
    AllVar src = all_var_from_int64(9999999999LL);
    AllVar dst;
    uint8_t buf[16];

    int n = all_var_marshal(buf, &src);
    assert_int_equal(8, n);
    all_var_unmarshal(&dst, ID_INT64, buf, 8);
    assert_int_equal(9999999999LL, dst.val.i64);
}

static void test_marshal_string_roundtrip(void **state) {
    (void)state;
    AllVar src = all_var_from_string("QuakeDB");
    AllVar dst;
    uint8_t buf[64];

    int n = all_var_marshal(buf, &src);
    assert_int_equal(7, n);
    all_var_unmarshal(&dst, ID_STRING, buf, 7);
    assert_string_equal("QuakeDB", dst.val.str);
    assert_int_equal(7, dst.str_len);
}

static void test_check_types_match(void **state) {
    (void)state;
    int8_t  col_types[3] = { ID_INT32, ID_INT64, ID_STRING };
    AllVar  values[3];
    values[0] = all_var_from_int32(1);
    values[1] = all_var_from_int64(2LL);
    values[2] = all_var_from_string("x");

    assert_int_equal(1, all_var_check_types(col_types, 3, values, 3));
}

static void test_check_types_mismatch(void **state) {
    (void)state;
    int8_t col_types[2] = { ID_INT32, ID_STRING };
    AllVar values[2];
    values[0] = all_var_from_int32(1);
    values[1] = all_var_from_int64(2LL);  /* wrong type */

    assert_int_equal(0, all_var_check_types(col_types, 2, values, 2));
}

static void test_check_types_count_mismatch(void **state) {
    (void)state;
    int8_t col_types[2] = { ID_INT32, ID_INT32 };
    AllVar values[1];
    values[0] = all_var_from_int32(1);

    assert_int_equal(0, all_var_check_types(col_types, 2, values, 1));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_from_int32),
        cmocka_unit_test(test_from_int64),
        cmocka_unit_test(test_from_string),
        cmocka_unit_test(test_string_truncation),
        cmocka_unit_test(test_marshal_int32_roundtrip),
        cmocka_unit_test(test_marshal_int64_roundtrip),
        cmocka_unit_test(test_marshal_string_roundtrip),
        cmocka_unit_test(test_check_types_match),
        cmocka_unit_test(test_check_types_mismatch),
        cmocka_unit_test(test_check_types_count_mismatch),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
