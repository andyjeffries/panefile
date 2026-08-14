#include "config/StyleSheetBuilder.h"

#include "config/Theme.h"

namespace pf::config {
namespace {

QString hex(const QColor &colour)
{
    return colour.name(QColor::HexRgb);
}

/// A colour a little lighter or darker than its base, whichever direction moves
/// it away from the background.
///
/// Hard-coded lighter() would produce an invisible hover on a light theme and a
/// washed-out one on a dark theme. Deriving the direction from the theme means
/// a shade reads the same way in both.
QString shade(const QColor &colour, bool towardsLight, int percent)
{
    return hex(towardsLight ? colour.lighter(100 + percent) : colour.darker(100 + percent));
}

} // namespace

QString buildStyleSheet(const Theme &theme)
{
    const bool light = theme.isLight();
    const QString hover = shade(theme.surface, !light, 12);
    const QString scrollHandle = shade(theme.background, !light, 40);

    // Widget selectors rather than object names wherever possible, so a widget
    // added later is styled without anyone remembering to add a rule for it.
    // Object names are used only where two instances of the same class need to
    // look different.
    return QStringLiteral(R"(
/* Generated from the active theme. Do not edit — change theme.toml instead. */

QWidget {
    background-color: %{background};
    color: %{text};
    font-size: %{font_size}pt;
}

QToolTip {
    background-color: %{surface};
    color: %{text};
    border: 1px solid %{border};
    padding: 4px 6px;
}

/* Panels ---------------------------------------------------------------- */

/* §9: "The focused panel must be unmistakable — use border_focused on the
   panel border plus a subtly lighter background. This is the single most
   important visual affordance in the app." */
/* A 2px accent edge along the top, not a box around the whole panel.
   A rectangle of colour drawn around a pane is the least native-looking thing
   the window can do, and it competes with the cursor pill for the same job.
   The edge is always present and merely transparent when the panel is not
   focused, so nothing reflows as focus moves. Panels butt together against a
   hairline seam instead of floating as rounded cards over a backdrop. */
QWidget#filePanel {
    background-color: %{background};
    border: none;
    border-left: 1px solid %{seam};
    border-top: 2px solid transparent;
    border-radius: 0px;
}

QWidget#filePanel[panelActive="true"] {
    border-top: 2px solid %{border_focused};
    background-color: %{panel_active_bg};
}

/* A hairline under the header rather than a filled bar: the path and the count
   are labels on the list, not a toolbar above it. */
/* The margins are set on the layout in FilePanel, because padding here would
   style this widget's own painting without insetting the labels inside it. */
QWidget#panelHeaderRow {
    background-color: transparent;
    border-bottom: 1px solid %{header_rule};
}

QLabel#panelHeader {
    background-color: transparent;
    color: %{overlay};
    font-size: %{chrome_font_size}pt;
    font-weight: 600;
}

/* Near-black in the focused panel against a mid grey in the other. Along with
   the accent edge and the filled cursor pill, that is the third place the same
   answer is given to "which pane am I in". */
QLabel#panelHeader[panelActive="true"] {
    color: %{text};
}

QLabel#panelHeaderCount[panelActive="true"] {
    color: %{subtext};
}

/* The count is a step quieter than the path, and both are a step quieter in a
   panel that is not focused — which is a second reading of the same signal the
   accent edge gives, for anyone whose eye is on the list rather than its top. */
QLabel#panelHeaderCount {
    background-color: transparent;
    color: %{overlay};
    font-size: %{small_font_size}pt;
}

QWidget#filePanel[panelActive="true"] QLabel#panelHeader {
    color: %{text};
}

QWidget#filePanel[panelActive="true"] QLabel#panelHeaderCount {
    color: %{subtext};
}

QLabel#panelStatus {
    color: %{error};
    padding: %{half_padding}px %{padding}px;
}

QListView#panelView {
    background-color: transparent;
    border: none;
    outline: none;
}

/* Sidebar --------------------------------------------------------------- */

/* A step off the content and a seam against it. Finder's chrome feels layered
   because the sidebar is a different surface; at the same white as the list it
   is just an indented column of words. */
QWidget#sidebar {
    background-color: %{sidebar_bg};
    border: none;
    border-right: 1px solid %{seam};
}

/* Transparent, or it paints the *content* background over the sidebar: the
   catch-all QWidget rule gives every widget the window's background colour, and
   a label that does not opt out of it stamps a lighter block behind its own
   text. That is what put a paler strip across the top of the sidebar. */
QLabel#sidebarSection {
    background-color: transparent;
    color: %{overlay};
    font-size: %{small_font_size}pt;
    font-weight: 600;
    padding: 12px 14px 6px 14px;
}

QListWidget#sidebarList {
    background-color: %{sidebar_bg};
    border: none;
    outline: none;
    padding: 0px 8px 8px 8px;
}

/* Inset from the sidebar's edges so the selection reads as a pill on a surface
   rather than a band running edge to edge. */
/* 26px rows inset from the sidebar's edges, so the selection reads as a pill on
   a surface rather than a band running from one edge to the other. */
QListWidget#sidebarList::item {
    color: %{text};
    font-size: %{chrome_font_size}pt;
    padding: 4px 10px;
    min-height: 22px;
    border-radius: %{small_radius}px;
}

QListWidget#sidebarList::item:hover {
    background-color: %{hover};
}

/* The sidebar's entries are shortcuts — press one and a panel goes there — not
   a state, so nothing here is highlighted unless the sidebar has focus, where
   the highlight means "this is the one Enter will open". That is arranged in
   Sidebar rather than here: Qt's stylesheet grammar does not reliably combine a
   widget pseudo-state with a sub-control one, and `:focus::item:selected`
   quietly painted the whole list instead of a row. */
QListWidget#sidebarList::item:selected {
    background-color: %{selection_bg};
    color: %{text};
}

/* Status furniture ------------------------------------------------------ */

/* A hairline along the top, so the status bar belongs to the window rather than
   hanging off the bottom of it. */
QWidget#footerRow {
    background-color: %{sidebar_bg};
    border-top: 1px solid %{seam};
    min-height: 26px;
}

/* The one place monospace survives. The permissions, owner, size and date are
   fixed-shape facts read by column, and a proportional face makes drwxr-xr-x
   harder to scan; everything else in the window is the system UI face, which is
   what stops the application reading as a terminal utility. */
QLabel#footer {
    background-color: transparent;
    color: %{subtext};
    font-family: %{mono_family};
    font-size: %{small_font_size}pt;
}

QLabel#selectionCount {
    background-color: transparent;
    color: %{subtext};
    font-size: %{small_font_size}pt;
}

QLabel#pendingKeys {
    background-color: transparent;
    color: %{accent};
    font-size: %{small_font_size}pt;
    font-weight: bold;
}

/* An overlay scrollbar, not a widget with a track. A thick bar with a visible
   groove is a Motif-era affordance; macOS shows a thin thumb over the content
   and nothing else. */
/* The square where a vertical and a horizontal scrollbar would meet. With the
   list body inset, that square is inside the panel and was drawing itself as a
   small bordered box in the bottom corner — an empty widget the user cannot
   interact with, which is exactly the kind of thing that reads as unfinished. */
QAbstractScrollArea::corner {
    background: transparent;
    border: none;
}

QScrollBar:vertical {
    background: transparent;
    border: none;
    width: 11px;
    margin: 0px;
}

QScrollBar::handle:vertical {
    background: %{scroll_handle};
    border-radius: 3px;
    min-height: 28px;
    margin: 2px 4px 2px 3px;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: transparent;
    border: none;
    height: 0px;
}

QScrollBar:horizontal {
    background: transparent;
    border: none;
    height: 11px;
}

QScrollBar::handle:horizontal {
    background: %{scroll_handle};
    border-radius: 3px;
    min-width: 28px;
    margin: 3px 2px 4px 2px;
}

QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
    background: transparent;
    border: none;
    width: 0px;
}

/* Modals ---------------------------------------------------------------- */

QWidget#modalContent {
    background-color: %{surface};
    border: 1px solid %{border};
    border-radius: %{radius}px;
}

QWidget#modalContent QLabel {
    background-color: transparent;
    color: %{text};
}

QLineEdit {
    background-color: %{background};
    color: %{text};
    border: 1px solid %{border};
    border-radius: %{small_radius}px;
    padding: 4px 6px;
    selection-background-color: %{accent};
    selection-color: %{background};
}

QLineEdit:focus {
    border: 1px solid %{border_focused};
}

QTreeWidget, QTreeView {
    background-color: %{background};
    alternate-background-color: %{surface};
    border: 1px solid %{border};
    border-radius: %{small_radius}px;
    outline: none;
}

QHeaderView::section {
    background-color: %{surface};
    color: %{subtext};
    border: none;
    border-bottom: 1px solid %{border};
    padding: 4px 6px;
}

QPushButton {
    background-color: %{surface};
    color: %{text};
    border: 1px solid %{border};
    border-radius: %{small_radius}px;
    padding: 5px 12px;
}

QPushButton:hover {
    background-color: %{hover};
}

QPushButton:default {
    border: 1px solid %{border_focused};
}

/* Splitters and scrollbars ---------------------------------------------- */

QSplitter::handle {
    background-color: %{border};
}

QSplitter::handle:hover {
    background-color: %{border_focused};
}

/* Except between panels, which draw their own full borders — a line there is a
   third edge between two that are already there, and reads as a seam. The
   handle keeps its width so it can still be dragged; it simply does not paint.
   Hover still shows, because a grab target you cannot see is worse than a
   line. */
QSplitter#panelSplitter::handle {
    background-color: transparent;
}

QSplitter#panelSplitter::handle:hover {
    background-color: %{border_focused};
}

/* The square where a vertical and a horizontal scrollbar would meet. With the
   list body inset, that square is inside the panel and was drawing itself as a
   small bordered box in the bottom corner — an empty widget the user cannot
   interact with, which is exactly the kind of thing that reads as unfinished. */
QAbstractScrollArea::corner {
    background: transparent;
    border: none;
}

QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 0px;
}

QScrollBar::handle:vertical {
    background: %{scroll_handle};
    min-height: 24px;
    border-radius: 5px;
}

QScrollBar::handle:vertical:hover {
    background: %{overlay};
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    height: 0px;
    background: transparent;
}

QProgressBar {
    background-color: %{surface};
    border: none;
    border-radius: %{small_radius}px;
    text-align: center;
    color: %{text};
}

QProgressBar::chunk {
    background-color: %{accent};
    border-radius: %{small_radius}px;
}
)")
        .replace(QLatin1String("%{background}"), hex(theme.background))
        .replace(QLatin1String("%{surface}"), hex(theme.surface))
        .replace(QLatin1String("%{overlay}"), hex(theme.overlay))
        .replace(QLatin1String("%{text}"), hex(theme.text))
        .replace(QLatin1String("%{subtext}"), hex(theme.subtext))
        .replace(QLatin1String("%{accent}"), hex(theme.accent))
        .replace(QLatin1String("%{selection_bg}"), hex(theme.selectionBackground))
        .replace(QLatin1String("%{error}"), hex(theme.error))
        .replace(QLatin1String("%{border_focused}"), hex(theme.borderFocused))
        .replace(QLatin1String("%{border}"), hex(theme.border))
        .replace(QLatin1String("%{hover}"), hover)
        .replace(QLatin1String("%{scroll_handle}"), scrollHandle)
        // §9's "subtly lighter background" for the focused panel. Subtle is the
        // operative word: a strong difference competes with the cursor row for
        // attention, and the border is already doing the work.
        // The focused panel is `surface`, not a shade of the background.
        //
        // It was shade(background, !light, 6), and in a light theme !light is
        // false — so the focused panel was *darkened*, turning the pane the user
        // is working in grey while the one they are not stayed near-white.
        // Exactly backwards, and it made the whole window look grey.
        //
        // surface is already the right colour in both macOS themes by
        // construction: #ffffff over #fbfbfc, and #22232a over #1f2026.
        .replace(QLatin1String("%{panel_active_bg}"), hex(theme.surface))
        // The seam between panels and under a header: a hairline, darker than
        // the panel in a dark theme and lighter in a light one, so it reads as a
        // join rather than as a drawn border.
        .replace(QLatin1String("%{seam}"), shade(theme.background, light, 14))
        // The rule under a panel header is internal to the panel, so it is far
        // fainter than the seam *between* panels — a hairline, not a border.
        .replace(QLatin1String("%{header_rule}"), shade(theme.background, light, 6))
        // The sidebar is a shade off the content so it reads as chrome, without
        // becoming a second colour in its own right.
        // Away from the content in both directions: darker under a light
        // theme, lifted under a dark one. `light` rather than `!light` sent it
        // towards white in a light theme, which is how the sidebar stopped
        // reading as a separate surface at all.
        .replace(QLatin1String("%{sidebar_bg}"), shade(theme.background, !light, 5))
        .replace(QLatin1String("%{mono_family}"),
                 QStringLiteral("'SF Mono', ui-monospace, Menlo, Consolas, monospace"))
        .replace(QLatin1String("%{chrome_font_size}"), QString::number(theme.fontSize - 1))
        .replace(QLatin1String("%{small_font_size}"), QString::number(theme.fontSize - 2))
        .replace(QLatin1String("%{font_size}"), QString::number(theme.fontSize))
        .replace(QLatin1String("%{radius}"), QString::number(theme.borderRadius))
        .replace(QLatin1String("%{small_radius}"),
                 QString::number(std::max(2, theme.borderRadius - 2)))
        .replace(QLatin1String("%{padding}"), QString::number(theme.panelPadding))
        .replace(QLatin1String("%{half_padding}"),
                 QString::number(std::max(2, theme.panelPadding / 2)));
}

} // namespace pf::config
