# Routing module design

## Responsibility

Map a parsed HTTP request to one application handler and translate handler results
into an HTTP response. Router owns no sockets, HTTP parsing, or SQL.

## Route priority

1. exact `/` browser-page route
2. `/health`
3. `/shorten`
4. `/{code}` when the path contains exactly one non-empty segment
5. fallback `404`

The path is matched without its optional query string, so `GET /?x` selects the
same handler as `GET /`. Paths are not percent-decoded.

## Method behavior

| Path | Allowed method | Other methods |
|---|---|---|
| `/` | `GET` | `405`, `Allow: GET` |
| `/health` | `GET` | `405`, `Allow: GET` |
| `/shorten` | `POST` | `405`, `Allow: POST` |
| `/{code}` | `GET` | `405`, `Allow: GET` |
| unknown | none | `404` |

## Public interface

`include/router.h` exposes one operation:

- `router_handle(const http_request_t *request, shortener_t *shortener,
  http_response_t *response)`

The response is completely initialized on every return. Router failures are
represented as an HTTP 500 response rather than leaked internal details.

## Handler mapping

- Root initializes a `200 text/html; charset=utf-8` response from the build-time
  embedded `web/index.html` bytes. The asset length is explicit and must not
  exceed `HTTP_MAX_BODY_BYTES`; failure to construct the bounded response maps
  to `500`.
- Health creates `200 text/plain` with body `OK`.
- Shorten asks HTTP to decode the JSON `url`, invokes Shortener create, then
  creates `201 application/json` with escaped `code` and `short_url` fields.
- Redirect passes the path segment to Shortener resolve and creates `302` with a
  `Location` header and an empty body.
- Invalid input maps to `400`; an unknown URL Record maps to `404`; internal
  failures map to `500`.

## Verification

Router tests cover exact-root priority over `/{code}`, root query strings, the
HTML content type and bounded embedded body, every allowed method, 405/Allow
behavior, unknown paths, malformed JSON, invalid/unknown Short Codes, and
successful create and redirect flows using a temporary SQLite database.
