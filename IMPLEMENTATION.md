# Panefile — Implementation Specification

A keyboard-driven, multi-panel file manager for Wayland, built with C++20 and Qt 6 Widgets.

**Working name:** `panefile`
**Binary:** `pf`
**Status:** greenfield, v1.0 spec
**Target platform:** Linux / Wayland (developed and tested on Arch + Hyprland; must not hard-depend on any compositor)

> The name appears in config paths, the D-Bus service name, the `.desktop` file and the CMake project name. If you want to rename it, do a single case-sensitive find/replace on `panefile`/`Panefile`/`pf` before starting — it is not used anywhere else.

---

## 1. Motivation and design goals

Conventional GTK/Qt file managers open a window (or tab) per location and expect the mouse. On a tiling Wayland compositor this is a poor fit: windows are a scarce, compositor-managed resource, and reaching for the mouse breaks flow.

Panefile takes the interaction model of [superfile](https://superfile.dev) — a terminal file manager — and implements it as a native GUI application. The core idea is that **one window contains N independent directory panels side by side**. Each panel has its own working directory, cursor position, history, sort order and filter. You create, split, close and cycle panels with single keystrokes, and copy between them without ever opening a second window.

### Goals

1. **Every action reachable from the keyboard.** The mouse is supported but never required.
2. **Independent panels, not Miller columns.** Panels are peers; none of them is "the parent of" another.
3. **Fast on real directories.** A 100,000-entry directory must open and scroll without perceptible stutter.
4. **Instant to launch.** Under 80 ms to a painted window. Any functionality that costs startup time is deferred or made lazy — see §3.4.
5. **Never block the UI.** All I/O — stat, read, copy, hash, thumbnail — happens off the GUI thread.
6. **Fully configurable.** Every keybinding remappable, every colour themeable, via plain text config.
7. **A good desktop citizen.** Registers as a `inode/directory` handler, honours XDG basedirs, XDG trash, freedesktop thumbnails, shared-mime-info and the icon theme.

### Non-goals for v1

- Windows and macOS support. X11 is best-effort only (it will probably work; it is not tested).
- Network/remote filesystems (SFTP, SMB, GVfs mounts). Local mounts only.
- A plugin system or scripting API.
- Tabs. Panels replace tabs. If you want another set of panels, launch another window.
- Git status integration, file syncing, cloud providers.

---

## 2. Technology stack

| Concern | Choice | Notes |
| --- | --- | --- |
| Language | C++20 | Concepts, ranges, `std::filesystem`, designated initialisers |
| UI toolkit | Qt 6.7+ Widgets | **No QML, no Qt Quick.** See §2.1 |
| Build | CMake 3.25+ + Ninja | `CMAKE_EXPORT_COMPILE_COMMANDS=ON` for clangd |
| Config parsing | [toml++](https://github.com/marzer/tomlplusplus) | Header-only, via `FetchContent` |
| Archives | libarchive | Create and extract |
| Video thumbnails | libffmpegthumbnailer | **Optional** — `PF_ENABLE_VIDEO_THUMBS`, default ON, degrade gracefully |
| Syntax highlighting | KF6 `KSyntaxHighlighting` | **Optional** — `PF_ENABLE_SYNTAX`, default ON, fall back to plain text |
| Removable media | udisks2 over `QtDBus` | Read `/proc/self/mountinfo` for mounted state |
| Tests | Qt Test (`QTest`) | See §14 |

Required Qt modules: `Core`, `Gui`, `Widgets`, `DBus`, `Concurrent`, `Test`.

Optional Qt modules, each behind a CMake flag with graceful degradation: `Multimedia` (`PF_ENABLE_MEDIA`, for Quick Look video and audio playback) and `Pdf` (`PF_ENABLE_PDF`, for Quick Look PDF rendering; `poppler-qt6` is the fallback).

### 2.1 Why Widgets and not QML

Do not be tempted to reach for QML partway through. The decision is deliberate:

- `QAbstractItemModel` + `QListView` gives virtualised scrolling for free, which is the single most important performance property of this application.
- Focus-chain semantics across many focusable widgets are well-defined in Widgets and genuinely painful in QML. This app has a lot of focusable regions.
- `QDrag` + `QMimeData` produce correct `text/uri-list` drops into external Wayland clients. This path is much better trodden than the QML equivalent.

Where animation is wanted, use `QPropertyAnimation` and `QVariantAnimation` on widget geometry and colour properties.

---

## 3. Architecture

### 3.1 Layers

```
┌────────────────────────────────────────────────────────┐
│ ui/          Widgets, delegates, modals, painting      │
├────────────────────────────────────────────────────────┤
│ app/         Actions, keymap dispatch, focus manager,  │
│              session state, panel lifecycle            │
├────────────────────────────────────────────────────────┤
│ model/       DirectoryModel, sort/filter proxy,        │
│              selection state, fuzzy matcher            │
├────────────────────────────────────────────────────────┤
│ fs/          Scanner, watcher, job engine, trash,      │
│              archive, mounts, thumbnail cache          │
├────────────────────────────────────────────────────────┤
│ config/      TOML load/validate/watch, theme → QSS     │
└────────────────────────────────────────────────────────┘
```

Dependencies point downward only. `fs/` must not include anything from `ui/`. Enforce this with a CMake target per layer.

### 3.2 Source tree

```
panefile/
├── CMakeLists.txt
├── cmake/
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── Application.{h,cpp}        # QApplication subclass, single instance
│   │   ├── ActionRegistry.{h,cpp}     # id → callable, the ONLY dispatch path
│   │   ├── Keymap.{h,cpp}             # QKeySequence → action id, per mode
│   │   ├── FocusManager.{h,cpp}
│   │   ├── PanelController.{h,cpp}
│   │   └── Session.{h,cpp}
│   ├── config/
│   │   ├── Config.{h,cpp}
│   │   ├── Theme.{h,cpp}
│   │   └── StyleSheetBuilder.{h,cpp}
│   ├── fs/
│   │   ├── DirectoryScanner.{h,cpp}
│   │   ├── DirectoryWatcher.{h,cpp}
│   │   ├── JobEngine.{h,cpp}
│   │   ├── jobs/{CopyJob,MoveJob,DeleteJob,TrashJob,ArchiveJob,ExtractJob}.{h,cpp}
│   │   ├── Trash.{h,cpp}
│   │   ├── MountMonitor.{h,cpp}
│   │   └── ThumbnailCache.{h,cpp}
│   ├── model/
│   │   ├── FileEntry.h
│   │   ├── DirectoryModel.{h,cpp}
│   │   ├── FilterSortProxy.{h,cpp}
│   │   └── FuzzyMatcher.{h,cpp}
│   └── ui/
│       ├── MainWindow.{h,cpp}
│       ├── PanelStrip.{h,cpp}
│       ├── FilePanel.{h,cpp}
│       ├── FileItemDelegate.{h,cpp}
│       ├── Sidebar.{h,cpp}
│       ├── QuickLook.{h,cpp}
│       ├── quicklook/
│       │   ├── QuickLookRenderer.h          # abstract interface
│       │   ├── QuickLookRegistry.{h,cpp}
│       │   └── renderers/{Text,Image,Video,Pdf,Archive,Directory,Hex}Renderer.{h,cpp}
│       ├── Footer.{h,cpp}
│       ├── ProcessBar.{h,cpp}
│       ├── Prompt.{h,cpp}
│       └── modals/{ConfirmModal,InputModal,ConflictModal,HelpModal,SortModal,FuzzyModal}.{h,cpp}
├── tests/
├── data/
│   ├── panefile.desktop
│   ├── icons/
│   └── themes/*.toml
└── docs/
```

### 3.3 Threading model

- **GUI thread:** all widgets, all models. Models are mutated only here.
- **Scanner pool:** `QThreadPool` with `maxThreadCount = 4`. Directory enumeration and `stat`ing. Results delivered to the model in batches via queued signal.
- **Job pool:** a dedicated `QThread` per active job group, capped at 4 concurrent groups; further jobs queue. File copies must be cancellable mid-file.
- **Thumbnail pool:** `QThreadPool`, `maxThreadCount = max(2, cores/2)`, low priority. Only ever generates thumbnails for entries currently in or near the viewport.

Rule: no `QWidget` is touched from any thread other than the GUI thread. All cross-thread communication is `Qt::QueuedConnection` signals carrying value types.


### 3.4 Startup and lazy initialisation

**Startup latency is a first-class requirement, not a polish item.** Panefile is launched constantly, often from a keybind, and anything the user can perceive as a delay defeats the point of the application. The target is a painted window with a usable directory listing before the compositor has finished its open animation.

The rule: **the only work permitted before the first paint is the work required to draw one panel.** Everything else is deferred or lazy. Design each subsystem so that "not yet initialised" is a valid state it can be in.

#### Critical path

In `main()`, in this order and nothing else:

1. Construct `QApplication`. Set `setDesktopFileName` and application metadata.
2. Parse argv (hand-rolled; do not construct `QCommandLineParser` for the common case).
3. Read and parse `config.toml` and `theme.toml` only. Not `hotkeys.toml` — see below.
4. Build the stylesheet string and apply it to the application **before** any widget is constructed. Applying a stylesheet after widgets exist forces a full restyle pass over the widget tree.
5. Construct `MainWindow` containing exactly one `FilePanel`.
6. Start the directory scan for the initial path.
7. `show()`.

Everything after `show()` is scheduled onto the event loop.

#### Deferred to after first paint

Post these with `QTimer::singleShot(0, …)` — or a small idle-work queue that runs one item per event loop turn, so a slow item can't stall input:

| Work | Notes |
| --- | --- |
| `hotkeys.toml` parse and keymap trie build | Bind a minimal hardcoded default map first so the very first keypress is never dropped, then swap in the full map |
| Single-instance server *connection handling* | `listen()` itself is two syscalls and stays on the critical path (§10.3); only the signal wiring is deferred |
| Config and theme file watchers | Hot reload is not needed in the first 50 ms |
| Session restore of panels 2..N | Panel 1 is enough to start working; the rest fill in |
| Sidebar contents | Construct the widget empty, populate XDG dirs and pinned entries on idle |
| Trash item count | Only if the sidebar shows it |
| Thumbnail cache directory creation and fail-cache read | On first thumbnail request |

#### Lazy — constructed on first use, never at startup

- **`QuickLook` and every renderer.** No renderer class is instantiated until Quick Look is first opened. This matters most for `KSyntaxHighlighting`: loading its syntax definition repository costs tens of milliseconds and must never happen at startup.
- **All modals.** Help, sort, conflict, input, fuzzy — created on first invocation, then cached.
- **`ProcessBar`** — created when the first job starts.
- **`Prompt`** — created on first `:` or `>`.
- **`MountMonitor` and all D-Bus.** Never open a bus connection at startup. Connect to udisks2 the first time the sidebar's Devices section becomes visible. A `QDBusConnection` plus `GetManagedObjects` is easily 20–40 ms and is pure waste for a user who never touches removable media.
- **libarchive, QtMultimedia, QtPdf, poppler.** See linking, below.
- **`QMimeDatabase`** — first call populates shared-mime-info caches. This is unavoidable but happens on the scanner thread, not the GUI thread, and only for entries that become visible.

#### Linking

- Optional heavy dependencies (`KSyntaxHighlighting`, QtMultimedia, QtPdf, poppler, libffmpegthumbnailer) must **not** be direct link-time dependencies of the main binary. Build each as a small plugin `.so` loaded with `QPluginLoader` on first use, or `dlopen` it behind a thin wrapper. A `DT_NEEDED` entry costs relocation and page-in time at every launch whether or not the code is called.
- Link with `-Wl,--as-needed`. Do **not** use `-Wl,-z,now`; lazy PLT binding is what you want here.
- Enable LTO and `-O2` in release builds. Avoid non-trivial static initialisers entirely — no global `QString`, `QMap` or registry objects at namespace scope. Use function-local statics so construction is on first call.
- No `qrc` blobs beyond icons that are actually needed at startup; themes are read from disk on demand.

#### Measurement

- A `--startup-trace` flag prints monotonic timestamps for each phase (`argv`, `config`, `stylesheet`, `window`, `scan-start`, `first-paint`, `scan-first-batch`) to stderr. First paint is measured from `QWidget::paintEvent` on the panel, not from `show()` returning.
- Add a CI job running `hyperfine --warmup 3 'pf --quit-after-paint ~'` against a fixed-size fixture directory, failing the build if the mean regresses more than 15% from the recorded baseline. Commit the baseline to the repo.
- Check the dynamic dependency count in CI too (`ldd` output size). A silent new `DT_NEEDED` entry is the most common way startup time regresses.

---

## 4. Data model

### 4.1 `FileEntry`

A cheap, copyable value type. One per directory entry.

```cpp
struct FileEntry {
    QString   name;            // basename only
    quint64   size = 0;
    QDateTime modified;
    mode_t    mode = 0;
    uid_t     uid = 0;
    gid_t     gid = 0;
    bool      isDir       = false;   // resolved through symlinks
    bool      isSymlink   = false;
    bool      isBroken    = false;   // dangling symlink
    bool      isHidden    = false;   // leading dot
    bool      isExecutable = false;
    QString   linkTarget;            // empty unless isSymlink
    QString   mimeName;              // lazily filled, see §4.3
};
```

Do **not** use `QFileSystemModel`. It is tree-shaped, does its own watching, hits the filesystem on the GUI thread, and gives you no control over batching. A flat per-directory model is both simpler and faster here.

### 4.2 `DirectoryModel`

`QAbstractListModel` over a `std::vector<FileEntry>`.

- `setPath(QString)` cancels any in-flight scan, clears, and starts a new one.
- The scanner delivers entries in batches of 512 via `entriesReady(QVector<FileEntry>)`. The model appends with `beginInsertRows`/`endInsertRows` so the view stays responsive on huge directories.
- Emits `scanStarted`, `scanProgress(int count)`, `scanFinished`, `scanFailed(QString reason)`.
- Exposes custom roles: `EntryRole` (the whole `FileEntry` as `QVariant`), `IconRole`, `ThumbnailRole`, `MatchSpansRole` (for highlighting fuzzy matches).

Symlink resolution uses `lstat` then `stat`; a failing `stat` on a successful `lstat` means `isBroken = true`. Never follow symlinks during recursive operations (see §7.4).

### 4.3 MIME and icons

- MIME type from `QMimeDatabase`, content-sniffing **disabled by default** (extension only) for speed. Sniff only for extensionless files, and only when the entry becomes visible.
- Icons from `QIcon::fromTheme(mime.iconName(), QIcon::fromTheme(mime.genericIconName()))`, with a bundled fallback set.
- Cache resolved `QIcon`s in a process-wide `QHash<QString, QIcon>` keyed on icon name.

### 4.4 `FilterSortProxy`

`QSortFilterProxyModel` subclass handling, in order:

1. Hidden-file filter (toggled per panel).
2. Search/fuzzy filter string (per panel).
3. Sort: directories first (configurable), then by the panel's sort key.

Sort keys: `name`, `size`, `modified`, `type`, `random`. Name sorting must be **natural** (`file2` before `file10`) and locale-aware — use `QCollator` with `setNumericMode(true)` and `setCaseSensitivity(Qt::CaseInsensitive)`. Reverse is an independent toggle.

---

## 5. User interface

### 5.1 Window layout

The main window is panels, sidebar and status furniture only. There is **no permanent preview pane** — Quick Look (§7.6) provides previewing, either as a floating overlay or docked into one of the edges.

```
┌──────────┬────────────────────────┬────────────────────────┐
│          │  Panel 1  [focus]      │  Panel 2               │
│ Sidebar  │  ~/Developer           │  /tmp                  │
│          ├────────────────────────┼────────────────────────┤
│ ~        │  ▸ dotfiles/           │    build.log           │
│ Downloads│  ▸ panefile/           │    core.1234           │
│ Projects │    README.md           │    notes.txt           │
│ ─────    │    TODO                │                        │
│ /mnt/usb │                        │                        │
├──────────┴────────────────────────┴────────────────────────┤
│ Footer: -rw-r--r--  andy:andy  4.2 KiB  2026-08-11 14:02   │
├────────────────────────────────────────────────────────────┤
│ Process: Copying 128 files… ████████░░░░░░░ 54% · 12 MiB/s │
└────────────────────────────────────────────────────────────┘
```

With Quick Look invoked in its default floating mode, pressing `Space` overlays the window:

```
┌──────────┬────────────────────────┬────────────────────────┐
│          │  ┌──────────────────────────────────────────┐   │
│ Sidebar  │  │ README.md          4.2 KiB   markdown  ✕ │   │
│          │  ├──────────────────────────────────────────┤   │
│ ~     ░░░│░░│                                          │░░░│
│ Downlo░░░│░░│  # Panefile                              │░░░│
│ Projec░░░│░░│                                          │░░░│
│ ─────    │  │  A keyboard-driven, multi-panel file …   │   │
│ /mnt/usb │  │                                          │   │
├──────────┤  │  ↑↓ move  ␣ close  ⏎ open  d dock        │   │
│ Footer:  │  └──────────────────────────────────────────┘   │
└──────────┴────────────────────────────────────────────────-┘
```

Regions:

| Region | Widget | Toggle | Notes |
| --- | --- | --- | --- |
| Sidebar | `Sidebar` | `s` focuses, `Ctrl+S` toggles visibility | Home, XDG user dirs, pinned dirs, mounted volumes |
| Panel strip | `PanelStrip` | always visible | `QSplitter(Qt::Horizontal)` of `FilePanel`s |
| Quick Look | `QuickLook` | `Space` | Floating overlay by default; dockable — see §7.6 |
| Footer | `Footer` | `F` | Metadata for cursor item; permissions, owner, size, mtime, MIME |
| Process bar | `ProcessBar` | auto-shows when jobs active; `p` focuses | Aggregate progress, expandable to per-job list |
| Prompt | `Prompt` | `:` shell, `>` internal | Single-line, overlays the footer |

Panel widths are equal by default and user-draggable via the splitter. `Ctrl+=` resets to equal.

### 5.2 `FilePanel`

Contains a header (breadcrumb path, item count, sort indicator, mode badge), a `QListView` in `ListMode` with `setUniformItemSizes(true)`, and an inline search bar that appears on `/`.

Per-panel state, all independently persisted:

```cpp
struct PanelState {
    QString path;
    QString cursorName;          // remembered by name, not index
    QSet<QString> selection;
    QStringList backStack, forwardStack;
    SortKey sortKey = SortKey::Name;
    bool reverseSort = false;
    bool showHidden = false;
    QString filter;
    PanelMode mode = PanelMode::Normal;
};
```

**Cursor memory:** when you navigate out of a directory and back in, the cursor must land on the directory you came from. Keep a bounded LRU (256 entries) of `path → cursorName`.

### 5.3 `FileItemDelegate`

Custom `QStyledItemDelegate`. Paints, left to right: selection marker, icon or thumbnail, name (with fuzzy-match spans in the accent colour), symlink arrow and target, right-aligned size and mtime. Broken symlinks render in the error colour with a strikethrough. Executables get the accent colour.

Row height is fixed per theme so `uniformItemSizes` holds. Thumbnails, when enabled, replace the icon at the same box size.

### 5.4 Modals

Modals are frameless child widgets centred over the main window with a dimmed backdrop, **not** separate top-level windows — that matters on a tiling compositor. They grab focus, `Esc` always dismisses, `Enter` always confirms.

---

## 6. Input model

### 6.1 Modes

| Mode | Entered by | Effect |
| --- | --- | --- |
| `Normal` | default | Single-key bindings active |
| `Selection` | `v` | Movement extends the selection; a badge shows in the panel header |
| `Typing` | `/`, `:`, `>`, or any input modal | All single-key bindings suspended; only `confirm_typing`/`cancel_typing` and editing keys apply |

Mode is **per panel**, except `Typing` which is global while an input has focus.

### 6.2 Dispatch

All behaviour goes through `ActionRegistry`. Every action has a stable string id (`list_down`, `paste_items`, …), a human-readable description for the help modal, and a callable. Nothing in the UI may call a behaviour function directly — menu items, keybindings and the command prompt all resolve through the registry. This is what makes remapping and the help modal work for free.

Do **not** use `QShortcut` or `QAction` shortcuts. They are limited to four-element sequences, resolve ambiguity in ways you can't control, and don't give you modal behaviour. Install a single `QApplication`-level event filter and implement dispatch yourself.

#### Binding model

A binding is a **sequence of one or more chords**. A chord is a set of modifiers plus one key (`Ctrl+C`, `Super+C`, `g`, `Shift+G`). An action may have **any number of bindings**, and they are fully independent — so `copy_items` can be bound to `Ctrl+C`, `Super+C` and `yy` simultaneously, all active at once.

```cpp
struct Chord   { Qt::KeyboardModifiers mods; int key; };
struct Binding { QVector<Chord> chords; };          // size 1 = simple, >1 = sequence

class Keymap {
    // mode → trie of chords → action id
    // Lookup returns one of: NoMatch, PartialMatch, ExactMatch(actionId)
};
```

Store bindings per mode in a **trie keyed on chords**, not a flat hash. This is what makes arbitrary-length sequences work and lets you distinguish "no such binding" from "a prefix of something longer".

#### Resolution algorithm

Maintain a `pendingChords` buffer, initially empty. On each `QKeyEvent`:

1. Ignore pure modifier presses (`Ctrl`, `Shift`, `Super`, `Alt` alone) — they never advance the buffer.
2. If the focused widget is a text input and the mode is `Typing`, only `confirm_typing`, `cancel_typing` and bindings with a modifier are considered; bare printable keys go to the widget.
3. Append the chord to `pendingChords` and look it up in the current mode's trie:
   - **ExactMatch and no longer binding shares this prefix** → invoke the action, clear the buffer, accept the event.
   - **ExactMatch but a longer binding also has this prefix** (e.g. `g` is bound *and* `gh` exists) → start the ambiguity timer (`config.keys.sequence_timeout_ms`, default 500). If another key arrives first, continue resolving. If the timer fires, invoke the shorter action. This case should be rare; warn about it at config-load time.
   - **PartialMatch only** → keep the buffer, start the sequence timer (default 1000 ms), show the pending prefix in the footer (`g-`), accept the event.
   - **NoMatch** → if the buffer had content, clear it and swallow the event (a mistyped sequence must not leak keys into the app). If the buffer was empty, let the event fall through to the widget.
4. `Esc` always clears the pending buffer and is otherwise handled normally.
5. Any pointer click, focus change or panel switch clears the pending buffer.

#### Precedence

When the same chord sequence is bound in more than one place, resolve in this order, first match wins:

1. Active modal's own bindings.
2. Current panel mode (`Selection` before `Normal`).
3. Global bindings.

Two actions bound to the identical sequence *within the same layer* is a config error: log it with both action ids, keep the one declared first, and list the conflict in the help modal so the user can see it.

#### Sequence-friendly defaults

Reserve `g` as the "go" prefix. Bundled defaults use `gg` (top), `gh` (home), `gr` (root), `gc` (config dir), `gt` (trash), `gp` (previous directory). Users can add their own without any code change.

### 6.3 Default keymap

Derived from superfile's defaults so muscle memory carries over. All remappable.

**General**

| Action id | Default | Description |
| --- | --- | --- |
| `confirm` | `Enter`, `Right`, `l` | Enter directory / open file |
| `quit` | `q`, `Esc` | Close modal, or quit if none |
| `open_help_menu` | `?` | Help modal listing all bindings |
| `open_command_line` | `:` | Shell command in panel's cwd |
| `open_panefile_prompt` | `>` | Internal command prompt (action ids) |
| `open_fuzzy_find` | `Ctrl+F` | Recursive fuzzy finder modal |
| `toggle_theme_dark_light` | `Ctrl+T` | |

**Panel management**

| Action id | Default | Description |
| --- | --- | --- |
| `create_new_file_panel` | `n` | New panel at home |
| `split_file_panel` | `N` | New panel duplicating the focused one |
| `close_file_panel` | `w` | Close focused panel (never the last) |
| `next_file_panel` | `Tab`, `L` | |
| `previous_file_panel` | `Shift+Tab`, `H` | |
| `quick_look` | `Space` | Toggle Quick Look (§7.6) |
| `quick_look_cycle_dock` | `Ctrl+Space` | float → right → bottom → left → panel |
| `quick_look_fullscreen` | `Ctrl+Shift+Space` | |
| `toggle_footer` | `F` | |
| `focus_on_sidebar` | `s` | |
| `focus_on_process_bar` | `p` | |
| `open_sort_options_menu` | `o` | |
| `toggle_reverse_sort` | `R` | |
| `equalise_panels` | `Ctrl+=` | |

**Movement**

| Action id | Default | Description |
| --- | --- | --- |
| `list_up` / `list_down` | `k` / `j`, arrows | |
| `page_up` / `page_down` | `PgUp` / `PgDn`, `Ctrl+U` / `Ctrl+D` | |
| `list_top` / `list_bottom` | `g g` / `G` | |
| `go_home` | `g h`, `Alt+Home` | Navigate panel to `$HOME` |
| `go_root` | `g r` | Navigate panel to `/` |
| `go_config` | `g c` | Navigate to the config directory |
| `go_trash` | `g t` | Open trash view in this panel |
| `go_previous` | `g p` | Last visited directory |
| `parent_directory` | `h`, `Left`, `Backspace` | |
| `go_back` / `go_forward` | `Alt+Left` / `Alt+Right` | History |
| `toggle_dot_file` | `.` | |
| `search_bar` | `/` | Filter within current directory |
| `change_panel_mode` | `v` | Normal ⇄ Selection |
| `select_all` | `A` | Selection mode only |
| `select_up` / `select_down` | `K` / `J`, `Shift`+arrows | Selection mode only |
| `pinned_directory` | `P` | Pin/unpin to sidebar |

**File operations**

| Action id | Default | Description |
| --- | --- | --- |
| `file_panel_item_create` | `Ctrl+N` | Trailing `/` creates a directory |
| `file_panel_item_rename` | `Ctrl+R` | Inline rename |
| `bulk_rename` | `Ctrl+B` | Selection → `$EDITOR` (see §7.7) |
| `copy_items` / `cut_items` | `Ctrl+C` / `Ctrl+X` | |
| `paste_items` | `Ctrl+V` | |
| `delete_items` | `Ctrl+D`, `Delete` | To trash |
| `permanently_delete_items` | `D`, `Shift+Delete` | Confirms twice |
| `copy_path` | `Ctrl+P` | Absolute path(s) to clipboard |
| `copy_present_working_directory` | `c` | |
| `compress_file` | `Ctrl+A` | |
| `extract_file` | `Ctrl+E` | |
| `open_file_with_editor` | `e` | `$EDITOR` in a terminal |
| `open_current_directory_with_editor` | `E` | |
| `open_with_default_app` | `Ctrl+Enter` | `xdg-open` |
| `open_terminal_here` | `T` | `$TERMINAL` in cwd |

---

## 7. Feature specifications

### 7.1 Panel lifecycle

- Minimum 1 panel, maximum 10. Attempting to exceed shows a transient footer message.
- Closing the focused panel moves focus to the panel on its left, or right if it was leftmost.
- New panels open at `$HOME` unless `config.new_panel_path` says otherwise; `split_file_panel` copies the focused panel's path, sort and filter settings but not its selection.
- Below a total width of 600 px, hide the sidebar automatically; below 400 px, show only the focused panel.

### 7.2 Directory scanning

Use `readdir` via `std::filesystem::directory_iterator` with `skip_permission_denied`. Do **not** call `stat` during enumeration if the `d_type` field already answers the question — only `stat` for size, mtime and mode, and do it in the scanner thread.

Scans are cancellable: the scanner checks an atomic flag every batch. A scan that is superseded must abandon its results, not deliver them.

If a scan fails (permission denied, ENOENT), the panel shows an inline error state with the reason and a hint, and keeps the previous listing available via `go_back`.

### 7.3 Watching

One `DirectoryWatcher` per distinct open path (refcounted — two panels on the same path share one watch). Wrap inotify directly rather than `QFileSystemWatcher`, so you can:

- Coalesce bursts with a 150 ms debounce timer.
- Apply targeted `IN_CREATE` / `IN_DELETE` / `IN_MOVED_FROM` / `IN_MOVED_TO` / `IN_ATTRIB` updates to the model rather than rescanning.
- Detect `IN_DELETE_SELF` / `IN_MOVE_SELF` and walk the panel up to the nearest existing ancestor.

Fall back to a full rescan if more than 200 events arrive in one debounce window.

### 7.4 Job engine

Every mutating operation is a `Job`:

```cpp
class Job : public QObject {
    // signals:
    void progress(quint64 bytesDone, quint64 bytesTotal,
                  int filesDone, int filesTotal, QString currentPath);
    void conflict(QString src, QString dst, ConflictInfo info);   // blocks until resolved
    void finished(JobResult);
    // slots:
    void cancel();
    void resolveConflict(ConflictResolution);
};
```

Requirements:

- **Two-phase:** enumerate first (count files and bytes), then execute. This gives real progress instead of a spinner. Show an indeterminate bar during enumeration.
- **Same-filesystem moves** use `rename(2)` and are instant. Detect by comparing `st_dev`. Cross-device moves are copy-then-delete, and the delete only happens after a fully successful copy.
- **Copies** use `copy_file_range(2)` where available (enables server-side/reflink copies on btrfs and XFS), falling back to a 1 MiB buffered read/write loop. Check cancellation between chunks.
- **Preserve** mode, mtime, and — best-effort, never fatal — ownership and extended attributes.
- **Never follow symlinks.** Recreate them as symlinks pointing at the same target.
- **Conflict resolution** offers: overwrite, overwrite if newer, skip, rename (auto-suffix ` (2)`), and each with an "apply to all remaining" checkbox. Show both files' size and mtime.
- **Cancellation** is cooperative and must leave no half-written destination file — write to `name.pf-partial` and `rename` on completion.
- Refuse, with a clear error, to copy a directory into itself or into its own descendant.

The `ProcessBar` shows the aggregate of all active jobs; expanding it (`p` then `Enter`) lists each with its own progress and a cancel button. Completed jobs linger for 5 s then fade.

### 7.5 Trash

Use `QFile::moveToTrash()` where it succeeds — it implements the XDG Trash spec including `.Trash-$uid` at the mount point for files on other filesystems. Where it fails, fall back to an explicit implementation:

- `$XDG_DATA_HOME/Trash/files/` and `.../info/`, with a `<name>.trashinfo` containing `Path=` (URL-encoded, absolute) and `DeletionDate=` in ISO 8601 local time.
- On name collision, append `-1`, `-2`, … to the trashed name.

Also implement a trash browser: the sidebar has a `Trash` entry that opens a panel in a virtual mode listing trashed items with their original paths and deletion dates, supporting restore (`Ctrl+Z` on selection) and empty-trash.

### 7.6 Quick Look

Modelled on macOS Quick Look. A single `Space` press shows a large, rich preview of the cursor item; a second `Space` dismisses it. Crucially, **while it is open the arrow keys still move the panel cursor**, and the Quick Look content follows — so you can hold `j` and flick through a directory of images.

This replaces the always-on preview pane. Quick Look is transient by default, but can be pinned into a dock position for users who do want it permanently visible.

#### Presentation modes

`config.quicklook.dock` selects the default; `quick_look_cycle_dock` (`Ctrl+Space`) cycles at runtime and persists the choice.

| Mode | Behaviour |
| --- | --- |
| `float` (default) | Centred frameless overlay at 70% of window size (configurable), dimmed backdrop, drop shadow. Transient: closes on `Space`/`Esc`. Never a separate top-level window — that matters on a tiling compositor |
| `right` / `left` | Docked into a `QSplitter` beside the panel strip, persistent, user-resizable |
| `bottom` | Docked below the panel strip, above the footer. Good for wide monitors with tall panels |
| `panel` | Occupies a slot in the panel strip itself, as though it were another panel. Counts toward `panels.max_count` |
| `full` | Fills the whole content area, hiding panels. Toggle with `Ctrl+Shift+Space` from any other mode; returns to the previous mode when dismissed |

Behaviour that differs between float and docked:

- **Float** grabs keyboard focus but forwards movement keys to the source panel. It closes on `Space`, `Esc`, focus loss, or panel switch.
- **Docked** modes never take focus. Focus stays in the panel; the dock simply tracks the focused panel's cursor. `Space` in a docked mode toggles the dock's *visibility*, not the modality. `Tab` may cycle into the dock (to scroll a long document); `Esc` returns to the panel.
- In every mode, the content follows the **focused panel's** cursor. Switching panels re-targets it.

#### Interaction

| Key | Action |
| --- | --- |
| `Space` | Toggle Quick Look |
| `j`/`k`, arrows | Move the source panel's cursor; content follows |
| `Enter` | Open in the default application and dismiss |
| `e` | Open in `$EDITOR` and dismiss |
| `Ctrl+Space` | Cycle dock position |
| `Ctrl+Shift+Space` | Toggle full-screen mode |
| `Esc` | Dismiss (float) or return focus to panel (docked) |
| `+` / `-` / `0` | Zoom in / out / fit — image and PDF renderers |
| `[` / `]` | Previous / next page — PDF and archive renderers |
| `/` | Search within the rendered content — text renderer |

The header bar shows filename, size, MIME description and a close affordance. The footer bar shows renderer-specific hints and, when it applies, page or zoom state. Both are themed and can be hidden via `quicklook.chrome = false`.

#### Renderer architecture

Do not write one giant switch statement. Define an interface and register implementations:

```cpp
class QuickLookRenderer {
public:
    virtual ~QuickLookRenderer() = default;
    virtual bool     canRender(const QMimeType&, const FileEntry&) const = 0;
    virtual int      priority() const { return 0; }   // higher wins ties
    virtual QWidget* createWidget(QWidget* parent) = 0;
    // Called on the GUI thread with content already loaded off-thread.
    virtual void     setContent(QuickLookContent&&) = 0;
    virtual void     clear() = 0;
    virtual QString  statusText() const { return {}; }
    virtual bool     handleKey(QKeyEvent*) { return false; }
};
```

`QuickLookRegistry` picks the highest-priority renderer whose `canRender` returns true, falling back to `HexRenderer`. Renderer widgets are created once and reused — creating a `QPdfView` per cursor movement will be visibly slow.

#### Renderers required for v1

| Renderer | Handles | Content |
| --- | --- | --- |
| `DirectoryRenderer` | `isDir` | Child listing (first 200), item count, aggregate size computed lazily and cancellably, `du`-style top-5 largest children |
| `TextRenderer` | `text/*` and known code MIME types | Full file up to the read cap, syntax highlighted via `KSyntaxHighlighting`, line numbers, wrap toggle, in-content search |
| `ImageRenderer` | `image/*` | Full-resolution decode off-thread, zoom and pan, EXIF summary (dimensions, camera, date), animated GIF/WebP playback |
| `VideoRenderer` | `video/*` | `QMediaPlayer` playback with scrub bar, `p` to play/pause, plus duration, resolution and codec. Falls back to a static thumbnail if QtMultimedia is unavailable |
| `AudioRenderer` | `audio/*` | Waveform-free minimal player, plus tag metadata |
| `PdfRenderer` | `application/pdf` | Paged rendering via `QtPdf` (preferred) or `poppler-qt6`, with page navigation and zoom |
| `ArchiveRenderer` | archive MIME types, via libarchive | Entry tree with sizes and compression ratios. No extraction |
| `HexRenderer` | fallback | Hex and ASCII dump of the first 4 KiB, plus MIME, size and `file(1)`-style description |

#### Loading rules

- Content loading is **always** off the GUI thread, into a `QuickLookContent` value type handed back by queued signal.
- Debounce cursor changes by 120 ms so holding `j` doesn't queue a hundred loads. Cancel any in-flight load when the cursor moves again.
- Show a skeleton/spinner state after 200 ms of loading, never before — instant content must not flash a spinner.
- `quicklook.max_read_bytes` (default 64 MiB) caps text and hex reads. Images and video are streamed by their respective libraries and are not subject to it, but files above `quicklook.max_decode_mb` (default 500) show a metadata-only card with an "open anyway" action.
- Cache the last 5 rendered contents keyed on path plus mtime, so moving back and forth between two files is instant.
- A renderer that throws or fails must degrade to `HexRenderer` with an inline error note, never crash or blank.

### 7.7 Thumbnails

Implement the freedesktop thumbnail spec so the cache is shared with other applications:

- Store at `$XDG_CACHE_HOME/thumbnails/{normal,large}/<md5-of-file-uri>.png` (`normal` = 128 px, `large` = 256 px).
- Embed `Thumb::URI` and `Thumb::MTime` PNG text chunks; treat a thumbnail whose `Thumb::MTime` doesn't match the source as stale.
- Write failures to `$XDG_CACHE_HOME/thumbnails/fail/panefile/` so you don't retry a file that can't be thumbnailed.
- Only request thumbnails for rows in or within 20 rows of the viewport; cancel requests for rows that scroll away.
- Config: `thumbnails.enabled`, `thumbnails.max_file_size_mb` (default 200), `thumbnails.video` (default true).

### 7.8 Fuzzy find

Two distinct things, don't conflate them:

1. **In-panel filter** (`/`): filters the current directory's model live via the proxy. Substring by default, fuzzy if `config.search.fuzzy = true`. `Enter` keeps the filter and returns focus to the list; `Esc` clears it.
2. **Recursive finder** (`Ctrl+F`): a modal that walks the panel's subtree on a worker thread, streaming candidates into a scored list. `Enter` navigates the panel to the result's directory and puts the cursor on it.

Implement `FuzzyMatcher` as an fzf-style matcher: a case-insensitive subsequence test as a fast reject, then a scoring pass rewarding consecutive matches, matches at word boundaries and camelCase humps, matches after path separators, and penalising gap length. Return both the score and the matched character spans so the delegate can highlight them. This must be a pure, header-testable function — see §14.

Cap the recursive walk at `config.search.max_results` (default 10,000) and respect `.gitignore` if `config.search.respect_gitignore` is true.

### 7.9 Bulk rename

`Ctrl+B` on a selection:

1. Write the selected basenames, one per line, to a temp file with a comment header.
2. Launch `$EDITOR` in a terminal, blocking until it exits.
3. Read back. Abort with an explanatory modal if the line count changed.
4. Compute the diff, detect cycles (`a→b`, `b→a`) and resolve them via temporary names.
5. Show a confirmation modal listing every `old → new` pair before executing.
6. Execute as a single undoable job.

### 7.10 Archives

Via libarchive:

- **Create** (`Ctrl+A`): modal picks format (`zip`, `tar.gz`, `tar.zst`, `7z` if supported) and name, defaulting to the cursor item's basename. Progress via the job engine.
- **Extract** (`Ctrl+E`): extracts into `<archive-basename>/` in the panel's cwd. Detect "tarbombs" — if the archive has a single top-level directory, extract directly instead of nesting.
- **Guard against path traversal.** Reject any entry whose resolved destination escapes the extraction root. This is not optional.
- Password-protected archives prompt; if libarchive can't handle the format, fail with a clear message rather than partially extracting.

### 7.11 Mounts and removable media

- Enumerate mounted filesystems from `/proc/self/mountinfo`, re-read on `IN_MODIFY`.
- Discover mountable-but-unmounted devices via udisks2 on the system bus (`org.freedesktop.UDisks2`, `ObjectManager.GetManagedObjects`), filtering to block devices with `HintAuto` or removable media.
- Sidebar shows these under a "Devices" heading with a mount state indicator. `Enter` mounts (via `org.freedesktop.UDisks2.Filesystem.Mount`) and navigates; `u` on a mounted device unmounts.
- Watch `InterfacesAdded`/`InterfacesRemoved` for hotplug.
- Never block the GUI thread on a D-Bus call — use `QDBusPendingCallWatcher` throughout.

### 7.12 Drag and drop

**Outgoing:** dragging from a panel starts a `QDrag` whose `QMimeData` carries `text/uri-list` (all selected items, or the cursor item if nothing selected) and `text/plain` (newline-joined paths). Set `Qt::CopyAction | Qt::MoveAction`. Attach a rendered pixmap of up to 3 rows plus a "+N" badge.

**Incoming:** panels accept `text/uri-list`. Default action is copy; `Shift` forces move, `Ctrl` forces copy, `Alt` opens a small menu offering copy/move/link. Dropping onto a directory row targets that directory; dropping onto empty space targets the panel's cwd. Highlight the drop target row clearly.

Note: on Wayland, a drag must originate from a genuine pointer press-and-move — start it from `mouseMoveEvent` past `QApplication::startDragDistance()`, not from a timer.

### 7.13 Undo

Maintain a bounded stack (50 entries) of undoable operations: move, rename, bulk rename, trash. `Ctrl+Z` undoes the last. Copy and permanent delete are **not** undoable and must be labelled as such in the confirmation. Undo of a trash operation restores from trash by `.trashinfo` path.

---

## 8. Configuration

XDG paths, all under `$XDG_CONFIG_HOME/panefile/` (default `~/.config/panefile/`):

| File | Contents |
| --- | --- |
| `config.toml` | Behaviour settings |
| `hotkeys.toml` | Action id → key sequence(s) |
| `theme.toml` | Active theme name, or inline overrides |
| `themes/*.toml` | User themes (bundled ones live in `$PREFIX/share/panefile/themes/`) |

State (pinned dirs, session, window geometry) goes to `$XDG_DATA_HOME/panefile/`, **not** the config dir. Never write to a user's config file.

### 8.1 `config.toml`

```toml
[general]
new_panel_path       = "~"
restore_session      = true
confirm_on_quit      = false
single_instance      = true

[panels]
default_count        = 1
max_count            = 10
directories_first    = true
default_sort         = "name"       # name | size | modified | type
show_hidden          = false

[quicklook]
dock                 = "float"     # float | right | left | bottom | panel | full
float_size_percent   = 70          # of window, in float mode
dock_size_percent    = 35          # of window, in right/left/bottom modes
chrome               = true        # show header and hint bars
debounce_ms          = 120
max_read_bytes       = 67108864    # text and hex renderers
max_decode_mb        = 500         # above this, metadata card only
follow_cursor        = true        # false = snapshot on open, don't track
close_on_panel_switch = true       # float mode only

[thumbnails]
enabled              = true
video                = true
max_file_size_mb     = 200

[search]
fuzzy                = true
respect_gitignore    = true
max_results          = 10000

[operations]
confirm_delete       = true
confirm_trash        = false
default_conflict     = "ask"        # ask | overwrite | skip | rename
follow_symlinks      = false

[cli]
file_action          = "select"    # select | quicklook | launch
on_focused           = "current_panel"
on_unfocused         = "new_panel"

[external]
editor               = ""            # empty = $EDITOR, then $VISUAL, then nano
terminal             = ""            # empty = $TERMINAL, then heuristic
```

### 8.2 `hotkeys.toml`

Each action maps to a list of bindings. Every binding in the list is active simultaneously. A binding containing spaces is a **sequence** — the chords must be pressed in order.

```toml
[keys]
sequence_timeout_ms  = 1000     # time to complete a started sequence
ambiguity_timeout_ms = 500      # wait before firing a binding that is also a prefix

[normal]
list_down    = ["j", "Down"]
list_up      = ["k", "Up"]
confirm      = ["Return", "Right", "l"]

# Multiple simultaneous bindings, mixing modifiers
copy_items   = ["Ctrl+C", "Meta+C", "y y"]
paste_items  = ["Ctrl+V", "Meta+V", "p"]

# Sequences
list_top     = ["g g"]
go_home      = ["g h", "Alt+Home"]
go_root      = ["g r"]
go_trash     = ["g t"]
delete_items = ["d d", "Delete"]

[selection]
select_down  = ["J", "Shift+Down"]
```

Notes for the implementer:

- **`Meta` is the Super/Windows key** in Qt's naming. `QKeySequence::fromString` accepts `"Meta+C"`. Accept `Super+C` as an alias in config and normalise it, because that's what users will type.
- Parse each chord with `QKeySequence::fromString`, then reject any chord string that parses to more than one element — Qt's own four-element sequence syntax (comma-separated) is **not** used here; whitespace separation is the sequence syntax, so the two don't collide.
- Reject and warn on: unparseable chords, sequences longer than 5 chords, and any binding whose first chord is `Esc`.
- `unbind` is available as a pseudo-action to remove a default without replacing it: `open_zoxide = []` also works.
- The help modal (`?`) lists every action with all of its bindings, rendered as `Ctrl+C  ·  Super+C  ·  y y`.

### 8.3 Validation and reload

Config parsing must never crash or silently produce garbage. On a malformed file: fall back to defaults for the affected keys, and show a dismissible banner naming the file, line and problem. Watch all four config files and hot-reload on change — including regenerating the stylesheet — without restarting.

---

## 9. Theming

`theme.toml` defines named colours; `StyleSheetBuilder` compiles them into a QSS string applied to the whole application, plus a `ThemePalette` struct that the delegate reads directly (delegates paint manually and can't use QSS).

```toml
name = "Catppuccin Mocha"
[colors]
background        = "#1e1e2e"
surface           = "#313244"
overlay           = "#6c7086"
text              = "#cdd6f4"
subtext           = "#a6adc8"
accent            = "#89b4fa"
selection_bg      = "#45475a"
cursor_bg         = "#585b70"
directory         = "#89b4fa"
executable        = "#a6e3a1"
symlink           = "#94e2d5"
broken            = "#f38ba8"
archive           = "#fab387"
image             = "#f9e2af"
error             = "#f38ba8"
warning           = "#fab387"
success           = "#a6e3a1"
border            = "#45475a"
border_focused    = "#89b4fa"

[ui]
font_family       = ""          # empty = system default
font_size         = 10
row_height        = 24
border_radius     = 6
panel_padding     = 8
```

Ship Catppuccin (all four flavours), Nord, Tokyo Night, Gruvbox Dark, Rose Pine and Dracula. Also ship a `system` theme that derives from `QPalette` so it follows the desktop.

The **focused panel must be unmistakable** — use `border_focused` on the panel border plus a subtly lighter background. This is the single most important visual affordance in the app.

---

## 10. Desktop integration and command line

### 10.1 Invocation

```
pf [OPTIONS] [PATH...]
```

Paths may be files or directories, absolute or relative (resolved against the **client's** working directory, not the running instance's), and may be `file://` URIs so that `%U` in the `.desktop` file works.

### 10.2 Where a path opens

This is the behaviour that makes Panefile usable as a default file manager and from a shell alias. Given a single path argument:

| Situation | Result |
| --- | --- |
| No instance running | Start up, open the path in the initial panel |
| Instance running, its window **is** focused | Navigate the **currently focused panel** to the path |
| Instance running, its window is **not** focused (unfocused, minimised, other workspace) | Open the path in a **new panel**, focus that panel, then raise and activate the window |

With multiple paths, the **first** follows the table above and each subsequent one always opens a new panel, up to `panels.max_count`; beyond that, extra paths are dropped with a footer warning.

Notes:

- "Focused" means the running instance's own window is active — `QGuiApplication::applicationState() == Qt::ApplicationActive` (cross-check with `QWidget::isActiveWindow()` for the specific window). The running instance decides this; the client never tries to inspect focus, which isn't possible on Wayland anyway.
- The common case falls out correctly: launching from a terminal means the terminal is focused, so you get a new panel. Launching from a keybind while already in Panefile navigates where you're looking.
- Navigating the focused panel **pushes history**, so `go_back` returns to where the user was. It never discards their selection silently — clear the selection and say so in the footer.
- A path that is a **file**, not a directory, navigates to its parent and places the cursor on it. `cli.file_action` chooses what happens next: `select` (default), `quicklook`, or `launch` (open in the default app).
- A path that doesn't exist is an error: print to stderr and exit 2 without disturbing a running instance. If *some* of several paths are bad, open the good ones and report the rest.

Overrides, which win over the table above:

| Flag | Effect |
| --- | --- |
| `--here` | Force use of the focused panel even if the window isn't focused |
| `--panel` | Force a new panel even if the window is focused |
| `--new-window` | New window in the existing process |
| `--new-instance` | Separate process entirely; skips the socket |

### 10.3 Single instance and IPC

`QLocalServer` on `$XDG_RUNTIME_DIR/panefile-$UID.sock`. Disable with `single_instance = false`.

- **Client side is on the startup critical path** and comes first: attempt `connectToServer` with a 0 ms timeout. On success, send the request, wait for a short ack (50 ms cap), exit 0 — **without ever constructing a `MainWindow`**. This is by far the fastest path through the program and should complete in a couple of milliseconds.
- **Server side**: `listen()` is two syscalls, so bind it before `show()`; only the connection-handling wiring is deferred. If `listen()` fails because a stale socket exists (previous process killed), `connect` to it first — if that fails, unlink and retry once.
- If a socket doesn't exist yet because another instance is mid-startup (a genuine but rare race), the client retries once after 50 ms before giving up and starting its own instance. Two windows is an acceptable worst case; a hang is not.
- **Message format** is a single JSON object: `{ cwd, paths[], flags{}, activation_token, desktop_startup_id }`. Version it with a `v` field so a future change doesn't break against an older running instance — on version mismatch, the client starts its own instance rather than sending something the server might misread.

### 10.4 Raising the window on Wayland

A Wayland client cannot raise itself unprompted; it needs an activation token from the compositor, granted to the process that currently has focus.

- The launching process usually has `XDG_ACTIVATION_TOKEN` in its environment (set by the compositor, launcher or terminal). The client must **forward it in the IPC message and then unset it locally**, since a token is single-use.
- The running instance sets that value into its own environment with `qputenv("XDG_ACTIVATION_TOKEN", token)` immediately before calling `QWindow::requestActivate()`; Qt's Wayland platform plugin picks it up from there.
- If no token is available, degrade gracefully: update the window's state so the compositor shows an attention hint, and do **not** attempt to force focus. Never treat failure to raise as a fatal error — the panel is still created and will be there when the user switches to it.
- Verify this specifically on Hyprland, and note in `docs/` that some compositors require `follow_mouse`/focus settings that make activation a no-op.

### 10.5 Desktop entry and misc

- Install `data/panefile.desktop` with `MimeType=inode/directory;` and `Exec=pf %U`, so it can be set as the default file manager and receive folder-open requests from other applications.
- Set `QGuiApplication::setDesktopFileName("panefile")` so Wayland associates the window with the correct icon.
- Flags: `--version`, `--help`, `--config-dir`, `--print-default-config`, `--startup-trace`, `--verbose`, `--benchmark <dir>`.
- Argument parsing on the critical path is hand-rolled; `QCommandLineParser` is constructed only for `--help` and `--version`.
- Log to stderr with `QLoggingCategory` under `panefile.*` categories.

---

## 11. Performance targets

Measure these; they are acceptance criteria, not aspirations.

| Metric | Target |
| --- | --- |
| Cold start to first painted window | < 80 ms |
| Cold start to first directory listing visible | < 120 ms |
| Warm start (page cache hot) | < 50 ms |
| Forwarding a path to a running instance (client process lifetime) | < 10 ms |
| Directory with 1,000 entries: keypress → painted | < 30 ms |
| Directory with 100,000 entries: first batch visible | < 150 ms |
| Scrolling at 60 fps | no dropped frames in a 100k directory |
| Idle memory, 2 panels | < 80 MiB RSS |
| Cursor movement → Quick Look content shown (cached) | < 50 ms |
| Quick Look open → first paint, 4 MB text file | < 120 ms |
| Holding `j` through 200 files with Quick Look open | no queued backlog, no dropped frames |

Include a `--benchmark <dir>` hidden flag that scans a directory and prints timings, so regressions are measurable.

---

## 12. Error handling

- No operation failure may crash the app or leave the UI in an inconsistent state.
- Filesystem errors surface as human-readable text with the `errno` meaning translated (`EACCES` → "Permission denied"), the affected path, and where sensible a suggested action.
- Non-fatal job errors (one file of 500 failed) accumulate into a summary shown at job end, listing failures with reasons — the job continues.
- Guard against `PATH_MAX` and deeply nested recursion; use iterative traversal with an explicit stack, not recursion.

---

## 13. Security considerations

- Archive extraction must reject path traversal (§7.10).
- The shell prompt (`:`) executes via `/bin/sh -c` in the panel's cwd. Do not attempt to sanitise — it is an explicit shell — but never construct shell strings from filenames elsewhere in the app. Everywhere else, use `QProcess` with an argument list, never `startCommand`.
- Never pass a filename to `xdg-open` without verifying the file exists and resolving it to an absolute path.
- Treat `.desktop` files in the filesystem as data, not as things to execute.

---

## 14. Testing

`tests/` with `QTest`, registered via `add_test`. Required coverage:

**Unit (pure, fast):**
- `FuzzyMatcher`: scoring order, span correctness, empty/pathological inputs, Unicode.
- Natural sort ordering, including numbers, locale and case.
- Size and date formatting.
- `.trashinfo` generation and parsing round-trip, including URL encoding of odd filenames.
- Config parsing: valid, malformed, partial, unknown keys.
- Keymap parsing and conflict detection.
- Keymap resolution: multiple simultaneous bindings for one action; sequence matching (`g h`); prefix ambiguity (`g` bound *and* `g h` bound); timeout expiry; mistyped sequence swallows the key rather than leaking it; buffer cleared on focus change; `Meta`/`Super` alias normalisation.
- Archive path-traversal rejection (fixtures with `../` entries).

**Integration (temp-dir fixtures):**
- Scanner on a directory with symlinks, broken symlinks, permission-denied subdirs, 10,000 files.
- Copy/move/delete jobs including cross-device simulation, conflicts, cancellation mid-copy leaving no partial file.
- Watcher: create/delete/rename events produce correct model deltas.
- Bulk rename cycle detection.
- CLI routing: all three cases in §10.2 (not running / running+focused / running+unfocused), multiple paths, file-vs-directory arguments, relative path resolution against the client cwd, `--here`/`--panel` overrides, nonexistent paths, IPC version mismatch, stale socket recovery.
- Quick Look: renderer selection by MIME and priority; fallback to hex on renderer failure; debounce cancels superseded loads; content cache hit on mtime match and miss on mtime change; dock mode transitions preserve the tracked cursor item.

**GUI:**
- `QTest::keyClicks` driving navigation, panel create/close/cycle, selection mode.

Run under ASan and UBSan in CI. Treat any leak in a job or scanner as a bug.

---

## 15. Packaging

- CMake install of binary, `.desktop`, icons, themes, and a man page.
- `PKGBUILD` for the AUR, depending on `qt6-base`, `libarchive`, and optionally `ksyntax-highlighting`, `ffmpegthumbnailer`, `poppler-qt6`.
- CI: build on Arch and Ubuntu 24.04 with GCC 14 and Clang 18, run the test suite, run clang-tidy.
- `clang-format` (LLVM base, 4-space indent, 100 col) and `.clang-tidy` in the repo root, enforced in CI.

---

## 16. Implementation plan

Work through these in order. Each milestone must build, pass its tests, and be manually usable before starting the next. Commit at each milestone.

| # | Milestone | Definition of done |
| --- | --- | --- |
| M0 | Skeleton | CMake project builds and runs, empty `MainWindow`, CI green, clang-format configured, `--startup-trace` and the hyperfine CI baseline in place from day one |
| M1 | One panel | `DirectoryScanner`, `DirectoryModel`, `FilePanel`, `FileItemDelegate`. Can browse with `hjkl`. Icons and sorting work |
| M2 | Panels + input | `ActionRegistry`, `Keymap`, `FocusManager`, `PanelStrip`. Create/split/close/cycle panels. Sidebar with pinned dirs. Help modal generated from the registry |
| M3 | Config + theme | TOML loading, validation, hot reload. `StyleSheetBuilder`, bundled themes |
| M4 | File operations | `JobEngine`, copy/move/delete/rename/create, conflict modal, `ProcessBar`, trash, undo stack |
| M5 | Watching | inotify watcher with debounce and targeted model updates |
| M6 | Quick Look + thumbnails | Renderer interface and registry, all v1 renderers (§7.6), float and all dock modes, freedesktop thumbnail cache |
| M7 | Search | In-panel filter, `FuzzyMatcher`, recursive finder modal, bulk rename |
| M8 | Archives + mounts | libarchive create/extract with traversal guard, udisks2 sidebar integration |
| M9 | Drag and drop | Outgoing and incoming, with the Wayland caveats in §7.12 |
| M10 | Polish | Session restore, single instance and CLI routing (§10), `.desktop` install, benchmarks against §11, PKGBUILD |

Startup budget is checked at **every** milestone, not just M10. If a milestone pushes cold start past §11's target, the fix is to make that milestone's work lazy before moving on — never to defer the problem.

### Conventions for the implementer

- C++20. Prefer `std::filesystem` and standard containers in `fs/` and `model/`; Qt types at the UI boundary.
- No raw `new`/`delete` outside Qt parent-child ownership. `std::unique_ptr` elsewhere.
- Prefer the new-style `connect` syntax with member function pointers, always.
- No `QFileSystemModel`, no QML, no Qt5 compatibility shims, no `qDebug` left in committed code — use `QLoggingCategory`.
- Every new action goes in `ActionRegistry` with a description, or it won't appear in the help modal.
- Nothing new goes on the startup critical path (§3.4) without a measured justification. Default to lazy.
- Write the tests for a module in the same commit as the module.
