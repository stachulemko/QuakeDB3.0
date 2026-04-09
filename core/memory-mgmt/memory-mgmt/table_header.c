#include "table_header.h"
#include <string.h>
#include <stdio.h>

void table_header_init(TableHeader *h) {
    memset(h, 0, sizeof(*h));
}

void table_header_set(TableHeader *h,
                      int32_t oid, int32_t block_id,
                      int32_t xmin, int32_t xmax,
                      int8_t contain_toast, int32_t num_columns,
                      int64_t owner, int8_t pg_namespace,
                      int32_t pg_constraint, int8_t rights,
                      int32_t free_space,
                      const int8_t *types,
                      const int8_t *types_allow_null,
                      const char (*col_names)[MAX_COL_NAME_LEN]) {
    int32_t i;
    int32_t nc = (num_columns <= MAX_COLUMNS) ? num_columns : MAX_COLUMNS;

    h->oid           = oid;
    h->block_id      = block_id;
    h->xmin          = xmin;
    h->xmax          = xmax;
    h->contain_toast = contain_toast;
    h->num_columns   = nc;
    h->owner         = owner;
    h->pg_namespace  = pg_namespace;
    h->pg_constraint = pg_constraint;
    h->rights        = rights;
    h->free_space    = free_space;

    for (i = 0; i < nc; i++) {
        h->types[i]            = types[i];
        h->types_allow_null[i] = types_allow_null[i];
        strncpy(h->col_names[i], col_names[i], MAX_COL_NAME_LEN - 1);
        h->col_names[i][MAX_COL_NAME_LEN - 1] = '\0';
    }
}

int table_header_marshal(uint8_t buf[BLOCK_SIZE], const TableHeader *h) {
    int32_t i, filled;
    int off = 0;

    memset(buf, 0, BLOCK_SIZE);

    /* --- stałe pola --- */
    off += marshal_int16(buf + off, ID_TABLE_HEADER);
    off += marshal_int32(buf + off, h->oid);
    off += marshal_int32(buf + off, h->block_id);
    off += marshal_int32(buf + off, h->xmin);
    off += marshal_int32(buf + off, h->xmax);
    off += marshal_int8 (buf + off, h->contain_toast);
    off += marshal_int32(buf + off, h->num_columns);
    off += marshal_int64(buf + off, h->owner);
    off += marshal_int8 (buf + off, h->pg_namespace);
    off += marshal_int32(buf + off, h->pg_constraint);
    off += marshal_int8 (buf + off, h->rights);
    off += marshal_int32(buf + off, h->free_space);

    /* --- sekcja typów --- */
    off += marshal_int16(buf + off, ID_TYPE_TABLE);
    off += marshal_int32(buf + off, h->num_columns);
    for (i = 0; i < h->num_columns; i++)
        off += marshal_int8(buf + off, h->types[i]);

    /* --- sekcja allow_null --- */
    off += marshal_int16(buf + off, ID_DATA_NULL);
    off += marshal_int32(buf + off, h->num_columns);
    for (i = 0; i < h->num_columns; i++)
        off += marshal_int8(buf + off, h->types_allow_null[i]);

    /* --- sekcja nazw kolumn --- */
    off += marshal_int16(buf + off, ID_COL_NAMES);
    off += marshal_int32(buf + off, h->num_columns);
    for (i = 0; i < h->num_columns; i++) {
        int32_t slen = (int32_t)strlen(h->col_names[i]);
        off += marshal_int16(buf + off, ID_STRING);
        off += marshal_int32(buf + off, slen);
        off += marshal_string(buf + off, h->col_names[i], slen);
    }

    filled = off;
    /* reszta bufora to zera (memset na początku) */
    return filled;
}

void table_header_unmarshal(TableHeader *h, const uint8_t buf[BLOCK_SIZE]) {
    int16_t tag;
    int32_t i, nc;
    int off = 0;

    table_header_init(h);

    unmarshal_int16(&tag, buf + off); off += 2;
    if (tag != ID_TABLE_HEADER) return;

    unmarshal_int32(&h->oid,           buf + off); off += 4;
    unmarshal_int32(&h->block_id,      buf + off); off += 4;
    unmarshal_int32(&h->xmin,          buf + off); off += 4;
    unmarshal_int32(&h->xmax,          buf + off); off += 4;
    unmarshal_int8 (&h->contain_toast, buf + off); off += 1;
    unmarshal_int32(&h->num_columns,   buf + off); off += 4;
    unmarshal_int64(&h->owner,         buf + off); off += 8;
    unmarshal_int8 (&h->pg_namespace,  buf + off); off += 1;
    unmarshal_int32(&h->pg_constraint, buf + off); off += 4;
    unmarshal_int8 (&h->rights,        buf + off); off += 1;
    unmarshal_int32(&h->free_space,    buf + off); off += 4;

    nc = (h->num_columns <= MAX_COLUMNS) ? h->num_columns : MAX_COLUMNS;

    /* typy */
    unmarshal_int16(&tag, buf + off); off += 2;
    if (tag != ID_TYPE_TABLE) return;
    unmarshal_int32(&i, buf + off); off += 4; /* count (ignorujemy, mamy nc) */
    for (i = 0; i < nc; i++) {
        unmarshal_int8(&h->types[i], buf + off); off += 1;
    }

    /* allow_null */
    unmarshal_int16(&tag, buf + off); off += 2;
    if (tag != ID_DATA_NULL) return;
    unmarshal_int32(&i, buf + off); off += 4;
    for (i = 0; i < nc; i++) {
        unmarshal_int8(&h->types_allow_null[i], buf + off); off += 1;
    }

    /* nazwy kolumn */
    unmarshal_int16(&tag, buf + off); off += 2;
    if (tag != ID_COL_NAMES) return;
    unmarshal_int32(&i, buf + off); off += 4;
    for (i = 0; i < nc; i++) {
        int16_t stag;
        int32_t slen;
        unmarshal_int16(&stag, buf + off); off += 2;
        unmarshal_int32(&slen, buf + off); off += 4;
        if (stag != ID_STRING) return;
        unmarshal_string(h->col_names[i], MAX_COL_NAME_LEN,
                         buf + off, slen);
        off += slen;
    }
}

void table_header_show(const TableHeader *h) {
    int32_t i;
    printf("oid:           %d\n",  h->oid);
    printf("block_id:      %d\n",  h->block_id);
    printf("xmin:          %d\n",  h->xmin);
    printf("xmax:          %d\n", h->xmax);
    printf("contain_toast: %d\n",  h->contain_toast);
    printf("num_columns:   %d\n",  h->num_columns);
    printf("owner:         %lld\n",(long long)h->owner);
    printf("pg_namespace:  %d\n",  h->pg_namespace);
    printf("pg_constraint: %d\n",  h->pg_constraint);
    printf("rights:        %d\n",  h->rights);
    printf("free_space:    %d\n",  h->free_space);
    printf("types:         ");
    for (i = 0; i < h->num_columns; i++) printf("%d ", h->types[i]);
    printf("\n");
    printf("allow_null:    ");
    for (i = 0; i < h->num_columns; i++) printf("%d ", h->types_allow_null[i]);
    printf("\n");
    printf("col_names:     ");
    for (i = 0; i < h->num_columns; i++) printf("[%s] ", h->col_names[i]);
    printf("\n");
}
