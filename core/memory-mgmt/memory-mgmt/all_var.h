#ifndef ALL_VAR_H
#define ALL_VAR_H

/*
 * all_var.h — C replacement for std::variant<int32_t, int64_t, std::string>.
 * Uses a tagged union with a fixed-length string buffer instead of
 * heap-allocated std::string.
 */

#include <stdint.h>
#include "config.h"
#include "type_ids.h"
#include "types_converter.h"

typedef struct {
    int16_t type;       /* ID_INT32, ID_INT64, or ID_STRING */
    int32_t str_len;    /* length of string (only used for ID_STRING) */
    union {
        int32_t i32;
        int64_t i64;
        char    str[MAX_STR_LEN];  /* fixed-length buffer, no heap allocation */
    } val;
} AllVar;

/* Constructors */
AllVar all_var_from_int32 (int32_t val);
AllVar all_var_from_int64 (int64_t val);
AllVar all_var_from_string(const char *str);

/* Metadata */
int16_t all_var_type_id(const AllVar *v);
int32_t all_var_size   (const AllVar *v);  /* size in bytes */

/* Serialization — returns number of bytes written */
int all_var_marshal(uint8_t *buf, const AllVar *v);

/* Deserialization */
int all_var_unmarshal(AllVar *v, int16_t type_id,
                      const uint8_t *buf, int32_t len);

void all_var_show(const AllVar *v);

/* Returns 1 if column types and value types match, 0 otherwise */
int all_var_check_types(const int8_t *col_types, int32_t col_count,
                        const AllVar *values, int32_t val_count);

#endif
