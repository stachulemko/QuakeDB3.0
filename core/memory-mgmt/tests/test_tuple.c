#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include "../memory-mgmt/tuple.h"

/* Build a simple tuple with 2 int32 values */
static void make_simple_tuple(Tuple *t) {
    int8_t  bm[2] = {1, 1};
    AllVar  vals[2];
    vals[0] = all_var_from_int32(100);
    vals[1] = all_var_from_int32(200);
    tuple_set(t, 1, 0, 1, 0, 35, 0, 0, bm, 2, vals, 2);
}

static void test_tuple_init(void **state) {
    (void)state;
    Tuple t;
    tuple_init(&t);
    assert_int_equal(0, t.header.t_xmin);
    assert_int_equal(0, t.dnb.data_count);
    assert_int_equal(0, t.dnb.bit_map_count);
}

static void test_tuple_size_positive(void **state) {
    (void)state;
    Tuple t;
    make_simple_tuple(&t);
    /* must be > 0 and < BLOCK_SIZE */
    assert_true(tuple_size(&t) > 0);
    assert_true(tuple_size(&t) < BLOCK_SIZE);
}

static void test_tuple_marshal_unmarshal_roundtrip(void **state) {
    (void)state;
    Tuple   src, dst;
    uint8_t buf[BLOCK_SIZE];

    make_simple_tuple(&src);
    int n = tuple_marshal(buf, &src);
    assert_true(n > 0);

    tuple_unmarshal(&dst, buf, n);

    assert_int_equal(src.header.t_xmin,     dst.header.t_xmin);
    assert_int_equal(src.header.t_xmax,     dst.header.t_xmax);
    assert_int_equal(src.header.t_cid,      dst.header.t_cid);
    assert_int_equal(src.dnb.data_count,    dst.dnb.data_count);
    assert_int_equal(src.dnb.bit_map_count, dst.dnb.bit_map_count);
    assert_int_equal(src.dnb.data[0].val.i32, dst.dnb.data[0].val.i32);
    assert_int_equal(src.dnb.data[1].val.i32, dst.dnb.data[1].val.i32);
}

static void test_tuple_with_string_value(void **state) {
    (void)state;
    Tuple   src, dst;
    uint8_t buf[BLOCK_SIZE];
    int8_t  bm[2] = {1, 1};
    AllVar  vals[2];

    vals[0] = all_var_from_int32(42);
    vals[1] = all_var_from_string("hello");
    tuple_set(&src, 10, 0, 5, 0, 35, 0, 0, bm, 2, vals, 2);

    int n = tuple_marshal(buf, &src);
    tuple_unmarshal(&dst, buf, n);

    assert_int_equal(42, dst.dnb.data[0].val.i32);
    assert_string_equal("hello", dst.dnb.data[1].val.str);
}

static void test_tuple_with_mixed_types(void **state) {
    (void)state;
    Tuple   src, dst;
    uint8_t buf[BLOCK_SIZE];
    int8_t  bm[3] = {1, 1, 0};
    AllVar  vals[3];

    vals[0] = all_var_from_int32(-1);
    vals[1] = all_var_from_int64(99999999LL);
    vals[2] = all_var_from_string("QuakeDB");
    tuple_set(&src, 1, 2, 3, 4, 35, 1, 7, bm, 3, vals, 3);

    int n = tuple_marshal(buf, &src);
    tuple_unmarshal(&dst, buf, n);

    assert_int_equal(-1,          dst.dnb.data[0].val.i32);
    assert_int_equal(99999999LL,  dst.dnb.data[1].val.i64);
    assert_string_equal("QuakeDB",dst.dnb.data[2].val.str);
    assert_int_equal(1,           dst.header.t_xmin);
    assert_int_equal(1,           dst.header.null_bitmap);
}

static void test_tuple_marshal_starts_with_id_tuple(void **state) {
    (void)state;
    Tuple   t;
    uint8_t buf[BLOCK_SIZE];
    int16_t tag = 0;

    make_simple_tuple(&t);
    tuple_marshal(buf, &t);
    unmarshal_int16(&tag, buf);
    assert_int_equal(ID_TUPLE, tag);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_tuple_init),
        cmocka_unit_test(test_tuple_size_positive),
        cmocka_unit_test(test_tuple_marshal_unmarshal_roundtrip),
        cmocka_unit_test(test_tuple_with_string_value),
        cmocka_unit_test(test_tuple_with_mixed_types),
        cmocka_unit_test(test_tuple_marshal_starts_with_id_tuple),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
