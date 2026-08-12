#include "app/Application.h"

#include "input/ActionRegistry.h"
#include "input/DefaultKeymap.h"
#include "input/Keymap.h"
#include "app/CommandLine.h"
#include "app/KeyDispatcher.h"
#include "app/PanelController.h"
#include "config/Config.h"
#include "config/ConfigWatcher.h"
#include "config/Hotkeys.h"
#include "config/StyleSheetBuilder.h"
#include "config/Theme.h"
#include "core/Logging.h"
#include "core/StartupTrace.h"
#include "core/Version.h"
#include "platform/Paths.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/Sidebar.h"
#include "ui/ThemePalette.h"
#include "ui/modals/HelpModal.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QKeyEvent>
#include <QTimer>

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

    m_panelController->registerActions();
    registerGlobalActions();
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

    m_registry->registerAction(QStringLiteral("quit"), tr("Close the modal, or quit"),
                               ActionCategory::General, [this] {
                                   if (m_helpModal != nullptr && m_helpModal->isVisible()) {
                                       m_helpModal->dismiss();
                                       return;
                                   }
                                   quit();
                               });

    m_registry->registerAction(QStringLiteral("toggle_sidebar"), tr("Show or hide the sidebar"),
                               ActionCategory::View, [this] { m_mainWindow->toggleSidebar(); });

    // Opening a place from the sidebar goes through the controller, so it obeys
    // the same placement rules as a path from the command line.
    connect(m_mainWindow->sidebar(), &ui::Sidebar::placeActivated, this,
            [this](const QString &path) { m_panelController->openPath(path, false); });
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

    loadConfiguration();

    m_mainWindow = std::make_unique<ui::MainWindow>();
    StartupTrace::mark(StartupPhase::WindowConstructed);

    connect(m_mainWindow.get(), &ui::MainWindow::firstPaintCompleted, this,
            &Application::onFirstPaint);

    buildInputSystem();

    // §3.4 step 6: start the scan before show(). It runs on a worker thread, so
    // dispatching it first overlaps enumeration with window realisation instead
    // of starting after it.
    const QString path = initialPath(options);
    ui::FilePanel *panel = m_mainWindow->panelStrip()->addPanel(path);
    StartupTrace::mark(StartupPhase::ScanStarted);

    if (panel != nullptr && !options.paths.isEmpty()) {
        const QFileInfo info(QDir::current().absoluteFilePath(options.paths.constFirst()));
        if (info.exists() && !info.isDir()) {
            panel->setCursorName(info.fileName());
        }
    }

    m_mainWindow->show();
    StartupTrace::mark(StartupPhase::Shown);

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
