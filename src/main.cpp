// Entry point.
//
// The ordering here is the startup critical path of §3.4 and is load-bearing.
// The rule is that the only work permitted before the first paint is the work
// required to draw one panel; anything else is queued with
// Application::postStartupTask() or made lazy. Adding to this function needs a
// measured justification, not a plausible one.

#include "app/Application.h"
#include "app/CommandLine.h"
#include "config/DefaultConfig.h"
#include "core/Logging.h"
#include "core/StartupTrace.h"
#include "core/Version.h"
#include "platform/Paths.h"

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

    // 2. Construct the application and set its metadata.
    pf::Application app(argc, argv);

    // 3. Build the window, start the scan, show.
    app.startUp(options);

    return pf::Application::exec();
}
