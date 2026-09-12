#include "http.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const unsigned char *current;
    const unsigned char *end;
    unsigned int depth;
} json_parser_t;

static bool ascii_equal_ci(const char *left, size_t left_length,
                           const char *right) {
    size_t index = 0U;
    while (right[index] != '\0') {
        unsigned char a;
        unsigned char b;
        if (index >= left_length) {
            return false;
        }
        a = (unsigned char)left[index];
        b = (unsigned char)right[index];
        if (a >= (unsigned char)'A' && a <= (unsigned char)'Z') {
            a = (unsigned char)(a + ((unsigned char)'a' - (unsigned char)'A'));
        }
        if (b >= (unsigned char)'A' && b <= (unsigned char)'Z') {
            b = (unsigned char)(b + ((unsigned char)'a' - (unsigned char)'A'));
        }
        if (a != b) {
            return false;
        }
        ++index;
    }
    return index == left_length;
}

static bool is_token_char(unsigned char character) {
    if ((character >= (unsigned char)'a' && character <= (unsigned char)'z') ||
        (character >= (unsigned char)'A' && character <= (unsigned char)'Z') ||
        (character >= (unsigned char)'0' && character <= (unsigned char)'9')) {
        return true;
    }
    return strchr("!#$%&'*+-.^_`|~", (int)character) != NULL;
}

static bool contains_bad_line_ending(const char *bytes, size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        if (bytes[index] == '\n' &&
            (index == 0U || bytes[index - 1U] != '\r')) {
            return true;
        }
        if (bytes[index] == '\r' && index + 1U < length &&
            bytes[index + 1U] != '\n') {
            return true;
        }
    }
    return false;
}

static const char *find_bytes(const char *bytes, size_t length,
                              const char *needle, size_t needle_length) {
    size_t index;
    if (needle_length > length) {
        return NULL;
    }
    for (index = 0U; index <= length - needle_length; ++index) {
        if (memcmp(bytes + index, needle, needle_length) == 0) {
            return bytes + index;
        }
    }
    return NULL;
}

static bool parse_decimal_size(const char *text, size_t length,
                               size_t *value) {
    size_t result = 0U;
    size_t index;
    if (length == 0U) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        unsigned int digit;
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        digit = (unsigned int)(text[index] - '0');
        if (result > (SIZE_MAX - digit) / 10U) {
            return false;
        }
        result = result * 10U + digit;
    }
    *value = result;
    return true;
}

http_parse_status_t http_parse_request(const char *bytes, size_t length,
                                       http_request_t *request) {
    const char *header_marker;
    const char *header_end;
    const char *line_end;
    const char *cursor;
    const char *first_space;
    const char *second_space;
    size_t header_bytes;
    size_t content_length = 0U;
    unsigned int content_length_count = 0U;
    bool has_transfer_encoding = false;

    if (bytes == NULL || request == NULL) {
        return HTTP_PARSE_BAD_REQUEST;
    }
    header_marker = find_bytes(bytes, length, "\r\n\r\n", 4U);
    if (header_marker == NULL) {
        if (contains_bad_line_ending(bytes, length)) {
            return HTTP_PARSE_BAD_REQUEST;
        }
        return length > HTTP_MAX_HEADER_BYTES ? HTTP_PARSE_HEADERS_TOO_LARGE
                                              : HTTP_PARSE_INCOMPLETE;
    }
    header_end = header_marker + 4U;
    header_bytes = (size_t)(header_end - bytes);
    if (contains_bad_line_ending(bytes, header_bytes)) {
        return HTTP_PARSE_BAD_REQUEST;
    }
    if (header_bytes > HTTP_MAX_HEADER_BYTES) {
        return HTTP_PARSE_HEADERS_TOO_LARGE;
    }

    line_end = find_bytes(bytes, (size_t)(header_marker - bytes) + 2U,
                          "\r\n", 2U);
    if (line_end == NULL || line_end == bytes) {
        return HTTP_PARSE_BAD_REQUEST;
    }
    first_space = memchr(bytes, ' ', (size_t)(line_end - bytes));
    if (first_space == NULL || first_space == bytes) {
        return HTTP_PARSE_BAD_REQUEST;
    }
    second_space = memchr(first_space + 1, ' ',
                          (size_t)(line_end - (first_space + 1)));
    if (second_space == NULL || second_space == first_space + 1 ||
        memchr(second_space + 1, ' ',
               (size_t)(line_end - (second_space + 1))) != NULL) {
        return HTTP_PARSE_BAD_REQUEST;
    }
    for (cursor = bytes; cursor < first_space; ++cursor) {
        if (!is_token_char((unsigned char)*cursor)) {
            return HTTP_PARSE_BAD_REQUEST;
        }
    }
    if ((size_t)(first_space - bytes) == 3U &&
        memcmp(bytes, "GET", 3U) == 0) {
        request->method = HTTP_METHOD_GET;
    } else if ((size_t)(first_space - bytes) == 4U &&
               memcmp(bytes, "POST", 4U) == 0) {
        request->method = HTTP_METHOD_POST;
    } else {
        request->method = HTTP_METHOD_OTHER;
    }

    if ((size_t)(line_end - (second_space + 1)) == 8U &&
        memcmp(second_space + 1, "HTTP/1.0", 8U) == 0) {
        request->version = HTTP_VERSION_1_0;
    } else if ((size_t)(line_end - (second_space + 1)) == 8U &&
               memcmp(second_space + 1, "HTTP/1.1", 8U) == 0) {
        request->version = HTTP_VERSION_1_1;
    } else {
        return HTTP_PARSE_BAD_REQUEST;
    }

    {
        size_t target_length = (size_t)(second_space - (first_space + 1));
        size_t index;
        if (target_length > HTTP_MAX_TARGET_BYTES) {
            return HTTP_PARSE_BAD_REQUEST;
        }
        for (index = 0U; index < target_length; ++index) {
            unsigned char character = (unsigned char)first_space[1 + index];
            if (character <= 0x20U || character == 0x7fU) {
                return HTTP_PARSE_BAD_REQUEST;
            }
        }
        memcpy(request->target, first_space + 1, target_length);
        request->target[target_length] = '\0';
    }

    cursor = line_end + 2U;
    while (cursor < header_marker) {
        const char *current_end = find_bytes(
            cursor, (size_t)(header_marker - cursor) + 2U, "\r\n", 2U);
        const char *colon;
        const char *value_start;
        const char *value_end;
        const char *scan;
        if (current_end == NULL || current_end == cursor) {
            return HTTP_PARSE_BAD_REQUEST;
        }
        colon = memchr(cursor, ':', (size_t)(current_end - cursor));
        if (colon == NULL || colon == cursor) {
            return HTTP_PARSE_BAD_REQUEST;
        }
        for (scan = cursor; scan < colon; ++scan) {
            if (!is_token_char((unsigned char)*scan)) {
                return HTTP_PARSE_BAD_REQUEST;
            }
        }
        value_start = colon + 1;
        while (value_start < current_end &&
               (*value_start == ' ' || *value_start == '\t')) {
            ++value_start;
        }
        value_end = current_end;
        while (value_end > value_start &&
               (value_end[-1] == ' ' || value_end[-1] == '\t')) {
            --value_end;
        }
        for (scan = value_start; scan < value_end; ++scan) {
            unsigned char character = (unsigned char)*scan;
            if ((character < 0x20U && character != (unsigned char)'\t') ||
                character == 0x7fU) {
                return HTTP_PARSE_BAD_REQUEST;
            }
        }
        if (ascii_equal_ci(cursor, (size_t)(colon - cursor),
                           "Content-Length")) {
            ++content_length_count;
            if (content_length_count > 1U ||
                !parse_decimal_size(value_start,
                                    (size_t)(value_end - value_start),
                                    &content_length)) {
                return HTTP_PARSE_BAD_REQUEST;
            }
        } else if (ascii_equal_ci(cursor, (size_t)(colon - cursor),
                                  "Transfer-Encoding")) {
            has_transfer_encoding = true;
        }
        cursor = current_end + 2U;
    }

    if (has_transfer_encoding) {
        return HTTP_PARSE_UNSUPPORTED_TRANSFER;
    }
    request->has_content_length = content_length_count == 1U;
    if (content_length > HTTP_MAX_BODY_BYTES) {
        return HTTP_PARSE_BODY_TOO_LARGE;
    }
    if (length - header_bytes < content_length) {
        return HTTP_PARSE_INCOMPLETE;
    }
    if (length - header_bytes != content_length) {
        return HTTP_PARSE_BAD_REQUEST;
    }
    if (content_length > 0U) {
        memcpy(request->body, header_end, content_length);
    }
    request->body[content_length] = '\0';
    request->body_length = content_length;
    return HTTP_PARSE_OK;
}

static void json_skip_whitespace(json_parser_t *parser) {
    while (parser->current < parser->end &&
           (*parser->current == (unsigned char)' ' ||
            *parser->current == (unsigned char)'\t' ||
            *parser->current == (unsigned char)'\r' ||
            *parser->current == (unsigned char)'\n')) {
        ++parser->current;
    }
}

static int hex_value(unsigned char character) {
    if (character >= (unsigned char)'0' && character <= (unsigned char)'9') {
        return (int)(character - (unsigned char)'0');
    }
    if (character >= (unsigned char)'a' && character <= (unsigned char)'f') {
        return 10 + (int)(character - (unsigned char)'a');
    }
    if (character >= (unsigned char)'A' && character <= (unsigned char)'F') {
        return 10 + (int)(character - (unsigned char)'A');
    }
    return -1;
}

static bool parse_hex4(json_parser_t *parser, uint32_t *value) {
    unsigned int index;
    uint32_t result = 0U;
    if ((size_t)(parser->end - parser->current) < 4U) {
        return false;
    }
    for (index = 0U; index < 4U; ++index) {
        int digit = hex_value(parser->current[index]);
        if (digit < 0) {
            return false;
        }
        result = result * 16U + (uint32_t)digit;
    }
    parser->current += 4U;
    *value = result;
    return true;
}

static bool append_byte(unsigned char *output, size_t capacity, size_t *length,
                        unsigned char byte) {
    if (*length >= capacity) {
        return false;
    }
    output[*length] = byte;
    ++*length;
    return true;
}

static bool append_codepoint(unsigned char *output, size_t capacity,
                             size_t *length, uint32_t codepoint) {
    if (codepoint <= 0x7fU) {
        return append_byte(output, capacity, length,
                           (unsigned char)codepoint);
    }
    if (codepoint <= 0x7ffU) {
        return append_byte(output, capacity, length,
                           (unsigned char)(0xc0U | (codepoint >> 6U))) &&
               append_byte(output, capacity, length,
                           (unsigned char)(0x80U | (codepoint & 0x3fU)));
    }
    if (codepoint <= 0xffffU) {
        return append_byte(output, capacity, length,
                           (unsigned char)(0xe0U | (codepoint >> 12U))) &&
               append_byte(output, capacity, length,
                           (unsigned char)(0x80U |
                                           ((codepoint >> 6U) & 0x3fU))) &&
               append_byte(output, capacity, length,
                           (unsigned char)(0x80U | (codepoint & 0x3fU)));
    }
    return append_byte(output, capacity, length,
                       (unsigned char)(0xf0U | (codepoint >> 18U))) &&
           append_byte(output, capacity, length,
                       (unsigned char)(0x80U |
                                       ((codepoint >> 12U) & 0x3fU))) &&
           append_byte(output, capacity, length,
                       (unsigned char)(0x80U |
                                       ((codepoint >> 6U) & 0x3fU))) &&
           append_byte(output, capacity, length,
                       (unsigned char)(0x80U | (codepoint & 0x3fU)));
}

static size_t valid_utf8_length(const unsigned char *text, size_t available) {
    unsigned char first;
    if (available == 0U) {
        return 0U;
    }
    first = text[0];
    if (first < 0x80U) {
        return 1U;
    }
    if (first >= 0xc2U && first <= 0xdfU && available >= 2U &&
        text[1] >= 0x80U && text[1] <= 0xbfU) {
        return 2U;
    }
    if (first >= 0xe0U && first <= 0xefU && available >= 3U &&
        text[1] >= 0x80U && text[1] <= 0xbfU && text[2] >= 0x80U &&
        text[2] <= 0xbfU && !(first == 0xe0U && text[1] < 0xa0U) &&
        !(first == 0xedU && text[1] >= 0xa0U)) {
        return 3U;
    }
    if (first >= 0xf0U && first <= 0xf4U && available >= 4U &&
        text[1] >= 0x80U && text[1] <= 0xbfU && text[2] >= 0x80U &&
        text[2] <= 0xbfU && text[3] >= 0x80U && text[3] <= 0xbfU &&
        !(first == 0xf0U && text[1] < 0x90U) &&
        !(first == 0xf4U && text[1] > 0x8fU)) {
        return 4U;
    }
    return 0U;
}

static bool json_parse_string(json_parser_t *parser, unsigned char *output,
                              size_t capacity, size_t *output_length) {
    size_t length = 0U;
    if (parser->current >= parser->end ||
        *parser->current != (unsigned char)'\"') {
        return false;
    }
    ++parser->current;
    while (parser->current < parser->end) {
        unsigned char character = *parser->current++;
        if (character == (unsigned char)'\"') {
            *output_length = length;
            return true;
        }
        if (character < 0x20U) {
            return false;
        }
        if (character == (unsigned char)'\\') {
            uint32_t codepoint;
            if (parser->current >= parser->end) {
                return false;
            }
            character = *parser->current++;
            switch (character) {
                case (unsigned char)'\"':
                case (unsigned char)'\\':
                case (unsigned char)'/':
                    if (!append_byte(output, capacity, &length, character)) {
                        return false;
                    }
                    break;
                case (unsigned char)'b':
                    if (!append_byte(output, capacity, &length, 0x08U)) {
                        return false;
                    }
                    break;
                case (unsigned char)'f':
                    if (!append_byte(output, capacity, &length, 0x0cU)) {
                        return false;
                    }
                    break;
                case (unsigned char)'n':
                    if (!append_byte(output, capacity, &length, (unsigned char)'\n')) {
                        return false;
                    }
                    break;
                case (unsigned char)'r':
                    if (!append_byte(output, capacity, &length, (unsigned char)'\r')) {
                        return false;
                    }
                    break;
                case (unsigned char)'t':
                    if (!append_byte(output, capacity, &length, (unsigned char)'\t')) {
                        return false;
                    }
                    break;
                case (unsigned char)'u':
                    if (!parse_hex4(parser, &codepoint)) {
                        return false;
                    }
                    if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
                        uint32_t low;
                        if ((size_t)(parser->end - parser->current) < 6U ||
                            parser->current[0] != (unsigned char)'\\' ||
                            parser->current[1] != (unsigned char)'u') {
                            return false;
                        }
                        parser->current += 2U;
                        if (!parse_hex4(parser, &low) || low < 0xdc00U ||
                            low > 0xdfffU) {
                            return false;
                        }
                        codepoint = 0x10000U +
                                    ((codepoint - 0xd800U) << 10U) +
                                    (low - 0xdc00U);
                    } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
                        return false;
                    }
                    if (!append_codepoint(output, capacity, &length, codepoint)) {
                        return false;
                    }
                    break;
                default:
                    return false;
            }
        } else if (character < 0x80U) {
            if (!append_byte(output, capacity, &length, character)) {
                return false;
            }
        } else {
            size_t sequence_length = valid_utf8_length(
                parser->current - 1,
                (size_t)(parser->end - (parser->current - 1)));
            size_t index;
            if (sequence_length == 0U) {
                return false;
            }
            for (index = 0U; index < sequence_length; ++index) {
                if (!append_byte(output, capacity, &length,
                                 (parser->current - 1)[index])) {
                    return false;
                }
            }
            parser->current += sequence_length - 1U;
        }
    }
    return false;
}

static bool json_parse_value(json_parser_t *parser);

static bool json_parse_literal(json_parser_t *parser, const char *literal) {
    size_t length = strlen(literal);
    if ((size_t)(parser->end - parser->current) < length ||
        memcmp(parser->current, literal, length) != 0) {
        return false;
    }
    parser->current += length;
    return true;
}

static bool json_parse_number(json_parser_t *parser) {
    const unsigned char *cursor = parser->current;
    if (cursor < parser->end && *cursor == (unsigned char)'-') {
        ++cursor;
    }
    if (cursor >= parser->end) {
        return false;
    }
    if (*cursor == (unsigned char)'0') {
        ++cursor;
        if (cursor < parser->end && *cursor >= (unsigned char)'0' &&
            *cursor <= (unsigned char)'9') {
            return false;
        }
    } else if (*cursor >= (unsigned char)'1' && *cursor <= (unsigned char)'9') {
        do {
            ++cursor;
        } while (cursor < parser->end && *cursor >= (unsigned char)'0' &&
                 *cursor <= (unsigned char)'9');
    } else {
        return false;
    }
    if (cursor < parser->end && *cursor == (unsigned char)'.') {
        ++cursor;
        if (cursor >= parser->end || *cursor < (unsigned char)'0' ||
            *cursor > (unsigned char)'9') {
            return false;
        }
        do {
            ++cursor;
        } while (cursor < parser->end && *cursor >= (unsigned char)'0' &&
                 *cursor <= (unsigned char)'9');
    }
    if (cursor < parser->end &&
        (*cursor == (unsigned char)'e' || *cursor == (unsigned char)'E')) {
        ++cursor;
        if (cursor < parser->end &&
            (*cursor == (unsigned char)'+' || *cursor == (unsigned char)'-')) {
            ++cursor;
        }
        if (cursor >= parser->end || *cursor < (unsigned char)'0' ||
            *cursor > (unsigned char)'9') {
            return false;
        }
        do {
            ++cursor;
        } while (cursor < parser->end && *cursor >= (unsigned char)'0' &&
                 *cursor <= (unsigned char)'9');
    }
    parser->current = cursor;
    return true;
}

static bool json_parse_array(json_parser_t *parser) {
    if (++parser->depth > 64U) {
        return false;
    }
    ++parser->current;
    json_skip_whitespace(parser);
    if (parser->current < parser->end &&
        *parser->current == (unsigned char)']') {
        ++parser->current;
        --parser->depth;
        return true;
    }
    for (;;) {
        if (!json_parse_value(parser)) {
            return false;
        }
        json_skip_whitespace(parser);
        if (parser->current >= parser->end) {
            return false;
        }
        if (*parser->current == (unsigned char)']') {
            ++parser->current;
            --parser->depth;
            return true;
        }
        if (*parser->current++ != (unsigned char)',') {
            return false;
        }
        json_skip_whitespace(parser);
    }
}

static bool json_parse_object(json_parser_t *parser) {
    unsigned char scratch[HTTP_MAX_BODY_BYTES + 1U];
    size_t ignored_length;
    if (++parser->depth > 64U) {
        return false;
    }
    ++parser->current;
    json_skip_whitespace(parser);
    if (parser->current < parser->end &&
        *parser->current == (unsigned char)'}') {
        ++parser->current;
        --parser->depth;
        return true;
    }
    for (;;) {
        if (!json_parse_string(parser, scratch, sizeof(scratch),
                               &ignored_length)) {
            return false;
        }
        json_skip_whitespace(parser);
        if (parser->current >= parser->end ||
            *parser->current++ != (unsigned char)':') {
            return false;
        }
        json_skip_whitespace(parser);
        if (!json_parse_value(parser)) {
            return false;
        }
        json_skip_whitespace(parser);
        if (parser->current >= parser->end) {
            return false;
        }
        if (*parser->current == (unsigned char)'}') {
            ++parser->current;
            --parser->depth;
            return true;
        }
        if (*parser->current++ != (unsigned char)',') {
            return false;
        }
        json_skip_whitespace(parser);
    }
}

static bool json_parse_value(json_parser_t *parser) {
    unsigned char scratch[HTTP_MAX_BODY_BYTES + 1U];
    size_t ignored_length;
    if (parser->current >= parser->end) {
        return false;
    }
    switch (*parser->current) {
        case (unsigned char)'\"':
            return json_parse_string(parser, scratch, sizeof(scratch),
                                     &ignored_length);
        case (unsigned char)'{':
            return json_parse_object(parser);
        case (unsigned char)'[':
            return json_parse_array(parser);
        case (unsigned char)'t':
            return json_parse_literal(parser, "true");
        case (unsigned char)'f':
            return json_parse_literal(parser, "false");
        case (unsigned char)'n':
            return json_parse_literal(parser, "null");
        default:
            return json_parse_number(parser);
    }
}

http_json_status_t http_parse_url_json(const char *body, size_t length,
                                       char *url, size_t capacity) {
    json_parser_t parser;
    unsigned char key[HTTP_MAX_BODY_BYTES + 1U];
    unsigned char decoded_url[HTTP_MAX_BODY_BYTES + 1U];
    size_t key_length;
    size_t decoded_length = 0U;
    bool found_url = false;

    if (body == NULL || url == NULL || capacity == 0U ||
        length > HTTP_MAX_BODY_BYTES) {
        return HTTP_JSON_INVALID;
    }
    parser.current = (const unsigned char *)body;
    parser.end = parser.current + length;
    parser.depth = 0U;
    json_skip_whitespace(&parser);
    if (parser.current >= parser.end ||
        *parser.current++ != (unsigned char)'{') {
        return HTTP_JSON_INVALID;
    }
    json_skip_whitespace(&parser);
    if (parser.current < parser.end &&
        *parser.current == (unsigned char)'}') {
        return HTTP_JSON_INVALID;
    }
    for (;;) {
        bool is_url;
        if (!json_parse_string(&parser, key, sizeof(key), &key_length)) {
            return HTTP_JSON_INVALID;
        }
        is_url = key_length == 3U && memcmp(key, "url", 3U) == 0;
        json_skip_whitespace(&parser);
        if (parser.current >= parser.end ||
            *parser.current++ != (unsigned char)':') {
            return HTTP_JSON_INVALID;
        }
        json_skip_whitespace(&parser);
        if (is_url) {
            if (found_url ||
                !json_parse_string(&parser, decoded_url,
                                   sizeof(decoded_url) - 1U,
                                   &decoded_length)) {
                return HTTP_JSON_INVALID;
            }
            found_url = true;
        } else if (!json_parse_value(&parser)) {
            return HTTP_JSON_INVALID;
        }
        json_skip_whitespace(&parser);
        if (parser.current >= parser.end) {
            return HTTP_JSON_INVALID;
        }
        if (*parser.current == (unsigned char)'}') {
            ++parser.current;
            break;
        }
        if (*parser.current++ != (unsigned char)',') {
            return HTTP_JSON_INVALID;
        }
        json_skip_whitespace(&parser);
    }
    json_skip_whitespace(&parser);
    if (parser.current != parser.end || !found_url ||
        memchr(decoded_url, '\0', decoded_length) != NULL) {
        return HTTP_JSON_INVALID;
    }
    if (decoded_length >= capacity) {
        return HTTP_JSON_BUFFER_TOO_SMALL;
    }
    memcpy(url, decoded_url, decoded_length);
    url[decoded_length] = '\0';
    return HTTP_JSON_OK;
}

static bool bounded_c_string(const char *text, size_t capacity,
                             size_t *length) {
    size_t index;
    if (text == NULL) {
        return false;
    }
    for (index = 0U; index < capacity; ++index) {
        if (text[index] == '\0') {
            *length = index;
            return true;
        }
    }
    return false;
}

static bool valid_header_value(const char *value, size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        unsigned char character = (unsigned char)value[index];
        if (character < 0x20U || character == 0x7fU) {
            return false;
        }
    }
    return true;
}

bool http_response_init(http_response_t *response, int status_code,
                        const char *content_type, const char *body,
                        size_t body_length) {
    size_t content_type_length;
    if (response == NULL || content_type == NULL ||
        (body == NULL && body_length != 0U) ||
        body_length > HTTP_MAX_BODY_BYTES ||
        !bounded_c_string(content_type, HTTP_MAX_CONTENT_TYPE_BYTES,
                          &content_type_length) ||
        !valid_header_value(content_type, content_type_length)) {
        return false;
    }
    memset(response, 0, sizeof(*response));
    response->status_code = status_code;
    memcpy(response->content_type, content_type, content_type_length + 1U);
    if (body_length > 0U) {
        memcpy(response->body, body, body_length);
    }
    response->body[body_length] = '\0';
    response->body_length = body_length;
    return true;
}

bool http_response_set_location(http_response_t *response,
                                const char *location) {
    size_t length;
    if (response == NULL ||
        !bounded_c_string(location, HTTP_MAX_LOCATION_BYTES + 1U, &length) ||
        !valid_header_value(location, length)) {
        return false;
    }
    memcpy(response->location, location, length + 1U);
    return true;
}

bool http_response_set_allow(http_response_t *response, const char *allow) {
    size_t length;
    if (response == NULL ||
        !bounded_c_string(allow, HTTP_MAX_ALLOW_BYTES, &length) ||
        !valid_header_value(allow, length)) {
        return false;
    }
    memcpy(response->allow, allow, length + 1U);
    return true;
}

static const char *reason_phrase(int status_code) {
    switch (status_code) {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 302:
            return "Found";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        default:
            return NULL;
    }
}

static bool append_bytes(char *output, size_t capacity, size_t *length,
                         const char *text, size_t text_length) {
    if (text_length > capacity - *length) {
        return false;
    }
    memcpy(output + *length, text, text_length);
    *length += text_length;
    return true;
}

static bool append_c_string(char *output, size_t capacity, size_t *length,
                            const char *text) {
    return append_bytes(output, capacity, length, text, strlen(text));
}

bool http_serialize_response(const http_response_t *response, char *bytes,
                             size_t capacity, size_t *written) {
    const char *phrase;
    size_t content_type_length;
    size_t location_length;
    size_t allow_length;
    size_t length = 0U;
    char number[32];
    int number_length;

    if (written != NULL) {
        *written = 0U;
    }
    if (response == NULL || bytes == NULL || written == NULL ||
        response->body_length > HTTP_MAX_BODY_BYTES ||
        !bounded_c_string(response->content_type,
                          HTTP_MAX_CONTENT_TYPE_BYTES, &content_type_length) ||
        !bounded_c_string(response->location,
                          HTTP_MAX_LOCATION_BYTES + 1U, &location_length) ||
        !bounded_c_string(response->allow, HTTP_MAX_ALLOW_BYTES,
                          &allow_length) ||
        !valid_header_value(response->content_type, content_type_length) ||
        !valid_header_value(response->location, location_length) ||
        !valid_header_value(response->allow, allow_length)) {
        return false;
    }
    phrase = reason_phrase(response->status_code);
    if (phrase == NULL) {
        return false;
    }
    number_length = snprintf(number, sizeof(number), "%d", response->status_code);
    if (number_length < 0 || (size_t)number_length >= sizeof(number) ||
        !append_c_string(bytes, capacity, &length, "HTTP/1.1 ") ||
        !append_bytes(bytes, capacity, &length, number, (size_t)number_length) ||
        !append_c_string(bytes, capacity, &length, " ") ||
        !append_c_string(bytes, capacity, &length, phrase) ||
        !append_c_string(bytes, capacity, &length, "\r\n")) {
        return false;
    }
    if (content_type_length > 0U &&
        (!append_c_string(bytes, capacity, &length, "Content-Type: ") ||
         !append_bytes(bytes, capacity, &length, response->content_type,
                       content_type_length) ||
         !append_c_string(bytes, capacity, &length, "\r\n"))) {
        return false;
    }
    if (location_length > 0U &&
        (!append_c_string(bytes, capacity, &length, "Location: ") ||
         !append_bytes(bytes, capacity, &length, response->location,
                       location_length) ||
         !append_c_string(bytes, capacity, &length, "\r\n"))) {
        return false;
    }
    if (allow_length > 0U &&
        (!append_c_string(bytes, capacity, &length, "Allow: ") ||
         !append_bytes(bytes, capacity, &length, response->allow,
                       allow_length) ||
         !append_c_string(bytes, capacity, &length, "\r\n"))) {
        return false;
    }
    number_length = snprintf(number, sizeof(number), "%zu", response->body_length);
    if (number_length < 0 || (size_t)number_length >= sizeof(number) ||
        !append_c_string(bytes, capacity, &length, "Content-Length: ") ||
        !append_bytes(bytes, capacity, &length, number, (size_t)number_length) ||
        !append_c_string(bytes, capacity, &length,
                         "\r\nConnection: close\r\n\r\n") ||
        !append_bytes(bytes, capacity, &length, response->body,
                      response->body_length)) {
        return false;
    }
    *written = length;
    return true;
}
