#pragma once

#include <QString>
#include <QStringList>

namespace pf {

/// What the process should do once argv has been read.
enum class CommandLineAction {
    Run, ///< normal startup, or hand off to a running instance
    ShowHelp,
    ShowVersion,
    PrintConfigDir,
    PrintDefaultConfig,
    Benchmark, ///< --benchmark <dir>: scan and print timings, then exit
    Error      ///< `message` explains, exit with `exitCode`
};

/// Where a path argument should open, overriding the §10.2 defaults.
enum class PlacementOverride {
    None,     ///< follow the focused/unfocused rules in §10.2
    Here,     ///< --here: use the focused panel regardless of window focus
    NewPanel, ///< --panel: always open a new panel
    NewWindow ///< --new-window: new window in the existing process
};

/// The result of parsing argv.
///
/// §10.5 puts argument parsing on the startup critical path, so this is a
/// hand-rolled parse over `char*` with no QCommandLineParser; that class is
/// constructed only to render --help and --version, where a few hundred
/// microseconds are irrelevant.
///
/// Parsing is a pure function of argv and the client's working directory, which
/// is what makes the §10.2 routing rules testable without a running instance.
struct CommandLineOptions {
    CommandLineAction action = CommandLineAction::Run;

    /// Path arguments in the order given, still unresolved: `file://` URIs are
    /// decoded but relative paths are *not* made absolute here, because §10.2
    /// resolves them against the client's cwd, which the parser does not know.
    QStringList paths;

    PlacementOverride placement = PlacementOverride::None;

    /// --new-instance: skip the single-instance socket entirely (§10.2).
    bool newInstance = false;

    bool startupTrace = false;

    /// --quit-after-paint: exit as soon as the first paint completes. Used by
    /// the hyperfine startup benchmark in CI (§3.4).
    bool quitAfterPaint = false;

    bool verbose = false;

    /// Directory for --benchmark.
    QString benchmarkPath;

    /// Populated when action is Error, or when a recoverable problem should be
    /// reported without stopping startup.
    QString message;
    int exitCode = 0;
};

/// Parses argv[1..argc-1]. argv[0] is ignored.
CommandLineOptions parseCommandLine(int argc, const char *const *argv);

/// Convenience overload for tests.
CommandLineOptions parseCommandLine(const QStringList &arguments);

/// Text written for --help.
QString helpText();

/// Single line written for --version.
QString versionText();

} // namespace pf
