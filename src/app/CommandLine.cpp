#include "app/CommandLine.h"

#include "core/Version.h"

#include <QUrl>

#include <cstring>

namespace pf {
namespace {

/// Decodes a `file://` URI to a local path so that `%U` in the .desktop entry
/// works (§10.1). Anything that is not a file URI is passed through unchanged —
/// a local file may legitimately be named `https:weird`, and rejecting it here
/// would be worse than treating it as the relative path it is.
QString normalisePathArgument(const QString &argument)
{
    if (!argument.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) {
        return argument;
    }
    const QUrl url(argument);
    const QString local = url.toLocalFile();
    return local.isEmpty() ? argument : local;
}

CommandLineOptions makeError(const QString &message, int exitCode = 2)
{
    CommandLineOptions options;
    options.action = CommandLineAction::Error;
    options.message = message;
    options.exitCode = exitCode;
    return options;
}

} // namespace

CommandLineOptions parseCommandLine(int argc, const char *const *argv)
{
    CommandLineOptions options;
    bool optionsEnded = false;

    for (int i = 1; i < argc; ++i) {
        const char *raw = argv[i];
        if (raw == nullptr) {
            continue;
        }

        // Everything after a bare "--" is a path, even if it looks like a flag.
        // Files really are named "--help" sometimes.
        if (!optionsEnded && std::strcmp(raw, "--") == 0) {
            optionsEnded = true;
            continue;
        }

        const bool looksLikeFlag = !optionsEnded && raw[0] == '-' && raw[1] != '\0';
        if (!looksLikeFlag) {
            options.paths << normalisePathArgument(QString::fromLocal8Bit(raw));
            continue;
        }

        const auto is = [raw](const char *name) { return std::strcmp(raw, name) == 0; };

        if (is("-h") || is("--help")) {
            options.action = CommandLineAction::ShowHelp;
            return options;
        }
        if (is("-v") || is("--version")) {
            options.action = CommandLineAction::ShowVersion;
            return options;
        }
        if (is("--config-dir")) {
            options.action = CommandLineAction::PrintConfigDir;
            return options;
        }
        if (is("--print-default-config")) {
            options.action = CommandLineAction::PrintDefaultConfig;
            return options;
        }
        if (is("--benchmark")) {
            if (i + 1 >= argc || argv[i + 1] == nullptr) {
                return makeError(QStringLiteral("--benchmark requires a directory argument"));
            }
            options.action = CommandLineAction::Benchmark;
            options.benchmarkPath = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        if (is("--here")) {
            options.placement = PlacementOverride::Here;
            continue;
        }
        if (is("--panel")) {
            options.placement = PlacementOverride::NewPanel;
            continue;
        }
        if (is("--new-window")) {
            options.placement = PlacementOverride::NewWindow;
            continue;
        }
        if (is("--new-instance")) {
            options.newInstance = true;
            continue;
        }
        if (is("--startup-trace")) {
            options.startupTrace = true;
            continue;
        }
        if (is("--quit-after-paint")) {
            options.quitAfterPaint = true;
            continue;
        }
        if (is("--verbose")) {
            options.verbose = true;
            continue;
        }

        // Qt's own platform arguments (-platform, -style, …) are consumed by
        // QApplication, not by us, so they must survive an unknown-flag check.
        // Everything else is a user error and worth reporting rather than
        // silently treating as a path.
        static constexpr const char *kQtOptionsWithValue[] = {
            "-platform", "-platformpluginpath", "-platformtheme", "-plugin",
            "-style",    "-stylesheet",         "-session",       "-display",
            "-geometry", "-widgetcount",        "-graphicssystem"};
        bool consumed = false;
        for (const char *qtOption : kQtOptionsWithValue) {
            if (is(qtOption)) {
                if (i + 1 < argc) {
                    ++i;
                }
                consumed = true;
                break;
            }
        }
        if (consumed || is("-reverse") || is("-qmljsdebugger")) {
            continue;
        }

        return makeError(QStringLiteral("unknown option '%1'\nTry 'pf --help'.")
                             .arg(QString::fromLocal8Bit(raw)));
    }

    return options;
}

CommandLineOptions parseCommandLine(const QStringList &arguments)
{
    QList<QByteArray> storage;
    storage.reserve(arguments.size());
    std::vector<const char *> argv;
    argv.reserve(arguments.size() + 1);

    for (const QString &argument : arguments) {
        storage.append(argument.toLocal8Bit());
    }
    for (const QByteArray &item : storage) {
        argv.push_back(item.constData());
    }

    return parseCommandLine(static_cast<int>(argv.size()), argv.data());
}

QString versionText()
{
    return QStringLiteral(PF_APPLICATION_DISPLAY " " PF_VERSION);
}

QString helpText()
{
    return QStringLiteral(
        "Usage: pf [OPTIONS] [PATH...]\n"
        "\n" PF_APPLICATION_DISPLAY " — a keyboard-driven, multi-panel file manager.\n"
        "\n"
        "Paths may be files or directories, absolute or relative, or file:// URIs.\n"
        "Relative paths resolve against the calling shell's working directory.\n"
        "\n"
        "Placement (default: the focused panel if the window is focused, a new\n"
        "panel otherwise):\n"
        "      --here            Use the focused panel even if the window is not focused\n"
        "      --panel           Always open a new panel\n"
        "      --new-window      Open a new window in the existing process\n"
        "      --new-instance    Start a separate process, ignoring any running instance\n"
        "\n"
        "Diagnostics:\n"
        "      --startup-trace   Print startup phase timings to stderr\n"
        "      --quit-after-paint\n"
        "                        Exit once the first frame is painted (for benchmarks)\n"
        "      --benchmark DIR   Scan DIR, print timings, and exit\n"
        "      --verbose         Enable panefile.* debug logging\n"
        "\n"
        "Information:\n"
        "      --config-dir      Print the configuration directory and exit\n"
        "      --print-default-config\n"
        "                        Print the default config.toml and exit\n"
        "  -h, --help            Show this help and exit\n"
        "  -v, --version         Show the version and exit\n"
        "\n"
        "Home page: " PF_HOMEPAGE_URL "\n");
}

} // namespace pf
