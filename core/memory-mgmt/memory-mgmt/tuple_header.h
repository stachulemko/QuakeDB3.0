#ifndef TUPLE_HEADER_H
#define TUPLE_HEADER_H

/*
 * tuple_header.h — tuple header (C struct replacing the HeaderTuple class)
 *
 * Binary layout (35 bytes):
 *   t_xmin        int32_t   4 B  — transaction that created this tuple
 *   t_xmax        int32_t   4 B  — transaction that deleted this tuple (or 0)
 *   t_cid         int32_t   4 B  — command id
 *   t_infomask    int32_t   4 B  — NULL / TOAST flags
 *   t_hoff        int16_t   2 B  — offset to actual data start
 *   null_bitmap   int8_t    1 B  — bool: has null bitmap
 *   optional_oid  int64_t   8 B  — optional OID
 */

#include <stdint.h>
#include "types_converter.h"

#define TUPLE_HEADER_SIZE 27

typedef struct {
    int32_t t_xmin;
    int32_t t_xmax;
    int32_t t_cid;
    int32_t t_infomask;
    int16_t t_hoff;
    int8_t  null_bitmap;
    int64_t optional_oid;
} TupleHeader;

void tuple_header_init    (TupleHeader *h);
void tuple_header_set     (TupleHeader *h, int32_t xmin, int32_t xmax,
                           int32_t cid, int32_t infomask,
                           int16_t hoff, int8_t bitmap, int64_t oid);

/* Serialization — writes exactly TUPLE_HEADER_SIZE bytes */
int  tuple_header_marshal  (uint8_t *buf, const TupleHeader *h);
/* Deserialization — reads exactly TUPLE_HEADER_SIZE bytes */
void tuple_header_unmarshal(TupleHeader *h, const uint8_t *buf);

void tuple_header_show(const TupleHeader *h);

#endif
