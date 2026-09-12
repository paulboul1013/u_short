# Base62 module design

## Responsibility

Convert between `uint64_t` values and canonical Short Codes. This is a pure,
stateless module with no allocation or I/O.

## Persistent alphabet

```text
0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ
```

The alphabet and digit order are a persistence format and must not change.

## Public interface

`include/base62.h` exposes:

- `BASE62_MAX_ENCODED_LENGTH` (`11`) and `BASE62_BUFFER_SIZE` (`12`)
- `base62_status_t`: `BASE62_OK`, `BASE62_INVALID`,
  `BASE62_OVERFLOW`, `BASE62_BUFFER_TOO_SMALL`
- `base62_encode(uint64_t value, char *output, size_t output_capacity)`
- `base62_decode(const char *input, uint64_t *value)`

`base62_encode` always produces a NUL-terminated canonical representation when
successful. Zero encodes as `"0"`.

`base62_decode` rejects NULL/empty input, characters outside the alphabet,
overflow, and leading zeroes except the canonical value `"0"`.

## Verification

Unit tests cover `0`, `1`, `61`, `62`, `UINT64_MAX`, insufficient output capacity,
invalid characters, empty input, leading zeroes, and overflow.
