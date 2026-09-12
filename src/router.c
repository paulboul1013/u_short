#include "router.h"

#include <stdio.h>
#include <string.h>

static void set_text_response(http_response_t *response, int status_code,
                              const char *body) {
    (void)http_response_init(response, status_code, "text/plain", body,
                             strlen(body));
}

static void set_error(http_response_t *response, int status_code) {
    switch (status_code) {
        case 400:
            set_text_response(response, 400, "Bad Request");
            break;
        case 404:
            set_text_response(response, 404, "Not Found");
            break;
        case 405:
            set_text_response(response, 405, "Method Not Allowed");
            break;
        default:
            set_text_response(response, 500, "Internal Server Error");
            break;
    }
}

static size_t route_path_length(const char *target) {
    size_t length = 0U;

    while (target[length] != '\0' && target[length] != '?') {
        ++length;
    }
    return length;
}

static int path_equals(const char *target, size_t path_length,
                       const char *expected) {
    size_t expected_length = strlen(expected);

    return path_length == expected_length &&
           memcmp(target, expected, expected_length) == 0;
}

static int is_code_path(const char *target, size_t path_length) {
    size_t index;

    if (path_length == 0U || target[0] != '/') {
        return 0;
    }
    for (index = 1U; index < path_length; ++index) {
        if (target[index] == '/') {
            return 0;
        }
    }
    return 1;
}

static void handle_method_not_allowed(http_response_t *response,
                                      const char *allow) {
    set_error(response, 405);
    if (!http_response_set_allow(response, allow)) {
        set_error(response, 500);
    }
}

static void handle_shorten(const http_request_t *request,
                           shortener_t *shortener,
                           http_response_t *response) {
    char original_url[SHORTENER_MAX_URL_LENGTH + 1U];
    char short_code[SHORTENER_SHORT_CODE_BUFFER_SIZE];
    char short_url[SHORTENER_SHORT_URL_BUFFER_SIZE];
    char json[256];
    http_json_status_t json_status;
    shortener_status_t shortener_status;
    int json_length;

    if (!request->has_content_length) {
        set_error(response, 400);
        return;
    }
    json_status = http_parse_url_json(request->body, request->body_length,
                                      original_url, sizeof(original_url));
    if (json_status != HTTP_JSON_OK) {
        set_error(response, 400);
        return;
    }
    shortener_status = shortener_create(shortener, original_url, short_code,
                                        sizeof(short_code), short_url,
                                        sizeof(short_url));
    if (shortener_status == SHORTENER_INVALID) {
        set_error(response, 400);
        return;
    }
    if (shortener_status != SHORTENER_OK) {
        set_error(response, 500);
        return;
    }

    json_length = snprintf(json, sizeof(json),
                           "{\"code\":\"%s\",\"short_url\":\"%s\"}",
                           short_code, short_url);
    if (json_length < 0 || (size_t)json_length >= sizeof(json) ||
        !http_response_init(response, 201, "application/json", json,
                            (size_t)json_length)) {
        set_error(response, 500);
    }
}

static void handle_redirect(const char *target, size_t path_length,
                            shortener_t *shortener,
                            http_response_t *response) {
    char short_code[HTTP_MAX_TARGET_BYTES + 1U];
    char original_url[SHORTENER_MAX_URL_LENGTH + 1U];
    size_t code_length = path_length - 1U;
    shortener_status_t status;

    (void)memcpy(short_code, target + 1U, code_length);
    short_code[code_length] = '\0';

    status = shortener_resolve(shortener, short_code, original_url,
                               sizeof(original_url));
    if (status == SHORTENER_INVALID) {
        set_error(response, 400);
        return;
    }
    if (status == SHORTENER_NOT_FOUND) {
        set_error(response, 404);
        return;
    }
    if (status != SHORTENER_OK ||
        !http_response_init(response, 302, "", "", 0U) ||
        !http_response_set_location(response, original_url)) {
        set_error(response, 500);
    }
}

void router_handle(const http_request_t *request, shortener_t *shortener,
                   http_response_t *response) {
    size_t path_length;

    if (response == NULL) {
        return;
    }
    if (request == NULL || shortener == NULL) {
        set_error(response, 500);
        return;
    }
    path_length = route_path_length(request->target);

    if (path_equals(request->target, path_length, "/health")) {
        if (request->method != HTTP_METHOD_GET) {
            handle_method_not_allowed(response, "GET");
        } else {
            set_text_response(response, 200, "OK");
        }
        return;
    }
    if (path_equals(request->target, path_length, "/shorten")) {
        if (request->method != HTTP_METHOD_POST) {
            handle_method_not_allowed(response, "POST");
        } else {
            handle_shorten(request, shortener, response);
        }
        return;
    }
    if (is_code_path(request->target, path_length)) {
        if (request->method != HTTP_METHOD_GET) {
            handle_method_not_allowed(response, "GET");
        } else {
            handle_redirect(request->target, path_length, shortener, response);
        }
        return;
    }
    set_error(response, 404);
}
