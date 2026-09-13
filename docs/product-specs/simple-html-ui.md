# Simple HTML web UI

## Status

**Completed and verified on 2026-09-13.**

This specification changes one completed URL Shortener v1 behavior: `GET /`,
which v1 treated as an empty Short Code and returned HTTP 400, now returns the
browser page with HTTP 200. All other v1 health, shortening, redirect,
persistence, routing, failure, and shutdown contracts remain unchanged.
The completed execution record is
`docs/exec-plans/completed/simple-html-ui.md`.

## Goal

Provide a small browser page for a local user to submit an Original URL, receive
the resulting Short URL, and copy or open it. The browser is a presentation
adapter over the existing service; the backend remains authoritative for URL
validation, URL Record creation, and Short Code behavior.

## Browser page

### GET /

`GET /` returns HTTP 200 with `Content-Type: text/html; charset=utf-8` and the
complete browser UI. The same page is returned when the request target contains
a query string, such as `GET /?source=local`, because query strings do not
participate in route matching.

Methods other than `GET` on `/` return HTTP 405 with `Allow: GET`. The exact root
route takes priority over the parameterized `/{code}` redirect route. All other
existing route priorities and responses remain unchanged.

The response body must not exceed the existing `HTTP_MAX_BODY_BYTES` limit of
4096 bytes. This increment does not change `http_response_t` or any other public
HTTP or Server interface.

## User interaction

The page contains:

- a visible label and text input for one Original URL
- a submit button
- a result area containing the returned Short URL, an open link, and a copy
  button after success
- a live status area for progress, validation feedback, copy feedback, and
  failures

Submitting sends a same-origin `POST /shorten` request with
`Content-Type: application/json` and a body created with `JSON.stringify`:

```json
{
    "url": "https://example.com/article"
}
```

On HTTP 201, the page reads `short_url` from the JSON response and displays it.
On HTTP 400, it asks the user for a valid absolute `http://` or `https://` URL.
Other non-success responses, network failures, and malformed success payloads
produce a generic retryable error without exposing backend details.

The browser may reject an empty value or a value without an `http:` or `https:`
scheme for immediate feedback. This is advisory only; backend validation remains
authoritative. Failed submissions retain the entered Original URL.

## Accessibility and responsive behavior

- The native form supports keyboard submission and every action is keyboard
  operable.
- Input and result controls have visible focus states and logical document
  order.
- Dynamic messages are announced through an `aria-live="polite"` region.
- Success and error states use text in addition to color and meet WCAG AA
  contrast.
- The page has no horizontal scrolling at a 375-pixel viewport; controls may
  stack below 480 pixels.

## Packaging and runtime

`web/index.html` is the single source asset and contains all HTML, CSS, and
JavaScript. It uses no framework, external dependency, image, web font, or
frontend build pipeline. The build embeds the asset in the application binary,
so serving `GET /` does not depend on the process working directory or a runtime
file path.

The Router owns `GET /`, method handling, and the HTML response. The Server
continues to own only the TCP lifecycle and uses the unchanged Router and HTTP
public interfaces.

## Security and data handling

- Returned values are inserted with `textContent` or equivalent DOM properties,
  never `innerHTML`.
- The page does not store submitted or shortened URLs in browser storage.
- Page JavaScript does not contact the Original URL.
- Clipboard failure leaves the Short URL visible and selectable.
- Same-origin requests avoid introducing a CORS contract.

## Build and verification commands

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Verification includes Router tests for root-path priority, query handling, body
size, content type, and 405/Allow behavior; a build-time check that the source
asset fits the 4096-byte body bound; and localhost browser-page acceptance of the
complete shorten flow. Existing v1 tests must continue to pass without being
skipped or disabled.

## Boundaries

- Always preserve backend validation and every v1 endpoint contract other than
  the explicitly replaced `GET /` empty-Short-Code 400 behavior.
- Changing a public C interface, response bound, or dependency direction
  requires separate architecture approval.
- Do not add runtime asset lookup, third-party frontend dependencies, CORS,
  browser persistence, or destination probing.

## Success criteria

- `GET /` and `GET /?query` return the embedded page with HTTP 200 and the
  documented content type.
- Unsupported methods on `/` return HTTP 405 with `Allow: GET`.
- A keyboard-only user can shorten a valid URL, copy the Short URL, and open it.
- Invalid input and server, network, clipboard, or payload failures are safe and
  retryable.
- The embedded page is at most 4096 bytes and does not require a runtime asset.
- All v1 acceptance criteria other than the explicitly replaced `GET /`
  empty-Short-Code 400 behavior remain satisfied.

## Scope exclusions

- Listing, editing, or deleting Short URLs
- User-selected Short Codes, expiration, visit statistics, or accounts
- Themes, localization, analytics, or offline support
- A frontend framework, package manager, or separate static-file server
