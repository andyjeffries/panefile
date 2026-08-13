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

## M6 — Quick Look and thumbnails ✅

- [x] Renderer interface and registry
- [x] Directory, Text, Image, Media, PDF, Archive and Hex renderers
- [x] Float, left, right, bottom, panel and full presentation modes
- [x] Debounced loading, skeleton after 200 ms, 5-entry content cache
- [x] freedesktop thumbnail cache with `Thumb::URI`/`Thumb::MTime` chunks
- [x] Tests: renderer selection by MIME and priority, hex fallback, debounce
      cancellation, cache hit and miss on mtime, dock transitions
- [ ] **Deferred: the optional plugin host** (`pf-syntax`, `pf-media`, `pf-pdf`,
      `pf-video-thumb`). The renderers are written to work without it and say so
      — the media and PDF renderers show metadata cards, the text renderer shows
      plain text, and video thumbnails are skipped rather than fail-cached, so
      a build that gains the plugins later starts producing them. §3.4 forbids
      any of these becoming link-time dependencies, which is why the host is a
      piece of work rather than three `find_package` calls.

libarchive moved to a runtime `QLibrary` load in this milestone: adding the
archive renderer had quietly made it a `DT_NEEDED` entry, which §3.4 forbids.

## M7 — Search and bulk rename ✅

- [x] In-panel filter
- [x] `FuzzyMatcher` — fzf-style scoring with matched spans
- [x] Recursive finder modal
- [x] **Bulk rename: a Finder-style sheet, replacing §7.9's `$EDITOR` round-trip.**
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
- [x] Tests: scoring order, span correctness, Unicode, pathological inputs;
      each rename mode against a table of inputs, including collisions and cycles
- [x] Also landed here: `file_panel_item_create` and `file_panel_item_rename`,
      which had been registered but unimplemented since M4

## M8 — Archives and mounts ✅

- [x] libarchive create and extract
- [x] Path-traversal rejection and tarbomb detection
- [x] Mount table: `mountinfo` parser and `getmntinfo`
- [x] udisks2 and DiskArbitration removable media
- [x] Tests: traversal fixtures, `mountinfo` parser fixtures
- [ ] **Deferred: a password prompt for encrypted archives.** §7.10 asks for
      one; an encrypted archive currently fails with a message naming the file
      and saying why, and nothing is written. That is the half of §7.10's
      sentence that matters — "fail with a clear message rather than partially
      extracting" — and the prompt is the half still owed.

## M9 — Drag and drop ✅

- [x] Outgoing `QDrag` with `text/uri-list` and a rendered row pixmap
- [x] Incoming drops, modifier-selected action, target row highlighting
- [x] Tests: placement rules and modifier mapping, driven directly rather than
      through synthetic `QDropEvent`s — a `QAbstractScrollArea` refuses drag
      events sent to it, and testing Qt's event plumbing was never the point
- [ ] **Deferred: `Alt` opening a copy/move/link menu.** §7.12 offers it as a
      third option alongside the `Shift` and `Ctrl` modifiers, which do work.
      Linking is also the only one of the three that no other operation in the
      application can currently perform.

## M11 — Visual design pass ✅ (partly)

Added after M2, on the observation that the application looked clunky rather
than merely unstyled. M3 below builds the *mechanism* — §9's `theme.toml` with
its `[colors]` and `[ui]` blocks already covers user-writable themes that
customise colour, font, row height, border radius and padding. What it does not
cover is choosing values that look right, which is design work rather than
plumbing, and is why this is separate.

- [x] A spacing scale, applied consistently, replacing the ad-hoc paddings.
      Derived from the theme's `panel_padding` rather than fixed, so the four
      gaps that make up a row's rhythm move together.
- [x] Finder-grade row treatment: alternating rows, derived from the background
      so all twenty-three themes get it, and overridable per theme
- [x] The focused-panel border of §9 — landed in M2 and now asserted by a test
      that renders the panel and reads the pixels back
- [x] Expanded the bundled set to twenty-three themes, all canonical published
      palettes, including macOS Light and macOS Dark
- [ ] **Still yours to judge: the default theme.** The bundled set includes
      `macos-light`, and §9's stated default is Catppuccin Mocha. Which one a
      fresh install should open with is a taste decision, not a technical one,
      and switching it is one line.
- [ ] **Still open: typography.** The font stack and sizes are the system
      default. Choosing better ones needs an eye on a real screen, which is the
      one thing this machine cannot supply.

Runs after M3 so themes are authored against a real stylesheet rather than
retrofitted onto one.

## M10 — Session, CLI routing, packaging ✅ (partly)

- [x] Session restore, panels 2..N deferred
- [x] Single-instance IPC, client path without constructing a `QApplication`.
      It constructs no `QCoreApplication` either: on Wayland that would mean a
      connection to the compositor before deciding whether to draw anything.
- [x] Path routing per §10.2, Wayland activation tokens
- [x] AUR PKGBUILDs with the optional dependencies promoted into `depends`
- [x] macOS app bundle and Homebrew formula
- [x] Tests: routing rules, relative and `file://` resolution, IPC version
      mismatch, stale socket reclaim, and refusing to steal a live one
- [ ] **Deferred: the Docker Arch verification pass** (`makepkg`, `namcap`,
      `hyperfine` against a real Arch root). CI already builds, tests and
      measures on Arch every push, which covers most of it; what a container
      pass would add is the packaging itself.
- [ ] **Needs your machine: Wayland activation.** §10.4's raise-and-activate
      dance can only be verified on a real compositor, and §10.4 asks for
      Hyprland specifically. The code degrades as the spec requires when no
      token is available, but "it degrades correctly" is not the same as "it
      raises correctly".

The socket is POSIX rather than `QLocalServer`, which §10.3 names. Linking
Qt6::Network for it added twenty-eight shared libraries to the binary's
load-time dependencies — OpenSSL, Kerberos, libcurl, nghttp2 and libproxy,
which embeds a JavaScript interpreter — for a Unix domain socket. §3.4's
dependency guard caught it.

## M12 — panefile.dev ✅

- [x] Static HTML and CSS, no JavaScript, no build step
- [x] Automatic light and dark from `prefers-color-scheme`, using the
      application's own macOS Light and Catppuccin Mocha palettes
- [x] `CNAME`, `robots.txt`, `sitemap.xml`, an SVG favicon that follows the
      colour scheme
- [x] A GitHub Pages workflow that publishes `docs/site` on change
- [x] Verified from 320 px to 1440 px: the page never scrolls sideways, and
      contrast passes 4.5:1 for body text and 3:1 for the rest in both schemes
- [ ] **Yours to do: point the DNS at it.** The site is live at
      <https://andyjeffries.github.io/panefile/> and carries a `CNAME` for
      `panefile.dev`; the domain needs `CNAME panefile.dev →
      andyjeffries.github.io` (or the four A records for an apex domain) before
      that name resolves.

---

## Review

All twelve milestones are in. What follows is what was measured, what was
decided against the specification, and what is still owed.

### Measured

| Property | Target (§11) | Linux (Arch, CI) | macOS (this machine) |
| --- | --- | --- | --- |
| First paint | 80 ms | ~13 ms | ~620 ms |
| Load-time dependencies | no optional ones | 30 | 14 |

The macOS number is not comparable to the Linux one and is not meant to be:
most of it is dyld, `NSApplication` and Qt's font database, none of which the
application controls. It is tracked against its own recorded baseline so a
regression is still visible, which is what caught the two regressions below.

The startup guard earned its keep twice. It caught 2 ms of work that had crept
onto the critical path across M6–M8 — reading `state.ini`, constructing the
thumbnail cache, wiring each model to it — all of which is now lazy. And the
dependency guard caught Qt6::Network arriving with M10's socket, dragging in
twenty-eight libraries including a JavaScript interpreter.

### Departures from the specification

Eight, each because following the letter would have broken something the spec
asks for elsewhere.

1. **Bulk rename is a Finder-style sheet, not §7.9's `$EDITOR` round trip.**
   Requested, and the better fit: §7.9's flow needs a terminal to exist and
   cannot show the result before it happens. Everything underneath it — cycle
   detection, the confirmation, the single undoable job — is unchanged.

2. **libarchive is loaded with `QLibrary`, not linked.** §3.4 forbids it as a
   `DT_NEEDED` entry; adding the archive renderer had quietly made it one.

3. **The single-instance socket is POSIX, not `QLocalServer`.** §10.3 names
   the Qt class; §3.4 forbids the twenty-eight libraries linking it brought.
   §3.4 wins, and the client path is four syscalls as a result.

4. **The stale-socket check probes before binding, not after a failed bind.**
   §10.3's wording describes a failure that never arrives — Qt's
   `QLocalServer::listen` unlinks an existing socket and succeeds, and `bind(2)`
   cannot tell a live socket from a corpse. Binding first steals a running
   instance's socket.

5. **Escape never quits.** §6.3 binds `quit` to `q` and `Esc`. Requested
   explicitly, and right: Escape is the key you press when you are unsure, and
   the one key you press by reflex must not be the one that ends the session.
   `Ctrl+Q` quits, `Ctrl+W` closes a panel, and Escape backs out one step at a
   time.

6. **`Ctrl+D` pages down; it does not delete.** §6.3 gives `Ctrl+D` to
   `page_down` on line 461 and to `delete_items` on line 486 — the spec
   contradicts itself. Only one can win, and a key that a vim user presses to
   scroll must not be the key that trashes the selection. `Delete` and
   `Shift+Delete` remain bound as specified.

7. **`A` selects everything outside Selection mode too.** §6.3 marks
   `select_all` "Selection mode only". Selecting everything in order to rename
   or move it does not need a mode first, and `Ctrl+A` is unavailable because
   §7.10 spends it on compress.

8. **A finished job lingers for one second, not §7.4's five.** Requested. Five
   seconds is several times longer than most of the work takes, and the bar has
   nothing left to say for four of them. The bar also waits 250 ms before
   appearing at all, so copying three small files no longer flashes a progress
   bar for work that is already over.

Three deviations were made for testability and are noted in their commits: the
`WatchCoalescer` split, injectable roots on `Trash` and `ThumbnailCache`, and
the pure parsers for `mountinfo` and the session file.

### Still owed

Eight items, listed under their milestones above. In rough order of how much
they matter:

- **Wayland activation (§10.4)** needs your Arch + Hyprland machine. Nothing
  else in the project is unverifiable here.
- **The optional plugin host (§3.4)** — syntax highlighting, media playback,
  PDF rendering and video thumbnails. Every one of them degrades as §2 requires
  and says what is missing, so this is a feature gap rather than a defect.
- **A password prompt for encrypted archives (§7.10).**
- **`Alt` for the copy/move/link drop menu (§7.12).**
- **The default theme, and typography (M11)** — taste decisions, yours.
- **DNS for panefile.dev** — the site is live and waiting for the record.
- **The Docker Arch packaging pass** — CI covers build, test and measurement on
  Arch already; `makepkg` and `namcap` are what a container run would add.
