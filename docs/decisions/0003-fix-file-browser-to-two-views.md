---
date: 2026-07-28
---

# Fix the File Browser to Two Views, Remove Add/Remove-View

## Context and Problem Statement

The match finder (ADR-2) locked in a workflow that assumed exactly two file browser views. Currently
the GUI starts with 1, and allows adding any number of views. Do we still need more than 2 views? Or
less than 2 views?

## Decision Drivers

- **Match the actual workflow.** The Match Finder is now how matches get found; it only ever
  addresses two views (primary/secondary). Arbitrary extra views serve the old manual-comparison
  workflow the Match Finder was built to replace.
- **Less UI, less code to keep correct.** Add/remove-view UI means splitter child-count bookkeeping,
  per-view close-button visibility toggling, and `MainWindow::m_views` needing to stay in sync with
  a mutable splitter.

## Considered Options

- Fix the file browser to exactly two views, remove add/remove-view UI
- Keep arbitrary views
- Keep add/remove UI but default to two

## Decision Outcome

Chosen option: **Fix the file browser to exactly two views, remove add/remove-view UI**.

## More Information

- Revisit if: a future workflow needs more than two trees open at once
