#pragma once

#include "config/Config.h"
#include "ui/quicklook/QuickLookDock.h"

#include <QObject>

class QKeyEvent;

namespace pf::input {
class ActionRegistry;
}

namespace pf::ui {
class FilePanel;
class MainWindow;
class PanelStrip;
class QuickLookView;
} // namespace pf::ui

namespace pf {

/// Quick Look's policy: when it opens, what it shows, and where (§7.6).
///
/// The view knows how to render a file and the window knows how to place a
/// widget; what neither knows is the behaviour §7.6 actually specifies — that a
/// second `Space` dismisses, that the content follows the *focused* panel's
/// cursor, that float closes on a panel switch and docked modes do not, and
/// that the dock choice persists. That lives here.
///
/// §3.4 requires that nothing Quick Look needs is built until it is first
/// opened, so the view — and through it every renderer — is created on the
/// first invocation, not at startup.
class QuickLookController : public QObject
{
    Q_OBJECT

public:
    QuickLookController(ui::MainWindow *window, input::ActionRegistry *registry,
                        QObject *parent = nullptr);

    /// Applies `[quicklook]`, including the configured dock. A dock the user
    /// has since cycled to takes precedence over the configured one, because
    /// §7.6 says the runtime choice persists.
    void applySettings(const config::Settings::QuickLook &settings);

    void registerActions();

    /// True while Quick Look is showing something.
    bool isOpen() const;

    void open();
    void close();
    void toggle();

    /// §7.6's `Ctrl+Space` and `Ctrl+Shift+Space`.
    void cycleDock();
    void toggleFullscreen();

    /// Offers a key to the active renderer before the dispatcher sees it
    /// (§7.6's `+`/`-`/`0`, `[`/`]`, `/`). Returns true when it was consumed —
    /// movement keys never are, which is what keeps the panel cursor live.
    bool handleKey(QKeyEvent *event);

    /// Exposed for the tests. Null until Quick Look is first opened.
    ui::QuickLookView *view() const;

private:
    ui::QuickLookView *ensureView();
    void followCursor();
    void connectPanel(ui::FilePanel *panel);
    void applyDock();

    /// Resolves the dock from the persisted choice and the configuration, once.
    /// §3.4: reading state.ini is a file open and an INI parse, and doing it
    /// before the first paint bought nothing — Quick Look is not on screen yet.
    void resolveDock();

    ui::MainWindow *m_window = nullptr;
    input::ActionRegistry *m_registry = nullptr;
    ui::QuickLookView *m_view = nullptr;

    config::Settings::QuickLook m_settings;
    ui::QuickLookDock m_dock = ui::QuickLookDock::Float;

    /// §7.6: full mode "returns to the previous mode when dismissed".
    ui::QuickLookDock m_dockBeforeFullscreen = ui::QuickLookDock::Float;

    bool m_open = false;

    /// False until resolveDock() has read the persisted choice.
    bool m_dockResolved = false;

    QMetaObject::Connection m_cursorConnection;
    QMetaObject::Connection m_pathConnection;
};

} // namespace pf
