#include "types_converter.h"
#include <stdio.h>
#include <string.h>

int marshal_int8(uint8_t *buf, int8_t val) {
    buf[0] = (uint8_t)val;
    return 1;
}

int marshal_int16(uint8_t *buf, int16_t val) {
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
    return 2;
}

int marshal_int32(uint8_t *buf, int32_t val) {
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >>  8) & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)((val >> 24) & 0xFF);
    return 4;
}

int marshal_int64(uint8_t *buf, int64_t val) {
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >>  8) & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)((val >> 24) & 0xFF);
    buf[4] = (uint8_t)((val >> 32) & 0xFF);
    buf[5] = (uint8_t)((val >> 40) & 0xFF);
    buf[6] = (uint8_t)((val >> 48) & 0xFF);
    buf[7] = (uint8_t)((val >> 56) & 0xFF);
    return 8;
}

int marshal_bool(uint8_t *buf, int8_t val) {
    buf[0] = val ? 1 : 0;
    return 1;
}

int marshal_string(uint8_t *buf, const char *str, int32_t len) {
    memcpy(buf, str, (size_t)len);
    return len;
}

void unmarshal_int8(int8_t *val, const uint8_t *buf) {
    *val = (int8_t)buf[0];
}

void unmarshal_int16(int16_t *val, const uint8_t *buf) {
    *val = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

void unmarshal_int32(int32_t *val, const uint8_t *buf) {
    *val = (int32_t)((uint32_t)buf[0]        |
                     ((uint32_t)buf[1] <<  8) |
                     ((uint32_t)buf[2] << 16) |
                     ((uint32_t)buf[3] << 24));
}

void unmarshal_int64(int64_t *val, const uint8_t *buf) {
    *val = (int64_t)((uint64_t)buf[0]        |
                     ((uint64_t)buf[1] <<  8) |
                     ((uint64_t)buf[2] << 16) |
                     ((uint64_t)buf[3] << 24) |
                     ((uint64_t)buf[4] << 32) |
                     ((uint64_t)buf[5] << 40) |
                     ((uint64_t)buf[6] << 48) |
                     ((uint64_t)buf[7] << 56));
}

void unmarshal_bool(int8_t *val, const uint8_t *buf) {
    *val = (buf[0] != 0) ? 1 : 0;
}

void unmarshal_string(char *val, int32_t max_len, const uint8_t *buf, int32_t len) {
    int32_t copy_len = (len < max_len - 1) ? len : max_len - 1;
    memcpy(val, buf, (size_t)copy_len);
    val[copy_len] = '\0';
}

void show_bytes(const uint8_t *buf, int32_t len) {
    for (int32_t i = 0; i < len; i++) {
        for (int b = 7; b >= 0; b--)
            printf("%d", (buf[i] >> b) & 1);
        printf(" ");
    }
    printf("\n");
}
