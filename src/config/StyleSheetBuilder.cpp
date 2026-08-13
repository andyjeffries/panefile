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
QWidget#filePanel {
    background-color: %{background};
    border: 1px solid %{border};
    border-radius: %{radius}px;
}

QWidget#filePanel[panelActive="true"] {
    border: 1px solid %{border_focused};
    background-color: %{panel_active_bg};
}

QLabel#panelHeader {
    background-color: %{surface};
    color: %{subtext};
    padding: %{half_padding}px %{padding}px;
    border-top-left-radius: %{radius}px;
    border-top-right-radius: %{radius}px;
}

QWidget#filePanel[panelActive="true"] QLabel#panelHeader {
    color: %{text};
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

QWidget#sidebar {
    background-color: %{surface};
    border: none;
}

QListWidget#sidebarList {
    background-color: %{surface};
    border: none;
    outline: none;
    padding: %{half_padding}px 0px;
}

QListWidget#sidebarList::item {
    color: %{subtext};
    padding: 3px %{padding}px;
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

QLabel#footer {
    color: %{subtext};
}

QLabel#pendingKeys {
    color: %{accent};
    font-weight: bold;
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
        .replace(QLatin1String("%{panel_active_bg}"), shade(theme.background, !light, 6))
        .replace(QLatin1String("%{font_size}"), QString::number(theme.fontSize))
        .replace(QLatin1String("%{radius}"), QString::number(theme.borderRadius))
        .replace(QLatin1String("%{small_radius}"),
                 QString::number(std::max(2, theme.borderRadius - 2)))
        .replace(QLatin1String("%{padding}"), QString::number(theme.panelPadding))
        .replace(QLatin1String("%{half_padding}"),
                 QString::number(std::max(2, theme.panelPadding / 2)));
}

} // namespace pf::config
