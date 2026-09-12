#include "shortener.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,      \
                          __LINE__, #condition);                               \
            return false;                                                      \
        }                                                                      \
    } while (0)

static char database_path[128];

static void remove_database(void) {
    (void)unlink(database_path);
}

static bool test_create_and_resolve_distinct_records(void) {
    database_t *database = NULL;
    shortener_t shortener;
    char first_code[SHORTENER_SHORT_CODE_BUFFER_SIZE];
    char second_code[SHORTENER_SHORT_CODE_BUFFER_SIZE];
    char short_url[SHORTENER_SHORT_URL_BUFFER_SIZE];
    char original_url[SHORTENER_MAX_URL_LENGTH + 1U];

    remove_database();
    CHECK(database_open(database_path, &database) == DATABASE_OK);
    CHECK(shortener_init(&shortener, database) == SHORTENER_OK);
    CHECK(shortener_create(&shortener, "https://example.com/a", first_code,
                           sizeof(first_code), short_url, sizeof(short_url)) ==
          SHORTENER_OK);
    CHECK(strcmp(first_code, "1") == 0);
    CHECK(strcmp(short_url, "http://localhost:8080/1") == 0);
    CHECK(shortener_create(&shortener, "https://example.com/a", second_code,
                           sizeof(second_code), short_url, sizeof(short_url)) ==
          SHORTENER_OK);
    CHECK(strcmp(second_code, "2") == 0);
    CHECK(shortener_resolve(&shortener, first_code, original_url,
                            sizeof(original_url)) == SHORTENER_OK);
    CHECK(strcmp(original_url, "https://example.com/a") == 0);

    database_close(database);
    return true;
}

static bool test_url_validation(void) {
    database_t *database = NULL;
    shortener_t shortener;
    char code[SHORTENER_SHORT_CODE_BUFFER_SIZE];
    char short_url[SHORTENER_SHORT_URL_BUFFER_SIZE];
    char too_long[SHORTENER_MAX_URL_LENGTH + 2U];
    char unterminated[SHORTENER_MAX_URL_LENGTH + 1U];
    const char *invalid_urls[] = {
        "", "ftp://example.com", "http://", "https:///path",
        "http://:8080/path", "http://[::1]evil/path",
        "http://[not-an-ip]/", "http://bad]host/", "http://a%zz/",
        "http://user@@host/", "http://example.com:bad/path",
        "https://example.com/a b",
        "https://example.com/a\r\nInjected: yes"
    };
    size_t index;

    remove_database();
    CHECK(database_open(database_path, &database) == DATABASE_OK);
    CHECK(shortener_init(&shortener, database) == SHORTENER_OK);

    for (index = 0U; index < sizeof(invalid_urls) / sizeof(invalid_urls[0]);
         ++index) {
        CHECK(shortener_create(&shortener, invalid_urls[index], code,
                               sizeof(code), short_url, sizeof(short_url)) ==
              SHORTENER_INVALID);
    }
    (void)memset(too_long, 'a', sizeof(too_long));
    (void)memcpy(too_long, "https://example.com/", 20U);
    too_long[sizeof(too_long) - 1U] = '\0';
    CHECK(strlen(too_long) > SHORTENER_MAX_URL_LENGTH);
    CHECK(shortener_create(&shortener, too_long, code, sizeof(code), short_url,
                           sizeof(short_url)) == SHORTENER_INVALID);
    CHECK(shortener_create(&shortener, NULL, code, sizeof(code), short_url,
                           sizeof(short_url)) == SHORTENER_INVALID);
    CHECK(shortener_create(&shortener, "http://[::1]/path", code,
                           sizeof(code), short_url, sizeof(short_url)) ==
          SHORTENER_OK);
    (void)memset(unterminated, 'a', sizeof(unterminated));
    CHECK(shortener_create(&shortener, unterminated, code, sizeof(code),
                           short_url, sizeof(short_url)) == SHORTENER_INVALID);

    database_close(database);
    return true;
}

static bool test_codes_and_buffers_map_to_public_statuses(void) {
    database_t *database = NULL;
    shortener_t shortener;
    char code[SHORTENER_SHORT_CODE_BUFFER_SIZE];
    char short_url[SHORTENER_SHORT_URL_BUFFER_SIZE];
    char original_url[SHORTENER_MAX_URL_LENGTH + 1U];
    char tiny[1];

    remove_database();
    CHECK(database_open(database_path, &database) == DATABASE_OK);
    CHECK(shortener_init(&shortener, database) == SHORTENER_OK);
    CHECK(shortener_create(&shortener, "http://example.com", code,
                           sizeof(code), short_url, sizeof(short_url)) ==
          SHORTENER_OK);
    CHECK(shortener_resolve(&shortener, "01", original_url,
                            sizeof(original_url)) == SHORTENER_INVALID);
    CHECK(shortener_resolve(&shortener, "0", original_url,
                            sizeof(original_url)) == SHORTENER_NOT_FOUND);
    CHECK(shortener_resolve(&shortener, "Z", original_url,
                            sizeof(original_url)) == SHORTENER_NOT_FOUND);
    CHECK(shortener_resolve(&shortener, code, tiny, sizeof(tiny)) ==
          SHORTENER_BUFFER_TOO_SMALL);
    CHECK(shortener_create(&shortener, "http://example.com", tiny,
                           sizeof(tiny), short_url, sizeof(short_url)) ==
          SHORTENER_BUFFER_TOO_SMALL);
    CHECK(shortener_init(NULL, database) == SHORTENER_INVALID);
    CHECK(shortener_init(&shortener, NULL) == SHORTENER_INVALID);

    database_close(database);
    return true;
}

int main(void) {
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"create and resolve distinct records",
         test_create_and_resolve_distinct_records},
        {"URL validation", test_url_validation},
        {"codes and buffers map to public statuses",
         test_codes_and_buffers_map_to_public_statuses},
    };
    size_t index;
    size_t failures = 0U;

    (void)snprintf(database_path, sizeof(database_path),
                   "/tmp/u_short_shortener_test_%ld.sqlite", (long)getpid());
    remove_database();
    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            (void)fprintf(stderr, "FAIL: %s\n", tests[index].name);
            ++failures;
        }
    }
    remove_database();

    if (failures != 0U) {
        (void)fprintf(stderr, "%zu shortener test(s) failed\n", failures);
        return 1;
    }
    (void)puts("shortener tests passed");
    return 0;
}
