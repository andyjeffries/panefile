// Entry point.
//
// The ordering here is the startup critical path of §3.4 and is load-bearing.
// The rule is that the only work permitted before the first paint is the work
// required to draw one panel; anything else is queued with
// Application::postStartupTask() or made lazy. Adding to this function needs a
// measured justification, not a plausible one.

#include "app/Application.h"
#include "app/CommandLine.h"
#include "app/InstanceMessage.h"
#include "app/SingleInstance.h"
#include "config/Config.h"
#include "config/DefaultConfig.h"
#include "core/Logging.h"
#include "core/StartupTrace.h"
#include "core/Version.h"
#include "platform/Paths.h"

#include <QDir>
#include <QTextStream>

#include <cstdio>

namespace {

void writeLine(FILE *stream, const QString &text)
{
    QTextStream out(stream);
    out << text;
    if (!text.endsWith(QLatin1Char('\n'))) {
        out << '\n';
    }
}

/// Handles the arguments that print something and exit. None of them construct
/// a QApplication: connecting to the display server to answer --version is pure
/// waste, and on Wayland it is not cheap waste.
///
/// Returns the exit code, or -1 to continue with normal startup.
int handleEarlyExit(const pf::CommandLineOptions &options)
{
    using pf::CommandLineAction;

    // Selecting the text first, then writing it once, rather than a write per
    // branch: the branches differ only in which string they produce, and saying
    // so directly is both shorter and honest about what varies.
    QString output;
    switch (options.action) {
    case CommandLineAction::ShowHelp:
        output = pf::helpText();
        break;
    case CommandLineAction::ShowVersion:
        output = pf::versionText();
        break;
    case CommandLineAction::PrintConfigDir:
        output = pf::platform::configDir();
        break;
    case CommandLineAction::PrintDefaultConfig:
        output = QString::fromUtf8(pf::config::kDefaultConfigToml.data(),
                                   static_cast<qsizetype>(pf::config::kDefaultConfigToml.size()));
        break;
    case CommandLineAction::Error:
        writeLine(stderr, QStringLiteral("pf: ") + options.message);
        return options.exitCode;
    case CommandLineAction::Run:
    case CommandLineAction::Benchmark:
        return -1;
    }

    writeLine(stdout, output);
    return 0;
}

/// §10.3's client half: hand the request to a running instance, if there is one.
///
/// "Client side is on the startup critical path and comes first: attempt
/// connectToServer with a 0 ms timeout. On success, send the request, wait for
/// a short ack (50 ms cap), exit 0 — **without ever constructing a
/// MainWindow**. This is by far the fastest path through the program and should
/// complete in a couple of milliseconds."
///
/// It goes further than the spec requires and constructs no QApplication
/// either: QLocalSocket needs only a QCoreApplication's event machinery, and on
/// Wayland a QApplication means a connection to the compositor before we have
/// decided whether to draw anything.
///
/// Returns true when a running instance took the request and this process
/// should exit 0.
bool handOffToRunningInstance(const pf::CommandLineOptions &options)
{
    if (options.newInstance) {
        // §10.2: "--new-instance: separate process entirely; skips the socket."
        return false;
    }

    pf::InstanceMessage message;
    message.cwd = QDir::currentPath();
    message.paths = options.paths;
    message.placement = options.placement;

    // §10.4: "The client must forward it in the IPC message and then unset it
    // locally, since a token is single-use." Unset before the send, not after:
    // if the send fails and this process goes on to start its own window, it
    // must not then present a token it has already handed over.
    message.activationToken = QString::fromLocal8Bit(qgetenv("XDG_ACTIVATION_TOKEN"));
    message.desktopStartupId = QString::fromLocal8Bit(qgetenv("DESKTOP_STARTUP_ID"));
    if (!message.activationToken.isEmpty()) {
        qunsetenv("XDG_ACTIVATION_TOKEN");
    }

    return pf::SingleInstance::sendToRunningInstance(pf::platform::singleInstanceSocketPath(),
                                                     message);
}

} // namespace

int main(int argc, char **argv)
{
    pf::StartupTrace::mark(pf::StartupPhase::ProcessStart);

    // 1. Parse argv. Hand-rolled and Qt-object-free (§10.5), so that the
    //    arguments which never need a GUI can be answered before we pay for one.
    const pf::CommandLineOptions options = pf::parseCommandLine(argc, argv);
    pf::StartupTrace::setReportingEnabled(options.startupTrace);
    pf::StartupTrace::mark(pf::StartupPhase::ArgvParsed);

    if (const int earlyExit = handleEarlyExit(options); earlyExit >= 0) {
        return earlyExit;
    }

    if (options.verbose) {
        pf::enableVerboseLogging();
    }

    // 2. §10.3: try to hand off to a running instance before anything else.
    //    This is the fastest path through the program — no QApplication, no
    //    window, no config read — and the one taken every time somebody opens a
    //    folder from another application.
    //
    //    The configuration is not consulted here on purpose. Reading
    //    config.toml to find out whether single_instance is on would cost more
    //    than the hand-off itself; a user who has turned it off simply has no
    //    socket to connect to, and the attempt fails in microseconds.
    if (options.action == pf::CommandLineAction::Run && handOffToRunningInstance(options)) {
        pf::StartupTrace::mark(pf::StartupPhase::HandedOff);
        return 0;
    }

    // 3. Construct the application and set its metadata.
    pf::Application app(argc, argv);

    // 4. Build the window, start the scan, show.
    app.startUp(options);

    return pf::Application::exec();
}
