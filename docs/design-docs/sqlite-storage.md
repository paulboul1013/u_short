# SQLite storage module design

## Responsibility

Hide SQLite connection management and SQL behind a small URL Record interface.
All SQL containing data uses bound parameters.

## Schema

```sql
CREATE TABLE IF NOT EXISTS urls (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    original_url TEXT NOT NULL,
    created_at INTEGER NOT NULL
);
```

## Public interface

`include/database.h` declares an opaque `database_t` and:

- `database_open(const char *path, database_t **database)`
- `database_close(database_t *database)`
- `database_insert_url(database_t *database, const char *original_url,
  int64_t created_at, int64_t *id)`
- `database_find_url(database_t *database, int64_t id, char *original_url,
  size_t capacity)`

The status enum distinguishes success, not-found, invalid arguments,
insufficient caller buffer, and internal storage failure. SQLite-specific values
never escape the module.

## Lifecycle and invariants

- Opening initializes the schema in the same connection.
- IDs are positive signed 64-bit integers allocated by SQLite.
- Repeated Original URLs are inserted as distinct rows.
- `created_at` is Unix time in UTC seconds.
- Lookup copies into caller-owned storage and always NUL-terminates on success.
- Close accepts NULL and releases every SQLite resource owned by the handle.

## Verification

Integration tests use a temporary database file and verify schema creation,
insert/find, duplicate URL records, persistence after reopen, not-found, and
insufficient output capacity.
