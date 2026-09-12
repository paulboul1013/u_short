#include "shortener.h"

#include "base62.h"

#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int is_ascii_alphanumeric(unsigned char byte) {
    return (byte >= (unsigned char)'0' && byte <= (unsigned char)'9') ||
           (byte >= (unsigned char)'A' && byte <= (unsigned char)'Z') ||
           (byte >= (unsigned char)'a' && byte <= (unsigned char)'z');
}

static int is_hex_digit(unsigned char byte) {
    return (byte >= (unsigned char)'0' && byte <= (unsigned char)'9') ||
           (byte >= (unsigned char)'a' && byte <= (unsigned char)'f') ||
           (byte >= (unsigned char)'A' && byte <= (unsigned char)'F');
}

static int valid_reg_name(const char *start, const char *end) {
    const char *cursor = start;

    if (start == end) {
        return 0;
    }
    while (cursor < end) {
        unsigned char byte = (unsigned char)*cursor;

        if (is_ascii_alphanumeric(byte) ||
            strchr("-._~!$&'()*+,;=", (int)byte) != NULL) {
            ++cursor;
            continue;
        }
        if (byte == (unsigned char)'%' && end - cursor >= 3 &&
            is_hex_digit((unsigned char)cursor[1]) &&
            is_hex_digit((unsigned char)cursor[2])) {
            cursor += 3;
            continue;
        }
        return 0;
    }
    return 1;
}

static int has_valid_original_url(const char *url) {
    const char *authority;
    const char *authority_end;
    const char *host;
    const char *cursor;
    size_t length = 0U;

    if (url == NULL) {
        return 0;
    }
    while (length <= SHORTENER_MAX_URL_LENGTH && url[length] != '\0') {
        unsigned char byte = (unsigned char)url[length];

        if (byte <= 0x20U || byte == 0x7fU) {
            return 0;
        }
        ++length;
    }
    if (length == 0U || length > SHORTENER_MAX_URL_LENGTH) {
        return 0;
    }

    if (strncmp(url, "http://", 7U) == 0) {
        authority = url + 7U;
    } else if (strncmp(url, "https://", 8U) == 0) {
        authority = url + 8U;
    } else {
        return 0;
    }

    authority_end = authority;
    while (*authority_end != '\0' && *authority_end != '/' &&
           *authority_end != '?' && *authority_end != '#') {
        ++authority_end;
    }
    if (authority == authority_end) {
        return 0;
    }

    host = authority;
    for (cursor = authority; cursor < authority_end; ++cursor) {
        if (*cursor == '@') {
            return 0;
        }
    }
    if (host == authority_end || *host == ':') {
        return 0;
    }
    if (*host == '[') {
        const char *closing = host + 1;
        char address[INET6_ADDRSTRLEN];
        size_t address_length;
        struct in6_addr parsed_address;

        while (closing < authority_end && *closing != ']') {
            ++closing;
        }
        if (closing == host + 1 || closing == authority_end ||
            (closing + 1 < authority_end && closing[1] != ':')) {
            return 0;
        }
        address_length = (size_t)(closing - (host + 1));
        if (address_length >= sizeof(address)) {
            return 0;
        }
        (void)memcpy(address, host + 1, address_length);
        address[address_length] = '\0';
        if (inet_pton(AF_INET6, address, &parsed_address) != 1) {
            return 0;
        }
        if (closing + 1 < authority_end) {
            const char *port = closing + 2;
            if (port == authority_end) {
                return 0;
            }
            while (port < authority_end) {
                if (*port < '0' || *port > '9') {
                    return 0;
                }
                ++port;
            }
        }
    } else {
        const char *colon = host;
        while (colon < authority_end && *colon != ':') {
            ++colon;
        }
        if (!valid_reg_name(host, colon)) {
            return 0;
        }
        if (colon < authority_end) {
            const char *port = colon + 1;
            if (port == authority_end) {
                return 0;
            }
            while (port < authority_end) {
                if (*port < '0' || *port > '9') {
                    return 0;
                }
                ++port;
            }
        }
    }

    return 1;
}

shortener_status_t shortener_init(shortener_t *shortener,
                                  database_t *database) {
    if (shortener == NULL || database == NULL) {
        return SHORTENER_INVALID;
    }
    shortener->database = database;
    return SHORTENER_OK;
}

shortener_status_t shortener_create(shortener_t *shortener,
                                    const char *original_url, char *short_code,
                                    size_t short_code_capacity,
                                    char *short_url,
                                    size_t short_url_capacity) {
    database_status_t database_status;
    base62_status_t base62_status;
    int64_t id = 0;
    time_t now;
    int written;

    if (shortener == NULL || shortener->database == NULL ||
        !has_valid_original_url(original_url) || short_code == NULL ||
        short_url == NULL) {
        return SHORTENER_INVALID;
    }
    if (short_code_capacity < BASE62_BUFFER_SIZE ||
        short_url_capacity < SHORTENER_SHORT_URL_BUFFER_SIZE) {
        return SHORTENER_BUFFER_TOO_SMALL;
    }

    now = time(NULL);
    if (now == (time_t)-1 || (uintmax_t)now > (uintmax_t)INT64_MAX) {
        return SHORTENER_INTERNAL_ERROR;
    }
    database_status = database_insert_url(shortener->database, original_url,
                                          (int64_t)now, &id);
    if (database_status != DATABASE_OK || id <= 0) {
        return SHORTENER_INTERNAL_ERROR;
    }

    base62_status = base62_encode((uint64_t)id, short_code,
                                  short_code_capacity);
    if (base62_status != BASE62_OK) {
        return SHORTENER_INTERNAL_ERROR;
    }
    written = snprintf(short_url, short_url_capacity, "%s/%s",
                       SHORTENER_BASE_URL, short_code);
    if (written < 0 || (size_t)written >= short_url_capacity) {
        return SHORTENER_INTERNAL_ERROR;
    }
    return SHORTENER_OK;
}

shortener_status_t shortener_resolve(shortener_t *shortener,
                                     const char *short_code,
                                     char *original_url,
                                     size_t original_url_capacity) {
    uint64_t decoded_id = 0U;
    base62_status_t base62_status;
    database_status_t database_status;

    if (shortener == NULL || shortener->database == NULL ||
        short_code == NULL || original_url == NULL ||
        original_url_capacity == 0U) {
        return SHORTENER_INVALID;
    }
    base62_status = base62_decode(short_code, &decoded_id);
    if (base62_status != BASE62_OK) {
        return SHORTENER_INVALID;
    }
    if (decoded_id == 0U || decoded_id > (uint64_t)INT64_MAX) {
        return SHORTENER_NOT_FOUND;
    }

    database_status = database_find_url(shortener->database,
                                        (int64_t)decoded_id, original_url,
                                        original_url_capacity);
    if (database_status == DATABASE_OK) {
        return SHORTENER_OK;
    }
    if (database_status == DATABASE_NOT_FOUND) {
        return SHORTENER_NOT_FOUND;
    }
    if (database_status == DATABASE_BUFFER_TOO_SMALL) {
        return SHORTENER_BUFFER_TOO_SMALL;
    }
    if (database_status == DATABASE_INVALID_ARGUMENT) {
        return SHORTENER_INVALID;
    }
    return SHORTENER_INTERNAL_ERROR;
}
