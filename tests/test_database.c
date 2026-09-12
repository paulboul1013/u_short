#include "database.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,      \
                          __LINE__, #condition);                                \
            return false;                                                      \
        }                                                                      \
    } while (0)

static char test_database_path[128];

static void remove_test_database(void) {
    char sidecar_path[160];

    (void)unlink(test_database_path);
    (void)snprintf(sidecar_path, sizeof(sidecar_path), "%s-wal",
                   test_database_path);
    (void)unlink(sidecar_path);
    (void)snprintf(sidecar_path, sizeof(sidecar_path), "%s-shm",
                   test_database_path);
    (void)unlink(sidecar_path);
}

static bool test_open_initializes_schema_and_insert_can_be_found(void) {
    database_t *database = NULL;
    int64_t id = 0;
    char original_url[128];

    remove_test_database();
    CHECK(database_open(test_database_path, &database) == DATABASE_OK);
    CHECK(database != NULL);
    CHECK(database_insert_url(database, "https://example.com/article", 1234,
                              &id) == DATABASE_OK);
    CHECK(id > 0);
    CHECK(database_find_url(database, id, original_url,
                            sizeof(original_url)) == DATABASE_OK);
    CHECK(strcmp(original_url, "https://example.com/article") == 0);

    database_close(database);
    return true;
}

static bool test_duplicate_urls_create_distinct_records(void) {
    database_t *database = NULL;
    int64_t first_id = 0;
    int64_t second_id = 0;
    char original_url[128];

    remove_test_database();
    CHECK(database_open(test_database_path, &database) == DATABASE_OK);
    CHECK(database_insert_url(database, "https://example.com/repeated", 100,
                              &first_id) == DATABASE_OK);
    CHECK(database_insert_url(database, "https://example.com/repeated", 200,
                              &second_id) == DATABASE_OK);
    CHECK(first_id > 0);
    CHECK(second_id > first_id);
    CHECK(database_find_url(database, first_id, original_url,
                            sizeof(original_url)) == DATABASE_OK);
    CHECK(strcmp(original_url, "https://example.com/repeated") == 0);
    CHECK(database_find_url(database, second_id, original_url,
                            sizeof(original_url)) == DATABASE_OK);
    CHECK(strcmp(original_url, "https://example.com/repeated") == 0);

    database_close(database);
    return true;
}

static bool test_records_persist_after_reopen(void) {
    database_t *database = NULL;
    int64_t id = 0;
    char original_url[128];

    remove_test_database();
    CHECK(database_open(test_database_path, &database) == DATABASE_OK);
    CHECK(database_insert_url(database, "https://example.com/persistent", 300,
                              &id) == DATABASE_OK);
    database_close(database);
    database = NULL;

    CHECK(database_open(test_database_path, &database) == DATABASE_OK);
    CHECK(database_find_url(database, id, original_url,
                            sizeof(original_url)) == DATABASE_OK);
    CHECK(strcmp(original_url, "https://example.com/persistent") == 0);

    database_close(database);
    return true;
}

static bool test_find_distinguishes_not_found_and_small_buffer(void) {
    database_t *database = NULL;
    int64_t id = 0;
    char exact_buffer[25];
    char small_buffer[24];

    remove_test_database();
    CHECK(database_open(test_database_path, &database) == DATABASE_OK);
    CHECK(database_insert_url(database, "https://example.com/path", 400, &id) ==
          DATABASE_OK);
    CHECK(database_find_url(database, id + 1000000, exact_buffer,
                            sizeof(exact_buffer)) == DATABASE_NOT_FOUND);
    CHECK(database_find_url(database, id, small_buffer,
                            sizeof(small_buffer)) == DATABASE_BUFFER_TOO_SMALL);
    CHECK(database_find_url(database, id, exact_buffer,
                            sizeof(exact_buffer)) == DATABASE_OK);
    CHECK(strcmp(exact_buffer, "https://example.com/path") == 0);

    database_close(database);
    return true;
}

static bool test_invalid_arguments_are_rejected(void) {
    database_t *database = NULL;
    database_t *unexpected_database = NULL;
    int64_t id = 0;
    char original_url[16];

    remove_test_database();
    CHECK(database_open(NULL, &unexpected_database) == DATABASE_INVALID_ARGUMENT);
    CHECK(unexpected_database == NULL);
    CHECK(database_open(test_database_path, NULL) == DATABASE_INVALID_ARGUMENT);
    CHECK(database_open(test_database_path, &database) == DATABASE_OK);
    CHECK(database_insert_url(NULL, "https://example.com", 500, &id) ==
          DATABASE_INVALID_ARGUMENT);
    CHECK(database_insert_url(database, NULL, 500, &id) ==
          DATABASE_INVALID_ARGUMENT);
    CHECK(database_insert_url(database, "https://example.com", 500, NULL) ==
          DATABASE_INVALID_ARGUMENT);
    CHECK(database_find_url(NULL, 1, original_url, sizeof(original_url)) ==
          DATABASE_INVALID_ARGUMENT);
    CHECK(database_find_url(database, 0, original_url, sizeof(original_url)) ==
          DATABASE_INVALID_ARGUMENT);
    CHECK(database_find_url(database, -1, original_url, sizeof(original_url)) ==
          DATABASE_INVALID_ARGUMENT);
    CHECK(database_find_url(database, 1, NULL, sizeof(original_url)) ==
          DATABASE_INVALID_ARGUMENT);
    CHECK(database_find_url(database, 1, original_url, 0) ==
          DATABASE_INVALID_ARGUMENT);

    database_close(database);
    database_close(NULL);
    return true;
}

int main(void) {
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"open initializes schema and insert can be found",
         test_open_initializes_schema_and_insert_can_be_found},
        {"duplicate URLs create distinct records",
         test_duplicate_urls_create_distinct_records},
        {"records persist after reopen", test_records_persist_after_reopen},
        {"find distinguishes not found and small buffer",
         test_find_distinguishes_not_found_and_small_buffer},
        {"invalid arguments are rejected", test_invalid_arguments_are_rejected},
    };
    size_t index = 0;
    size_t failures = 0;

    (void)snprintf(test_database_path, sizeof(test_database_path),
                   "/tmp/u_short_database_test_%ld.sqlite", (long)getpid());
    remove_test_database();

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            (void)fprintf(stderr, "FAIL: %s\n", tests[index].name);
            ++failures;
        }
    }

    remove_test_database();
    if (failures != 0) {
        (void)fprintf(stderr, "%zu database test(s) failed\n", failures);
        return 1;
    }

    (void)puts("database tests passed");
    return 0;
}
