#pragma once

#include <QString>

namespace pf::ui {

/// Where Quick Look is shown (§7.6's presentation modes).
///
/// `config.quicklook.dock` selects the default and `quick_look_cycle_dock`
/// (`Ctrl+Space`) cycles at runtime, so these need a stable string form in both
/// directions.
enum class QuickLookDock {
    Float, ///< Centred frameless overlay, transient. The default.
    Right, ///< Docked beside the panel strip, persistent.
    Left,
    Bottom, ///< Below the panel strip, above the footer.
    Panel,  ///< A slot in the panel strip, as though it were another panel.
    Full,   ///< Fills the content area, hiding the panels.
};

/// Parses `config.quicklook.dock`. Anything unrecognised is Float, which is the
/// documented default and the only mode that is guaranteed to fit.
QuickLookDock parseDock(const QString &name);

QString dockName(QuickLookDock dock);

/// True for the modes §7.6 calls docked — persistent, never taking focus.
bool isDocked(QuickLookDock dock);

/// The next mode in the cycle `Ctrl+Space` walks.
///
/// Full is not in the cycle: §7.6 gives it its own toggle
/// (`Ctrl+Shift+Space`) which "returns to the previous mode when dismissed",
/// and a mode you can cycle *into* has no previous mode to return to.
QuickLookDock nextDock(QuickLookDock dock);

} // namespace pf::ui
