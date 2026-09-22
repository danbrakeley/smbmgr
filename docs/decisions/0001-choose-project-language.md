---
date: 2026-07-12
---

# Choose C++ / Qt / libsmb2 for the SMB Manager

## Context and Problem Statement

The SMB Manager is a cross-platform GUI desktop app for manually deduplicating files on an SMB share
by replacing near-duplicate files with hard links (see the project `README.md`). Before writing
application code we needed to settle the foundational stack: the implementation language, the GUI
toolkit, and the SMB access library. The riskiest unknown was whether _any_ library, in _any_
language, could — over SMB2/3 against the real target server — read inode and hard-link-count
metadata and **create hard links**. That question had to be answered empirically before the
language/GUI choice could be made with confidence.

## Decision Drivers

- **SMB feasibility (k.o. criterion):** must connect over SMB, read inode number and hard-link
  count, and create hard links on the real server. If a stack can't do this, nothing else matters.
- **Instant startup**, no delay hidden by a splash screen or loading bar.
- **Small binary + low memory footprint**: no Electron / web rendering stack. Dynamic linking of
  shared GUI libraries is acceptable.
- **Cross-platform**, Windows/amd64 and Linux/amd64 required; macOS/arm nice-to-have.
- **Native look-and-feel** on each platform.
- **Table-heavy UI:** the app is essentially a multi-pane, sortable, filterable file browser;
  toolkit quality for multi-column data tables matters a lot.
- **Developer velocity and preference:** the author has ~20 years of C++ (including prior Qt), plus
  recent Go/TypeScript. Toolkit ergonomics are a first-class concern.

## Considered Options

- **C++ / Qt Widgets / libsmb2**
- **Go / Fyne / go-smb2**
- **Rust / egui (or Slint) / OS-mount or patched SMB**

## Decision Outcome

Chosen option: **C++ / Qt Widgets / libsmb2**, because the SMB k.o. criterion was satisfied with
libsmb2, and among the options only Qt delivers the desired native look-and-feel _and_ a great
model/view framework that maps almost 1:1 onto this app's multi-pane, sortable, filterable file
tables.
