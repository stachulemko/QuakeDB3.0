#include "tuple_header.h"
#include <stdio.h>
#include <string.h>

void tuple_header_init(TupleHeader *h) {
    memset(h, 0, sizeof(*h));
}

void tuple_header_set(TupleHeader *h, int32_t xmin, int32_t xmax,
                      int32_t cid, int32_t infomask,
                      int16_t hoff, int8_t bitmap, int64_t oid) {
    h->t_xmin       = xmin;
    h->t_xmax       = xmax;
    h->t_cid        = cid;
    h->t_infomask   = infomask;
    h->t_hoff       = hoff;
    h->null_bitmap  = bitmap;
    h->optional_oid = oid;
}

int tuple_header_marshal(uint8_t *buf, const TupleHeader *h) {
    int off = 0;
    off += marshal_int32(buf + off, h->t_xmin);
    off += marshal_int32(buf + off, h->t_xmax);
    off += marshal_int32(buf + off, h->t_cid);
    off += marshal_int32(buf + off, h->t_infomask);
    off += marshal_int16(buf + off, h->t_hoff);
    off += marshal_bool (buf + off, h->null_bitmap);
    off += marshal_int64(buf + off, h->optional_oid);
    return off; /* 35 */
}

void tuple_header_unmarshal(TupleHeader *h, const uint8_t *buf) {
    unmarshal_int32(&h->t_xmin,       buf +  0);
    unmarshal_int32(&h->t_xmax,       buf +  4);
    unmarshal_int32(&h->t_cid,        buf +  8);
    unmarshal_int32(&h->t_infomask,   buf + 12);
    unmarshal_int16(&h->t_hoff,       buf + 16);
    unmarshal_bool (&h->null_bitmap,  buf + 18);
    unmarshal_int64(&h->optional_oid, buf + 19);
}

void tuple_header_show(const TupleHeader *h) {
    printf("t_xmin:       %d\n", h->t_xmin);
    printf("t_xmax:       %d\n", h->t_xmax);
    printf("t_cid:        %d\n",   h->t_cid);
    printf("t_infomask:   %d\n",   h->t_infomask);
    printf("t_hoff:       %d\n",   h->t_hoff);
    printf("null_bitmap:  %d\n",   h->null_bitmap);
    printf("optional_oid: %lld\n", (long long)h->optional_oid);
}
