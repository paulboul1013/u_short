# Implementation Plan: URL Shortener v1

## Overview

Build the approved C17 URL shortener in contract-first increments. The root agent
owns public interfaces, integration, and acceptance; subagents own only explicitly
assigned files and verify their work.

## Architecture decisions

- Fixed, bounded value types avoid hidden allocation in HTTP paths.
- SQLite remains behind an opaque Database module and parameterized statements.
- Base62 is a pure module whose alphabet is persistent data format.
- The single-threaded Server module handles one request per connection.
- Module statuses are translated to HTTP only in Router.

## Task list

### Phase 0: Contracts and build scaffold

- [x] Task 0.1: Finalize architecture and four design documents.
  - Acceptance: responsibilities, interfaces, invariants, and errors are explicit.
  - Verification: documentation agrees with product specification and acceptance.
  - Dependencies: none.
  - Files: `ARCHITECTURE.md`, `docs/design-docs/*.md`.
- [x] Task 0.2: Add public headers, CMake targets, and test harness wiring.
  - Acceptance: every module has one public header; strict C17 warnings enabled.
  - Verification: CMake configures once SQLite development files are available.
  - Dependencies: Task 0.1.
  - Files: `include/*.h`, `CMakeLists.txt`.

### Phase 1: Parallel foundations

- [x] Task 1.1: Implement Base62 test-first.
  - Acceptance: canonical encode/decode and all documented error cases work.
  - Verification: `ctest --test-dir build -R base62 --output-on-failure`.
  - Dependencies: Task 0.2.
  - Owner files: `src/base62.c`, `tests/test_base62.c`.
- [x] Task 1.2: Implement SQLite storage test-first.
  - Acceptance: insert, lookup, duplicates, reopen persistence, and errors work.
  - Verification: `ctest --test-dir build -R database --output-on-failure`.
  - Dependencies: Task 0.2 and SQLite development package.
  - Owner files: `src/database.c`, `tests/test_database.c`.
- [x] Task 1.3: Implement HTTP parsing/serialization test-first.
  - Acceptance: bounded framing, JSON decoding, serialization, and injection
    defenses match the design.
  - Verification: `ctest --test-dir build -R http --output-on-failure`.
  - Dependencies: Task 0.2.
  - Owner files: `src/http.c`, `tests/test_http.c`.

### Checkpoint: Foundations

- [x] Configure and build with strict warnings.
- [x] Base62, database, and HTTP test targets pass.
- [x] Root reviews subagent changes against frozen interfaces.

### Phase 2: Application slices

- [x] Task 2.1: Implement Shortener create and resolve test-first.
  - Acceptance: URL validation, persistence, Base62, duplicate creation, and
    result mapping work through the public interface.
  - Verification: `ctest --test-dir build -R shortener --output-on-failure`.
  - Dependencies: Tasks 1.1 and 1.2.
  - Files: `src/shortener.c`, `tests/test_shortener.c`.
- [x] Task 2.2: Implement Router endpoint behavior test-first.
  - Acceptance: health, shorten, redirect, 400/404/405/500 mappings work.
  - Verification: `ctest --test-dir build -R router --output-on-failure`.
  - Dependencies: Tasks 1.3 and 2.1.
  - Files: `src/router.c`, `tests/test_router.c`.
- [x] Task 2.3: Implement Server and composition root.
  - Acceptance: bounded one-request connections and graceful shutdown work.
  - Verification: build plus localhost smoke test.
  - Dependencies: Task 2.2.
  - Files: `src/server.c`, `src/main.c`, `tests/test_acceptance.sh`.

### Checkpoint: End-to-end

- [x] `GET /health` returns 200 and `OK`.
- [x] `POST /shorten` persists and returns a valid Short URL.
- [x] `GET /{code}` returns 302 with the exact Original URL.
- [x] Restarting preserves redirects.

### Phase 3: Integration review

- [x] Task 3.1: Run standards/spec review and address findings.
- [x] Task 3.2: Run full build and acceptance suite.
- [x] Task 3.3: Move this plan to `completed/` only after every criterion passes.

## Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| SQLite development package is absent locally | Build blocked | Use the isolated `/tmp` package extraction for verification; normal environments install `libsqlite3-dev` |
| Handwritten HTTP/JSON parsing accepts ambiguous input | Security/correctness | Strict bounds, reject ambiguity, table-driven negative tests |
| Parallel changes conflict in shared build files | Rework | Root alone owns headers and CMake; agents own disjoint `.c` and test files |
| SQLite IDs exceed signed range | Incorrect conversion | Reject non-positive IDs and keep `UINT64_MAX` testing inside Base62 |

## Open questions

None. Product behavior was confirmed before implementation.
