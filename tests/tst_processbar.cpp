#include "fs/JobEngine.h"
#include "fs/jobs/DeleteJob.h"
#include "ui/ProcessBar.h"

#include <QElapsedTimer>
#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;

/// When the progress bar shows itself, and how long it stays.
class TestProcessBar : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Work that is over before the delay elapses never puts the bar on screen.
    ///
    /// Copying three small files finished in a few milliseconds and the bar
    /// appeared anyway — a flash of progress for something that had already
    /// stopped being true, which draws the eye for no reason.
    void shortJobsNeverShowTheBar()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QStringList paths;
        for (int n = 0; n < 3; ++n) {
            const QString path = dir.filePath(QStringLiteral("small%1.txt").arg(n));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x");
            file.close();
            paths << path;
        }

        fs::JobEngine engine;
        ui::ProcessBar bar(&engine);
        QSignalSpy appeared(&bar, &ui::ProcessBar::shouldAppear);
        QSignalSpy finished(&engine, &fs::JobEngine::jobFinished);

        engine.submit(std::make_unique<fs::DeleteJob>(fs::DeleteJob::Mode::Permanent, paths));
        QVERIFY(finished.wait(5000));

        // Well past the delay, so a bar that was going to appear would have.
        QTest::qWait(ui::ProcessBar::kAppearDelayMs * 3);
        QCOMPARE(appeared.count(), 0);
    }

    /// And once it is over, the bar says so and goes away promptly.
    ///
    /// It used to sit at 100% for five seconds saying "Idle" — a full bar with
    /// no word for it reads as something still running, and five seconds is
    /// several times longer than the work most people do in a file manager.
    void aFinishedJobSaysDoneAndHidesPromptly()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("gone.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        fs::JobEngine engine;
        ui::ProcessBar bar(&engine);
        QSignalSpy idle(&bar, &ui::ProcessBar::becameIdle);

        QElapsedTimer elapsed;
        elapsed.start();
        engine.submit(
            std::make_unique<fs::DeleteJob>(fs::DeleteJob::Mode::Permanent, QStringList{path}));

        QVERIFY(idle.wait(5000));

        // The linger, not the old five seconds. Generous upper bound: the point
        // is that it is nearer one second than five.
        QVERIFY2(elapsed.elapsed() < 3000, qPrintable(QString::number(elapsed.elapsed())));

        const auto *summary = bar.findChild<QLabel *>(QStringLiteral("processSummary"));
        QVERIFY(summary != nullptr);
        QCOMPARE(summary->text(), QStringLiteral("Done"));
    }
};

QTEST_MAIN(TestProcessBar)
#include "tst_processbar.moc"
