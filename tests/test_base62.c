#include "base62.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,      \
                          __LINE__, #condition);                                 \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static void check_round_trip(uint64_t input, const char *expected)
{
    char encoded[BASE62_BUFFER_SIZE];
    uint64_t decoded = 0U;

    CHECK(base62_encode(input, encoded, sizeof(encoded)) == BASE62_OK);
    CHECK(strcmp(encoded, expected) == 0);
    CHECK(base62_decode(encoded, &decoded) == BASE62_OK);
    CHECK(decoded == input);
}

static void test_known_values_and_round_trips(void)
{
    check_round_trip(UINT64_C(0), "0");
    check_round_trip(UINT64_C(1), "1");
    check_round_trip(UINT64_C(61), "Z");
    check_round_trip(UINT64_C(62), "10");
    check_round_trip(UINT64_MAX, "lYGhA16ahyf");
}

static void test_encode_rejects_invalid_output(void)
{
    char exact[2];
    char too_small[1];

    CHECK(base62_encode(UINT64_C(61), exact, sizeof(exact)) == BASE62_OK);
    CHECK(strcmp(exact, "Z") == 0);
    CHECK(base62_encode(UINT64_C(61), too_small, sizeof(too_small)) ==
          BASE62_BUFFER_TOO_SMALL);
    CHECK(base62_encode(UINT64_C(0), NULL, BASE62_BUFFER_SIZE) == BASE62_INVALID);
}

static void test_decode_rejects_invalid_input(void)
{
    uint64_t value = UINT64_C(123);

    CHECK(base62_decode(NULL, &value) == BASE62_INVALID);
    CHECK(base62_decode("", &value) == BASE62_INVALID);
    CHECK(base62_decode("1-", &value) == BASE62_INVALID);
    CHECK(base62_decode("a b", &value) == BASE62_INVALID);
    CHECK(base62_decode("01", &value) == BASE62_INVALID);
    CHECK(base62_decode("00", &value) == BASE62_INVALID);
    CHECK(base62_decode("0", NULL) == BASE62_INVALID);
}

static void test_decode_detects_overflow(void)
{
    uint64_t value = 0U;

    CHECK(base62_decode("lYGhA16ahyg", &value) == BASE62_OVERFLOW);
    CHECK(base62_decode("ZZZZZZZZZZZ", &value) == BASE62_OVERFLOW);
}

int main(void)
{
    test_known_values_and_round_trips();
    test_encode_rejects_invalid_output();
    test_decode_rejects_invalid_input();
    test_decode_detects_overflow();

    if (failures != 0) {
        (void)fprintf(stderr, "%d Base62 test(s) failed\n", failures);
        return 1;
    }

    (void)puts("Base62 tests passed");
    return 0;
}
