// Command line parsing (§10.1, §10.5).
//
// The parser is a pure function of argv, which is what makes the routing rules
// of §10.2 testable without a running instance to route to. The cases that
// matter most are the ones where a mistake is silent: a path that looks like a
// flag, a file:// URI from the .desktop entry, and Qt's own platform arguments
// which must survive the unknown-option check.

#include "app/CommandLine.h"

#include <QTest>

using namespace pf;

class TestCommandLine : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void noArguments();
    void pathsArePreservedInOrder();
    void fileUriIsDecoded();
    void fileUriWithSpacesIsDecoded();
    void nonFileUriIsTreatedAsPath();
    void informationalFlags_data();
    void informationalFlags();
    void placementOverrides_data();
    void placementOverrides();
    void diagnosticFlags();
    void benchmarkRequiresArgument();
    void benchmarkTakesPath();
    void doubleDashEndsOptions();
    void unknownOptionIsAnError();
    void qtPlatformArgumentsAreIgnored();
    void firstInformationalFlagWins();
    void combinedFlagsAndPaths();
};

void TestCommandLine::noArguments()
{
    const auto options = parseCommandLine(QStringList{"pf"});

    QCOMPARE(options.action, CommandLineAction::Run);
    QVERIFY(options.paths.isEmpty());
    QCOMPARE(options.placement, PlacementOverride::None);
    QVERIFY(!options.newInstance);
    QVERIFY(!options.startupTrace);
    QVERIFY(!options.quitAfterPaint);
    QVERIFY(!options.verbose);
}

void TestCommandLine::pathsArePreservedInOrder()
{
    // §10.2: the first path follows the focus rules and each subsequent one
    // opens a new panel, so the order is meaningful and must not be sorted or
    // deduplicated here.
    const auto options = parseCommandLine(QStringList{"pf", "/tmp", "~/Downloads", "relative/dir"});

    QCOMPARE(options.action, CommandLineAction::Run);
    QCOMPARE(options.paths, (QStringList{"/tmp", "~/Downloads", "relative/dir"}));
}

void TestCommandLine::fileUriIsDecoded()
{
    // %U in the .desktop entry hands us URIs, not paths (§10.1).
    const auto options = parseCommandLine(QStringList{"pf", "file:///home/andy/Developer"});

    QCOMPARE(options.paths, QStringList{"/home/andy/Developer"});
}

void TestCommandLine::fileUriWithSpacesIsDecoded()
{
    const auto options = parseCommandLine(QStringList{"pf", "file:///tmp/My%20Documents/a%23b"});

    QCOMPARE(options.paths, QStringList{"/tmp/My Documents/a#b"});
}

void TestCommandLine::nonFileUriIsTreatedAsPath()
{
    // A local file may legitimately be named something that looks like a URI.
    // Treating it as a path is recoverable; rejecting it is not.
    const auto options = parseCommandLine(QStringList{"pf", "https:notaurl"});

    QCOMPARE(options.action, CommandLineAction::Run);
    QCOMPARE(options.paths, QStringList{"https:notaurl"});
}

void TestCommandLine::informationalFlags_data()
{
    QTest::addColumn<QString>("flag");
    QTest::addColumn<CommandLineAction>("expected");

    QTest::newRow("-h") << "-h" << CommandLineAction::ShowHelp;
    QTest::newRow("--help") << "--help" << CommandLineAction::ShowHelp;
    QTest::newRow("-v") << "-v" << CommandLineAction::ShowVersion;
    QTest::newRow("--version") << "--version" << CommandLineAction::ShowVersion;
    QTest::newRow("--config-dir") << "--config-dir" << CommandLineAction::PrintConfigDir;
    QTest::newRow("--print-default-config")
        << "--print-default-config" << CommandLineAction::PrintDefaultConfig;
}

void TestCommandLine::informationalFlags()
{
    QFETCH(QString, flag);
    QFETCH(CommandLineAction, expected);

    const auto options = parseCommandLine(QStringList{"pf", flag});
    QCOMPARE(options.action, expected);
}

void TestCommandLine::placementOverrides_data()
{
    QTest::addColumn<QString>("flag");
    QTest::addColumn<PlacementOverride>("expected");

    QTest::newRow("--here") << "--here" << PlacementOverride::Here;
    QTest::newRow("--panel") << "--panel" << PlacementOverride::NewPanel;
    QTest::newRow("--new-window") << "--new-window" << PlacementOverride::NewWindow;
}

void TestCommandLine::placementOverrides()
{
    QFETCH(QString, flag);
    QFETCH(PlacementOverride, expected);

    const auto options = parseCommandLine(QStringList{"pf", flag, "/tmp"});

    QCOMPARE(options.action, CommandLineAction::Run);
    QCOMPARE(options.placement, expected);
    QCOMPARE(options.paths, QStringList{"/tmp"});
}

void TestCommandLine::diagnosticFlags()
{
    const auto options = parseCommandLine(
        QStringList{"pf", "--startup-trace", "--quit-after-paint", "--verbose", "--new-instance"});

    QVERIFY(options.startupTrace);
    QVERIFY(options.quitAfterPaint);
    QVERIFY(options.verbose);
    QVERIFY(options.newInstance);
    QCOMPARE(options.action, CommandLineAction::Run);
}

void TestCommandLine::benchmarkRequiresArgument()
{
    const auto options = parseCommandLine(QStringList{"pf", "--benchmark"});

    QCOMPARE(options.action, CommandLineAction::Error);
    QCOMPARE(options.exitCode, 2);
    QVERIFY(options.message.contains("--benchmark"));
}

void TestCommandLine::benchmarkTakesPath()
{
    const auto options = parseCommandLine(QStringList{"pf", "--benchmark", "/usr/share"});

    QCOMPARE(options.action, CommandLineAction::Benchmark);
    QCOMPARE(options.benchmarkPath, QStringLiteral("/usr/share"));
}

void TestCommandLine::doubleDashEndsOptions()
{
    // Files really do get named "--help". Everything after "--" is a path.
    const auto options = parseCommandLine(QStringList{"pf", "--", "--help", "-v"});

    QCOMPARE(options.action, CommandLineAction::Run);
    QCOMPARE(options.paths, (QStringList{"--help", "-v"}));
}

void TestCommandLine::unknownOptionIsAnError()
{
    // Silently treating an unknown flag as a path would mean `pf --hlep` opened
    // a panel showing a nonexistent directory instead of saying what was wrong.
    const auto options = parseCommandLine(QStringList{"pf", "--nonsense"});

    QCOMPARE(options.action, CommandLineAction::Error);
    QCOMPARE(options.exitCode, 2);
    QVERIFY(options.message.contains("--nonsense"));
}

void TestCommandLine::qtPlatformArgumentsAreIgnored()
{
    // QApplication consumes these; the test suite itself is launched with
    // -platform offscreen, so getting this wrong breaks every GUI test.
    const auto options =
        parseCommandLine(QStringList{"pf", "-platform", "offscreen", "-style", "fusion", "/tmp"});

    QCOMPARE(options.action, CommandLineAction::Run);
    QCOMPARE(options.paths, QStringList{"/tmp"});
}

void TestCommandLine::firstInformationalFlagWins()
{
    const auto options = parseCommandLine(QStringList{"pf", "--version", "--help"});

    QCOMPARE(options.action, CommandLineAction::ShowVersion);
}

void TestCommandLine::combinedFlagsAndPaths()
{
    const auto options =
        parseCommandLine(QStringList{"pf", "/a", "--panel", "/b", "--verbose", "/c"});

    QCOMPARE(options.action, CommandLineAction::Run);
    QCOMPARE(options.placement, PlacementOverride::NewPanel);
    QVERIFY(options.verbose);
    QCOMPARE(options.paths, (QStringList{"/a", "/b", "/c"}));
}

QTEST_APPLESS_MAIN(TestCommandLine)
#include "tst_commandline.moc"
