#include "data_null_bitmap.h"
#include <string.h>
#include <stdio.h>

void dnb_init(DataNullBitmap *d) {
    memset(d, 0, sizeof(*d));
}

void dnb_set(DataNullBitmap *d,
             const int8_t *bitmap, int32_t bm_count,
             const AllVar  *values, int32_t val_count) {
    int32_t i;

    d->bit_map_count = (bm_count <= MAX_COLUMNS) ? bm_count : MAX_COLUMNS;
    for (i = 0; i < d->bit_map_count; i++)
        d->bit_map[i] = bitmap[i];

    d->data_count = (val_count <= MAX_COLUMNS) ? val_count : MAX_COLUMNS;
    for (i = 0; i < d->data_count; i++)
        d->data[i] = values[i];
}

int32_t dnb_serial_size(const DataNullBitmap *d) {
    int32_t i;
    /* ID_ROW(2) + ID_BITMAP(2)+count(4)+count*1 + ID_DATA(2)+total(4)+
       per value: type(2)+size(4)+bytes */
    int32_t sz = 2 + 2 + 4 + d->bit_map_count + 2 + 4;
    for (i = 0; i < d->data_count; i++)
        sz += 2 + 4 + all_var_size(&d->data[i]);
    return sz;
}

int dnb_marshal(uint8_t *buf, const DataNullBitmap *d) {
    int32_t i, data_total;
    int off = 0;

    /* --- row id --- */
    off += marshal_int16(buf + off, ID_ROW);

    /* --- bitmap section --- */
    off += marshal_int16(buf + off, ID_BITMAP);
    off += marshal_int32(buf + off, d->bit_map_count);
    for (i = 0; i < d->bit_map_count; i++)
        off += marshal_bool(buf + off, d->bit_map[i]);

    /* --- data section --- */
    off += marshal_int16(buf + off, ID_DATA);

    /* oblicz total bajtów danych (type+size+bytes per value) */
    data_total = 0;
    for (i = 0; i < d->data_count; i++)
        data_total += 2 + 4 + all_var_size(&d->data[i]);
    off += marshal_int32(buf + off, data_total);

    for (i = 0; i < d->data_count; i++) {
        off += marshal_int16(buf + off, all_var_type_id(&d->data[i]));
        off += marshal_int32(buf + off, all_var_size(&d->data[i]));
        off += all_var_marshal(buf + off, &d->data[i]);
    }

    return off;
}

void dnb_unmarshal(DataNullBitmap *d, const uint8_t *buf, int32_t len) {
    int16_t tag;
    int32_t off = 0;
    int32_t i, bm_count, data_total, bytes_read;

    dnb_init(d);
    if (len < 2) return;

    /* ID_ROW */
    unmarshal_int16(&tag, buf + off); off += 2;
    if (tag != ID_ROW) return;

    /* ID_BITMAP */
    if (off + 2 > len) return;
    unmarshal_int16(&tag, buf + off); off += 2;
    if (tag != ID_BITMAP) return;

    if (off + 4 > len) return;
    unmarshal_int32(&bm_count, buf + off); off += 4;
    d->bit_map_count = (bm_count <= MAX_COLUMNS) ? bm_count : MAX_COLUMNS;

    for (i = 0; i < d->bit_map_count; i++) {
        if (off >= len) return;
        unmarshal_bool(&d->bit_map[i], buf + off); off += 1;
    }
    if (bm_count > d->bit_map_count)  /* pomiń nadmiar */
        off += bm_count - d->bit_map_count;

    /* ID_DATA */
    if (off + 2 > len) return;
    unmarshal_int16(&tag, buf + off); off += 2;
    if (tag != ID_DATA) return;

    if (off + 4 > len) return;
    unmarshal_int32(&data_total, buf + off); off += 4;

    bytes_read = 0;
    d->data_count = 0;
    while (bytes_read < data_total && d->data_count < MAX_COLUMNS) {
        int16_t vtype;
        int32_t vsize;

        if (off + 6 > len) return;
        unmarshal_int16(&vtype, buf + off); off += 2;
        unmarshal_int32(&vsize, buf + off); off += 4;
        bytes_read += 6;

        if (off + vsize > len) return;
        all_var_unmarshal(&d->data[d->data_count], vtype, buf + off, vsize);
        off        += vsize;
        bytes_read += vsize;
        d->data_count++;
    }
}

void dnb_show(const DataNullBitmap *d) {
    int32_t i;
    printf("bitmap: ");
    for (i = 0; i < d->bit_map_count; i++)
        printf("%d ", d->bit_map[i]);
    printf("\n");
    printf("data:   ");
    for (i = 0; i < d->data_count; i++) {
        all_var_show(&d->data[i]);
        printf(" ");
    }
    printf("\n");
}
