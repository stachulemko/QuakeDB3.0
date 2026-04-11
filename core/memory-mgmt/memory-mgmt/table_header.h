#ifndef TABLE_HEADER_H
#define TABLE_HEADER_H

/*
 * table_header.h — table header (C struct replacing the tableHeader class)
 * Fixed-size arrays instead of vector<string> and vector<int8_t>.
 *
 * Serialized to a BLOCK_SIZE (8192 B) block; remainder is zero-padded.
 */

#include <stdint.h>
#include "config.h"
#include "type_ids.h"
#include "types_converter.h"

typedef struct {
    int32_t oid;
    int32_t block_id;
    int32_t xmin;
    int32_t xmax;
    int8_t  contain_toast;
    int32_t num_columns;
    int64_t owner;
    int8_t  pg_namespace;
    int32_t pg_constraint;
    int8_t  rights;
    int32_t free_space;

    /* Column types: ID_INT32 / ID_INT64 / ID_STRING */
    int8_t  types[MAX_COLUMNS];
    /* Whether each column allows NULL: 0 = no, 1 = yes */
    int8_t  types_allow_null[MAX_COLUMNS];
    /* Column names — fixed-length char array instead of std::string */
    char    col_names[MAX_COLUMNS][MAX_COL_NAME_LEN];
} TableHeader;


static inline void create_table_headerM(TableHeader **h) {
    *h = (TableHeader *)malloc(sizeof(TableHeader));
}
static inline void create_table_headerC(TableHeader **h) {
    *h = (TableHeader *)calloc(1, sizeof(TableHeader));
}

void table_header_init(TableHeader *h);
void table_header_set (TableHeader *h,
                       int32_t oid, int32_t block_id,
                       int32_t xmin, int32_t xmax,
                       int8_t contain_toast, int32_t num_columns,
                       int64_t owner, int8_t pg_namespace,
                       int32_t pg_constraint, int8_t rights,
                       int32_t free_space,
                       const int8_t *types,
                       const int8_t *types_allow_null,
                       const char (*col_names)[MAX_COL_NAME_LEN]);

/* Serialization — writes to buf[BLOCK_SIZE], returns bytes used */
int  table_header_marshal  (uint8_t buf[BLOCK_SIZE], const TableHeader *h);
/* Deserialization — reads from buf[BLOCK_SIZE] */
void table_header_unmarshal(TableHeader *h, const uint8_t buf[BLOCK_SIZE]);

void table_header_show(const TableHeader *h);

#endif
