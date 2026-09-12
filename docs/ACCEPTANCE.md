# Acceptance Criteria

## Build and tests

The project must complete without compiler warnings or errors:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

No test may be skipped or disabled.

Building the application requires SQLite3 development files. With
`BUILD_TESTING=ON`, CMake also requires `curl` and a Python 3 interpreter for the
localhost socket acceptance test; POSIX `sed`, `tr`, and `grep` are used by the
test script, along with POSIX `wc`.

## Base62

`decode(encode(x)) == x` must hold for:

- `0`
- `1`
- `61`
- `62`
- `UINT64_MAX`

The codec must reject empty input, invalid characters, non-canonical leading
zeroes, decode overflow, and insufficient output capacity.

## Health endpoint

`GET /health` returns HTTP 200, `Content-Type: text/plain`, and body `OK`.

## URL creation

`POST /shorten` with a valid JSON body returns HTTP 201 and JSON containing a
canonical Short Code and `http://localhost:8080/{code}`. The URL Record must be
persisted. Submitting the same Original URL twice produces two different Short
Codes.

The endpoint returns HTTP 400 for malformed JSON, missing/duplicate/non-string
`url`, invalid scheme or host, whitespace/control characters, or a URL longer
than 2048 bytes.

## Redirect

`GET /{code}` for an existing URL Record returns HTTP 302 with `Location` equal
to the exact decoded and stored Original URL.

- An invalid or non-canonical Short Code returns HTTP 400.
- A valid canonical Short Code without a URL Record returns HTTP 404.

After the process restarts with the same database file, existing redirects must
still work.

## Routing

- Unsupported methods on `/health`, `/shorten`, or `/{code}` return HTTP 405
  with the correct `Allow` header.
- Unknown paths return HTTP 404.
- Query strings do not participate in route matching.

## HTTP framing and safety

- HTTP/1.0 and HTTP/1.1 requests are accepted.
- Header names are case-insensitive.
- Partial socket reads are assembled into one complete request.
- A client that leaves a request incomplete cannot block the single-threaded
  server indefinitely.
- Headers over 8 KiB and bodies over 4 KiB are rejected.
- Missing `Content-Length` on `POST /shorten`, or any malformed, duplicate, or
  conflicting `Content-Length`, is rejected with HTTP 400.
- Any `Transfer-Encoding` is rejected with HTTP 501.
- CR/LF or other control characters cannot be injected into response headers.
- Every response includes an accurate `Content-Length` and
  `Connection: close`.

## Failure and shutdown

- Failure to open or initialize SQLite exits non-zero and reports the problem to
  standard error.
- Internal request failures return HTTP 500 without exposing implementation
  details.
- `SIGINT` and `SIGTERM` close server-owned resources and exit normally.
