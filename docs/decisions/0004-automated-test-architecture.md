---
date: 2026-07-28
---

# Automated Tests: Qt Test In-Process, Samba-in-Docker Fixture

## Context and Problem Statement

Manual testing is slow and cumbersome, and can't be automatically run by
an agent. We need automated testing.

## Decision Drivers

- **Should test against a real SMB server** And this should work on any
  supported dev environment (Windows, Linux; nice-to-have macOS). Tests
  should be able to verify inode/nlink the apps sees is accurate by querying
  the test server directly.
- **Headless and deterministic**: suites run under ctest with no visible
  windows, no modal dialogs blocking, no reliance on timing luck.
- **Minimal app-code changes**: test seams should not reshape the app.

## Considered Options

- Qt Test in-process + Samba in Docker
- External UI automation (accessibility APIs / Squish) against the built exe
- Mocked SMB layer (abstract SmbSession behind an interface)

## Decision Outcome

Chosen option: **Qt Test in-process + Samba in Docker**.

External automation is brittle and slow; a mocked session would skip the
layer most worth testing and force an interface onto a concrete class for
no product benefit.

### More Info

The suite already paid for itself twice during construction, catching a
latent use-after-free and two `pathutil::normalize` edge cases.
