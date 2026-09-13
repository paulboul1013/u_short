# Simple HTML web UI design

## Status

**Implemented and verified on 2026-09-13.** Product behavior is specified in
`docs/product-specs/simple-html-ui.md`, and the completed execution record is
`docs/exec-plans/completed/simple-html-ui.md`. This increment replaces the
completed v1 behavior where `GET /` was resolved as an empty Short Code and
returned HTTP 400: it now returns the browser page with HTTP 200. All other v1
contracts remain unchanged.

## Goal

Provide one small, responsive page where a user can paste an Original URL,
create a Short URL, and copy or open the result. The page is a thin browser
adapter over the existing `POST /shorten` interface; URL validation and Short
Code creation remain in the backend modules.

## Page layout

```text
+--------------------------------------------------+
|                    URL Shortener                 |
|        Turn a long link into a short one.        |
|                                                  |
|  Original URL                                   |
|  [ https://example.com/article               ]  |
|  [              Shorten URL                   ]  |
|                                                  |
|  Result                                          |
|  [ http://localhost:8080/21 ] [ Copy ]           |
|  Open short URL                                  |
|                                                  |
|  Status or error message                         |
+--------------------------------------------------+
```

- A centered card is the only primary visual region.
- On wide screens the result and Copy button share one row.
- Below `480px`, controls use the full width and stack vertically.
- The form is usable without a mouse and preserves the entered URL after an
  error.

## Visual language

- Use system fonts and a neutral light background.
- Limit the content width to approximately `640px`.
- Use one high-contrast accent color for the submit button, links, and focus
  indicators.
- Use a green-tinted result panel for success and a red-tinted message for
  errors; color is supplemented by text.
- Keep all CSS and JavaScript dependency-free. No images, web fonts, or frontend
  framework are required.

## Interaction states

| State | Form behavior | Message/result |
|---|---|---|
| Initial | URL input is focused; submit is enabled | No result is shown |
| Submitting | Input is retained; submit is disabled | Button text becomes `Shortening...` |
| Success | Submit is enabled | Show the returned Short URL, `Open short URL`, and `Copy` |
| Invalid input | Submit is enabled; focus returns to input | Show a concise validation message |
| Server/network error | Submit is enabled | Explain that the URL could not be shortened and allow retry |
| Copied | No form change | Copy button temporarily reads `Copied` |

Submitting the form sends:

```http
POST /shorten
Content-Type: application/json

{"url":"https://example.com/article"}
```

On HTTP `201`, the page reads `short_url` from the JSON response. For HTTP `400`,
it displays `Enter a valid absolute http:// or https:// URL.` Other non-success
responses use a generic message and do not expose backend details.

The browser may perform lightweight checks for an empty value and an `http:` or
`https:` scheme to give immediate feedback. These checks are advisory; backend
validation remains authoritative.

## Accessibility

- Associate the URL input with a visible `<label>`.
- Use a native `<form>` so Enter submits it.
- Keep visible keyboard focus styles and logical document order.
- Announce result and error changes through an `aria-live="polite"` region.
- Use semantic `<main>`, `<form>`, `<button>`, and `<a>` elements.
- Meet WCAG AA text and control contrast.

## Module and seam placement

The browser UI is one module with a small interface: the user supplies an
Original URL and receives either a Short URL or a human-readable error. Its
implementation owns presentation state, `fetch`, and clipboard interaction.

The existing HTTP request/response contract is the seam between the browser
adapter and the backend.
The UI must not reproduce Base62, persistence, or authoritative URL-validation
logic. At runtime the browser calls the Server over HTTP; Server delegates route
selection to Router, and Router invokes Shortener only for shortening and redirect
behavior.

To keep the first implementation small, use one `web/index.html` file with
embedded CSS and JavaScript. The build embeds this source asset in the application
binary. Router serves the embedded bytes from `GET /` as
`text/html; charset=utf-8`; Server does not load or select the asset, and the
running process does not depend on a runtime file path. Serving the page at the
same origin avoids adding a CORS interface.

The exact `/` route has priority over `/{code}`. Query strings do not participate
in route matching, so `GET /?x` also returns the page. Other methods on `/` return
HTTP 405 with `Allow: GET`.

The complete HTML response must not exceed the existing
`HTTP_MAX_BODY_BYTES=4096`. The build must enforce this bound; this increment does
not change `http_response_t`, `server_run`, or any other public interface.

## Security and failure behavior

- Insert returned values with `textContent` or DOM properties, never
  `innerHTML`.
- Serialize the request body with `JSON.stringify`.
- Do not store submitted or shortened URLs in browser storage.
- Do not contact the Original URL from page JavaScript.
- A clipboard failure leaves the Short URL selectable and shows a brief message.
- A malformed success payload is handled as a generic server error.

## Verification

- The page renders at desktop and `375px` viewport widths without horizontal
  scrolling.
- Keyboard-only users can submit, copy, and open the result.
- A valid URL reaches `POST /shorten` and displays the returned Short URL.
- Invalid input, HTTP `400`, HTTP `500`, and network failure show the documented
  states and permit retry.
- Result values containing markup-like text are displayed as text, not executed.
- Existing health, shortening, redirect, persistence, and shutdown acceptance
  tests continue to pass.
- `GET /?x` returns the same page, unsupported methods on `/` return 405 with
  `Allow: GET`, and the executable serves the page without `web/index.html`
  present at runtime.
- The build rejects an HTML source asset larger than 4096 bytes.

## Out of scope

- Listing, editing, or deleting Short URLs
- User-selected Short Codes, expiration, or visit statistics
- Accounts, authentication, or administration
- Dark mode, themes, localization, analytics, or offline support
- A frontend build pipeline or third-party JavaScript dependencies
