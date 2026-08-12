// Startup instrumentation (§3.4).
//
// The trace is what makes the startup budget of §11 an acceptance criterion
// rather than an aspiration, so its own correctness matters: a phase recorded
// twice must keep the first timestamp (paintEvent fires repeatedly), and an
// unreached phase must be distinguishable from one that took no time.

#include "core/StartupTrace.h"

#include <QTest>

using namespace pf;

class TestStartupTrace : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void unreachedPhasesReportNegative();
    void markingRecordsMonotonicTimestamps();
    void remarkingKeepsTheFirstTimestamp();
    void reportListsOnlyReachedPhases();
    void reportingIsOffByDefault();
    void everyPhaseHasAName();
};

void TestStartupTrace::unreachedPhasesReportNegative()
{
    // ScanFirstBatch is never marked by this test, and -1 must mean "did not
    // happen" rather than "happened at time zero".
    QCOMPARE(StartupTrace::nanosSinceStart(StartupPhase::ScanFirstBatch), -1);
    QVERIFY(!StartupTrace::hasReached(StartupPhase::ScanFirstBatch));
}

void TestStartupTrace::markingRecordsMonotonicTimestamps()
{
    StartupTrace::mark(StartupPhase::ProcessStart);
    StartupTrace::mark(StartupPhase::ArgvParsed);
    QTest::qSleep(2);
    StartupTrace::mark(StartupPhase::WindowConstructed);

    const qint64 argv = StartupTrace::nanosSinceStart(StartupPhase::ArgvParsed);
    const qint64 window = StartupTrace::nanosSinceStart(StartupPhase::WindowConstructed);

    QVERIFY(argv >= 0);
    QVERIFY(window > argv);
}

void TestStartupTrace::remarkingKeepsTheFirstTimestamp()
{
    // MainWindow::paintEvent marks FirstPaint on every paint; only the first
    // one is the measurement.
    StartupTrace::mark(StartupPhase::FirstPaint);
    const qint64 first = StartupTrace::nanosSinceStart(StartupPhase::FirstPaint);

    QTest::qSleep(2);
    StartupTrace::mark(StartupPhase::FirstPaint);

    QCOMPARE(StartupTrace::nanosSinceStart(StartupPhase::FirstPaint), first);
}

void TestStartupTrace::reportListsOnlyReachedPhases()
{
    const QString report = StartupTrace::formatReport();

    QVERIFY(report.contains(QLatin1String("argv")));
    QVERIFY(report.contains(QLatin1String("first-paint")));
    QVERIFY(!report.contains(QLatin1String("scan-first-batch")));
}

void TestStartupTrace::reportingIsOffByDefault()
{
    // dump() must stay silent unless --startup-trace asked for it; stray output
    // on stderr would corrupt the machine-readable modes of §10.5.
    QVERIFY(!StartupTrace::isReportingEnabled());

    StartupTrace::setReportingEnabled(true);
    QVERIFY(StartupTrace::isReportingEnabled());
    StartupTrace::setReportingEnabled(false);
}

void TestStartupTrace::everyPhaseHasAName()
{
    for (int i = 0; i < static_cast<int>(StartupPhase::Count); ++i) {
        const char *name = StartupTrace::phaseName(static_cast<StartupPhase>(i));
        QVERIFY(name != nullptr);
        QVERIFY2(qstrcmp(name, "unknown") != 0,
                 qPrintable(QStringLiteral("phase %1 has no name").arg(i)));
    }
}

QTEST_APPLESS_MAIN(TestStartupTrace)
#include "tst_startuptrace.moc"
