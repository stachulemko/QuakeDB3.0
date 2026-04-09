#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include "../memory-mgmt/block8kb.h"

static void make_block(Block8kb *b) {
    block8kb_init(b, 0, -1, 1, 0, 0, 0, 0);
}

static void make_tuple(Tuple *t, int32_t val) {
    int8_t bm[1] = {1};
    AllVar v[1];
    v[0] = all_var_from_int32(val);
    tuple_set(t, 1, 0, 1, 0, 35, 0, 0, bm, 1, v, 1);
}

static void test_block_init(void **state) {
    (void)state;
    Block8kb b;
    make_block(&b);
    assert_int_equal(0,  b.tuple_count);
    assert_int_equal(BLOCK_SIZE, b.usable_size);
}

static void test_block_add_tuple(void **state) {
    (void)state;
    Block8kb b;
    Tuple    t;
    make_block(&b);
    make_tuple(&t, 42);

    int rc = block8kb_add(&b, &t);
    assert_int_equal(0, rc);
    assert_int_equal(1, b.tuple_count);
}

static void test_block_full_returns_one_when_no_space(void **state) {
    (void)state;
    Block8kb b;
    Tuple    t;
    /* usable_size = 0 → anything should be "full" */
    block8kb_init(&b, BLOCK_SIZE, -1, 1, 0, 0, 0, 0);
    make_tuple(&t, 1);
    assert_int_equal(1, block8kb_full(&b, &t));
}

static void test_block_add_fails_when_full(void **state) {
    (void)state;
    Block8kb b;
    Tuple    t;
    block8kb_init(&b, BLOCK_SIZE, -1, 1, 0, 0, 0, 0);
    make_tuple(&t, 1);
    assert_int_equal(-1, block8kb_add(&b, &t));
}

static void test_block_used_increases_after_add(void **state) {
    (void)state;
    Block8kb b;
    Tuple    t;
    make_block(&b);
    int32_t before = block8kb_used(&b);

    make_tuple(&t, 99);
    block8kb_add(&b, &t);

    assert_true(block8kb_used(&b) > before);
}

static void test_block_marshal_size(void **state) {
    (void)state;
    Block8kb b;
    uint8_t  buf[BLOCK_SIZE];
    make_block(&b);
    int n = block8kb_marshal(buf, &b);
    assert_int_equal(BLOCK_SIZE, n);
}

static void test_block_marshal_starts_with_id_all_block(void **state) {
    (void)state;
    Block8kb b;
    uint8_t  buf[BLOCK_SIZE];
    int16_t  tag = 0;
    make_block(&b);
    block8kb_marshal(buf, &b);
    unmarshal_int16(&tag, buf);
    assert_int_equal(ID_ALL_BLOCK, tag);
}

static void test_block_marshal_unmarshal_roundtrip(void **state) {
    (void)state;
    Block8kb src, dst;
    uint8_t  buf[BLOCK_SIZE];
    Tuple    t;
    int      i;

    block8kb_init(&src, 0, 5, 10, 100, 0, 0, 1);
    for (i = 0; i < 3; i++) {
        make_tuple(&t, i * 10);
        block8kb_add(&src, &t);
    }

    block8kb_marshal(buf, &src);
    block8kb_unmarshal(&dst, buf);

    assert_int_equal(src.tuple_count,           dst.tuple_count);
    assert_int_equal(src.header.nextblock,      dst.header.nextblock);
    assert_int_equal(src.header.block_id,       dst.header.block_id);
    assert_int_equal(src.header.contain_toast,  dst.header.contain_toast);

    for (i = 0; i < 3; i++) {
        assert_int_equal(src.tuples[i].dnb.data[0].val.i32,
                         dst.tuples[i].dnb.data[0].val.i32);
    }
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_block_init),
        cmocka_unit_test(test_block_add_tuple),
        cmocka_unit_test(test_block_full_returns_one_when_no_space),
        cmocka_unit_test(test_block_add_fails_when_full),
        cmocka_unit_test(test_block_used_increases_after_add),
        cmocka_unit_test(test_block_marshal_size),
        cmocka_unit_test(test_block_marshal_starts_with_id_all_block),
        cmocka_unit_test(test_block_marshal_unmarshal_roundtrip),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
