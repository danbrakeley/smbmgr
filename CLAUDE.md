# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

SMB Manager (`smbmgr`): a C++20 / Qt 6 Widgets desktop app (Windows + Linux) that connects to an SMB share via libsmb2, browses files with inode and hard-link counts, finds candidate duplicate files by metadata only (no content comparison), and replaces chosen files with hard links.

## Workflow

- `docs/master-plan.md` is the only plan document: what to work on next, in order. Decisions that outlive a unit of work are ADRs in `docs/decisions/`.
- The loop is the `/next` skill (`.claude/skills/next/SKILL.md`): pick the target, branch `next/<slug>`, design, implement, gate, review, doc pass, handoff. Run it rather than a prose "do the next chunk" prompt, so no step is skipped.
- The state is the branch plus the plan at `local/plans/<slug>.md`, not the conversation. `local/` is gitignored. `/clear` once the plan is written (mandatory) and between units, then run `/next` again; it resumes from the plan's Progress section.
- `/next` never pushes, opens a PR or merges. It hands back a committed branch.

## Build & test

The `Makefile` wraps the CMake presets (`CMakePresets.json`) and picks Windows or Linux names automatically:

```
make configure   # after a fresh clone, `make clean`, or any CMakeLists.txt edit (not run implicitly)
make debug       # app only
make release
make test-unit          # "unit" label: pure logic, fast
make test-integration   # "unit" + "integration": serverless, no Docker
make test-docker        # "docker" label only; needs Docker + Compose v2
make test-all           # everything; needs Docker + Compose v2
```

- Test executables are `EXCLUDE_FROM_ALL`. Build them before running `ctest`. Each preset has a matching umbrella target: `smbmgr_tests_unit`, `smbmgr_tests_integration`, `smbmgr_tests_docker`, or `smbmgr_tests` (all).
- On Windows (Visual Studio generator) the target must be subdirectory-qualified: `--target tests/smbmgr_tests`. On Linux (Ninja) use the bare name.
- Run a single suite: `ctest --preset windows-all -R tst_pathutil` (or `linux-all`), or run the built `tst_*` exe directly, since each suite is its own executable. Output lands in `build/windows/bin/Debug/` on Windows and `build/linux-debug/bin/` on Linux.
- Minimum CMake is 4.2 (the Visual Studio 18 2026 generator), set in both `CMakeLists.txt` and `CMakePresets.json`. CI reads the project version from the `VERSION` inside `project(...)` only.
- The Windows preset hardcodes `CMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64`. CI overrides it with Qt 6.8.3.
- libsmb2 comes from FetchContent, pinned to an exact commit and linked statically.

## Architecture

All app code builds into the static library `smbmgr_core`. The `smbmgr` executable is just `src/main.cpp`, and every test links the same library. Icons are added with `qt_add_resources`, which links the resource's initializer into every executable, so no `Q_INIT_RESOURCE` is needed. On Windows every executable must also call `WSAStartup` (libsmb2 doesn't).

- **`smb/SmbSession`**: the single libsmb2 connection. It runs entirely on the GUI thread and drives libsmb2's async API with `QSocketNotifier`s plus a tick timer, so the app has no worker threads or locking. Listing and stat replies are correlated by path and can arrive in any order. Mutating ops (rename, hard link, unlink) return a request id answered by `operationSucceeded`/`operationFailed`, which always fire asynchronously. There is no per-operation cancel: callers drop stale replies themselves. Directory enumeration doesn't include link counts, so `nlink` is filled in lazily by per-file stats (`FileEntry::kNlinkUnknown`/`kNlinkUnavailable`).
- **`core/`**: `MatchSearcher` recursively lists primary/secondary roots with bounded concurrency (the roots may overlap) and pairs files by size tolerance, skipping pairs that already share an inode. `LinkRunner` executes jobs sequentially: rename victim → `*.smbmgr-tmp`, link primary → victim path, unlink tmp, and on link failure it renames back. Header-only pure logic (`PathUtil`, `MatchPairing`, `MatchConflicts`, `LogFormat`, `VersionCompare`) is where most unit tests aim. Paths are share-absolute (`/` = share root) and normalized with `pathutil::normalize`.
- **`core/Logger`**: a singleton JSONL audit log (`AppLocalDataLocation/log.jsonl`). Connects, disconnects, errors, and every server-side mutation are logged from inside `SmbSession`.
- **`ui/`**: `MainWindow` holds the URL bar (`smb://[domain;]user@host[:port]/share`), `MatchFinderPanel` (left), and a fixed pair of `FileBrowserView`s (stacked on the right). Views and the panel share one `SmbSession`. `MainWindow` rebroadcasts cross-view state such as icon mode. `MainWindow::setPasswordPrompt` is the test seam that avoids a modal dialog.
- **`models/`**: `FileListModel` keeps the full listing. `FileFilterProxyModel` sorts and filters via `SortRole`/`IsDirRole`, so formatting never affects ordering. `MatchResultsModel` backs the checkable results table.
- **Build info**: `cmake/GenerateBuildInfo.cmake` regenerates `BuildInfo.h` on every build from `git describe` (falling back to `project(VERSION)`). The About dialog uses it.

## Tests

Qt Test, one executable per suite, and every suite uses `SMBMGR_TEST_MAIN` from `tests/common/TestMain.h` instead of `QTEST_MAIN`. That macro sets `QT_QPA_PLATFORM=offscreen` by default and enables `QStandardPaths` test mode. Register new suites with `smbmgr_add_test(...)` in `tests/CMakeLists.txt`.

Each suite carries the label of its folder, plus `docker` if it needs the Samba server. `test-integration` excludes `docker`, so it runs only the serverless integration suites.

- Label `unit`: `tests/unit/`, pure logic, no server or network.
- Label `integration`: `tests/integration/`, talks to the OS, the network, or an SMB server. `tst_smbsession_offline` is the only one without `docker`; its DNS failure takes ~11 s on Windows.
- Label `widget`: `tests/widget/`, drives the UI against an SMB server (all are also `docker`).
- Label `docker`: suites against a real Samba container (`tests/docker/`). They are registered only if `docker` is on PATH. ctest brings the container up and down as a fixture (`down -v` wipes the named volume). The host port is 10445, and the share uses a named volume, not a bind mount, so hard links behave correctly. `SmbFixture` seeds and verifies share state out-of-band via `docker exec` and namespaces each run under a unique directory. Suites skip when the server is unreachable unless `SMBMGR_TEST_SMB_STRICT=1`.

## CI / versioning

- `.github/workflows/version-check.yml`: every PR into `main` must increase `VERSION` in the top-level `CMakeLists.txt`.
- `.github/workflows/release.yml`: a `v*` tag must match `VERSION`. The Windows job runs unit + integration tests (no Docker); the Linux job runs all tests and builds a `.deb` via CPack.

## Other

- `docs/decisions/` holds MADR-format ADRs.
- `.editorconfig`: UTF-8, LF, final newline, trim trailing whitespace.
- Third-party icons are credited in `THIRD_PARTY_NOTICES.md` and the per-folder READMEs under `resources/icons/`.

## Before handing work off

A unit of work is not finished when the code is finished. Take a pass over the documentation the task actually used (`CLAUDE.md`, `docs/`, any `.claude/` skill) and land the updates on the same branch as the work:

- **Record what the task taught.** A gotcha that cost time, a tool that behaved differently than documented: write it where the next session will look for it. The target's entry in `docs/master-plan.md` moves on the branch that moves the target.
- **Correct what the task disproved.** Fix the claim outright rather than leaving both versions standing.
- **Delete what has turned into history.** Git and the PRs hold the history. A passage earns its place only if it changes what a future session *does*.
- **Put it in the smallest home that works.** `CLAUDE.md` is loaded into every session, so a line costs most here. Detail only one kind of task needs belongs in that task's doc or skill.
