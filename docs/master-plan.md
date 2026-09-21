# Master plan: what to work on next

The standing, prioritized work list for this repo. `CLAUDE.md` says what the repo *is* and how to work in
it; this says what to work on. The `/next` skill (`.claude/skills/next/SKILL.md`) takes the first item of
"Next up", designs it into `local/plans/<slug>.md` (gitignored), and carries it to a branch ready for
review.

**This is the only plan document, and it should stay the only one.** It carries the scope, decisions and
traps that bind future work. Decisions that outlive a unit are ADRs in `docs/decisions/`; history lives in
git and the PRs.

## What this plan optimizes for

Growing the app from a hard-link deduplication tool into a general SMB file manager that happens to
have hard link management as a feature. Work that does not move that forward waits under "Deferred",
however cheap it looks.

### Every unit must hold to these

- have a clean separation of logic and UI
- be efficient in how it accesses, stores, and processes data
- always keep the UI responsive to user input
- allow the user to cancel any long-running operation

Where the code does not meet one yet, closing that gap is plan work, not an excuse.

## Next up

1. TBD

## Entries

### TBD

(none yet)

## Deferred

Nothing here is scheduled, and this is not a queue: an item moves to "Next up" only on an explicit
decision.

- (none yet)

## Keeping this current

- Update a target's entry on the branch that moves it. When a target lands, its entry is removed, or
  shrinks to what remains and any decision a *later* unit depends on. What the unit decided lives in the
  PR description, the comments at the site, and an ADR when it binds future work; do not list it here.
- "Next up" is an ordered list of entry titles and nothing else. Scope, decisions and traps go in the
  entry.
- A new idea gets an entry and a place in "Next up", or a line under "Deferred". It does not get a second
  plan document.
