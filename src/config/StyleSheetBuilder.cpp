#include "config/StyleSheetBuilder.h"

#include "config/Theme.h"

#include <QFile>
#include <QList>
#include <QPair>
#include <QTemporaryDir>

namespace pf::config {
namespace {

QString hex(const QColor &colour)
{
    return colour.name(QColor::HexRgb);
}

/// Black or white, whichever the given background can carry. Rec. 709 luma,
/// which tracks perceived brightness far better than a mean of the channels.
QColor readableOn(const QColor &background)
{
    const double luma = (0.2126 * background.redF()) + (0.7152 * background.greenF()) +
                        (0.0722 * background.blueF());
    return luma > 0.55 ? QColor(0, 0, 0) : QColor(255, 255, 255);
}

/// A chevron in the given colour, written once per process and colour, and its
/// path for a stylesheet's `image: url(...)`.
///
/// The arrows on combo and spin boxes can only come from image files — Qt's
/// stylesheet engine draws those sub-controls from an image or not at all, and
/// never asks the base style — and an image file cannot follow a theme. So the
/// files are made to match it: a few hundred bytes of SVG each, in a temporary
/// directory that goes away with the process.
QString chevronFile(const char *direction, const QColor &colour)
{
    static const QTemporaryDir directory;
    if (!directory.isValid()) {
        return {};
    }

    const bool up = qstrcmp(direction, "up") == 0;
    const QString path = directory.filePath(
        QStringLiteral("chevron-%1-%2.svg").arg(QLatin1String(direction), hex(colour).mid(1)));
    if (!QFile::exists(path)) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(
                QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\">"
                               "<path d=\"%1\" fill=\"none\" stroke=\"%2\" stroke-width=\"1.5\" "
                               "stroke-linecap=\"round\" stroke-linejoin=\"round\"/></svg>")
                    .arg(up ? QStringLiteral("M2 6.5 5 3.5 8 6.5")
                            : QStringLiteral("M2 3.5 5 6.5 8 3.5"),
                         hex(colour))
                    .toUtf8());
        }
    }
    return path;
}

/// The stylesheet's vocabulary, from the theme.
///
/// A theme stores a palette; a window needs a few more colours than a palette
/// names — a fill for a button, a stronger border for a popup, the hover over
/// each surface. Every one of them is derived here, in one place, by mixing the
/// theme's own colours rather than by lighter()/darker(), so a derived shade
/// keeps the theme's temperature: Panefile Light's hover is a warm grey because
/// its text is a warm near-black, not because anybody chose a grey.
QList<QPair<QString, QString>> tokensFor(const Theme &theme)
{
    const bool light = theme.isLight();
    const QColor sidebar = theme.effectiveSidebarBackground();

    // Fills a step and two steps off a surface, for controls that sit on one:
    // a button at rest, then hovered or pressed. Review calls these surface2
    // and surface3.
    const QColor fill = mixColours(theme.surface, theme.text, light ? 0.035 : 0.045);
    const QColor fillStrong = mixColours(theme.surface, theme.text, light ? 0.08 : 0.09);

    const QColor accentHover = light ? theme.accent.darker(110) : theme.accent.lighter(112);

    return {
        {QStringLiteral("chevron_down"), chevronFile("down", theme.subtext)},
        {QStringLiteral("chevron_up"), chevronFile("up", theme.subtext)},
        {QStringLiteral("background"), hex(theme.background)},
        {QStringLiteral("surface"), hex(theme.surface)},
        {QStringLiteral("text"), hex(theme.text)},
        // Three levels of text. `muted` for secondary facts, `faint` for
        // furniture. Themes predating this used subtext and overlay as one
        // grey, and still can; nothing breaks, the levels simply coincide.
        {QStringLiteral("muted"), hex(theme.subtext)},
        {QStringLiteral("faint"), hex(theme.overlay)},
        {QStringLiteral("accent_hover"), hex(accentHover)},
        // The accent's tint, for something that is on; the focus colour's, for
        // where you are in a list — the same tint as the cursor row.
        {QStringLiteral("accent_soft"),
         hex(mixColours(theme.surface, theme.accent, light ? 0.12 : 0.19))},
        {QStringLiteral("focus_soft"), hex(theme.focusedCursorBackground())},
        {QStringLiteral("accent"), hex(theme.accent)},
        {QStringLiteral("selection_bg"), hex(theme.selectionBackground)},
        {QStringLiteral("error"), hex(theme.error)},
        {QStringLiteral("border_focused"), hex(theme.borderFocused)},
        {QStringLiteral("border_strong"), hex(mixColours(theme.border, theme.text, 0.12))},
        {QStringLiteral("border"), hex(theme.border)},
        // The rule under a panel header is internal to the panel, so it is
        // fainter than the seam *between* panels — a hairline, not a border.
        {QStringLiteral("header_rule"), hex(mixColours(theme.border, theme.surface, 0.45))},
        {QStringLiteral("fill_strong"), hex(fillStrong)},
        {QStringLiteral("fill"), hex(fill)},
        {QStringLiteral("hover_sidebar"), hex(theme.hoverBackground(sidebar))},
        {QStringLiteral("hover"), hex(theme.hoverBackground(theme.surface))},
        {QStringLiteral("sidebar_bg"), hex(sidebar)},
        {QStringLiteral("scroll_handle_hover"),
         hex(mixColours(theme.background, theme.text, light ? 0.42 : 0.45))},
        {QStringLiteral("scroll_handle"),
         hex(mixColours(theme.background, theme.text, light ? 0.24 : 0.28))},
        {QStringLiteral("mono_family"),
         QStringLiteral("'JetBrains Mono', 'SF Mono', ui-monospace, Menlo, Consolas, monospace")},
        // White or black against the accent, whichever the accent can carry —
        // a light theme's accent may need dark text on it.
        {QStringLiteral("on_selection"), hex(readableOn(theme.selectionBackground))},
        {QStringLiteral("on_accent"), hex(readableOn(theme.accent))},
        {QStringLiteral("on_error"), hex(readableOn(theme.error))},
        {QStringLiteral("error_hover"),
         hex(light ? theme.error.darker(110) : theme.error.lighter(112))},
        // The type scale, in pixels: body, then a step down for secondary
        // facts, a step further for captions, and up for titles.
        {QStringLiteral("title_font_size"), QString::number(theme.fontSize + 3)},
        {QStringLiteral("heading_font_size"), QString::number(theme.fontSize + 1)},
        {QStringLiteral("small_font_size"), QString::number(theme.fontSize - 1)},
        {QStringLiteral("caption_font_size"), QString::number(theme.fontSize - 2)},
        {QStringLiteral("font_size"), QString::number(theme.fontSize)},
        {QStringLiteral("inner_large_radius"), QString::number(theme.borderRadius + 5)},
        {QStringLiteral("large_radius"), QString::number(theme.borderRadius + 6)},
        {QStringLiteral("control_radius"), QString::number(theme.borderRadius + 2)},
        {QStringLiteral("small_radius"), QString::number(std::max(2, theme.borderRadius - 2))},
        {QStringLiteral("radius"), QString::number(theme.borderRadius)},
        {QStringLiteral("half_padding"), QString::number(std::max(2, theme.panelPadding / 2))},
        {QStringLiteral("padding"), QString::number(theme.panelPadding)},
    };
}

} // namespace

QString buildStyleSheet(const Theme &theme)
{
    // Widget selectors rather than object names wherever possible, so a widget
    // added later is styled without anyone remembering to add a rule for it.
    // Object names are used only where two instances of the same class need to
    // look different.
    //
    // Check marks and radio dots are not here: QSS can only draw them from
    // image files. ui::PanefileStyle paints them in the theme's colours, which
    // only works while these rules leave the indicator sub-controls alone. The
    // arrows on combo and spin boxes have no such way out; see chevronFile().
    QString sheet = QStringLiteral(R"(
/* Generated from the active theme. Do not edit — change theme.toml instead. */

QWidget {
    background-color: %{background};
    color: %{text};
    font-size: %{font_size}px;
}

QToolTip {
    background-color: %{surface};
    color: %{text};
    border: 1px solid %{border_strong};
    border-radius: %{radius}px;
    padding: 5px 8px;
    font-size: %{small_font_size}px;
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
   hairline seam — the splitter handle — instead of floating as rounded cards
   over a backdrop. */
QWidget#filePanel {
    background-color: %{background};
    border: none;
    border-top: 2px solid transparent;
    border-radius: 0px;
}

/* The focused panel is `surface`, not a shade of the background: white over
   an off-white body in a light theme, lifted off a near-black one in a dark
   theme. */
QWidget#filePanel[panelActive="true"] {
    border-top: 2px solid %{border_focused};
    background-color: %{surface};
}

/* A hairline under the header rather than a filled bar: the path and the count
   are labels on the list, not a toolbar above it. */
/* The margins are set on the layout in FilePanel, because padding here would
   style this widget's own painting without insetting the labels inside it. */
QWidget#panelHeaderRow {
    background-color: transparent;
    border-bottom: 1px solid %{header_rule};
}

/* The path is rich text with its own colours — parents faint, the folder
   itself in the text colour — so only its size is set here. */
QLabel#panelHeader {
    background-color: transparent;
    color: %{faint};
    font-size: %{font_size}px;
}

/* The count is a step quieter than the path, and both are a step quieter in a
   panel that is not focused — a second reading of the same signal the accent
   edge gives, for anyone whose eye is on the list rather than its top. */
QLabel#panelHeaderCount {
    background-color: transparent;
    color: %{faint};
    font-size: %{small_font_size}px;
}

QLabel#panelHeaderCount[panelActive="true"] {
    color: %{muted};
}

QListView#panelView {
    background-color: transparent;
    border: none;
    outline: none;
}

/* The type-to-filter field, inset into the panel like the rows above it. */
QLineEdit#panelFilter {
    background-color: %{fill};
    margin: 4px 6px 6px 6px;
}

/* Sidebar --------------------------------------------------------------- */

/* A step off the content, so the sidebar reads as a separate surface rather
   than an indented column of words. No border of its own: the first panel's
   splitter handle beside it is the join, and a second line made a double rule. */
QWidget#sidebar {
    background-color: %{sidebar_bg};
    border: none;
}

/* Transparent, or it paints the *content* background over the sidebar: the
   catch-all QWidget rule gives every widget the window's background colour, and
   a label that does not opt out of it stamps a lighter block behind its own
   text. */
QPushButton#sidebarMenu {
    border-radius: %{radius}px;
    padding: 0px;
}

QPushButton#sidebarMenu:hover {
    background-color: %{hover_sidebar};
}

QLabel#sidebarSection {
    background-color: transparent;
    color: %{faint};
    font-size: %{caption_font_size}px;
    font-weight: 600;
    padding: 14px 18px 4px 18px;
}

QListWidget#sidebarList {
    background-color: %{sidebar_bg};
    border: none;
    outline: none;
    padding: 0px 8px 8px 8px;
}

/* Rows inset from the sidebar's edges so the selection reads as a pill on a
   surface rather than a band running from one edge to the other. */
QListWidget#sidebarList::item {
    color: %{text};
    padding: 5px 8px;
    min-height: 20px;
    border-radius: %{radius}px;
}

QListWidget#sidebarList::item:hover {
    background-color: %{hover_sidebar};
}

/* Headings and the divider are rows that are not places: no hover. */
QListWidget#sidebarList::item:disabled, QListWidget#sidebarList::item:disabled:hover {
    background-color: transparent;
}

QFrame#sidebarDivider {
    background-color: %{border};
    border: none;
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
   hanging off the bottom of it. The same surface as the sidebar: both are
   chrome around the panels. */
QWidget#footerRow {
    background-color: %{sidebar_bg};
    border-top: 1px solid %{border};
    min-height: 28px;
}

/* The UI face, with the permission string set in monospace inside the label's
   own rich text: drwxr-xr-x is read by column and wants fixed widths, the size
   and the date do not, and a whole strip of monospace read as a terminal. */
QLabel#footer {
    background-color: transparent;
    color: %{muted};
    font-size: %{small_font_size}px;
}

QLabel#selectionCount {
    background-color: transparent;
    color: %{muted};
    font-size: %{small_font_size}px;
}

/* A pending key sequence as a keycap: what you have typed so far, drawn as the
   keys it is. */
QLabel#pendingKeys {
    background-color: %{fill_strong};
    color: %{text};
    border: 1px solid %{border};
    border-radius: 5px;
    padding: 0px 6px;
    font-family: %{mono_family};
    font-size: %{caption_font_size}px;
}

/* The process bar: the same chrome as the status bar it sits under. */
QWidget#processBar {
    background-color: %{sidebar_bg};
    border-top: 1px solid %{border};
}

QLabel#processSummary {
    background-color: transparent;
    color: %{muted};
    font-size: %{small_font_size}px;
}

QTreeWidget#processJobs {
    background-color: transparent;
    border: none;
}

/* Controls ------------------------------------------------------------- */

/* A quiet button: a filled shape a step off whatever it sits on, as Review's
   "subtle" button. The default button is the one primary action and takes the
   accent; nothing else in a dialog does. */
QPushButton {
    background-color: %{fill};
    color: %{text};
    border: 1px solid %{border};
    border-radius: %{control_radius}px;
    padding: 5px 14px;
    font-weight: 500;
}

QPushButton:hover {
    background-color: %{fill_strong};
}

QPushButton:pressed {
    background-color: %{fill_strong};
    border-color: %{border_strong};
}

QPushButton:default {
    background-color: %{accent};
    border: 1px solid %{accent};
    color: %{on_accent};
    font-weight: 600;
}

QPushButton:default:hover {
    background-color: %{accent_hover};
    border-color: %{accent_hover};
}

QPushButton:disabled {
    color: %{faint};
    background-color: %{fill};
    border-color: %{border};
}

/* Keyboard focus on a button, for the confirmations where Enter presses
   whichever one has it. */
QPushButton:focus {
    border-color: %{border_focused};
}

/* The one button that destroys something: the error colour, filled. */
QPushButton#destructiveButton {
    background-color: %{error};
    border: 1px solid %{error};
    color: %{on_error};
    font-weight: 600;
}

QPushButton#destructiveButton:hover {
    background-color: %{error_hover};
    border-color: %{error_hover};
}

QPushButton#destructiveButton:focus {
    border-color: %{text};
}

/* A flat button is a ghost: no shape until the pointer is on it. */
QPushButton:flat {
    background-color: transparent;
    border: none;
}

QPushButton:flat:hover {
    background-color: %{hover};
}

QLineEdit {
    background-color: %{surface};
    color: %{text};
    border: 1px solid %{border};
    border-radius: %{control_radius}px;
    padding: 5px 9px;
    selection-background-color: %{accent};
    selection-color: %{on_accent};
}

QLineEdit:focus {
    border: 1px solid %{border_focused};
}

QLineEdit:disabled {
    color: %{faint};
}

QComboBox, QSpinBox {
    background-color: %{fill};
    color: %{text};
    border: 1px solid %{border};
    border-radius: %{control_radius}px;
    padding: 4px 9px;
    selection-background-color: %{accent};
    selection-color: %{on_accent};
}

QComboBox:hover, QSpinBox:hover {
    background-color: %{fill_strong};
}

QComboBox:focus, QSpinBox:focus {
    border: 1px solid %{border_focused};
}

QComboBox::drop-down {
    background-color: transparent;
    border: none;
    width: 22px;
    subcontrol-origin: padding;
    subcontrol-position: center right;
}

QComboBox::down-arrow, QSpinBox::down-arrow {
    image: url("%{chevron_down}");
    width: 10px;
    height: 10px;
}

QSpinBox::up-arrow {
    image: url("%{chevron_up}");
    width: 10px;
    height: 10px;
}

QComboBox QAbstractItemView {
    background-color: %{surface};
    color: %{text};
    border: 1px solid %{border_strong};
    outline: none;
    padding: 4px;
    selection-background-color: %{fill_strong};
    selection-color: %{text};
}

QSpinBox {
    padding-right: 22px;
}

QSpinBox::up-button, QSpinBox::down-button {
    subcontrol-origin: border;
    background-color: transparent;
    border: none;
    width: 20px;
}

QSpinBox::up-button {
    subcontrol-position: top right;
}

QSpinBox::down-button {
    subcontrol-position: bottom right;
}

QCheckBox, QRadioButton {
    background-color: transparent;
    spacing: 8px;
}

QCheckBox:disabled, QRadioButton:disabled {
    color: %{faint};
}

/* Menus: a card with inset, rounded items — the same shape as a list row's
   pill, so a context menu looks like it belongs to the list it came from.
   Rounded corners need a translucent window, which PanefileStyle arranges. */
QMenu {
    background-color: %{surface};
    color: %{text};
    border: 1px solid %{border_strong};
    border-radius: %{large_radius}px;
    padding: 5px;
}

QMenu::item {
    background-color: transparent;
    padding: 6px 28px 6px 12px;
    border-radius: %{radius}px;
}

QMenu::item:selected {
    background-color: %{fill_strong};
    color: %{text};
}

QMenu::item:disabled {
    color: %{faint};
}

QMenu::separator {
    height: 1px;
    background-color: %{border};
    margin: 4px 6px;
}

QProgressBar {
    background-color: %{fill_strong};
    border: none;
    border-radius: 3px;
    max-height: 6px;
    text-align: center;
    color: transparent;
}

QProgressBar::chunk {
    background-color: %{accent};
    border-radius: 3px;
}

/* Lists and trees inside dialogs ---------------------------------------- */

QTreeWidget, QTreeView, QListWidget {
    background-color: %{surface};
    alternate-background-color: %{surface};
    border: 1px solid %{border};
    border-radius: %{control_radius}px;
    outline: none;
}

QTreeView::item, QListWidget::item {
    padding: 4px 6px;
    border: none;
}

QTreeView::item:hover, QListWidget::item:hover {
    background-color: %{hover};
}

QTreeView::item:selected, QListWidget::item:selected {
    background-color: %{selection_bg};
    color: %{text};
}

/* Column headers as captions: small, faint and on the list's own surface,
   rather than a raised bar of buttons. */
QHeaderView {
    background-color: transparent;
    border: none;
}

QHeaderView::section {
    background-color: %{surface};
    color: %{faint};
    border: none;
    border-bottom: 1px solid %{border};
    padding: 6px 8px;
    font-size: %{caption_font_size}px;
    font-weight: 600;
}

/* Settings --------------------------------------------------------------- */

/* A preferences toolbar: a centred row of icon-over-label tabs, with a
   hairline separating it from the content. Not a sidebar list, which is a web
   idiom, and not a tab bar, which is for documents. */
QWidget#settingsToolbar {
    background-color: %{sidebar_bg};
    border-bottom: 1px solid %{border};
    border-top-left-radius: %{inner_large_radius}px;
    border-top-right-radius: %{inner_large_radius}px;
}

QPushButton#settingsTab {
    background-color: transparent;
    border: none;
    border-radius: %{control_radius}px;
    min-width: 92px;
    padding: 0px;
}

QPushButton#settingsTab:hover {
    background-color: %{hover_sidebar};
}

/* The selected tab carries the accent in its glyph and label, on the accent's
   soft tint: a control that is on, as a checked box is, rather than a place in
   a list, which takes the focus colour. */
QPushButton#settingsTab:checked {
    background-color: %{accent_soft};
}

QLabel#settingsTabGlyph {
    background-color: transparent;
    color: %{muted};
}

QLabel#settingsTabLabel {
    background-color: transparent;
    color: %{muted};
    font-size: %{small_font_size}px;
    font-weight: 500;
}

QPushButton#settingsTab:checked QLabel#settingsTabGlyph,
QPushButton#settingsTab:checked QLabel#settingsTabLabel {
    color: %{accent};
}

QStackedWidget#settingsPages {
    background-color: %{surface};
    border-bottom-left-radius: %{inner_large_radius}px;
    border-bottom-right-radius: %{inner_large_radius}px;
}

QStackedWidget#settingsPages QLabel, QStackedWidget#settingsPages QCheckBox {
    background-color: transparent;
}

QLabel#settingsHeading {
    background-color: transparent;
    color: %{text};
    font-size: %{heading_font_size}px;
    font-weight: 600;
}

QLabel#settingsNote {
    background-color: transparent;
    color: %{faint};
    font-size: %{small_font_size}px;
}

QListWidget#settingsThemeList::item {
    color: %{text};
    padding: 5px 8px;
    margin: 1px 4px;
    border-radius: %{radius}px;
}

QListWidget#settingsThemeList::item:selected {
    background-color: %{focus_soft};
    color: %{text};
}

/* Modals ---------------------------------------------------------------- */

QWidget#modalContent {
    background-color: %{surface};
    border: 1px solid %{border_strong};
    border-radius: %{large_radius}px;
}

QWidget#modalContent QLabel {
    background-color: transparent;
    color: %{text};
}

QWidget#modalContent QLabel#modalTitle {
    font-size: %{title_font_size}px;
    font-weight: 600;
}

QWidget#modalContent QLabel#modalMessage {
    color: %{muted};
}

QWidget#modalContent QLabel#modalHint,
QWidget#modalContent QLabel#findStatus {
    color: %{muted};
    font-size: %{small_font_size}px;
}

QWidget#modalContent QLabel#modalProblem {
    color: %{error};
    font-size: %{small_font_size}px;
}

/* Inputs inside a card take a fill, so they read as wells in it rather than
   as white boxes on white. */
QWidget#modalContent QLineEdit {
    background-color: %{fill};
}

QWidget#modalContent QLineEdit:focus {
    background-color: %{surface};
}

/* The keyboard reference is the card's whole content, so it needs no box of
   its own inside the box it is already in. */
QWidget#modalContent QTreeWidget#helpTable {
    border: none;
    background-color: transparent;
}

QWidget#modalContent QTreeWidget#helpTable QHeaderView::section {
    background-color: %{surface};
}

/* Quick Look ------------------------------------------------------------- */

QWidget#quickLook, QStackedWidget#quickLookStack {
    background-color: %{surface};
}

QWidget#quickLookHeader {
    background-color: transparent;
    border-bottom: 1px solid %{header_rule};
}

QWidget#quickLookFooter {
    background-color: transparent;
    border-top: 1px solid %{header_rule};
}

QLabel#quickLookSkeleton {
    background-color: transparent;
    color: %{faint};
}

QPushButton#quickLookClose {
    border-radius: %{radius}px;
    padding: 0px;
}

QLabel#quickLookTitle {
    background-color: transparent;
    color: %{text};
    font-weight: 600;
}

QLabel#quickLookSubtitle, QLabel#quickLookHint {
    background-color: transparent;
    color: %{faint};
    font-size: %{small_font_size}px;
}

/* The notice bar --------------------------------------------------------- */

/* The offer to become the default file manager: a slim strip across the top of
   the window, the same surface as a focused panel, so it reads as part of the
   window rather than as a dialog that has landed on it. */
QWidget#noticeBar {
    background-color: %{surface};
    border-bottom: 1px solid %{border};
}

QLabel#noticeBarText {
    background-color: transparent;
    color: %{text};
}

QLabel#noticeBarText[noticeState="error"] {
    color: %{error};
}

QPushButton#noticeBarButton, QPushButton#noticeBarPrimary {
    padding: 3px 12px;
    font-size: %{small_font_size}px;
}

QPushButton#noticeBarPrimary {
    background-color: %{accent};
    border: 1px solid %{accent};
    color: %{on_accent};
    font-weight: 600;
}

QPushButton#noticeBarPrimary:hover {
    background-color: %{accent_hover};
}

/* Splitters and scrollbars ---------------------------------------------- */

/* One hairline where panes meet: the handle is the seam, a pixel wide with a
   wider grab area Qt gives it, and it takes the accent while it is held or
   under the pointer. */
QSplitter::handle {
    background-color: %{border};
}

QSplitter::handle:hover, QSplitter::handle:pressed {
    background-color: %{border_focused};
}

/* An overlay scrollbar, not a widget with a track. A thick bar with a visible
   groove is a Motif-era affordance; a thin thumb over the content is all a
   list needs, and it fattens under the pointer to be easy to grab. */
QAbstractScrollArea::corner {
    background: transparent;
    border: none;
}

QScrollBar:vertical {
    background: transparent;
    border: none;
    width: 10px;
    margin: 0px;
}

QScrollBar::handle:vertical {
    background: %{scroll_handle};
    border-radius: 3px;
    min-height: 28px;
    margin: 2px 2px 2px 2px;
}

QScrollBar::handle:vertical:hover, QScrollBar::handle:vertical:pressed {
    background: %{scroll_handle_hover};
    border-radius: 3px;
    margin: 2px 1px 2px 1px;
}

QScrollBar:horizontal {
    background: transparent;
    border: none;
    height: 10px;
    margin: 0px;
}

QScrollBar::handle:horizontal {
    background: %{scroll_handle};
    border-radius: 3px;
    min-width: 28px;
    margin: 2px 2px 2px 2px;
}

QScrollBar::handle:horizontal:hover, QScrollBar::handle:horizontal:pressed {
    background: %{scroll_handle_hover};
}

QScrollBar::add-line, QScrollBar::sub-line,
QScrollBar::add-page, QScrollBar::sub-page {
    background: transparent;
    border: none;
    width: 0px;
    height: 0px;
}
)");

    // Every token is closed by `}`, so no name can match the front of another.
    for (const auto &[name, value] : tokensFor(theme)) {
        sheet.replace(QStringLiteral("%{") + name + QLatin1Char('}'), value);
    }
    return sheet;
}

} // namespace pf::config
