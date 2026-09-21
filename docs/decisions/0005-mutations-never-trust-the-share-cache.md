---
date: 2026-09-21
---

# Mutations Never Trust the Share Cache

## Context and Problem Statement

A share cache is being put between the GUI and `SmbSession` (see
`docs/master-plan.md`). Cached listings and link counts are shown until the
user refreshes, so they can be arbitrarily old. The match finder builds
rename → link → unlink jobs from that same data, and today `LinkRunner` runs
them with no check of the files' current state. How do we keep stale metadata
from replacing a file that has changed since it was listed?

## Decision Drivers

- **A wrong link destroys data.** A stale browse view is an inconvenience; a
  stale link job replaces the wrong bytes.
- **Hard links reach across directories.** Linking changes the link count of
  every other name of both inodes, wherever those names live.
- **Caching must stay cheap to reason about.** Reads should not need
  per-consumer freshness rules.

## Considered Options

- Verify against the server before mutating, and write through the cache
- Require a fresh search (or a cache TTL) before linking is enabled
- Trust the cache

## Decision Outcome

Chosen option: **Verify against the server before mutating, and write through
the cache**.

- Immediately before a job's first mutation, both files are statted on the
  server, bypassing the cache. If size, mtime or inode differ from what the
  match was built from, the job fails without touching anything.
- Every mutation goes through the cache facade, which invalidates both parent
  directories and the link count of every cached path sharing either inode.

A TTL only narrows the window, and forcing a fresh search throws away the
cache's main benefit.

## More Information

- Revisit if: mutations gain a server-side precondition (e.g. a handle-based
  compare-and-rename) that makes the pre-stat redundant
