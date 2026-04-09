#ifndef TYPES_CONVERTER_H
#define TYPES_CONVERTER_H
#include <stdint.h>
#include <string.h>

/*
 * Marshal functions: write value to buf (little-endian),
 * return number of bytes written.
 */
int marshal_int8  (uint8_t *buf, int8_t  val);
int marshal_int16 (uint8_t *buf, int16_t val);
int marshal_int32 (uint8_t *buf, int32_t val);
int marshal_int64 (uint8_t *buf, int64_t val);
int marshal_bool  (uint8_t *buf, int8_t  val);
int marshal_string(uint8_t *buf, const char *str, int32_t len);

/*
 * Unmarshal functions: read value from buf (little-endian).
 */
void unmarshal_int8  (int8_t  *val, const uint8_t *buf);
void unmarshal_int16 (int16_t *val, const uint8_t *buf);
void unmarshal_int32 (int32_t *val, const uint8_t *buf);
void unmarshal_int64 (int64_t *val, const uint8_t *buf);
void unmarshal_bool  (int8_t  *val, const uint8_t *buf);
void unmarshal_string(char *val, int32_t max_len, const uint8_t *buf, int32_t len);

void show_bytes(const uint8_t *buf, int32_t len);

#endif
