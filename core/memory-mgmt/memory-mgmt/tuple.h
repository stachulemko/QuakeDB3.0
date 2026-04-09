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

#endif
