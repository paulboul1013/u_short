# Architecture

## Runtime flow

```text
Browser
   |
   v
server.c -- parses/serializes through --> http.c
   |
   v
router.c
   |
   v
shortener.c
   |       \
   v        v
database.c  base62.c
```

`main.c` is the composition root: it opens the database, initializes the
Shortener module, and runs the Server module.

## Compile-time dependency direction

```text
main
  +--> database
  +--> shortener
  `--> server

server
  +--> http
  `--> router

router
  +--> http
  `--> shortener

shortener
  +--> database
  `--> base62
```

Dependencies must not point upward. In particular, `database` and `base62` know
nothing about HTTP, and `http` knows nothing about routing or persistence.

## Module ownership

### Server

Owns socket creation, bind/listen/accept, bounded reads, writes, signal-aware
shutdown, and one-request-per-connection lifecycle.

### HTTP

Owns the supported HTTP/1.x parser, request framing, the `/shorten` JSON body
decoder, and response serialization. It does not choose routes or status codes.

### Router

Owns method-and-path matching, route priority, handler selection, and translating
module results into HTTP responses.

### Shortener

Owns Original URL validation, creation of URL Records, conversion between numeric
IDs and Short Codes, and lookup semantics.

### Database

Owns SQLite connection lifetime, schema initialization, parameterized inserts,
and URL Record lookup.

### Base62

Owns canonical conversion between `uint64_t` and the persistent Base62 alphabet.

## Interface rules

- Public interfaces live in `include/`; implementation-only helpers remain
  `static` in their `.c` files.
- Callers receive explicit status values; modules do not expose SQLite error
  codes or parser internals across their seams.
- Buffers have explicit capacities. No interface relies on unchecked string
  copies.
- The database handle is opaque. Returned URL text is copied into caller-owned
  bounded storage.
- Tests exercise the same public interfaces as production callers.
