#ifndef DATA_NULL_BITMAP_H
#define DATA_NULL_BITMAP_H

/*
 * data_null_bitmap.h — null bitmap + tuple data
 * Replaces DataNullBitMapTuple class.
 * Fixed-size arrays instead of vector<bool> and vector<allVars>.
 *
 * Binary layout:
 *   ID_ROW    (int16)
 *   ID_BITMAP (int16) | count (int32) | count x bool (1 B each)
 *   ID_DATA   (int16) | total_bytes (int32) |
 *       per value: type (int16) | size (int32) | raw bytes
 */

#include <stdint.h>
#include "config.h"
#include "type_ids.h"
#include "all_var.h"

typedef struct {
    int8_t  bit_map[MAX_COLUMNS];
    int32_t bit_map_count;

    AllVar  data[MAX_COLUMNS];
    int32_t data_count;
} DataNullBitmap;

void    dnb_init (DataNullBitmap *d);
void    dnb_set  (DataNullBitmap *d,
                  const int8_t *bitmap, int32_t bm_count,
                  const AllVar *values, int32_t val_count);

/* Serialized size in bytes */
int32_t dnb_serial_size(const DataNullBitmap *d);

/* Serialization — writes to buf, returns bytes written */
int     dnb_marshal  (uint8_t *buf, const DataNullBitmap *d);
/* Deserialization */
void    dnb_unmarshal(DataNullBitmap *d, const uint8_t *buf, int32_t len);

void    dnb_show(const DataNullBitmap *d);

#endif
