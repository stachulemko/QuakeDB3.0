#ifndef TYPE_IDS_H
#define TYPE_IDS_H
#include <stdint.h>

/* Binary stream type identifiers */
#define ID_ROW              ((int16_t)1)
#define ID_BITMAP           ((int16_t)2)
#define ID_DATA             ((int16_t)3)
#define ID_INT32            ((int16_t)4)
#define ID_STRING           ((int16_t)5)
#define ID_INT64            ((int16_t)6)
#define ID_TABLE_HEADER     ((int16_t)7)
#define ID_TUPLE            ((int16_t)8)
#define ID_DATA_NULL        ((int16_t)9)
#define ID_COL_NAMES        ((int16_t)10)
#define ID_ALL_BLOCK        ((int16_t)11)
#define ID_BYTE_UNIT        ((int16_t)12)
#define ID_TYPE_TABLE       ((int16_t)13)
#define ID_BLOCK_HEADER     ((int16_t)14)

#endif
