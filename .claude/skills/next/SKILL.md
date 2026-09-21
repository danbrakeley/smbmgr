---
name: next
description: Do the next unit of work from docs/master-plan.md end to end — pick the target, branch, design it in this session (plan to local/plans, user answering design questions in one batch), /clear, then implement, gate, review, do the doc pass and hand the branch back. Resumable: on a next/<slug> branch with its plan in local/plans, it continues from the plan's Progress section, so /clear mid-unit and run it again. Use whenever the user runs /next or asks for "the next chunk of work" from the master plan.
---

# /next — one unit of work, start to handoff

This is the whole loop as numbered steps, so that no step is skipped and the conversation is never the
state. **The state lives in two places only**: the current git branch, and the plan at
`local/plans/<slug>.md` with its **Progress** section. `local/` is gitignored, so the plan survives a
`/clear` and a branch switch, and is never in a commit.

Tick each step in the plan's Progress section as it completes.

## 0. Orient and resume

1. `git status --short`, `git branch --show-current`, and list `local/plans/` whole. Never Write over a
   plan you have not read.
2. **On `main` with a clean tree**: this is a fresh unit. `git pull --ff-only`, then go to step 1.
3. **On a `next/<slug>` branch**: look for the plan under `local/plans/` whose header names that branch.
   - Found: read its Progress section and **resume at the first unticked step**. Do not redo the design
     or re-open the target choice. Skim `git log main..HEAD --oneline` and `git status` to confirm the
     work matches the ticked steps; a mismatch is reported to the user, not silently repaired.
   - Not found: work is mid-flight without a plan. Stop and report the branch and its changes to the
     user; do not start a second unit.
4. **Anything else** (a dirty `main`, some other branch): stop and report. Do not stash, reset or switch
   away from work you did not create.

## 1. Pick the target

Read `docs/master-plan.md`: "Next up" first, then the target's own entry. Take the first item unless its
entry says it is blocked. Decide the **kind** now: `code` (the default) or `design` (the deliverable is an
ADR or a design doc, not code; steps 4 to 6 are skipped). If the order is genuinely ambiguous (two items,
neither first), ask the user. This is the one question allowed before any work exists.

## 2. Branch

`git switch -c next/<slug>`, where `<slug>` is a short kebab-case name for the target. The plan file
takes the same slug.

## 3. Design (this session)

Write the plan yourself, here, with the user in the loop. Steps 4 to 8 run after a `/clear` with only the
plan for context, so every judgment call is made in this step, in writing, with its reason. A plan line
that says "decide at implementation time" has failed.

**Read what the target needs, and read it now, not from memory:**

1. The `docs/master-plan.md` entry: scope, decisions already made, traps already recorded. Do not
   re-litigate a decision it records; do flag one the code now contradicts. Then the list under "Every
   unit must hold to these" in the same file: the design is checked against each line, and the plan says
   how it holds to each one the unit touches. Where the code in scope already breaks one, the plan either
   closes the gap or says why that is a separate master-plan entry; it does not build further on the
   breakage.
2. The ADRs in `docs/decisions/` that touch the area. An ADR is binding until a new one supersedes it.
3. The code in scope and its callers, and the tests that cover it (`tests/CMakeLists.txt` says which
   suite carries which labels). A broad sweep may go to `Explore`; it returns `file:line` conclusions,
   never file dumps.

Everything the plan claims about the code — a class that exists, a signal that fires, a caller that must
change — is cited `file:line` from a read or grep made in this step. The master-plan entry may be stale;
the code is not.

**Questions go to the user in one batch per round.** Collect them while reading, ask them all in one
message, and wait. Carry each answer into the plan's Decisions section.

Then write `local/plans/<slug>.md` to the layout in `plan-template.md` (beside this file), with an empty
Progress checklist, and tick step 3.

**`/clear` now, and run `/next` again.** This is mandatory, not a suggestion: the design context is the
largest of the unit, and every later turn would carry it. Say so to the user in one line when you tick the
step.

## 4. Implement

Work through the plan's **Implementation order**, ticking each step in Progress as it lands. Commit on
the branch at sensible points; small commits are fine, the PR is squash-merged.

A `CMakeLists.txt` edit needs `make configure` before the next build. New test suites are registered with
`smbmgr_add_test(...)` and use `SMBMGR_TEST_MAIN` (`CLAUDE.md`, "Tests").

A question the plan does not answer is a design question: see "Amending the plan" below. Do not guess and
move on.

## 5. Gate

Run the plan's **Test plan**. At minimum `make test-integration`; add `make test-docker` when the change
touches `src/smb/`, `src/ui/`, `LinkRunner`, or anything else only the Samba-backed suites exercise (if
Docker is unavailable, say so in the handoff rather than skipping silently). Keep build logs out of the
main context: run the target with `run_in_background` and read only the tail and the failing lines, or
hand the exact commands to a subagent that returns PASS/FAIL and the failing lines only. Never poll.

On FAIL, fix and re-run the affected target. A failure the quoted lines do not explain gets a fresh look
at the log before a fix, not a guess. Repeat until every command is PASS. Tick step 5.

## 6. Review

Run `/code-review` on the branch diff, and the `qt-cpp-review` skill when Qt C++ changed. One after the
other, not concurrently. Then:

- Real findings are fixed, then back to step 5 for the affected targets.
- Check the diff yourself against the plan's Constraints section: a change that breaks a line of "Every
  unit must hold to these" is a finding, whether or not a reviewer raised it.
- A finding that the *design* was wrong is amended in the plan first (see below), then implemented.
- Re-review only when code changed. Tick step 6. Another good `/clear` moment.

### Amending the plan

After a `/clear`, a design question is answered from the plan's own section and the code it cites, not by
re-running the step-3 reading list: read the section, open the `file:line`s it names, decide, and write
the amendment into the plan with its reason. A question the user must answer goes to them in the same
batched form as step 3.

## 7. The doc pass

`CLAUDE.md`, "Before handing work off", scoped to what this unit touched:

- `docs/master-plan.md`: remove or shrink the target's entry and advance "Next up", on this branch.
- A new ADR in `docs/decisions/` for any decision a later unit must not undo.
- Anything the unit taught or disproved, in the smallest home that works (`CLAUDE.md` last).
- Bump `VERSION` in the top-level `CMakeLists.txt` (the version-check workflow requires it on every PR
  into `main`): patch for fixes, docs and internal work, minor for a user-visible feature.

Tick step 7.

## 8. Hand off

1. Commit everything on the branch. `git status` is clean.
2. Tick step 8. Report to the user: the branch, the plan path, the gates run and their results, the review
   verdict, the decisions and assumptions they should look at, and a draft PR title and description.
3. **Never push, open a PR or merge.** The user reviews, pushes and merges, switches back to `main`,
   `/clear`s, and runs `/next` again. The plan file stays in `local/plans/` until the user deletes it.
