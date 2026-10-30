#include "app/Application.h"
#include "core/Format.h"

#include "input/ActionRegistry.h"
#include "input/Chord.h"
#include "input/DefaultKeymap.h"
#include "input/Keymap.h"
#include "app/CommandLine.h"
#include "app/DefaultFileManagerOffer.h"
#include "app/FileOperations.h"
#include "app/KeyDispatcher.h"
#include "app/PanelController.h"
#include "app/QuickLookController.h"
#include "app/SearchController.h"
#include "app/Session.h"
#include "app/SingleInstance.h"
#include "app/TextInput.h"
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
#include "model/SymbolicIcon.h"
#include "model/ThumbnailCache.h"
#include "platform/FileManagerService.h"
#include "platform/Launcher.h"
#include "platform/Paths.h"
#include "ui/DefaultFileManagerBar.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanefileStyle.h"
#include "ui/PanelStrip.h"
#include "ui/PanelView.h"
#include "ui/ProcessBar.h"
#include "ui/Sidebar.h"
#include "ui/ThemePalette.h"
#include "ui/modals/CommandPaletteModal.h"
#include "ui/modals/ConfirmModal.h"
#include "ui/modals/HelpModal.h"
#include "ui/modals/InputModal.h"
#include "ui/modals/SettingsWindow.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QKeyEvent>
#include <QMenu>
#include <QProcess>
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

    // Press `g` and pause: the window lists what can follow it.
    connect(m_dispatcher.get(), &KeyDispatcher::hintRequested, m_mainWindow.get(),
            [this](const QString &pending, const QList<input::Keymap::Continuation> &next) {
                QList<QPair<QString, QString>> rows;
                for (const input::Keymap::Continuation &continuation : next) {
                    const input::Action *action = m_registry->find(continuation.actionId);
                    const QPair<QString, QString> row{
                        input::bindingToString(continuation.remaining),
                        action != nullptr ? action->description : continuation.actionId};
                    if (!rows.contains(row)) {
                        rows.append(row);
                    }
                }
                m_mainWindow->showChordHint(pending, rows);
            });

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
    if (m_mainWindow != nullptr) {
        const ui::FilePanel *focused = m_mainWindow->panelStrip()->focusedPanel();
        m_mainWindow->setSelectionCount(focused != nullptr ? focused->selectionCount() : 0);
    }

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

void Application::configurePanel(ui::FilePanel *panel)
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

    // Enter or a double-click on a file opens it with the desktop's default
    // application — the same as open_with_default_app, which is what anyone
    // pressing Enter on a video expects.
    //
    // The panel has always emitted this; nothing was listening, so Enter and
    // double-click on a file silently did nothing.
    connect(panel, &ui::FilePanel::fileActivated, this, [this](const QString &path) {
        if (!platform::Launcher::openWithDefaultApplication(path)) {
            m_mainWindow->showStatusMessage(
                tr("Could not open %1").arg(QFileInfo(path).fileName()));
        }
    });

    // Right-clicking a file opens the palette, whose first rows are that file's actions.
    connect(panel, &ui::FilePanel::contextMenuRequested, this,
            [this, panel] { openPaletteFromPanel(panel); });

    // The name being typed shows where a half-typed chord does.
    connect(panel->view(), &ui::PanelView::typeAheadChanged, m_mainWindow.get(),
            [window = m_mainWindow.get()](const QString &text) {
                window->showPendingKeys(text.isEmpty() ? QString()
                                                       : QStringLiteral("“%1”").arg(text));
            });

    panel->setShowHidden(m_settings.panels.showHidden);
    panel->setDefaultSortKey(sortKeyFromName(m_settings.panels.defaultSort));

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

    m_registry->registerAction(QStringLiteral("toggle_theme_dark_light"),
                               tr("Switch between the light and dark theme"), ActionCategory::View,
                               [this] { toggleLightDark(); });

    m_registry->registerAction(QStringLiteral("focus_on_process_bar"),
                               tr("Focus the running-jobs panel"), ActionCategory::View,
                               [this] { focusProcessBar(); });

    m_registry->registerAction(QStringLiteral("open_command_line"),
                               tr("Run a shell command in this directory"), ActionCategory::General,
                               [this] { promptForShellCommand(); });

    m_registry->registerAction(QStringLiteral("open_settings"), tr("Settings"),
                               ActionCategory::General, [this] { settingsWindow()->present(); });

    m_registry->registerAction(QStringLiteral("open_panefile_prompt"),
                               tr("Open the command palette"), ActionCategory::General,
                               [this] { promptForAction(); });

    // Opening a place from the sidebar goes through the controller, so it obeys
    // the same placement rules as a path from the command line.
    connect(m_mainWindow->sidebar(), &ui::Sidebar::placeActivated, this,
            [this](const QString &path) { m_panelController->openPath(path, false); });

    connect(m_mainWindow->sidebar(), &ui::Sidebar::statusMessage, m_mainWindow.get(),
            &ui::MainWindow::showStatusMessage);

    connect(m_mainWindow->sidebar(), &ui::Sidebar::menuRequested, this,
            &Application::showApplicationMenu);

    // Written when they change rather than only at quit, so a crash or a kill
    // does not lose a sidebar the user has just arranged.
    //
    // To its own file, whatever the session setting: the arrangement is the
    // user's sidebar, not part of the last session's panels.
    connect(m_mainWindow->sidebar(), &ui::Sidebar::layoutChanged, this,
            [this] { m_mainWindow->sidebar()->sidebarLayout().save(); });

    connect(m_mainWindow->sidebar(), &ui::Sidebar::revealRequested, this,
            [this](const QString &path) { m_panelController->revealPath(path); });

    connect(m_mainWindow->sidebar(), &ui::Sidebar::sectionRemovalRequested, this,
            [this](const QString &id, const QString &title, int count) {
                if (m_sectionConfirm == nullptr) {
                    m_sectionConfirm = new ui::ConfirmModal(m_mainWindow.get());
                }
                m_sectionConfirm->ask(
                    tr("Remove the section “%1” and its %n item(s)?", nullptr, count).arg(title),
                    tr("The files themselves are not touched."), tr("Remove"),
                    [this, id] { m_mainWindow->sidebar()->removeSection(id); });
            });

    connect(m_mainWindow->sidebar(), &ui::Sidebar::newSectionRequested, this,
            [this](const QString &moveEntryPath) {
                ui::InputModal *modal = sectionPromptModal();
                m_sectionPromptRename.clear();
                m_sectionPromptMovePath = moveEntryPath;
                modal->ask(tr("New section"), tr("A name for the section"));
            });
    connect(m_mainWindow->sidebar(), &ui::Sidebar::renameSectionRequested, this,
            [this](const QString &id, const QString &title) {
                ui::InputModal *modal = sectionPromptModal();
                m_sectionPromptRename = id;
                m_sectionPromptMovePath.clear();
                modal->ask(tr("Rename section"), tr("A new name for the section"), title, 0,
                           static_cast<int>(title.size()));
            });
}

void Application::showApplicationMenu(const QPoint &globalPosition)
{
    // The application menu: the commonest actions, each with the key that does
    // it, so the menu teaches the keyboard as much as it stands in for it.
    // Every item goes through the registry, the one dispatch path, so a menu
    // click and a keypress cannot behave differently.
    struct Item {
        const char *actionId;
        QString label;
    };
    const QList<QList<Item>> groups{
        {{.actionId = "create_new_file_panel", .label = tr("New Panel")},
         {.actionId = "split_file_panel", .label = tr("Duplicate Panel")},
         {.actionId = "close_file_panel", .label = tr("Close Panel")}},
        {{.actionId = "confirm", .label = tr("Open")},
         {.actionId = "go_back", .label = tr("Back")},
         {.actionId = "parent_directory", .label = tr("Enclosing Folder")},
         {.actionId = "go_home", .label = tr("Home")},
         {.actionId = "open_fuzzy_find", .label = tr("Find…")}},
        {{.actionId = "toggle_dot_file", .label = tr("Show Hidden Files")},
         {.actionId = "toggle_sidebar", .label = tr("Sidebar")},
         {.actionId = "sidebar_new_section", .label = tr("New Sidebar Section…")},
         {.actionId = "toggle_footer", .label = tr("Status Bar")},
         {.actionId = "toggle_theme_dark_light", .label = tr("Light or Dark")}},
        {{.actionId = "open_settings", .label = tr("Settings…")},
         {.actionId = "open_help_menu", .label = tr("Keyboard Shortcuts")}},
        {{.actionId = "quit", .label = tr("Quit")}},
    };

    const auto keyFor = [this](const QString &actionId) { return bindingTextFor(actionId); };

    QMenu menu(m_mainWindow.get());
    for (qsizetype group = 0; group < groups.size(); ++group) {
        if (group > 0) {
            menu.addSeparator();
        }
        for (const Item &item : groups[group]) {
            const QString actionId = QLatin1String(item.actionId);
            if (m_registry->find(actionId) == nullptr) {
                continue;
            }
            // The text after a tab is QMenu's shortcut column. Display only:
            // no QAction shortcut is set, because §6.2 keeps every key binding
            // in the dispatcher.
            const QString key = keyFor(actionId);
            const QAction *action =
                menu.addAction(key.isEmpty() ? item.label : item.label + QLatin1Char('\t') + key);
            connect(action, &QAction::triggered, this,
                    [this, actionId] { m_registry->invoke(actionId); });
        }
    }
    menu.exec(globalPosition);
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
    QApplication::setKeyboardInputInterval(m_settings.keys.typeAheadTimeoutMs);

    const config::ThemeLoadResult themeResult =
        config::loadActiveTheme(platform::configDir() + QStringLiteral("/theme.toml"));
    m_configIssues += themeResult.issues;

    ui::setCurrentPalette(themeResult.theme);
    StartupTrace::mark(StartupPhase::ConfigLoaded);

    // §3.4 step 4: apply the stylesheet *before any widget is constructed*.
    // "Applying a stylesheet after widgets exist forces a full restyle pass
    // over the widget tree", and that pass is proportional to how much of the
    // application is already built.
    //
    // The proxy style first, because the stylesheet wraps whatever style is
    // installed when it is set; see ui::PanefileStyle for what it draws.
    // Once: a hot reload comes back through here, and replacing the style
    // would restyle every widget for nothing.
    //
    // Owned by the application from setStyle() on, which the analyser cannot
    // see.
    // NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)
    if (!m_styleInstalled) {
        setStyle(new ui::PanefileStyle);
        m_styleInstalled = true;
    }
    // NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)
    setStyleSheet(config::buildStyleSheet(themeResult.theme));

    // Always, not only when the theme names a family.
    //
    // The stylesheet carries font-size too, but a QStyledItemDelegate paints
    // from QStyleOptionViewItem::font — which comes from the widget, not from
    // the stylesheet cascade — so row names were rendering at the platform
    // default while everything around them followed the theme. Setting the
    // application font makes the delegate agree with the chrome.
    setFont(config::applicationFont(themeResult.theme, font()));

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
        connect(styleHints(), &QStyleHints::colorSchemeChanged, this,
                [this] { applyTheme(config::defaultThemeForDesktop()); });
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
    // Qt's own setting for how long typed letters run together, so the
    // sidebar's built-in type-ahead and the panels' agree.
    QApplication::setKeyboardInputInterval(m_settings.keys.typeAheadTimeoutMs);

    updateDefaultFileManagerKeyHints();

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
            m_mainWindow->sidebar()->refreshTheme();
            if (m_quickLook != nullptr) {
                m_quickLook->applySettings(m_settings.quicklook);
            }
            if (m_search != nullptr) {
                m_search->setSettings(m_settings);
            }
        }
        if (m_defaultOffer != nullptr) {
            m_defaultOffer->setOptedIn(m_settings.general.offerDefaultFileManager);
        }
    }

    if (hotkeysChanged) {
        m_keymap->clear();
        input::installDefaultKeymap(*m_keymap);
        loadHotkeys();
    }

    if (m_mainWindow != nullptr) {
        m_mainWindow->showStatusMessage(
            m_configIssues.isEmpty() ? tr("Configuration reloaded")
                                     : tr("Configuration reloaded, with %1")
                                           .arg(counted(static_cast<int>(m_configIssues.size()),
                                                        tr("problem"), tr("problems"))));
    }
}

void Application::startUp(const CommandLineOptions &options)
{
    m_quitAfterPaint = options.quitAfterPaint;
    m_dbusService = options.dbusService;

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

    // --dbus-service has nothing to show until a FileManager1 call says what:
    // no panel, no window, and no session restored underneath the folder the
    // caller asked for.
    if (!m_dbusService) {
        // §3.4 step 6: start the scan before show(). It runs on a worker
        // thread, so dispatching it first overlaps enumeration with window
        // realisation instead of starting after it.
        restoreSessionOrOpenInitialPanel(options);
        StartupTrace::mark(StartupPhase::ScanStarted);

        m_mainWindow->show();
        StartupTrace::mark(StartupPhase::Shown);
    }

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
            // Panefile is already running. A bus-started process hands the
            // calls to it rather than opening a second window.
            m_forwardToRunningInstance = m_dbusService;
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

    // Last, and off the GUI thread once it runs: finding the default file
    // manager reads a dozen files, none of which the first paint needs.
    if (m_dbusService) {
        startAsDbusService();
    } else {
        postStartupTask([this] {
            defaultFileManagerOffer()->start(m_settings.general.offerDefaultFileManager,
                                             DefaultFileManagerOffer::isDbusActivated(false),
                                             m_instance != nullptr);
        });
    }

    // §8.3: a malformed config shows "a dismissible banner naming the file,
    // line and problem". Deferred, because a banner is not worth delaying the
    // first paint for, and the application is already running on defaults.
    if (!m_configIssues.isEmpty()) {
        postStartupTask([this] {
            m_mainWindow->showStatusMessage(
                tr("%1 in your configuration — see the log")
                    .arg(counted(static_cast<int>(m_configIssues.size()), tr("problem"),
                                 tr("problems"))));
        });
    }
}

void Application::restoreSessionOrOpenInitialPanel(const CommandLineOptions &options)
{
    ui::PanelStrip *strip = m_mainWindow->panelStrip();

    // A path on the command line is an instruction, and it outranks whatever
    // the last session happened to be showing.
    const bool hasPathArguments = !options.paths.isEmpty();

    // The sidebar's arrangement, always — it is the user's sidebar, not part of
    // the last session's panels. The first run after upgrading builds it from
    // the pins and removed places the session file used to hold.
    m_mainWindow->sidebar()->setLayout(ui::SidebarLayout::load());

    if (m_settings.general.restoreSession && !hasPathArguments) {
        // §3.4: "Session restore of panels 2..N — panel 1 is enough to start
        // working; the rest fill in." Panel one is opened now; the others are
        // queued, so the first paint waits for one scan rather than for six.
        const Session session = Session::load().pruned();

        if (!session.isEmpty()) {
            const SessionPanel &first = session.panels.constFirst();
            if (ui::FilePanel *panel = strip->addPanel(first.path); panel != nullptr) {
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
                        panel->setShowHidden(saved.showHidden);
                        panel->setCursorName(saved.cursorName);
                    }
                }
                strip->focusPanelAt(session.focusedPanel);
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

/// The glyph a palette row wears: one for the action itself where there is a
/// good one, else one for its category.
QString paletteGlyph(const QString &id, input::ActionCategory category)
{
    struct Entry {
        const char *id;
        const char *glyph;
    };
    static constexpr std::array<Entry, 20> exact{{
        {"rotate_image_left", "arrow-uturn-left"},
        {"rotate_image_right", "arrow-uturn-right"},
        {"flip_image_horizontally", "arrows-right-left"},
        {"flip_image_vertically", "arrows-up-down"},
        {"compress_file", "archive-box"},
        {"extract_file", "archive-box"},
        {"copy_items", "document-duplicate"},
        {"copy_path", "document-duplicate"},
        {"cut_items", "scissors"},
        {"paste_items", "clipboard-document"},
        {"delete_items", "trash"},
        {"permanently_delete_items", "trash"},
        {"file_panel_item_rename", "pencil-square"},
        {"bulk_rename", "pencil-square"},
        {"open_with_default_app", "arrow-top-right-on-square"},
        {"open_file_with_editor", "code-bracket"},
        {"open_fuzzy_find", "magnifying-glass"},
        {"open_settings", "cog-6-tooth"},
        {"convert_", "arrow-path"},
        {"extract_audio_", "arrow-path"},
    }};
    QString glyph;
    for (const Entry &entry : exact) {
        const QLatin1String key(entry.id);
        const bool prefix = key.endsWith(QLatin1Char('_'));
        if (prefix ? id.startsWith(key) : id == key) {
            glyph = QLatin1String(entry.glyph);
            break;
        }
    }
    if (glyph.isEmpty() || !SymbolicIcon::exists(glyph)) {
        switch (category) {
        case input::ActionCategory::General:
            return QStringLiteral("bolt");
        case input::ActionCategory::Panels:
            return QStringLiteral("view-columns");
        case input::ActionCategory::Selection:
            return QStringLiteral("check-circle");
        case input::ActionCategory::FileOperations:
            return QStringLiteral("document-text");
        case input::ActionCategory::View:
            return QStringLiteral("eye");
        case input::ActionCategory::Movement:
            return QStringLiteral("chevron-right");
        }
    }
    return glyph;
}


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

ui::SettingsWindow *Application::settingsWindow()
{
    if (m_settingsWindow == nullptr) {
        m_settingsWindow =
            new ui::SettingsWindow(m_registry.get(), m_keymap.get(), m_mainWindow.get());

        // Live preview: a theme is judged by looking at it, so it changes under
        // the pointer rather than when the window closes. An empty name means
        // "go back to following the desktop".
        connect(m_settingsWindow, &ui::SettingsWindow::themePreviewRequested, this,
                [this](const QString &name) {
                    const config::Theme theme = name.isEmpty()
                                                    ? config::defaultThemeForDesktop()
                                                    : config::loadThemeByName(name).theme;
                    applyTheme(theme);
                });

        connect(m_settingsWindow, &ui::SettingsWindow::settingsChanged, m_mainWindow.get(),
                &ui::MainWindow::showStatusMessage);

        // The same code as the bar's Yes, so the two cannot drift apart.
        connect(m_settingsWindow, &ui::SettingsWindow::makeDefaultFileManagerRequested, this,
                [this] { defaultFileManagerOffer()->makeDefault(); });
    }
    return m_settingsWindow;
}

ui::InputModal *Application::shellPromptModal()
{
    if (m_shellPrompt == nullptr) {
        m_shellPrompt = new ui::InputModal(m_mainWindow.get());
        connect(m_shellPrompt, &ui::InputModal::submitted, this, [this](const QString &command) {
            const ui::FilePanel *panel = m_mainWindow->panelStrip()->focusedPanel();
            if (panel == nullptr || command.trimmed().isEmpty()) {
                return;
            }

            // Through a shell, because the whole point of a command line is
            // that pipes, redirection and globs work. Detached, because
            // Panefile is not a terminal and has nowhere to show the output —
            // a command that wants to be watched belongs in a terminal.
            const QString shell = qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
            if (QProcess::startDetached(shell, {QStringLiteral("-c"), command}, panel->path())) {
                m_mainWindow->showStatusMessage(tr("Running: %1").arg(command));
            } else {
                m_mainWindow->showStatusMessage(tr("Could not run: %1").arg(command));
            }
        });
    }
    return m_shellPrompt;
}

ui::InputModal *Application::sectionPromptModal()
{
    if (m_sectionPrompt == nullptr) {
        m_sectionPrompt = new ui::InputModal(m_mainWindow.get());
        connect(m_sectionPrompt, &ui::InputModal::submitted, this, [this](const QString &title) {
            ui::Sidebar *sidebar = m_mainWindow->sidebar();
            if (!m_sectionPromptRename.isEmpty()) {
                sidebar->renameSection(m_sectionPromptRename, title);
            } else if (!m_sectionPromptMovePath.isEmpty()) {
                sidebar->addSectionWith(title, m_sectionPromptMovePath);
            } else {
                sidebar->addSection(title);
            }
        });
    }
    return m_sectionPrompt;
}

void Application::toggleLightDark()
{
    // Swap to the counterpart of whatever is showing, and write it to
    // theme.toml — otherwise the next desktop appearance change would undo it,
    // since following the desktop is only the behaviour while the user has not
    // chosen. Pressing this key *is* choosing.
    const bool dark = !ui::currentPalette().isLight();
    const QString name = dark ? QStringLiteral("panefile-light") : QStringLiteral("panefile-dark");

    const config::ThemeLoadResult loaded = config::loadThemeByName(name);
    if (!loaded.issues.isEmpty()) {
        m_mainWindow->showStatusMessage(tr("Could not load the %1 theme").arg(name));
        return;
    }

    applyTheme(loaded.theme);

    const QString path = platform::configDir() + QStringLiteral("/theme.toml");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(QStringLiteral("# Written by toggle_theme_dark_light. Delete this file to go\n"
                                  "# back to following the desktop's own light/dark setting.\n"
                                  "name = \"%1\"\n")
                       .arg(name)
                       .toUtf8());
    }

    m_mainWindow->showStatusMessage(tr("Theme: %1").arg(loaded.theme.name));
}

void Application::applyTheme(const config::Theme &theme)
{
    ui::setCurrentPalette(theme);
    setStyleSheet(config::buildStyleSheet(theme));

    setFont(config::applicationFont(theme, font()));

    // A stylesheet change does not reach a QStyledItemDelegate, which paints
    // from the palette directly, nor the header path, which carries its
    // colours in its own markup — so each panel is told.
    if (m_mainWindow != nullptr) {
        m_mainWindow->update();
        for (ui::FilePanel *panel : m_mainWindow->panelStrip()->panels()) {
            panel->refreshTheme();
        }
        // Its icons are tinted pixmaps, which a stylesheet cannot recolour.
        m_mainWindow->sidebar()->refreshTheme();
    }
}

void Application::focusProcessBar()
{
    // Constructing it here would put an empty jobs list on screen for a user who
    // has never run one, so this reports rather than conjures.
    if (m_processBar == nullptr) {
        m_mainWindow->showStatusMessage(tr("No jobs have run yet"));
        return;
    }
    m_mainWindow->showProcessBar(m_processBar);
    m_processBar->setExpanded(true);
    m_processBar->setFocus(Qt::OtherFocusReason);
}

void Application::promptForShellCommand()
{
    const ui::FilePanel *panel = m_mainWindow->panelStrip()->focusedPanel();
    if (panel == nullptr) {
        return;
    }
    const QString directory = panel->path();

    ui::InputModal *modal = shellPromptModal();
    modal->ask(tr("Run a command"), tr("Runs in %1").arg(QDir(directory).dirName()));
}

QString Application::bindingTextFor(const QString &actionId) const
{
    // The first global binding, else the first Normal one: the one that works
    // wherever the focus is, when there is one.
    for (const input::KeymapLayer layer :
         {input::KeymapLayer::Global, input::KeymapLayer::Normal}) {
        const QList<input::Binding> bindings = m_keymap->bindingsFor(layer, actionId);
        if (!bindings.isEmpty()) {
            return input::bindingToString(bindings.first());
        }
    }
    return {};
}

ui::CommandPaletteModal *Application::commandPalette()
{
    if (m_palette == nullptr) {
        // §3.4: on first use. Nothing in it is needed to draw a panel.
        m_palette = new ui::CommandPaletteModal(m_mainWindow.get());
        connect(m_palette, &ui::CommandPaletteModal::actionChosen, this, [this](const QString &id) {
            if (!m_registry->contains(id)) {
                m_mainWindow->showStatusMessage(tr("No such action: %1").arg(id));
                return;
            }
            if (!m_registry->invoke(id)) {
                m_mainWindow->showStatusMessage(tr("%1 is not available right now").arg(id));
            }
        });
        connect(m_palette, &ui::CommandPaletteModal::placeChosen, this,
                [this](const QString &path) {
                    if (QFileInfo(path).isDir()) {
                        m_panelController->openPath(path, false);
                    } else {
                        m_panelController->revealPath(path);
                    }
                });
    }
    return m_palette;
}

void Application::promptForAction()
{
    using ui::PaletteItem;
    QList<PaletteItem> items;

    // Plumbing that only makes sense inside a modal, or that a palette cannot
    // usefully run: bound only in the Modal layer, or movement.
    const auto isPlumbing = [this](const input::Action &action) {
        if (action.category == input::ActionCategory::Movement ||
            action.id == QLatin1String("open_panefile_prompt")) {
            return true;
        }
        const bool inModal = !m_keymap->bindingsFor(input::KeymapLayer::Modal, action.id).isEmpty();
        const bool elsewhere =
            !m_keymap->bindingsFor(input::KeymapLayer::Global, action.id).isEmpty() ||
            !m_keymap->bindingsFor(input::KeymapLayer::Normal, action.id).isEmpty() ||
            !m_keymap->bindingsFor(input::KeymapLayer::Selection, action.id).isEmpty();
        return inModal && !elsewhere;
    };

    for (const QString &id : m_registry->ids()) {
        const input::Action *action = m_registry->find(id);
        if (action == nullptr || isPlumbing(*action) || !m_registry->isEnabled(id)) {
            continue;
        }
        PaletteItem item{.id = id,
                         .title = action->description,
                         .detail = {},
                         .keys = bindingTextFor(id),
                         .group = 1,
                         .path = {},
                         .icon = paletteGlyph(id, action->category)};
        if (action->contextual) {
            item.group = 0;
            item.detail = action->contextLabel ? action->contextLabel() : QString();
        } else {
            item.detail = input::ActionRegistry::categoryTitle(action->category);
        }
        items.append(item);
    }

    const QString home = QDir::homePath();
    for (const ui::SidebarSection &section : m_mainWindow->sidebar()->sidebarLayout().sections) {
        for (const ui::SidebarEntry &entry : section.entries) {
            const QString path = entry.resolvedPath();
            if (path.isEmpty() || !QFileInfo::exists(path)) {
                continue;
            }
            QString shown = QDir::toNativeSeparators(path);
            if (path == home || path.startsWith(home + QLatin1Char('/'))) {
                shown = QStringLiteral("~") + path.mid(home.size());
            }
            const QString name = path == home ? tr("Home") : QFileInfo(path).fileName();
            items.append(PaletteItem{
                .id = {},
                .title = name,
                .detail = shown,
                .keys = {},
                .group = 2,
                .path = path,
                .icon = ui::Sidebar::iconNameForEntry(path)});
        }
    }

    commandPalette()->open(items);
}

void Application::openPaletteFromPanel(ui::FilePanel *panel)
{
    // Focus first, so the contextual actions and their targets are this panel's.
    m_mainWindow->panelStrip()->setFocusedPanel(panel);
    promptForAction();
}

void Application::saveSession() const
{
    if (!m_settings.general.restoreSession || m_mainWindow == nullptr) {
        return;
    }

    // A --dbus-service process that was never asked for a window has no
    // panels, and writing that would replace the user's real session with an
    // empty one.
    if (m_mainWindow->panelStrip()->panels().isEmpty()) {
        return;
    }

    Session session;
    session.windowGeometry = m_mainWindow->geometry();
    session.windowMaximised = m_mainWindow->isMaximized();
    session.focusedPanel = m_mainWindow->panelStrip()->focusedIndex();

    for (const ui::FilePanel *panel : m_mainWindow->panelStrip()->panels()) {
        session.panels.append(SessionPanel{.path = panel->path(),
                                           .cursorName = panel->cursorName(),
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

    // A --dbus-service process has no panel until its first request, so there
    // is no focused one to navigate.
    if (strip->panels().isEmpty()) {
        useFocusedPanel = false;
    }

    // One panel per folder. For `pf a b c` that is one per path, as §10.2 has
    // always said; "Show in folder" groups the items it reveals by folder.
    const QList<FolderRequest> requests =
        InstanceMessage::folderRequests(usable, message.selectItems);

    for (qsizetype i = 0; i < requests.size(); ++i) {
        const FolderRequest &request = requests.at(i);

        // §10.2: "With multiple paths, the first follows the table above and
        // each subsequent one always opens a new panel."
        ui::FilePanel *panel = nullptr;
        if (i == 0 && useFocusedPanel) {
            panel = strip->focusedPanel();
            if (panel != nullptr) {
                // §10.2: "Navigating the focused panel pushes history, so
                // go_back returns to where the user was. It never discards
                // their selection silently." Revealing a file replaces the
                // selection with that file, which says so by being visible.
                if (panel->selectionCount() > 0) {
                    panel->clearSelection();
                    if (!message.selectItems) {
                        m_mainWindow->showStatusMessage(tr("Selection cleared"));
                    }
                }
                panel->navigateTo(request.directory);
            }
        } else {
            panel = strip->addPanel(request.directory);
            if (panel == nullptr) {
                // §10.2: "beyond that, extra paths are dropped with a footer
                // warning."
                m_mainWindow->showStatusMessage(
                    tr("At most %1 panels — %2 not opened")
                        .arg(QString::number(ui::PanelStrip::kMaxPanels),
                             counted(static_cast<int>(requests.size() - i), tr("path"),
                                     tr("paths"))));
                break;
            }
        }

        if (panel == nullptr || request.names.isEmpty()) {
            continue;
        }
        panel->setCursorName(request.names.constFirst());
        if (message.selectItems) {
            panel->selectNames(request.names);
        }
    }

    if (!missing.isEmpty()) {
        // §10.2: "If some of several paths are bad, open the good ones and
        // report the rest."
        m_mainWindow->showStatusMessage(
            tr("%1 does not exist").arg(QDir::toNativeSeparators(missing.constFirst())));
    }

    // A --dbus-service process asked only about files that do not exist has
    // nothing to show, so it stays hidden and its idle timer keeps running.
    if (strip->panels().isEmpty()) {
        return;
    }
    if (m_dbusIdleTimer != nullptr) {
        m_dbusIdleTimer->stop();
    }

    // §10.2: the window is raised when the request did not come from it.
    if (!windowFocused) {
        raiseWindow(message.activationToken);
    }
}

platform::FileManagerService *Application::fileManagerService()
{
    if (m_fileManagerService == nullptr) {
        m_fileManagerService = platform::FileManagerService::create();

        // Into the same routing a `pf <path>` request takes, so "Show in
        // folder" obeys §10.2's placement rules and there is one path from a
        // request to a panel rather than two.
        connect(m_fileManagerService.get(), &platform::FileManagerService::showRequested, this,
                [this](const QStringList &paths, bool selectItems, const QString &startupId) {
                    InstanceMessage message;
                    message.cwd = QDir::rootPath();
                    message.paths = paths;
                    message.selectItems = selectItems;
                    // The caller's startup id is its activation token on
                    // Wayland, and the X11 startup id on X11.
                    message.activationToken = startupId;
                    message.desktopStartupId = startupId;

                    if (m_forwardToRunningInstance) {
                        if (m_dbusIdleTimer != nullptr) {
                            m_dbusIdleTimer->start();
                        }
                        if (SingleInstance::sendToRunningInstance(
                                platform::singleInstanceSocketPath(), message)) {
                            return;
                        }
                        // It went away between our start and this call. Show it
                        // here instead; the request matters more than the window
                        // it lands in.
                        qCWarning(pfIpc) << "the running instance did not answer; showing here";
                        m_forwardToRunningInstance = false;
                    }
                    openRequest(message);
                });
    }
    return m_fileManagerService.get();
}

DefaultFileManagerOffer *Application::defaultFileManagerOffer()
{
    if (m_defaultOffer == nullptr) {
        m_defaultOffer =
            std::make_unique<DefaultFileManagerOffer>(m_mainWindow.get(), fileManagerService());
        m_defaultOffer->registerActions(m_registry.get());
        connect(m_defaultOffer.get(), &DefaultFileManagerOffer::statusMessage, m_mainWindow.get(),
                &ui::MainWindow::showStatusMessage);
        connect(m_defaultOffer.get(), &DefaultFileManagerOffer::madeDefault, this,
                [this](bool, const QString &message) {
                    // The bar reports its own outcome; the footer reports one
                    // that came from Settings.
                    const ui::DefaultFileManagerBar *bar = m_defaultOffer->bar();
                    if (bar == nullptr || bar->isHidden()) {
                        m_mainWindow->showStatusMessage(message);
                    }
                    if (m_settingsWindow != nullptr) {
                        m_settingsWindow->refreshDefaultFileManagerStatus();
                    }
                });
        updateDefaultFileManagerKeyHints();
    }
    return m_defaultOffer.get();
}

void Application::updateDefaultFileManagerKeyHints()
{
    if (m_defaultOffer == nullptr || m_keymap == nullptr) {
        return;
    }
    const auto hint = [this](const char *actionId) {
        const QList<input::Binding> bindings =
            m_keymap->bindingsFor(input::KeymapLayer::Global, QLatin1String(actionId));
        return bindings.isEmpty() ? QString() : input::bindingToString(bindings.constFirst());
    };
    m_defaultOffer->setKeyHints(hint(DefaultFileManagerOffer::kYesAction),
                                hint(DefaultFileManagerOffer::kNotNowAction),
                                hint(DefaultFileManagerOffer::kNeverAction));
}

void Application::startAsDbusService()
{
    platform::FileManagerService *service = fileManagerService();

    // Never offers anything — the bus started it, which it can only have done
    // because Panefile is already the default — but its actions are
    // registered like any other run's, so the help modal lists the same keys.
    defaultFileManagerOffer();

    // The bus started this process because somebody called. If the call never
    // comes — the caller gave up, or another file manager answered first —
    // there is no window for anyone to close, so nothing else would end it.
    m_dbusIdleTimer = new QTimer(this);
    m_dbusIdleTimer->setSingleShot(true);
    m_dbusIdleTimer->setInterval(kDbusServiceIdleMs);
    connect(m_dbusIdleTimer, &QTimer::timeout, this, [this] {
        if (m_mainWindow == nullptr || !m_mainWindow->isVisible()) {
            qCInfo(pfIpc) << "--dbus-service: no window to show, exiting";
            quit();
        }
    });
    m_dbusIdleTimer->start();

    connect(service, &platform::FileManagerService::nameUnavailable, this,
            [](const QString &reason) {
                qCWarning(pfIpc) << "--dbus-service: could not take the name:" << reason;
            });
    service->requestName();

    // No window paints until a call arrives, and the deferred work — the
    // socket's serving half above all — must not wait for one.
    m_firstPaintSeen = true;
    QTimer::singleShot(0, this, &Application::runNextStartupTask);
}

bool Application::notify(QObject *receiver, QEvent *event)
{
    // An open popup — the sort menu, a context menu — owns the keyboard until
    // it closes. Offering its keys to the dispatcher first would let the
    // arrows and Escape act on the panel behind it, leaving the menu
    // unnavigable and impossible to dismiss from the keyboard.
    if (event->type() == QEvent::KeyPress && m_dispatcher != nullptr &&
        activePopupWidget() == nullptr) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);

        // §6.2 step 2: while a text input has focus, bare printable keys belong
        // to the widget, so typing a filename cannot trigger a binding. So do
        // the text-editing chords — Ctrl+A, Ctrl+C, Ctrl+V and the rest — which
        // would otherwise select, copy or paste files from inside a text box.
        // Every other chord still reaches the keymap.
        //
        // A panel part-way through a typed name counts as a text input for
        // the keys that edit the name: Space and `.` continue it rather than
        // opening Quick Look or toggling hidden files, Backspace deletes from
        // it rather than going up a directory, and Escape abandons it.
        const auto *focus = focusWidget();
        const bool unmodified =
            !(keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        const auto *panelView = qobject_cast<const ui::PanelView *>(focus);
        // `?` is the exception: it opens the shortcut help even mid-name.
        const bool editsTypedName =
            panelView != nullptr && panelView->isTypingAhead() &&
            (keyEvent->key() == Qt::Key_Backspace || keyEvent->key() == Qt::Key_Escape ||
             (!keyEvent->text().isEmpty() && keyEvent->text().front().isPrint() &&
              keyEvent->text() != QLatin1String("?")));
        // A shortcut recorder is capturing: every key is its own, including the
        // ones that would otherwise open the palette or close the panel.
        const bool typing = (isTextInput(focus) && (unmodified || isTextEditingChord(keyEvent))) ||
                            (unmodified && editsTypedName) || capturesKeys(focus);

        // §7.6's per-renderer keys — Alt with `+`/`-`/`0`, `[`/`]`, `w`, `p` —
        // are offered to the renderer before the dispatcher. Movement keys are
        // never consumed there, which is what keeps "the arrow keys still move
        // the panel cursor" true while a preview is open.
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
    const auto task = m_startupTasks[m_nextStartupTask++];
    if (task) {
        task();
    }

    if (m_nextStartupTask < m_startupTasks.size()) {
        QTimer::singleShot(0, this, &Application::runNextStartupTask);
    }
}

} // namespace pf
