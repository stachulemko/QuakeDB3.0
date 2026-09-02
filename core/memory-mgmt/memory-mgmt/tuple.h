#ifndef TUPLE_H
#define TUPLE_H

/*
 * tuple.h — data tuple (C struct replacing the tuple class)
 *
 * Binary layout:
 *   ID_TUPLE  (int16)
 *   size      (int32)  — payload: header + data_null_bitmap
 *   TupleHeader       (35 B)
 *   DataNullBitmap    (variable)
 */

#include <stdint.h>
#include "tuple_header.h"
#include "data_null_bitmap.h"

typedef struct {
    TupleHeader    header;
    DataNullBitmap dnb;
} Tuple;

void    tuple_init(Tuple *t);
void    tuple_set (Tuple *t,
                   int32_t xmin, int32_t xmax, int32_t cid,
                   int32_t infomask, int16_t hoff, int8_t bitmap,
                   int64_t oid,
                   const int8_t *bm, int32_t bm_count,
                   const AllVar *vals, int32_t val_count);

/* Estimated in-block size of the tuple */
int32_t tuple_size    (const Tuple *t);

/* Serialization — writes to buf, returns bytes written */
int     tuple_marshal  (uint8_t *buf, const Tuple *t);
/* Deserialization */
void    tuple_unmarshal(Tuple *t, const uint8_t *buf, int32_t len);

void    tuple_show(const Tuple *t);

static inline int8_t evaluateTuples(Tuple t1, Tuple t2) {
    if (t1.dnb.data_count != t2.dnb.data_count) return 0;
    if (t1.dnb.bit_map_count != t2.dnb.bit_map_count) return 0;

    for (int i = 0; i < t1.dnb.bit_map_count; i++) {
        if (t1.dnb.bit_map[i] != t2.dnb.bit_map[i]) return 0;
    }

    for (int i = 0; i < t1.dnb.data_count; i++) {
        if (t1.dnb.data[i].type != t2.dnb.data[i].type) return 0;
        if (t1.dnb.data[i].str_len != t2.dnb.data[i].str_len) return 0;

        switch (t1.dnb.data[i].type) {
            case ID_INT32:
                if (t1.dnb.data[i].val.i32 != t2.dnb.data[i].val.i32) return 0;
                break;
            case ID_INT64:
                if (t1.dnb.data[i].val.i64 != t2.dnb.data[i].val.i64) return 0;
                break;
            case ID_STRING:
                if (strcmp(t1.dnb.data[i].val.str, t2.dnb.data[i].val.str) != 0) return 0;
                break;
            default:
                return 0;
        }
    }

    return 1;
}


#endif
