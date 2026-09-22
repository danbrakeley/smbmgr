---
date: 2026-07-23
---

# Find Duplicate Candidates with a Metadata-Only Match Finder

## Context and Problem Statement

The app allows manually pairing two files in order to replace one with a hard link to the other.
This process is too slow.

Content hashing is off the table because the files are large, and hashing every file would slow
things down too much. Additionally, early tests showed that in some cases the file sizes were close,
but not exact, but should still be considered a match.

So how can we improve this?

## Decision Drivers

- **The user decides what matches.** Full automation was already rejected.
- **Cheap signals only.** Candidate discovery must be fast over SMB. The directory enumeration
  already returns name, size, mtime, inode, so focus on those first.
- **Fit the existing architecture:** the single async `SmbSession` on the GUI thread, replies
  correlated by path, no per-operation cancellation, and the proven rename → link → unlink
  replacement sequence.
- **Real trees are messy:** the two search roots may overlap or be identical, candidates live
  several folders deep, and byte sizes of true matches are close but not always equal.

## Considered Options

- Match Finder: a search that generates a reviewable candidate list
- Better manual tooling (tree view inside the table, synced filters, size-range filters)

## Decision Outcome

Chosen option: **Match Finder**, because it removes the slow part (finding candidates across two
trees) while keeping the human decision (which candidates are real matches).
