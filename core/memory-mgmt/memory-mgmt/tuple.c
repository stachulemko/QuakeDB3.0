#include "tuple.h"
#include <string.h>
#include <stdio.h>

void tuple_init(Tuple *t) {
    tuple_header_init(&t->header);
    dnb_init(&t->dnb);
}

void tuple_set(Tuple *t,
               int32_t xmin, int32_t xmax, int32_t cid,
               int32_t infomask, int16_t hoff, int8_t bitmap,
               int64_t oid,
               const int8_t *bm, int32_t bm_count,
               const AllVar *vals, int32_t val_count) {
    tuple_header_set(&t->header, xmin, xmax, cid, infomask, hoff, bitmap, oid);
    dnb_set(&t->dnb, bm, bm_count, vals, val_count);
}

int32_t tuple_size(const Tuple *t) {
    /* type(2) + size(4) + header(27) + dnb */
    return 2 + 4 + TUPLE_HEADER_SIZE + dnb_serial_size(&t->dnb);
}

int tuple_marshal(uint8_t *buf, const Tuple *t) {
    int off = 0;
    int32_t payload_size;
    uint8_t tmp[BLOCK_SIZE];
    int hdr_bytes, dnb_bytes;

    hdr_bytes = tuple_header_marshal(tmp, &t->header);
    dnb_bytes = dnb_marshal(tmp + hdr_bytes, &t->dnb);
    payload_size = hdr_bytes + dnb_bytes;

    off += marshal_int16(buf + off, ID_TUPLE);
    off += marshal_int32(buf + off, payload_size);
    memcpy(buf + off, tmp, payload_size);
    off += payload_size;
    return off;
}

void tuple_unmarshal(Tuple *t, const uint8_t *buf, int32_t len) {
    int16_t tag;
    int32_t payload_size;
    int off = 0;

    tuple_init(t);
    if (len < 6) return;

    unmarshal_int16(&tag, buf + off); off += 2;
    if (tag != ID_TUPLE) return;

    unmarshal_int32(&payload_size, buf + off); off += 4;
    if (off + payload_size > len) return;

    tuple_header_unmarshal(&t->header, buf + off);
    off += TUPLE_HEADER_SIZE;

    dnb_unmarshal(&t->dnb, buf + off, payload_size - TUPLE_HEADER_SIZE);
}

void tuple_show(const Tuple *t) {
    tuple_header_show(&t->header);
    dnb_show(&t->dnb);
}
