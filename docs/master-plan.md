# Master plan: what to work on next

The standing, prioritized work list for this repo. `CLAUDE.md` says what the repo _is_ and how to
work in it; this says what to work on. The `/next` skill (`.claude/skills/next/SKILL.md`) takes the
first item of "Next up", designs it into `local/plans/<slug>.md` (gitignored), and carries it to a
branch ready for review.

**This is the only plan document, and it should stay the only one.** It carries the scope, decisions
and traps that bind future work. Decisions that outlive a unit are ADRs in `docs/decisions/`;
history lives in git and the PRs.

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

1. Share cache facade, and the file views read through it
2. Match search through the cache, and verified write-through linking
3. Refresh button and data-age display

## Entries

### Share cache: context shared by the three entries below

Not a target. It shrinks as the entries land, and goes when the last one does.

**The goal.** One abstraction between the GUI and `SmbSession` that every consumer asks for share
data. It fetches what it does not have, caches everything with the time it was fetched, and keeps
serving the cached data until the user refreshes. It holds directory listings and per-file hard-link
counts, owns the fetch scheduling, and batches its change notifications so the GUI is signalled at
most about 60 times a second. Browsing is the first step toward the general file manager: every
later feature reads through it.

**Why now.** What the code does today (checked 2026-09-21):

- `SmbSession` broadcasts replies by path (`src/smb/SmbSession.h:115-118`) and every consumer
  filters them itself (`src/ui/FileBrowserView.cpp:284-288`, `src/core/MatchSearcher.cpp:109-113`).
  Two views on the same folder send duplicate listings and duplicate stats.
- Each view runs its own stat scheduler, 32 in flight per view (`src/ui/FileBrowserView.cpp:24`,
  `:375-429`). That is scheduling logic inside a widget.
- `MatchSearcher` re-lists every folder on every run (`src/core/MatchSearcher.cpp:85-98`).

**The layers, decided.** One facade for the GUI, but not one large class:

1. `SmbSession` stays the transport. It gains only what a unit below names.
2. A pure store: directory snapshots, per-entry link counts, timestamps, an inode → paths index, and
   the invalidation rules. No event loop, no network, an injected clock. **Landed** as
   `sharecache::Store` (`src/core/ShareCacheStore.h`, suite `tst_sharecachestore`); the header
   documents the contract.
3. A `QObject` facade over both: fetching, request coalescing, one global stat scheduler,
   notification batching. `FileBrowserView`, `MatchSearcher` and `LinkRunner` end up taking this,
   not `SmbSession*`. Name: `ShareCache` in `src/core/ShareCache.{h,cpp}`.

**Decided across all three:**

- Reads are synchronous and changes arrive as signals (the `QFileSystemModel` shape), not
  request-and-reply. `lookup(dir)` returns the snapshot, its state (Missing, Loading, Fresh, Stale,
  Failed) and when it was fetched. `ensure(dir)` fetches only when missing; `refresh(dir)` always
  fetches.
- Failures are cached: an unlistable folder stays Failed until refreshed. `kNlinkUnavailable` is
  already a cached failure of the same kind (`src/smb/SmbTypes.h:12-13`).
- Age and TTL logic run on a monotonic clock. Wall-clock time is for display only, since
  sleep/resume and NTP steps move it.
- The cache belongs to one connection and is cleared on disconnect. Inodes mean nothing across
  shares.
- No eviction yet, but the store counts its entries so a budget can be added without a redesign.
- Mutations never trust the cache: ADR-5
  (`docs/decisions/0005-mutations-never-trust-the-share-cache.md`).
- OS file icons stay out of the cache (see "Deferred").

### Share cache facade, and the file views read through it

**Scope.** The `QObject` facade, and `FileBrowserView`/`FileListModel` moved onto it.
`MatchSearcher` and `LinkRunner` still talk to `SmbSession` directly after this unit.

**What the store hands the facade** (decided in the store unit; `src/core/ShareCacheStore.h` is the
reference):

- Entries are stored sorted by name, case-sensitive. `FileListModel`'s source order becomes name
  order; the sort proxy orders the view anyway, so nothing user-visible changes.
- `applyListing` returns a `Diff`: `removed` (old-list rows, descending), then `inserted` and
  `changed` (new-list rows, ascending). Apply them in that order and the model matches the store.
- `markLoading` keeps the old entries, so a refresh keeps serving. A directory invalidated while
  Loading lands **Stale**, not Fresh: "re-fetch subscribed Stale directories" has to be the facade's
  normal loop, not a special case.
- Each `note*()` returns an `Invalidation` (stale directories, stale link-count rows) to signal
  from. `markAllStale()` is the `linkRunFinished` hook named under "Traps".
- `nextStats(subscriptions, exclude, max)`: the facade passes its in-flight set as `exclude`; each
  `Subscription` carries the visible names as its priority hint. A cached stat failure is never
  retried by the store.
- Pointers from `find`/`findEntry` die on the next non-const call. Hold an `EntryRef` (dir, row)
  across calls and re-resolve after a listing lands.
- `applyListing` on 100k entries is one synchronous pass of about 300 ms in a Debug build
  (`footprint100k` prints the numbers). Decide here whether that needs chunking; the store does not
  chunk.

**The facade:**

- Takes the `SmbSession*`; `MainWindow` creates it beside the session (`src/ui/MainWindow.cpp:30`)
  and hands it to the views where it hands them the session today.
- `lookup`/`ensure`/`refresh` as above. Concurrent requests for one path coalesce into one
  `listDirectory` call.
- **Subscriptions.** A view subscribes to the directory it shows and unsubscribes when it leaves.
  Only subscribed directories get stats, which is what stops unit 3's crawl from queueing a stat per
  file on the share.
- **One global stat scheduler** replacing the per-view one: a single in-flight budget (start from
  the current 32, `src/ui/FileBrowserView.cpp:24`), ordering from the store. Only a view knows which
  rows are visible, so it sends a `prioritize(dir, names)` hint on scroll and after a listing lands,
  replacing `nextStatRow`'s viewport walk (`src/ui/FileBrowserView.cpp:407-429`). Two views on one
  folder cost one stat per file.
- **Batched notifications.** Per-entry changes accumulate as dirty rows per directory and flush from
  a single-shot timer of about 16 ms, armed by the first dirty row. No timer runs while idle. The
  flush emits contiguous ranges. One-off events are **not** gated: a listing arriving, a failure, a
  state change. Qt already compresses repaints; the cost being removed is one `dataChanged` per stat
  reply pushed through the sort proxy, worst when sorted by Links.
- Clears the store when the session leaves Connected, and drops replies that arrive after it.

**The views:**

- `FileBrowserView` loses `m_statInFlight`, `m_statRequested`, `m_statOrder`, `m_statCursor`,
  `resetStatQueue`, `pumpStats` and `nextStatRow`, and its four `SmbSession` connections
  (`src/ui/FileBrowserView.cpp:210-217`). Navigation becomes subscribe + `ensure`. A cached
  directory shows at once, with no round trip.
- `FileListModel` applies the store's diffs instead of `setEntries`' full reset
  (`src/models/FileListModel.h:43`), so a re-list keeps selection and scroll position. The
  workaround for the reset eating the selection (`src/ui/FileBrowserView.cpp:300-303`) goes with it.
- A failed listing keeps today's behaviour: the last good listing stays up and the path box reverts
  (`src/ui/FileBrowserView.cpp:306-316`).

**Traps.**

- **Links made before unit 3 are invisible to the cache.** `LinkRunner` still mutates through
  `SmbSession`, and the only hook is `linkRunFinished` → `view->refresh()`
  (`src/ui/MainWindow.cpp:237-242`). Once directories persist beyond the one on screen, that misses
  every other cached name of the two inodes. Until unit 3 routes mutations through the facade, this
  unit calls `Store::markAllStale()` on `linkRunFinished`. Crude and correct; unit 3 replaces it.
- `refresh()` has to mean `ShareCache::refresh(dir)`, not `ensure`, or the post-link refresh becomes
  a no-op (`src/ui/FileBrowserView.cpp:228-233`).
- `SMBMGR_STAT_DELAY_MS` (`src/smb/SmbSession.h:158`) is the way to see the scheduler and the
  batching work by eye.
- `navigateToAndReveal` depends on a listing landing (`src/ui/FileBrowserView.cpp:257-266`,
  `:296-299`). With a cache hit there is no asynchronous reply to wait for; reveal must work on both
  paths.
- The constructor change ripples into `tests/widget/tst_filebrowserview.cpp` and
  `tests/widget/tst_mainwindow.cpp`.

**Tests.** A facade suite under `tests/integration/` with the `docker` label (coalescing,
subscription gating of stats, clear on disconnect, batching observed with `QSignalSpy`). Update the
widget suites; add cases for a cache-hit navigation, selection surviving a re-list, and two views on
one folder. Gate with `make test-docker`.

### Match search through the cache, and verified write-through linking

**Scope.** `MatchSearcher` reads through the facade, and `LinkRunner` mutates through it under
ADR-5. The two halves ship together on purpose: the moment a search can be served from cached
listings, a link job can be built from stale data, so the verification cannot land later than the
caching.

**Search:**

- `MatchSearcher` calls `ensure(dir)` per folder instead of `listDirectory`
  (`src/core/MatchSearcher.cpp:94`) and keeps its own bound on outstanding requests
  (`kMaxListsInFlight`, `:11`). It does not subscribe, so a crawl triggers no stats.
- Cached folders answer synchronously, so a second search with a different tolerance does no I/O.
  The traversal then runs entirely on the GUI thread in one go: **chunk it** (yield to the event
  loop every N folders) so the UI stays responsive and Cancel still works. `m_inPump` exists for
  synchronous re-entry (`src/core/MatchSearcher.cpp:85-98`); read it before changing the loop.
- Failed folders are served from the cache as Failed and still count as folder errors.
- The panel should say how old the data behind a result set is (the oldest fetch time among the
  folders used). A search does not refresh anything by itself.

**Linking, per ADR-5:**

- A match must carry what verification compares. Today `Match` has paths and sizes only, and
  `FileRecord` has no mtime (`src/core/MatchPairing.h:23-37`); `LinkRunner::Job` has two paths
  (`src/core/LinkRunner.h:21-25`), built at `src/ui/MatchFinderPanel.cpp:433-438`. Carry size, mtime
  and inode for both sides from the listing through to the job.
- `fileStatted` reports only the link count and inode (`src/smb/SmbSession.h:117`). Verification
  needs size and mtime as well, from a stat that **bypasses the cache**. Extend the stat reply or
  add a dedicated call; decide in the design.
- New first step per job: stat both files, compare, and fail the job untouched on any difference,
  with a message that says which field changed. Then the existing rename → link → unlink
  (`src/core/LinkRunner.cpp:53`, `:78`, `:83`, undo at `:111`).
- Every mutation goes through the facade, which applies the store's invalidation rules as each
  operation succeeds. This replaces unit 2's mark-everything-Stale hook, and `linkRunFinished` no
  longer needs to refresh the views (`src/ui/MainWindow.cpp:237-242`): subscribed directories that
  went Stale re-fetch by themselves.
- The audit log is written inside `SmbSession` and must stay there. Log a verification failure too.

**Traps.**

- **`LinkRunner` cannot be cancelled** (`src/core/LinkRunner.h:29-32`), which breaks "allow the user
  to cancel any long-running operation". This unit touches it, so the plan either adds cancel
  **between** jobs (never inside a rename → link → unlink sequence) or gives it its own entry, with
  the reason.
- The verify stat and the rename are not atomic. ADR-5 accepts that window; do not try to close it
  with retries.
- Matching is metadata-only by ADR-2. Verification compares metadata to metadata; it is not a
  content check and must not become one.
- `.smbmgr-tmp` names are excluded from candidates (`src/core/MatchPairing.h:46-49`). A crash
  mid-job leaves one on the share, and a later cached listing will contain it. Keep excluding it.
- `tests/integration/tst_matchsearcher.cpp`, `tests/integration/tst_linkrunner.cpp` and
  `tests/widget/tst_matchfinderpanel.cpp` all construct against `SmbSession`.

**Tests.** `tst_linkrunner`: a victim changed out-of-band between search and link (`SmbFixture`
writes via `docker exec`) fails verification and leaves both files untouched; link counts of a third
name of the same inode, in another directory, update after a link. `tst_matchsearcher`: a second
search does no listings; cancel works during a fully cached traversal. Unit-test any new pure
comparison logic. Gate with `make test-docker`.

### Refresh button and data-age display

**Scope.** The user-facing controls for what units 1 to 3 built.

- A Refresh action on the main toolbar (`src/ui/MainWindow.cpp:36-64`), object name
  `mw.refreshAction`, enabled only while Connected, with F5 as its shortcut.
- **What it refreshes** (recommended; confirm with the user in the design questions): both views'
  current directories are re-fetched now, listings and link counts, and everything else in the cache
  goes Stale, so it re-fetches when next visited or searched. Not a whole-cache re-crawl.
- While a refresh is in flight the old rows stay up, link counts included (the store's carry-over),
  and the diff lands in place.
- Each view's status bar (`src/ui/FileBrowserView.cpp:176-196`) gains the age of its listing
  ("Listed 3 min ago"), kept current by a coarse timer that runs only while a listing is showing.
  Age comes from the monotonic clock; a wall-clock time may go in the tooltip.
- A new toolbar icon follows the existing pattern: `coloredIcon` over an SVG in `resources/icons/`,
  credited in `THIRD_PARTY_NOTICES.md` and the folder's README.

**Traps.**

- Refresh during a running search or link run: decide and write down (recommended: disabled during a
  link run, allowed during a search, where it affects only folders the search has not yet read).
- The toolbar is `ToolButtonTextBesideIcon` with a stretching URL box (`src/ui/MainWindow.cpp:38`,
  `:50`); check the layout at the minimum window width.
- User-visible feature: minor `VERSION` bump.

**Tests.** `tests/widget/tst_mainwindow.cpp` and `tst_filebrowserview.cpp`: a file added out-of-band
appears only after Refresh, selection survives it, and the age label resets. Gate with
`make test-docker`.

## Deferred

Nothing here is scheduled, and this is not a queue: an item moves to "Next up" only on an explicit
decision.

- **Automatic refresh by age (TTL).** Data older than a configurable age re-fetches by itself. The
  store's timestamps and the Stale state are built for it; it needs a settings surface and a policy
  for subscribed versus merely cached directories.
- **Cache eviction.** An entry budget with LRU eviction of unsubscribed directories. Wait for a
  measurement from a real large share; the store already counts entries.
- **OS file icons in the cache.** Left out deliberately: `osIcon` is local, synchronous, keyed by
  extension and already memoized (`src/ui/IconUtil.h:13-17`), so it is never stale, and caching it
  would put a QtGui type in the data layer. Revisit only if icons start coming from the share
  (per-file icons read out of `.exe` resources, thumbnails).
- **Keeping the cache across a reconnect to the same share.** Cleared on disconnect for now.

## Keeping this current

- Update a target's entry on the branch that moves it. When a target lands, its entry is removed, or
  shrinks to what remains and any decision a _later_ unit depends on. What the unit decided lives in
  the PR description, the comments at the site, and an ADR when it binds future work; do not list it
  here.
- "Next up" is an ordered list of entry titles and nothing else. Scope, decisions and traps go in
  the entry.
- A new idea gets an entry and a place in "Next up", or a line under "Deferred". It does not get a
  second plan document.
