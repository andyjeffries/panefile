# Panefile — implementation progress

Milestones follow §16 of the specification. Each must build clean, pass its
tests, hold the startup budget and be manually usable before the next begins.

## M0 — Skeleton ✅

- [x] CMake project with one target per architectural layer
- [x] Configure-time layering check (a layer depending upward is a hard error)
- [x] toml++ vendored as a single header, so a fresh configure needs no network
- [x] Platform seam with Linux and macOS implementations, selected at CMake level
- [x] `Paths` — XDG on Linux, `~/Library` on macOS, `PANEFILE_*_DIR` overrides
- [x] `StartupTrace` and `--startup-trace` / `--quit-after-paint`
- [x] Hand-rolled argv parser off the Qt object system
- [x] `--help`, `--version`, `--config-dir`, `--print-default-config`, `--verbose`
- [x] Default `config.toml` as the single source of truth for defaults
- [x] `MainWindow` with first-paint instrumentation
- [x] Deferred startup queue — one item per event loop turn
- [x] `.clang-format`, `.clang-tidy`
- [x] CI: Arch, Ubuntu 24.04 (GCC 14 + Clang 18), macOS, ASan/UBSan, lint
- [x] Startup budget and dynamic-dependency guards, with baselines committed
- [x] Desktop entry, man page, icon, macOS `Info.plist`
- [x] Tests: command line, paths, startup trace, default config

Startup at M0 (macOS, Release, empty window): 159 ms to first paint, 12 dynamic
dependencies. See `docs/startup-budget.md`.

## M1 — One panel ✅

- [x] `FileEntry`, `DirectoryScanner` with cancellation and 512-entry batching
- [x] `DirectoryModel` over `std::vector<FileEntry>`, custom roles
- [x] `FilterSortProxy` — hidden filter, natural locale-aware sort, five sort keys
- [x] `FilePanel` with header, history, cursor memory and inline error state
- [x] `FileItemDelegate` — colour by kind, symlink targets, struck-through broken links
- [x] `IconProvider` with a process-wide cache and a bundled fallback icon set
- [x] `Format` — sizes, list and footer timestamps, permission strings
- [x] `scripts/ci-local.sh` — one command running every gate CI runs
- [x] Benchmark fixture generator, so the startup guard measures something fixed
- [x] Tests: scanner (symlinks, broken symlinks, permission denied, 10k files,
      superseded scans, awkward filenames), natural sort, formatting, cursor
      memory, and a rendered-panel GUI test

Startup at M1 (macOS, Release, 2,200-entry fixture): 292 ms to first paint,
12 dynamic dependencies. ~170 ms of that is Qt's own start-up rather than ours —
see `docs/startup-budget.md`.

Deferred to M3 with the rest of theming: the panel currently sets its colours
through `QPalette`, which the stylesheet built in M3 replaces.

Deferred to M2: `--benchmark <dir>` is parsed but not yet implemented; it wants
the action registry to report through.

## M2 — Panels and input ✅

- [x] `ActionRegistry` — the only dispatch path, with per-action enablement
- [x] `Chord` — two-form model so `j` and `J` stay distinct (see below)
- [x] `Keymap` — chord trie, layers, sequences, conflict detection
- [x] `KeyDispatcher` — application-level filter with sequence and ambiguity timers
- [x] `PanelStrip` — create, split, close, cycle, equalise, compact layout
- [x] `Sidebar` with XDG user dirs and pinned entries, populated on idle
- [x] `Modal` base and a help modal generated entirely from the registry
- [x] `PanelController` as composition root, registering §6.3's actions
- [x] Tests: every item on §14's list, plus the shipped defaults

Three bugs found while building it, all of which would have shipped:

1. **`QKeySequence` cannot distinguish `j` from `J`** — both parse to `Key_J`,
   but §6.3 binds them to `list_down` and `select_down`. A chord is therefore
   stored either as a key code plus modifiers, or as the character a bare key
   produces. The second form also makes non-US layouts work, where `?` may or
   may not be Shift+/.
2. **`Qt::UniqueConnection` asserts with lambdas**, so the footer was stacking a
   connection per focus change. Connections are now tracked and dropped.
3. **§6.3's own default table binds `Ctrl+D` twice** — `page_down` in Movement
   and `delete_items` in File operations. Movement keeps it; see the reasoning
   in `DefaultKeymap.cpp`. Worth a second opinion.

Two layout bugs a rendered screenshot caught that no assertion would have: a
third panel appearing as an unreadable sliver (a splitter re-divides by stretch
factor, and a widget added later defaults to zero), and the panel header's long
path imposing a minimum width the splitter could not shrink below.

## M3 — Config and theme ✅

- [x] `Config` — parse, validate, per-key fallback with file/line/key issues
- [x] `Theme` and `StyleSheetBuilder`, applied before any widget exists (§3.4)
- [x] `hotkeys.toml`, parsed after the first paint over the already-bound defaults
- [x] `ConfigWatcher` — hot reload of all four files, reloading only what changed
- [x] 23 bundled themes, canonical palettes only, plus `system` from `QPalette`
- [x] The focused-panel border of §9, which needed a `paintEvent` to appear at all
- [x] Tests: valid, malformed, partial, unknown-key, wrong-type, out-of-range and
      invalid-enum configs; theme colours and clamping; stylesheet substitution;
      hotkey replacement, unbinding and conflicts

Themes: Catppuccin (Mocha, Macchiato, Frappé, Latte), Nord, Tokyo Night (Night,
Storm, Day), Gruvbox (Dark, Light), Rosé Pine (Main, Moon, Dawn), Dracula,
Solarized (Dark, Light), One (Dark, Light), Everforest (Dark, Light), Kanagawa,
and macOS Light and Dark from Apple's published system colours.

One behaviour bug the tests caught: a user's `list_down = ["s"]` was *losing* to
the default that already held `s`, because the conflict rule of §6.2 was being
applied to a user-versus-default collision. §6.2's rule is about two entries in
one file. A remap now takes the chord from whatever default held it; two
bindings within the user's own file still conflict, and still keep the first.

Also fixed: the startup baseline is now tagged with the platform plugin it was
measured under. Switching the benchmark to `offscreen` — so it stops opening
windows over the developer's work — made every cocoa-measured baseline report a
2.5× regression that did not exist.

## M4 — File operations ✅

- [x] `Job` — two-phase enumerate-then-execute, cooperative cancellation,
      blocking conflict resolution across the thread boundary
- [x] `JobEngine` — a thread per job, four at once, the rest queued
- [x] `TransferJob` — copy and move, partial files, symlinks recreated not
      followed, metadata preserved, self-containment guard
- [x] `DeleteJob` — trash and permanent, neither following symlinks
- [x] Platform acceleration: `copy_file_range` + `FICLONE`, `fcopyfile`
- [x] `Trash` — XDG spec with an injectable root, plus restore and empty
- [x] `UndoStack` — bounded at 50, refusing to overwrite
- [x] `ConflictModal` and `ProcessBar`
- [x] Selection mode in `FilePanel`, and the clipboard interop that lets a cut
      in Panefile be understood as a cut by Nautilus and Dolphin
- [x] Tests: 29 covering conflicts, cancellation, symlinks, the trash round trip
      with awkward filenames, and undo

Rename and create are deferred to M7, where the Finder-style rename sheet
replaces §7.9's `$EDITOR` flow; the two share their confirmation and their
undo entry, so building them apart would mean building them twice.

The trash browser of §7.5 is deferred to M6, which brings the virtual panel
mode it needs.

## M5 — Directory watching ✅

- [x] `WatchCoalescer` — 150 ms debounce, coalescing, rename pairing, 200-event
      rescan threshold. Pure logic, tested identically on both platforms.
- [x] inotify backend, wrapping the syscalls directly per §7.3
- [x] FSEvents backend
- [x] Targeted model deltas; the panel walks up to the nearest existing ancestor
- [x] `DirectoryWatcher`, refcounted per path so two panels share one watch
- [x] Tests: 26, all the coalescing cases from synthetic events plus one that
      exercises the real backend

Two bugs the tests found, both macOS-only and both silent:

1. FSEvents reports *canonical* paths. On macOS `/tmp` and `/var` are symlinks
   into `/private`, so comparing against the uncanonicalised path discarded
   every event for anything below them.
2. Scheduling the stream on the main dispatch queue works under Qt's Cocoa
   plugin and not under the offscreen one, which uses a poll-based dispatcher
   and never drains that queue. Watching therefore worked in the application and
   failed silently in every headless run. It now uses a private serial queue and
   hops to the object's thread itself.

## M6 — Quick Look and thumbnails

- [ ] Renderer interface and registry
- [ ] Directory, Text, Image, Video, Audio, PDF, Archive and Hex renderers
- [ ] Float, left, right, bottom, panel and full presentation modes
- [ ] Debounced loading, skeleton after 200 ms, 5-entry content cache
- [ ] freedesktop thumbnail cache with `Thumb::URI`/`Thumb::MTime` chunks
- [ ] Optional plugins: syntax, media, pdf, video-thumb
- [ ] Tests: renderer selection by MIME and priority, hex fallback, debounce
      cancellation, cache hit and miss on mtime

## M7 — Search and bulk rename

- [ ] In-panel filter
- [ ] `FuzzyMatcher` — fzf-style scoring with matched spans
- [ ] Recursive finder modal
- [ ] **Bulk rename: a Finder-style sheet, replacing §7.9's `$EDITOR` round-trip.**
      Requested by the user, and the better fit: §7.9 writes the names to a temp
      file, launches `$EDITOR` in a terminal, blocks until it exits, and aborts
      if the line count changed. That is a terminal idiom in a GUI application —
      it needs a terminal to exist, freezes the window while it runs, and cannot
      show what the result will be. Finder's sheet has three modes and a live
      example instead:
      - Replace Text — find / replace with
      - Add Text — text, placed before or after the name
      - Format — Name and Index / Counter / Date, custom format, placement,
        start number
      §7.9's other requirements are unchanged and apply to any rename source:
      cycle detection via temporary names (`a→b`, `b→a`), a confirmation listing
      every `old → new` pair, and execution as a single undoable job.
- [ ] Tests: scoring order, span correctness, Unicode, pathological inputs;
      each rename mode against a table of inputs, including collisions and cycles

## M8 — Archives and mounts

- [ ] libarchive create and extract
- [ ] Path-traversal rejection and tarbomb detection
- [ ] Mount table: `mountinfo` parser and `getmntinfo`
- [ ] udisks2 and DiskArbitration removable media
- [ ] Tests: traversal fixtures, `mountinfo` parser fixtures

## M9 — Drag and drop

- [ ] Outgoing `QDrag` with `text/uri-list` and a rendered row pixmap
- [ ] Incoming drops, modifier-selected action, target row highlighting

## M11 — Visual design pass

Added after M2, on the observation that the application looked clunky rather
than merely unstyled. M3 below builds the *mechanism* — §9's `theme.toml` with
its `[colors]` and `[ui]` blocks already covers user-writable themes that
customise colour, font, row height, border radius and padding. What it does not
cover is choosing values that look right, which is design work rather than
plumbing, and is why this is separate.

- [ ] A spacing scale, applied consistently, replacing the ad-hoc paddings
- [ ] A light, Finder-like default theme — §9 ships only dark themes plus a
      `system` one derived from `QPalette`
- [ ] Finder-grade row treatment: row height, alternating rows, column alignment
- [ ] The focused-panel border of §9, which calls it "the single most important
      visual affordance in the app" and which currently is not drawn at all —
      only a slightly lighter background distinguishes the focused panel
- [ ] Typography: font stack and sizes per platform rather than the system default
- [ ] Expand the bundled set to 10–20 themes, including light variants
      (Catppuccin Latte, Rose Pine Dawn, Solarized Light, Nord Light,
      GitHub Light and Dark, One Dark, Everforest, Kanagawa)

Runs after M3 so themes are authored against a real stylesheet rather than
retrofitted onto one.

## M10 — Session, CLI routing, packaging

- [ ] Session restore, panels 2..N deferred
- [ ] Single-instance IPC, client path without constructing a `QApplication`
- [ ] Path routing per §10.2, Wayland activation tokens
- [ ] AUR PKGBUILDs with the optional dependencies promoted into `depends`
- [ ] macOS app bundle and Homebrew formula
- [ ] Docker Arch verification pass: build, test, `makepkg`, `namcap`, hyperfine
- [ ] Tests: all three routing cases, multiple paths, relative resolution,
      `--here`/`--panel`, nonexistent paths, IPC version mismatch, stale socket
