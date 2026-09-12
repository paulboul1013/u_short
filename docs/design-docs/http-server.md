# HTTP and server module design

## HTTP responsibility

Parse one bounded HTTP/1.0 or HTTP/1.1 request from bytes and serialize one
bounded response. The module also decodes the small JSON body contract used by
`POST /shorten`; it does not route requests.

## HTTP public interface

`include/http.h` defines bounded request and response value types plus:

- `http_parse_request(const char *bytes, size_t length,
  http_request_t *request)`
- `http_parse_url_json(const char *body, size_t length, char *url,
  size_t capacity)`
- `http_serialize_response(const http_response_t *response, char *bytes,
  size_t capacity, size_t *written)`
- one bounded response initializer plus setters for `Location` and `Allow`

Parser status distinguishes complete, incomplete, malformed, too large, and
unsupported transfer encoding. Header names are case-insensitive. Duplicate or
malformed `Content-Length` is invalid; its presence is retained as framing
metadata so Router can require it specifically for `POST /shorten`. Chunked and
other transfer encodings are unsupported. Parsed method, target, and body are
copied into fixed-capacity value types.

The JSON decoder accepts a valid object with exactly one string member named
`url`, permits unknown members and whitespace, implements JSON string escapes,
and rejects duplicate `url` members or malformed/trailing input.

The serializer emits a status line, bounded application headers,
`Content-Length`, `Connection: close`, a blank line, and the body. Header values
must not contain CR, LF, or other control characters.

## Server responsibility

Own the TCP lifecycle for a fixed `127.0.0.1:8080` listener. Each accepted
connection receives one request and is then closed.

## Server public interface

`include/server.h` exposes:

- `server_run(shortener_t *shortener)` returning zero on normal shutdown and
  non-zero on setup or unrecoverable socket failure

The module installs `SIGINT` and `SIGTERM` handlers that only set a
`sig_atomic_t` stop flag. Normal control flow closes the listener and client
sockets.

## Limits and failure behavior

- Maximum header section: 8 KiB
- Maximum body: 4 KiB
- Every client request has a two-second total deadline measured with a monotonic
  clock; receiving a slow byte does not reset it
- Reads continue until one complete request, a limit violation, EOF, or error
- Malformed framing returns 400; unsupported transfer encoding returns 501
- Socket failures are logged without reflecting internal details to clients
- Short writes and interrupted system calls are handled explicitly

## Verification

HTTP unit tests cover partial input, limits, framing, header casing,
Content-Length errors, transfer encoding, JSON escapes/errors, serialization, and
CR/LF rejection. Server behavior is verified through localhost acceptance tests.
