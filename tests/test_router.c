#include "router.h"

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

static void make_request(http_request_t *request, http_method_t method,
                         const char *target, const char *body) {
    size_t body_length = strlen(body);

    (void)memset(request, 0, sizeof(*request));
    request->method = method;
    request->version = HTTP_VERSION_1_1;
    (void)snprintf(request->target, sizeof(request->target), "%s", target);
    (void)memcpy(request->body, body, body_length + 1U);
    request->body_length = body_length;
    request->has_content_length = body_length > 0U;
}

static bool load_web_asset(char *asset, size_t capacity, size_t *asset_length) {
    char asset_path[sizeof(__FILE__) + sizeof("../web/index.html")];
    char *last_separator;
    FILE *asset_file;
    size_t length;

    CHECK(asset != NULL);
    CHECK(capacity > HTTP_MAX_BODY_BYTES);
    CHECK(asset_length != NULL);
    (void)snprintf(asset_path, sizeof(asset_path), "%s", __FILE__);
    last_separator = strrchr(asset_path, '/');
    CHECK(last_separator != NULL);
    (void)snprintf(last_separator + 1,
                   sizeof(asset_path) - (size_t)(last_separator + 1 - asset_path),
                   "../web/index.html");

    asset_file = fopen(asset_path, "rb");
    CHECK(asset_file != NULL);
    length = fread(asset, 1U, capacity, asset_file);
    CHECK(!ferror(asset_file));
    CHECK(feof(asset_file));
    CHECK(fclose(asset_file) == 0);
    CHECK(length <= HTTP_MAX_BODY_BYTES);
    *asset_length = length;
    return true;
}

static bool test_root_page_and_method_handling(shortener_t *shortener) {
    char expected_body[HTTP_MAX_BODY_BYTES + 1U];
    size_t expected_length;
    http_request_t request;
    http_response_t response;

    CHECK(load_web_asset(expected_body, sizeof(expected_body),
                         &expected_length));

    make_request(&request, HTTP_METHOD_GET, "/", "");
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 200);
    CHECK(strcmp(response.content_type, "text/html; charset=utf-8") == 0);
    CHECK(response.body_length == expected_length);
    CHECK(response.body_length <= HTTP_MAX_BODY_BYTES);
    CHECK(memcmp(response.body, expected_body, expected_length) == 0);
    CHECK(response.body[response.body_length] == '\0');

    make_request(&request, HTTP_METHOD_GET, "/?x", "");
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 200);
    CHECK(strcmp(response.content_type, "text/html; charset=utf-8") == 0);
    CHECK(response.body_length == expected_length);
    CHECK(memcmp(response.body, expected_body, expected_length) == 0);

    make_request(&request, HTTP_METHOD_POST, "/", "");
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 405);
    CHECK(strcmp(response.allow, "GET") == 0);

    make_request(&request, HTTP_METHOD_OTHER, "/?x", "");
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 405);
    CHECK(strcmp(response.allow, "GET") == 0);
    return true;
}

static bool test_health_and_method_handling(shortener_t *shortener) {
    http_request_t request;
    http_response_t response;

    make_request(&request, HTTP_METHOD_GET, "/health?verbose=1", "");
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 200);
    CHECK(strcmp(response.content_type, "text/plain") == 0);
    CHECK(response.body_length == 2U);
    CHECK(memcmp(response.body, "OK", 2U) == 0);

    make_request(&request, HTTP_METHOD_POST, "/health", "");
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 405);
    CHECK(strcmp(response.allow, "GET") == 0);

    make_request(&request, HTTP_METHOD_GET, "/shorten", "");
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 405);
    CHECK(strcmp(response.allow, "POST") == 0);
    return true;
}

static bool test_create_redirect_and_errors(shortener_t *shortener) {
    http_request_t request;
    http_response_t response;

    make_request(&request, HTTP_METHOD_POST, "/shorten",
                 "{\"url\":\"https://example.com/a\"}");
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 201);
    CHECK(strcmp(response.content_type, "application/json") == 0);
    CHECK(strcmp(response.body,
                 "{\"code\":\"1\",\"short_url\":"
                 "\"http://localhost:8080/1\"}") == 0);

    make_request(&request, HTTP_METHOD_GET, "/1", "");
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 302);
    CHECK(strcmp(response.location, "https://example.com/a") == 0);
    CHECK(response.body_length == 0U);

    make_request(&request, HTTP_METHOD_GET, "/01", "");
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 400);
    make_request(&request, HTTP_METHOD_GET, "/Z", "");
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 404);

    make_request(&request, HTTP_METHOD_POST, "/shorten", "{bad json}");
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 400);
    make_request(&request, HTTP_METHOD_POST, "/shorten", "");
    request.has_content_length = false;
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 400);
    return true;
}

static bool test_unknown_paths_and_code_method(shortener_t *shortener) {
    http_request_t request;
    http_response_t response;

    make_request(&request, HTTP_METHOD_GET, "/a/b", "");
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 404);
    make_request(&request, HTTP_METHOD_POST, "/abc", "");
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 405);
    CHECK(strcmp(response.allow, "GET") == 0);
    make_request(&request, HTTP_METHOD_OTHER, "/unknown/path", "");
    router_handle(&request, shortener, &response);
    CHECK(response.status_code == 404);
    return true;
}

int main(void) {
    char database_path[128];
    database_t *database = NULL;
    shortener_t shortener;
    size_t failures = 0U;

    (void)snprintf(database_path, sizeof(database_path),
                   "/tmp/u_short_router_test_%ld.sqlite", (long)getpid());
    (void)unlink(database_path);
    if (database_open(database_path, &database) != DATABASE_OK ||
        shortener_init(&shortener, database) != SHORTENER_OK) {
        (void)fputs("router test setup failed\n", stderr);
        database_close(database);
        (void)unlink(database_path);
        return 1;
    }

    if (!test_root_page_and_method_handling(&shortener)) {
        ++failures;
    }
    if (!test_health_and_method_handling(&shortener)) {
        ++failures;
    }
    if (!test_create_redirect_and_errors(&shortener)) {
        ++failures;
    }
    if (!test_unknown_paths_and_code_method(&shortener)) {
        ++failures;
    }

    database_close(database);
    (void)unlink(database_path);
    if (failures != 0U) {
        (void)fprintf(stderr, "%zu router test group(s) failed\n", failures);
        return 1;
    }
    (void)puts("router tests passed");
    return 0;
}
