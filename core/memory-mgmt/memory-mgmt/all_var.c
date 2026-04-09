#include "all_var.h"
#include <stdio.h>
#include <string.h>

AllVar all_var_from_int32(int32_t val) {
    AllVar v;
    v.type    = ID_INT32;
    v.str_len = 0;
    v.val.i32 = val;
    return v;
}

AllVar all_var_from_int64(int64_t val) {
    AllVar v;
    v.type    = ID_INT64;
    v.str_len = 0;
    v.val.i64 = val;
    return v;
}

AllVar all_var_from_string(const char *str) {
    AllVar v;
    int32_t len;
    v.type = ID_STRING;
    len = (int32_t)strlen(str);
    if (len >= MAX_STR_LEN) len = MAX_STR_LEN - 1;
    v.str_len = len;
    memcpy(v.val.str, str, (size_t)len);
    v.val.str[len] = '\0';
    return v;
}

int16_t all_var_type_id(const AllVar *v) {
    return v->type;
}

int32_t all_var_size(const AllVar *v) {
    switch (v->type) {
        case ID_INT32:  return 4;
        case ID_INT64:  return 8;
        case ID_STRING: return v->str_len;
        default:        return -1;
    }
}

int all_var_marshal(uint8_t *buf, const AllVar *v) {
    switch (v->type) {
        case ID_INT32:  return marshal_int32(buf, v->val.i32);
        case ID_INT64:  return marshal_int64(buf, v->val.i64);
        case ID_STRING: return marshal_string(buf, v->val.str, v->str_len);
        default:        return 0;
    }
}

int all_var_unmarshal(AllVar *v, int16_t type_id,
                      const uint8_t *buf, int32_t len) {
    v->type = type_id;
    switch (type_id) {
        case ID_INT32:
            unmarshal_int32(&v->val.i32, buf);
            v->str_len = 0;
            return 4;
        case ID_INT64:
            unmarshal_int64(&v->val.i64, buf);
            v->str_len = 0;
            return 8;
        case ID_STRING: {
            int32_t copy_len = (len < MAX_STR_LEN - 1) ? len : MAX_STR_LEN - 1;
            memcpy(v->val.str, buf, (size_t)copy_len);
            v->val.str[copy_len] = '\0';
            v->str_len = copy_len;
            return copy_len;
        }
        default:
            v->str_len = 0;
            return 0;
    }
}

void all_var_show(const AllVar *v) {
    switch (v->type) {
        case ID_INT32:  printf("%d",   v->val.i32);            break;
        case ID_INT64:  printf("%lld", (long long)v->val.i64); break;
        case ID_STRING: printf("%s",   v->val.str);            break;
        default:        printf("NULL");                        break;
    }
}

int all_var_check_types(const int8_t *col_types, int32_t col_count,
                        const AllVar *values, int32_t val_count) {
    int32_t i;
    if (col_count != val_count) return 0;
    for (i = 0; i < col_count; i++) {
        if (all_var_type_id(&values[i]) != (int16_t)col_types[i])
            return 0;
    }
    return 1;
}
