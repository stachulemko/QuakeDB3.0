#ifndef ALL_VAR_H
#define ALL_VAR_H

/*
 * all_var.h — C replacement for std::variant<int32_t, int64_t, std::string>.
 * Uses a tagged union with a fixed-length string buffer instead of
 * heap-allocated std::string.
 */

#include <stdint.h>
#include <string.h>
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


static inline int all_var_cmp(const AllVar *a, const AllVar *b) {
    if (a->type != b->type) return (a->type < b->type) ? -1 : 1;
    switch (a->type) {
        case ID_INT32: return (a->val.i32 > b->val.i32) - (a->val.i32 < b->val.i32);
        case ID_INT64: return (a->val.i64 > b->val.i64) - (a->val.i64 < b->val.i64);
        case ID_STRING:
            return strcmp(a->val.str, b->val.str);
        default:
            return 0;
    }
}

static inline void swap_allvar_ptr(AllVar **a, AllVar **b) {
    AllVar *tmp = *a;
    *a = *b;
    *b = tmp;
}

static inline int8_t evaluateAllVar(const AllVar *a, const AllVar *b, int8_t operator,int16_t method) {
    /* operator:
       0 - equal
       1 - not equal
       2 - less
       3 - more
       4 - lessEqual
       5 - moreEqual
       method:
       0 - for strings: compare by length first, then lexicographically
       other - default behavior (value comparison)
    */

    if (a == NULL || b == NULL) return 0;

    int cmp = 0; /* negative if a<b, 0 if equal, positive if a>b */

    /* same type -> compare by actual value (with optional method for strings) */
    if (a->type == b->type) {
        switch (a->type) {
            case ID_INT32:
                if (a->val.i32 < b->val.i32) cmp = -1;
                else if (a->val.i32 > b->val.i32) cmp = 1;
                else cmp = 0;
                break;
            case ID_INT64:
                if (a->val.i64 < b->val.i64) cmp = -1;
                else if (a->val.i64 > b->val.i64) cmp = 1;
                else cmp = 0;
                break;
            case ID_STRING:
                if (method == 0) { /* first by size then lexicographic */
                    if (a->str_len < b->str_len) cmp = -1;
                    else if (a->str_len > b->str_len) cmp = 1;
                    else cmp = strcmp(a->val.str, b->val.str);
                } else {
                    cmp = strcmp(a->val.str, b->val.str);
                }
                break;
            default:
                /* fallback to generic cmp (type equality handled above) */
                cmp = all_var_cmp(a, b);
                break;
        }
    } else {
        /* different types: use type id ordering (consistent with all_var_cmp) */
        /* this gives deterministic ordering between differing types */
        if (a->type < b->type) cmp = -1;
        else cmp = 1;
    }

    switch (operator) {
        case 0: /* equal */
            return (cmp == 0) ? 1 : 0;
        case 1: /* not equal */
            return (cmp != 0) ? 1 : 0;
        case 2: /* less */
            return (cmp < 0) ? 1 : 0;
        case 3: /* more */
            return (cmp > 0) ? 1 : 0;
        case 4: /* lessEqual */
            return (cmp <= 0) ? 1 : 0;
        case 5: /* moreEqual */
            return (cmp >= 0) ? 1 : 0;
        default:
            return 0; /* unknown operator -> false */
    }
}




#endif
