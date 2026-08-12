#include "app/Application.h"

#include "app/CommandLine.h"
#include "core/Logging.h"
#include "core/StartupTrace.h"
#include "core/Version.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>

namespace pf {

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
    // §10.1: paths are resolved against the *client's* working directory. In a
    // freshly started process that is simply our own, but stating it here keeps
    // the rule in one place for when M10 adds the forwarding case, where the
    // running instance's cwd is the wrong answer.
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

void Application::startUp(const CommandLineOptions &options)
{
    m_quitAfterPaint = options.quitAfterPaint;

    m_mainWindow = std::make_unique<ui::MainWindow>();
    StartupTrace::mark(StartupPhase::WindowConstructed);

    connect(m_mainWindow.get(), &ui::MainWindow::firstPaintCompleted, this,
            &Application::onFirstPaint);

    // §3.4 step 6: start the scan before show(). It runs on a worker thread, so
    // dispatching it first means the enumeration overlaps with window
    // realisation instead of starting after it.
    m_mainWindow->activePanel()->navigateTo(initialPath(options));
    StartupTrace::mark(StartupPhase::ScanStarted);

    if (!options.paths.isEmpty()) {
        const QFileInfo info(QDir::current().absoluteFilePath(options.paths.constFirst()));
        if (info.exists() && !info.isDir()) {
            m_mainWindow->activePanel()->setCursorName(info.fileName());
        }
    }

    m_mainWindow->show();
    StartupTrace::mark(StartupPhase::Shown);
}

void Application::postStartupTask(std::function<void()> task)
{
    m_startupTasks.push_back(std::move(task));
    if (m_firstPaintSeen) {
        scheduleStartupTasks();
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
        // The paint has completed and been measured; leaving via the event loop
        // keeps the shutdown path identical to a normal quit.
        quit();
        return;
    }

    scheduleStartupTasks();
}

void Application::scheduleStartupTasks()
{
    if (m_nextStartupTask >= m_startupTasks.size()) {
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
    // than the user's first keypress (§3.4).
    auto task = m_startupTasks[m_nextStartupTask++];
    if (task) {
        task();
    }

    scheduleStartupTasks();
}

} // namespace pf
