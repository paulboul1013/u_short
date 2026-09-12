#include "http.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_parse(const char *wire, size_t length,
                         http_parse_status_t expected) {
    http_request_t request;
    assert(http_parse_request(wire, length, &request) == expected);
}

static void test_parse_complete_requests(void) {
    static const char post[] =
        "POST /shorten HTTP/1.1\r\n"
        "host: localhost\r\n"
        "cOnTeNt-LeNgTh: 11\r\n\r\n"
        "{\"url\":\"x\"}";
    http_request_t request;

    assert(http_parse_request(post, sizeof(post) - 1U, &request) ==
           HTTP_PARSE_OK);
    assert(request.method == HTTP_METHOD_POST);
    assert(request.version == HTTP_VERSION_1_1);
    assert(strcmp(request.target, "/shorten") == 0);
    assert(request.body_length == 11U);
    assert(memcmp(request.body, "{\"url\":\"x\"}", 11U) == 0);
    assert(request.body[11] == '\0');
    assert(request.has_content_length);

    static const char get[] = "GET /health HTTP/1.0\r\nHost: x\r\n\r\n";
    assert(http_parse_request(get, sizeof(get) - 1U, &request) ==
           HTTP_PARSE_OK);
    assert(request.method == HTTP_METHOD_GET);
    assert(request.version == HTTP_VERSION_1_0);
    assert(request.body_length == 0U);
    assert(!request.has_content_length);

    static const char other[] = "PATCH /x HTTP/1.1\r\nHost: x\r\n\r\n";
    assert(http_parse_request(other, sizeof(other) - 1U, &request) ==
           HTTP_PARSE_OK);
    assert(request.method == HTTP_METHOD_OTHER);

    static const char multiline_json[] =
        "POST /shorten HTTP/1.1\r\nContent-Length: 31\r\n\r\n"
        "{\n\"url\":\"https://example.com\"\n}";
    assert(http_parse_request(multiline_json, sizeof(multiline_json) - 1U,
                              &request) == HTTP_PARSE_OK);
}

static void test_parse_partial_and_bounded_input(void) {
    static const char partial_headers[] = "GET / HTTP/1.1\r\nHost: x\r\n";
    static const char partial_body[] =
        "POST /shorten HTTP/1.1\r\nContent-Length: 4\r\n\r\nabc";
    char *large_headers = malloc(HTTP_MAX_HEADER_BYTES + 2U);
    char *large_body = malloc(HTTP_MAX_BODY_BYTES + 256U);
    assert(large_headers != NULL);
    assert(large_body != NULL);

    expect_parse(partial_headers, sizeof(partial_headers) - 1U,
                 HTTP_PARSE_INCOMPLETE);
    expect_parse(partial_body, sizeof(partial_body) - 1U,
                 HTTP_PARSE_INCOMPLETE);

    memset(large_headers, 'a', HTTP_MAX_HEADER_BYTES + 1U);
    expect_parse(large_headers, HTTP_MAX_HEADER_BYTES + 1U,
                 HTTP_PARSE_HEADERS_TOO_LARGE);

    int prefix = snprintf(large_body, HTTP_MAX_BODY_BYTES + 256U,
                          "POST /shorten HTTP/1.1\r\nContent-Length: %u\r\n\r\n",
                          HTTP_MAX_BODY_BYTES + 1U);
    assert(prefix > 0);
    memset(large_body + (size_t)prefix, 'x', HTTP_MAX_BODY_BYTES + 1U);
    expect_parse(large_body, (size_t)prefix + HTTP_MAX_BODY_BYTES + 1U,
                 HTTP_PARSE_BODY_TOO_LARGE);

    free(large_body);
    free(large_headers);
}

static void test_parse_rejects_ambiguous_or_bad_framing(void) {
    static const char missing[] =
        "POST /health HTTP/1.1\r\nHost: x\r\n\r\n";
    static const char duplicate_same[] =
        "POST /shorten HTTP/1.1\r\nContent-Length: 2\r\n"
        "Content-Length: 2\r\n\r\n{}";
    static const char duplicate_conflicting[] =
        "POST /shorten HTTP/1.1\r\nContent-Length: 2\r\n"
        "Content-Length: 3\r\n\r\n{}";
    static const char malformed[] =
        "POST /shorten HTTP/1.1\r\nContent-Length: +2\r\n\r\n{}";
    static const char trailing[] =
        "POST /shorten HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}x";

    expect_parse(missing, sizeof(missing) - 1U, HTTP_PARSE_OK);
    expect_parse(duplicate_same, sizeof(duplicate_same) - 1U,
                 HTTP_PARSE_BAD_REQUEST);
    expect_parse(duplicate_conflicting, sizeof(duplicate_conflicting) - 1U,
                 HTTP_PARSE_BAD_REQUEST);
    expect_parse(malformed, sizeof(malformed) - 1U, HTTP_PARSE_BAD_REQUEST);
    expect_parse(trailing, sizeof(trailing) - 1U, HTTP_PARSE_BAD_REQUEST);
}

static void test_parse_rejects_transfer_encoding(void) {
    static const char chunked[] =
        "POST /shorten HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
    static const char identity[] =
        "POST /shorten HTTP/1.1\r\nTransfer-Encoding: identity\r\n"
        "Content-Length: 0\r\n\r\n";

    expect_parse(chunked, sizeof(chunked) - 1U,
                 HTTP_PARSE_UNSUPPORTED_TRANSFER);
    expect_parse(identity, sizeof(identity) - 1U,
                 HTTP_PARSE_UNSUPPORTED_TRANSFER);
}

static void test_parse_rejects_malformed_http(void) {
    static const char bad_lf[] = "GET / HTTP/1.1\nHost: x\n\n";
    static const char bad_version[] = "GET / HTTP/2\r\nHost: x\r\n\r\n";
    static const char bad_header[] = "GET / HTTP/1.1\r\nNoColon\r\n\r\n";
    static const char empty_target[] = "GET  HTTP/1.1\r\n\r\n";

    expect_parse(bad_lf, sizeof(bad_lf) - 1U, HTTP_PARSE_BAD_REQUEST);
    expect_parse(bad_version, sizeof(bad_version) - 1U,
                 HTTP_PARSE_BAD_REQUEST);
    expect_parse(bad_header, sizeof(bad_header) - 1U,
                 HTTP_PARSE_BAD_REQUEST);
    expect_parse(empty_target, sizeof(empty_target) - 1U,
                 HTTP_PARSE_BAD_REQUEST);
}

static void expect_json(const char *json, http_json_status_t expected,
                        const char *expected_url) {
    char url[HTTP_MAX_LOCATION_BYTES + 1U];
    http_json_status_t status =
        http_parse_url_json(json, strlen(json), url, sizeof(url));
    assert(status == expected);
    if (expected == HTTP_JSON_OK) {
        assert(strcmp(url, expected_url) == 0);
    }
}

static void test_json_decodes_escapes_and_unicode(void) {
    expect_json(" { \"ignored\": [true, null, {\"n\":-1.25e+2}], "
                "\"url\": \"https:\\/\\/example.com\\/a\\tb\\\"c\" } ",
                HTTP_JSON_OK, "https://example.com/a\tb\"c");
    expect_json("{\"url\":\"https://example.com/\\u4F60\\u597D/"
                "\\uD83D\\uDE00\"}",
                HTTP_JSON_OK,
                "https://example.com/\xE4\xBD\xA0\xE5\xA5\xBD/"
                "\xF0\x9F\x98\x80");
    expect_json("{\"url\":\"https://example.com/\xE4\xBD\xA0\xE5\xA5\xBD\"}",
                HTTP_JSON_OK,
                "https://example.com/\xE4\xBD\xA0\xE5\xA5\xBD");
    expect_json("{\"\\u0075rl\":\"https://example.com\",\"x\":{}}",
                HTTP_JSON_OK, "https://example.com");
}

static void test_json_rejects_invalid_contract(void) {
    expect_json("{}", HTTP_JSON_INVALID, NULL);
    expect_json("{\"url\":1}", HTTP_JSON_INVALID, NULL);
    expect_json("{\"url\":\"a\",\"url\":\"b\"}", HTTP_JSON_INVALID,
                NULL);
    expect_json("{\"url\":\"a\"} trailing", HTTP_JSON_INVALID, NULL);
    expect_json("{\"url\":\"\\uD800\"}", HTTP_JSON_INVALID, NULL);
    expect_json("{\"url\":\"line\nbreak\"}", HTTP_JSON_INVALID, NULL);
    expect_json("[]", HTTP_JSON_INVALID, NULL);
}

static void test_json_reports_small_destination(void) {
    char url[4];
    assert(http_parse_url_json("{\"url\":\"abcd\"}", 14U, url,
                               sizeof(url)) == HTTP_JSON_BUFFER_TOO_SMALL);
}

static void test_response_serialization(void) {
    http_response_t response;
    char wire[512];
    size_t written = 0U;

    assert(http_response_init(&response, 201, "application/json", "{}", 2U));
    assert(http_response_set_location(&response, "https://example.com/a"));
    assert(http_response_set_allow(&response, "GET, POST"));
    assert(http_serialize_response(&response, wire, sizeof(wire), &written));
    wire[written] = '\0';
    assert(strcmp(wire,
                  "HTTP/1.1 201 Created\r\n"
                  "Content-Type: application/json\r\n"
                  "Location: https://example.com/a\r\n"
                  "Allow: GET, POST\r\n"
                  "Content-Length: 2\r\n"
                  "Connection: close\r\n\r\n{}") == 0);
}

static void test_response_rejects_injection_and_small_buffers(void) {
    http_response_t response;
    char wire[16];
    size_t written = 99U;

    assert(http_response_init(&response, 302, "text/plain", "", 0U));
    assert(!http_response_set_location(&response,
                                       "https://safe/\r\nX-Evil: yes"));
    assert(!http_response_set_allow(&response, "GET\x7fPOST"));
    assert(!http_response_init(&response, 200, "text/plain\nX: y", "OK",
                               2U));

    assert(http_response_init(&response, 200, "text/plain", "OK", 2U));
    memcpy(response.location, "x\r\ny", 5U);
    response.location[5] = '\0';
    assert(!http_serialize_response(&response, wire, sizeof(wire), &written));
    assert(written == 0U);

    response.location[0] = '\0';
    assert(!http_serialize_response(&response, wire, sizeof(wire), &written));
    assert(written == 0U);
}

int main(void) {
    test_parse_complete_requests();
    test_parse_partial_and_bounded_input();
    test_parse_rejects_ambiguous_or_bad_framing();
    test_parse_rejects_transfer_encoding();
    test_parse_rejects_malformed_http();
    test_json_decodes_escapes_and_unicode();
    test_json_rejects_invalid_contract();
    test_json_reports_small_destination();
    test_response_serialization();
    test_response_rejects_injection_and_small_buffers();
    puts("http tests passed");
    return 0;
}
