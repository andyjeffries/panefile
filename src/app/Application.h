#pragma once

#include <QApplication>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace pf {

struct CommandLineOptions;

namespace ui {
class MainWindow;
}

/// The QApplication subclass that owns the window and the deferred-startup
/// queue.
///
/// §3.4 divides startup work into three classes: on the critical path, deferred
/// to after the first paint, and lazy. This class is where the second class
/// lives — postStartupTask() queues work that runs one item per event loop turn
/// once the window is up, so no single slow item can stall input.
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

    /// Queues work to run after the first paint, one item per event loop turn.
    /// Safe to call before the window exists; the queue drains once it does.
    void postStartupTask(std::function<void()> task);

private Q_SLOTS:
    void onFirstPaint();
    void runNextStartupTask();

private:
    void scheduleStartupTasks();

    std::unique_ptr<ui::MainWindow> m_mainWindow{};
    std::vector<std::function<void()>> m_startupTasks;
    std::size_t m_nextStartupTask = 0;
    bool m_quitAfterPaint = false;
    bool m_firstPaintSeen = false;
};

} // namespace pf
