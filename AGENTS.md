# AGENTS.md

## Required reading

Before implementation, read in order:

1. the relevant product specification under `docs/product-specs/`
2. `ARCHITECTURE.md`
3. `docs/ACCEPTANCE.md`
4. the relevant module documents under `docs/design-docs/`
5. the relevant plan under `docs/exec-plans/active/`

When no active plan exists, inspect the related completed plan for historical
context, then create and approve a new active plan before changing behavior.

## Source of truth

- Product behavior: `docs/product-specs/`
- Architecture and dependency direction: `ARCHITECTURE.md`
- Detailed module design: `docs/design-docs/`
- Acceptance criteria: `docs/ACCEPTANCE.md`
- Current execution state: `docs/exec-plans/active/`
- Completed execution history: `docs/exec-plans/completed/`
- Known technical debt: `docs/exec-plans/tech-debt-tracker.md`
- Domain language: `CONTEXT.md`

## Repository discovery

Discover the repository progressively with `rg --files`. Start from the source-of-
truth pointers above, then inspect only the directories and files relevant to the
current task. Treat the filesystem as the authoritative directory structure; do
not cache a file tree in this document.

## Project status

- URL Shortener v1 is complete and verified.
- Its historical execution plan is
  `docs/exec-plans/completed/url-shortener-v1.md`.
- `docs/exec-plans/active/` currently contains no active implementation plan.
- A web UI has not yet been specified or added to the source tree.

## Root agent responsibilities

The root agent owns:

- architecture
- public interfaces
- dependency direction
- task decomposition and file ownership
- integration
- acceptance verification
- execution-plan state transitions

## Subagent rules

Subagents must:

- modify only assigned files
- preserve the documented architecture and dependency direction
- request root approval before changing public interfaces
- compile and test their work
- report changed files and exact verification results

The root agent reviews every subagent change before integration.
