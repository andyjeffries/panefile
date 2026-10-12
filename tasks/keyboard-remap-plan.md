# Plan — free the letter keys for type-to-select

## Goal

Typing in a panel moves the cursor to the first entry whose name starts with
what was typed. For that to work, no default binding may consume a key that
produces text, with two deliberate exceptions: `.` (toggle hidden files) and
`Space` (Quick Look) — and both give way while a type-ahead is in progress.

The rule, stated once: **a default binding's first chord must carry Ctrl/Super
(or Alt), or be a named non-text key** (arrows, Return, Tab, Escape, Delete,
Backspace, Home/End, PgUp/PgDown, F-keys). Shift alone does not count, because
Shift+letter produces text. Allowed exceptions: `.` and `Space`.

`Ctrl` below is Qt's `Ctrl`, i.e. ⌘ on macOS.

## Remapping table

### Dropped — an equivalent already exists

| Old | Action | Still reachable by |
|---|---|---|
| `l` | confirm | Return, Right |
| `q` | quit | Ctrl+Q |
| `w` | close_file_panel | Ctrl+W |
| `,` | open_settings | Ctrl+, |
| `j` / `k` | list_down / list_up | Down / Up |
| `h` | parent_directory | Left, Backspace |
| `L` / `H` | next / previous_file_panel | Tab / Shift+Tab (+ new Ctrl+PgDown / Ctrl+PgUp) |
| `D` | permanently_delete_items | Shift+Delete |
| `\` | clear_filter | Escape (still in the palette) |
| Ctrl+T | toggle_theme_dark_light | hamburger menu and palette only; Ctrl+T goes to new panel |
| Selection `j` `k` `J` `K` | select_down / select_up | Down, Up, Shift+Down, Shift+Up |

### Moved to the Ctrl+G leader ("go to")

| Old | New | Action |
|---|---|---|
| `g g` | Ctrl+G g (+ Home) | list_top |
| `G` | Ctrl+G G (+ End) | list_bottom |
| `g h` | Ctrl+G h | go_home (Alt+Home kept) |
| `g r` | Ctrl+G r | go_root |
| `g c` | Ctrl+G c | go_config |
| `g t`, `g w` | Ctrl+G t, Ctrl+G w | go_trash |
| `g p` | Ctrl+G p | go_previous |
| `s` | Ctrl+G s | focus_on_sidebar |
| `p` | Ctrl+G j ("jobs") | focus_on_process_bar |

Home/End are bound explicitly so top/bottom have a one-key form too.

### Moved to direct chords

| Old | New | Action | Reasoning |
|---|---|---|---|
| `n` | Ctrl+T | create_new_file_panel | panels behave like tabs |
| `N` | Ctrl+\ | split_file_panel | VS Code's "split editor" |
| — | Ctrl+PgDown / Ctrl+PgUp | next / previous_file_panel | tab convention, added |
| `F` | Ctrl+J | toggle_footer | VS Code's "toggle bottom panel" |
| `o` | Ctrl+O | open_sort_options_menu | "order"; Return already opens |
| `R` | Ctrl+Shift+O | toggle_reverse_sort | also in the sort menu |
| `A` | **Ctrl+A** | select_all | the universal binding |
| (Ctrl+A) | Ctrl+Shift+A | compress_file | evicted from Ctrl+A ("archive") |
| `v` | Ctrl+Shift+V | change_panel_mode | "visual" mnemonic kept |
| `P` | Ctrl+Shift+D | pinned_directory | browsers/Nautilus bookmark on Ctrl+D |
| `c` | Ctrl+Shift+C | copy_present_working_directory | pairs with Ctrl+P (copy item path) |
| `u` | Ctrl+Shift+E | unmount_device | Finder ejects on ⌘E; Ctrl+E is extract |
| `e` | F4 | open_file_with_editor | Midnight/Total Commander convention |
| `E` | Shift+F4 | open_current_directory_with_editor | |
| `T` | Ctrl+Shift+T | open_terminal_here | |
| `/` | Ctrl+/ | search_bar (filter) | same key, now chorded |
| `?` | F1, Ctrl+? | open_help_menu | GTK's shortcuts-window binding |
| `>` | Ctrl+Shift+P | open_panefile_prompt | command-palette convention |
| `:` | Ctrl+Shift+R | open_command_line | "run" |

### Kept as plain keys

| Key | Action | Exception |
|---|---|---|
| `.` | toggle_dot_file | appends to the type-ahead buffer while one is live |
| `Space` | quick_look | appends to the type-ahead buffer while one is live |

## Type-to-select

1. `PanelView` overrides `QAbstractItemView::keyboardSearch()` with its own
   buffer and single-shot timer, rather than Qt's 400 ms default.
2. Matching is a case-insensitive prefix match on the file name with its
   leading dots stripped, walked in proxy (display) order, wrapping. Hidden
   entries are only candidates when they are shown, since the proxy filters
   them out otherwise. A buffer never starts with `.`, because `.` with no
   buffer toggles hidden files.
3. Repeating the same single character cycles through matches (Qt's own
   behaviour, kept). No match leaves the cursor where it is.
4. While the buffer is live: `Space` and `.` append, `Backspace` deletes the
   last character (instead of going to the parent), and `Escape` clears the
   buffer (instead of cancelling).
5. Routing: `Application::notify` already treats a focused `QLineEdit` as
   "typing". Extend that to "the focused `PanelView` has a live type-ahead"
   for unmodified text keys, Backspace and Escape, so the dispatcher never
   sees them. Everything else (arrows, Ctrl chords) still goes to the
   dispatcher and clears the buffer.
6. In Selection mode, type-ahead moves the cursor without extending the
   selection, as Finder does.
7. The buffer is shown in the footer where the pending chord (`Ctrl+G-`)
   already appears.
8. Add `type_ahead_timeout_ms` (default 1000) next to the existing sequence
   timeouts in `config.toml`.
9. The sidebar gets Qt's built-in type-ahead with no extra work once letters
   stop being swallowed.

## Ctrl+G leader details

- The dispatcher already handles multi-chord bindings and the ChordHint
  popup; it will show `Ctrl+G-` and list the continuations.
- Also accept the second key with Ctrl still held (`Ctrl+G Ctrl+H`). The
  smallest way to do this is a retry inside `KeyDispatcher::handleKeyPress`:
  on NoMatch with a pending buffer, strip Ctrl and look the key up again.
  Without it, a held Ctrl silently swallows the sequence.

## Quick Look renderer keys

`QuickLookController::handleKey` runs before the dispatcher whenever Quick
Look is open, so today's plain renderer keys would beat type-ahead. They move
to Alt, the same whether Quick Look is docked or fullscreen:

| Old | New | Renderer |
|---|---|---|
| `+` / `=` | Alt+= | image, PDF zoom in |
| `-` | Alt+- | image, PDF zoom out |
| `0` | Alt+0 | image, PDF reset zoom |
| `w` | Alt+W | text wrap |
| `p` | Alt+P | media play/pause |

Ctrl was considered and rejected: Ctrl+= is equalise_panels, Ctrl+W closes a
panel and Ctrl+P copies a path, so only some of the keys could have used it.

## Work items

- [x] `src/input/DefaultKeymap.cpp`: rewrite the table above; fix the comments
      that describe superfile-heritage bindings
- [x] `KeyDispatcher`: Ctrl-held retry for leader continuations
- [x] `PanelView` / `FilePanel`: type-ahead buffer, dot stripping, timer,
      footer text, Selection-mode behaviour
- [x] `Application::notify`: route keys to a live type-ahead before the dispatcher
- [x] Quick Look renderers: Alt+key, gated once in `QuickLookView::handleKey`,
      which strips the Alt so the renderers and plugins are unchanged
- [ ] Check on macOS that Option+letter still reports the letter's `key()`
- [x] Config: `type_ahead_timeout_ms` in `[keys]`, read from config.toml and
      hotkeys.toml, applied as `QApplication::keyboardInputInterval` so the
      sidebar's built-in type-ahead agrees
- [x] `Ctrl+?`, `Ctrl+/`, `Ctrl+\`: key-form chords now drop Shift on
      punctuation keys, so `Ctrl+?` matches a real Ctrl+Shift+/ press
- [x] Tests (all 41 suites pass)
- [x] Docs: IMPLEMENTATION.md §6.3 tables, README, `docs/site/index.html`,
      `data/pf.1.in`
- [ ] Manual check in the running app on Linux; macOS ⌘ variants via CI

### Added during implementation (requested mid-way)

- [x] Clicking in a panel makes it the active panel (`PanelStrip` watches each
      list's FocusIn)
- [x] Shift+Up/Down extend the selection outside Selection mode too, anchored
      at the row the cursor leaves; a plain arrow, page, Home/End or typed name
      afterwards ends the selection (Selection mode keeps it)

## Decisions

1. Ctrl+T opens a new panel. The theme toggle has no default binding.
2. Ctrl+A is select_all, and compress moves to Ctrl+Shift+A.
3. Quick Look renderer keys are Alt+key, whether Quick Look is docked or
   fullscreen.

## Review

- Baseline on clean `main`: `tst_scanner` and `tst_search` failed once and
  passed on every later run — flaky, unrelated to this change.
- `scripts/ci-local.sh tidy` fails locally on any tree: the compile database
  carries Qt's GCC-only `-mno-direct-extern-access`, which clang-tidy rejects.
  Run against a copy with the flag stripped, the changed files are clean.
- Fixed afterwards: text-editing chords (QKeySequence's SelectAll, Copy, Cut,
  Paste, Undo, Redo and the word/line movement and deletion keys) now go to a
  focused text input instead of the keymap (`src/app/TextInput.*`). Spin boxes
  count as text inputs too, so typing `.` in bulk rename's start number no
  longer toggles hidden files.
