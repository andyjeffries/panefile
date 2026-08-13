#include "app/QuickLookController.h"

#include "input/ActionRegistry.h"
#include "core/Logging.h"
#include "platform/Paths.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/quicklook/QuickLookView.h"

#include <QDir>
#include <QKeyEvent>
#include <QSettings>

using pf::input::ActionCategory;
using pf::ui::QuickLookDock;

namespace pf {
namespace {

/// §7.6: `quick_look_cycle_dock` "cycles at runtime and persists the choice".
/// State, not configuration — §8 is explicit that Panefile never writes to a
/// user's config file.
QString stateFilePath()
{
    return platform::stateDir() + QStringLiteral("/state.ini");
}

QString readPersistedDock()
{
    const QSettings state(stateFilePath(), QSettings::IniFormat);
    return state.value(QStringLiteral("quicklook/dock")).toString();
}

void persistDock(QuickLookDock dock)
{
    QDir().mkpath(platform::stateDir());
    QSettings state(stateFilePath(), QSettings::IniFormat);
    state.setValue(QStringLiteral("quicklook/dock"), ui::dockName(dock));
}

} // namespace

QuickLookController::QuickLookController(ui::MainWindow *window, input::ActionRegistry *registry,
                                         QObject *parent)
    : QObject(parent), m_window(window), m_registry(registry)
{
    if (m_window != nullptr) {
        connect(m_window, &ui::MainWindow::quickLookDismissed, this, &QuickLookController::close);

        connect(m_window->panelStrip(), &ui::PanelStrip::focusedPanelChanged, this,
                [this](ui::FilePanel *panel) {
                    connectPanel(panel);

                    // §7.6: float "closes on Space, Esc, focus loss, or panel
                    // switch"; docked modes "simply track the focused panel's
                    // cursor". Same event, opposite responses.
                    if (m_open && m_dock == QuickLookDock::Float && m_settings.closeOnPanelSwitch) {
                        close();
                        return;
                    }
                    followCursor();
                });
    }
}

void QuickLookController::applySettings(const config::Settings::QuickLook &settings)
{
    m_settings = settings;

    if (m_view != nullptr) {
        // Only once Quick Look exists. Before that there is nothing to apply
        // the dock to, and resolving it would mean reading state.ini on the
        // startup path for a user who may never press Space.
        m_view->applySettings(settings);
        resolveDock();
        applyDock();
    }
}

void QuickLookController::resolveDock()
{
    if (m_dockResolved) {
        return;
    }
    m_dockResolved = true;

    // The persisted runtime choice wins over the configured default: a user who
    // pressed Ctrl+Space last session meant it, and re-reading config.toml on a
    // hot reload must not silently undo it.
    const QString persisted = readPersistedDock();
    m_dock = ui::parseDock(persisted.isEmpty() ? m_settings.dock : persisted);

    if (m_dock != QuickLookDock::Full) {
        m_dockBeforeFullscreen = m_dock;
    }
}

ui::QuickLookView *QuickLookController::view() const
{
    return m_view;
}

ui::QuickLookView *QuickLookController::ensureView()
{
    if (m_view != nullptr) {
        return m_view;
    }

    // §3.4: "no renderer is instantiated until Quick Look is first opened".
    // This is that moment, and it is deliberately not at startup.
    resolveDock();

    m_view = new ui::QuickLookView;
    m_view->applySettings(m_settings);

    connect(m_view, &ui::QuickLookView::closeRequested, this, &QuickLookController::close);

    m_window->setQuickLookWidget(m_view);
    applyDock();

    return m_view;
}

void QuickLookController::applyDock()
{
    if (m_window == nullptr) {
        return;
    }
    m_window->setQuickLookDock(m_dock, m_settings.floatSizePercent, m_settings.dockSizePercent);
}

bool QuickLookController::isOpen() const
{
    return m_open;
}

void QuickLookController::connectPanel(ui::FilePanel *panel)
{
    disconnect(m_cursorConnection);
    disconnect(m_pathConnection);

    if (panel == nullptr) {
        return;
    }

    m_cursorConnection =
        connect(panel, &ui::FilePanel::cursorChanged, this, [this](const QString &) {
            // §7.6: "while it is open the arrow keys still move the panel
            // cursor, and the Quick Look content follows". The 120 ms debounce
            // that makes holding `j` survivable lives in the loader, so this
            // stays a straight call.
            followCursor();
        });

    m_pathConnection = connect(panel, &ui::FilePanel::pathChanged, this,
                               [this](const QString &) { followCursor(); });
}

void QuickLookController::followCursor()
{
    if (!m_open || m_view == nullptr) {
        return;
    }

    // §7.6's `follow_cursor = false`: "snapshot on open, don't track".
    if (!m_settings.followCursor) {
        return;
    }

    const ui::FilePanel *panel = m_window->activePanel();
    if (panel == nullptr) {
        return;
    }

    const QString path = panel->cursorPath();
    if (path.isEmpty()) {
        m_view->clear();
        return;
    }

    m_view->showFile(path, panel->cursorEntry());
}

void QuickLookController::open()
{
    if (m_window == nullptr) {
        return;
    }

    ui::QuickLookView *view = ensureView();
    m_open = true;

    connectPanel(m_window->activePanel());
    m_window->setQuickLookVisible(true);

    if (const ui::FilePanel *panel = m_window->activePanel(); panel != nullptr) {
        const QString path = panel->cursorPath();
        if (path.isEmpty()) {
            view->clear();
        } else {
            view->showFile(path, panel->cursorEntry());
        }
    }
}

void QuickLookController::close()
{
    if (!m_open) {
        return;
    }

    m_open = false;
    m_window->setQuickLookVisible(false);

    if (m_view != nullptr) {
        // Drops the decoded content as well as the display: §7.6's cache is
        // there to make cursor movement instant, not to hold a 40-megapixel
        // decode for the rest of the session.
        m_view->clear();
    }

    // §7.6: full mode "returns to the previous mode when dismissed".
    if (m_dock == QuickLookDock::Full) {
        m_dock = m_dockBeforeFullscreen;
        applyDock();
    }
}

void QuickLookController::toggle()
{
    if (m_open) {
        close();
        return;
    }
    open();
}

void QuickLookController::cycleDock()
{
    resolveDock();

    m_dock = ui::nextDock(m_dock);
    m_dockBeforeFullscreen = m_dock;
    persistDock(m_dock);

    qCDebug(pfApp) << "quick look dock:" << ui::dockName(m_dock);

    if (m_view == nullptr) {
        // Cycling before Quick Look has ever been opened is legitimate — the
        // choice is remembered, and the view is still not built until it is
        // actually needed.
        return;
    }

    applyDock();
    m_window->setQuickLookVisible(m_open);
}

void QuickLookController::toggleFullscreen()
{
    resolveDock();

    if (m_dock == QuickLookDock::Full) {
        m_dock = m_dockBeforeFullscreen;
    } else {
        m_dockBeforeFullscreen = m_dock;
        m_dock = QuickLookDock::Full;
    }

    ensureView();
    applyDock();

    // Toggling into full mode from a closed state opens it: §7.6 offers it as a
    // way to see a file large, and requiring Space first would be a step the
    // key itself already implies.
    if (!m_open) {
        open();
    } else {
        m_window->setQuickLookVisible(true);
    }
}

bool QuickLookController::handleKey(QKeyEvent *event)
{
    if (!m_open || m_view == nullptr) {
        return false;
    }
    return m_view->handleKey(event);
}

void QuickLookController::registerActions()
{
    if (m_registry == nullptr) {
        return;
    }

    m_registry->registerAction(QStringLiteral("quick_look"), tr("Preview the cursor item"),
                               ActionCategory::View, [this] { toggle(); });

    m_registry->registerAction(QStringLiteral("quick_look_cycle_dock"),
                               tr("Cycle the Quick Look dock position"), ActionCategory::View,
                               [this] { cycleDock(); });

    m_registry->registerAction(QStringLiteral("quick_look_fullscreen"),
                               tr("Show Quick Look full-screen"), ActionCategory::View,
                               [this] { toggleFullscreen(); });
}

} // namespace pf
