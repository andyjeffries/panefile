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

## M2 — Panels and input

- [ ] `ActionRegistry` — the only dispatch path
- [ ] `Keymap` — chord trie, sequences, ambiguity and sequence timers
- [ ] Application-level event filter (no `QShortcut`, no `QAction` shortcuts)
- [ ] `FocusManager`, `PanelStrip`, panel lifecycle
- [ ] `Sidebar` with XDG user dirs and pinned entries
- [ ] Help modal generated from the registry
- [ ] Tests: simultaneous bindings, sequence matching, prefix ambiguity, timeout
      expiry, mistyped sequence swallows the key, buffer cleared on focus change,
      `Meta`/`Super` normalisation, conflict detection

## M3 — Config and theme

- [ ] `Config` — parse, validate, per-key fallback, malformed-file banner
- [ ] `hotkeys.toml` parsing, deferred off the critical path
- [ ] `Theme` and `StyleSheetBuilder`, applied before any widget exists
- [ ] Hot reload of all four config files
- [ ] Bundled themes: Catppuccin ×4, Nord, Tokyo Night, Gruvbox, Rose Pine,
      Dracula, and a `system` theme derived from `QPalette`
- [ ] Tests: valid, malformed, partial and unknown-key configs

## M4 — File operations

- [ ] `JobEngine`, two-phase enumerate-then-execute
- [ ] Copy, move, delete, rename, create
- [ ] `copy_file_range`/`FICLONE` and `clonefile` acceleration
- [ ] Conflict modal with apply-to-all
- [ ] `ProcessBar`
- [ ] Trash: XDG spec on Linux, `NSFileManager` on macOS, plus a trash browser
- [ ] Undo stack
- [ ] Tests: cross-device, conflicts, cancellation leaving no partial file,
      `.trashinfo` round-trip including URL encoding

## M5 — Directory watching

- [ ] Portable `WatchCoalescer` — debounce, coalescing, rescan threshold
- [ ] inotify backend
- [ ] FSEvents backend
- [ ] Targeted model deltas; walk up on self-delete
- [ ] Tests: create/delete/rename produce correct deltas, driven by synthetic events

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
- [ ] Bulk rename with cycle detection
- [ ] Tests: scoring order, span correctness, Unicode, pathological inputs

## M8 — Archives and mounts

- [ ] libarchive create and extract
- [ ] Path-traversal rejection and tarbomb detection
- [ ] Mount table: `mountinfo` parser and `getmntinfo`
- [ ] udisks2 and DiskArbitration removable media
- [ ] Tests: traversal fixtures, `mountinfo` parser fixtures

## M9 — Drag and drop

- [ ] Outgoing `QDrag` with `text/uri-list` and a rendered row pixmap
- [ ] Incoming drops, modifier-selected action, target row highlighting

## M10 — Session, CLI routing, packaging

- [ ] Session restore, panels 2..N deferred
- [ ] Single-instance IPC, client path without constructing a `QApplication`
- [ ] Path routing per §10.2, Wayland activation tokens
- [ ] AUR PKGBUILDs with the optional dependencies promoted into `depends`
- [ ] macOS app bundle and Homebrew formula
- [ ] Docker Arch verification pass: build, test, `makepkg`, `namcap`, hyperfine
- [ ] Tests: all three routing cases, multiple paths, relative resolution,
      `--here`/`--panel`, nonexistent paths, IPC version mismatch, stale socket
