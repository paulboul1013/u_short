#ifndef URL_SHORTENER_BASE62_H
#define URL_SHORTENER_BASE62_H

#include <stddef.h>
#include <stdint.h>

#define BASE62_MAX_ENCODED_LENGTH 11U
#define BASE62_BUFFER_SIZE (BASE62_MAX_ENCODED_LENGTH + 1U)

typedef enum {
    BASE62_OK = 0,
    BASE62_INVALID,
    BASE62_OVERFLOW,
    BASE62_BUFFER_TOO_SMALL
} base62_status_t;

base62_status_t base62_encode(uint64_t value, char *output,
                              size_t output_capacity);
base62_status_t base62_decode(const char *input, uint64_t *value);

#endif
