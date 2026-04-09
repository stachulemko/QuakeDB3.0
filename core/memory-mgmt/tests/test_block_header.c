#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "../memory-mgmt/block_header.h"

static void test_init_defaults(void **state) {
    (void)state;
    BlockHeader h;
    block_header_init(&h);
    assert_int_equal(-1, h.nextblock);
    assert_int_equal(-1, h.block_id);
    assert_int_equal(0,  h.pd_lsn);
    assert_int_equal(0,  h.pd_checksum);
    assert_int_equal(0,  h.pd_flags);
    assert_int_equal(0,  h.contain_toast);
}

static void test_set_and_get(void **state) {
    (void)state;
    BlockHeader h;
    block_header_set(&h, 5, 10, 100, 200, 300, 1);
    assert_int_equal(5,   h.nextblock);
    assert_int_equal(10,  h.block_id);
    assert_int_equal(100, h.pd_lsn);
    assert_int_equal(200, h.pd_checksum);
    assert_int_equal(300, h.pd_flags);
    assert_int_equal(1,   h.contain_toast);
}

static void test_marshal_size(void **state) {
    (void)state;
    BlockHeader h;
    uint8_t buf[BLOCK_HEADER_SIZE];
    block_header_init(&h);
    int n = block_header_marshal(buf, &h);
    assert_int_equal(BLOCK_HEADER_SIZE, n);
}

static void test_marshal_unmarshal_roundtrip(void **state) {
    (void)state;
    BlockHeader src, dst;
    uint8_t buf[BLOCK_HEADER_SIZE];

    block_header_set(&src, 7, 13, 42, 1234, 5678, 1);
    block_header_marshal(buf, &src);
    block_header_unmarshal(&dst, buf);

    assert_int_equal(src.nextblock,     dst.nextblock);
    assert_int_equal(src.block_id,      dst.block_id);
    assert_int_equal(src.pd_lsn,        dst.pd_lsn);
    assert_int_equal(src.pd_checksum,   dst.pd_checksum);
    assert_int_equal(src.pd_flags,      dst.pd_flags);
    assert_int_equal(src.contain_toast, dst.contain_toast);
}

static void test_negative_values_roundtrip(void **state) {
    (void)state;
    BlockHeader src, dst;
    uint8_t buf[BLOCK_HEADER_SIZE];

    block_header_set(&src, -1, -1, -100, -1, -1, 0);
    block_header_marshal(buf, &src);
    block_header_unmarshal(&dst, buf);

    assert_int_equal(-1,   dst.nextblock);
    assert_int_equal(-1,   dst.block_id);
    assert_int_equal(-100, dst.pd_lsn);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_init_defaults),
        cmocka_unit_test(test_set_and_get),
        cmocka_unit_test(test_marshal_size),
        cmocka_unit_test(test_marshal_unmarshal_roundtrip),
        cmocka_unit_test(test_negative_values_roundtrip),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
