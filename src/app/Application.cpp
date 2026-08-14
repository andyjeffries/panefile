#include "app/Application.h"

#include "input/ActionRegistry.h"
#include "input/DefaultKeymap.h"
#include "input/Keymap.h"
#include "app/CommandLine.h"
#include "app/FileOperations.h"
#include "app/KeyDispatcher.h"
#include "app/PanelController.h"
#include "app/QuickLookController.h"
#include "app/SearchController.h"
#include "app/Session.h"
#include "app/SingleInstance.h"
#include "config/Config.h"
#include "config/ConfigWatcher.h"
#include "config/Hotkeys.h"
#include "config/StyleSheetBuilder.h"
#include "config/Theme.h"
#include "core/Logging.h"
#include "core/StartupTrace.h"
#include "core/Version.h"
#include "core/WorkerPools.h"
#include "fs/JobEngine.h"
#include "fs/UndoStack.h"
#include "model/FilterSortProxy.h"
#include "model/ThumbnailCache.h"
#include "platform/Paths.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/PanelView.h"
#include "ui/ProcessBar.h"
#include "ui/Sidebar.h"
#include "ui/ThemePalette.h"
#include "ui/modals/HelpModal.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QKeyEvent>
#include <QSocketNotifier>
#include <QStyleHints>
#include <QTimer>
#include <QWindow>

#include <array>
#include <csignal>

#include <sys/socket.h>
#include <unistd.h>

namespace pf {

using input::ActionCategory;

Application::Application(int &argc, char **argv) : QApplication(argc, argv)
{
    setApplicationName(QStringLiteral(PF_APPLICATION_NAME));
    setApplicationDisplayName(QStringLiteral(PF_APPLICATION_DISPLAY));
    setApplicationVersion(QStringLiteral(PF_VERSION));
    setOrganizationDomain(QStringLiteral(PF_ORGANIZATION_DOMAIN));

    // §10.5: without this the Wayland compositor cannot match the window to its
    // .desktop entry, and the window gets a generic icon.
    setDesktopFileName(QStringLiteral(PF_APPLICATION_NAME));
}

Application::~Application() = default;

ui::MainWindow *Application::mainWindow() const
{
    return m_mainWindow.get();
}

QString Application::initialPath(const CommandLineOptions &options)
{
    // §10.1: paths resolve against the *client's* working directory. In a
    // freshly started process that is simply our own, but keeping the rule here
    // matters for M10, where the running instance's cwd is the wrong answer.
    for (const QString &argument : options.paths) {
        const QFileInfo info(QDir::current().absoluteFilePath(argument));
        if (!info.exists()) {
            continue;
        }
        // §10.2: a path naming a file navigates to its parent and puts the
        // cursor on the file.
        return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    }

    return QDir::homePath();
}

void Application::buildInputSystem()
{
    m_registry = std::make_unique<input::ActionRegistry>();
    m_keymap = std::make_unique<input::Keymap>();

    // §3.4: bind a default map before the first keypress can arrive, so the
    // very first key is never dropped. hotkeys.toml is parsed on idle in M3 and
    // merged over this.
    input::installDefaultKeymap(*m_keymap);

    m_dispatcher = std::make_unique<KeyDispatcher>(m_registry.get(), m_keymap.get(), this);

    m_panelController =
        std::make_unique<PanelController>(m_mainWindow.get(), m_mainWindow->panelStrip(),
                                          m_mainWindow->sidebar(), m_registry.get(), this);

    connect(m_panelController.get(), &PanelController::statusMessage, m_mainWindow.get(),
            &ui::MainWindow::showStatusMessage);
    connect(m_dispatcher.get(), &KeyDispatcher::pendingChanged, m_mainWindow.get(),
            &ui::MainWindow::showPendingKeys);

    m_jobEngine = std::make_unique<fs::JobEngine>(this);
    m_undoStack = std::make_unique<fs::UndoStack>(this);

    m_fileOperations = std::make_unique<FileOperations>(
        m_mainWindow.get(), m_mainWindow->panelStrip(), m_registry.get(), m_jobEngine.get(),
        m_undoStack.get(), this);
    m_fileOperations->setSettings(m_settings);
    connect(m_fileOperations.get(), &FileOperations::statusMessage, m_mainWindow.get(),
            &ui::MainWindow::showStatusMessage);

    // §5.1: the process bar "auto-shows when jobs active". Constructing it on
    // the first submission rather than at startup keeps it off the critical
    // path (§3.4) — and the connection has to be made before any job can be
    // submitted, which is why it is wired here rather than lazily.
    connect(m_jobEngine.get(), &fs::JobEngine::jobSubmitted, this, [this](int, const QString &) {
        // Construct it, so it starts tracking — but let it decide when
        // it is worth showing. Three small files are copied before its
        // appearance delay elapses, and a bar that flashes up and
        // vanishes is worse than none at all.
        processBar();
    });

    m_panelController->registerActions();
    m_fileOperations->registerActions();
    m_quickLook = std::make_unique<QuickLookController>(m_mainWindow.get(), m_registry.get());
    m_quickLook->applySettings(m_settings.quicklook);
    m_quickLook->registerActions();

    m_search = std::make_unique<SearchController>(m_mainWindow.get(), m_mainWindow->panelStrip(),
                                                  m_registry.get());
    m_search->setSettings(m_settings);
    m_search->registerActions();
    connect(m_search.get(), &SearchController::statusMessage, m_mainWindow.get(),
            &ui::MainWindow::showStatusMessage);

    connect(m_mainWindow->panelStrip(), &ui::PanelStrip::panelCreated, this,
            &Application::configurePanel);

    connect(m_mainWindow->panelStrip(), &ui::PanelStrip::focusedPanelChanged, this,
            [this](ui::FilePanel *) { updateActiveLayers(); });

    registerGlobalActions();
}

void Application::updateActiveLayers()
{
    if (m_dispatcher == nullptr || m_mainWindow == nullptr) {
        return;
    }

    const ui::FilePanel *panel = m_mainWindow->panelStrip()->focusedPanel();
    const bool selecting = panel != nullptr && panel->isSelectionMode();

    // §6.2's precedence: "Current panel mode (Selection before Normal)", then
    // global. The mode is per panel, so this is re-evaluated on every panel
    // switch as well as on every mode change.
    m_dispatcher->setActiveLayers(
        selecting
            ? QList<input::KeymapLayer>{input::KeymapLayer::Selection, input::KeymapLayer::Normal,
                                        input::KeymapLayer::Global}
            : QList<input::KeymapLayer>{input::KeymapLayer::Normal, input::KeymapLayer::Global});
}

void Application::configurePanel(ui::FilePanel *panel) const
{
    if (panel == nullptr) {
        return;
    }

    // Every panel's mode feeds the dispatcher, because §6.1 makes the mode a
    // property of the panel rather than of the application.
    connect(panel, &ui::FilePanel::modeChanged, this,
            [this] { const_cast<Application *>(this)->updateActiveLayers(); });

    // §7.12: a drop is a copy or a move, which is FileOperations' business —
    // the panel neither knows about conflicts nor about the undo stack.
    connect(panel, &ui::FilePanel::filesDropped, m_fileOperations.get(),
            &FileOperations::onFilesDropped);

    panel->setShowHidden(m_settings.panels.showHidden);
    panel->setSortKey(sortKeyFromName(m_settings.panels.defaultSort));

    // §7.7: thumbnails are a panel-level facility, so a build with them
    // disabled never constructs the cache's memory tier at all.
    panel->setThumbnailsEnabled(m_settings.thumbnails.enabled);

    if (m_search != nullptr) {
        m_search->configurePanel(panel);
    }

    ThumbnailCache::instance().setEnabled(m_settings.thumbnails.enabled);
    ThumbnailCache::instance().setMaxFileSizeMb(m_settings.thumbnails.maxFileSizeMb);
    ThumbnailCache::instance().setVideoEnabled(m_settings.thumbnails.video);
}

void Application::registerGlobalActions()
{
    m_registry->registerAction(QStringLiteral("open_help_menu"),
                               tr("Show every action and its keys"), ActionCategory::General,
                               [this] {
                                   ui::HelpModal *modal = helpModal();
                                   modal->refresh();
                                   modal->showModal();
                               });

    // §6.3 gives `quit` both `q` and `Esc`, with the description "Close modal,
    // or quit if none". Escape no longer quits.
    //
    // The escalation was the problem, not the binding. Esc is the key you press
    // to back out of the thing you are in — a modal, a preview, a filter — and
    // making the *absence* of such a thing mean "quit the application" turns a
    // reflex into a way to lose your session. It is also unrecoverable in a way
    // none of the other steps are.
    //
    // So Esc cancels and never does anything else, and quitting has the keys
    // the platform already uses for it: Cmd+Q on macOS and Ctrl+Q on Linux,
    // which are the same chord as far as Qt is concerned.
    m_registry->registerAction(
        QStringLiteral("cancel"), tr("Dismiss the modal, the preview, or the filter"),
        ActionCategory::General, [this] {
            if (m_helpModal != nullptr && m_helpModal->isVisible()) {
                m_helpModal->dismiss();
                return;
            }

            // §7.6: Esc dismisses Quick Look before it means anything else.
            if (m_quickLook != nullptr && m_quickLook->isOpen()) {
                m_quickLook->close();
                return;
            }

            ui::FilePanel *panel = m_mainWindow->panelStrip()->focusedPanel();
            if (panel == nullptr) {
                return;
            }

            // §7.8: "Esc clears it."
            if (!panel->filterText().isEmpty() || panel->isFilterBarOpen()) {
                panel->closeFilterBar(false);
                return;
            }

            // §6.1: and leaves Selection mode, which is the last thing there is
            // to back out of.
            if (panel->isSelectionMode()) {
                panel->setSelectionMode(false);
                return;
            }

            if (panel->selectionCount() > 0) {
                panel->clearSelection();
                m_mainWindow->showStatusMessage(tr("Selection cleared"));
                return;
            }

            // And last, the copy list, so a set gathered by mistake can be
            // abandoned without pasting it somewhere to get rid of it.
            if (m_fileOperations != nullptr && m_fileOperations->hasPendingClipboard()) {
                m_fileOperations->clearClipboard();
            }
        });

    m_registry->registerAction(QStringLiteral("quit"), tr("Quit Panefile"), ActionCategory::General,
                               [] { quit(); });

    m_registry->registerAction(QStringLiteral("toggle_sidebar"), tr("Show or hide the sidebar"),
                               ActionCategory::View, [this] { m_mainWindow->toggleSidebar(); });

    // Opening a place from the sidebar goes through the controller, so it obeys
    // the same placement rules as a path from the command line.
    connect(m_mainWindow->sidebar(), &ui::Sidebar::placeActivated, this,
            [this](const QString &path) { m_panelController->openPath(path, false); });

    connect(m_mainWindow->sidebar(), &ui::Sidebar::statusMessage, m_mainWindow.get(),
            &ui::MainWindow::showStatusMessage);
}

ui::ProcessBar *Application::processBar()
{
    if (m_processBar == nullptr) {
        m_processBar = new ui::ProcessBar(m_jobEngine.get(), m_mainWindow.get());
        connect(m_processBar, &ui::ProcessBar::becameIdle, m_mainWindow.get(),
                &ui::MainWindow::hideProcessBar);
        connect(m_processBar, &ui::ProcessBar::shouldAppear, m_mainWindow.get(),
                [this] { m_mainWindow->showProcessBar(m_processBar); });
    }
    return m_processBar;
}

ui::HelpModal *Application::helpModal()
{
    if (m_helpModal == nullptr) {
        // §3.4: created on first invocation, then cached. Parented to the
        // window, which owns it from here on.
        m_helpModal = new ui::HelpModal(*m_registry, *m_keymap, m_mainWindow.get());
    }
    return m_helpModal;
}

void Application::loadConfiguration()
{
    // §3.4 step 3: config.toml and theme.toml, and nothing else. hotkeys.toml
    // is deferred — the default keymap is already bound, so the first keypress
    // is never dropped while it loads.
    const config::ConfigLoadResult configResult =
        config::loadConfig(platform::configDir() + QStringLiteral("/config.toml"));
    m_settings = configResult.settings;
    m_configIssues = configResult.issues;

    const config::ThemeLoadResult themeResult =
        config::loadActiveTheme(platform::configDir() + QStringLiteral("/theme.toml"));
    m_configIssues += themeResult.issues;

    ui::setCurrentPalette(themeResult.theme);
    StartupTrace::mark(StartupPhase::ConfigLoaded);

    // §3.4 step 4: apply the stylesheet *before any widget is constructed*.
    // "Applying a stylesheet after widgets exist forces a full restyle pass
    // over the widget tree", and that pass is proportional to how much of the
    // application is already built.
    setStyleSheet(config::buildStyleSheet(themeResult.theme));

    if (!themeResult.theme.fontFamily.isEmpty()) {
        const QFont themeFont(themeResult.theme.fontFamily, themeResult.theme.fontSize);
        setFont(themeFont);
    }

    StartupTrace::mark(StartupPhase::StylesheetApplied);

    // Follow the desktop when it changes, not only when it starts.
    //
    // Only when the user has not chosen a theme themselves: a theme.toml is an
    // explicit decision, and overriding it every time the Mac flips to dark at
    // sunset would be the application arguing with its own configuration.
    //
    // Restyling after widgets exist is the expensive pass §3.4 warns about, but
    // this fires when the desktop's appearance changes — a handful of times a
    // day at most — rather than on the startup path it protects.
    if (!QFile::exists(platform::configDir() + QStringLiteral("/theme.toml"))) {
        connect(styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
            const config::Theme theme = config::defaultThemeForDesktop();
            ui::setCurrentPalette(theme);
            setStyleSheet(config::buildStyleSheet(theme));

            // A stylesheet change does not reach a QStyledItemDelegate, which
            // paints from the palette directly, so the rows would keep the old
            // colours until something else invalidated them.
            if (m_mainWindow != nullptr) {
                m_mainWindow->update();
                for (ui::FilePanel *panel : m_mainWindow->panelStrip()->panels()) {
                    panel->update();
                    panel->view()->viewport()->update();
                }
            }
        });
    }

    for (const config::ConfigIssue &issue : std::as_const(m_configIssues)) {
        qCWarning(pfConfig) << issue.toString();
    }
}

void Application::loadHotkeys()
{
    const config::HotkeysLoadResult result = config::loadHotkeys(
        platform::configDir() + QStringLiteral("/hotkeys.toml"), *m_keymap, &m_settings.keys);

    m_configIssues += result.issues;
    for (const config::ConfigIssue &issue : result.issues) {
        qCWarning(pfConfig) << issue.toString();
    }

    m_dispatcher->setSequenceTimeout(m_settings.keys.sequenceTimeoutMs);
    m_dispatcher->setAmbiguityTimeout(m_settings.keys.ambiguityTimeoutMs);

    // The help modal lists bindings, so a rebuilt keymap makes its contents
    // stale. Only refreshed if it has been opened — building it here would
    // undo the point of constructing it lazily.
    if (m_helpModal != nullptr) {
        m_helpModal->refresh();
    }
}

void Application::startWatchingConfig()
{
    m_configWatcher = std::make_unique<config::ConfigWatcher>(this);
    connect(m_configWatcher.get(), &config::ConfigWatcher::configChanged, this,
            &Application::reloadConfiguration);
    m_configWatcher->watchConfigDirectory(platform::configDir());
}

void Application::reloadConfiguration(const QStringList &changedFiles)
{
    // §8.3: hot-reload "including regenerating the stylesheet — without
    // restarting". Only what changed is reloaded: rebuilding the keymap because
    // a colour changed would discard a pending chord for no reason.
    const bool themeChanged = changedFiles.contains(QStringLiteral("theme.toml")) ||
                              changedFiles.contains(QStringLiteral("themes"));
    const bool hotkeysChanged = changedFiles.contains(QStringLiteral("hotkeys.toml"));
    const bool settingsChanged = changedFiles.contains(QStringLiteral("config.toml"));

    m_configIssues.clear();

    if (settingsChanged || themeChanged) {
        loadConfiguration();
        // The delegate reads the palette directly, so every panel has to be
        // told to repaint; a stylesheet change alone would not reach it.
        if (m_mainWindow != nullptr) {
            m_mainWindow->update();
            for (ui::FilePanel *panel : m_mainWindow->panelStrip()->panels()) {
                panel->refreshTheme();
            }
            if (m_quickLook != nullptr) {
                m_quickLook->applySettings(m_settings.quicklook);
            }
            if (m_search != nullptr) {
                m_search->setSettings(m_settings);
            }
        }
    }

    if (hotkeysChanged) {
        m_keymap->clear();
        input::installDefaultKeymap(*m_keymap);
        loadHotkeys();
    }

    if (m_mainWindow != nullptr) {
        m_mainWindow->showStatusMessage(m_configIssues.isEmpty()
                                            ? tr("Configuration reloaded")
                                            : tr("Configuration reloaded, with %n problem(s)",
                                                 nullptr, static_cast<int>(m_configIssues.size())));
    }
}

void Application::startUp(const CommandLineOptions &options)
{
    m_quitAfterPaint = options.quitAfterPaint;

    // First, before any of the work below. A session manager can send SIGTERM
    // at any moment, and until the handler is in place the default disposition
    // kills the process outright — during startup that costs nothing, but the
    // handler is cheap and the window where it is missing should be as small as
    // it can be.
    installSignalHandling();

    // Shutdown, in the one place that owns it.
    //
    // Both of these were written, tested and then never connected to anything.
    // saveSession() explained in its own comment that aboutToQuit "is what
    // writes the session" — and nothing did, so the session file kept whatever
    // was in it the last time something wrote one, and every navigation since
    // was lost. Reopening the application put you back in a directory you had
    // left days earlier.
    //
    // WorkerPools::drainAll() is worse: it exists to stop a scanner thread
    // reaching QMimeDatabase after static destruction has begun, which is a
    // crash on exit, and it was dead code. That the crash became rare is a
    // matter of timing, not of the fix being in place.
    //
    // Order matters. The session is written while the panels are still intact;
    // the pools are drained after, so no worker outlives the event loop.
    connect(this, &QCoreApplication::aboutToQuit, this, [this] {
        saveSession();
        WorkerPools::drainAll();
    });

    loadConfiguration();

    m_mainWindow = std::make_unique<ui::MainWindow>();
    StartupTrace::mark(StartupPhase::WindowConstructed);

    connect(m_mainWindow.get(), &ui::MainWindow::firstPaintCompleted, this,
            &Application::onFirstPaint);

    buildInputSystem();

    // §3.4 step 6: start the scan before show(). It runs on a worker thread, so
    // dispatching it first overlaps enumeration with window realisation instead
    // of starting after it.
    restoreSessionOrOpenInitialPanel(options);
    StartupTrace::mark(StartupPhase::ScanStarted);

    m_mainWindow->show();
    StartupTrace::mark(StartupPhase::Shown);

    // §7.7's cache settings, deferred for the same reason the cache itself is:
    // constructing it resolves the thumbnail directory, which reads XDG
    // configuration, and no thumbnail can be wanted before the first scan.
    postStartupTask([this] {
        ThumbnailCache::instance().setEnabled(m_settings.thumbnails.enabled);
        ThumbnailCache::instance().setMaxFileSizeMb(m_settings.thumbnails.maxFileSizeMb);
        ThumbnailCache::instance().setVideoEnabled(m_settings.thumbnails.video);
    });

    // §10.3: "listen() is two syscalls, so bind it before show(); only the
    // connection-handling wiring is deferred." The bind has to happen early
    // enough that a second launch during startup finds a socket to talk to.
    if (m_settings.general.singleInstance && !options.newInstance) {
        m_instance = std::make_unique<SingleInstance>(this);
        if (m_instance->listen(platform::singleInstanceSocketPath())) {
            connect(m_instance.get(), &SingleInstance::messageReceived, this,
                    &Application::openRequest);
            postStartupTask([this] { m_instance->startServing(); });
        } else {
            m_instance.reset();
        }
    }

    // §3.4's deferred list: the sidebar is constructed empty and populated on
    // idle, because resolving XDG user directories reads a config file and none
    // of it is needed to draw the first panel.
    postStartupTask([this] { m_mainWindow->sidebar()->populate(); });

    // §3.4's deferred list, in its order: hotkeys.toml is parsed after the
    // first paint, over the defaults already bound, so the very first keypress
    // is never dropped waiting for a file read.
    postStartupTask([this] { loadHotkeys(); });

    // "Hot reload is not needed in the first 50 ms."
    postStartupTask([this] { startWatchingConfig(); });

    // §8.3: a malformed config shows "a dismissible banner naming the file,
    // line and problem". Deferred, because a banner is not worth delaying the
    // first paint for, and the application is already running on defaults.
    if (!m_configIssues.isEmpty()) {
        postStartupTask([this] {
            m_mainWindow->showStatusMessage(tr("%n problem(s) in your configuration — see the log",
                                               nullptr, static_cast<int>(m_configIssues.size())));
        });
    }
}

void Application::restoreSessionOrOpenInitialPanel(const CommandLineOptions &options)
{
    ui::PanelStrip *strip = m_mainWindow->panelStrip();

    // A path on the command line is an instruction, and it outranks whatever
    // the last session happened to be showing.
    const bool hasPathArguments = !options.paths.isEmpty();

    if (m_settings.general.restoreSession && !hasPathArguments) {
        // §3.4: "Session restore of panels 2..N — panel 1 is enough to start
        // working; the rest fill in." Panel one is opened now; the others are
        // queued, so the first paint waits for one scan rather than for six.
        const Session session = Session::load().pruned();

        if (!session.isEmpty()) {
            const SessionPanel &first = session.panels.constFirst();
            if (ui::FilePanel *panel = strip->addPanel(first.path); panel != nullptr) {
                panel->setSortKey(sortKeyFromName(first.sortKey));
                panel->setReverseSort(first.reverseSort);
                panel->setShowHidden(first.showHidden);
                panel->setCursorName(first.cursorName);
            }

            if (!session.windowGeometry.isEmpty()) {
                m_mainWindow->setGeometry(session.windowGeometry);
            }
            if (session.windowMaximised) {
                m_mainWindow->showMaximized();
            }

            postStartupTask([this, session] {
                ui::PanelStrip *strip = m_mainWindow->panelStrip();
                for (qsizetype i = 1; i < session.panels.size(); ++i) {
                    const SessionPanel &saved = session.panels.at(i);
                    if (ui::FilePanel *panel = strip->addPanel(saved.path); panel != nullptr) {
                        panel->setSortKey(sortKeyFromName(saved.sortKey));
                        panel->setReverseSort(saved.reverseSort);
                        panel->setShowHidden(saved.showHidden);
                        panel->setCursorName(saved.cursorName);
                    }
                }
                strip->focusPanelAt(session.focusedPanel);
                m_mainWindow->sidebar()->setPinnedPaths(session.pinnedPaths);
            });
            return;
        }
    }

    ui::FilePanel *panel = strip->addPanel(initialPath(options));

    if (panel != nullptr && hasPathArguments) {
        // §10.2: "A path that is a file, not a directory, navigates to its
        // parent and places the cursor on it."
        const QFileInfo info(QDir::current().absoluteFilePath(options.paths.constFirst()));
        if (info.exists() && !info.isDir()) {
            panel->setCursorName(info.fileName());
        }
    }
}

namespace {

/// The write end of the self-pipe, read by the notifier below.
///
/// A file-scope int rather than a member, because a signal handler has no
/// `this` and cannot be given one. `volatile sig_atomic_t` is the only type the
/// standard promises is safe to touch from a handler.
volatile std::sig_atomic_t g_signalPipe = -1;

/// Everything a signal handler is allowed to do here.
///
/// write(2) is on the async-signal-safe list; almost nothing else is — not
/// malloc, not qDebug, and certainly not saving a session file. So the handler
/// writes one byte and returns, and the real work happens on the event loop.
extern "C" void onTerminationSignal(int number)
{
    const auto byte = static_cast<char>(number);
    const ssize_t written = ::write(g_signalPipe, &byte, 1);
    Q_UNUSED(written)
}

} // namespace

void Application::installSignalHandling()
{
    std::array<int, 2> fds{};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) != 0) {
        qCWarning(pfApp) << "could not create the signal pipe; SIGTERM will not save the session";
        return;
    }

    g_signalPipe = fds[0];

    auto *notifier = new QSocketNotifier(fds[1], QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [notifier](QSocketDescriptor fd) {
        char byte = 0;
        const ssize_t read = ::read(static_cast<int>(fd), &byte, 1);
        Q_UNUSED(read)

        notifier->setEnabled(false);
        qCDebug(pfApp) << "terminating on signal" << static_cast<int>(byte);

        // quit(), not exit(): this unwinds through aboutToQuit, which is what
        // writes the session.
        quit();
    });

    // The previous disposition is discarded deliberately. There is nothing
    // useful to do with it: these two are being taken over for the whole life
    // of the process, and a caller that had already installed a handler would
    // be a second owner of the same signal, which is not a situation to
    // negotiate at runtime.
    (void)std::signal(SIGTERM, &onTerminationSignal);
    (void)std::signal(SIGINT, &onTerminationSignal);
}

void Application::saveSession() const
{
    if (!m_settings.general.restoreSession || m_mainWindow == nullptr) {
        return;
    }

    Session session;
    session.windowGeometry = m_mainWindow->geometry();
    session.windowMaximised = m_mainWindow->isMaximized();
    session.focusedPanel = m_mainWindow->panelStrip()->focusedIndex();
    session.pinnedPaths = m_mainWindow->sidebar()->pinnedPaths();

    for (const ui::FilePanel *panel : m_mainWindow->panelStrip()->panels()) {
        session.panels.append(SessionPanel{.path = panel->path(),
                                           .cursorName = panel->cursorName(),
                                           .sortKey = sortKeyName(panel->sortKey()),
                                           .reverseSort = panel->reverseSort(),
                                           .showHidden = panel->showHidden()});
    }

    session.save();
}

void Application::raiseWindow(const QString &activationToken)
{
    if (m_mainWindow == nullptr) {
        return;
    }

    // §10.4: "The running instance sets that value into its own environment
    // with qputenv immediately before calling requestActivate(); Qt's Wayland
    // platform plugin picks it up from there."
    if (!activationToken.isEmpty()) {
        qputenv("XDG_ACTIVATION_TOKEN", activationToken.toUtf8());
    }

    m_mainWindow->show();
    m_mainWindow->raise();

    if (QWindow *window = m_mainWindow->windowHandle(); window != nullptr) {
        window->requestActivate();
    }

    // §10.4: "If no token is available, degrade gracefully: update the window's
    // state so the compositor shows an attention hint, and do not attempt to
    // force focus. Never treat failure to raise as a fatal error."
    if (activationToken.isEmpty()) {
        alert(m_mainWindow.get());
    }

    if (!activationToken.isEmpty()) {
        // Single-use: leaving it set would have the next activation present a
        // token the compositor has already spent, which it rejects silently.
        qunsetenv("XDG_ACTIVATION_TOKEN");
    }
}

void Application::openRequest(const InstanceMessage &message)
{
    if (m_mainWindow == nullptr) {
        return;
    }

    const QStringList paths = message.absolutePaths();

    QStringList missing;
    QStringList usable;
    for (const QString &path : paths) {
        if (QFileInfo::exists(path)) {
            usable.append(path);
        } else {
            missing.append(path);
        }
    }

    // §10.2: "the running instance's own window is active". The instance
    // decides; the client never tries to inspect focus, which is not possible
    // on Wayland anyway.
    const bool windowFocused =
        applicationState() == Qt::ApplicationActive && m_mainWindow->isActiveWindow();

    bool useFocusedPanel = windowFocused;
    switch (message.placement) {
    case PlacementOverride::Here:
        useFocusedPanel = true;
        break;
    case PlacementOverride::NewPanel:
    case PlacementOverride::NewWindow:
        useFocusedPanel = false;
        break;
    case PlacementOverride::None:
        break;
    }

    ui::PanelStrip *strip = m_mainWindow->panelStrip();

    for (qsizetype i = 0; i < usable.size(); ++i) {
        const QFileInfo info(usable.at(i));
        const QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();

        // §10.2: "With multiple paths, the first follows the table above and
        // each subsequent one always opens a new panel."
        ui::FilePanel *panel = nullptr;
        if (i == 0 && useFocusedPanel) {
            panel = strip->focusedPanel();
            if (panel != nullptr) {
                // §10.2: "Navigating the focused panel pushes history, so
                // go_back returns to where the user was. It never discards
                // their selection silently."
                if (panel->selectionCount() > 0) {
                    panel->clearSelection();
                    m_mainWindow->showStatusMessage(tr("Selection cleared"));
                }
                panel->navigateTo(directory);
            }
        } else {
            panel = strip->addPanel(directory);
            if (panel == nullptr) {
                // §10.2: "beyond that, extra paths are dropped with a footer
                // warning."
                m_mainWindow->showStatusMessage(tr("At most %1 panels — %n path(s) not opened",
                                                   nullptr, static_cast<int>(usable.size() - i))
                                                    .arg(ui::PanelStrip::kMaxPanels));
                break;
            }
        }

        if (panel != nullptr && !info.isDir()) {
            panel->setCursorName(info.fileName());
        }
    }

    if (!missing.isEmpty()) {
        // §10.2: "If some of several paths are bad, open the good ones and
        // report the rest."
        m_mainWindow->showStatusMessage(
            tr("%1 does not exist").arg(QDir::toNativeSeparators(missing.constFirst())));
    }

    // §10.2: the window is raised when the request did not come from it.
    if (!windowFocused) {
        raiseWindow(message.activationToken);
    }
}

bool Application::notify(QObject *receiver, QEvent *event)
{
    if (event->type() == QEvent::KeyPress && m_dispatcher != nullptr) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);

        // §6.2 step 2: while a text input has focus, bare printable keys belong
        // to the widget. Only modified chords and the confirm/cancel keys are
        // considered, so typing a filename cannot trigger `d d`.
        auto *focus = focusWidget();
        const bool typing =
            focus != nullptr && focus->inherits("QLineEdit") &&
            !(keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));

        // §7.6's per-renderer keys — `+`/`-`/`0`, `[`/`]`, `/` — are offered to
        // the renderer before the dispatcher. Movement keys are never consumed
        // there, which is what keeps "the arrow keys still move the panel
        // cursor" true while a preview is open.
        if (!typing && m_quickLook != nullptr && m_quickLook->handleKey(keyEvent)) {
            return true;
        }

        if (!typing && m_dispatcher->handleKeyPress(keyEvent)) {
            return true;
        }
    }

    // §6.2 step 5: a pointer press clears the pending buffer. A half-typed
    // sequence completing after a click would act on a panel the user is no
    // longer looking at.
    if (event->type() == QEvent::MouseButtonPress && m_dispatcher != nullptr) {
        m_dispatcher->clearPending();
    }

    return QApplication::notify(receiver, event);
}

void Application::postStartupTask(std::function<void()> task)
{
    m_startupTasks.push_back(std::move(task));
    if (m_firstPaintSeen) {
        QTimer::singleShot(0, this, &Application::runNextStartupTask);
    }
}

void Application::onFirstPaint()
{
    if (m_firstPaintSeen) {
        return;
    }
    m_firstPaintSeen = true;

    StartupTrace::dump();

    if (m_quitAfterPaint) {
        // The paint has completed and been measured; leaving through the event
        // loop keeps shutdown identical to a normal quit.
        quit();
        return;
    }

    QTimer::singleShot(0, this, &Application::runNextStartupTask);
}

void Application::runNextStartupTask()
{
    if (m_nextStartupTask >= m_startupTasks.size()) {
        return;
    }

    // One item per event loop turn, so a slow task delays the next task rather
    // than the user's next keypress (§3.4).
    auto task = m_startupTasks[m_nextStartupTask++];
    if (task) {
        task();
    }

    if (m_nextStartupTask < m_startupTasks.size()) {
        QTimer::singleShot(0, this, &Application::runNextStartupTask);
    }
}

} // namespace pf
