#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include "../memory-mgmt/universal_block.h"

/* helper — tworzy zserializowany blok danych */
static void make_block_buf(uint8_t buf[BLOCK_SIZE]) {
    Block8kb b;
    block8kb_init(&b, 0, 1, 2, 0, 0, 0, 0);
    block8kb_marshal(buf, &b);
}

/* helper — tworzy zserializowany nagłówek tabeli */
static void make_header_buf(uint8_t buf[BLOCK_SIZE]) {
    TableHeader h;
    table_header_init(&h);
    table_header_marshal(buf, &h);
}

/* --- testy --- */

static void test_create_from_block_buf(void **state) {
    (void)state;
    uint8_t buf[BLOCK_SIZE];
    make_block_buf(buf);

    UniversalBlock *ub = createUniversalBlock(buf);
    assert_non_null(ub);
    assert_non_null(ub->block);
    assert_null(ub->header);

    free(ub->block);
    free(ub);
}

static void test_create_from_header_buf(void **state) {
    (void)state;
    uint8_t buf[BLOCK_SIZE];
    make_header_buf(buf);

    UniversalBlock *ub = createUniversalBlock(buf);
    assert_non_null(ub);
    assert_non_null(ub->header);
    assert_null(ub->block);

    free(ub->header);
    free(ub);
}

static void test_create_from_unknown_tag_returns_null(void **state) {
    (void)state;
    uint8_t buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE); /* tag = 0, nieznany */

    UniversalBlock *ub = createUniversalBlock(buf);
    assert_null(ub);
}

static void test_block_data_preserved(void **state) {
    (void)state;
    uint8_t buf[BLOCK_SIZE];
    Block8kb src;

    block8kb_init(&src, 0, 7, 42, 100, 0, 0, 1);
    block8kb_marshal(buf, &src);

    UniversalBlock *ub = createUniversalBlock(buf);
    assert_non_null(ub);
    assert_non_null(ub->block);
    assert_int_equal(42, ub->block->header.block_id);
    assert_int_equal(7,  ub->block->header.nextblock);
    assert_int_equal(1,  ub->block->header.contain_toast);

    free(ub->block);
    free(ub);
}

static void test_header_data_preserved(void **state) {
    (void)state;
    uint8_t buf[BLOCK_SIZE];
    TableHeader h;

    table_header_init(&h);
    h.oid      = 999;
    h.block_id = 5;
    h.xmin     = 10;
    h.xmax     = 20;
    table_header_marshal(buf, &h);

    UniversalBlock *ub = createUniversalBlock(buf);
    assert_non_null(ub);
    assert_non_null(ub->header);
    assert_int_equal(999, ub->header->oid);
    assert_int_equal(5,   ub->header->block_id);
    assert_int_equal(10,  ub->header->xmin);
    assert_int_equal(20,  ub->header->xmax);

    free(ub->header);
    free(ub);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_create_from_block_buf),
        cmocka_unit_test(test_create_from_header_buf),
        cmocka_unit_test(test_create_from_unknown_tag_returns_null),
        cmocka_unit_test(test_block_data_preserved),
        cmocka_unit_test(test_header_data_preserved),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
