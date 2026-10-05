# Panefile

A keyboard-driven, multi-panel file manager. C++20 and Qt 6 Widgets.

Conventional file managers open a window or a tab per location and expect the
mouse. On a tiling compositor that is a poor fit: windows are a scarce,
compositor-managed resource, and reaching for the mouse breaks flow.

Panefile puts **N independent directory panels side by side in one window**.
Each panel keeps its own working directory, cursor, history and filter, and
any folder you sort differently remembers that order. You create, split, close and cycle panels with single keystrokes, and
copy between them without ever opening a second window.

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
└────────────────────────────────────────────────────────────┘
```

**Status: under construction.** See `tasks/todo.md` for what works today.

## Design goals

- **Every action reachable from the keyboard.** The mouse is supported, never required.
- **Independent panels, not Miller columns.** Panels are peers; none is the parent of another.
- **Fast on real directories.** A 100,000-entry directory opens and scrolls without stutter.
- **Instant to launch.** Panefile is launched constantly, often from a keybind. Anything
  perceivable as a delay defeats the point, so startup latency is an acceptance criterion
  and everything that isn't needed to draw one panel is deferred or lazy.
- **Never block the UI.** All I/O — stat, read, copy, hash, thumbnail — happens off the GUI thread.
- **Fully configurable.** Every keybinding remappable, every colour themeable, in plain TOML.

## Platforms

Linux is the primary target, developed against Wayland. macOS is supported as a
first-class second platform rather than a development convenience: it runs the
same test suite in CI, and each OS-specific concern has a real implementation on
both sides rather than a stub.

| Concern | Linux | macOS |
| --- | --- | --- |
| Directory watching | inotify | FSEvents |
| Mounted volumes | `/proc/self/mountinfo` | `getmntinfo(3)` |
| Removable media | udisks2 over D-Bus | DiskArbitration |
| Copy acceleration | `copy_file_range`, `FICLONE` | `clonefile`, `fcopyfile` |
| Trash | XDG Trash spec | `NSFileManager` trash |
| Config | `~/.config/panefile/` | `~/Library/Application Support/panefile/` |

Windows is not supported. X11 is best-effort: it will probably work, it is not tested.

## Building

Requires CMake 3.25+, Ninja, a C++20 compiler and Qt 6.7+.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

`--preset release` builds optimised with LTO; `--preset asan` adds Address and
UndefinedBehavior sanitizers.

Optional dependencies — `KSyntaxHighlighting`, QtMultimedia, QtPdf,
`poppler-qt6`, `libffmpegthumbnailer` — are never linked into the binary. Each
becomes a plugin opened on first use, so a missing one degrades gracefully at
runtime and a present one costs nothing at launch. The configure summary lists
which you are getting.

## Installing

Arch (AUR):

```sh
yay -S panefile-git
```

macOS:

```sh
brew install andyjeffries/tap/panefile
```

## Configuration

```sh
mkdir -p "$(pf --config-dir)"
pf --print-default-config > "$(pf --config-dir)/config.toml"
```

| File | Contents |
| --- | --- |
| `config.toml` | Behaviour settings |
| `hotkeys.toml` | Action id → key sequences |
| `theme.toml` | Active theme, or inline colour overrides |
| `themes/*.toml` | User themes |

Every action may have any number of bindings, all active at once, and a binding
containing spaces is a sequence:

```toml
[normal]
copy_items = ["Ctrl+C", "Super+C", "y y"]
go_home    = ["g h", "Alt+Home"]
```

State — pinned directories, session, window geometry — is written to
`~/.local/share/panefile/`.

## Default file manager

On Linux, Panefile offers once per launch to become the default file manager,
in a bar along the top of the window: **Yes**, **Not now** (ask again next
launch) or **Never** (sets `offer_default_file_manager = false` in
`config.toml`). The keys are `Alt+Y`, `Alt+N` and `Alt+Shift+N`, remappable
like any other action. Settings → General has the same switch and a **Make
default** button, and `pf --make-default` does it from a shell.

"Default file manager" is two separate things on Linux, and Yes sets up both:

- **Opening folders** — `xdg-open ~`, GIO, the file chooser portal. This is
  `inode/directory=panefile.desktop` under `[Default Applications]` in
  `mimeapps.list`. Panefile edits whichever `mimeapps.list` currently decides it
  (usually `~/.config/mimeapps.list`, or a desktop-specific one such as
  `~/.config/hyprland-mimeapps.list`), changes only that line and one in
  `[Added Associations]`, and writes through a symlink rather than replacing it
  — so a `mimeapps.list` managed by stow or another dotfiles tool stays managed.
- **"Show in folder"** — a browser revealing a download, an Electron app
  revealing a file. These call the `org.freedesktop.FileManager1` D-Bus
  service, which Nautilus usually owns. Panefile writes
  `~/.local/share/dbus-1/services/org.freedesktop.FileManager1.service`, which
  the session bus prefers over Nautilus's copy, so the bus starts `pf
  --dbus-service` when nothing else holds the name. If Nautilus is already
  running and holding it, Panefile waits its turn: "Show in folder" reaches
  Panefile once Nautilus exits, or after your next login.

`pf --default-status` shows where both stand. To undo:

```sh
xdg-mime default org.gnome.Nautilus.desktop inode/directory   # or edit mimeapps.list by hand
rm ~/.local/share/dbus-1/services/org.freedesktop.FileManager1.service
```

(`xdg-mime` replaces a symlinked `mimeapps.list` with a plain file; edit the
line yourself if yours is a symlink.)

None of this applies on macOS, where Finder cannot be replaced.

## Licence

MIT. See `LICENSE`.
