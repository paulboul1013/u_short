#ifndef URL_SHORTENER_HTTP_H
#define URL_SHORTENER_HTTP_H

#include <stdbool.h>
#include <stddef.h>

#define HTTP_MAX_HEADER_BYTES 8192U
#define HTTP_MAX_BODY_BYTES 4096U
#define HTTP_MAX_TARGET_BYTES 2048U
#define HTTP_MAX_CONTENT_TYPE_BYTES 64U
#define HTTP_MAX_LOCATION_BYTES 2048U
#define HTTP_MAX_ALLOW_BYTES 16U
#define HTTP_MAX_RESPONSE_BYTES 16384U

typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST,
    HTTP_METHOD_OTHER
} http_method_t;

typedef enum {
    HTTP_VERSION_1_0 = 0,
    HTTP_VERSION_1_1
} http_version_t;

typedef struct {
    http_method_t method;
    http_version_t version;
    char target[HTTP_MAX_TARGET_BYTES + 1U];
    char body[HTTP_MAX_BODY_BYTES + 1U];
    size_t body_length;
    bool has_content_length;
} http_request_t;

typedef struct {
    int status_code;
    char content_type[HTTP_MAX_CONTENT_TYPE_BYTES];
    char location[HTTP_MAX_LOCATION_BYTES + 1U];
    char allow[HTTP_MAX_ALLOW_BYTES];
    char body[HTTP_MAX_BODY_BYTES + 1U];
    size_t body_length;
} http_response_t;

typedef enum {
    HTTP_PARSE_OK = 0,
    HTTP_PARSE_INCOMPLETE,
    HTTP_PARSE_BAD_REQUEST,
    HTTP_PARSE_HEADERS_TOO_LARGE,
    HTTP_PARSE_BODY_TOO_LARGE,
    HTTP_PARSE_UNSUPPORTED_TRANSFER
} http_parse_status_t;

typedef enum {
    HTTP_JSON_OK = 0,
    HTTP_JSON_INVALID,
    HTTP_JSON_BUFFER_TOO_SMALL
} http_json_status_t;

http_parse_status_t http_parse_request(const char *bytes, size_t length,
                                       http_request_t *request);
http_json_status_t http_parse_url_json(const char *body, size_t length,
                                       char *url, size_t capacity);

bool http_response_init(http_response_t *response, int status_code,
                        const char *content_type, const char *body,
                        size_t body_length);
bool http_response_set_location(http_response_t *response,
                                const char *location);
bool http_response_set_allow(http_response_t *response, const char *allow);
bool http_serialize_response(const http_response_t *response, char *bytes,
                             size_t capacity, size_t *written);

#endif
