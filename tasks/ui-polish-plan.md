# UI polish, part 8 — review and plan

**Status: built.** Everything below is in the working tree, uncommitted; see "Not done" at the end.

Reference points: Review and Folio (`~/Developer/dotfiles/{review,folio}`), and
GNOME Files (Nautilus) 50. Screenshots of Panefile were taken through
`pf-screenshot` (offscreen) and of the real binary under Xvfb/xcb; Nautilus
under Xvfb in a sandboxed `$HOME`.

## What the comparison shows

Panefile's structure is sound. Parts 1–7 got the bones right: inset pill,
1px seams, banding only where focus is, quiet unfocused panels, no sliced rows,
filenames winning the row. What separates it from Review and Nautilus is not
layout but *finish*, and most of it comes from a short list of causes.

### 1. Type is too big and the wrong face on Linux

- `font_size = 13` is applied as **points** (`setPointSize`, QSS `pt`). On a
  Mac that is 13px; on Linux at 96 dpi it is ~17px. Review and Folio are 13px.
  This alone is why Linux Panefile looks blown-up next to them.
- `font_family = ""` resolves to fontconfig's `sans-serif`, which here is
  **Liberation Sans** (an Arial clone). Review sets Inter; Nautilus uses
  Adwaita Sans (Inter-derived). Both are installed.
- The footer is the only monospace text and reads as a terminal strip.

### 2. Only one level of secondary text

Review has three: `text`, `muted`, `faint`. Panefile has `text` and
`subtext`/`overlay`, which the macOS themes set to the same colour. So paths,
counts, section labels, timestamps and hints all land on one grey, and nothing
recedes.

### 3. Chrome has no icons, and the glyphs that exist are Unicode

- Sidebar: text only. Nautilus and Review put a symbolic icon on every place.
- Settings tabs: `◐ ⚙ ◱ ⌘` glyphs, which render in whatever font has them.
- Quick Look close button is `✕`.
- File icons on Linux come from the system theme when Qt finds one (it often
  does not without a platform theme), bypassing the tint, unfocused dimming and
  white-on-pill treatment. Without one, the bundled fallback set is used — 8
  icons drawn at 16px, adequate but plainer than the rest of the design.

### 4. Half the widget set is unstyled

No QSS for `QMenu`, `QComboBox`, `QCheckBox`, `QSpinBox`, `QPushButton` (outside
the notice bar), `QMessageBox`, `QHeaderView` on light themes, or the process
bar. In the settings window this is the most visible gap: black spinboxes,
`☒` checkboxes, a dark combo on a light page. The sort and drop menus are
native-looking popups in an otherwise themed app.

### 5. Modals wash out instead of dimming

The backdrop is `background` at alpha 180, which on a light theme fogs the app
white. No shadow on the card. Titles are a bold QFont in some modals and nothing
in others; `modalTitle`, `modalHint`, `modalProblem` have no rules.

### 6. Missing states

- No row hover (Nautilus and Review both have one, in the pill shape).
- No empty-folder state: an empty directory is just blank.
- Errors are a red QLabel with a `\n` in it.

### 7. Bugs that read as polish problems

- Scrollbar QSS is defined twice (`StyleSheetBuilder.cpp:238-282` and
  `:476-505`); the merge yields a ~3px thumb.
- The delegate keys "focused panel" off `QStyle::State_Active`, not
  `FilePanel::m_active`. With the window inactive, a modal open, or the filter
  focused, the focused panel loses its accent pill and banding and its names dim
  as if unfocused, while its header still says it is focused.
- Panel splitter is 6px transparent; the sidebar join stacks three hairlines.
- Sidebar and Quick Look palettes are not refreshed on a theme change, and the
  desktop light/dark follower does not reset the font.
- `make-screenshots.sh` uses macOS `mkfile`, so it fails on Linux.

## Plan

Each step is screenshotted in light and dark against the previous one before
moving on. Steps marked **(decision)** depend on the questions below.

### A. Foundations
- [x] Font size in pixels (`setPixelSize`, QSS `px`); settings suffix "px".
      Unchanged on macOS, ~25% smaller on Linux.
- [x] Default family: system UI font on macOS; on Linux prefer Inter, then
      Adwaita Sans, then fontconfig's default.
- [x] Three text levels: add `muted` and `faint` tokens (derived when a theme
      does not set them), and use them consistently: path parents, counts,
      timestamps, section labels, hints.
- [x] Footer in the UI font with tabular figures and `·` separators.
- [x] Remove the duplicate scrollbar block; one overlay scrollbar, 8px, rounded,
      widening on hover.
- [x] **(decision)** Palette.

### B. Panel
- [x] Delegate focus from the panel's own active flag, not `State_Active`.
- [x] Hover in the pill shape.
- [x] Header path: parents faint, current folder in `text` DemiBold; segments
      clickable to jump up.
- [x] Empty-folder state and error state: icon, heading, one muted line.
- [x] Splitter: 1px seam with an invisible 6px grab area; one line at the
      sidebar join.
- [x] **(decision)** Cursor/selection treatment.
- [x] **(decision)** Banding and row height.

### C. Sidebar
- [x] Vendored symbolic icon set (Heroicons outline, as Review/Folio), tinted
      through one `SymbolicIcon` helper that renders SVG at the device pixel
      ratio. Home, Desktop, Downloads, Documents, Pictures, Music, Videos,
      folder, drive, eject.
- [ ] ~~Section headers as Review's~~ — left as they are (faint, 11px, 600); the help modal's group headings got the small-caps treatment instead.
- [ ] **Not done:** current location highlighted with the same inset pill as rows
      (Nautilus does this; today nothing is shown unless focused).
- [x] Refresh palette on theme change.

### D. Widgets
- [x] QSS for buttons (ghost / subtle / primary, as Review's `Btn`), line
      edits with a focus ring, combo boxes, spin boxes, check boxes (accent fill,
      white check), menus (radius 10, padding 5, 30px items, separators inset),
      tooltips, header views, progress bar, process bar.
- [x] Settings window: icons instead of Unicode glyphs, a segmented-looking
      tab strip, consistent row rhythm.
- [x] Delete confirmation as a themed `Modal` instead of a `QMessageBox`.

### E. Modals and overlays
- [x] Scrim that dims (black at ~33% light / 60% dark), card radius 12, soft
      layered shadow, title 16 DemiBold, hints faint, keycaps in help.
- [x] Quick Look: icon close button, themed through QSS, refreshed on theme
      change.

### F. File icons — **(decision)**

### G. Finish
- [x] Fix `make-screenshots.sh` on Linux. (Site screenshots not regenerated — see below.)
- [x] Update `tst_appearance` / `tst_config` where a decision changes a locked
      behaviour; add tests for the focus-flag fix and the px sizing.
- [ ] `scripts/ci-local.sh` green.

## Decisions (Andy, 2026-10-05)

1. **Palette:** new `panefile-light` / `panefile-dark` themes in Review/Folio's
   warm neutrals become the follow-the-desktop default. macOS themes stay in the
   picker; `Ctrl+T` toggles between the two panefile themes.
2. **Cursor:** focused cursor is a soft accent tint (~15%) with normal text
   colour, as Nautilus. Unfocused cursor stays neutral.
3. **File icons:** bundled, redrawn set (~20 types) in one style, used on every
   platform so tinting and dimming always apply.
4. **Density:** no banding by default (still a theme option), 30px rows.

## Not done, and why

- **Sidebar current-location pill.** Commit 2228b9b deliberately removed any
  persistent sidebar highlight, and `tst_sidebar::openingAPlaceLeavesNoHighlight`
  locks it in. With several panels it is also ambiguous which panel's location
  it would show. Worth deciding rather than slipping in.
- **Site screenshots.** `docs/site/style.css` is coloured from the macOS
  themes, so regenerating the screenshots in Panefile Light/Dark would clash
  with the page around them. A site palette change belongs with that.
- **`data/icons/fallback/`** is no longer referenced (the bundled sets in
  `data/icons/files` and `data/icons/symbolic` replace it) and can be removed.
- **Menu shadows.** Popups are separate windows, so a shadow is the
  compositor's job; Hyprland draws none. The menu card has a stronger border
  instead.

## Asked for during the pass (Andy, 2026-10-05)

- Cursor tint is **blue**, not terracotta: it is derived from `border_focused`,
  which the Panefile themes set to Review's focus blue. Terracotta stays for
  buttons, checks and badges.
- `?` opens with its filter focused (Modal::initialFocusWidget()).
- **Wastebasket** in the sidebar under a divider.
- **Settings had no working key**: `,` and `Ctrl+,` were dropped at start-up
  because parseChord rejected every comma. Fixed, and `everyDefaultBindingParses`
  now guards the whole default table.
- **Chord hint**: press `g` and pause (450 ms) and a card lists what can
  follow; the sequence then waits for a key, Esc or a click.
- **Application menu**: a ☰ button at the top of the sidebar with the common
  actions and their keys.

## What changed, in one place

- Themes: `panefile-light` / `panefile-dark` (Review/Folio palette) are the
  follow-the-desktop default and the `Ctrl+T` pair; all themes 30px rows, no
  banding. New optional key `sidebar_bg`.
- Type: pixels, not points; Inter → Adwaita Sans → fontconfig default on Linux;
  unhinted. One `config::applicationFont()` for every caller.
- Stylesheet: rewritten around derived tokens (`fill`, `fill_strong`,
  `border_strong`, `hover`, `accent_soft`, three text levels); full coverage of
  buttons, inputs, combos, spins, menus, headers, progress, modals; one
  scrollbar block; tinted chevron SVGs generated per theme.
- `ui::PanefileStyle`: check boxes, radio buttons, menu ticks, chevrons, no
  focus rects, translucent menus/tooltips.
- Panels: focus from the panel, not the window (with a test that fails on the
  old logic); soft accent cursor; hover pill; rounded drop target and selection
  marker; crumb path with clickable parents; empty / error / no-match states
  drawn in the list with an icon; 1px splitter seams.
- Modals: dimming scrim, layered shadow, 12px card; `ConfirmModal` replaces
  `QMessageBox` for delete (Cancel holds focus); help modal keycaps; settings
  tab icons; Quick Look close icon and stylesheet-driven colours.
- Icons (agent): redrawn 20-kind file set and 23 Heroicons glyphs, compiled
  in, used on every platform; sidebar icons; `tst_icons`.
- Footer: UI face with only the permission string in mono, `·` separators;
  pending keys as a keycap.
