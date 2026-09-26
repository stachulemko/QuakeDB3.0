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

/* slot marked INFOMASK_UNUSED the way vacuum does it: header only, no data */
static void make_unused(Tuple *t) {
    t->header.t_infomask = INFOMASK_UNUSED;
    t->header.t_cid = 0;
    dnb_init(&t->dnb);
}

static void test_block_add_reuses_unused_slot(void **state) {
    (void)state;
    Block8kb b;
    Tuple    t;
    int      i;
    make_block(&b);
    for (i = 0; i < 3; i++) {
        make_tuple(&t, i);
        block8kb_add(&b, &t);
    }
    make_unused(&b.tuples[1]);

    make_tuple(&t, 777);
    assert_int_equal(1, block8kb_add(&b, &t));
    assert_int_equal(3, b.tuple_count);
    assert_int_equal(777, b.tuples[1].dnb.data[0].val.i32);
    assert_int_equal(0,   b.tuples[1].header.t_infomask & INFOMASK_UNUSED);
    /* the other tuples (their TIDs) are unchanged */
    assert_int_equal(0, b.tuples[0].dnb.data[0].val.i32);
    assert_int_equal(2, b.tuples[2].dnb.data[0].val.i32);
}

static void test_block_add_appends_without_unused_slot(void **state) {
    (void)state;
    Block8kb b;
    Tuple    t;
    make_block(&b);
    make_tuple(&t, 1);
    block8kb_add(&b, &t);
    block8kb_add(&b, &t);

    make_tuple(&t, 2);
    assert_int_equal(2, block8kb_add(&b, &t));
    assert_int_equal(3, b.tuple_count);
}

static void test_block_add_takes_first_unused_slot(void **state) {
    (void)state;
    Block8kb b;
    Tuple    t;
    int      i;
    make_block(&b);
    for (i = 0; i < 4; i++) {
        make_tuple(&t, i);
        block8kb_add(&b, &t);
    }
    make_unused(&b.tuples[3]);
    make_unused(&b.tuples[1]);

    make_tuple(&t, 50);
    assert_int_equal(1, block8kb_add(&b, &t));
    make_tuple(&t, 51);
    assert_int_equal(3, block8kb_add(&b, &t));
    make_tuple(&t, 52);
    assert_int_equal(4, block8kb_add(&b, &t));
}

static void test_block_unused_slot_frees_space(void **state) {
    (void)state;
    Block8kb b;
    Tuple    t;
    make_block(&b);
    make_tuple(&t, 1);
    block8kb_add(&b, &t);
    int32_t before = block8kb_used(&b);

    make_unused(&b.tuples[0]);
    assert_true(block8kb_used(&b) < before);
}

static void test_block_unused_slot_survives_roundtrip(void **state) {
    (void)state;
    Block8kb src, dst;
    uint8_t  buf[BLOCK_SIZE];
    Tuple    t;
    int      i;
    make_block(&src);
    for (i = 0; i < 3; i++) {
        make_tuple(&t, i * 10);
        block8kb_add(&src, &t);
    }
    make_unused(&src.tuples[1]);

    block8kb_marshal(buf, &src);
    block8kb_unmarshal(&dst, buf);

    /* the tombstone keeps its position — tuple 2's index does not shift */
    assert_int_equal(3, dst.tuple_count);
    assert_true(dst.tuples[1].header.t_infomask & INFOMASK_UNUSED);
    assert_int_equal(20, dst.tuples[2].dnb.data[0].val.i32);

    /* usable_size is not serialized — unmarshal leaves 0 */
    dst.usable_size = src.usable_size;
    make_tuple(&t, 5);
    assert_int_equal(1, block8kb_add(&dst, &t));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_block_add_reuses_unused_slot),
        cmocka_unit_test(test_block_add_appends_without_unused_slot),
        cmocka_unit_test(test_block_add_takes_first_unused_slot),
        cmocka_unit_test(test_block_unused_slot_frees_space),
        cmocka_unit_test(test_block_unused_slot_survives_roundtrip),
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
