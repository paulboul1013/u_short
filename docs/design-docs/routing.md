# Routing module design

## Responsibility

Map a parsed HTTP request to one application handler and translate handler results
into an HTTP response. Router owns no sockets, HTTP parsing, or SQL.

## Route priority

1. `/health`
2. `/shorten`
3. `/{code}` when the path contains at most one segment; `GET /` reaches this
   handler with an empty Short Code and returns `400`
4. fallback `404`

The path is matched without its optional query string. v1 does not percent-decode
paths.

## Method behavior

| Path | Allowed method | Other methods |
|---|---|---|
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

- Health creates `200 text/plain` with body `OK`.
- Shorten asks HTTP to decode the JSON `url`, invokes Shortener create, then
  creates `201 application/json` with escaped `code` and `short_url` fields.
- Redirect passes the path segment to Shortener resolve and creates `302` with a
  `Location` header and an empty body.
- Invalid input maps to `400`; an unknown URL Record maps to `404`; internal
  failures map to `500`.

## Verification

Router tests cover route priority, every allowed method, 405/Allow behavior,
unknown paths, malformed JSON, invalid/unknown Short Codes, and successful create
and redirect flows using a temporary SQLite database.
