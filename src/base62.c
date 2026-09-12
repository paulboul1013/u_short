#include "base62.h"

#include <stdint.h>

static const char base62_alphabet[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

static int base62_digit(char character, uint64_t *digit)
{
    uint64_t index;

    for (index = 0U; index < UINT64_C(62); ++index) {
        if (base62_alphabet[index] == character) {
            *digit = index;
            return 1;
        }
    }

    return 0;
}

base62_status_t base62_encode(uint64_t value, char *output,
                              size_t output_capacity)
{
    char reversed[BASE62_MAX_ENCODED_LENGTH];
    size_t length = 0U;
    size_t index;

    if (output == NULL) {
        return BASE62_INVALID;
    }

    do {
        reversed[length] = base62_alphabet[value % UINT64_C(62)];
        ++length;
        value /= UINT64_C(62);
    } while (value != 0U);

    if (output_capacity <= length) {
        return BASE62_BUFFER_TOO_SMALL;
    }

    for (index = 0U; index < length; ++index) {
        output[index] = reversed[length - index - 1U];
    }
    output[length] = '\0';

    return BASE62_OK;
}

base62_status_t base62_decode(const char *input, uint64_t *value)
{
    uint64_t decoded = 0U;
    size_t index;

    if (input == NULL || value == NULL || input[0] == '\0') {
        return BASE62_INVALID;
    }
    if (input[0] == '0' && input[1] != '\0') {
        return BASE62_INVALID;
    }

    for (index = 0U; input[index] != '\0'; ++index) {
        uint64_t digit;

        if (!base62_digit(input[index], &digit)) {
            return BASE62_INVALID;
        }
        if (decoded > (UINT64_MAX - digit) / UINT64_C(62)) {
            return BASE62_OVERFLOW;
        }
        decoded = decoded * UINT64_C(62) + digit;
    }

    *value = decoded;
    return BASE62_OK;
}
