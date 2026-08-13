#pragma once

#include "config/Config.h"

namespace pf::config {
class ConfigWatcher;
}

namespace pf::fs {
class JobEngine;
class UndoStack;
} // namespace pf::fs

#include <QApplication>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace pf::input {
class ActionRegistry;
class Keymap;
} // namespace pf::input

namespace pf::ui {
class FilePanel;
class HelpModal;
class MainWindow;
class ProcessBar;
} // namespace pf::ui

namespace pf {

struct CommandLineOptions;
class FileOperations;
class KeyDispatcher;
class PanelController;
class QuickLookController;
class SearchController;
class SingleInstance;
struct InstanceMessage;

/// The QApplication subclass that owns the window and the deferred-startup
/// queue.
///
/// §3.4 divides startup work into three classes: on the critical path, deferred
/// to after the first paint, and lazy. This class is where the second class
/// lives — postStartupTask() queues work that runs one item per event loop turn
/// once the window is up, so no single slow item can stall input.
///
/// It is also the composition root: the action registry, keymap, dispatcher and
/// panel controller are wired together here, because that wiring is the one
/// place that needs to know about every layer.
class Application : public QApplication
{
    Q_OBJECT

public:
    Application(int &argc, char **argv);
    ~Application() override;

    /// Builds the window and starts the initial scan. Everything here is on the
    /// critical path and is expected to stay short.
    void startUp(const CommandLineOptions &options);

    ui::MainWindow *mainWindow() const;

    /// The directory the initial panel opens at, from the path arguments or
    /// $HOME (§10.1, §10.2). Static and pure so the routing rules are testable.
    static QString initialPath(const CommandLineOptions &options);

    /// Queues work to run after the first paint, one item per event loop turn.
    /// Safe to call before the window exists; the queue drains once it does.
    void postStartupTask(std::function<void()> task);

    /// §10.2's routing. Public because main() hands the first request to it,
    /// and because §14 drives the rules directly.
    void openRequest(const InstanceMessage &message);

    /// §6.2: a single application-level filter is the entire dispatch path.
    /// QShortcut and QAction shortcuts are not used — they cap out at four
    /// elements, resolve ambiguity uncontrollably, and have no notion of mode.
    bool notify(QObject *receiver, QEvent *event) override;

private Q_SLOTS:
    void onFirstPaint();
    void runNextStartupTask();

private:
    void loadConfiguration();
    void loadHotkeys();
    void startWatchingConfig();
    void reloadConfiguration(const QStringList &changedFiles);
    void buildInputSystem();
    void registerGlobalActions();

    /// Applies `[panels]` and `[thumbnails]` to a newly created panel.
    void configurePanel(ui::FilePanel *panel) const;

    /// Puts the dispatcher on the layers §6.2's precedence rules call for.
    ///
    /// Nothing called this before, so the dispatcher sat on {Normal, Global}
    /// for the life of the process and the entire Selection layer was
    /// unreachable: `v` set the mode, the header said [SELECT], and not one of
    /// the bindings that mode exists for could fire.
    void updateActiveLayers();

    /// §10.4: raises and activates the window, using a forwarded activation
    /// token when one is available.
    void raiseWindow(const QString &activationToken);

    /// §8: restores the previous session, or opens one panel at the configured
    /// path when there is nothing to restore.
    void restoreSessionOrOpenInitialPanel(const CommandLineOptions &options);

    void saveSession() const;

    /// Makes SIGTERM and SIGINT quit cleanly, so the session survives a logout.
    ///
    /// A session manager ends a session with SIGTERM, and Qt's default
    /// disposition for it is to kill the process outright — which skips
    /// aboutToQuit and loses exactly the session the user is about to want
    /// back. The self-pipe below is the standard way to get a signal onto the
    /// event loop, because almost nothing is safe to do inside a handler.
    void installSignalHandling();
    ui::HelpModal *helpModal();
    ui::ProcessBar *processBar();

    config::Settings m_settings;
    QList<config::ConfigIssue> m_configIssues;

    std::unique_ptr<ui::MainWindow> m_mainWindow;
    std::unique_ptr<input::ActionRegistry> m_registry;
    std::unique_ptr<input::Keymap> m_keymap;
    std::unique_ptr<KeyDispatcher> m_dispatcher;
    std::unique_ptr<PanelController> m_panelController;
    std::unique_ptr<QuickLookController> m_quickLook;
    std::unique_ptr<SearchController> m_search;
    std::unique_ptr<SingleInstance> m_instance;
    std::unique_ptr<FileOperations> m_fileOperations;
    std::unique_ptr<config::ConfigWatcher> m_configWatcher;
    std::unique_ptr<fs::JobEngine> m_jobEngine;
    std::unique_ptr<fs::UndoStack> m_undoStack;

    /// §3.4: created when the first job starts, not at startup. A user who
    /// copies nothing never pays for it.
    ui::ProcessBar *m_processBar = nullptr;

    /// §3.4: modals are constructed on first invocation, then cached. The help
    /// modal builds a tree of every action, which is not work to do at startup
    /// for a user who never presses `?`. Owned by the window through the Qt
    /// parent-child relationship, so this is a raw observing pointer.
    ui::HelpModal *m_helpModal = nullptr;

    std::vector<std::function<void()>> m_startupTasks;
    std::size_t m_nextStartupTask = 0;
    bool m_quitAfterPaint = false;
    bool m_firstPaintSeen = false;
};

} // namespace pf
