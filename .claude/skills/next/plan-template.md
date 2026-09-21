# The plan: `local/plans/<slug>.md`

Written in `/next` step 3, in the main session. `local/` is gitignored, so the file survives a `/clear`
and is never in a commit.

Write it so a session that has `/clear`ed can implement the unit from this file alone, and can answer a
review finding from the plan and the `file:line`s it cites.

## Header

Three lines, first in the file; `/next` step 0 resumes a half-done unit from them:

```
Target: <as docs/master-plan.md spells it>
Branch: next/<slug>
Kind: code | design
```

## Sections, in order

1. **Scope.** What this unit delivers, in a sentence or two, and what it deliberately leaves out (and
   where that goes: a later master-plan entry, or nowhere).
2. **Design.** Each file touched or added, with what changes and why. New classes with their
   responsibilities, signals and owners. Callers that must change. Everything said about existing code
   carries a `file:line` from a read or grep made while writing the plan.
3. **Constraints.** One line per item of "Every unit must hold to these" in `docs/master-plan.md`: how
   this design holds to it, or "not touched" when the unit cannot affect it. Where the code in scope
   already breaks one, say whether this unit closes the gap or which master-plan entry will. Step 6
   checks the diff against this section.
4. **Decisions and risks.** Anything the code could not settle, each with the user's answer (from the
   batched questions in step 3) or, failing one, the assumption the plan proceeds on and what would change
   if it is wrong. A decision that binds later units is marked "needs ADR". These lines go into the
   handoff report.
5. **Test plan.** Suites to add or change, with their folder and labels (`unit`, `integration`, `widget`,
   `docker`), and the `make` targets step 5 runs.
6. **Implementation order.** Numbered steps, each small enough to finish and tick in one sitting, with
   the `make configure` points called out.
7. **Progress.** One `- [ ]` line per `/next` step 3 to 8, with step 4 expanded to one line per
   implementation step above. Every box empty when the plan is first written.

For a `design` unit, section 2 becomes the outline of the document to be written (the ADR or design doc,
and its path), section 3 says how the *proposed* design holds to each constraint, sections 5 and 6 shrink
to the writing steps, and Progress omits steps 4 to 6 of `/next` in favour of "write the document" and
"user has read it".
