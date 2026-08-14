# Settings window — plan, and what was built

**Built.** The window, all four tabs, the TOML writer, `follow_system`, and both
key bindings. What is deliberately not built is noted at the end.

This document was the design; it is kept because the reasoning is still the
reason the code looks the way it does.

## Why

Installing Panefile and then changing anything about it currently requires
knowing three things nobody is told: that configuration lives in
`~/Library/Application Support/panefile/` (or `$XDG_CONFIG_HOME/panefile/`),
that the files are called `config.toml`, `theme.toml` and `hotkeys.toml`, and
that none of them exist until you create them. There is no command to list the
bundled themes, no picker, and no way to discover that `nord` is a valid value
for a key you have not been told the name of.

That is a poor first hour for an application whose whole pitch is that it is
configurable, and it is the reason `Ctrl+T` existed as a binding for months
without an implementation: nothing else in the interface admitted that themes
were a thing.

## What it is not

Not a replacement for the files. The files stay authoritative and hand-editable,
and anyone who prefers them should never notice this exists. The dialog is a
second way in, not a new source of truth — which is why every decision below is
about round-tripping rather than about owning state.

## Shape

One modal, reached from either `,` or `Ctrl+,` — both, as decided below — and
from the help modal. Four sections down the
left, content on the right, in the same modal frame the other dialogs use so it
inherits the theme without new styling.

| Tab | Reads / writes | Controls |
| --- | --- | --- |
| General | `[general]`, `[panels]` in `config.toml` | New panel path, restore session, confirm on quit, single instance, panel count and max, directories first, default sort, show hidden |
| Appearance | `theme.toml`, `[ui]` overrides | Theme list with live swatches, follow-the-desktop toggle, font size, row height |
| Quick Look | `[quicklook]` | Dock position, float size, max read bytes |
| Keys | `hotkeys.toml` | Every action, its bindings, and a recorder for changing them |

## The three hard parts

**1. Writing TOML without destroying the file.** `toml++` parses to a mutable
table and can serialise it back, but a naive round-trip discards comments — and
the default config is mostly comments explaining what each key does. Losing them
the first time someone opens the dialog would be a straight downgrade.

The approach: never serialise the whole document. Read the file as text, locate
the key's line by its parsed source position (toml++ records line and column),
and splice in the new value. A key that does not exist yet is appended to its
section, or the section is appended if it is missing too. Comments, ordering and
whitespace all survive because nothing else is touched.

Falls back to writing a minimal file when the existing one cannot be parsed —
but only after saying so, since silently replacing a file someone has been
editing is exactly the failure this is trying to avoid.

**2. Applying without restarting.** `Application::applyTheme` already exists and
does the whole job for appearance — palette, stylesheet, font, delegate repaint
— so Appearance is nearly free. General and Quick Look need
`setSettings()` on the controllers that already take one. Keys need
`installDefaultKeymap` plus the user's overlay re-applied, which
`ConfigWatcher` already does when the file changes on disk.

That last point is the shortcut worth taking: **write the file, and let
`ConfigWatcher` apply it.** The dialog then has exactly one job — editing text —
and every code path that applies configuration stays the one that is already
tested. It also means hand-editing and the dialog behave identically, because
they are the same path.

**3. The keys tab.** A chord recorder is the fiddly part: it has to capture
key presses without the dispatcher acting on them, show `⌘⇧P` in the platform's
own notation (`chordToString` already does this), detect that a chord is already
bound elsewhere (`Keymap::conflicts()` already reports this), and offer to
rebind rather than silently shadow. It is the section most likely to be cut from
a first version.

## Order

0. **The window itself** — toolbar, tabs, and an empty content area. It is the
   part with a look to get right, and everything else drops into it.
1. **Appearance first**, with the theme list. It is the section people actually
   want, `applyTheme` already exists, and it proves the splice-writer against a
   file with one key in it.
3. **General and Quick Look.** Plain controls over keys that already round-trip
   through the parser, and the first real test of preserving comments.
4. **Keys.** Read-only first — the whole table, searchable, which is most of the
   value and none of the risk. The recorder after that.

## What was built

All of steps 0–4, except the chord recorder. The Keys tab lists every registered
action with its bindings in the platform's own notation and a search box, and
says to edit hotkeys.toml to change them.

Two things worth recording because they were not obvious:

**Opening the window must write nothing.** Every control writes when it changes,
and populating them *is* changing them — so without a guard, merely looking at
the settings would author a fully-populated config.toml for someone who had
never configured anything, and would flatten the carefully partial file of
someone who had. `tst_settings` asserts that opening the window creates neither
file.

**`follow_system` had to become real.** The plan proposed it and it is now a key
the theme loader honours, ahead of any `name` left in the file. The `[ui]` block
still applies over the top, so following the desktop does not mean giving up a
personal row height.

## Still to build

- The chord recorder in the Keys tab, which is the fiddly part described above.
- Thumbnails, search and operations settings, which have no tab yet. They are
  the least-asked-for third of §8.1 and the window has room for a fifth tab.

## What needs deciding

- ~~**`,` or `Ctrl+,`?**~~ **Decided: both.** `Ctrl+,` in the Global layer,
  which Qt maps to `⌘,` on macOS and matches every other desktop application,
  and a bare `,` in the Normal layer, which matches how the rest of Panefile's
  single-key bindings work. §6.3 spends neither, so there is no conflict.
- ~~**Does the dialog own "follow the desktop"?**~~ **Decided: an explicit
  `follow_system` key.** The current behaviour is implicit — no `theme.toml`
  means follow, any `theme.toml` means don't — which is invisible and means the
  only way to go back to following is to delete a file. `follow_system = true`
  makes the toggle a checkbox and makes `toggle_theme_dark_light` clear it
  rather than relying on the file's existence.
- ~~**Live preview or apply-on-close?**~~ **Decided: live for Appearance,
  apply-on-close for the rest.** A theme is judged by looking at it, so it has
  to change under the cursor; a panel count changing while the mouse is over a
  spinbox is just alarming.
