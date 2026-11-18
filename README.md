# Panefile

A keyboard-driven, multi-panel file manager. C++20 and Qt 6 Widgets.

Conventional file managers open a window or a tab per location and expect the
mouse. On a tiling compositor that is a poor fit: windows are a scarce,
compositor-managed resource, and reaching for the mouse breaks flow.

Panefile puts **N independent directory panels side by side in one window**.
Each panel keeps its own working directory, cursor, history and filter, and
any folder you sort differently remembers that order. You create, split, close and cycle panels from the keyboard, and
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

The sections are `[normal]`, `[selection]`, `[global]` and `[typing]`. Listing
an action in one replaces that action's bindings in that section only; an empty
list unbinds it there.

State — the session (`session.ini`), the sidebar's sections and entries
(`sidebar.ini`), window geometry — is written to `~/.local/share/panefile/`.

The sidebar is yours to arrange: drag any file or folder in, drag entries to
reorder them, drag a heading to move or remove a whole section. `Ctrl+K`
(`⌘K` on macOS) opens the command palette: search every action, with the ones
that fit the selected files first (convert images, rotate, convert video and
audio with `ffmpeg`), and jump to sidebar places. The palette's shortcut can be
changed under Settings → Keys.

### Keys

Plain letters are not bound by default, so that typing in a panel can find a
file: the cursor moves to the first entry whose name starts with what you have
typed, ignoring case and leading dots, and pressing the same letter again steps
through the entries that start with it. The typed name lasts
`type_ahead_timeout_ms` between keys (default 1000, in the `[keys]` table of
`hotkeys.toml`). The only plain keys bound are `.` (hidden files) and `Space`
(Quick Look); everything else takes `Ctrl` (`⌘` on macOS) or `Alt`, or is a key
that types nothing, such as an arrow or `F1`, which lists every binding.

To get vim-style movement back:

```toml
[normal]
list_down        = ["j"]
list_up          = ["k"]
parent_directory = ["h"]
confirm          = ["l"]
```

The arrow keys, `Enter` and `Backspace` keep working, because their defaults
live in `[global]`. A letter bound this way runs its action instead of starting
a typed name, so `j`, `k`, `h` and `l` can no longer begin a search for a file.

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

## System Open and Save dialogs

On Linux Panefile can be the Open and Save dialog for other applications. They
ask the desktop portal (xdg-desktop-portal), which passes the request to
whichever FileChooser backend `portals.conf` names; Panefile is one. Switch it
on in Settings → General → **Open and Save dialogs**, or:

```sh
pf --make-file-chooser      # on; restarts xdg-desktop-portal, no login needed
pf --restore-file-chooser   # back to what it was
pf --default-status         # where it stands
```

It writes `~/.local/share/xdg-desktop-portal/portals/panefile.portal`, a D-Bus
service file for `pf --portal`, and one line in the `portals.conf` that is in
effect: `org.freedesktop.impl.portal.FileChooser=panefile;` followed by the old
value as the fallback. A system `portals.conf` (say `hyprland-portals.conf`) is
copied into `~/.config/xdg-desktop-portal/` under the same name first, because
the portal reads only the first file it finds; every other line is untouched,
so screen sharing keeps working. `--restore-file-chooser` puts the old line
back and deletes the copy again when nothing else in it changed. Restarting the portal pauses a running screen
share for a moment.

Each dialog is a Panefile window with one list, your keys, themes and Quick
Look, and a bar underneath: the name (Save), the application's own options, the
file type, **Cancel** and **Open** / **Save**. Ctrl+L focuses the name, Tab
moves between the name and the list, Esc cancels, and Save asks before
replacing a file and is disabled where it can't succeed. The window's
application id is `panefile-chooser`, so a Hyprland rule can float it:

```
windowrule = float on, match:class ^(panefile-chooser)$
windowrule = size 1100 700, match:class ^(panefile-chooser)$
```

Which applications use it: Chromium, Electron, Firefox (set
`widget.use-xdg-desktop-portal.file-picker` to `1` in `about:config`) and
Flatpaks already ask the portal. Native GTK and Qt applications keep their own
dialog unless their environment says otherwise:

| Toolkit | Environment |
| --- | --- |
| GTK 3 | `GTK_USE_PORTAL=1` |
| GTK 4 | `GDK_DEBUG=portals` |
| Qt | `QT_QPA_PLATFORMTHEME=xdgdesktopportal` |

Only local files are returned, and a file-type filter given as a MIME type is
matched by name, so a file with no extension is hidden by it. The dialog opens
at the folder the application suggests, then the last one used for it, then
home. Not available on macOS.

## Licence

MIT. See `LICENSE`.
