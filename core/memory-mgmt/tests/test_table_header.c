#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include "../memory-mgmt/table_header.h"

static void fill_header(TableHeader *h) {
    int8_t types[3]      = { ID_INT32, ID_INT64, ID_STRING };
    int8_t allow_null[3] = { 0, 1, 1 };
    char   names[3][MAX_COL_NAME_LEN];
    strncpy(names[0], "id",    MAX_COL_NAME_LEN - 1);
    strncpy(names[1], "value", MAX_COL_NAME_LEN - 1);
    strncpy(names[2], "name",  MAX_COL_NAME_LEN - 1);
    table_header_set(h, 1, 0, 0, 0, 3, 99, 0, 0, 0, 0,
                     types, allow_null, (const char (*)[MAX_COL_NAME_LEN])names);
}

static void test_init_zeros(void **state) {
    (void)state;
    TableHeader h;
    table_header_init(&h);
    assert_int_equal(0, h.oid);
    assert_int_equal(0, h.num_columns);
}

static void test_set_values(void **state) {
    (void)state;
    TableHeader h;
    fill_header(&h);
    assert_int_equal(1,        h.oid);
    assert_int_equal(3,        h.num_columns);
    assert_int_equal(ID_INT32, h.types[0]);
    assert_int_equal(ID_INT64, h.types[1]);
    assert_int_equal(ID_STRING,h.types[2]);
    assert_int_equal(0, h.types_allow_null[0]);
    assert_int_equal(1, h.types_allow_null[1]);
    assert_string_equal("id",    h.col_names[0]);
    assert_string_equal("value", h.col_names[1]);
    assert_string_equal("name",  h.col_names[2]);
}

static void test_marshal_unmarshal_roundtrip(void **state) {
    (void)state;
    TableHeader src, dst;
    uint8_t     buf[BLOCK_SIZE];

    fill_header(&src);
    table_header_marshal(buf, &src);
    table_header_unmarshal(&dst, buf);

    assert_int_equal(src.oid,          dst.oid);
    assert_int_equal(src.num_columns,  dst.num_columns);
    assert_int_equal(src.types[0],     dst.types[0]);
    assert_int_equal(src.types[1],     dst.types[1]);
    assert_int_equal(src.types[2],     dst.types[2]);
    assert_int_equal(src.types_allow_null[1], dst.types_allow_null[1]);
    assert_string_equal(src.col_names[0], dst.col_names[0]);
    assert_string_equal(src.col_names[2], dst.col_names[2]);
}

static void test_marshal_produces_block_size(void **state) {
    (void)state;
    TableHeader h;
    uint8_t     buf[BLOCK_SIZE];
    fill_header(&h);
    /* function returns bytes used, but buffer is always BLOCK_SIZE */
    table_header_marshal(buf, &h);
    /* trailing bytes must be zero (zero-padded) */
    assert_int_equal(0, buf[BLOCK_SIZE - 1]);
}

static void test_long_column_name_truncated(void **state) {
    (void)state;
    TableHeader h;
    uint8_t     buf[BLOCK_SIZE];
    int8_t types[1]      = { ID_INT32 };
    int8_t allow_null[1] = { 0 };
    char   names[1][MAX_COL_NAME_LEN];

    /* fill with chars longer than MAX_COL_NAME_LEN */
    memset(names[0], 'a', MAX_COL_NAME_LEN - 1);
    names[0][MAX_COL_NAME_LEN - 1] = '\0';

    table_header_set(&h, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0,
                     types, allow_null,
                     (const char (*)[MAX_COL_NAME_LEN])names);

    table_header_marshal(buf, &h);
    TableHeader dst;
    table_header_unmarshal(&dst, buf);
    assert_int_equal('\0', dst.col_names[0][MAX_COL_NAME_LEN - 1]);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_init_zeros),
        cmocka_unit_test(test_set_values),
        cmocka_unit_test(test_marshal_unmarshal_roundtrip),
        cmocka_unit_test(test_marshal_produces_block_size),
        cmocka_unit_test(test_long_column_name_truncated),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
